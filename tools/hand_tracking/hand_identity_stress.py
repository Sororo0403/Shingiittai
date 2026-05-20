import argparse
import math
import random
import statistics
import sys

import hand_udp_sender as tracker


FPS = 60.0
DT = 1.0 / FPS


def make_hand(raw_id, x, y, label, score=0.97):
    return tracker.simulated_hand(raw_id, x, y, label, score)


def scenario_detections(rng, step, t, config, state):
    phase = math.sin(t * config.get("freq", 0.78))
    wiggle = math.sin(t * config.get("wiggle_freq", 1.9)) * config.get(
        "wiggle", 0.035
    )
    amp = config.get("amp", 0.34) * config.get("squeeze", 1.0)
    ysep = config.get("ysep", 0.10)
    jitter = config.get("jitter", 0.0)

    ax = 0.5 + phase * amp + rng.gauss(0.0, jitter)
    bx = 0.5 - phase * amp + rng.gauss(0.0, jitter)
    if config.get("vertical", False):
        ay = 0.5 + phase * config.get("vertical_amp", 0.22)
        by = 0.5 - phase * config.get("vertical_amp", 0.22)
        ay += rng.gauss(0.0, jitter * 0.7)
        by += rng.gauss(0.0, jitter * 0.7)
    else:
        ay = 0.5 - ysep * 0.5 + wiggle + rng.gauss(0.0, jitter * 0.7)
        by = 0.5 + ysep * 0.5 - wiggle + rng.gauss(0.0, jitter * 0.7)

    if rng.random() < config.get("teleport_rate", 0.0):
        if rng.random() < 0.5:
            ax += rng.choice((-0.18, 0.18))
        else:
            bx += rng.choice((-0.18, 0.18))

    label_a, label_b = "Right", "Left"
    label_mode = config.get("label", "stable")
    if label_mode == "flip_cross" and abs(phase) < config.get("flip_window", 0.30):
        label_a, label_b = label_b, label_a
    elif label_mode == "random":
        probability = config.get("label_random_probability", 0.35)
        if rng.random() < probability:
            label_a = "Left" if label_a == "Right" else "Right"
        if rng.random() < probability:
            label_b = "Right" if label_b == "Left" else "Left"
    elif label_mode == "always_wrong":
        label_a, label_b = "Left", "Right"

    detections = [
        make_hand("A", ax, ay, label_a),
        make_hand("B", bx, by, label_b),
    ]
    state["truth"] = {
        "A": (tracker.clamp01(ax), tracker.clamp01(ay)),
        "B": (tracker.clamp01(bx), tracker.clamp01(by)),
    }

    both_burst_seconds = config.get("both_burst_seconds", 0.0)
    if both_burst_seconds > 0.0:
        if step >= state.get("both_until", -1) and rng.random() < config.get(
            "both_burst_rate", 0.008
        ):
            state["both_until"] = step + int(both_burst_seconds * FPS)
        if step < state.get("both_until", -1):
            detections = []

    if detections:
        one_burst_seconds = config.get("one_burst_seconds", 0.0)
        if one_burst_seconds > 0.0:
            if step >= state.get("one_until", -1) and rng.random() < config.get(
                "one_burst_rate", 0.010
            ):
                state["one_until"] = step + int(one_burst_seconds * FPS)
                state["one_raw"] = rng.choice(("A", "B"))
            if step < state.get("one_until", -1):
                detections = [
                    hand for hand in detections if hand["raw_id"] == state["one_raw"]
                ]

    if detections and rng.random() < config.get("drop_one_rate", 0.0):
        keep = rng.choice(("A", "B"))
        detections = [hand for hand in detections if hand["raw_id"] == keep]

    if len(detections) == 2 and rng.random() < config.get("shuffle_rate", 1.0):
        detections.reverse()

    return detections


def scenario_pose_slots(rng, config, state):
    if not config.get("pose", False):
        return None

    truth = state.get("truth")
    if not truth:
        return None

    slots = [None, None]
    pose_jitter = config.get("pose_jitter", 0.010)
    pose_drop_rate = config.get("pose_drop_rate", 0.0)
    pose_swap_rate = config.get("pose_swap_rate", 0.0)
    pose_wrong_wrist_rate = config.get("pose_wrong_wrist_rate", 0.0)
    slot_specs = [(0, "A", 0.66), (1, "B", 0.34)]
    if rng.random() < pose_swap_rate:
        slot_specs = [(0, "B", 0.66), (1, "A", 0.34)]

    for slot, raw_id, shoulder_x in slot_specs:
        if rng.random() < pose_drop_rate:
            continue

        wrist_x, wrist_y = truth[raw_id]
        if rng.random() < pose_wrong_wrist_rate:
            other_raw_id = "B" if raw_id == "A" else "A"
            wrist_x, wrist_y = truth[other_raw_id]
        wrist = (
            tracker.clamp01(wrist_x + rng.gauss(0.0, pose_jitter)),
            tracker.clamp01(wrist_y + rng.gauss(0.0, pose_jitter)),
        )
        shoulder = (shoulder_x, 0.78)
        elbow = (
            tracker.clamp01(wrist[0] * 0.58 + shoulder[0] * 0.42),
            tracker.clamp01(wrist[1] * 0.58 + shoulder[1] * 0.42),
        )
        slots[slot] = {
            "wrist": wrist,
            "elbow": elbow,
            "shoulder": shoulder,
            "side_x": shoulder_x,
            "confidence": 0.88,
        }
    return slots


def run_once(seed, config):
    rng = random.Random(seed)
    frame_count = int(config.get("duration", 16.0) * FPS)
    max_hands = 2
    prev_positions = [(0.5, 0.5) for _ in range(max_hands)]
    prev_deltas = [(0.0, 0.0) for _ in range(max_hands)]
    prev_landmarks = [None for _ in range(max_hands)]
    missing_timers = [tracker.DETECTION_GRACE_SECONDS for _ in range(max_hands)]
    identity_anchors = [None for _ in range(max_hands)]
    identity_labels = [None for _ in range(max_hands)]
    identity_candidate_state = {"signature": None, "count": 0}
    pose_conflict_state = {"order": None, "count": 0}
    canonical = [None for _ in range(max_hands)]
    slot_raw = [None for _ in range(max_hands)]
    state = {}

    locked = False
    flips = 0
    wrong_frames = 0
    visible_after_lock = 0
    lock_step = None
    first_event = None
    wrong_runs = []
    wrong_start = None
    one_frames = 0
    none_frames = 0
    close_frames = 0
    ambiguous_frames = 0

    t = config.get("offset", 0.0)
    for step in range(frame_count):
        dt = DT
        if config.get("fps_jitter", False):
            dt *= rng.uniform(0.40, 2.20)
        t += dt

        detections = scenario_detections(rng, step, t, config, state)
        pose_slots = scenario_pose_slots(rng, config, state)
        if len(detections) == 0:
            none_frames += 1
        elif len(detections) == 1:
            one_frames += 1
        elif (
            abs(detections[0]["x"] - detections[1]["x"])
            < config.get("close_x_threshold", 0.12)
        ):
            close_frames += 1
        if len(detections) == 2:
            if tracker.distance_sq(
                (detections[0]["x"], detections[0]["y"]),
                (detections[1]["x"], detections[1]["y"]),
            ) < tracker.HAND_ASSIGNMENT_AMBIGUOUS_SEPARATION ** 2:
                ambiguous_frames += 1

        assigned, assignment_metadata = tracker.assign_detected_hands_with_debug(
            detections,
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
        tracker.register_assigned_hand_identity_stable(
            assigned,
            identity_anchors,
            identity_labels,
            identity_candidate_state,
        )

        if (
            not locked
            and identity_anchors[0] is not None
            and assigned[0] is not None
            and assigned[1] is not None
        ):
            canonical = [assigned[0]["raw_id"], assigned[1]["raw_id"]]
            slot_raw = list(canonical)
            lock_step = step
            locked = True

        current = []
        for slot, hand in enumerate(assigned):
            raw = hand.get("raw_id") if hand is not None else None
            current.append(raw)
            if locked and raw is not None:
                if slot_raw[slot] is not None and raw != slot_raw[slot]:
                    flips += 1
                    if first_event is None:
                        first_event = {
                            "time": round(t, 3),
                            "slot": slot,
                            "from": slot_raw[slot],
                            "to": raw,
                            "detections": [
                                (
                                    hand.get("raw_id"),
                                    round(hand["x"], 3),
                                    round(hand["y"], 3),
                                    hand.get("label"),
                                )
                                for hand in detections
                            ],
                            "canonical": list(canonical),
                            "missing": [round(value, 3) for value in missing_timers],
                            "reasons": [
                                hand.get("assignment_reason") if hand is not None else None
                                for hand in assigned
                            ],
                            "identity_confidence": [
                                round(
                                    hand.get("identity_confidence", 0.0),
                                    3,
                                )
                                if hand is not None
                                else 0.0
                                for hand in assigned
                            ],
                            "assignment_margin": [
                                round(meta.get("margin", 0.0), 4)
                                for meta in assignment_metadata
                            ],
                        }
                slot_raw[slot] = raw

        if locked and any(raw is not None for raw in current):
            visible_after_lock += 1

        wrong = locked and any(
            current[slot] is not None and current[slot] != canonical[slot]
            for slot in range(max_hands)
        )
        if wrong:
            wrong_frames += 1
            if wrong_start is None:
                wrong_start = step
        elif wrong_start is not None:
            wrong_runs.append((step - wrong_start) * DT)
            wrong_start = None

        for slot, hand in enumerate(assigned):
            if hand is None:
                missing_timers[slot] += dt
                prev_deltas[slot] = (
                    prev_deltas[slot][0] * tracker.HAND_ASSIGNMENT_DELTA_DECAY,
                    prev_deltas[slot][1] * tracker.HAND_ASSIGNMENT_DELTA_DECAY,
                )
                continue

            dx = hand["x"] - prev_positions[slot][0]
            dy = hand["y"] - prev_positions[slot][1]
            prev_deltas[slot] = (
                prev_deltas[slot][0] * (1.0 - tracker.HAND_ASSIGNMENT_DELTA_FOLLOW)
                + dx * tracker.HAND_ASSIGNMENT_DELTA_FOLLOW,
                prev_deltas[slot][1] * (1.0 - tracker.HAND_ASSIGNMENT_DELTA_FOLLOW)
                + dy * tracker.HAND_ASSIGNMENT_DELTA_FOLLOW,
            )
            prev_positions[slot] = (hand["x"], hand["y"])
            prev_landmarks[slot] = hand["points"]
            missing_timers[slot] = 0.0

    if wrong_start is not None:
        wrong_runs.append((frame_count - wrong_start) * DT)

    return {
        "seed": seed,
        "locked": locked,
        "lock_step": lock_step,
        "flips": flips,
        "wrong_frames": wrong_frames,
        "wrong_pct": wrong_frames / max(1, visible_after_lock) * 100.0,
        "max_wrong_run": max(wrong_runs) if wrong_runs else 0.0,
        "first_event": first_event,
        "one_frames": one_frames,
        "none_frames": none_frames,
        "close_frames": close_frames,
        "ambiguous_frames": ambiguous_frames,
    }


def summarize(name, rows):
    failures = [
        row
        for row in rows
        if not row["locked"] or row["flips"] > 0 or row["wrong_frames"] > 0
    ]
    worst = max(
        rows,
        key=lambda row: (
            not row["locked"],
            row["wrong_pct"],
            row["max_wrong_run"],
            row["flips"],
        ),
    )
    return {
        "name": name,
        "runs": len(rows),
        "failed": len(failures),
        "unlocked": sum(1 for row in rows if not row["locked"]),
        "avg_wrong_pct": statistics.mean(row["wrong_pct"] for row in rows),
        "max_wrong_pct": max(row["wrong_pct"] for row in rows),
        "max_wrong_run": max(row["max_wrong_run"] for row in rows),
        "avg_ambiguous": statistics.mean(row["ambiguous_frames"] for row in rows),
        "avg_one": statistics.mean(row["one_frames"] for row in rows),
        "avg_none": statistics.mean(row["none_frames"] for row in rows),
        "worst": worst,
    }


def scenario_matrix():
    scenarios = {
        "clean_cross": {"jitter": 0.0},
        "label_flip_cross": {"jitter": 0.01, "label": "flip_cross"},
        "always_wrong_label": {"jitter": 0.01, "label": "always_wrong"},
        "random_label_10": {
            "jitter": 0.012,
            "label": "random",
            "label_random_probability": 0.10,
        },
        "random_label_25": {
            "jitter": 0.012,
            "label": "random",
            "label_random_probability": 0.25,
        },
        "random_label_35": {
            "jitter": 0.012,
            "label": "random",
            "label_random_probability": 0.35,
        },
        "short_single_drop_08": {
            "jitter": 0.012,
            "label": "flip_cross",
            "drop_one_rate": 0.08,
        },
        "fast_cross": {
            "freq": 1.55,
            "amp": 0.38,
            "jitter": 0.018,
            "label": "flip_cross",
            "drop_one_rate": 0.05,
            "fps_jitter": True,
        },
        "near_center_80": {
            "jitter": 0.014,
            "label": "random",
            "label_random_probability": 0.20,
            "squeeze": 0.80,
        },
        "near_center_65": {
            "jitter": 0.014,
            "label": "random",
            "label_random_probability": 0.20,
            "squeeze": 0.65,
        },
        "near_center_45": {
            "jitter": 0.014,
            "label": "random",
            "label_random_probability": 0.20,
            "squeeze": 0.45,
        },
        "one_burst_200ms": {
            "jitter": 0.012,
            "label": "flip_cross",
            "one_burst_seconds": 0.20,
        },
        "one_burst_500ms": {
            "jitter": 0.012,
            "label": "flip_cross",
            "one_burst_seconds": 0.50,
        },
        "both_burst_200ms": {
            "jitter": 0.012,
            "label": "flip_cross",
            "both_burst_seconds": 0.20,
        },
        "both_burst_500ms": {
            "jitter": 0.012,
            "label": "flip_cross",
            "both_burst_seconds": 0.50,
        },
        "teleport_spikes": {
            "jitter": 0.012,
            "label": "flip_cross",
            "teleport_rate": 0.012,
        },
        "random_label_35_pose": {
            "jitter": 0.012,
            "label": "random",
            "label_random_probability": 0.35,
            "pose": True,
        },
        "fast_cross_pose": {
            "freq": 1.55,
            "amp": 0.38,
            "jitter": 0.018,
            "label": "flip_cross",
            "drop_one_rate": 0.05,
            "fps_jitter": True,
            "pose": True,
        },
        "near_center_45_pose": {
            "jitter": 0.014,
            "label": "random",
            "label_random_probability": 0.20,
            "squeeze": 0.45,
            "pose": True,
        },
        "deep_center_30_pose": {
            "jitter": 0.014,
            "label": "random",
            "label_random_probability": 0.25,
            "squeeze": 0.30,
            "pose": True,
        },
        "vertical_cross_pose": {
            "freq": 1.25,
            "amp": 0.30,
            "jitter": 0.016,
            "label": "random",
            "label_random_probability": 0.25,
            "vertical": True,
            "vertical_amp": 0.26,
            "pose": True,
        },
        "one_burst_500ms_pose": {
            "jitter": 0.012,
            "label": "flip_cross",
            "one_burst_seconds": 0.50,
            "pose": True,
        },
        "one_burst_1000ms_pose": {
            "jitter": 0.012,
            "label": "flip_cross",
            "one_burst_seconds": 1.00,
            "pose": True,
        },
        "both_burst_500ms_pose": {
            "jitter": 0.012,
            "label": "flip_cross",
            "both_burst_seconds": 0.50,
            "pose": True,
        },
        "both_burst_1000ms_pose": {
            "jitter": 0.012,
            "label": "flip_cross",
            "both_burst_seconds": 1.00,
            "pose": True,
        },
        "teleport_spikes_pose": {
            "jitter": 0.012,
            "label": "flip_cross",
            "teleport_rate": 0.012,
            "pose": True,
        },
        "pose_jitter_heavy": {
            "jitter": 0.012,
            "label": "random",
            "label_random_probability": 0.25,
            "pose": True,
            "pose_jitter": 0.040,
        },
        "pose_dropout_15": {
            "jitter": 0.012,
            "label": "flip_cross",
            "drop_one_rate": 0.04,
            "pose": True,
            "pose_drop_rate": 0.15,
        },
        "pose_dropout_35": {
            "jitter": 0.012,
            "label": "flip_cross",
            "drop_one_rate": 0.06,
            "pose": True,
            "pose_drop_rate": 0.35,
        },
        "pose_dropout_50": {
            "jitter": 0.012,
            "label": "flip_cross",
            "drop_one_rate": 0.08,
            "pose": True,
            "pose_drop_rate": 0.50,
        },
        "pose_swap_02": {
            "jitter": 0.012,
            "label": "random",
            "label_random_probability": 0.25,
            "pose": True,
            "pose_swap_rate": 0.02,
        },
        "pose_wrong_wrist_05": {
            "jitter": 0.012,
            "label": "random",
            "label_random_probability": 0.25,
            "pose": True,
            "pose_wrong_wrist_rate": 0.05,
        },
        "pose_mixed_abuse": {
            "freq": 1.35,
            "amp": 0.36,
            "jitter": 0.018,
            "label": "random",
            "label_random_probability": 0.35,
            "drop_one_rate": 0.05,
            "fps_jitter": True,
            "pose": True,
            "pose_jitter": 0.030,
            "pose_drop_rate": 0.18,
            "pose_swap_rate": 0.01,
            "pose_wrong_wrist_rate": 0.03,
        },
        "pose_mixed_extreme": {
            "freq": 1.55,
            "amp": 0.38,
            "jitter": 0.022,
            "label": "random",
            "label_random_probability": 0.40,
            "drop_one_rate": 0.08,
            "fps_jitter": True,
            "pose": True,
            "pose_jitter": 0.045,
            "pose_drop_rate": 0.28,
            "pose_swap_rate": 0.025,
            "pose_wrong_wrist_rate": 0.06,
        },
    }
    return scenarios


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seeds", type=int, default=120)
    parser.add_argument("--duration", type=float, default=16.0)
    parser.add_argument("--scenario", default="")
    args = parser.parse_args()

    scenarios = scenario_matrix()
    if args.scenario:
        scenarios = {args.scenario: scenarios[args.scenario]}

    for name, config in scenarios.items():
        config = dict(config)
        config["duration"] = args.duration
        rows = [run_once(seed, config) for seed in range(args.seeds)]
        summary = summarize(name, rows)
        print(
            f"{summary['name']}: failed {summary['failed']}/{summary['runs']} "
            f"unlocked {summary['unlocked']} "
            f"avg_wrong {summary['avg_wrong_pct']:.3f}% "
            f"max_wrong {summary['max_wrong_pct']:.3f}% "
            f"max_run {summary['max_wrong_run']:.3f}s "
            f"avg_ambiguous {summary['avg_ambiguous']:.1f} "
            f"avg_one {summary['avg_one']:.1f} "
            f"avg_none {summary['avg_none']:.1f}"
        )
        if summary["failed"]:
            print(f"  worst: {summary['worst']}")


if __name__ == "__main__":
    sys.exit(main())
