import argparse
import json
import math
import os
import platform
import socket
import sys
import time

import cv2


PREVIEW_CHUNK_BYTES = 1150

HAND_WRIST = 0
HAND_INDEX_MCP = 5
HAND_MIDDLE_MCP = 9
HAND_RING_MCP = 13
HAND_PINKY_MCP = 17
PALM_INDICES = (
    HAND_WRIST,
    HAND_INDEX_MCP,
    HAND_MIDDLE_MCP,
    HAND_RING_MCP,
    HAND_PINKY_MCP,
)


def clamp01(value):
    return max(0.0, min(1.0, float(value)))


def parse_camera(value):
    text = str(value)
    return int(text) if text.isdigit() else text


def mirror_point(point):
    return (1.0 - point[0], point[1])


def point_to_px(point, width, height):
    return [
        int(round(clamp01(point[0]) * max(0, width - 1))),
        int(round(clamp01(point[1]) * max(0, height - 1))),
    ]


def rounded_point(point):
    return [round(float(point[0]), 6), round(float(point[1]), 6)]


def log_info(message):
    print(f"[INFO] {message}", flush=True)


def log_warn(message):
    print(f"[WARN] {message}", flush=True)


def log_error(message):
    print(f"[ERROR] {message}", flush=True)


def open_camera(source, width, height):
    parsed = parse_camera(source)
    attempts = [("DSHOW", cv2.CAP_DSHOW), ("DEFAULT", 0)]
    for name, backend in attempts:
        cap = cv2.VideoCapture(parsed, backend) if backend else cv2.VideoCapture(parsed)
        if not cap.isOpened():
            cap.release()
            continue
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        cap.set(cv2.CAP_PROP_FPS, 30)
        return cap, name
    return cv2.VideoCapture(), "none"


def create_tasks_vision():
    try:
        from mediapipe.tasks import python as mp_python
        from mediapipe.tasks.python import vision
    except Exception as error:
        log_warn(f"MediaPipe Tasks unavailable: {error}")
        return None, None
    return mp_python, vision


def create_hands_detector(mp, model_path):
    if hasattr(mp, "solutions") and hasattr(mp.solutions, "hands"):
        log_info("Hands: solutions API")
        return {
            "type": "solutions",
            "detector": mp.solutions.hands.Hands(
                static_image_mode=False,
                max_num_hands=2,
                model_complexity=1,
                min_detection_confidence=0.45,
                min_tracking_confidence=0.45,
            ),
        }

    log_warn("mp.solutions.hands not available. Switching to MediaPipe Tasks API.")
    mp_python, vision = create_tasks_vision()
    if mp_python is None or vision is None:
        return None
    if not model_path or not os.path.exists(model_path):
        log_error(f"Hands model not found: {model_path}")
        return None

    options = vision.HandLandmarkerOptions(
        base_options=mp_python.BaseOptions(model_asset_path=model_path),
        running_mode=vision.RunningMode.VIDEO,
        num_hands=2,
        min_hand_detection_confidence=0.45,
        min_hand_presence_confidence=0.45,
        min_tracking_confidence=0.45,
    )
    log_info(f"Hands: Tasks API model={model_path}")
    return {
        "type": "tasks",
        "mp": mp,
        "detector": vision.HandLandmarker.create_from_options(options),
    }


def close_detector(detector):
    if detector is not None and detector.get("detector") is not None:
        detector["detector"].close()


def classification_data(item):
    classifications = getattr(item, "classification", item)
    if not classifications:
        return "Hand", 0.0
    cls = classifications[0]
    label = getattr(cls, "label", getattr(cls, "category_name", "Hand"))
    score = float(getattr(cls, "score", 0.0))
    return label, clamp01(score)


def landmark_xy(landmark):
    return (clamp01(landmark.x), clamp01(landmark.y))


def palm_center(points):
    return (
        sum(points[index][0] for index in PALM_INDICES) / len(PALM_INDICES),
        sum(points[index][1] for index in PALM_INDICES) / len(PALM_INDICES),
    )


def hand_to_raw_dict(raw_index, points, label, score):
    mirrored_points = [mirror_point(point) for point in points]
    palm = palm_center(points)
    mirrored_palm = mirror_point(palm)
    wrist = points[HAND_WRIST]
    mirrored_wrist = mirror_point(wrist)

    return {
        "rawIndex": raw_index,
        "label": label,
        "score": round(score, 6),
        "landmarks01": [rounded_point(point) for point in points],
        "mirroredLandmarks01": [
            rounded_point(point) for point in mirrored_points
        ],
        "wrist01": rounded_point(wrist),
        "mirroredWrist01": rounded_point(mirrored_wrist),
        "palm01": rounded_point(palm),
        "mirroredPalm01": rounded_point(mirrored_palm),
    }


def detect_hands(detector, frame, timestamp_ms):
    if detector is None:
        return []

    height, width = frame.shape[:2]
    rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    if detector["type"] == "solutions":
        result = detector["detector"].process(rgb)
        hand_landmarks = result.multi_hand_landmarks or []
        handedness = result.multi_handedness or []
        landmark_groups = [landmarks.landmark for landmarks in hand_landmarks]
    else:
        mp_image = detector["mp"].Image(
            image_format=detector["mp"].ImageFormat.SRGB,
            data=rgb,
        )
        result = detector["detector"].detect_for_video(mp_image, timestamp_ms)
        landmark_groups = result.hand_landmarks or []
        handedness = result.handedness or []

    hands = []
    for index, landmarks in enumerate(landmark_groups[:2]):
        points = [landmark_xy(landmark) for landmark in landmarks]
        label = "Hand"
        score = 0.0
        if index < len(handedness):
            label, score = classification_data(handedness[index])
        hands.append(hand_to_raw_dict(index, points, label, score))
    return hands


def build_raw_packet(frame_index, timestamp_ms, dt, frame, hands, args):
    height, width = frame.shape[:2]
    packet = {
        "type": "HAND_RAW",
        "frame": frame_index,
        "timestampMs": timestamp_ms,
        "dt": round(dt, 6),
        "image": {
            "width": width,
            "height": height,
            "camera": str(args.camera),
            "mirrorPreview": args.mirror_preview,
            "mirrorControls": args.mirror_controls,
            "coordinateSpace": "MediaPipe normalized image coordinates",
        },
        "hands": hands,
    }
    return "HAND_RAW " + json.dumps(packet, separators=(",", ":"))


def draw_hands(frame, hands, mirror_preview):
    height, width = frame.shape[:2]
    for hand in hands:
        points = hand["mirroredLandmarks01"] if mirror_preview else hand["landmarks01"]
        color = (40, 180, 255) if hand["rawIndex"] == 0 else (80, 240, 120)
        for point in points:
            x, y = point_to_px(point, width, height)
            cv2.circle(frame, (x, y), 2, color, -1, cv2.LINE_AA)

        palm = hand["mirroredPalm01"] if mirror_preview else hand["palm01"]
        wrist = hand["mirroredWrist01"] if mirror_preview else hand["wrist01"]
        px, py = point_to_px(palm, width, height)
        wx, wy = point_to_px(wrist, width, height)
        cv2.circle(frame, (px, py), 8, color, 2, cv2.LINE_AA)
        cv2.circle(frame, (wx, wy), 5, (245, 245, 245), 1, cv2.LINE_AA)
        cv2.putText(
            frame,
            f"raw{hand['rawIndex']} {hand['label']} {hand['score']:.2f}",
            (max(8, min(width - 180, px + 10)), max(22, py - 10)),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.45,
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
        header = (
            f"SGCAM {frame_id} {chunk_index} {chunk_count} {len(data)}\n"
        ).encode("ascii")
        sock.sendto(header + data[start:end], target)
    state["last_time"] = now


def print_startup_log(args, cap, backend, hands_detector):
    actual_width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    actual_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    actual_fps = cap.get(cv2.CAP_PROP_FPS)
    try:
        import mediapipe as mp
        mediapipe_version = getattr(mp, "__version__", "unknown")
    except Exception:
        mediapipe_version = "unavailable"

    log_info(f"Python: {platform.python_version()}")
    log_info(f"MediaPipe: {mediapipe_version}")
    log_info(
        f"Camera: index={args.camera} backend={backend} "
        f"{actual_width}x{actual_height} fps={actual_fps:.2f}"
    )
    log_info(f"Hands model: {args.model}")
    log_info(
        f"Hands API: {hands_detector['type'] if hands_detector else 'disabled'}"
    )
    log_info("Mode: HAND_RAW only.")
    log_info(f"UDP: {args.udp_host}:{args.udp_port}")
    log_info(f"Preview UDP: {args.preview_host}:{args.preview_port}")
    log_info(
        f"mirror_preview={args.mirror_preview} "
        f"mirror_controls={args.mirror_controls}"
    )


def parse_args():
    parser = argparse.ArgumentParser(description="Shingiittai raw hand sender.")
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
    parser.add_argument("--sword1-hand", choices=("right", "left"), default="right")
    parser.add_argument("--sword2-hand", choices=("right", "left"), default="left")
    parser.add_argument("--send-json", action="store_true")
    parser.add_argument("--no-raw-json", dest="raw_json", action="store_false")
    parser.add_argument("--log-interval", type=float, default=1.0)
    parser.add_argument(
        "--no-mirror-controls", dest="mirror_controls", action="store_false"
    )
    parser.add_argument(
        "--no-mirror-preview", dest="mirror_preview", action="store_false"
    )
    parser.set_defaults(mirror_controls=True, mirror_preview=True, raw_json=True)
    args, _unknown = parser.parse_known_args()
    if args.pose_model:
        log_warn("--pose-model is accepted for compatibility but ignored in raw mode")
    return args


def main():
    args = parse_args()

    cap, camera_backend = open_camera(args.camera, args.width, args.height)
    if not cap.isOpened():
        log_error("Camera open failed.")
        log_error(
            f"index={args.camera} requested={args.width}x{args.height}@30 "
            "hint=try --camera 1 or close other camera apps"
        )
        return 1

    try:
        import mediapipe as mp
    except Exception as error:
        log_error(f"MediaPipe unavailable: {error}")
        cap.release()
        return 1

    hands_detector = create_hands_detector(mp, args.model)
    if hands_detector is None:
        log_error("Hands detector is unavailable.")
        cap.release()
        return 1

    print_startup_log(args, cap, camera_backend, hands_detector)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (args.udp_host, args.udp_port)
    preview_target = (args.preview_host, args.preview_port)
    preview_state = {"frame_id": 0, "last_time": 0.0}

    last_time = time.perf_counter()
    last_log_time = 0.0
    frame_index = 0
    log_info("Raw hand sender started")
    try:
        while True:
            ok, frame = cap.read()
            if not ok or frame is None:
                time.sleep(0.02)
                continue

            now = time.perf_counter()
            dt = max(0.0, now - last_time)
            last_time = now
            timestamp_ms = int(now * 1000)
            frame_index += 1

            hands = detect_hands(hands_detector, frame, timestamp_ms)
            if args.raw_json:
                raw_packet = build_raw_packet(
                    frame_index, timestamp_ms, dt, frame, hands, args
                )
                raw_payload = raw_packet.encode("utf-8")
                sock.sendto(raw_payload, target)

            display = cv2.flip(frame, 1) if args.mirror_preview else frame.copy()
            draw_hands(display, hands, args.mirror_preview)
            cv2.putText(
                display,
                "HAND RAW",
                (14, 28),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.72,
                (245, 245, 245),
                2,
                cv2.LINE_AA,
            )
            send_preview(sock, preview_target, display, preview_state, args, now)

            if now - last_log_time >= max(0.2, args.log_interval):
                log_info(f"[HAND_RAW] frame={frame_index} hands={len(hands)}")
                last_log_time = now

            if args.show_window:
                cv2.imshow("Shingiittai Hand Raw", display)
                if cv2.waitKey(1) & 0xFF == 27:
                    break
    finally:
        close_detector(hands_detector)
        cap.release()
        sock.close()
        if args.show_window:
            cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    sys.exit(main())
