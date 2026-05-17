import argparse
import math
import os
import socket
import tempfile
import time

os.environ.setdefault(
    "MPLCONFIGDIR", os.path.join(tempfile.gettempdir(), "shingiittai-matplotlib")
)

import cv2
import mediapipe as mp
import numpy as np
from mediapipe.tasks import python
from mediapipe.tasks.python import vision


PALM_INDICES = (0, 5, 9, 13, 17)
HAND_ANCHOR_WRIST_WEIGHT = 0.70
HAND_ANCHOR_BBOX_WEIGHT = 0.30
DETECTION_GRACE_SECONDS = 0.32
PREDICTED_HAND_CONFIDENCE = 0.38
FRAME_EXIT_MARGIN = 0.035
MIN_IN_FRAME_LANDMARK_RATIO = 0.48
MIN_IN_FRAME_PALM_POINTS = 3
DISTANCE_REFERENCE_HAND_SIZE = 0.20
DISTANCE_SCALE_MIN = 0.65
DISTANCE_SCALE_MAX = 2.45
DISTANCE_SCALE_FOLLOW = 0.35
MAX_TRACKED_STEP_PER_SECOND = 16.0
MAX_TRACKED_STEP = 0.34
MIN_TRACKED_STEP = 0.08
CENTER_FILTER_MIN_CUTOFF = 3.4
CENTER_FILTER_BETA = 34.0
POINT_FILTER_MIN_CUTOFF = 3.2
POINT_FILTER_BETA = 28.0
FILTER_D_CUTOFF = 1.0
MOTION_HISTORY_SECONDS = 0.035
MOTION_HISTORY_MIN_SPEED = 0.16
KALMAN_PROCESS_NOISE = 0.018
KALMAN_MEASUREMENT_NOISE = 0.020
KALMAN_ERROR = 0.08
DEBUG_TRAIL_SECONDS = 0.45
DEBUG_TRAIL_MIN_ALPHA = 0.25
DEBUG_RAW_POINT_COLOR = (0, 220, 255)
DEBUG_PREDICTED_COLOR = (240, 180, 80)
PREVIEW_CHUNK_BYTES = 1150


def default_model_path():
    return os.path.join(
        os.path.dirname(__file__), "models", "hand_landmarker.task"
    )


def clamp01(value):
    return max(0.0, min(1.0, value))


def one_euro_alpha(cutoff, dt):
    tau = 1.0 / (2.0 * math.pi * cutoff)
    return 1.0 / (1.0 + tau / max(dt, 1.0 / 240.0))


class OneEuroValueFilter:
    def __init__(self, min_cutoff, beta, d_cutoff):
        self.min_cutoff = min_cutoff
        self.beta = beta
        self.d_cutoff = d_cutoff
        self.value = 0.0
        self.derivative = 0.0
        self.ready = False

    def reset(self):
        self.ready = False
        self.derivative = 0.0

    def apply(self, value, dt):
        if not self.ready:
            self.value = value
            self.derivative = 0.0
            self.ready = True
            return value

        raw_derivative = (value - self.value) / max(dt, 1.0 / 240.0)
        d_alpha = one_euro_alpha(self.d_cutoff, dt)
        self.derivative = (
            self.derivative + d_alpha * (raw_derivative - self.derivative)
        )
        cutoff = self.min_cutoff + self.beta * abs(self.derivative)
        alpha = one_euro_alpha(cutoff, dt)
        self.value = self.value + alpha * (value - self.value)
        return self.value


class PointFilter:
    def __init__(self, min_cutoff, beta, d_cutoff):
        self.x = OneEuroValueFilter(min_cutoff, beta, d_cutoff)
        self.y = OneEuroValueFilter(min_cutoff, beta, d_cutoff)

    def reset(self):
        self.x.reset()
        self.y.reset()

    def apply(self, point, dt):
        return clamp01(self.x.apply(point[0], dt)), clamp01(self.y.apply(point[1], dt))


class PointListFilter:
    def __init__(self, min_cutoff, beta, d_cutoff):
        self.min_cutoff = min_cutoff
        self.beta = beta
        self.d_cutoff = d_cutoff
        self.filters = []

    def reset(self):
        for point_filter in self.filters:
            point_filter.reset()

    def apply(self, points, dt):
        if len(self.filters) != len(points):
            self.filters = [
                PointFilter(self.min_cutoff, self.beta, self.d_cutoff)
                for _ in points
            ]

        return [
            point_filter.apply(point, dt)
            for point_filter, point in zip(self.filters, points)
        ]


class MotionHistory:
    def __init__(self, window_seconds):
        self.window_seconds = window_seconds
        self.samples = []

    def reset(self):
        self.samples = []

    def apply(self, dx, dy, speed, dt):
        self.samples = [
            (age + dt, sample_dx, sample_dy, sample_speed)
            for age, sample_dx, sample_dy, sample_speed in self.samples
            if age + dt <= self.window_seconds
        ]

        current_length = math.sqrt(dx * dx + dy * dy)
        if speed >= MOTION_HISTORY_MIN_SPEED or current_length > 0.0015:
            self.samples.append((0.0, dx, dy, speed))

        if not self.samples:
            return dx, dy, speed

        weighted_dx = 0.0
        weighted_dy = 0.0
        weighted_length = 0.0
        weighted_speed = 0.0
        total_weight = 0.0
        for age, sample_dx, sample_dy, sample_speed in self.samples:
            recency = max(0.0, 1.0 - age / self.window_seconds)
            weight = 0.18 + recency * recency
            weighted_dx += sample_dx * weight
            weighted_dy += sample_dy * weight
            weighted_length += math.sqrt(sample_dx * sample_dx + sample_dy * sample_dy) * weight
            weighted_speed += sample_speed * weight
            total_weight += weight

        if total_weight <= 0.0:
            return dx, dy, speed

        avg_dx = weighted_dx / total_weight
        avg_dy = weighted_dy / total_weight
        avg_length = weighted_length / total_weight
        avg_speed = weighted_speed / total_weight
        direction_length = math.sqrt(avg_dx * avg_dx + avg_dy * avg_dy)
        if direction_length <= 0.000001:
            return dx, dy, speed

        target_length = max(current_length, avg_length)
        return (
            avg_dx / direction_length * target_length,
            avg_dy / direction_length * target_length,
            max(speed, avg_speed),
        )


class CenterKalmanFilter:
    def __init__(self):
        self.filter = cv2.KalmanFilter(4, 2)
        self.filter.measurementMatrix = np.array(
            [[1, 0, 0, 0], [0, 1, 0, 0]], np.float32
        )
        self.filter.processNoiseCov = np.eye(4, dtype=np.float32) * KALMAN_PROCESS_NOISE
        self.filter.measurementNoiseCov = (
            np.eye(2, dtype=np.float32) * KALMAN_MEASUREMENT_NOISE
        )
        self.filter.errorCovPost = np.eye(4, dtype=np.float32) * KALMAN_ERROR
        self.ready = False

    def reset(self):
        self.ready = False
        self.filter.errorCovPost = np.eye(4, dtype=np.float32) * KALMAN_ERROR

    def configure_transition(self, dt):
        safe_dt = max(dt, 1.0 / 240.0)
        self.filter.transitionMatrix = np.array(
            [[1, 0, safe_dt, 0], [0, 1, 0, safe_dt], [0, 0, 1, 0], [0, 0, 0, 1]],
            np.float32,
        )

    def apply(self, point, dt):
        self.configure_transition(dt)
        x = np.float32(clamp01(point[0]))
        y = np.float32(clamp01(point[1]))
        if not self.ready:
            self.filter.statePost = np.array([[x], [y], [0], [0]], np.float32)
            self.ready = True
            return float(x), float(y)

        self.filter.predict()
        corrected = self.filter.correct(np.array([[x], [y]], np.float32))
        return clamp01(float(corrected[0, 0])), clamp01(float(corrected[1, 0]))

    def predict(self, dt):
        if not self.ready:
            return None

        self.configure_transition(dt)
        predicted = self.filter.predict()
        self.filter.statePost = predicted
        return clamp01(float(predicted[0, 0])), clamp01(float(predicted[1, 0]))


def average_landmark(landmarks, indices):
    x = sum(landmarks[index].x for index in indices) / len(indices)
    y = sum(landmarks[index].y for index in indices) / len(indices)
    return clamp01(x), clamp01(y)


def stable_palm_center(landmarks):
    avg_x, avg_y = average_landmark(landmarks, PALM_INDICES)
    xs = sorted(landmarks[index].x for index in PALM_INDICES)
    ys = sorted(landmarks[index].y for index in PALM_INDICES)
    median_x = xs[len(xs) // 2]
    median_y = ys[len(ys) // 2]
    return clamp01(avg_x * 0.65 + median_x * 0.35), clamp01(
        avg_y * 0.65 + median_y * 0.35
    )


def landmark_bbox_center(landmarks):
    xs = [clamp01(landmark.x) for landmark in landmarks]
    ys = [clamp01(landmark.y) for landmark in landmarks]
    return (min(xs) + max(xs)) * 0.5, (min(ys) + max(ys)) * 0.5


def copy_landmark_points(landmarks):
    return [(clamp01(landmark.x), clamp01(landmark.y)) for landmark in landmarks]


def copy_palm_points(landmarks):
    return [
        (clamp01(landmarks[index].x), clamp01(landmarks[index].y))
        for index in PALM_INDICES
    ]


def landmark_in_frame(landmark, margin=FRAME_EXIT_MARGIN):
    return (
        -margin <= landmark.x <= 1.0 + margin
        and -margin <= landmark.y <= 1.0 + margin
    )


def hand_is_reliable_in_frame(landmarks):
    xs = [landmark.x for landmark in landmarks]
    ys = [landmark.y for landmark in landmarks]
    if (
        max(xs) < -FRAME_EXIT_MARGIN
        or min(xs) > 1.0 + FRAME_EXIT_MARGIN
        or max(ys) < -FRAME_EXIT_MARGIN
        or min(ys) > 1.0 + FRAME_EXIT_MARGIN
    ):
        return False

    in_frame_count = sum(1 for landmark in landmarks if landmark_in_frame(landmark))
    palm_in_frame_count = sum(
        1 for index in PALM_INDICES if landmark_in_frame(landmarks[index])
    )
    in_frame_ratio = in_frame_count / max(1, len(landmarks))
    return (
        in_frame_ratio >= MIN_IN_FRAME_LANDMARK_RATIO
        and palm_in_frame_count >= MIN_IN_FRAME_PALM_POINTS
    )


def landmark_distance(a, b):
    dx = a.x - b.x
    dy = a.y - b.y
    return math.sqrt(dx * dx + dy * dy)


def hand_apparent_size(landmarks):
    xs = [landmark.x for landmark in landmarks]
    ys = [landmark.y for landmark in landmarks]
    bbox_w = max(xs) - min(xs)
    bbox_h = max(ys) - min(ys)
    bbox_diag = math.sqrt(bbox_w * bbox_w + bbox_h * bbox_h)
    palm_width = landmark_distance(landmarks[5], landmarks[17])
    palm_length = landmark_distance(landmarks[0], landmarks[9])
    return max(0.001, palm_width * 0.50 + palm_length * 0.30 + bbox_diag * 0.20)


def distance_motion_scale(landmarks):
    apparent_size = hand_apparent_size(landmarks)
    raw_scale = DISTANCE_REFERENCE_HAND_SIZE / apparent_size
    return max(DISTANCE_SCALE_MIN, min(DISTANCE_SCALE_MAX, raw_scale))


def handedness_score(handedness):
    if not handedness:
        return 1.0
    return clamp01(max(category.score for category in handedness))


def distance_sq(a, b):
    dx = a[0] - b[0]
    dy = a[1] - b[1]
    return dx * dx + dy * dy


def limit_step(prev_x, prev_y, x, y, max_step):
    dx = x - prev_x
    dy = y - prev_y
    distance = math.sqrt(dx * dx + dy * dy)
    if distance <= max_step or distance <= 0.000001:
        return x, y

    scale = max_step / distance
    return prev_x + dx * scale, prev_y + dy * scale


def limit_points_motion(points, prev_points, max_step):
    if prev_points is None:
        return points

    limited = []
    for point, prev_point in zip(points, prev_points):
        limited.append(
            limit_step(prev_point[0], prev_point[1], point[0], point[1], max_step)
        )
    return limited


def landmark_point(landmark):
    return clamp01(landmark.x), clamp01(landmark.y)


def midpoint(a, b):
    return (a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5


def blend_points(weighted_points):
    total_weight = sum(weight for _, weight in weighted_points)
    if total_weight <= 0.0:
        return 0.5, 0.5

    x = sum(point[0] * weight for point, weight in weighted_points) / total_weight
    y = sum(point[1] * weight for point, weight in weighted_points) / total_weight
    return clamp01(x), clamp01(y)


def build_control_center(hand_landmarks):
    wrist = landmark_point(hand_landmarks[0])
    bbox_center = landmark_bbox_center(hand_landmarks)
    return blend_points(
        (
            (wrist, HAND_ANCHOR_WRIST_WEIGHT),
            (bbox_center, HAND_ANCHOR_BBOX_WEIGHT),
        )
    )


def build_motion_points(hand_landmarks):
    palm = stable_palm_center(hand_landmarks)
    wrist = landmark_point(hand_landmarks[0])

    points = [
        palm,
        palm,
        midpoint(palm, wrist),
        wrist,
        wrist,
    ]
    points.extend([wrist, midpoint(wrist, palm), palm, palm])
    return points


def assign_detected_hands(
    detected_hands, prev_positions, prev_landmarks, missing_timers, max_hands
):
    assigned = [None for _ in range(max_hands)]
    if not detected_hands:
        return assigned

    unused_detections = set(range(len(detected_hands)))
    active_slots = [
        index
        for index in range(max_hands)
        if prev_landmarks[index] is not None
        and missing_timers[index] <= DETECTION_GRACE_SECONDS * 2.0
    ]
    pairs = []
    for slot in active_slots:
        for detection_index in unused_detections:
            hand = detected_hands[detection_index]
            pairs.append(
                (
                    distance_sq(prev_positions[slot], (hand["x"], hand["y"])),
                    slot,
                    detection_index,
                )
            )

    used_slots = set()
    for _, slot, detection_index in sorted(pairs):
        if slot in used_slots or detection_index not in unused_detections:
            continue
        assigned[slot] = detected_hands[detection_index]
        used_slots.add(slot)
        unused_detections.remove(detection_index)

    empty_slots = [index for index in range(max_hands) if assigned[index] is None]
    new_hands = sorted(
        (detected_hands[index] for index in unused_detections),
        key=lambda hand: hand["x"],
        reverse=True,
    )
    for slot, hand in zip(empty_slots, new_hands):
        assigned[slot] = hand

    return assigned


def get_average_motion(points, prev_points, dt):
    if points is None or prev_points is None or len(points) != len(prev_points):
        return 0.0, 0.0, 0.0

    deltas = []
    for point, prev_point in zip(points, prev_points):
        dx = point[0] - prev_point[0]
        dy = point[1] - prev_point[1]
        deltas.append((dx, dy, math.sqrt(dx * dx + dy * dy)))

    lengths = sorted(delta[2] for delta in deltas)
    median_length = lengths[len(lengths) // 2]
    max_kept_length = max(median_length * 2.4, 0.018)
    kept = [delta for delta in deltas if delta[2] <= max_kept_length]
    if len(kept) < max(3, len(deltas) // 2):
        kept = deltas

    dx_values = sorted(delta[0] for delta in kept)
    dy_values = sorted(delta[1] for delta in kept)
    median_dx = dx_values[len(dx_values) // 2]
    median_dy = dy_values[len(dy_values) // 2]
    mean_dx = sum(delta[0] for delta in kept) / len(kept)
    mean_dy = sum(delta[1] for delta in kept) / len(kept)

    dx = mean_dx * 0.45 + median_dx * 0.55
    dy = mean_dy * 0.45 + median_dy * 0.55
    speed = sum(delta[2] for delta in kept) / len(kept) / dt
    return dx, dy, speed


def to_pixel(point, width, height):
    x = int(round(clamp01(point[0]) * (width - 1)))
    y = int(round(clamp01(point[1]) * (height - 1)))
    return x, y


def blend_color(color, alpha):
    base = 35
    return tuple(int(base * (1.0 - alpha) + channel * alpha) for channel in color)


def draw_debug_point(frame, point, color, label=None, radius=7, filled=True):
    height, width = frame.shape[:2]
    pixel = to_pixel(point, width, height)
    cv2.circle(frame, pixel, radius, color, -1 if filled else 2, cv2.LINE_AA)
    cv2.circle(frame, pixel, radius + 2, (20, 20, 20), 1, cv2.LINE_AA)
    if label:
        cv2.putText(
            frame,
            label,
            (pixel[0] + 9, pixel[1] - 9),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.42,
            color,
            1,
            cv2.LINE_AA,
        )


def draw_hand_bbox(frame, landmarks, color):
    height, width = frame.shape[:2]
    xs = [clamp01(landmark.x) for landmark in landmarks]
    ys = [clamp01(landmark.y) for landmark in landmarks]
    left, top = to_pixel((min(xs), min(ys)), width, height)
    right, bottom = to_pixel((max(xs), max(ys)), width, height)
    cv2.rectangle(
        frame,
        (left, top),
        (right, bottom),
        color,
        2,
        cv2.LINE_AA,
    )


def update_debug_trail(trail, point, dt):
    trail[:] = [
        (age + dt, sample)
        for age, sample in trail
        if age + dt <= DEBUG_TRAIL_SECONDS
    ]
    trail.insert(0, (0.0, point))


def draw_debug_trail(frame, trail, color):
    if len(trail) < 2:
        return

    height, width = frame.shape[:2]
    for index in range(len(trail) - 1):
        age = max(trail[index][0], trail[index + 1][0])
        life = max(0.0, 1.0 - age / DEBUG_TRAIL_SECONDS)
        alpha = DEBUG_TRAIL_MIN_ALPHA + life * (1.0 - DEBUG_TRAIL_MIN_ALPHA)
        line_color = blend_color(color, alpha)
        thickness = 1 + int(life * 3.0)
        cv2.line(
            frame,
            to_pixel(trail[index][1], width, height),
            to_pixel(trail[index + 1][1], width, height),
            line_color,
            thickness,
            cv2.LINE_AA,
        )


def parse_args():
    parser = argparse.ArgumentParser(description="Send MediaPipe hand motion over UDP.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5005)
    parser.add_argument("--preview-host", default="127.0.0.1")
    parser.add_argument("--preview-port", type=int, default=5006)
    parser.add_argument("--preview-width", type=int, default=320)
    parser.add_argument("--preview-height", type=int, default=240)
    parser.add_argument("--preview-fps", type=float, default=20.0)
    parser.add_argument("--preview-quality", type=int, default=62)
    parser.add_argument("--show-window", action="store_true")
    parser.add_argument("--camera", type=int, default=0)
    parser.add_argument("--max-hands", type=int, default=2)
    parser.add_argument("--model", default=default_model_path())
    return parser.parse_args()


def send_preview_frame(sock, target, frame, args, state, now):
    interval = 1.0 / max(args.preview_fps, 1.0)
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
    if not data:
        return

    state["frame_id"] = (state["frame_id"] + 1) & 0xFFFFFFFF
    frame_id = state["frame_id"]
    chunk_count = math.ceil(len(data) / PREVIEW_CHUNK_BYTES)
    for chunk_index in range(chunk_count):
        start = chunk_index * PREVIEW_CHUNK_BYTES
        end = min(start + PREVIEW_CHUNK_BYTES, len(data))
        header = (
            f"SGCAM {frame_id} {chunk_index} {chunk_count} {len(data)}\n"
        ).encode("ascii")
        sock.sendto(header + data[start:end], target)

    state["last_time"] = now


def main():
    args = parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (args.host, args.port)
    preview_target = (args.preview_host, args.preview_port)
    preview_state = {"frame_id": 0, "last_time": 0.0}

    capture = cv2.VideoCapture(args.camera)
    if not capture.isOpened():
        raise RuntimeError(f"Could not open camera {args.camera}")

    capture.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    capture.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    capture.set(cv2.CAP_PROP_FPS, 60)
    capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    base_options = python.BaseOptions(model_asset_path=args.model)
    options = vision.HandLandmarkerOptions(
        base_options=base_options,
        running_mode=vision.RunningMode.VIDEO,
        num_hands=args.max_hands,
        min_hand_detection_confidence=0.35,
        min_hand_presence_confidence=0.35,
        min_tracking_confidence=0.35,
    )
    landmarker = vision.HandLandmarker.create_from_options(options)

    max_hands = max(1, min(args.max_hands, 2))
    prev_positions = [(0.5, 0.5) for _ in range(max_hands)]
    prev_landmarks = [None for _ in range(max_hands)]
    missing_timers = [DETECTION_GRACE_SECONDS for _ in range(max_hands)]
    center_filters = [
        PointFilter(CENTER_FILTER_MIN_CUTOFF, CENTER_FILTER_BETA, FILTER_D_CUTOFF)
        for _ in range(max_hands)
    ]
    center_kalman_filters = [CenterKalmanFilter() for _ in range(max_hands)]
    point_filters = [
        PointListFilter(POINT_FILTER_MIN_CUTOFF, POINT_FILTER_BETA, FILTER_D_CUTOFF)
        for _ in range(max_hands)
    ]
    motion_histories = [
        MotionHistory(MOTION_HISTORY_SECONDS) for _ in range(max_hands)
    ]
    distance_scales = [1.0 for _ in range(max_hands)]
    center_trails = [[] for _ in range(max_hands)]
    start_time = time.perf_counter()
    prev_time = time.perf_counter()
    last_timestamp_ms = -1

    try:
        while True:
            ok, frame = capture.read()
            if not ok:
                break

            frame = cv2.flip(frame, 1)
            camera_preview_frame = frame.copy()
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
            now = time.perf_counter()
            timestamp_ms = max(last_timestamp_ms + 1, int((now - start_time) * 1000))
            last_timestamp_ms = timestamp_ms
            result = landmarker.detect_for_video(image, timestamp_ms)

            dt = max(now - prev_time, 1.0 / 120.0)
            prev_time = now

            detected_hands = []
            if result.hand_landmarks:
                for hand_index, hand_landmarks in enumerate(
                    result.hand_landmarks[:max_hands]
                ):
                    if not hand_is_reliable_in_frame(hand_landmarks):
                        continue

                    center_x, center_y = build_control_center(hand_landmarks)
                    handedness = (
                        result.handedness[hand_index]
                        if result.handedness and hand_index < len(result.handedness)
                        else None
                    )
                    detected_hands.append(
                        {
                            "landmarks": hand_landmarks,
                            "points": build_motion_points(hand_landmarks),
                            "x": center_x,
                            "y": center_y,
                            "motion_scale": distance_motion_scale(hand_landmarks),
                            "confidence": max(0.65, handedness_score(handedness)),
                        }
                    )

            assigned_hands = assign_detected_hands(
                detected_hands, prev_positions, prev_landmarks, missing_timers, max_hands
            )

            speeds = []
            valid_count = 0
            height, width = frame.shape[:2]
            colors = [(30, 240, 90), (80, 180, 255)]

            for hand_index in range(max_hands):
                hand = assigned_hands[hand_index]
                detected = hand is not None
                prev_x, prev_y = prev_positions[hand_index]
                x = prev_x
                y = prev_y
                raw_x = x
                raw_y = y
                confidence = 0.0
                motion_scale = distance_scales[hand_index]
                points = None
                predicted_only = False

                if detected:
                    x = hand["x"]
                    y = hand["y"]
                    raw_x = x
                    raw_y = y
                    confidence = hand["confidence"]
                    motion_scale = (
                        motion_scale
                        + (hand["motion_scale"] - motion_scale)
                        * DISTANCE_SCALE_FOLLOW
                    )
                    distance_scales[hand_index] = motion_scale
                    points = hand["points"]
                    draw_hand_bbox(frame, hand["landmarks"], colors[hand_index])
                    draw_debug_point(
                        frame,
                        (raw_x, raw_y),
                        DEBUG_RAW_POINT_COLOR,
                        radius=4,
                    )
                    max_step = max(
                        MIN_TRACKED_STEP,
                        min(MAX_TRACKED_STEP, MAX_TRACKED_STEP_PER_SECOND * dt),
                    )
                    if prev_landmarks[hand_index] is not None:
                        x, y = limit_step(prev_x, prev_y, x, y, max_step)
                        points = limit_points_motion(
                            points, prev_landmarks[hand_index], max_step
                        )
                    x, y = center_kalman_filters[hand_index].apply((x, y), dt)
                    x, y = center_filters[hand_index].apply((x, y), dt)
                    points = point_filters[hand_index].apply(points, dt)
                    valid_count += 1
                    missing_timers[hand_index] = 0.0

                    for landmark in hand["landmarks"]:
                        px = int(max(0, min(width - 1, landmark.x * width)))
                        py = int(max(0, min(height - 1, landmark.y * height)))
                        cv2.circle(frame, (px, py), 4, colors[hand_index], -1)
                else:
                    missing_timers[hand_index] += dt

                valid = detected or (
                    prev_landmarks[hand_index] is not None
                    and missing_timers[hand_index] <= DETECTION_GRACE_SECONDS
                )
                if not detected and valid:
                    predicted = center_kalman_filters[hand_index].predict(dt)
                    if predicted is not None:
                        x, y = predicted
                        predicted_only = True
                        confidence = PREDICTED_HAND_CONFIDENCE

                dx = x - prev_x
                dy = y - prev_y
                landmark_dx, landmark_dy, landmark_speed = get_average_motion(
                    points, prev_landmarks[hand_index], dt
                ) if detected else (0.0, 0.0, 0.0)
                if detected:
                    landmark_dx, landmark_dy, landmark_speed = motion_histories[
                        hand_index
                    ].apply(landmark_dx, landmark_dy, landmark_speed, dt)
                center_len = math.sqrt(dx * dx + dy * dy)
                landmark_len = math.sqrt(landmark_dx * landmark_dx + landmark_dy * landmark_dy)
                motion_dx = dx
                motion_dy = dy
                if detected and landmark_len > center_len * 0.60:
                    motion_dx = landmark_dx
                    motion_dy = landmark_dy
                packet_dx = motion_dx * motion_scale
                packet_dy = motion_dy * motion_scale
                center_speed = center_len / dt
                motion_speed = max(center_speed, landmark_speed)
                speed = motion_speed * motion_scale
                prev_positions[hand_index] = (x, y)
                if detected:
                    prev_landmarks[hand_index] = points
                elif not valid:
                    distance_scales[hand_index] = 1.0
                    prev_landmarks[hand_index] = None
                    center_trails[hand_index] = []
                    center_filters[hand_index].reset()
                    center_kalman_filters[hand_index].reset()
                    point_filters[hand_index].reset()
                    motion_histories[hand_index].reset()
                speeds.append(speed if valid else 0.0)

                if valid:
                    update_debug_trail(center_trails[hand_index], (x, y), dt)
                    draw_debug_trail(frame, center_trails[hand_index], colors[hand_index])
                    draw_debug_point(
                        frame,
                        (x, y),
                        DEBUG_PREDICTED_COLOR if predicted_only else colors[hand_index],
                        radius=7,
                        filled=detected,
                    )

                packet = (
                    f"HAND{hand_index + 1} {1 if valid else 0} "
                    f"{x:.6f} {y:.6f} {packet_dx:.6f} {packet_dy:.6f} "
                    f"{speed:.6f} {confidence:.6f} "
                    f"0.000000 1.000000 {speed:.6f}"
                )
                sock.sendto(packet.encode("ascii"), target)

            send_preview_frame(
                sock, preview_target, camera_preview_frame, args, preview_state, now
            )
            if args.show_window:
                cv2.imshow("Hand UDP Sender", frame)
                if cv2.waitKey(1) & 0xFF == 27:
                    break
    finally:
        landmarker.close()
        capture.release()
        if args.show_window:
            cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
