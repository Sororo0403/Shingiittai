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
from mediapipe.tasks import python
from mediapipe.tasks.python import vision


PALM_INDICES = (0, 5, 9, 13, 17)
TRACKING_INDICES = tuple(range(21))


def clamp01(value):
    return max(0.0, min(1.0, value))


def average_landmark(landmarks, indices):
    x = sum(landmarks[index].x for index in indices) / len(indices)
    y = sum(landmarks[index].y for index in indices) / len(indices)
    return clamp01(x), clamp01(y)


def copy_landmark_points(landmarks):
    return [(clamp01(landmark.x), clamp01(landmark.y)) for landmark in landmarks]


def get_average_motion(points, prev_points, dt):
    if prev_points is None:
        return 0.0, 0.0, 0.0

    dx_total = 0.0
    dy_total = 0.0
    speed_total = 0.0
    for index in TRACKING_INDICES:
        dx = points[index][0] - prev_points[index][0]
        dy = points[index][1] - prev_points[index][1]
        dx_total += dx
        dy_total += dy
        speed_total += math.sqrt(dx * dx + dy * dy)

    count = float(len(TRACKING_INDICES))
    return dx_total / count, dy_total / count, speed_total / count / dt


def parse_args():
    parser = argparse.ArgumentParser(description="Send MediaPipe hand motion over UDP.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5005)
    parser.add_argument("--camera", type=int, default=0)
    parser.add_argument("--max-hands", type=int, default=2)
    parser.add_argument("--model", default=r"C:\models\hand_landmarker.task")
    return parser.parse_args()


def main():
    args = parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (args.host, args.port)

    capture = cv2.VideoCapture(args.camera)
    if not capture.isOpened():
        raise RuntimeError(f"Could not open camera {args.camera}")

    base_options = python.BaseOptions(model_asset_path=args.model)
    options = vision.HandLandmarkerOptions(
        base_options=base_options,
        running_mode=vision.RunningMode.IMAGE,
        num_hands=args.max_hands,
        min_hand_detection_confidence=0.55,
        min_hand_presence_confidence=0.55,
        min_tracking_confidence=0.55,
    )
    landmarker = vision.HandLandmarker.create_from_options(options)

    max_hands = max(1, min(args.max_hands, 2))
    prev_positions = [(0.5, 0.5) for _ in range(max_hands)]
    prev_landmarks = [None for _ in range(max_hands)]
    prev_time = time.perf_counter()

    try:
        while True:
            ok, frame = capture.read()
            if not ok:
                break

            frame = cv2.flip(frame, 1)
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
            result = landmarker.detect(image)

            now = time.perf_counter()
            dt = max(now - prev_time, 1.0 / 120.0)
            prev_time = now

            detected_hands = []
            if result.hand_landmarks:
                for hand_landmarks in result.hand_landmarks[:max_hands]:
                    wrist = hand_landmarks[0]
                    center_x, center_y = average_landmark(hand_landmarks, PALM_INDICES)
                    detected_hands.append(
                        {
                            "landmarks": hand_landmarks,
                            "points": copy_landmark_points(hand_landmarks),
                            "x": center_x,
                            "y": center_y,
                            "confidence": clamp01(1.0 - abs(hand_landmarks[9].z - wrist.z)),
                        }
                    )

            detected_hands.sort(key=lambda hand: hand["x"], reverse=True)

            speeds = []
            valid_count = 0
            height, width = frame.shape[:2]
            colors = [(30, 240, 90), (80, 180, 255)]
            for hand_index in range(max_hands):
                valid = hand_index < len(detected_hands)
                prev_x, prev_y = prev_positions[hand_index]
                x = prev_x
                y = prev_y
                confidence = 0.0
                points = None

                if valid:
                    hand = detected_hands[hand_index]
                    x = hand["x"]
                    y = hand["y"]
                    confidence = hand["confidence"]
                    points = hand["points"]
                    valid_count += 1

                    for landmark in hand["landmarks"]:
                        px = int(max(0, min(width - 1, landmark.x * width)))
                        py = int(max(0, min(height - 1, landmark.y * height)))
                        cv2.circle(frame, (px, py), 4, colors[hand_index], -1)

                dx = x - prev_x
                dy = y - prev_y
                landmark_dx, landmark_dy, landmark_speed = get_average_motion(
                    points, prev_landmarks[hand_index], dt
                ) if valid else (0.0, 0.0, 0.0)
                speed = max(math.sqrt(dx * dx + dy * dy) / dt, landmark_speed)
                prev_positions[hand_index] = (x, y)
                prev_landmarks[hand_index] = points if valid else None
                speeds.append(speed if valid else 0.0)

                packet = (
                    f"HAND{hand_index + 1} {1 if valid else 0} "
                    f"{x:.6f} {y:.6f} {landmark_dx:.6f} {landmark_dy:.6f} "
                    f"{speed:.6f} {confidence:.6f} "
                    f"0.000000 1.000000 {landmark_speed:.6f}"
                )
                sock.sendto(packet.encode("ascii"), target)

            cv2.putText(
                frame,
                (
                    f"UDP {args.host}:{args.port} hands={valid_count} "
                    f"R={speeds[0]:.2f} L={(speeds[1] if max_hands > 1 else 0.0):.2f}"
                ),
                (12, 28),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                (30, 240, 90) if valid_count > 0 else (60, 60, 240),
                2,
                cv2.LINE_AA,
            )
            cv2.imshow("Hand UDP Sender", frame)
            if cv2.waitKey(1) & 0xFF == 27:
                break
    finally:
        landmarker.close()
        capture.release()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
