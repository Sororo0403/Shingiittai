import argparse
import math
import socket
import sys
import time

import cv2

PREVIEW_CHUNK_BYTES = 1150


def clamp01(value):
    return max(0.0, min(1.0, float(value)))


def parse_camera(value):
    text = str(value)
    return int(text) if text.isdigit() else text


def to_pixel(point, width, height):
    return int(point[0] * width), int(point[1] * height)


def open_camera(source, width, height):
    cap = cv2.VideoCapture(parse_camera(source), cv2.CAP_DSHOW)
    if not cap.isOpened():
        cap = cv2.VideoCapture(parse_camera(source))
    if cap.isOpened():
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        cap.set(cv2.CAP_PROP_FPS, 30)
    return cap


def create_hands_detector():
    try:
        import mediapipe as mp
    except Exception as error:
        print(f"MediaPipe unavailable: {error}", flush=True)
        return None, None
    return mp, mp.solutions.hands.Hands(
        static_image_mode=False,
        max_num_hands=2,
        model_complexity=1,
        min_detection_confidence=0.45,
        min_tracking_confidence=0.45,
    )


def detect_hands(mp, hands, frame):
    if hands is None:
        return []
    rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    result = hands.process(rgb)
    if not result.multi_hand_landmarks:
        return []

    detected = []
    handedness = result.multi_handedness or []
    for index, landmarks in enumerate(result.multi_hand_landmarks):
        points = [(lm.x, lm.y) for lm in landmarks.landmark]
        palm_indices = (0, 5, 9, 13, 17)
        center_x = sum(points[i][0] for i in palm_indices) / len(palm_indices)
        center_y = sum(points[i][1] for i in palm_indices) / len(palm_indices)
        confidence = 0.85
        label = "Hand"
        if index < len(handedness) and handedness[index].classification:
            cls = handedness[index].classification[0]
            label = cls.label
            confidence = float(cls.score)
        detected.append(
            {
                "x": clamp01(center_x),
                "y": clamp01(center_y),
                "points": points,
                "confidence": clamp01(confidence),
                "label": label,
            }
        )
    detected.sort(key=lambda hand: hand["x"], reverse=True)
    return detected[:2]


def draw_hands(frame, hands):
    height, width = frame.shape[:2]
    colors = ((40, 180, 255), (80, 240, 120))
    for index, hand in enumerate(hands):
        color = colors[index % len(colors)]
        for point in hand["points"]:
            x, y = to_pixel(point, width, height)
            cv2.circle(frame, (x, y), 2, color, -1, cv2.LINE_AA)
        x, y = to_pixel((hand["x"], hand["y"]), width, height)
        cv2.circle(frame, (x, y), 8, color, 2, cv2.LINE_AA)
        cv2.putText(
            frame,
            f"SWORD{index + 1} {hand['confidence']:.2f}",
            (x + 10, y - 10),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.48,
            color,
            1,
            cv2.LINE_AA,
        )


def send_preview(sock, target, frame, state, args, now):
    interval = 1.0 / max(1.0, args.preview_fps)
    if now - state["last_time"] < interval:
        return
    preview = cv2.resize(
        frame,
        (max(1, args.preview_width), max(1, args.preview_height)),
        interpolation=cv2.INTER_AREA,
    )
    ok, encoded = cv2.imencode(
        ".jpg",
        preview,
        [cv2.IMWRITE_JPEG_QUALITY, max(1, min(95, args.preview_quality))],
    )
    if not ok:
        return
    data = encoded.tobytes()
    state["frame_id"] = (state["frame_id"] + 1) & 0xFFFFFFFF
    frame_id = state["frame_id"]
    chunk_count = max(1, math.ceil(len(data) / PREVIEW_CHUNK_BYTES))
    for chunk_index in range(chunk_count):
        start = chunk_index * PREVIEW_CHUNK_BYTES
        end = min(start + PREVIEW_CHUNK_BYTES, len(data))
        header = f"SGCAM {frame_id} {chunk_index} {chunk_count} {len(data)}\n".encode("ascii")
        sock.sendto(header + data[start:end], target)
    state["last_time"] = now


def build_player_packet(timestamp_ms, hand_states):
    values = [str(timestamp_ms)]
    for index in range(2):
        state = hand_states[index]
        values.extend(
            [
                f"{state['speed']:.6f}",
                f"{state['dir_x']:.6f}",
                f"{state['dir_y']:.6f}",
                f"{state['confidence']:.6f}",
            ]
        )
    values.append("0")
    return "PLAYER_INPUT " + " ".join(values)


def update_hand_states(detections, previous, dt):
    states = []
    for index in range(2):
        if index >= len(detections):
            states.append({"speed": 0.0, "dir_x": 1.0 if index == 0 else -1.0, "dir_y": 0.0, "confidence": 0.0})
            previous[index] = None
            continue

        hand = detections[index]
        x = hand["x"]
        y = hand["y"]
        prev = previous[index]
        if prev is None or dt <= 0.0:
            dx = 0.0
            dy = 0.0
        else:
            dx = x - prev[0]
            dy = y - prev[1]
        previous[index] = (x, y)

        length = math.sqrt(dx * dx + dy * dy)
        if length > 0.0001:
            dir_x = dx / length
            dir_y = dy / length
        else:
            dir_x = 1.0 if index == 0 else -1.0
            dir_y = 0.0
        raw_speed = length / max(dt, 0.001) * 900.0
        confidence = hand["confidence"] if raw_speed >= 12.0 else 0.0
        states.append(
            {
                "speed": raw_speed if confidence > 0.0 else 0.0,
                "dir_x": dir_x,
                "dir_y": dir_y,
                "confidence": confidence,
            }
        )
    return states


def main():
    parser = argparse.ArgumentParser(description="Sword-only camera UDP sender.")
    parser.add_argument("--camera", default="0")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--udp-host", default="127.0.0.1")
    parser.add_argument("--udp-port", type=int, default=5005)
    parser.add_argument("--preview-host", default="127.0.0.1")
    parser.add_argument("--preview-port", type=int, default=5006)
    parser.add_argument("--preview-width", type=int, default=320)
    parser.add_argument("--preview-height", type=int, default=180)
    parser.add_argument("--preview-fps", type=float, default=12.0)
    parser.add_argument("--preview-quality", type=int, default=62)
    parser.add_argument("--show-window", action="store_true")
    parser.add_argument("--model", default="")
    parser.add_argument("--pose-model", default="")
    parser.add_argument("--start-paused", action="store_true")
    parser.add_argument("--status-host", default="127.0.0.1")
    parser.add_argument("--status-port", type=int, default=5008)
    args, _unknown = parser.parse_known_args()

    cap = open_camera(args.camera, args.width, args.height)
    if not cap.isOpened():
        print(f"Camera failed: could not open {args.camera}", flush=True)
        return 1

    mp, hands = create_hands_detector()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (args.udp_host, args.udp_port)
    preview_target = (args.preview_host, args.preview_port)
    preview_state = {"frame_id": 0, "last_time": 0.0}
    previous = [None, None]
    last_time = time.perf_counter()

    print("Sword-only camera sender started", flush=True)
    try:
        while True:
            ok, frame = cap.read()
            if not ok or frame is None:
                time.sleep(0.02)
                continue
            frame = cv2.flip(frame, 1)
            now = time.perf_counter()
            dt = now - last_time
            last_time = now

            detections = detect_hands(mp, hands, frame)
            hand_states = update_hand_states(detections, previous, dt)
            draw_hands(frame, detections)
            cv2.putText(
                frame,
                "SWORD CAMERA",
                (14, 28),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.72,
                (245, 245, 245),
                2,
                cv2.LINE_AA,
            )

            packet = build_player_packet(int(time.time() * 1000), hand_states)
            sock.sendto(packet.encode("ascii"), target)
            send_preview(sock, preview_target, frame, preview_state, args, now)

            if args.show_window:
                cv2.imshow("Shingiittai Sword Camera", frame)
                if cv2.waitKey(1) & 0xFF == 27:
                    break
    finally:
        if hands is not None:
            hands.close()
        cap.release()
        sock.close()
        if args.show_window:
            cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    sys.exit(main())
