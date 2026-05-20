# Hand Tracking Stress Report

## Run

Stress tests were run with synthetic hand streams against the current Pose-backed identity tracker.

Main matrix:

- `120 seeds`
- `16 seconds`
- all scenarios

Focused runs:

- `240 seeds`
- `16 seconds`
- key Pose scenarios

Additional heavy runs:

- `1000 seeds`
- `20 seconds`
- normal Pose, dropout, burst, and Pose-abuse scenarios

New edge-case runs:

- `500 seeds`
- `20 seconds`
- deeper center overlap, vertical crossing, `1.0s` occlusion, `50%` Pose dropout, and extreme mixed Pose abuse

## Fixes Added In This Pass

The tracker now rejects several dangerous updates instead of forcing an assignment:

- Pose/continuity conflict must persist for `3` frames before Pose can override the old track.
- If the Pose pipeline exists but Pose disappears for the current frame, the tracker holds instead of re-initializing from hand order.
- During one-hand visibility while both slots are still recent, the tracker holds instead of assigning the visible hand to a possibly wrong slot.
- Initial identity anchors still require `3` stable frames before locking.
- Debug metadata now exposes `POSE`, `CONTINUITY`, `HOLD`, `PREDICTED`, `POSE_CONFLICT`, `INIT`, and `FALLBACK` reasons.

## Strong Results

These Pose-backed cases were fully stable in the latest heavy runs:

- `random_label_35_pose`: `0/1000` failed, max wrong run `0.000s`
- `near_center_45_pose`: `0/1000` failed, max wrong run `0.000s`
- `fast_cross_pose`: `0/1000` failed, max wrong run `0.000s`
- `deep_center_30_pose`: `0/500` failed, max wrong run `0.000s`
- `teleport_spikes_pose`: `2/1000` failed, max wrong run `0.017s`

Mostly stable, with only single-frame or very short blips:

- `one_burst_500ms_pose`: `91/1000` failed, max wrong run `0.033s`
- `both_burst_500ms_pose`: `84/1000` failed, max wrong run `0.017s`
- `one_burst_1000ms_pose`: `239/500` failed, max wrong run `0.017s`
- `both_burst_1000ms_pose`: `204/500` failed, max wrong run `0.017s`
- `vertical_cross_pose`: `1/500` failed, max wrong run `0.017s`
- `pose_jitter_heavy`: `97/1000` failed, max wrong run `0.050s`
- `pose_dropout_15`: `11/1000` failed, max wrong run `0.050s`

Interpretation: normal play conditions with Pose available no longer produce long slot inversions in the synthetic suite.

## Hard Pose-Abuse Results

These cases intentionally make Pose itself lie:

- `pose_swap_02`: `131/240` failed, max wrong run `0.050s`
- `pose_wrong_wrist_05`: `230/240` failed, max wrong run `0.067s`
- `pose_mixed_abuse`: `135/240` failed, max wrong run `0.100s`
- `pose_dropout_35`: `5/240` failed, max wrong run `0.133s`

Heavier runs:

- `pose_swap_02`: `564/1000` failed, max wrong run `0.050s`
- `pose_wrong_wrist_05`: `961/1000` failed, max wrong run `0.067s`
- `pose_mixed_abuse`: `775/1000` failed, max wrong run `0.117s`
- `pose_dropout_35`: `19/1000` failed, max wrong run `0.133s`
- `pose_dropout_50`: `9/500` failed, max wrong run `0.117s`
- `pose_mixed_extreme`: `490/500` failed, max wrong run `0.200s`

The failure count is high because the test treats even one wrong frame as a failed run. The important improvement is that long multi-second inversions are gone in these Pose-backed abuse scenarios; remaining errors are short bursts.

`pose_mixed_extreme` is the first synthetic case in this pass that still produced a visible-length `0.200s` burst. That scenario combines FPS jitter, high hand jitter, one-hand dropout, `28%` Pose dropout, `2.5%` Pose side swaps, and `6%` wrong Pose wrists.

## Rejected Tuning

Increasing Pose conflict confirmation from `3` frames to `4` frames was tested.

Result:

- `fast_cross_pose` stayed stable.
- `one_burst_1000ms_pose` stayed short.
- `pose_mixed_abuse` and `pose_mixed_extreme` got worse in average wrong frames.

Conclusion: simply waiting longer is not the right next fix. It can make recovery worse after Pose/dropout gaps. The next improvement should be reliability scoring, not a longer hard delay.

## Expected Weakness Without Pose

Hand-only scenarios still fail badly when labels are noisy or hands are near/crossed.

Examples from `120 seeds / 16 seconds`:

- `random_label_35`: `112/120` failed, max wrong run about `12s`
- `fast_cross`: `71/120` failed, max wrong run about `14s`
- `near_center_45`: `119/120` failed, max wrong run about `12s`

Interpretation: this confirms the design assumption. Hands alone do not contain enough information in ambiguous crossing/occlusion cases.

## Current Verdict

The tracker is now good enough for Pose-backed playtesting, especially because the game receives lower confidence during HOLD/uncertain periods.

The remaining frontier is not hand label noise. It is detecting when Pose arm identity itself is confidently wrong for several frames. More rule tuning can reduce single-frame blips, but eliminating them completely needs a Pose reliability model rather than trusting each frame independently.

## Recommended Next Fix

Add a rolling Pose reliability score per arm slot:

- Penalize Pose when wrist ownership contradicts previous stable hand identity.
- Penalize repeated shoulder/wrist semantic swaps.
- Recover reliability slowly after several clean frames.
- When reliability is low, prefer HOLD/PREDICTED over `POSE`.

This should target the remaining `pose_swap_02`, `pose_wrong_wrist_05`, and `pose_mixed_abuse` cases without hurting normal crossing.
