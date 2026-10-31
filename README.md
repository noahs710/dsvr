# DSVR — native VR conversion for Dark Souls Remastered

Current documentation contract: 21 October 2026.

Build a native-feeling VR experience inside DSR's existing Windows/Steam engine: true stereo, independent 6DoF head tracking, tracked native arms/equipment, physical combat, collision-aware roomscale, usable interactions and controller-accessible UI. Native conversion is the fixed direction.

Keep native world, AI, progression, equipment, damage, stamina, poise, collision and actor lifecycle authoritative. Physical melee must use tracked contact and native outcomes. Gesture-triggered canned attacks, direct HP subtraction, a duplicate avatar, and mono presented to both eyes do not satisfy the product.

## Read these in order

1. This file: product scope and documentation authority.
2. [Cockpit orchestration plan](MODDING_PLAN.md) and lane files (.empryo/cockpit/lanes/A-K.md): the **only active execution plan**, milestones A–K, dependencies, task loop, completion ledger, evidence index and starting prompt.
3. [Engineering](docs/ENGINEERING.md): architectural invariants and module ownership.
4. [Testing policy](docs/TESTING.md) and [verification runbook](docs/VERIFICATION_RUNBOOK.md): protected operation, acceptance thresholds and run procedure.
5. [Current handoff](MODLOG.md): where to resume and what remains unverified.
6. [Native research index](MODDING_PLAN.md), [melee evidence](docs/NATIVE_MELEE_CONTRACT.md) and [arm evidence](docs/ARM_IK_EVIDENCE.md): observations and unresolved contracts, not alternative plans.

## Authority and maintenance

Direct user instructions take precedence. This file owns product scope; the execution guide alone owns task order; ENGINEERING owns architecture; TESTING owns operating policy; VERIFICATION_RUNBOOK owns acceptance procedures and numerical thresholds. Research notes and MODLOG cannot change those requirements. If a new observation exposes a conflict, update the owning document and its dependents in the same change; do not silently choose an easier requirement.

Superseded plans/specifications have been removed from active documentation. [History](docs/history/README.md) retains exact pre-cleanup text for evidence recovery only. Historical commands, approval requirements, test totals and next steps have no current authority. Do not resume tasks from history or use it as an instruction source. No additional design-approval chain or parallel-agent requirement is implied by old plans.

Update the owning documents in place. New focused native contract/evidence notes are acceptable when they preserve a distinct recovered ABI; do not create competing roadmaps. Third-party notices are separate dependency records and do not direct DSVR execution.

## Required experience

- Runtime-derived left/right scene views from one simulation state, with correct scale, occlusion and full tracked head orientation/translation.
- Head-oriented smooth locomotion by default. The left stick is always steered by exactly one live reference: your head, or the selected left/right/non-dominant hand, falling back to head if that hand is lost. Choose it in DSVR-Setup or the mod menu and it persists in DSVR.toml. Travel is interpreted in the heading of the camera the mod writes, so turning the view with the right stick turns where you walk and the native capsule follows that request. Snap/smooth turning and collision-resolved physical walking.
- Calibrated native avatar, armor, hands and weapons with coherent body ownership. Estimated legs/feet must not be described as measured tracking.
- Physical slash/thrust, directional shield block/parry, native costs/reactions/status and confirmed-outcome haptics; two-hand/off-hand and weapon-family support, ranged/catalyst aiming.
- Core combat target (28 October operator direction): continuous whole-blade sweeps at speed; silent sparks while dragging along stone/walls; direct wall impacts spend extra stamina while allowing continued attacks without a wall stun. Shields block at their actual tracked surface with native mitigation/costs; parry by moving the shield toward an incoming attack at speed. Dodges follow continuous stick direction through 360 degrees, including while locked on.
- Head obstruction protection, coherent physical crouch/collision/hurt geometry, adjustable comfort effects, reliable tracking/load/death recovery.
- Controller-only routine progression, interaction selection/manipulation where supported, readable HUD, menus, quick slots and recenter/calibration.
- Campaign traversal, bosses and special actions qualified on the supported matrix; reproducible install, rollback and support package.

Target: 90 FPS game cadence and 90 Hz runtime, measured at the declared settings. Configured values are not acceptance evidence. Reduced-rate/reprojection modes must be labeled and do not close the 90 Hz gate. Same-simulation true stereo is required; AFR is optional and not equivalent.

Offline-only operation. Online play, game-asset redistribution, engine replacement, full physics reconstruction of enemies, dismemberment and decorative extras are outside scope. Required functionality cannot be dropped by an agent to declare completion.

## Current state

This is a development candidate, not a completed VR mod. Existing work includes XR mono, tracked camera/input, calibration, native pose adapters, equipment reads, contact math and menu/config tooling. Production stereo, native physical damage/defense, collision-resolved body follow and complete native/headset qualification remain open. See the execution ledger and handoff for qualified status; do not infer working features from settings or diagnostic proxies.

## Development controller controls

In gameplay, hold the **left stick click** and tilt that stick to select a native
D-pad direction (items, spells or equipment) without walking. Release and centre
the stick before walking again. A centred lone click still sends native L3 on
release with a bounded 120 ms hold. Native and DSVR menus use the left stick directly, without a modifier.
These routes have offline regression coverage; headset/native acceptance remains open.

Controller-profile binding changes require a game restart. Existing controls stay
active until then, including when an older config enables `binding_live_reattach`.

## Build and test

```powershell
pwsh -File tools/auto_verify.ps1
```

The verifier configures/builds Release, runs CTest and doctor, then refreshes the development package on success. It does not install or launch DSR. Debug and native/headset qualification are separate gates. Exact commands, packaging and protected operation are in TESTING. Sources/tests define config defaults; never copy defaults from historical previews.

Original code: MIT, AI-assisted. See [LICENSE](LICENSE) and [third-party notices](third_party/README.md).
