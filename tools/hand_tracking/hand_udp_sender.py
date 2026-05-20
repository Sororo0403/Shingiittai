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
CONTROL_COMMAND_BYTES = 128
HAND_IDENTITY_MIN_SEPARATION = 0.16
HAND_IDENTITY_REGISTER_FRAMES = 3
HAND_IDENTITY_ACTIVE_ANCHOR_WEIGHT = 0.10
HAND_IDENTITY_RECENT_ANCHOR_WEIGHT = 0.02
HAND_IDENTITY_RECOVER_ANCHOR_WEIGHT = 1.15
HAND_IDENTITY_ACTIVE_LABEL_MISMATCH_PENALTY = 0.012
HAND_IDENTITY_RECOVER_LABEL_MISMATCH_PENALTY = 0.18
HAND_ASSIGNMENT_ACTIVE_SWAP_MARGIN = 0.002
HAND_ASSIGNMENT_CONTINUITY_OVERRIDE_MARGIN = 0.018
HAND_ASSIGNMENT_AMBIGUOUS_SEPARATION = 0.14
HAND_ASSIGNMENT_DIRECTION_MIN_DELTA = 0.004
HAND_ASSIGNMENT_DIRECTION_OVERRIDE_MARGIN = 99.0
HAND_ASSIGNMENT_DIRECTION_MAX_CONTINUITY_PENALTY = 0.001
HAND_ASSIGNMENT_PREDICTION_FRAMES = 3.0
HAND_SINGLE_ACTIVE_CONTINUITY_MAX_DIST = 0.20 * 0.20
HAND_RECOVERY_LAST_POSITION_SECONDS = 1.20
HAND_RECOVERY_POSITION_WEIGHT = 0.85
HAND_ASSIGNMENT_DELTA_FOLLOW = 0.62
HAND_ASSIGNMENT_DELTA_DECAY = 0.82
POSE_LEFT_SHOULDER = 11
POSE_RIGHT_SHOULDER = 12
POSE_LEFT_ELBOW = 13
POSE_RIGHT_ELBOW = 14
POSE_LEFT_WRIST = 15
POSE_RIGHT_WRIST = 16
POSE_MIN_CONFIDENCE = 0.32
HAND_POSE_WRIST_MAX_DIST = 0.32
HAND_POSE_ASSIGNMENT_MARGIN = 0.002
HAND_ASSIGNMENT_CLEAR_MARGIN = 0.024
HAND_ASSIGNMENT_UNCERTAIN_MARGIN = 0.030
HAND_ASSIGNMENT_CLOSE_HOLD_MARGIN = 0.16
HAND_ASSIGNMENT_POSE_CONFLICT_MARGIN = 2.0
HAND_ASSIGNMENT_POSE_CONFLICT_CONFIRM_FRAMES = 3
HAND_ASSIGNMENT_SINGLE_STRONG_POSE_COST = 0.22
HAND_ASSIGNMENT_SINGLE_POSE_MARGIN = 0.06
HAND_ASSIGNMENT_SINGLE_POSE_CONFLICT_MAX_COST = 0.85
HAND_ASSIGNMENT_POSE_WEIGHT = 0.72
HAND_ASSIGNMENT_CONTINUITY_WEIGHT = 0.30
HAND_ASSIGNMENT_RECENT_WEIGHT = 0.48
HAND_ASSIGNMENT_PREDICTION_WEIGHT = 0.18
HAND_ASSIGNMENT_NO_POSE_PENALTY = 0.10
HAND_ASSIGNMENT_NO_TRACK_PENALTY = 0.08
HAND_ASSIGNMENT_LABEL_WEIGHT = 0.028
HAND_ASSIGNMENT_BODY_SCALE_MIN = 0.16
HAND_ASSIGNMENT_BODY_SCALE_MAX = 0.72
HAND_ASSIGNMENT_DEFAULT_BODY_SCALE = 0.34
IDENTITY_CONFIDENCE_POSE = 0.94
IDENTITY_CONFIDENCE_CONTINUITY = 0.72
IDENTITY_CONFIDENCE_HOLD = 0.46
IDENTITY_CONFIDENCE_PREDICTED = 0.34
IDENTITY_CONFIDENCE_INIT = 0.62
IDENTITY_CONFIDENCE_FALLBACK = 0.52


def default_model_path():
    return os.path.join(
        os.path.dirname(__file__), "models", "hand_landmarker.task"
    )


def default_pose_model_path():
    return os.path.join(
        os.path.dirname(__file__), "models", "pose_landmarker_lite.task"
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


def handedness_label(handedness):
    if not handedness:
        return None
    best = max(handedness, key=lambda category: category.score)
    label = getattr(best, "category_name", "") or getattr(best, "display_name", "")
    return label if label else None


def distance_sq(a, b):
    dx = a[0] - b[0]
    dy = a[1] - b[1]
    return dx * dx + dy * dy


def vector_length_sq(v):
    return v[0] * v[0] + v[1] * v[1]


def vector_length(v):
    return math.sqrt(vector_length_sq(v))


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


def pose_landmark_confidence(landmark):
    visibility = getattr(landmark, "visibility", 1.0)
    presence = getattr(landmark, "presence", 1.0)
    return clamp01(min(visibility, presence))


def pose_point(landmarks, index, min_confidence=POSE_MIN_CONFIDENCE):
    if index >= len(landmarks):
        return None

    landmark = landmarks[index]
    confidence = pose_landmark_confidence(landmark)
    if confidence < min_confidence:
        return None
    return {
        "point": (clamp01(landmark.x), clamp01(landmark.y)),
        "confidence": confidence,
    }


def build_pose_arm(landmarks, shoulder_index, elbow_index, wrist_index):
    wrist = pose_point(landmarks, wrist_index)
    elbow = pose_point(landmarks, elbow_index)
    shoulder = pose_point(landmarks, shoulder_index)
    if wrist is None:
        return None

    anchors = [point for point in (shoulder, elbow) if point is not None]
    if not anchors:
        return None

    side_anchor = shoulder or elbow or wrist
    confidence = min(
        wrist["confidence"],
        max(anchor["confidence"] for anchor in anchors),
    )
    return {
        "wrist": wrist["point"],
        "elbow": elbow["point"] if elbow is not None else None,
        "shoulder": shoulder["point"] if shoulder is not None else None,
        "side_x": side_anchor["point"][0],
        "confidence": confidence,
    }


def build_pose_slots(pose_result, max_hands):
    slots = [None for _ in range(max_hands)]
    if pose_result is None or not getattr(pose_result, "pose_landmarks", None):
        return slots

    landmarks = pose_result.pose_landmarks[0]
    arms = [
        build_pose_arm(
            landmarks, POSE_LEFT_SHOULDER, POSE_LEFT_ELBOW, POSE_LEFT_WRIST
        ),
        build_pose_arm(
            landmarks, POSE_RIGHT_SHOULDER, POSE_RIGHT_ELBOW, POSE_RIGHT_WRIST
        ),
    ]
    arms = [arm for arm in arms if arm is not None]
    if not arms:
        return slots

    # After the camera mirror flip, slot 0 is the right side of the screen/body
    # and slot 1 is the left side. Shoulder position is stabler than wrist order.
    arms.sort(key=lambda arm: arm["side_x"], reverse=True)
    for slot, arm in enumerate(arms[:max_hands]):
        slots[slot] = arm
    return slots


def pose_body_scale(pose_slots):
    if not pose_slots:
        return HAND_ASSIGNMENT_DEFAULT_BODY_SCALE

    shoulders = [
        arm.get("shoulder")
        for arm in pose_slots[:2]
        if arm is not None and arm.get("shoulder") is not None
    ]
    if len(shoulders) >= 2:
        return max(
            HAND_ASSIGNMENT_BODY_SCALE_MIN,
            min(
                HAND_ASSIGNMENT_BODY_SCALE_MAX,
                math.sqrt(distance_sq(shoulders[0], shoulders[1])),
            ),
        )

    arm_lengths = []
    for arm in pose_slots[:2]:
        if arm is None:
            continue
        wrist = arm.get("wrist")
        elbow = arm.get("elbow")
        shoulder = arm.get("shoulder")
        if wrist is not None and elbow is not None:
            arm_lengths.append(math.sqrt(distance_sq(wrist, elbow)))
        if elbow is not None and shoulder is not None:
            arm_lengths.append(math.sqrt(distance_sq(elbow, shoulder)))

    if arm_lengths:
        return max(
            HAND_ASSIGNMENT_BODY_SCALE_MIN,
            min(HAND_ASSIGNMENT_BODY_SCALE_MAX, sum(arm_lengths) / len(arm_lengths)),
        )
    return HAND_ASSIGNMENT_DEFAULT_BODY_SCALE


def normalized_distance_sq(a, b, scale):
    safe_scale = max(HAND_ASSIGNMENT_BODY_SCALE_MIN, scale)
    return distance_sq(a, b) / (safe_scale * safe_scale)


def register_assigned_hand_identity(assigned_hands, identity_anchors, identity_labels):
    if len(assigned_hands) < 2 or identity_anchors[0] is not None:
        return

    first = assigned_hands[0]
    second = assigned_hands[1]
    if first is None or second is None:
        return
    if abs(first["x"] - second["x"]) < HAND_IDENTITY_MIN_SEPARATION:
        return

    for slot, hand in enumerate((first, second)):
        identity_anchors[slot] = (hand["x"], hand["y"])
        identity_labels[slot] = hand.get("label")


def register_assigned_hand_identity_stable(
    assigned_hands,
    identity_anchors,
    identity_labels,
    candidate_state,
):
    if len(assigned_hands) < 2 or identity_anchors[0] is not None:
        return

    first = assigned_hands[0]
    second = assigned_hands[1]
    if first is None or second is None:
        candidate_state["count"] = 0
        candidate_state["signature"] = None
        return
    if abs(first["x"] - second["x"]) < HAND_IDENTITY_MIN_SEPARATION:
        candidate_state["count"] = 0
        candidate_state["signature"] = None
        return

    signature = (
        first.get("raw_id") or first.get("label") or "slot0",
        second.get("raw_id") or second.get("label") or "slot1",
    )
    if candidate_state.get("signature") == signature:
        candidate_state["count"] = candidate_state.get("count", 0) + 1
    else:
        candidate_state["signature"] = signature
        candidate_state["count"] = 1

    if candidate_state["count"] < HAND_IDENTITY_REGISTER_FRAMES:
        return

    for slot, hand in enumerate((first, second)):
        identity_anchors[slot] = (hand["x"], hand["y"])
        identity_labels[slot] = hand.get("label")


def hand_assignment_cost(
    slot,
    hand,
    prev_positions,
    prev_landmarks,
    missing_timers,
    identity_anchors,
    identity_labels,
    prev_deltas=None,
):
    active = (
        prev_landmarks[slot] is not None
        and missing_timers[slot] <= DETECTION_GRACE_SECONDS * 2.0
    )
    recent = hand_track_is_recent(slot, prev_landmarks, missing_timers)
    has_anchor = identity_anchors[slot] is not None

    cost = 0.0
    if active:
        cost += distance_sq(prev_positions[slot], (hand["x"], hand["y"]))
    elif recent:
        cost += HAND_RECOVERY_POSITION_WEIGHT * distance_sq(
            prev_positions[slot], (hand["x"], hand["y"])
        )
    if (active or recent) and prev_deltas is not None and slot < len(prev_deltas):
        delta = prev_deltas[slot]
        if vector_length_sq(delta) >= HAND_ASSIGNMENT_DIRECTION_MIN_DELTA ** 2:
            predicted = (
                prev_positions[slot][0] + delta[0] * HAND_ASSIGNMENT_PREDICTION_FRAMES,
                prev_positions[slot][1] + delta[1] * HAND_ASSIGNMENT_PREDICTION_FRAMES,
            )
            cost += distance_sq(predicted, (hand["x"], hand["y"]))
    if has_anchor:
        if active:
            anchor_weight = HAND_IDENTITY_ACTIVE_ANCHOR_WEIGHT
        elif recent:
            anchor_weight = HAND_IDENTITY_RECENT_ANCHOR_WEIGHT
        else:
            anchor_weight = HAND_IDENTITY_RECOVER_ANCHOR_WEIGHT
        cost += anchor_weight * distance_sq(identity_anchors[slot], (hand["x"], hand["y"]))

    label = hand.get("label")
    if (
        identity_labels[slot]
        and label
        and identity_labels[slot] != label
    ):
        label_score = hand.get("label_score", 1.0)
        label_penalty = (
            HAND_IDENTITY_ACTIVE_LABEL_MISMATCH_PENALTY
            if active
            else HAND_IDENTITY_RECOVER_LABEL_MISMATCH_PENALTY
        )
        cost += label_penalty * label_score

    return cost


def hand_track_is_active(slot, prev_landmarks, missing_timers):
    return (
        prev_landmarks[slot] is not None
        and missing_timers[slot] <= DETECTION_GRACE_SECONDS * 2.0
    )


def hand_track_is_recent(slot, prev_landmarks, missing_timers):
    return (
        prev_landmarks[slot] is not None
        and missing_timers[slot] <= HAND_RECOVERY_LAST_POSITION_SECONDS
    )


def two_hand_assignment_order(
    detected_hands,
    prev_positions,
    prev_landmarks,
    missing_timers,
    identity_anchors,
    identity_labels,
    prev_deltas=None,
):
    if len(detected_hands) != 2:
        return None

    slot_count = min(2, len(prev_positions))
    if slot_count < 2:
        return None

    trackable_slots = [
        slot
        for slot in range(slot_count)
        if hand_track_is_active(slot, prev_landmarks, missing_timers)
        or hand_track_is_recent(slot, prev_landmarks, missing_timers)
        or identity_anchors[slot] is not None
    ]
    if not trackable_slots:
        return None

    orders = ((0, 1), (1, 0))

    def total_cost(order):
        return sum(
            hand_assignment_cost(
                slot,
                detected_hands[order[slot]],
                prev_positions,
                prev_landmarks,
                missing_timers,
                identity_anchors,
                identity_labels,
                prev_deltas,
            )
            for slot in range(slot_count)
        )

    def continuity_cost(order):
        cost = 0.0
        for slot in range(slot_count):
            if hand_track_is_active(slot, prev_landmarks, missing_timers):
                hand = detected_hands[order[slot]]
                cost += distance_sq(prev_positions[slot], (hand["x"], hand["y"]))
            elif hand_track_is_recent(slot, prev_landmarks, missing_timers):
                hand = detected_hands[order[slot]]
                cost += HAND_RECOVERY_POSITION_WEIGHT * distance_sq(
                    prev_positions[slot], (hand["x"], hand["y"])
                )
        return cost

    def direction_cost(order):
        cost = 0.0
        usable = 0
        if prev_deltas is None:
            return None
        for slot in range(slot_count):
            if not hand_track_is_recent(slot, prev_landmarks, missing_timers):
                continue
            delta = prev_deltas[slot]
            delta_len = vector_length(delta)
            if delta_len < HAND_ASSIGNMENT_DIRECTION_MIN_DELTA:
                continue
            hand = detected_hands[order[slot]]
            candidate = (
                hand["x"] - prev_positions[slot][0],
                hand["y"] - prev_positions[slot][1],
            )
            candidate_len = vector_length(candidate)
            if candidate_len < HAND_ASSIGNMENT_DIRECTION_MIN_DELTA:
                continue
            dot = (
                delta[0] * candidate[0] + delta[1] * candidate[1]
            ) / (delta_len * candidate_len)
            cost += 1.0 - max(-1.0, min(1.0, dot))
            usable += 1
        if usable < 2:
            return None
        return cost

    hand_separation = math.sqrt(
        distance_sq(
            (detected_hands[0]["x"], detected_hands[0]["y"]),
            (detected_hands[1]["x"], detected_hands[1]["y"]),
        )
    )

    total_costs = {order: total_cost(order) for order in orders}
    continuity_costs = {order: continuity_cost(order) for order in orders}
    direction_costs = {order: direction_cost(order) for order in orders}
    best_order = min(orders, key=lambda order: total_costs[order])
    continuity_order = min(orders, key=lambda order: continuity_costs[order])
    valid_direction_orders = [
        order for order in orders if direction_costs[order] is not None
    ]

    both_tracks_active = all(
        hand_track_is_recent(slot, prev_landmarks, missing_timers)
        for slot in range(slot_count)
    )
    if valid_direction_orders and hand_separation < HAND_ASSIGNMENT_AMBIGUOUS_SEPARATION:
        direction_order = min(valid_direction_orders, key=lambda order: direction_costs[order])
        other_order = orders[1] if direction_order == orders[0] else orders[0]
        if (
            direction_costs[other_order] is not None
            and direction_costs[other_order] - direction_costs[direction_order]
            > HAND_ASSIGNMENT_DIRECTION_OVERRIDE_MARGIN
            and continuity_costs[direction_order] - continuity_costs[continuity_order]
            <= HAND_ASSIGNMENT_DIRECTION_MAX_CONTINUITY_PENALTY
        ):
            return direction_order

    if both_tracks_active and best_order != continuity_order:
        continuity_advantage = (
            continuity_costs[best_order] - continuity_costs[continuity_order]
        )
        if continuity_advantage > HAND_ASSIGNMENT_CONTINUITY_OVERRIDE_MARGIN:
            return continuity_order
        improvement = total_costs[continuity_order] - total_costs[best_order]
        if improvement < HAND_ASSIGNMENT_ACTIVE_SWAP_MARGIN:
            return continuity_order

    return best_order


def single_hand_assignment_slot(
    hand,
    prev_positions,
    prev_landmarks,
    missing_timers,
    identity_anchors,
    identity_labels,
    max_hands,
    prev_deltas=None,
):
    hand_position = (hand["x"], hand["y"])
    active_slots = [
        slot
        for slot in range(min(max_hands, len(prev_positions)))
        if hand_track_is_active(slot, prev_landmarks, missing_timers)
        or hand_track_is_recent(slot, prev_landmarks, missing_timers)
    ]
    if active_slots:
        continuity = sorted(
            (distance_sq(prev_positions[slot], hand_position), slot)
            for slot in active_slots
        )
        best_dist, best_slot = continuity[0]
        if len(active_slots) > 1:
            return best_slot
        if best_dist <= HAND_SINGLE_ACTIVE_CONTINUITY_MAX_DIST:
            return best_slot

    recovery_slots = [
        slot
        for slot in range(min(max_hands, len(identity_anchors)))
        if identity_anchors[slot] is not None
    ]
    if not recovery_slots:
        return 0 if max_hands > 0 else None

    return min(
        recovery_slots,
        key=lambda slot: hand_assignment_cost(
            slot,
            hand,
            prev_positions,
            prev_landmarks,
            missing_timers,
            identity_anchors,
            identity_labels,
            prev_deltas,
        ),
    )


def pose_slot_assignment(
    detected_hands,
    pose_slots,
    max_hands,
    prev_positions=None,
    prev_landmarks=None,
    missing_timers=None,
):
    if not pose_slots:
        return None

    valid_slots = [
        slot for slot, arm in enumerate(pose_slots[:max_hands]) if arm is not None
    ]
    if not valid_slots or not detected_hands:
        return None

    max_dist_sq = HAND_POSE_WRIST_MAX_DIST * HAND_POSE_WRIST_MAX_DIST

    def cost(slot, detection_index):
        return distance_sq(
            pose_slots[slot]["wrist"],
            (detected_hands[detection_index]["x"], detected_hands[detection_index]["y"]),
        )

    def continuity_slot_for_hand(hand):
        if (
            prev_positions is None
            or prev_landmarks is None
            or missing_timers is None
        ):
            return None
        active_slots = [
            slot
            for slot in valid_slots
            if hand_track_is_active(slot, prev_landmarks, missing_timers)
            or hand_track_is_recent(slot, prev_landmarks, missing_timers)
        ]
        if not active_slots:
            return None
        return min(
            active_slots,
            key=lambda slot: distance_sq(prev_positions[slot], (hand["x"], hand["y"])),
        )

    def continuity_order(slots):
        if (
            prev_positions is None
            or prev_landmarks is None
            or missing_timers is None
            or len(slots) < 2
        ):
            return None
        if not all(
            hand_track_is_active(slot, prev_landmarks, missing_timers)
            or hand_track_is_recent(slot, prev_landmarks, missing_timers)
            for slot in slots
        ):
            return None
        orders = ((0, 1), (1, 0))
        return min(
            orders,
            key=lambda order: sum(
                distance_sq(
                    prev_positions[slot],
                    (
                        detected_hands[order[index]]["x"],
                        detected_hands[order[index]]["y"],
                    ),
                )
                for index, slot in enumerate(slots)
            ),
        )

    if len(detected_hands) == 1:
        costs = sorted((cost(slot, 0), slot) for slot in valid_slots)
        best_cost, best_slot = costs[0]
        if best_cost > max_dist_sq:
            return None
        if len(costs) > 1 and costs[1][0] - best_cost < HAND_POSE_ASSIGNMENT_MARGIN:
            continuity_slot = continuity_slot_for_hand(detected_hands[0])
            if continuity_slot is None:
                return None
            best_slot = continuity_slot

        assigned = [None for _ in range(max_hands)]
        assigned[best_slot] = detected_hands[0]
        return assigned

    if max_hands < 2 or len(valid_slots) < 2 or len(detected_hands) < 2:
        return None

    slots = valid_slots[:2]
    orders = ((0, 1), (1, 0))
    scored_orders = []
    for order in orders:
        slot_costs = [cost(slot, order[index]) for index, slot in enumerate(slots)]
        if any(slot_cost > max_dist_sq for slot_cost in slot_costs):
            continue
        scored_orders.append((sum(slot_costs), order))

    if not scored_orders:
        return None

    scored_orders.sort(key=lambda item: item[0])
    best_cost, best_order = scored_orders[0]
    if (
        len(scored_orders) > 1
        and scored_orders[1][0] - best_cost < HAND_POSE_ASSIGNMENT_MARGIN
    ):
        best_order = continuity_order(slots)
        if best_order is None:
            return None

    assigned = [None for _ in range(max_hands)]
    for index, slot in enumerate(slots):
        assigned[slot] = detected_hands[best_order[index]]
    return assigned


def legacy_assign_detected_hands_unused(
    detected_hands,
    prev_positions,
    prev_landmarks,
    missing_timers,
    max_hands,
    identity_anchors,
    identity_labels,
    prev_deltas=None,
    pose_slots=None,
):
    assigned = [None for _ in range(max_hands)]
    if not detected_hands:
        return assigned

    pose_assigned = pose_slot_assignment(
        detected_hands,
        pose_slots,
        max_hands,
        prev_positions,
        prev_landmarks,
        missing_timers,
    )
    if pose_assigned is not None:
        return pose_assigned

    if identity_anchors and identity_anchors[0] is None and len(detected_hands) >= 2:
        ordered = sorted(detected_hands[:max_hands], key=lambda hand: hand["x"], reverse=True)
        if abs(ordered[0]["x"] - ordered[1]["x"]) >= HAND_IDENTITY_MIN_SEPARATION:
            for slot, hand in enumerate(ordered[:max_hands]):
                assigned[slot] = hand
            return assigned

    if len(detected_hands) == 1:
        slot = single_hand_assignment_slot(
            detected_hands[0],
            prev_positions,
            prev_landmarks,
            missing_timers,
            identity_anchors,
            identity_labels,
            max_hands,
            prev_deltas,
        )
        if slot is not None and 0 <= slot < max_hands:
            assigned[slot] = detected_hands[0]
        return assigned

    two_hand_order = two_hand_assignment_order(
        detected_hands,
        prev_positions,
        prev_landmarks,
        missing_timers,
        identity_anchors,
        identity_labels,
        prev_deltas,
    )
    if two_hand_order is not None:
        for slot, detection_index in enumerate(two_hand_order):
            assigned[slot] = detected_hands[detection_index]
        return assigned

    unused_detections = set(range(len(detected_hands)))
    trackable_slots = [
        index
        for index in range(max_hands)
        if (
            prev_landmarks[index] is not None
            and missing_timers[index] <= DETECTION_GRACE_SECONDS * 2.0
        )
        or hand_track_is_recent(index, prev_landmarks, missing_timers)
        or identity_anchors[index] is not None
    ]
    pairs = []
    for slot in trackable_slots:
        for detection_index in unused_detections:
            hand = detected_hands[detection_index]
            pairs.append(
                (
                    hand_assignment_cost(
                        slot,
                        hand,
                        prev_positions,
                        prev_landmarks,
                        missing_timers,
                        identity_anchors,
                        identity_labels,
                        prev_deltas,
                    ),
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
    if empty_slots:
        if any(identity_anchors[slot] is not None for slot in empty_slots):
            recovery_pairs = []
            for slot in empty_slots:
                if identity_anchors[slot] is None:
                    continue
                for detection_index in unused_detections:
                    recovery_pairs.append(
                        (
                            hand_assignment_cost(
                                slot,
                                detected_hands[detection_index],
                                prev_positions,
                                prev_landmarks,
                                missing_timers,
                                identity_anchors,
                                identity_labels,
                                prev_deltas,
                            ),
                            slot,
                            detection_index,
                        )
                    )
            for _, slot, detection_index in sorted(recovery_pairs):
                if assigned[slot] is not None or detection_index not in unused_detections:
                    continue
                assigned[slot] = detected_hands[detection_index]
                unused_detections.remove(detection_index)

        remaining_slots = [
            index for index in range(max_hands) if assigned[index] is None
        ]
        new_hands = sorted(
            (detected_hands[index] for index in unused_detections),
            key=lambda hand: hand["x"],
            reverse=True,
        )
        for slot, hand in zip(remaining_slots, new_hands):
            assigned[slot] = hand

    return assigned


def assignment_metadata(reason="LOST", confidence=0.0, margin=0.0, cost=0.0):
    return {
        "reason": reason,
        "identity_confidence": clamp01(confidence),
        "margin": margin,
        "cost": cost,
    }


def hand_position(hand):
    return hand["x"], hand["y"]


def slot_has_recent_identity(slot, prev_landmarks, missing_timers, identity_anchors):
    return (
        hand_track_is_active(slot, prev_landmarks, missing_timers)
        or hand_track_is_recent(slot, prev_landmarks, missing_timers)
        or (
            slot < len(identity_anchors)
            and identity_anchors[slot] is not None
            and missing_timers[slot] <= HAND_RECOVERY_LAST_POSITION_SECONDS
        )
    )


def predicted_slot_position(slot, prev_positions, prev_deltas):
    if prev_deltas is None or slot >= len(prev_deltas):
        return prev_positions[slot]
    delta = prev_deltas[slot]
    return (
        prev_positions[slot][0] + delta[0] * HAND_ASSIGNMENT_PREDICTION_FRAMES,
        prev_positions[slot][1] + delta[1] * HAND_ASSIGNMENT_PREDICTION_FRAMES,
    )


def fresh_assignment_cost(
    slot,
    hand,
    prev_positions,
    prev_landmarks,
    missing_timers,
    identity_anchors,
    identity_labels,
    prev_deltas,
    pose_slots,
    body_scale,
):
    position = hand_position(hand)
    cost = 0.0
    pose_used = False
    continuity_used = False

    pose_arm = (
        pose_slots[slot]
        if pose_slots is not None and slot < len(pose_slots)
        else None
    )
    if pose_arm is not None:
        pose_used = True
        pose_confidence = pose_arm.get("confidence", 0.65)
        pose_cost = normalized_distance_sq(position, pose_arm["wrist"], body_scale)
        cost += HAND_ASSIGNMENT_POSE_WEIGHT * pose_cost / max(0.35, pose_confidence)
    else:
        cost += HAND_ASSIGNMENT_NO_POSE_PENALTY

    if hand_track_is_active(slot, prev_landmarks, missing_timers):
        continuity_used = True
        cost += HAND_ASSIGNMENT_CONTINUITY_WEIGHT * normalized_distance_sq(
            prev_positions[slot], position, body_scale
        )
        cost += HAND_ASSIGNMENT_PREDICTION_WEIGHT * normalized_distance_sq(
            predicted_slot_position(slot, prev_positions, prev_deltas),
            position,
            body_scale,
        )
    elif hand_track_is_recent(slot, prev_landmarks, missing_timers):
        continuity_used = True
        age_scale = 1.0 + missing_timers[slot] / max(0.001, HAND_RECOVERY_LAST_POSITION_SECONDS)
        cost += HAND_ASSIGNMENT_RECENT_WEIGHT * normalized_distance_sq(
            prev_positions[slot], position, body_scale
        ) * age_scale
    else:
        cost += HAND_ASSIGNMENT_NO_TRACK_PENALTY

    if slot < len(identity_anchors) and identity_anchors[slot] is not None:
        cost += 0.08 * normalized_distance_sq(
            identity_anchors[slot], position, body_scale
        )

    label = hand.get("label")
    if (
        slot < len(identity_labels)
        and identity_labels[slot]
        and label
        and identity_labels[slot] != label
    ):
        cost += HAND_ASSIGNMENT_LABEL_WEIGHT * hand.get("label_score", 1.0)

    return cost, pose_used, continuity_used


def assignment_label_mismatch_count(order, detected_hands, identity_labels):
    mismatches = 0
    for slot, hand_index in enumerate(order):
        if slot >= len(identity_labels):
            continue
        expected_label = identity_labels[slot]
        hand_label = detected_hands[hand_index].get("label")
        if expected_label and hand_label and expected_label != hand_label:
            mismatches += 1
    return mismatches


def classify_assignment_reason(slot_costs):
    if any(item["pose_used"] for item in slot_costs):
        return "POSE", IDENTITY_CONFIDENCE_POSE
    if any(item["continuity_used"] for item in slot_costs):
        return "CONTINUITY", IDENTITY_CONFIDENCE_CONTINUITY
    return "INIT", IDENTITY_CONFIDENCE_INIT


def decorate_assigned_hands(assigned, metadata):
    decorated = []
    for slot, hand in enumerate(assigned):
        if hand is None:
            decorated.append(None)
            continue
        copy = dict(hand)
        copy["assignment_reason"] = metadata[slot]["reason"]
        copy["identity_confidence"] = metadata[slot]["identity_confidence"]
        copy["assignment_margin"] = metadata[slot]["margin"]
        decorated.append(copy)
    return decorated


def hold_previous_assignment(max_hands, confidence=IDENTITY_CONFIDENCE_HOLD, margin=0.0):
    return (
        [None for _ in range(max_hands)],
        [
            assignment_metadata("HOLD", confidence, margin)
            for _ in range(max_hands)
        ],
    )


def reset_pose_conflict_state(pose_conflict_state):
    if pose_conflict_state is None:
        return
    pose_conflict_state["order"] = None
    pose_conflict_state["count"] = 0


def pose_conflict_persisted(pose_conflict_state, order):
    if pose_conflict_state is None:
        return True

    order = tuple(order)
    if pose_conflict_state.get("order") == order:
        pose_conflict_state["count"] = pose_conflict_state.get("count", 0) + 1
    else:
        pose_conflict_state["order"] = order
        pose_conflict_state["count"] = 1

    return (
        pose_conflict_state["count"]
        >= HAND_ASSIGNMENT_POSE_CONFLICT_CONFIRM_FRAMES
    )


def hold_pose_conflict_assignment(max_hands, margin, cost):
    return (
        [None for _ in range(max_hands)],
        [
            assignment_metadata(
                "POSE_CONFLICT",
                IDENTITY_CONFIDENCE_HOLD,
                margin,
                cost,
            )
            for _ in range(max_hands)
        ],
    )


def initial_body_side_assignment(detected_hands, max_hands):
    if max_hands < 2 or len(detected_hands) < 2:
        return None

    ordered = sorted(detected_hands[:max_hands], key=lambda hand: hand["x"], reverse=True)
    if abs(ordered[0]["x"] - ordered[1]["x"]) < HAND_IDENTITY_MIN_SEPARATION:
        return None

    assigned = [None for _ in range(max_hands)]
    metadata = [assignment_metadata() for _ in range(max_hands)]
    for slot, hand in enumerate(ordered[:max_hands]):
        assigned[slot] = hand
        metadata[slot] = assignment_metadata(
            "INIT", IDENTITY_CONFIDENCE_INIT, 1.0, 0.0
        )
    return decorate_assigned_hands(assigned, metadata), metadata


def assign_two_hands_globally(
    detected_hands,
    prev_positions,
    prev_landmarks,
    missing_timers,
    max_hands,
    identity_anchors,
    identity_labels,
    prev_deltas,
    pose_slots,
    body_scale,
    pose_conflict_state=None,
):
    slot_count = min(2, max_hands)
    if slot_count < 2 or len(detected_hands) < 2:
        return None

    orders = ((0, 1), (1, 0))
    scored = []
    for order in orders:
        slot_costs = []
        total = 0.0
        for slot in range(slot_count):
            cost, pose_used, continuity_used = fresh_assignment_cost(
                slot,
                detected_hands[order[slot]],
                prev_positions,
                prev_landmarks,
                missing_timers,
                identity_anchors,
                identity_labels,
                prev_deltas,
                pose_slots,
                body_scale,
            )
            total += cost
            slot_costs.append(
                {
                    "cost": cost,
                    "pose_used": pose_used,
                    "continuity_used": continuity_used,
                }
            )
        scored.append((total, order, slot_costs))

    scored.sort(key=lambda item: item[0])
    best_cost, best_order, best_slot_costs = scored[0]
    second_cost = scored[1][0]
    margin = second_cost - best_cost

    has_recent_identity = any(
        slot_has_recent_identity(slot, prev_landmarks, missing_timers, identity_anchors)
        for slot in range(slot_count)
    )
    has_identity_anchor = any(
        slot < len(identity_anchors) and identity_anchors[slot] is not None
        for slot in range(slot_count)
    )
    has_pose_evidence = any(
        pose_slots is not None
        and slot < len(pose_slots)
        and pose_slots[slot] is not None
        for slot in range(slot_count)
    )
    if has_pose_evidence and has_recent_identity and all(
        hand_track_is_active(slot, prev_landmarks, missing_timers)
        or hand_track_is_recent(slot, prev_landmarks, missing_timers)
        for slot in range(slot_count)
    ):
        continuity_scores = []
        for order in orders:
            continuity_scores.append(
                (
                    sum(
                        normalized_distance_sq(
                            prev_positions[slot],
                            hand_position(detected_hands[order[slot]]),
                            body_scale,
                        )
                        for slot in range(slot_count)
                    ),
                    order,
                )
            )
        continuity_scores.sort(key=lambda item: item[0])
        continuity_order = continuity_scores[0][1]
        if best_order != continuity_order:
            if (
                assignment_label_mismatch_count(
                    best_order, detected_hands, identity_labels
                )
                >= slot_count
                or
                margin < HAND_ASSIGNMENT_POSE_CONFLICT_MARGIN
                or not pose_conflict_persisted(pose_conflict_state, best_order)
            ):
                return hold_pose_conflict_assignment(max_hands, margin, best_cost)
        else:
            reset_pose_conflict_state(pose_conflict_state)
    hand_separation = math.sqrt(
        distance_sq(
            hand_position(detected_hands[0]),
            hand_position(detected_hands[1]),
        )
    )
    if pose_slots is not None and not has_pose_evidence and has_identity_anchor:
        reset_pose_conflict_state(pose_conflict_state)
        return hold_previous_assignment(max_hands, margin=margin)

    if (
        has_pose_evidence
        and has_recent_identity
        and hand_separation < HAND_ASSIGNMENT_AMBIGUOUS_SEPARATION
        and margin < HAND_ASSIGNMENT_CLOSE_HOLD_MARGIN
    ):
        reset_pose_conflict_state(pose_conflict_state)
        return hold_previous_assignment(max_hands, margin=margin)

    if has_pose_evidence and has_recent_identity and margin < HAND_ASSIGNMENT_CLEAR_MARGIN:
        reset_pose_conflict_state(pose_conflict_state)
        return hold_previous_assignment(max_hands, margin=margin)

    clear = (
        margin >= HAND_ASSIGNMENT_CLEAR_MARGIN
        or best_cost <= HAND_ASSIGNMENT_UNCERTAIN_MARGIN
    )

    assigned = [None for _ in range(max_hands)]
    metadata = [assignment_metadata() for _ in range(max_hands)]
    reason, confidence = classify_assignment_reason(best_slot_costs)
    if not clear:
        confidence = min(confidence, IDENTITY_CONFIDENCE_FALLBACK)
        reason = "FALLBACK"

    for slot in range(slot_count):
        assigned[slot] = detected_hands[best_order[slot]]
        metadata[slot] = assignment_metadata(
            reason,
            confidence,
            margin,
            best_slot_costs[slot]["cost"],
        )
    reset_pose_conflict_state(pose_conflict_state)
    return decorate_assigned_hands(assigned, metadata), metadata


def assign_single_hand_globally(
    hand,
    prev_positions,
    prev_landmarks,
    missing_timers,
    max_hands,
    identity_anchors,
    identity_labels,
    prev_deltas,
    pose_slots,
    body_scale,
    pose_conflict_state=None,
):
    has_recent_identity = any(
        slot_has_recent_identity(slot, prev_landmarks, missing_timers, identity_anchors)
        for slot in range(max_hands)
    )
    has_identity_anchor = any(
        slot < len(identity_anchors) and identity_anchors[slot] is not None
        for slot in range(max_hands)
    )
    has_pose_evidence = any(
        pose_slots is not None
        and slot < len(pose_slots)
        and pose_slots[slot] is not None
        for slot in range(max_hands)
    )
    if pose_slots is not None and not has_pose_evidence and has_identity_anchor:
        return hold_previous_assignment(max_hands)

    pose_candidates = []
    for slot in range(max_hands):
        pose_arm = (
            pose_slots[slot]
            if pose_slots is not None and slot < len(pose_slots)
            else None
        )
        if pose_arm is None:
            continue
        pose_candidates.append(
            (
                normalized_distance_sq(hand_position(hand), pose_arm["wrist"], body_scale),
                slot,
            )
        )
    if pose_candidates:
        pose_candidates.sort()
        best_pose_cost, best_pose_slot = pose_candidates[0]
        second_pose_cost = (
            pose_candidates[1][0]
            if len(pose_candidates) > 1
            else best_pose_cost + 1.0
        )
        pose_margin = second_pose_cost - best_pose_cost
        if (
            best_pose_cost <= HAND_ASSIGNMENT_SINGLE_STRONG_POSE_COST
            and pose_margin >= HAND_ASSIGNMENT_SINGLE_POSE_MARGIN
            and not has_recent_identity
        ):
            assigned = [None for _ in range(max_hands)]
            metadata = [assignment_metadata() for _ in range(max_hands)]
            assigned[best_pose_slot] = hand
            metadata[best_pose_slot] = assignment_metadata(
                "POSE",
                IDENTITY_CONFIDENCE_POSE,
                pose_margin,
                best_pose_cost,
            )
            return decorate_assigned_hands(assigned, metadata), metadata

    scored = []
    for slot in range(max_hands):
        cost, pose_used, continuity_used = fresh_assignment_cost(
            slot,
            hand,
            prev_positions,
            prev_landmarks,
            missing_timers,
            identity_anchors,
            identity_labels,
            prev_deltas,
            pose_slots,
            body_scale,
        )
        scored.append(
            {
                "slot": slot,
                "cost": cost,
                "pose_used": pose_used,
                "continuity_used": continuity_used,
            }
        )

    scored.sort(key=lambda item: item["cost"])
    best = scored[0]
    second_cost = scored[1]["cost"] if len(scored) > 1 else best["cost"] + 1.0
    margin = second_cost - best["cost"]
    recent_slot_count = sum(
        1
        for slot in range(max_hands)
        if slot_has_recent_identity(slot, prev_landmarks, missing_timers, identity_anchors)
    )
    if has_pose_evidence and has_recent_identity and best["pose_used"]:
        continuity_candidates = []
        for slot in range(max_hands):
            if not slot_has_recent_identity(
                slot, prev_landmarks, missing_timers, identity_anchors
            ):
                continue
            continuity_candidates.append(
                (
                    normalized_distance_sq(
                        prev_positions[slot],
                        hand_position(hand),
                        body_scale,
                    ),
                    slot,
                )
            )
        if continuity_candidates:
            continuity_candidates.sort(key=lambda item: item[0])
            continuity_cost, continuity_slot = continuity_candidates[0]
            if (
                best["slot"] != continuity_slot
                and continuity_cost <= HAND_ASSIGNMENT_SINGLE_POSE_CONFLICT_MAX_COST
            ):
                return hold_pose_conflict_assignment(
                    max_hands,
                    margin,
                    best["cost"],
                )

    if has_pose_evidence and recent_slot_count >= 2:
        return hold_previous_assignment(max_hands, margin=margin)

    if has_pose_evidence and has_recent_identity and margin < HAND_ASSIGNMENT_CLEAR_MARGIN:
        return hold_previous_assignment(max_hands, margin=margin)

    clear = (
        margin >= HAND_ASSIGNMENT_CLEAR_MARGIN
        or (
            best["continuity_used"]
            and best["cost"] <= HAND_ASSIGNMENT_UNCERTAIN_MARGIN
        )
        or (
            best["pose_used"]
            and best["cost"] <= HAND_ASSIGNMENT_UNCERTAIN_MARGIN
        )
    )

    assigned = [None for _ in range(max_hands)]
    metadata = [assignment_metadata() for _ in range(max_hands)]
    reason, confidence = classify_assignment_reason([best])
    if not clear:
        confidence = min(confidence, IDENTITY_CONFIDENCE_FALLBACK)
        reason = "FALLBACK"

    assigned[best["slot"]] = hand
    metadata[best["slot"]] = assignment_metadata(
        reason,
        confidence,
        margin,
        best["cost"],
    )
    return decorate_assigned_hands(assigned, metadata), metadata


def assign_detected_hands_with_debug(
    detected_hands,
    prev_positions,
    prev_landmarks,
    missing_timers,
    max_hands,
    identity_anchors,
    identity_labels,
    prev_deltas=None,
    pose_slots=None,
    pose_conflict_state=None,
):
    metadata = [assignment_metadata() for _ in range(max_hands)]
    if not detected_hands:
        reset_pose_conflict_state(pose_conflict_state)
        for slot in range(max_hands):
            if slot_has_recent_identity(
                slot, prev_landmarks, missing_timers, identity_anchors
            ):
                metadata[slot] = assignment_metadata(
                    "PREDICTED", IDENTITY_CONFIDENCE_PREDICTED
                )
        return [None for _ in range(max_hands)], metadata

    body_scale = pose_body_scale(pose_slots)
    limited_hands = detected_hands[:max_hands]

    has_established_identity = any(
        identity_anchors[slot] is not None
        for slot in range(min(max_hands, len(identity_anchors)))
    )
    if not has_established_identity:
        initial = initial_body_side_assignment(limited_hands, max_hands)
        if initial is not None:
            return initial
        return (
            [None for _ in range(max_hands)],
            [
                assignment_metadata("INIT_WAIT", 0.0)
                for _ in range(max_hands)
            ],
        )

    if len(limited_hands) >= 2 and max_hands >= 2:
        assigned = assign_two_hands_globally(
            limited_hands,
            prev_positions,
            prev_landmarks,
            missing_timers,
            max_hands,
            identity_anchors,
            identity_labels,
            prev_deltas,
            pose_slots,
            body_scale,
            pose_conflict_state,
        )
        if assigned is not None:
            return assigned

    reset_pose_conflict_state(pose_conflict_state)
    return assign_single_hand_globally(
        limited_hands[0],
        prev_positions,
        prev_landmarks,
        missing_timers,
        max_hands,
        identity_anchors,
        identity_labels,
        prev_deltas,
        pose_slots,
        body_scale,
        pose_conflict_state,
    )


def assign_detected_hands(
    detected_hands,
    prev_positions,
    prev_landmarks,
    missing_timers,
    max_hands,
    identity_anchors,
    identity_labels,
    prev_deltas=None,
    pose_slots=None,
    pose_conflict_state=None,
):
    assigned, _ = assign_detected_hands_with_debug(
        detected_hands,
        prev_positions,
        prev_landmarks,
        missing_timers,
        max_hands,
        identity_anchors,
        identity_labels,
        prev_deltas,
        pose_slots,
        pose_conflict_state,
    )
    return assigned


def create_control_socket(port):
    control_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    control_sock.setblocking(False)
    try:
        control_sock.bind(("127.0.0.1", port))
    except OSError:
        control_sock.close()
        return None
    return control_sock


def poll_control_commands(control_sock):
    if control_sock is None:
        return False, False

    should_reset = False
    should_start_camera = False
    while True:
        try:
            data, _ = control_sock.recvfrom(CONTROL_COMMAND_BYTES)
        except BlockingIOError:
            break
        except OSError:
            break
        command = data.strip().decode("ascii", errors="ignore")
        if command == "SGCAMERA_START":
            should_start_camera = True
        elif command == "SGREGISTER_RESET":
            should_reset = True
    return should_reset, should_start_camera


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


def draw_pose_slots(frame, pose_slots):
    if not pose_slots:
        return

    height, width = frame.shape[:2]
    colors = [(255, 120, 45), (55, 210, 255)]
    for slot, arm in enumerate(pose_slots[:2]):
        if arm is None:
            continue

        color = colors[slot % len(colors)]
        points = [
            arm.get("wrist"),
            arm.get("elbow"),
            arm.get("shoulder"),
        ]
        pixels = [
            to_pixel(point, width, height)
            for point in points
            if point is not None
        ]
        for index, pixel in enumerate(pixels):
            cv2.circle(frame, pixel, 8 if index == 0 else 5, color, 2, cv2.LINE_AA)
            if index > 0:
                cv2.line(frame, pixels[index - 1], pixel, color, 2, cv2.LINE_AA)
        if pixels:
            cv2.putText(
                frame,
                f"ARM{slot + 1}",
                (pixels[0][0] + 10, pixels[0][1] - 10),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.46,
                color,
                1,
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
    parser.add_argument("--control-port", type=int, default=5007)
    parser.add_argument("--status-host", default="127.0.0.1")
    parser.add_argument("--status-port", type=int, default=5008)
    parser.add_argument("--show-window", action="store_true")
    parser.add_argument("--start-paused", action="store_true")
    parser.add_argument("--simulate", action="store_true")
    parser.add_argument(
        "--simulate-scenario",
        choices=("cross", "label-flip", "dropout", "noisy-cross"),
        default="cross",
    )
    parser.add_argument("--camera", type=int, default=0)
    parser.add_argument("--max-hands", type=int, default=2)
    parser.add_argument("--model", default=default_model_path())
    parser.add_argument("--pose-model", default=default_pose_model_path())
    parser.add_argument("--disable-pose", action="store_true")
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


def send_status(args, message):
    try:
        status_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        status_sock.sendto(
            message.encode("ascii"), (args.status_host, args.status_port)
        )
        status_sock.close()
    except OSError:
        pass


def simulated_motion_points(x, y):
    return [
        (clamp01(x), clamp01(y)),
        (clamp01(x - 0.018), clamp01(y + 0.010)),
        (clamp01(x + 0.018), clamp01(y + 0.010)),
        (clamp01(x - 0.026), clamp01(y - 0.018)),
        (clamp01(x + 0.026), clamp01(y - 0.018)),
        (clamp01(x), clamp01(y - 0.040)),
        (clamp01(x - 0.038), clamp01(y + 0.034)),
        (clamp01(x), clamp01(y + 0.042)),
        (clamp01(x + 0.038), clamp01(y + 0.034)),
    ]


def simulated_hand(raw_id, x, y, label, label_score=0.92):
    return {
        "raw_id": raw_id,
        "x": clamp01(x),
        "y": clamp01(y),
        "points": simulated_motion_points(x, y),
        "motion_scale": 1.0,
        "confidence": 0.95,
        "label": label,
        "label_score": label_score,
    }


def simulated_detections(t, scenario):
    phase = math.sin(t * 0.72)
    wiggle = math.sin(t * 1.9) * 0.035
    hand_a = simulated_hand("A", 0.50 + phase * 0.34, 0.46 + wiggle, "Right")
    hand_b = simulated_hand("B", 0.50 - phase * 0.34, 0.56 - wiggle, "Left")

    if scenario in ("label-flip", "noisy-cross") and abs(phase) < 0.26:
        hand_a["label"], hand_b["label"] = hand_b["label"], hand_a["label"]
        hand_a["label_score"] = 0.98
        hand_b["label_score"] = 0.98

    if scenario in ("dropout", "noisy-cross"):
        dropout_phase = int(t * 1.15) % 8
        if dropout_phase == 2:
            return [hand_b]
        if dropout_phase == 5:
            return [hand_a]

    if scenario == "noisy-cross":
        noise = math.sin(t * 17.0) * 0.022
        hand_a["x"] = clamp01(hand_a["x"] + noise)
        hand_b["x"] = clamp01(hand_b["x"] - noise * 0.8)
        hand_a["y"] = clamp01(hand_a["y"] + math.cos(t * 13.0) * 0.018)
        hand_b["y"] = clamp01(hand_b["y"] - math.cos(t * 11.0) * 0.018)
        hand_a["points"] = simulated_motion_points(hand_a["x"], hand_a["y"])
        hand_b["points"] = simulated_motion_points(hand_b["x"], hand_b["y"])

    detections = [hand_a, hand_b]
    if int(t * 3.0) % 2 == 0 or abs(phase) < 0.18:
        detections.reverse()
    return detections


def draw_simulated_frame(frame, detections, assigned_hands, t):
    frame[:] = (15, 18, 22)
    height, width = frame.shape[:2]
    cv2.rectangle(frame, (10, 10), (width - 10, height - 10), (70, 80, 96), 1)
    cv2.line(frame, (width // 2, 10), (width // 2, height - 10), (55, 58, 64), 1)

    raw_colors = {"A": (60, 170, 255), "B": (70, 235, 130)}
    for hand in detections:
        color = raw_colors.get(hand.get("raw_id"), (220, 220, 220))
        px, py = to_pixel((hand["x"], hand["y"]), width, height)
        cv2.circle(frame, (px, py), 17, color, 2, cv2.LINE_AA)
        cv2.circle(frame, (px, py), 5, color, -1, cv2.LINE_AA)
        cv2.putText(
            frame,
            f"raw {hand.get('raw_id', '?')} {hand.get('label', '-')}",
            (px + 12, py - 12),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.42,
            color,
            1,
            cv2.LINE_AA,
        )

    slot_colors = [(255, 145, 40), (80, 240, 120)]
    for slot, hand in enumerate(assigned_hands[:2]):
        if hand is None:
            continue
        px, py = to_pixel((hand["x"], hand["y"]), width, height)
        color = slot_colors[slot]
        cv2.rectangle(frame, (px - 25, py - 25), (px + 25, py + 25), color, 2)
        cv2.putText(
            frame,
            f"HAND{slot + 1}",
            (px - 24, py + 39),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.48,
            color,
            1,
            cv2.LINE_AA,
        )

    cv2.putText(
        frame,
        f"SIM {t:05.2f}",
        (18, height - 18),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.50,
        (230, 230, 230),
        1,
        cv2.LINE_AA,
    )


def run_simulation(args, sock, target, preview_target, control_sock, preview_state):
    send_status(args, "SGCAMERA_READY")
    max_hands = max(1, min(args.max_hands, 2))
    prev_positions = [(0.5, 0.5) for _ in range(max_hands)]
    prev_deltas = [(0.0, 0.0) for _ in range(max_hands)]
    prev_landmarks = [None for _ in range(max_hands)]
    missing_timers = [DETECTION_GRACE_SECONDS for _ in range(max_hands)]
    identity_anchors = [None for _ in range(max_hands)]
    identity_labels = [None for _ in range(max_hands)]
    identity_candidate_state = {"signature": None, "count": 0}
    pose_conflict_state = {"order": None, "count": 0}
    frame = np.zeros((480, 640, 3), dtype=np.uint8)
    start_time = time.perf_counter()
    prev_time = time.perf_counter()

    try:
        while True:
            now = time.perf_counter()
            t = now - start_time
            dt = max(now - prev_time, 1.0 / 120.0)
            prev_time = now

            should_reset, _ = poll_control_commands(control_sock)
            if should_reset:
                prev_positions = [(0.5, 0.5) for _ in range(max_hands)]
                prev_deltas = [(0.0, 0.0) for _ in range(max_hands)]
                prev_landmarks = [None for _ in range(max_hands)]
                missing_timers = [DETECTION_GRACE_SECONDS for _ in range(max_hands)]
                identity_anchors = [None for _ in range(max_hands)]
                identity_labels = [None for _ in range(max_hands)]
                identity_candidate_state = {"signature": None, "count": 0}
                pose_conflict_state = {"order": None, "count": 0}

            detected_hands = simulated_detections(t, args.simulate_scenario)
            assigned_hands, assignment_metadata_list = assign_detected_hands_with_debug(
                detected_hands,
                prev_positions,
                prev_landmarks,
                missing_timers,
                max_hands,
                identity_anchors,
                identity_labels,
                prev_deltas,
                pose_conflict_state=pose_conflict_state,
            )
            register_assigned_hand_identity_stable(
                assigned_hands,
                identity_anchors,
                identity_labels,
                identity_candidate_state,
            )

            for hand_index in range(max_hands):
                hand = assigned_hands[hand_index]
                assignment_meta = assignment_metadata_list[hand_index]
                prev_x, prev_y = prev_positions[hand_index]
                if hand is None:
                    missing_timers[hand_index] += dt
                    prev_deltas[hand_index] = (
                        prev_deltas[hand_index][0] * HAND_ASSIGNMENT_DELTA_DECAY,
                        prev_deltas[hand_index][1] * HAND_ASSIGNMENT_DELTA_DECAY,
                    )
                    valid = (
                        prev_landmarks[hand_index] is not None
                        and missing_timers[hand_index] <= DETECTION_GRACE_SECONDS
                    )
                    x, y = prev_x, prev_y
                    dx = dy = speed = confidence = 0.0
                else:
                    x, y = hand["x"], hand["y"]
                    dx = x - prev_x
                    dy = y - prev_y
                    prev_deltas[hand_index] = (
                        prev_deltas[hand_index][0]
                        * (1.0 - HAND_ASSIGNMENT_DELTA_FOLLOW)
                        + dx * HAND_ASSIGNMENT_DELTA_FOLLOW,
                        prev_deltas[hand_index][1]
                        * (1.0 - HAND_ASSIGNMENT_DELTA_FOLLOW)
                        + dy * HAND_ASSIGNMENT_DELTA_FOLLOW,
                    )
                    speed = math.sqrt(dx * dx + dy * dy) / dt
                    confidence = min(
                        hand["confidence"],
                        hand.get(
                            "identity_confidence",
                            assignment_meta["identity_confidence"],
                        ),
                    )
                    prev_positions[hand_index] = (x, y)
                    prev_landmarks[hand_index] = hand["points"]
                    missing_timers[hand_index] = 0.0
                    valid = True

                packet = (
                    f"HAND{hand_index + 1} {1 if valid else 0} "
                    f"{x:.6f} {y:.6f} {dx:.6f} {dy:.6f} "
                    f"{speed:.6f} {confidence:.6f} "
                    f"0.000000 1.000000 {speed:.6f} "
                    f"{assignment_meta['reason']}"
                )
                sock.sendto(packet.encode("ascii"), target)

            draw_simulated_frame(frame, detected_hands, assigned_hands, t)
            send_preview_frame(sock, preview_target, frame, args, preview_state, now)

            if args.show_window:
                cv2.imshow("Hand UDP Sender Simulation", frame)
                if cv2.waitKey(1) & 0xFF == 27:
                    break
            time.sleep(1.0 / 60.0)
    finally:
        if control_sock is not None:
            control_sock.close()
        if args.show_window:
            cv2.destroyAllWindows()


def main():
    args = parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (args.host, args.port)
    preview_target = (args.preview_host, args.preview_port)
    control_sock = create_control_socket(args.control_port)
    preview_state = {"frame_id": 0, "last_time": 0.0}

    if args.simulate:
        run_simulation(args, sock, target, preview_target, control_sock, preview_state)
        return

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
    pose_landmarker = None
    if not args.disable_pose and args.pose_model and os.path.exists(args.pose_model):
        try:
            pose_options = vision.PoseLandmarkerOptions(
                base_options=python.BaseOptions(model_asset_path=args.pose_model),
                running_mode=vision.RunningMode.VIDEO,
                num_poses=1,
                min_pose_detection_confidence=0.35,
                min_pose_presence_confidence=0.35,
                min_tracking_confidence=0.35,
            )
            pose_landmarker = vision.PoseLandmarker.create_from_options(pose_options)
        except Exception as error:
            print(f"PoseLandmarker disabled: {error}", flush=True)
    send_status(args, "SGCAMERA_READY")

    capture = None
    try:
        if args.start_paused:
            while True:
                _, should_start_camera = poll_control_commands(control_sock)
                if should_start_camera:
                    break
                time.sleep(1.0 / 60.0)

        capture = cv2.VideoCapture(args.camera)
        if not capture.isOpened():
            raise RuntimeError(f"Could not open camera {args.camera}")

        capture.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        capture.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        capture.set(cv2.CAP_PROP_FPS, 60)
        capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)

        max_hands = max(1, min(args.max_hands, 2))
        prev_positions = [(0.5, 0.5) for _ in range(max_hands)]
        prev_deltas = [(0.0, 0.0) for _ in range(max_hands)]
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
        identity_anchors = [None for _ in range(max_hands)]
        identity_labels = [None for _ in range(max_hands)]
        identity_candidate_state = {"signature": None, "count": 0}
        pose_conflict_state = {"order": None, "count": 0}
        start_time = time.perf_counter()
        prev_time = time.perf_counter()
        last_timestamp_ms = -1

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
            pose_result = (
                pose_landmarker.detect_for_video(image, timestamp_ms)
                if pose_landmarker is not None
                else None
            )
            pose_slots = build_pose_slots(pose_result, max_hands)

            dt = max(now - prev_time, 1.0 / 120.0)
            prev_time = now

            should_reset, _ = poll_control_commands(control_sock)
            if should_reset:
                prev_positions = [(0.5, 0.5) for _ in range(max_hands)]
                prev_deltas = [(0.0, 0.0) for _ in range(max_hands)]
                prev_landmarks = [None for _ in range(max_hands)]
                missing_timers = [DETECTION_GRACE_SECONDS for _ in range(max_hands)]
                distance_scales = [1.0 for _ in range(max_hands)]
                center_trails = [[] for _ in range(max_hands)]
                identity_anchors = [None for _ in range(max_hands)]
                identity_labels = [None for _ in range(max_hands)]
                identity_candidate_state = {"signature": None, "count": 0}
                pose_conflict_state = {"order": None, "count": 0}
                for filter_ in center_filters:
                    filter_.reset()
                for filter_ in center_kalman_filters:
                    filter_.reset()
                for filter_ in point_filters:
                    filter_.reset()
                for history in motion_histories:
                    history.reset()
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
                            "label": handedness_label(handedness),
                            "label_score": handedness_score(handedness),
                        }
                    )

            assigned_hands, assignment_metadata_list = assign_detected_hands_with_debug(
                detected_hands,
                prev_positions,
                prev_landmarks,
                missing_timers,
                max_hands,
                identity_anchors,
                identity_labels,
                prev_deltas,
                pose_slots,
                pose_conflict_state,
            )
            register_assigned_hand_identity_stable(
                assigned_hands,
                identity_anchors,
                identity_labels,
                identity_candidate_state,
            )

            speeds = []
            valid_count = 0
            height, width = frame.shape[:2]
            colors = [(30, 240, 90), (80, 180, 255)]
            draw_pose_slots(frame, pose_slots)

            for hand_index in range(max_hands):
                hand = assigned_hands[hand_index]
                assignment_meta = assignment_metadata_list[hand_index]
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
                    confidence = min(
                        hand["confidence"],
                        hand.get(
                            "identity_confidence",
                            assignment_meta["identity_confidence"],
                        ),
                    )
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
                    prev_deltas[hand_index] = (
                        prev_deltas[hand_index][0] * HAND_ASSIGNMENT_DELTA_DECAY,
                        prev_deltas[hand_index][1] * HAND_ASSIGNMENT_DELTA_DECAY,
                    )

                valid = detected or (
                    prev_landmarks[hand_index] is not None
                    and missing_timers[hand_index] <= DETECTION_GRACE_SECONDS
                )
                if not detected and valid:
                    predicted = center_kalman_filters[hand_index].predict(dt)
                    if predicted is not None:
                        x, y = predicted
                        predicted_only = True
                        confidence = min(
                            PREDICTED_HAND_CONFIDENCE,
                            max(
                                assignment_meta["identity_confidence"],
                                IDENTITY_CONFIDENCE_PREDICTED,
                            ),
                        )

                dx = x - prev_x
                dy = y - prev_y
                prev_deltas[hand_index] = (
                    prev_deltas[hand_index][0] * (1.0 - HAND_ASSIGNMENT_DELTA_FOLLOW)
                    + dx * HAND_ASSIGNMENT_DELTA_FOLLOW,
                    prev_deltas[hand_index][1] * (1.0 - HAND_ASSIGNMENT_DELTA_FOLLOW)
                    + dy * HAND_ASSIGNMENT_DELTA_FOLLOW,
                )
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
                        label=(
                            f"H{hand_index + 1} "
                            f"{'PREDICTED' if predicted_only else assignment_meta['reason']} "
                            f"{confidence:.2f}"
                        ),
                        radius=7,
                        filled=detected,
                    )

                packet = (
                    f"HAND{hand_index + 1} {1 if valid else 0} "
                    f"{x:.6f} {y:.6f} {packet_dx:.6f} {packet_dy:.6f} "
                    f"{speed:.6f} {confidence:.6f} "
                    f"0.000000 1.000000 {speed:.6f} "
                    f"{'PREDICTED' if predicted_only else assignment_meta['reason']}"
                )
                sock.sendto(packet.encode("ascii"), target)

            send_preview_frame(
                sock, preview_target, frame, args, preview_state, now
            )
            if args.show_window:
                cv2.imshow("Hand UDP Sender", frame)
                if cv2.waitKey(1) & 0xFF == 27:
                    break
    finally:
        if pose_landmarker is not None:
            pose_landmarker.close()
        landmarker.close()
        if capture is not None:
            capture.release()
        if control_sock is not None:
            control_sock.close()
        if args.show_window:
            cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
