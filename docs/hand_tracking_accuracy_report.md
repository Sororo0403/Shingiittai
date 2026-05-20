# Hand Tracking Accuracy Design Report

## Goal

Hand mode should feel immediate and stable enough that the player does not need to think about calibration. The main target is not perfect anatomical tracking. The game needs two stable control slots:

- `HAND1`: player right side / right weapon side
- `HAND2`: player left side / left weapon side

The system should prefer short smoothing or short uncertainty over a visible left/right swap. A one-frame delay is acceptable if it prevents a long identity inversion.

## Current Failure Definition

A failure is not simply "MediaPipe labels a hand wrong." The real failure is when game slot identity changes:

- Right-side control suddenly receives left-hand motion.
- Left-side control suddenly receives right-hand motion.
- The wrong assignment survives for more than a few frames.
- A hand disappears and returns into the opposite slot.
- A near-center crossing causes a permanent or long-lived swap.

Short single-frame uncertainty is much less harmful than a long identity inversion. The tests should report both:

- `wrong_frames`: how many frames were assigned to the wrong slot.
- `max_wrong_run`: the longest continuous wrong assignment.

## What Failed In The Hand-Only Design

The hand-only tracker uses hand center, previous position, handedness label, and short-term motion. This works for clean crossing and simple label flips, but breaks in harder cases.

Observed weak cases:

- Random handedness labels near crossing.
- Hands close to the center for a long time.
- One hand disappears briefly while the other remains visible.
- Both hands disappear and return near the opposite side.
- Fast crossing with frame-rate jitter.
- Teleport-like detection spikes.

Root cause: with only two moving hand points, some frames are mathematically ambiguous. If both detected hands are close together and labels are unreliable, the tracker cannot know which point belongs to which physical arm. It can only guess from history, and history can be poisoned after occlusion or a bad label burst.

## Key Test Result

Synthetic stress showed the difference clearly:

- Hand-only difficult cases often produced long swaps, sometimes several seconds.
- Adding simulated arm/Pose anchors reduced the same failures to short bursts, usually a few frames.

Example from the latest run:

- `random_label_35` hand-only: many long swaps, max wrong run around 12 seconds.
- `random_label_35_pose`: `0/240` failed, max wrong run `0.000s`.
- `fast_cross` hand-only: worst long inversion around 14 seconds.
- `fast_cross_pose`: `0/240` failed, max wrong run `0.000s`.

This means arm information is not just a small improvement. It changes the problem from identity guessing to body-side assignment.

## Best Architecture

Use a layered tracker with explicit confidence and fallback rules.

### 1. Detection Layer

Inputs:

- MediaPipe Hands: detailed hand landmarks, palm center, motion scale, handedness label.
- MediaPipe Pose: shoulder, elbow, wrist landmarks for both arms.

Hands provide fine control. Pose provides identity.

Pose should not replace hand tracking for movement. Pose wrists are usually less precise than hand landmarks. The best design is:

- Use Hands for exact weapon motion.
- Use Pose to decide which hand belongs to which slot.

### 2. Body-Side Slot Layer

Create two body-side slots every frame:

- Slot 0 from the right side of the mirrored camera image.
- Slot 1 from the left side of the mirrored camera image.

The slot should be derived from shoulder position first, not wrist position. Shoulders are more stable during crossing. Wrists cross often; shoulders usually do not.

For each arm slot store:

- shoulder point
- elbow point
- wrist point
- confidence
- age / last seen time

### 3. Hand-To-Arm Assignment Layer

For each detected hand, calculate assignment cost to each arm slot.

Important cost terms:

- Distance from hand center to Pose wrist.
- Distance from hand wrist landmark to Pose wrist.
- Agreement with previous slot position.
- Arm confidence and recency.
- Penalty for impossible jumps.

Handedness label should be low priority. It is useful when the scene is easy, but dangerous near crossings.

Recommended priority order:

1. Strong Pose arm match.
2. Stable previous slot continuity.
3. Handedness label only as a weak tie-breaker.
4. X-order only when there is no history and hands are clearly separated.

### 4. Temporal Identity Layer

Each slot should maintain a track state:

- `active`: detected this frame.
- `predicted`: temporarily missing but still valid.
- `uncertain`: assignment is ambiguous.
- `lost`: too old to trust.

When assignment is ambiguous, do not eagerly swap. Hold previous identity for a short window, or output predicted motion with reduced confidence.

This is important: the game should prefer "no new update for 1-3 frames" over "wrong hand controls the weapon."

### 5. Confidence Output Layer

The UDP packet currently sends validity and confidence. That confidence should eventually represent identity confidence too, not only hand detection confidence.

Suggested behavior:

- Strong hand + strong Pose: high confidence.
- Strong hand + weak Pose but good continuity: medium confidence.
- Ambiguous crossing: low confidence, keep previous slot.
- Missing hand predicted by Kalman/history: low confidence.
- Lost hand: invalid.

Game-side controls can then dampen low-confidence motion instead of reacting violently.

## Optimal Failure Handling

The optimal design should not try to force a fresh assignment every frame. It should choose one of three actions:

- Assign: evidence clearly says this hand belongs to this slot.
- Hold: evidence is ambiguous, keep previous slot identity.
- Drop/Predict: evidence is bad, avoid sending a wrong hand as valid.

Good failures:

- Short confidence dip during hand crossing.
- Weapon slows or briefly coasts.
- One hand remains valid while the other is temporarily predicted.

Bad failures:

- Slot identity flips and stays flipped.
- A single bad label burst changes identity.
- A hand returning from occlusion steals the other slot.
- Calibration or registration permanently locks the wrong side.

## Proposed Scoring Model

For each hand `h` and slot `s`:

```text
cost(h, s) =
    pose_wrist_distance * pose_weight
  + hand_wrist_to_pose_wrist_distance * wrist_weight
  + previous_slot_distance * continuity_weight
  + predicted_slot_distance * prediction_weight
  + label_mismatch_penalty * label_weight
  + jump_penalty
```

The weights should change by state:

- When Pose is strong, Pose dominates.
- When Pose is weak but track is active, continuity dominates.
- When both are weak, hold or invalidate instead of trusting labels.
- When no identity exists yet, initialize by shoulder/body side plus clear separation.

## Recommended State Machine

```text
LOST
  -> ACTIVE when strong hand + strong slot evidence exists

ACTIVE
  -> ACTIVE when assignment remains clear
  -> UNCERTAIN when best and second-best assignment are close
  -> PREDICTED when hand disappears briefly
  -> LOST after timeout

UNCERTAIN
  -> ACTIVE when evidence becomes clear
  -> PREDICTED if no safe assignment
  -> LOST after timeout

PREDICTED
  -> ACTIVE when returning hand matches Pose/continuity
  -> LOST after timeout
```

The critical rule is that `UNCERTAIN` should not swap identity. It should hold.

## Testing Strategy

Keep synthetic tests because they reveal identity bugs faster than camera testing.

Required synthetic scenarios:

- Clean cross.
- Label flip near cross.
- Always-wrong labels.
- Random label noise.
- Near-center slow crossing.
- Fast crossing with FPS jitter.
- Single-hand dropout.
- Both-hand dropout.
- Teleport spikes.
- Pose wrist jitter.
- Pose dropout.
- Pose left/right semantic swap.
- One arm hidden but hand visible.
- Hand visible but Pose wrist wrong.

Required real-camera scenarios:

- Slow cross at center.
- Fast cross.
- One hand covers the other.
- Both hands leave frame and return.
- Right hand only, then left hand only.
- Hands close together near chest.
- Hands close to camera.
- Body rotated slightly left/right.
- Sleeves/background colors similar to hands.

Pass criteria should focus on `max_wrong_run`:

- Ideal: 0 wrong frames.
- Acceptable: wrong run under 0.050 seconds.
- Suspicious: 0.050-0.150 seconds.
- Failure: over 0.150 seconds or any long-lived inversion.

## Current Implementation Gap

The current Pose addition is a good first layer, but not final architecture.

Still missing:

- Full track state machine.
- Explicit `UNCERTAIN` state.
- Identity confidence in UDP output.
- Pose dropout stress scenarios.
- Real camera recording/replay harness.
- Debug overlay that shows why a slot was assigned.

The current code uses Pose wrist assignment plus continuity fallback. It greatly reduces long swaps, but very close hand overlap can still cause a few-frame identity blip.

## Recommended Next Steps

1. Add assignment reason/debug output.
   Show `POSE`, `CONTINUITY`, `HOLD`, `PREDICTED`, or `LABEL` per slot in the camera test scene.

2. Add an explicit `UNCERTAIN/HOLD` state.
   If Pose costs are too close, keep previous assignment instead of reassigning.

3. Add identity confidence to packets.
   Let the game dampen motion when identity is uncertain.

4. Add Pose dropout and wrong-Pose synthetic tests.
   The current synthetic Pose tests assume Pose wrists are mostly correct.

5. Add camera replay capture.
   Record detection outputs from real camera sessions, then replay them without needing the camera. This makes regressions easy to test.

6. Tune with real movement.
   Synthetic tests prove the math, but final thresholds need real camera behavior.

## Conclusion

The optimal design is not calibration. Calibration only tries to name the hands once, but the real problem happens later during crossing, occlusion, and label noise.

The better design is continuous identity tracking:

- Hands for precise motion.
- Pose arms for body-side identity.
- Continuity for short gaps.
- Uncertainty state to avoid bad swaps.
- Confidence output so the game can ignore risky frames.

The important design principle is simple: never trust a single frame enough to swap identity when the evidence is ambiguous.
