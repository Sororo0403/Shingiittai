import argparse
import statistics
import time

import cv2


DEFAULT_MODES = (
    (640, 480, 30),
    (640, 480, 60),
    (1280, 720, 30),
    (1280, 720, 60),
    (1920, 1080, 30),
    (1920, 1080, 60),
)


def parse_camera(value):
    text = str(value)
    return int(text) if text.isdigit() else text


def fourcc_to_text(value):
    code = int(value)
    chars = [chr((code >> (8 * i)) & 0xFF) for i in range(4)]
    text = "".join(chars)
    return text if text.strip("\x00 ") else "0"


def percentile(values, ratio):
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(round((len(ordered) - 1) * ratio))))
    return ordered[index]


def open_capture(camera, backend_name):
    source = parse_camera(camera)
    if backend_name == "DSHOW":
        return cv2.VideoCapture(source, cv2.CAP_DSHOW)
    if backend_name == "MSMF":
        return cv2.VideoCapture(source, cv2.CAP_MSMF)
    return cv2.VideoCapture(source)


def measure_mode(camera, backend, width, height, fps, fourcc, seconds, warmup):
    cap = open_capture(camera, backend)
    if not cap.isOpened():
        return {"ok": False, "error": "open failed"}

    try:
        if fourcc:
            cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*fourcc))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        cap.set(cv2.CAP_PROP_FPS, fps)

        warmup_end = time.perf_counter() + warmup
        while time.perf_counter() < warmup_end:
            cap.read()

        timestamps = []
        read_times = []
        end_time = time.perf_counter() + seconds
        while time.perf_counter() < end_time:
            before = time.perf_counter()
            ok, frame = cap.read()
            after = time.perf_counter()
            if not ok or frame is None:
                continue
            timestamps.append(after)
            read_times.append(after - before)

        intervals = [
            (timestamps[i] - timestamps[i - 1]) * 1000.0
            for i in range(1, len(timestamps))
        ]
        elapsed = timestamps[-1] - timestamps[0] if len(timestamps) >= 2 else 0.0
        measured_fps = (len(timestamps) - 1) / elapsed if elapsed > 0.0 else 0.0

        actual_width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        actual_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        actual_fps = float(cap.get(cv2.CAP_PROP_FPS))
        actual_fourcc = fourcc_to_text(cap.get(cv2.CAP_PROP_FOURCC))
        avg_read_ms = statistics.mean(read_times) * 1000.0 if read_times else 0.0
        avg_interval_ms = statistics.mean(intervals) if intervals else 0.0

        return {
            "ok": True,
            "requested": f"{width}x{height}@{fps} {fourcc or 'default'}",
            "actual": f"{actual_width}x{actual_height}@{actual_fps:.2f} {actual_fourcc}",
            "measured_fps": measured_fps,
            "avg_interval_ms": avg_interval_ms,
            "p95_interval_ms": percentile(intervals, 0.95),
            "max_interval_ms": max(intervals) if intervals else 0.0,
            "avg_read_ms": avg_read_ms,
            "frames": len(timestamps),
        }
    finally:
        cap.release()


def parse_modes(text):
    modes = []
    for item in text.split(","):
        parts = item.lower().replace("@", "x").split("x")
        if len(parts) != 3:
            raise ValueError(f"invalid mode: {item}")
        modes.append((int(parts[0]), int(parts[1]), int(parts[2])))
    return modes


def parse_fourccs(text):
    fourccs = []
    for item in text.split(","):
        value = item.strip().upper()
        if value in ("", "0", "DEFAULT", "NONE"):
            value = ""
        if value not in fourccs:
            fourccs.append(value)
    return fourccs


def main():
    parser = argparse.ArgumentParser(description="Camera capture performance benchmark.")
    parser.add_argument("--camera", default="0")
    parser.add_argument("--backend", choices=("DSHOW", "MSMF", "DEFAULT"), default="DSHOW")
    parser.add_argument("--seconds", type=float, default=3.0)
    parser.add_argument("--warmup", type=float, default=0.6)
    parser.add_argument(
        "--modes",
        default=",".join(f"{w}x{h}@{fps}" for w, h, fps in DEFAULT_MODES),
        help="Comma-separated modes like 640x480@60,1280x720@60.",
    )
    parser.add_argument(
        "--fourcc",
        default="MJPG,YUY2,",
        help="Comma-separated FOURCC values. Empty entry means default.",
    )
    args = parser.parse_args()

    modes = parse_modes(args.modes)
    fourccs = parse_fourccs(args.fourcc)

    print(
        f"camera={args.camera} backend={args.backend} "
        f"seconds={args.seconds:.1f} warmup={args.warmup:.1f}",
        flush=True,
    )
    print(
        "requested              actual                 measured  avg_ms  p95_ms  max_ms  read_ms  frames",
        flush=True,
    )
    for width, height, fps in modes:
        for fourcc in fourccs:
            if fourcc and len(fourcc) != 4:
                continue
            result = measure_mode(
                args.camera,
                args.backend,
                width,
                height,
                fps,
                fourcc,
                args.seconds,
                args.warmup,
            )
            if not result["ok"]:
                print(f"{width}x{height}@{fps:<3} {fourcc or 'default':7}  {result['error']}", flush=True)
                continue
            print(
                f"{result['requested']:<22} "
                f"{result['actual']:<22} "
                f"{result['measured_fps']:7.2f} "
                f"{result['avg_interval_ms']:7.2f} "
                f"{result['p95_interval_ms']:7.2f} "
                f"{result['max_interval_ms']:7.2f} "
                f"{result['avg_read_ms']:7.2f} "
                f"{result['frames']:6d}",
                flush=True,
            )


if __name__ == "__main__":
    main()
