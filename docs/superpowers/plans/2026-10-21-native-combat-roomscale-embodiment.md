# Working physical combat, configurable locomotion, embodiment, and comfort

Date: 2026-10-21. Status: **IN PROGRESS — L1 settings and reference-frame groundwork; native/headset acceptance still required.**

This plan supersedes the reduced completion scope in
`.empryo/plans/plan-mvg263vh-qi35s6-t_mvh3zyf7_59650u0t024z_1.md` for these features.
The earlier offline result is a regression baseline, not completion of the requested product.
Implementation has begun. Planned behavior remains incomplete until its listed evidence and exit criteria are met.

Revision: movement orientation, head/capsule collision, physical crouching and comfort requirements added. The user explicitly selected **head/controller modes with HMD as the default**. This replaces the earlier HMD-only requirement throughout this plan.

## 1. Required result

- Physical weapon swings must cause real, correctly attributed native melee outcomes. Sword hits, shield blocks/parries, two-handed handling, and weapon/environment contact must work in an actual encounter.
- The left stick translates in a persisted, selectable **Head** or **Controller** orientation mode. **Head is the default.** Controller mode defaults its reference to the non-dominant controller and permits explicit left/right selection. Orientation determines forward/right; HMD **position** independently supplies physical displacement. Changing weapon handedness does not silently change the movement frame during a held input.
- The right stick controls intentional snap or smooth turning. Looking around, strafing, body animation, and physical walking must not introduce artificial view rotation.
- Physical walking must retain direct tracked camera movement and move the native avatar through the game's collision/movement system. Physical movement must continue while either stick is used.
- A separate head collision query must prevent visibility/interactions through solid walls using a controlled fade and return-to-valid-space guidance. The native body collider and tracked head offset have distinct responsibilities.
- Physical crouching must lower the tracked camera, estimated body, native movement collision height, and relevant native hurt volumes coherently. Ducking must permit geometric projectile avoidance and clearance beneath suitable low obstacles; lowering only the view or granting synthetic invulnerability is insufficient.
- Provide adjustable dynamic comfort vignetting and a gravity-aligned artificial locomotion frame. Artificial turn/bob/animation must not inject camera pitch or roll; physical tracked HMD pitch, roll and yaw remain intact.
- Head, torso, pelvis, arms, legs, feet, and held equipment must form one coherent body during physical walking, stick walking, turning, leaning, crouching, and combat.
- Native collision, defense, damage parameters, stamina, poise, invulnerability, and actor lifetime rules remain authoritative.

**Completion rule:** working native damage and headset-tested movement/embodiment are mandatory. An unavailable binding, synthetic proving ground, green offline suite, or future-test document cannot close this plan. Runtime guards for invalid tracking, expired actors, or an unsupported executable remain necessary; they are fault handling, not a replacement deliverable. A failed experiment creates the next diagnostic task, not a smaller definition of success.

Arm-swing locomotion, running in place, and redirected walking are background techniques in the request. They are not substitutes for the required stick and physical walking modes and are not added in this implementation sequence.

## 2. Grounded baseline and specific gaps

Reviewed source through a refreshed `DSVR` codebase-memory index, plus `MODDING_PLAN.md`, `docs/TESTING.md`, and the prior execution plans. The existing Debug and Release builds and 86/86 suites passed in the preceding verification; that establishes no native damage or headset-quality result.

| Area | Current observation | Required change |
|---|---|---|
| Native combat | `combat_native.*` reads equipment; `physical_damage_bound` is false. `ClassifyWeaponContact` names geometric events only. | Add verified native contact/damage execution and outcome observation. |
| Native research | `CreateDamage_NoCollision` reaches contact bookkeeping, without an established end-to-end damage contract. | Join contact-side and health-side evidence using a genuine native melee hit. |
| Weapon geometry | `ArmIkTick` uses a `.9f` physical blade length; the proving ground includes proxy targets. | Use equipped weapon geometry, real actor hurt/contact volumes, and rotational sweeps. |
| Stick movement | Config/input allow head/left-hand/right-hand movement references and a head-directed toggle. | Consolidate into explicit Head/Controller mode, HMD default, with a separately persisted controller reference and deterministic legacy migration. |
| Physical movement | `UpdateRoomscaleInput` refuses roomscale output when either stick moves. | Compose physical displacement, stick translation, and turning without dropping tracking samples. |
| Camera reconciliation | `CameraApplyFirstPerson` credits a projection of root delta along roomscale velocity, with a 150 ms bound. | Establish motor ownership and attributable resolved motion; replace the heuristic where it cannot distinguish stick/physical/forced motion. |
| Body | Post-animation pose editing, calibrated arm/torso helpers, and leg solving exist. | Qualify final pose ownership, pelvis/foot targets, floor contacts, continuity, and rendered equipment alignment. |
| Head collision/crouch | The existing plan has a wall fade concept but no qualified head-query, dynamic motor-height, or matching native hurt-volume contract. | Recover native geometry/query ownership and implement/test head obstruction plus geometric crouch clearance and avoidance. |
| Comfort | `comfort_vignette` and `vignette_strength` settings exist; the inspected `xr.cpp::BlitToEye` boundary does not establish dynamic comfort behavior. | Trace actual shader/composition wiring and add a coherent per-frame vignette envelope, configuration and capture tests. |

Existing relevant tests include `test_input.cpp`, `test_camera_writes.cpp`, `test_roomscale_math.cpp`, `test_vr_heading.cpp`, `test_arm_pose_native.cpp`, `test_arm_ik_math.cpp`, `test_body_pose.cpp`, `test_combat_native.cpp`, `test_combat_contact.cpp`, and `test_combat_math.cpp`. Extend these boundaries instead of accepting helper-only demonstrations.

## 3. Architecture and route selection

### Combat routes

**Preferred: adapt tracked contact into the verified native melee-contact pipeline.** Native weapon/attack context, target validation, defenses, and result processing stay together. The main work is recovering ownership, scheduling, collision semantics, and ABI.

**Second route: construct a legitimate melee context using verified native constructors and submit tracked contacts to the verified damage dispatcher.** Use this if the native collision path cannot accept tracked weapon poses. It needs independent proof of context initialization, cleanup, attack parameters, defense checks, and exactly-once side effects.

**Third route: integrate at the native melee update that owns attack context and collision queries.** Use physical pose/contact to drive that update while replacing only the local weapon animation dependency. This is more intrusive and requires stronger animation/movement regression coverage, but preserves a complete native rules path.

Select using evidence from milestones N1/N2. None of these routes permits direct HP subtraction, calling the health setter as an attack API, pressing attack buttons in response to gestures, or presenting a canned animation as physical combat. Any native record must have a proven constructor/owner/lifetime; do not retain transient pointers or invent a structure from guessed offsets.

### Shared movement and body model

Keep the tracking-to-world transform, native motor root, HMD yaw, visual body orientation, and movement command basis separate. Publish a coherent snapshot carrying tracking sequence/time, space epoch, calibration revision, player lifetime, and native update ID. Bind movement, pose, and contact observations to compatible snapshots; rendering may reuse them but must never advance gameplay twice.

Left-stick direction is `stick_x * reference_right_xz + stick_y * reference_forward_xz`, using the selected reference's normalized horizontal axes in world space. Convert to the verified native movement basis once, at the motor/input boundary. Diagonals are magnitude-clamped. The two modes have deliberately different semantics:

| Mode | Forward reference | Looking around during held forward input |
|---|---|---|
| Head (default) | Current HMD horizontal heading | Head yaw steers the trajectory. Free head rotation is preserved, but travel direction is not decoupled from gaze. |
| Controller | Selected controller's calibrated horizontal heading | Head yaw/pitch/roll do not steer travel while the controller keeps a stable heading. Controller yaw intentionally steers travel. |

Do not label either mode universally preferable or promise a straight trajectory while changing gaze in Head mode. Changing mode, reference controller or automatic non-dominant assignment takes effect after left-stick release; no mid-stride direction jump. A missing/stale selected reference neutralizes artificial translation without stopping valid physical walking or switching reference silently.

Looking steeply up/down in Head mode uses the last valid HMD yaw from the same tracking epoch. In Controller mode, a calibrated aim/grip axis and a near-vertical hysteresis cone prevent a dropped/upward-pointing controller from flipping planar forward through a projection singularity. Retain the last stable planar heading only while current tracking is valid; if no stable heading exists, require a valid heading and neutral/rearm before movement. Intentional horizontal controller turns still steer normally.

Physical displacement is the difference between coherent HMD positions in tracking space, mapped by the tracking-to-world transform. It is not rotated again by where the user looks. Keep sampling during stick translation/turning. Maintain an explicit physical offset/residual and reconcile only resolved motor movement attributable to that physical request, so native-root follow does not count physical walking twice.

The camera retains direct tracked head motion. Body smoothing, joint constraints, and foot placement cannot feed rotation or delay back into the HMD view. Artificial turning pivots around the current horizontal HMD position rather than orbiting an offset player around the original tracking origin.

### Collision, crouching and comfort contracts

Model the virtual anchor as the native motor's collision body (capsule or verified native equivalent). Stick motion moves that body through native collision. Physical movement initially changes the tracked head/body offset within a bounded lean allowance; meaningful walking requests collision-resolved body follow through L2. This combines leaning freedom with actual avatar follow, rather than leaving the avatar behind indefinitely. Ledge/support queries must keep a small look-over lean from dragging the body off an edge.

Use a separate swept head sphere/volume against verified solid scene geometry. On head obstruction, retain tracked pose internally and obscure invalid world imagery; do not force the physical camera to stop, counter-translate the world, or add pitch/roll. Fade must cover occluded views before they expose hidden space. Show a restrained return cue tied to the last valid head/capsule relation; do not reveal the obstructed world through the cue. Recover only on verified clearance on a valid side of the obstruction, with hysteresis. A controller cannot attack/interact through a wall just because the view is faded.

Physical crouch uses HMD height above the calibrated tracking floor plus current native ground support. Preserve stature, world scale, foot contact and seated offsets. Adjust collider height and center with a floor-anchored base and verified native APIs; avoid globally scaling the collider, armor or model. Movement collision and damage hurt volumes are separate: both must match the crouched body through native queries/pose consumers. Standing under a low ceiling requires a clearance test before collider expansion; the view continues tracking physical height and uses obstruction fade if the physical head rises into the ceiling. No forced camera lowering and no launch/teleport from collider growth.

Interpret **horizon lock** as a level artificial locomotion frame, not clamped physical head orientation. Compose a gravity-aligned artificial yaw frame with the full tracked HMD quaternion. Suppress inherited walking bob, slope-induced camera tilt, animation shake and artificial pitch/roll. A person must still be able to look up/down or tilt their head normally while walking. Optional smoothing acts on artificial speed/effects or estimated body pose, never on the tracked view.

Dynamic vignette is a user-adjustable peripheral mask, not a projection/FOV distortion. Use artificial linear speed/acceleration and artificial turn rate, with an extra bounded contribution when artificial translation coincides with rapid physical head rotation. Physical looking/walking alone should not trigger the default comfort mask. Derive angular speed from coherent quaternion samples; reset on epoch change instead of treating a recenter as rapid rotation. Fade strength smoothly with bounded attack/release, feathering and minimum visible aperture. Comfort assistance can be disabled independently of head-collision occlusion. These are comfort aids to qualify with the operator, not guarantees against nausea.

## 4. Execution order

`N0 baseline -> N1 native hit trace -> N2 first real physical hit -> N3 combat breadth`

`N0 baseline -> L1 orientation modes -> L2 native physical follow -> L3 head collision/crouch`

`L2 -> E1 coherent body; L3 + E1 -> final crouch/hurt-volume integration`

`L1 + L2 -> C1 comfort composition`

`N3 + L3 + E1 + C1 -> Q1 integrated encounter and release gate`

Do read-only damage investigation early; do not postpone combat behind unrelated stereo, AFR, UI polish, or rendering work. Integrate one native mutation path at a time so each observation has a clear cause.

### N0 — Establish reproducible evidence and regression fixtures

Files: `docs/TESTING.md`, `docs/VERIFICATION_RUNBOOK.md`, existing trace/test tools; new focused trace helpers only where needed.

1. Record exact game EXE hash/version/module identity, native analysis database identity, DSVR DLL/config hashes, runtime route, equipped weapon/PARAM IDs, disposable character and repeatable encounter.
2. Reuse the project's existing same-account offline/disposable-save procedure and preserve both save roots plus Steam metadata; do not restart the already-resolved lab-account discussion. Planning does not launch or deploy anything.
3. Capture unmodified native sword hit, miss, shield block, parry, and two-handed attack as comparison cases. Record before/after health, stamina, poise/reaction where observable, attacker/target identity and simulation update IDs.
4. Add bounded trace events for tracking, movement intent, native root/camera motion, committed weapon pose, contact, dispatch, and native outcome. Keep raw native ABI evidence external to distributable mod artifacts.
5. Add replay/fixture capture with explicit provenance. Fixtures must preserve expected values independently of the implementation under test.

Exit: one reproducible encounter and synchronized evidence sufficient to investigate a hit and movement discrepancy. Offline green tests alone do not satisfy this milestone's native observations.

### N1 — Recover the complete native melee contract

Files: `MODDING_PLAN.md`, new `docs/NATIVE_MELEE_CONTRACT.md`; eventual signatures/bindings in `src/core/combat_native.*` only after verification.

1. Start from the retained contact chain: registration/thunk `0x1400200C0` / `0x1404B0EC0`, wrapper `0x140490F40`, then `0x1403C9B50 -> 0x1403C8DD0/6030 -> 0x1403C6850 -> 0x1403C7520 -> 0x1403C4020`. These are research leads at preferred image base, not ready-to-call APIs.
2. Repair split control-flow boundaries, including type-1 `0x1403C4140 -> 0x1425BA7DC -> 0x1403C4162`, and compare type-2 `0x1403C4210` consumers. Verify instructions and actual callsites, not truncated pseudocode signatures.
3. Work backward from observed native health changes. Existing health setter `0x140322910` and PlayerIns vtable+`0x3E8` lead `0x140359240` are observation anchors, never substitutes for melee damage. Recover the split continuation `0x14280602C -> 0x14104C734 -> 0x14035925B` where relevant.
4. During the N0 normal melee hit, use narrowly filtered read-only hooks/debugger traces to connect attacker, equipped attack context, collision/contact consumer, defenses, result dispatcher, and authoritative outcome. Compare miss, block and invulnerable target cases. Identify every side-effect owner.
5. Document calling convention/register and stack arguments, structure layouts, constructors/destructors, thread/update ownership, reentrancy, actor handles/generations, parameter lookup, return semantics, and callback latency. Pin signatures to the exact supported executable and use module base plus verified RVA.
6. If one route stalls, state the unanswered question and pivot to a different producer/caller or the health-side trace. Do not repeat broad name searches or call absence of proof completion. Choose one route from section 3 with a complete contract before injection.

Exit: an ordinary native attack has an evidence-backed path to an attributed damage/defense outcome, and the chosen integration point has a complete narrow contract. Finding a setter or an event named Damage is insufficient.

### N2 — Deliver the first real physical weapon hit

Files: `src/core/combat_native.h/.cpp`, `combat_contact.h`, `combat_math.h`, `arm_ik.cpp`; introduce `combat_runtime.h/.cpp` for contact lifecycle/dispatch ownership, and register it in build/test targets.

1. Define a narrow contact request containing attacker/target lifetime IDs, equipped weapon/attack context, committed pose/update ID, contact point/normal, swing ID, and physical kinematics. Define outcomes that distinguish rejected, submitted, and native-confirmed results. A successful call return alone is not confirmed damage.
2. Schedule native execution on the proven simulation owner, once per contact/update. Revalidate equipment, scene, actor lifetime, team/target eligibility, tracking/calibration epoch, and native attack state at consumption time. Expire queued work across transitions.
3. Derive the first sword's actual grip/blade extent from verified equipment/model/collision data. Sweep both translation and rotation against real native target geometry; the synthetic target and fixed `.9f` reach cannot drive production damage.
4. Arm a physical attack from measured physical weapon motion, including angular tip speed. Keep artificial turn, stick translation, recenter, tracking jumps, and root movement out of attack energy. A deliberate advancing swing still hits through its real world-space sweep. Head bob alone must not create a swing.
5. Create/submit the valid native context through the N1 route. Let native rules compute damage and associated costs/effects. Begin with one known sword/attack profile; map physical slash/thrust to verified native variants before introducing any tuned energy scaling.
6. Correlate native result callbacks/state changes with target generation, attack/swing ID, update ID and health/outcome evidence. Emit confirmed haptics only after the relevant native outcome. A blocked hit may have a valid zero-HP-damage outcome with native stamina/reaction evidence.
7. Implement exactly-once accounting across repeat render visits, two eyes, sustained overlap, asynchronous outcomes, multi-target swings, death and pointer reuse. Define a new swing/contact episode explicitly; a timer alone cannot allow resting damage.
8. Test unbound/invalid ABI, expired target, equipment swap, duplicate submission, delayed result, invulnerability, no-contact and native acceptance at the adapter boundary. Then run a physical hit/miss comparison in game immediately.

Exit: ten deliberate physical sword hits damage the intended vulnerable target through native processing, ten misses do not, and no attack button/canned swing substitutes. Repeat a resettable fixture to compare native context/results. Save logs and video before expanding breadth.

### N3 — Finish physical weapon interactions and native rules

Files: `combat_runtime.*`, `combat_native.*`, `combat_contact.h`, `combat_math.h`, `weapon_grip.*`, `arm_ik.cpp`, haptic input boundary; related combat/weapon tests.

1. Replace guard/target proxies with verified native hurt volumes, shield coverage/facing, environment collision and attack threat geometry. Establish earliest contact/time of impact with swept shapes or error-bounded substeps, including fast rotational swings and moving targets.
2. Implement physical shield blocking, parry timing/contact and guard break through native defenses. Confirm damage mitigation, stamina debit, poise and reactions; do not use shield height alone or a synthetic 200 ms window as qualification.
3. Qualify native two-hand stance/stats and support grip attach/release. Use one rigid weapon frame, stable wrists and actual handle positions. Restore stance/context on release, weapon replacement, death, menus, focus/tracking loss and shutdown.
4. Implement wall/prop impacts using native collision/rules and bounded feedback. Prevent damage through nearer solid occluders. If visual weapon constraints are required, specify the visible-versus-contact pose contract and use the same committed contact pose for hit tests.
5. Cover slash, thrust and blunt families, short/long weapons, left/right/off-hand, two-handed, upgraded and damage-type variants. Maintain a manifest of supported stock melee families with at least one tested representative each; inventory unsupported mechanics explicitly and complete them before the final all-family claim.
6. Verify native status application, resistances, invulnerability, multi-target ordering and no repeated stamina/poise/status charges. Clearly separate contact feedback from confirmed hit/block/parry feedback.

Exit: a physical sword/shield encounter and two-handed encounter are playable, including blocking/parrying and environment interaction, and the declared stock melee-family matrix passes. Ranged attacks and spell gesture systems are separate scope.

### L1 — Implement selectable head/controller continuous locomotion

Files: `src/core/input.*`, `pad_math.h`, `vr_heading.h`, `camera.*`, `src/common/config.*`, `mod_menu.cpp`, shipped TOML defaults; tests for input/config/menu/turns/heading.

1. Consolidate settings into `movement_orientation = head|controller` (default `head`) and `movement_controller = non_dominant|left|right` (default `non_dominant`). Explicit new settings take precedence. Migrate legacy `movement_reference=head` to Head and `left_hand`/`right_hand` to Controller with that explicit reference. Retire `head_directed_move=false` as a native-axis gameplay bypass, with a logged migration; absent/unknown orientation defaults to Head while invalid new config input is rejected consistently. Persist canonical values and expose the mode/reference in the VR menu and Setup defaults/presets.
2. Use the coherent current selected orientation for every stick translation sample. Include selected controller pose validity and epoch in the input contract. In Controller mode the reference is independent of HMD look direction, including when the physical movement direction differs. Implement stable horizontal extraction, near-vertical/lowered-arm hysteresis and neutral/rearm behavior; never substitute HMD heading on controller loss. Keep native motor basis conversion separate from view/body heading and do it once. Preserve analog magnitude, forward/back/strafe and diagonal normalization.
3. Right-stick snap and smooth modes remain selectable. Specify snap angle/rearm and smooth angular speed in config/UI; use time-based integration. Rotate the rig about current HMD horizontal position, preserving position through a turn while the player is physically offset from origin.
4. Keep native movement-facing animation and optional lock-on combat state from feeding artificial camera yaw. Test lock-on explicitly: its attack/target behavior may remain native, but it must not silently change requested translation or steal right-stick turning in immersive gameplay.
5. Neutralize controls through menus/calibration, stale tracking, focus loss and epoch changes; require the established release/rearm rules. Keep keyboard/menu access and prior controller chords working.
6. Apply orientation/reference/dominant-hand setting changes after left-stick release without restarting. Test save/reload, legacy migration, automatic non-dominant resolution, explicit overrides and controller reconnect. Physical walking remains available with a valid HMD when only the selected controller is unavailable.

Exit: in Head mode cardinal/diagonal movement follows HMD yaw at 0/90/180/270 degrees while either hand aims elsewhere. In Controller mode a stable selected controller keeps the same travel heading while the user looks around, and intentional controller yaw steers travel. Looking up/down does not change speed, dropping/raising the reference arm does not produce an unintended reversal, and settings transitions do not jump direction. Snap/smooth release stops artificial turning in both modes.

### L2 — Integrate physical walking and native collision

Files: `input.*::UpdateRoomscaleInput`, `camera.*::CameraApplyFirstPerson`, `pad_math.h`, `vr_heading.h`; introduce `locomotion_state.h/.cpp` for shared movement state and, if necessary, `locomotion_native.h/.cpp` at a verified native motor seam. Tests: input, roomscale math, camera writes, heading, and new motor-contract tests.

1. Trace XInput consumption to native player movement, collision resolution and root publication. Establish logic/physics/render root timing, locomotion speed/deadzone response, rotation ownership, stairs/slopes, and forced-motion paths. Observe ordinary walking rather than inferring it from teleport experiments.
2. Keep physical HMD position sampling active while translating or turning with sticks. Compose physical and artificial intents before native collision, with magnitude/velocity limits that do not discard physical samples. Remove the current either-stick exclusion.
3. Replace fixed speed assumptions with measured motor response. Use the XInput route only if it can meet displacement, collision and mixed-input requirements. Otherwise move to the verified native movement-intent consumer; do not bypass collision with actor/root writes.
4. Track requested physical displacement, pending residual, native accepted movement, artificial input, and forced motion by simulation tick. Demonstrate attribution at the motor seam; projection along recent velocity is not enough under simultaneous stick motion, knockback, stairs or wall contact. If the input bridge cannot provide the needed ownership, instrument/use the native motor seam before claiming mixed-mode support.
5. Reconcile accepted physical follow against the tracking offset exactly once. Do not consume artificial locomotion, knockback, animation/root motion, or a teleport as physical-follow credit. Prevent unbounded residual accumulation, replay after a pause, and delayed catch-up lunges.
6. Maintain direct tracked view translation. At a collision-limited wall, keep the avatar out of solid geometry and publish obstruction/follow state for L3 rather than snapping or dragging the tracked camera. Discard blocked follow debt so stepping back does not cause a lunge. Stairs/slopes use native grounded motion; physical crouching does not become a jump command. Distinguish a bounded lean/peek from walking intent using support state and calibrated body offset; do not claim head/controller tracking can perfectly distinguish those actions.
7. Treat space epoch, recenter, load/teleport, death and focus loss as explicit baseline changes. Preserve calibration measurements while clearing transient integration/turn/contact history.
8. Test physical-only, stick-only and combined motion, including opposing directions, diagonal saturation, turning during walking, native forced motion and wall contacts. Test 30/60/72/90/120 Hz sampling with independent native update rates and repeated render frames.

Exit: physical walking remains one-to-one in open space, the body follows through native collision, simultaneous stick use never drops physical tracking, and neither double movement nor root-driven camera yaw occurs. Numeric qualification targets are in section 5.

### L3 — Implement head obstruction and physical crouch collision

Files: `locomotion_native.*`, `locomotion_state.*`, `camera.*`, `calibration.*`, `body_pose_math.h`, `arm_ik.cpp`; add `head_collision.h/.cpp` and focused native query/crouch fixtures. Final occlusion composition uses C1's render boundary. Document the verified contracts in `docs/NATIVE_LOCOMOTION_CONTRACT.md`.

1. Recover the native player motor shape, scene sweeps/overlaps, shape resize/update ownership, ground support, actor hurt volumes and projectile hit-test consumers. Verify whether hurt volumes follow the final posed skeleton or an earlier collision pose; visual IK does not prove geometric dodging. Bind only established native query/update contracts.
2. Add bounded head-volume sweeps between valid tracked poses, including thin walls, corners, low ceilings and doors. Qualify the collision layers: scenery occludes the view; incoming projectiles still cause native damage rather than stopping the head camera. Publish obstruction identity/clearance, last safe side/pose and an occlusion state tied to the current epoch.
3. Implement clear/approaching/obstructed/recovering states with margins and hysteresis. Fully occlude invalid world imagery, keep UI return guidance readable, and require verified valid clearance/capsule relation before revealing the scene. A large physical step through a thin wall cannot instantly reveal its far side. Recenter cannot clear obstruction without geometry checks. Never push the virtual environment against ongoing head motion.
4. Measure crouch depth against standing/seated calibration and tracking floor, not the native camera's animated height. Compute bounded target motor height/center with its base on the supported floor. Preserve lateral collision radius unless the native contract requires a justified change; enforce valid geometry dimensions and native broadphase/contact refresh.
5. Integrate crouch pose and native hurt volumes with E1 at the correct simulation stage. A geometric duck moves the head/torso target below a projectile path while normal native hit detection still applies. Do not grant a dodge state, invulnerability or a global damage multiplier. If the game lacks a direct crouch API, implement through the verified native shape/pose consumers; view-only crouching cannot close this milestone.
6. Sweep/check clearance before expanding a crouched collider, handle moving overhead obstacles and maintain floor contact. If the wearer stands under a virtual low ceiling, retain a valid constrained body state and occlude the invalid view until clearance returns; do not clamp the physical HMD pose. Clear transient crouch/obstruction state on actor/epoch replacement while retaining saved stature measurements.
7. Test low-bar traversal with sufficient native floor/navigation clearance, partial and deep crouches, head leaning versus crouching, tilted HMD, stairs/slopes, seated play, moving doors, thin-wall steps and ledge peeking. Define a declared supported obstacle geometry range rather than promising passage through arbitrary level meshes.

Exit: head obstruction cannot expose or interact through hidden solid space, physical ducking changes real native collision/hurt geometry, qualified low obstacles are traversable while crouched, and standing under a ceiling cannot clip/launch the avatar. A crouched body must be visibly aligned and have matching native projectile hit/miss evidence.

### C1 — Implement dynamic vignetting and a stable artificial horizon

Files: `src/core/xr.cpp::EnsureBlit/BlitToEye` and its shader/composition resources, `camera.*`, `locomotion_state.*`, `src/common/config.*`, `mod_menu.cpp`, `src/setup/gui.cpp`; add pure comfort-envelope tests and final-composition fixtures.

1. Trace existing `comfort_vignette`/`vignette_strength` settings to actual shader use. Reuse those settings where appropriate; define validated aperture/feather, movement/turn activation thresholds, head-turn boost and effect attack/release controls. Offer Off/Low/Medium/High/custom without resetting the selected movement mode. New profiles start at Low; preserve existing saved on/off/strength intent during migration. Start qualification with a bounded 100 ms attack/300 ms release envelope, then record any tuning before acceptance runs.
2. Feed artificial translation/turn/acceleration and coherent HMD angular velocity into a render-only envelope. Add the requested stronger response during simultaneous artificial movement and rapid head rotation; also support translation-only and smooth-turn comfort assistance. Derive signals from resolved artificial locomotion where available so a blocked stick against a wall does not simulate nonexistent visual travel. Bound and reset derivatives after stalls/recenter/focus or epoch changes.
3. Apply a feathered peripheral mask per eye using angular aperture and the submitted view/FOV, with a coherent center/strength and no cross-eye mismatch. Keep the game's projection, runtime FOV and tracked pose unchanged. The effect must not use the existing projection/FOV scale as a substitute and must not narrow gameplay hit detection.
4. Compose L3 head-obstruction occlusion with higher priority than the optional comfort vignette. Disabling comfort assistance cannot reveal solid-wall interiors. Keep return guidance and essential menu access readable. Qualify immersive and theater behavior separately and avoid flicker/black-state leakage across transitions.
5. Enforce a gravity-aligned artificial frame and yaw-only turning. Remove inherited native bob/pitch/roll/shake from the view while preserving full physical HMD orientation. Quaternion fixtures must prove that a physical head tilt remains a matching view tilt while native animation or a slope cannot introduce extra tilt. Do not implement a screen-space horizon clamp that cancels real head motion.
6. Test effect signal thresholds, hysteresis, time-step invariance, attack/release, disabled behavior, rapid head turns at rest, physical walking alone, stick movement with head turns, snap/smooth turn and tracking loss. Capture final eye images to verify mask shape/feather and measure added GPU time without readbacks in the normal loop.

Exit: comfort controls persist and visibly affect the intended movement cases, the world view retains full tracked head motion, no artificial roll/pitch/bob is injected, and wall occlusion remains effective with comfort disabled. Operator comfort is recorded per setting; no universal sickness-prevention claim is made.

### E1 — Deliver coherent full-body embodiment

Files: `arm_ik.*`, `arm_pose_math.h`, `body_pose_math.h`, `arm_ik_math.h`, `head_rider.h`, calibration/weapon-grip state as needed; tests: arm IK/pose/native pose, body pose, calibration and camera boundaries.

1. Define an explicit local-player bone ownership set and final write order. Trace animation, pose-copy, skinning palette and equipment attachment consumers. Replace animation contributions to owned bones at the final proven stage; preserve native gameplay state updates and other actors. Test that a later writer cannot overwrite the committed result.
2. Solve from validated rest/calibrated dimensions and the shared L2 locomotion state. Solve pelvis/root-relative body orientation, torso/head/neck, corrected shoulders, arms/hands/twist, legs and feet in dependency order, then rebuild owned descendants and attachments consistently. Do not recalibrate from a moving animation every frame.
3. Preserve measured eye/head/controller offsets and consistent physical-to-avatar scale. Separate posture from stature. Moving the native root must carry the camera/body once; walking must not be interpreted as torso lean or stretch. Share crouch depth, support plane and constrained collider state with L3 so camera/body/hurt geometry cannot disagree about posture.
4. Estimate lower-body pose from tracked head/hands plus native locomotion and verified ground queries. Feet are estimated unless actual foot trackers are supplied. Add stable planted feet, gait phase, stride placement, foot lift/land, turning steps, floor normal and stairs support; match gait to resolved movement speed. Bound knee/elbow bend planes, joint twist and stretch without shrinking bones or pinching meshes.
5. Permit bounded head-versus-torso twist; make the estimated pelvis and feet turn naturally before anatomical limits are exceeded. Use continuous quaternion/angle handling through repeated 180/360-degree turns. Body orientation corrections must never rotate the HMD view.
6. Explicitly handle crouch/lean, unreachable targets, support-hand constraints and tracking loss. Blend body ownership transitions from valid states without stale-pose snaps; native ragdoll/death/required scripted sequences need defined temporary ownership and deterministic recovery.
7. Test final rendered armor/hands/weapons as well as CPU matrices. Compare standing, physical walking, stick walking and combined turning while swinging. Add independent known-pose fixtures and post-animation overwrite fixtures, not just solver self-consistency tests.

Exit: in headset the camera stays at tracked eyes, both hands/held equipment align, the pelvis follows the player, knees bend consistently, feet meet the floor, and native walking animation does not fight the owned pose. No foot-tracking accuracy claim is made from HMD/controller estimates.

### Q1 — Integrated native and headset qualification

Files: `docs/TESTING.md`, `docs/VERIFICATION_RUNBOOK.md`, `docs/ENGINEERING.md`, this plan and the original `.empryo` plan. Evidence under `build/verification/<run-id>/`.

1. Build all production/test targets in Debug and Release. Run focused tests after each vertical slice, then the full suites on the final candidate. Package only the exact passing DLL/config combination and record hashes.
2. Run protected desktop native tests for motor/collision, crouch/hurt geometry and the verified melee adapter before requesting headset time. Capture the actual comfort/obstruction composition as well as pure policy tests. Record unresolved failures with a reproducible input/trace and fix them.
3. Guide the operator through section 5 on that exact package. Save synchronized trace/log/video, config, environment/build identity, observations and verdict per case. Repeat failed cases after correction and rerun affected integration cases.
4. Mark the plan complete only when every required gate passes. Keep the work open when a native contract or physical behavior is unresolved. Do not replace those tasks with a blocked-feature table or a new offline-only completion scope.

## 5. Acceptance matrix

These are proposed engineering pass thresholds, not measured performance or platform guarantees. Record them before qualification. Change a threshold only with a documented engineering reason; never relax one simply to label a failing run successful.

| Gate | Procedure | Required result |
|---|---|---|
| Real damage | Ten deliberate hits and ten misses against a repeatable vulnerable target with one qualified sword; record swing/contact/context/target generation and native outcome. | Each hit has exactly one expected native result; each miss has none. HP or native defense outcome is attributable to that contact. Native comparison context produces matching rules. |
| Physical rules | Repeat against shield block, parry, dodge invulnerability, solid wall and two-handed stance; inspect native costs/reaction/status. | Correct native mitigation/invulnerability, stamina/poise/reaction, no through-wall hit or duplicate side effects. |
| Resting and false hits | Hold overlap 10 s; walk, strafe and right-stick turn with a still weapon; recenter; lose/recover tracking; swap equipment and reuse actor addresses. | Zero unintended attacks, resting repeat damage or stale-target calls. A fresh valid swing can attack again. |
| Sweep and timing | Replay the same captured slash/thrust, including fast rotational motion, with varied tracking/native rates and duplicate render visits. | Same target/contact episode and attack variant; no tunneling/duplicate outcomes. Deterministic native fixture has equal damage/costs; noisy human repetitions are analyzed separately. |
| HMD-relative stick | All cardinal/diagonal stick directions at HMD yaw 0/90/180/270, both hands pointing elsewhere, head pitched up/down. | Horizontal intent within 2 degrees of expected HMD-relative direction; diagonal speed no greater than full cardinal speed; no controller-derived steering. |
| Controller-relative stick | Repeat cardinal/diagonal tests with the selected controller heading fixed and HMD scanning left/right/up/down; then intentionally yaw the reference controller. Test non-dominant and explicit left/right. | Head scanning changes view without changing the intended travel heading; intent follows selected controller yaw within 2 degrees. The unselected controller does not steer. |
| Reference stability/preferences | Lower/raise the selected hand through vertical, lose/recover controller tracking, change orientation/reference/handedness while holding forward, then release/rearm and restart. | No accidental forward reversal or silent HMD fallback. Artificial translation neutralizes on invalid selected tracking while valid physical walking continues. Setting changes apply after neutral, persist, and legacy hand settings migrate as specified; fresh profiles default to Head. |
| Physical displacement | Mark and walk a 1 m path forward/back/sideways on open level ground, return to start, then repeat with stick input. | View maps physical motion directly; after native follow settles, avatar displacement attributable to the physical path is within 5 cm or 5%, whichever is larger. No doubled travel; no discarded samples during stick use. |
| Follow latency | Log coherent HMD displacement, issued movement, resolved root and camera at native update boundaries during ordinary walking. | Physical-follow root response p95 within 100 ms on the qualification machine, without filtering/delaying the tracked camera to obtain that number. Report tracking/native rates and saturation. |
| Turn stability | Ten 360-degree turns each direction, physical and snap/smooth, standing and moving with physical offset from origin. | No wrap snap, accumulating twist, uncommanded artificial yaw, or turn-induced positional orbit; no artificial turn after neutral input is consumed. |
| Collision/forced motion | Walk into/along wall, step back, use stairs/slope, then repeat with mixed stick motion and a native knockback. | Native body collision remains valid; no floor penetration, stored-debt lunge or incorrect roomscale credit for forced motion. Wall occlusion response remains coherent. |
| Head obstruction | Lean/step into a thin wall, corner, closed door or low ceiling; take a larger step across; recenter while obstructed; repeat with comfort vignette Off. | Invalid world imagery is obscured before hidden space becomes visible. No far-side reveal, through-wall interaction or forced view push-back. Clearance/valid-side checks and hysteresis govern recovery; return guidance remains readable. |
| Lean/ledge anchor | Lean over a supported low obstacle/ledge within the configured allowance, return, then take a deliberate supported step. | Lean offset does not pull the native body off the ledge; supported walking still moves the avatar. No claim of perfect lean/step classification from head/controllers; tested thresholds and edge behavior are recorded. |
| Physical crouch/clearance | Compare standing and crouched traversal under a qualified low obstacle; physically stand under the ceiling; repeat looking down without lowering HMD position and in seated mode. | HMD position lowers view, collider/body height and relevant native hurt geometry together. Looking down alone does not shrink the collider. Qualified low space is traversable crouched; blocked expansion causes no penetration, launch or camera clamp. Saved stature/scale does not change. |
| Geometric ducking | Use the same verified projectile trajectory that intersects the standing upper body; duck and repeat, then use a lower trajectory intersecting the crouched body. Observe native hit tests/outcomes. | Upper trajectory misses the crouched hurt volume and hits the standing one; lower intersecting trajectory still produces normal native damage. No synthetic invulnerability, damage suppression or view-only dodge substitutes. |
| Dynamic vignette | Compare artificial travel, travel plus fast head turns, smooth turns, physical-only motion and rest. Test Off and Low/High, restart, vary render rate, and capture both eye outputs. | Requested movement cases produce a smooth feathered mask with bounded aperture/envelope; the combined case has the configured boost. Physical-only look/walk stays clear by default. Off removes only the comfort effect. No projection change, eye mismatch or stale black frame. |
| Artificial horizon/tracked pose | While stationary and moving, physically pitch/roll the HMD; separately inject native bob/animation/slope tilt and perform right-stick turns. Compare view quaternion with the calibrated full tracked pose plus artificial yaw. | Physical tilt is preserved within 1 degree in the CPU pose fixture; unintended artificial pitch/roll residual is <=0.5 degree. Walking/turning does not clamp tracked head tilt or inherit native camera bob/shake. |
| Body and attachments | Standing/lean/crouch, physical/stick/combined walking, overhead/cross-body swings, two-hand support, slow/fast full turns. | Tracked hands/eyes align within calibrated tolerance (target <=3 cm in reachable poses); settled floor contacts target <=2 cm error; no joint flip, mesh collapse, animation overwrite or attachment separation. Qualify visual results as well as logs. |
| Transitions | Menu/calibration cancel/commit, focus loss, runtime pause/recovery, load, death/respawn, equipment and model replacement. | No stale input, contact, actor pointers or motion debt; saved measurements persist; supported gameplay restores tracking/body/combat correctly. |
| Encounter | Complete the fixed sword/shield and two-handed encounters using physical contact, physical and stick walking, physical ducking, both orientation modes and snap/smooth turning; compare comfort settings. | Operator confirms playable coherent embodiment and predictable controls; all earlier native/trace gates pass on the same package. Discomfort or a failed required case triggers correction and rerun. |

At least one tested representative per declared stock melee family is required for broad melee support. The first sword success is an intermediate milestone, not completion of the full weapon matrix.

## 6. Validation commands

Run from the workspace root after implementation; register new targets before using these filters.

```powershell
cmake --build build --config Debug --parallel 4
ctest --test-dir build -C Debug --output-on-failure -R 'combat|input|roomscale|camera_writes|vr_heading|turn|arm_|body_pose|calibration|weapon_grip|config|mod_menu|locomotion|head_collision|crouch|comfort|composition'
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

Native observation and operator-guided headset qualification follow separately; CTest cannot establish an injected melee ABI or subjective body/locomotion quality.

## 7. Research and implementation discipline

- Each investigation must name a concrete missing fact, evidence to collect, and next callsite/consumer to inspect. For native-evidence blockers, pin Ghidra analysis to `C:\\Tools\\DarkSoulsRemaster_De\\analysis-full\\DSRFull.gpr` (read-only primary). First use `npx -y rea-agents@4.0.1` against the exact target; if its provider yields no useful observations, use targeted REA MCP Ghidra probes. The sibling `analysis-full-attack\\DSRFull.gpr` is a repair workspace only; never use it as the primary source. Do not repeat broad name searches; pursue concrete callers, xrefs, instructions and data-flow until the evidence question is answered or the environment blocks the tool.
- Preserve findings and failed hypotheses so subsequent work does not repeat them. Record game-build identity and evidence paths alongside every binding decision.
- Keep integration changes narrow. The long `PoseUpdateDetour` should publish the committed pose to the combat owner; native contact lifecycle belongs in `combat_runtime`, not an expanding render/IK hook.
- Do not claim guaranteed discovery dates for unknown native contracts. The obligation is to pursue and test the working integration, and to keep incomplete requirements open until demonstrated.
- This plan requires no new headset, foot tracker, SDK migration, stereo renderer, redirected walking or gesture locomotion to begin. Reuse current native C++/OpenXR and existing qualification procedures.

## 8. Design references

- [Meta: Locomotion types](https://developers.meta.com/vr/design/locomotion-types/) — physical one-to-one movement and artificial slide locomotion; checked 2026-10-21.
- [Meta: Artificial locomotion input maps](https://developers.meta.com/horizon/design/locomotion-input-maps) — controller slide and snap/smooth turn guidance; checked through the Meta documentation tools on 2026-10-21. This is design guidance, not a proposal to add Unity/ISDK to DSVR.
- [Meta: Locomotion user preferences](https://developers.meta.com/horizon/design/locomotion-user-preferences) — head/hand direction mapping is preference-dependent; checked 2026-10-21. The Head default and controller-reference policy are the user's choice and this plan's design decisions, not a universal player-ranking claim.
- [Meta: Locomotion virtual environments](https://developers.meta.com/horizon/design/locomotion-virtual-environments) — head collision, occlusion, bounded leaning and dynamic capsule/ceiling considerations; checked 2026-10-21. These concepts do not establish a DSR native collider ABI.
- [Meta: Locomotion comfort](https://developers.meta.com/vr/documentation/unity/unity-isdk-locomotion-comfort/) — angular-aperture feathered vignettes responsive to locomotion signals; checked 2026-10-21. DSVR will implement the effect in its native composition path, without an ISDK dependency.
- [Meta: Comfort guidelines](https://github.com/meta-quest/agentic-tools/blob/main/skills/hz-immersive-designer/references/comfort-guidelines.md) — player-controlled camera motion, avoiding added roll/shake, visual stability and low head-motion latency; checked 2026-10-21. Preserving the full tracked HMD pose while restricting artificial motion to yaw is this plan's implementation contract.
- [Locomotion Vault](https://locomotionvault.github.io/) — user-provided taxonomy context; core movement scope remains physical walking plus configurable head/controller-relative stick movement.
- Local implementation/evidence: `MODDING_PLAN.md` sections dated 20 October and section 3; `docs/TESTING.md` protected native lab and acceptance procedures; existing native C++ input/camera/body/combat boundaries named above.
