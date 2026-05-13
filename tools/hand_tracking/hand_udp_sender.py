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


def parse_args():
    parser = argparse.ArgumentParser(description="Send MediaPipe hand motion over UDP.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5005)
    parser.add_argument("--camera", type=int, default=0)
    parser.add_argument("--max-hands", type=int, default=1)
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

    prev_x = 0.5
    prev_y = 0.5
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

            valid = 0
            x = prev_x
            y = prev_y
            confidence = 0.0

            if result.hand_landmarks:
                hand_landmarks = result.hand_landmarks[0]
                center = hand_landmarks[9]
                wrist = hand_landmarks[0]
                x = max(0.0, min(1.0, center.x))
                y = max(0.0, min(1.0, center.y))
                confidence = max(0.0, min(1.0, 1.0 - abs(center.z - wrist.z)))
                valid = 1

                height, width = frame.shape[:2]
                for landmark in hand_landmarks:
                    px = int(max(0, min(width - 1, landmark.x * width)))
                    py = int(max(0, min(height - 1, landmark.y * height)))
                    cv2.circle(frame, (px, py), 4, (30, 240, 90), -1)

            dx = x - prev_x
            dy = y - prev_y
            speed = math.sqrt(dx * dx + dy * dy) / dt
            prev_x = x
            prev_y = y

            packet = (
                f"HAND1 {valid} {x:.6f} {y:.6f} {dx:.6f} "
                f"{dy:.6f} {speed:.6f} {confidence:.6f}"
            )
            sock.sendto(packet.encode("ascii"), target)

            cv2.putText(
                frame,
                f"UDP {args.host}:{args.port} valid={valid} speed={speed:.2f}",
                (12, 28),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                (30, 240, 90) if valid else (60, 60, 240),
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
