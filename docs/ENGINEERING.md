# Engineering boundaries

This document states design constraints, not implemented status or task order.
For current evidence use [EVIDENCE.md](EVIDENCE.md); for priorities use
[PLAN.md](../PLAN.md). A code path existing is not a native or headset result.

## Native ownership

DSR owns gameplay state, collision, actor lifecycle, equipment, damage,
stamina, AI and progression. DSVR may replace the local player's view and
physical input only through an attributed native boundary. Before adding a
native call or write, verify the exact executable, entry bytes, complete ABI,
receiver, lifetime, update thread, side effects and independent game result.
An HP setter, AI request, parameter row or decompiler guess is not a melee API.

Keep the mod's tracking space, calibrated world mapping, game root, rendered
body and view distinct. Record units and coordinate spaces at their boundaries.
Refuse non-finite, stale or generation-mismatched samples. Equipment, load,
death, focus and tracking changes must invalidate transient state without
discarding valid user calibration.

## View and rendering

Physical HMD pose must reach the view without smoothing that changes the
tracked movement. Artificial turning and body smoothing must not rotate or
delay physical head motion. Menus and cutscenes need a readable theater path.

True stereo requires two game scene renders using each eye's pose and
asymmetric projection from one simulation state. XR image acquisition, two
textures or a copied mono frame alone do not qualify it. Gameplay, AI,
collision and event costs must advance once, and both eyes must complete or
cancel together. Resource and history lifetime must be traced through GPU use,
not inferred from a CPU return.

## Body, movement and contact

Arm IK may change only owned native pose rows after a verified source/copy
boundary. The native rig, unowned bones, animation events and equipment stay
authoritative. Calibrated reach, head height and roomscale input share one
mapping; apparent world scale does not prove a resized mesh or collider.
Tracked weapon geometry must follow the committed weapon pose, not a diagnostic
proxy. Lost tracking, walking, turning or resting overlap cannot create hits.

The game's motor and solid geometry govern travel. Camera-relative pad input
and a follow heuristic do not establish authoritative root or collision
ownership. A physical sword or shield result needs a real native contact,
correct target/actor generation and native outcome. Direct HP edits, synthetic
enemies and gesture-to-button substitutes cannot satisfy physical combat.
Haptics follow confirmed outcomes, never a predicted hit alone.

## Input, lifecycle and diagnostics

Keep essential menus and progression usable with controllers. Capture/release
transitions must not leak held input; profile changes must not tear down an
attached OpenXR action set. One owner advances each XR frame. Inactive or
failed frames must release owned resources and leave a recoverable game.

Use bounded, loss-reporting traces. A dropped record, missing footer, unknown
actor type or synthetic simulator event weakens the verdict; never fill the
gap with a guessed expected value. Preserve exact candidate and receipt
identity through analysis and restoration.

## Module seams

One deep module owns native-trace decoding: `tools/vr_trace.py`. The autopilot
keeps a second reader for its verdict contract; fold that reader onto
`read_capture` when the trace format next changes. Do not add a third reader.

Decision gates live in pure, clock-free headers rather than inline in the frame
loops: `src/core/mono_provenance.h` owns the camera-receipt gate and
`src/core/bridge_gates.h` owns the tracked-swing attack/defence/suppress gates.
Extend those headers and their unit tests instead of adding gates at a call
site.

Do not extract speculative modules for motion-input episodes, settings state or
XR frame transactions until a real change needs the seam. Each would add an
interface before it removes a caller.
