import argparse
import json
import math
import os
import platform
import socket
import sys
import tempfile
import time

import cv2


PREVIEW_CHUNK_BYTES = 1150

HAND_WRIST = 0
HAND_INDEX_MCP = 5
HAND_INDEX_PIP = 6
HAND_MIDDLE_MCP = 9
HAND_MIDDLE_PIP = 10
HAND_RING_MCP = 13
HAND_RING_PIP = 14
HAND_PINKY_MCP = 17
HAND_PINKY_PIP = 18
PALM_INDICES = (
    HAND_WRIST,
    HAND_INDEX_MCP,
    HAND_MIDDLE_MCP,
    HAND_RING_MCP,
    HAND_PINKY_MCP,
)
MCP_INDICES = (HAND_INDEX_MCP, HAND_MIDDLE_MCP, HAND_RING_MCP, HAND_PINKY_MCP)
PIP_INDICES = (HAND_INDEX_PIP, HAND_MIDDLE_PIP, HAND_RING_PIP, HAND_PINKY_PIP)
POSE_LEFT_SHOULDER = 11
POSE_RIGHT_SHOULDER = 12
POSE_LEFT_HIP = 23
POSE_RIGHT_HIP = 24
BODY_LANDMARK_INDICES = (
    POSE_LEFT_SHOULDER,
    POSE_RIGHT_SHOULDER,
    POSE_LEFT_HIP,
    POSE_RIGHT_HIP,
)
NOMINAL_SHOULDER_WIDTH = 0.32
NOMINAL_TORSO_HEIGHT = 0.34


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
                min_detection_confidence=0.35,
                min_tracking_confidence=0.35,
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
        min_hand_detection_confidence=0.35,
        min_hand_presence_confidence=0.35,
        min_tracking_confidence=0.35,
    )
    log_info(f"Hands: Tasks API model={model_path}")
    return {
        "type": "tasks",
        "mp": mp,
        "detector": vision.HandLandmarker.create_from_options(options),
    }


def create_pose_detector(mp):
    if hasattr(mp, "solutions") and hasattr(mp.solutions, "pose"):
        log_info("Pose: solutions API")
        return {
            "type": "solutions",
            "detector": mp.solutions.pose.Pose(
                static_image_mode=False,
                model_complexity=1,
                smooth_landmarks=True,
                min_detection_confidence=0.45,
                min_tracking_confidence=0.45,
            ),
        }

    log_warn("Pose detector unavailable. Body-angle correction disabled.")
    return None


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


def weighted_point(items):
    total = sum(weight for _point, weight in items)
    if total <= 0.0:
        return (0.5, 0.5)
    return (
        sum(point[0] * weight for point, weight in items) / total,
        sum(point[1] * weight for point, weight in items) / total,
    )


def grip_center(points):
    # Fingertips become unstable when the hand is closed, so use knuckle and
    # palm-base landmarks that remain visible in both open and fist poses.
    mcp = average_point([points[index] for index in MCP_INDICES])
    pip = average_point([points[index] for index in PIP_INDICES])
    wrist = points[HAND_WRIST]
    return weighted_point(((mcp, 0.55), (pip, 0.25), (wrist, 0.20)))


def distance(a, b):
    dx = a[0] - b[0]
    dy = a[1] - b[1]
    return math.sqrt(dx * dx + dy * dy)


def average_point(points):
    return (
        sum(point[0] for point in points) / max(1, len(points)),
        sum(point[1] for point in points) / max(1, len(points)),
    )


def normalize2(vector, fallback):
    length = math.sqrt(vector[0] * vector[0] + vector[1] * vector[1])
    if length < 0.0001:
        return fallback
    return (vector[0] / length, vector[1] / length)


def dot2(a, b):
    return a[0] * b[0] + a[1] * b[1]


def hand_to_raw_dict(raw_index, points, label, score):
    mirrored_points = [mirror_point(point) for point in points]
    palm = palm_center(points)
    mirrored_palm = mirror_point(palm)
    grip = grip_center(points)
    mirrored_grip = mirror_point(grip)
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
        "grip01": rounded_point(grip),
        "mirroredGrip01": rounded_point(mirrored_grip),
    }


def body_to_raw_dict(points):
    left_shoulder = points[POSE_LEFT_SHOULDER]
    right_shoulder = points[POSE_RIGHT_SHOULDER]
    left_hip = points[POSE_LEFT_HIP]
    right_hip = points[POSE_RIGHT_HIP]
    center = average_point(
        [left_shoulder, right_shoulder, left_hip, right_hip]
    )
    shoulder_center = average_point([left_shoulder, right_shoulder])
    hip_center = average_point([left_hip, right_hip])
    shoulder_width = max(0.08, distance(left_shoulder, right_shoulder))
    torso_height = max(0.10, distance(shoulder_center, hip_center))

    return {
        "center01": rounded_point(center),
        "mirroredCenter01": rounded_point(mirror_point(center)),
        "leftShoulder01": rounded_point(left_shoulder),
        "rightShoulder01": rounded_point(right_shoulder),
        "leftHip01": rounded_point(left_hip),
        "rightHip01": rounded_point(right_hip),
        "mirroredLeftShoulder01": rounded_point(mirror_point(left_shoulder)),
        "mirroredRightShoulder01": rounded_point(mirror_point(right_shoulder)),
        "mirroredLeftHip01": rounded_point(mirror_point(left_hip)),
        "mirroredRightHip01": rounded_point(mirror_point(right_hip)),
        "shoulderWidth01": round(shoulder_width, 6),
        "torsoHeight01": round(torso_height, 6),
    }


def body_correct_point(point, body, mirrored):
    if body is None:
        return point

    if mirrored:
        center = body["mirroredCenter01"]
        left_shoulder = body["mirroredLeftShoulder01"]
        right_shoulder = body["mirroredRightShoulder01"]
        left_hip = body["mirroredLeftHip01"]
        right_hip = body["mirroredRightHip01"]
    else:
        center = body["center01"]
        left_shoulder = body["leftShoulder01"]
        right_shoulder = body["rightShoulder01"]
        left_hip = body["leftHip01"]
        right_hip = body["rightHip01"]

    shoulder_center = average_point([left_shoulder, right_shoulder])
    hip_center = average_point([left_hip, right_hip])
    right_axis = normalize2(
        (right_shoulder[0] - left_shoulder[0],
         right_shoulder[1] - left_shoulder[1]),
        (1.0, 0.0),
    )
    down_axis = normalize2(
        (hip_center[0] - shoulder_center[0],
         hip_center[1] - shoulder_center[1]),
        (-right_axis[1], right_axis[0]),
    )
    # Keep axes orthogonal enough that shoulder tilt does not leak into swing up/down.
    down_axis = normalize2(
        (down_axis[0] - right_axis[0] * dot2(down_axis, right_axis),
         down_axis[1] - right_axis[1] * dot2(down_axis, right_axis)),
        (-right_axis[1], right_axis[0]),
    )

    shoulder_width = max(0.08, float(body.get("shoulderWidth01", 0.0)))
    torso_height = max(0.10, float(body.get("torsoHeight01", 0.0)))
    scale_x = max(0.72, min(1.80, NOMINAL_SHOULDER_WIDTH / shoulder_width))
    scale_y = max(0.72, min(1.55, NOMINAL_TORSO_HEIGHT / torso_height))
    offset = (point[0] - center[0], point[1] - center[1])
    return (
        clamp01(0.5 + dot2(offset, right_axis) * scale_x),
        clamp01(0.5 + dot2(offset, down_axis) * scale_y),
    )


def apply_body_correction(hands, body):
    if body is None:
        return
    for hand in hands:
        hand["bodyCorrectedPalm01"] = rounded_point(
            body_correct_point(hand["palm01"], body, False)
        )
        hand["bodyCorrectedMirroredPalm01"] = rounded_point(
            body_correct_point(hand["mirroredPalm01"], body, True)
        )
        hand["bodyCorrectedGrip01"] = rounded_point(
            body_correct_point(hand["grip01"], body, False)
        )
        hand["bodyCorrectedMirroredGrip01"] = rounded_point(
            body_correct_point(hand["mirroredGrip01"], body, True)
        )


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


def detect_body(detector, frame, timestamp_ms):
    if detector is None or detector["type"] != "solutions":
        return None

    rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    result = detector["detector"].process(rgb)
    if result is None or not result.pose_landmarks:
        return None
    landmarks = result.pose_landmarks.landmark
    if not landmarks or len(landmarks) <= max(BODY_LANDMARK_INDICES):
        return None

    points = [landmark_xy(landmark) for landmark in landmarks]
    return body_to_raw_dict(points)


def build_raw_packet(frame_index, timestamp_ms, dt, frame, hands, body, args):
    height, width = frame.shape[:2]
    debug = {
        "handCount": len(hands),
        "bodyTracked": body is not None,
        "bodyCorrectedHands": sum(
            1 for hand in hands if "bodyCorrectedMirroredPalm01" in hand
        ),
        "labels": [hand.get("label", "Hand") for hand in hands],
        "scores": [round(float(hand.get("score", 0.0)), 6) for hand in hands],
        "mirroredPalms01": [
            hand.get("mirroredPalm01", [0.5, 0.5]) for hand in hands
        ],
        "mirroredGrips01": [
            hand.get("mirroredGrip01", [0.5, 0.5]) for hand in hands
        ],
        "bodyCorrectedMirroredPalms01": [
            hand.get("bodyCorrectedMirroredPalm01") for hand in hands
        ],
        "bodyCorrectedMirroredGrips01": [
            hand.get("bodyCorrectedMirroredGrip01") for hand in hands
        ],
    }
    packet = {
        "type": "HAND_RAW",
        "frame": frame_index,
        "timestampMs": timestamp_ms,
        "dt": round(dt, 6),
        "debug": debug,
        "image": {
            "width": width,
            "height": height,
            "camera": str(args.camera),
            "mirrorPreview": args.mirror_preview,
            "mirrorControls": args.mirror_controls,
            "coordinateSpace": "MediaPipe normalized image coordinates",
        },
        "body": body,
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

        palm = hand["mirroredGrip01"] if mirror_preview else hand["grip01"]
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


def body_point(body, name, mirror_preview):
    if mirror_preview:
        return body.get("mirrored" + name[0].upper() + name[1:])
    return body.get(name)


def draw_body(frame, body, mirror_preview):
    if body is None:
        return

    height, width = frame.shape[:2]
    names = ("leftShoulder01", "rightShoulder01", "leftHip01", "rightHip01")
    points = []
    for name in names:
        point = body_point(body, name, mirror_preview)
        if point is None:
            return
        points.append(point_to_px(point, width, height))

    color = (255, 210, 80)
    cv2.line(frame, tuple(points[0]), tuple(points[1]), color, 2, cv2.LINE_AA)
    cv2.line(frame, tuple(points[0]), tuple(points[2]), color, 2, cv2.LINE_AA)
    cv2.line(frame, tuple(points[1]), tuple(points[3]), color, 2, cv2.LINE_AA)
    cv2.line(frame, tuple(points[2]), tuple(points[3]), color, 2, cv2.LINE_AA)
    center_key = "mirroredCenter01" if mirror_preview else "center01"
    cx, cy = point_to_px(body[center_key], width, height)
    cv2.circle(frame, (cx, cy), 7, color, 2, cv2.LINE_AA)


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


def print_startup_log(args, cap, backend, hands_detector, pose_detector):
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
    log_info(
        f"Pose API: {pose_detector['type'] if pose_detector else 'disabled'}"
    )
    log_info("Mode: HAND_RAW only.")
    log_info(f"UDP: {args.udp_host}:{args.udp_port}")
    log_info(f"Preview UDP: {args.preview_host}:{args.preview_port}")
    log_info(
        f"Raw log: {args.raw_log if args.raw_log_enabled else 'disabled'}"
    )
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
    parser.add_argument(
        "--raw-log",
        default=os.path.join(tempfile.gettempdir(), "shingiittai_hand_raw.jsonl"),
    )
    parser.add_argument("--no-raw-log", dest="raw_log_enabled", action="store_false")
    parser.add_argument("--no-raw-json", dest="raw_json", action="store_false")
    parser.add_argument("--log-interval", type=float, default=1.0)
    parser.add_argument(
        "--no-mirror-controls", dest="mirror_controls", action="store_false"
    )
    parser.add_argument(
        "--no-mirror-preview", dest="mirror_preview", action="store_false"
    )
    parser.set_defaults(
        mirror_controls=True,
        mirror_preview=True,
        raw_json=True,
        raw_log_enabled=True,
    )
    args, _unknown = parser.parse_known_args()
    if args.pose_model:
        log_warn("--pose-model is accepted for compatibility but not required")
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
    pose_detector = create_pose_detector(mp)

    print_startup_log(args, cap, camera_backend, hands_detector, pose_detector)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (args.udp_host, args.udp_port)
    preview_target = (args.preview_host, args.preview_port)
    preview_state = {"frame_id": 0, "last_time": 0.0}
    raw_log_file = None
    if args.raw_log_enabled:
        try:
            raw_log_file = open(args.raw_log, "w", encoding="utf-8")
            log_info(f"Raw JSONL log opened: {args.raw_log}")
        except Exception as error:
            log_warn(f"Raw JSONL log open failed: {error}")

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
            body = detect_body(pose_detector, frame, timestamp_ms)
            apply_body_correction(hands, body)
            if args.raw_json:
                raw_packet = build_raw_packet(
                    frame_index, timestamp_ms, dt, frame, hands, body, args
                )
                if raw_log_file is not None:
                    raw_log_file.write(raw_packet[len("HAND_RAW "):] + "\n")
                    if frame_index % 30 == 0:
                        raw_log_file.flush()
                raw_payload = raw_packet.encode("utf-8")
                sock.sendto(raw_payload, target)

            display = cv2.flip(frame, 1) if args.mirror_preview else frame.copy()
            draw_body(display, body, args.mirror_preview)
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
        close_detector(pose_detector)
        cap.release()
        if raw_log_file is not None:
            raw_log_file.close()
        sock.close()
        if args.show_window:
            cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    sys.exit(main())
