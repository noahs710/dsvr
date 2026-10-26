# DSVR verification runbook

Current: 21 October 2026. [Authority](../README.md#authority-and-maintenance). This file owns acceptance procedures/thresholds. [TESTING](TESTING.md) owns protected operation; the [execution guide](superpowers/plans/2026-10-21-native-conversion-execution.md) owns sequence and gates. Cases below are requirements, not claims of implemented features.

## Prepare and restore

1. Choose a ready gate and exact candidate. Check its prerequisites; do not ask for headset qualification of an absent binding. Record any independent work still possible when a gate is blocked.
2. Follow TESTING's current protected transaction procedure and inspect `tools/verify_preflight.ps1` before using its flags. Record preflight, save/install inventory and hashes, offline/runtime state, rollback receipt and candidate package/config hashes.
3. Fix scene/character/equipment/graphics/runtime for comparison. Record hardware and actual eye resolution. Existing reference environment: Quest 3/Touch, RX 9060 XT 16 GB, Ryzen 7 7700X, 64 GB; recheck actual hardware. Qualify VDXR and SteamVR separately. Do not change an active global runtime silently.
4. Observe native behavior and capture bounded evidence first. For headset segments, provide exact controls and one feel/comfort question per segment. Stop for discomfort or invalid state.
5. Collect evidence, restore per receipt and verify hashes after every transaction including failure. Record exact restoration status before another candidate.

Evidence folder: `build/verification/<run-id>/` with candidate identity, config, preflight, before/after manifests, raw timings/traces/captures, restoration receipt and results.md. Use PASS/FAIL/UNVERIFIED/BLOCKED as defined in TESTING. Name missing coverage; never average it away.

## Rendering, performance and usability gates

| Case | Procedure and required result |
|---|---|
| True stereo | Inspect final left/right scene outputs with near/far occluders and asymmetric markers, linked from predicted views through consumed matrices/targets to submission. Correct eye order, parallax, scale and occlusion; headset fusion accepted. Different textures/hashes alone fail. |
| Single simulation | Trace native update/animation/AI/collision/events/history/Present and compare game speed, costs/projectiles with baseline. Two eyes cannot duplicate gameplay or retire shared resources early. |
| 90 Hz target | At fixed declared settings, complete eye-pair p95 <=11.11 ms with positive documented headroom; actual runtime period near 11,111,111 ns and measured game rate consistent with 90. Record p50/p95/p99, dropped/reprojected frames and CPU/GPU coverage over at least five steady rate-evidence windows. The `Pacing:` lines in `DSVR.log` carry the measured frame-boundary p50/p95/p99, mean, min/max, long/short counts and `even` over each five-second window, plus a cumulative total; `even` near 1.0 with `long=0` is the consistency half of this case, and a mean that is not the runtime cadence is tagged `NOT-MATCHING-RUNTIME-CADENCE`. A matching mean with low `even` is judder, not a rate fault. Reprojection/reduced-rate output cannot be labeled native 90 Hz. |
| Reduced mode | If separately supported, declare rate/settings before testing; 72 Hz budget is 13.89 ms per pair. Label explicitly and leave the 90 Hz target open. |
| Rate independence | Same captured motion/contact over supported rates: native damage/stamina variance <=5%, no duplicate first-impact reactions. Distinguish deterministic fixtures from noisy human trials. |
| Calibration | Once-per-launch capture, explicit recalibration/cancel, seated/standing, death/load/equipment/focus and recenter. Measurements persist within session; recenter does not change anatomy; failed/cancelled replacement retains usable prior data. |
| Coherent world/body fit | Compare T and relaxed A captures for the same person; record estimated anatomy and the `CalibrationFit` native-units/m log. Exercise short/tall standing profiles, seated posture, world/player trim and a model replacement. A lowered arm must not shrink estimated reach; sitting must not shrink anatomy. Head, reachable grips and physical path use one applied conversion, with no arm-only gain, scaled bone bases or collision edits. Independently qualify native mesh proportions and stereo eye scale when those bindings exist; global apparent sizing alone does not close them. |
| Visibility/attachments | Native head/face/hair/helmet exclusion across supported models/armor/LOD without mesh collapse; rendered arms/equipment remain aligned during walking and grip changes. Unsupported forearm-only mode cannot claim success. |
| Controller usability | Title, Home, calibration, settings save/restart, recenter, HUD/quick slots, inventory/equipment, dialogue/shop and native progression actions usable without keyboard. No input leak or hidden essential prompt. |
| Quest capture/release regression | Close Home or finish calibration while holding one grip or navigation stick. Captured channels stay suppressed until their own release, but a newly pressed A and an unrelated fresh stick work on the next input sample. Repeat in theater/front-end and gameplay. In DSVR controls, A confirms focused items even when the ray/tracking snapshot is unavailable; the trigger selects only a valid ray target and never also activates keyboard/gamepad focus. Verify press, held, release, reacquisition and no-physical-gamepad cases. |
| Quest D-pad modifier | In gameplay, hold left stick-click and tilt up/down/left/right to select native D-pad actions. No locomotion or native L3 during selection; no lunge/jump on modifier release before the stick centres. A centred lone click retains L3 on release. Verify all four directions, diagonal policy, grip tracking loss, menu close, calibration, both-click Home/recenter and left-click + double-A recalibration. Native menus and spatial UI navigate directly without a modifier. Record the active OpenXR profile and any suggested-binding rejection; no fabricated directional Touch paths. |
| Binding edits preserve input | While A or locomotion is held, save a profile change and request reload, including with legacy binding_live_reattach=true. Current actions remain usable without teardown, dropout or a second attachment. Restart explicitly before expecting new profile suggestions. Repeat with an unchanged profile set and the legacy preference false. |
| Interaction | Each supported pickup/door/lever/bonfire/chest/NPC/ladder/elevator class passes eligibility, line-of-sight/reach, one activation, release/cancel and load/death/target-reuse cases. Physical manipulation qualified per supported object class. |
| Ranged/breadth | Native weapon-family support matrix and bow/crossbow/catalyst paths preserve projectile origin/aim, ammo/resources, timing, costs, defenses, swaps and handedness. Representative native encounters per declared family. |
| Campaign | Distinct area/rendering/traversal/boss/special-action matrix plus longer progression session; rolls, grabs, ripostes, ladders, falling, cutscenes and transitions preserve gameplay with comfortable camera ownership. |
| Runtime recovery | Inactive-headset startup, focus/session loss, disconnect/reconnect, resize, image errors/timeouts and shutdown restore usable state without stale images/pointers/input or indefinite waits. |
| Install/package | Exact build/config/hash linkage; clean install/upgrade/uninstall and interrupted rollback; original files restored; immutable version and no game-derived payloads. |

### Frame pacing window operator procedure

Run this without any assistant present; it needs only the operator and the game.

Preconditions, re-checked every time:

1. `pwsh -File tools/verify_preflight.ps1` prints `HEADSET RUN: READY TO START THE RUNBOOK`
   and `BLOCKED=0`. A `STALE` line means the installed payload is not the packaged
   build and any headset number from it is worthless.
2. Installed payload must equal the package: compare the installed
   `DSVR.dll`/`dinput8.dll` full SHA-256 against
   `dist/DSVR-latest-verification.json` (`binaries_sha256`) — never trust a
   hardcoded prefix copied from an old candidate. To fix a stale install
   with the game closed, run `dist\DSVR-latest\DSVR-Setup.exe --install --game "<game folder>" --yes`.
   Do not install from `build\src\setup\Release\DSVR-Setup.exe`: that directory holds
   unrelated 18 October `DSVR.dll`/`dinput8.dll` copies that the build does not refresh,
   so it silently installs a two-week-old core.

Run: launch with Steam offline and the VDXR route active; sit still in one steady
scene for 30 seconds (six five-second windows); walk a normal loop for 30 seconds;
then hold the combat-heavy scene for 30 seconds; exit the game cleanly; copy
`DSVR.log` out of the game folder.

Read the `Pacing:` lines. Each window reports `target` (the runtime cadence),
p50/p95/p99, mean, min/max, `long`/`short`, `rejected` and `even`, followed by a
cumulative total. `RateEvidence:` reports the runtime Hz beside the effective rate.

Pass: `target` equals the runtime Hz with no `NOT-MATCHING-RUNTIME-CADENCE` tag,
`even` at or above 0.9, `long=0`, and `rejected` either zero or explained by a
menu/pause you deliberately caused. A matching mean with low `even` is judder, not
a rate fault: check the `RenderMetrics:` GPU line and the rate re-apply path before
touching the rate policy. A `NOT-MATCHING-RUNTIME-CADENCE` tag beside a correct
`RateMatch:` line means the setter did not take effect on the owner. Growing
`rejected` with steady `frames` means the game stalled past 100 ms and the window
refused to average it. Paste the `Pacing:` lines and the preflight summary into the
run record and MODLOG; `frame_limit_fps=90` is the installed profile and 100 Hz is
not in the supported rate set.

## Detailed combat, roomscale, body and comfort matrix

### Diagnostic capture before native combat qualification

Use `verbose_logging=true` and the existing F9/menu capture: maximum 20 seconds.
The ordinary build logs capability/queue health but deliberately excludes the
natural native contact hook. A separately verified `DSVR_ENABLE_LAB=ON` build is
required for contact observations; preserve the regular build's option/cache.
The capture header records `combat_hook`, `combat_detail_schema` and
`combat_context_schema`: verify
these instead of assuming verbose mode installed the contact observer.

Capture the existing B1 original-controls cases separately (hit, miss, block,
parry, dodge/invulnerability) under TESTING's disposable-save transaction. Record
the intended case, actor/equipment and independent visible result; do not label
a callback or HP delta as an attributable weapon hit. The contact observer reads
at most 256 natural calls per capture, without injecting calls or haptics.

Analyze with `python tools/vr_trace.py <capture.jsonl>`. Its exit 2 is expected
while full VR acceptance is incomplete; inspect individual checks. The declared
`capture_domain` sets the scope: a `combat` capture installs no render or camera
probes, so `submission_coverage` is `NOT_APPLICABLE` there and its absence is
not a defect, while a `full` or legacy capture with no submissions stays invalid.
Either way `gpu_stereo`, `native_scenario` and `comfort` remain `UNVERIFIED`.
Require
`capture_integrity` and `combat_observation_integrity` PASS for usable detailed
correlation, then inspect `coverage_limitations`, `contact_limit_reached`, actor
lifetime validity, `read_reason_counts` and native/read-cost p95/p99. A detail
limit or missing natural-contact producer still leaves B/C open even when the
file itself is valid. `TraceHealth` logs queue peak/drops/contention and bounded
native/read maximum costs every five seconds with new data and at close;
`CombatHealth` reports capability changes, plus five-second verbose heartbeats.
Neither is a headset/game-performance or physical-combat PASS.

With `combat_context_schema=1`, require complete five-record batches and inspect
`native_context` plus per-case `attack_selections`/`event_handles`. Behavior ID,
selected AtkParam ID and animation ID are different domains. Missing read bits
mean unknown fields. An attack type 64 observation is a parry context candidate,
not proof that the parry succeeded; record failed parry, parry-only and
parry-plus-riposte sequences separately. The payload `+C8/+C9` bytes are not a
raw copy of AtkParam `+7E`. Finish the actual trace footer before ending a case
or restoring the session. A reused contact stack address is not attack identity.

Read-diagnostic nibble codes (source before, target before, source after, target
after): 0 valid; 1 invalid address; 2 unreadable/out-of-image vtable; 3 unqualified
type/getter; 4 unreadable handle; 5 unreadable stats; 6 out-of-range stats;
7 unreadable record; 8 actor replaced; 9 actor not admitted before the call.
Unknown outcomes remain null. Observer read/assembly timing excludes the native
call, queue admission and writer I/O. Bench traces are explicitly provisional
pre-copy geometry with assumed blade length/targets, never native hits/defenses.

These are proposed engineering pass thresholds, not measured performance or platform guarantees. Record them before qualification. Change a threshold only with a documented engineering reason; never relax one simply to label a failing run successful.

| Gate | Procedure | Required result |
|---|---|---|
| Real damage | Ten deliberate hits and ten misses against a repeatable vulnerable target with one qualified sword; record swing/contact/context/target generation and native outcome. | Each hit has exactly one expected native result; each miss has none. HP or native defense outcome is attributable to that contact. Native comparison context produces matching rules. |
| Physical rules | Repeat against shield block, parry, dodge invulnerability, solid wall and two-handed stance; inspect native costs/reaction/status. | Correct native mitigation/invulnerability, stamina/poise/reaction, no through-wall hit or duplicate side effects. |
| Resting and false hits | Hold overlap 10 s; walk, strafe and right-stick turn with a still weapon; recenter; lose/recover tracking; swap equipment and reuse actor addresses. | Zero unintended attacks, resting repeat damage or stale-target calls. A fresh valid swing can attack again. |
| Sweep and timing | Replay the same captured slash/thrust, including fast rotational motion, with varied tracking/native rates and duplicate render visits. | Same target/contact episode and attack variant; no tunneling/duplicate outcomes. Deterministic native fixture has equal damage/costs; noisy human repetitions are analyzed separately. |
| HMD-relative stick | All cardinal/diagonal stick directions at HMD yaw 0/90/180/270, both hands pointing elsewhere, head pitched up/down. | Horizontal intent within 2 degrees of expected HMD-relative direction; diagonal speed no greater than full cardinal speed; no controller-derived steering. |
| Controller-relative stick | Repeat cardinal/diagonal tests with the selected controller heading fixed and HMD scanning left/right/up/down; then intentionally yaw the reference controller. Test non-dominant and explicit left/right. | Head scanning changes view without changing the intended travel heading; intent follows selected controller yaw within 2 degrees. The unselected controller does not steer. |
| Reference stability/preferences | Lower/raise the selected hand through vertical, lose/recover controller tracking, change orientation/reference/handedness while holding forward, then release/rearm and restart. Set the reference in DSVR-Setup and in the mod menu, reload (F7) and restart, and confirm a fresh install default. | No accidental forward reversal. The left stick is never dead: an untracked or vertically-aimed selected hand returns steering to the head reference, and a reacquired hand re-engages only after a neutral stick. Artificial translation neutralizes only when the locomotion packet is stale or epoch-mismatched, while valid physical walking continues. Setting changes apply after neutral, persist to DSVR.toml, and legacy hand settings migrate as specified; fresh installs default to Head-oriented smooth locomotion. |
| Turn-relative travel (23 Oct regression) | On open ground push forward, then turn the view 90, 180 and 270 degrees with the right stick and push forward again at each heading; repeat while already travelling, and repeat after a snap turn. | Travel direction matches the view heading within 2 degrees at every heading. No reversal, no residual heading from the previous turn, no pushing into a wall. The capsule turns toward the camera-relative request through native's own movement, with no player-facing write. |
| Local player visibility | Look down at the body with `hide_player_model` and `hide_second_model` both true, then with only `hide_second_model` true, and after a load/rebirth. | At most one of the two local-player render models is ever hidden. With both flags true the body still renders (the 23 Oct run had a completely invisible player for the whole session) and `DSVR.log` logs the conflicting-settings warning once. |
| Body fit anchor (23 Oct addition) | Stand still on flat floor and look down with `embodiment_fit = "head"` (default), and read the `CalibrationFit:` line in `DSVR.log`; switch to `"arm"`, reload with F7 and compare; then crouch, roll, kneel and lunge and re-read the line; finally set the anchor in DSVR-Setup and in the mod menu and confirm both persist to DSVR.toml. | Head mode puts the eye at the native Head-bone height and the body occupies the wearer's space with no head clipping; arm mode matches the hands exactly and may leave the head off that bone. The logged native head/shoulder numbers stay frozen through crouch, roll, kneel and airborne frames. One ratio moves world and player together; no bone base, collider or independent mesh resize changes, and a missing upright measurement falls back to the arm fit with one logged warning instead of a guess. |
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
| Verified native copy publication (23 Oct addition) | With arm IK live (not log-only), idle and walk in the main scene while watching `ArmIK:` lines in `DSVR.log`; drop one controller out of tracking and re-acquire it; reload models with F7; repeat after focus loss and restore. | The `native body copy verified=` counter climbs while gameplay runs and refusals stay rare; every refusal logs one warning and shows no visible hand/head publication for that frame. Solved arms keep tracking the controllers between copies. F7 reload and controller re-acquisition re-arm publication without restarting the game. A verified counter that never moves means the source seam never fired: capture the reported pose-gate name and treat the case as FAIL. |
| Transitions | Menu/calibration cancel/commit, focus loss, runtime pause/recovery, load, death/respawn, equipment and model replacement. | No stale input, contact, actor pointers or motion debt; saved measurements persist; supported gameplay restores tracking/body/combat correctly. |
| Encounter | Complete the fixed sword/shield and two-handed encounters using physical contact, physical and stick walking, physical ducking, both orientation modes and snap/smooth turning; compare comfort settings. | Operator confirms playable coherent embodiment and predictable controls; all earlier native/trace gates pass on the same package. Discomfort or a failed required case triggers correction and rerun. |

At least one tested representative per declared stock melee family is required for broad melee support. The first sword success is an intermediate milestone, not completion of the full weapon matrix.

## Result record

For each case record: gate/case, candidate hashes, environment, predeclared threshold, procedure, actual result, evidence level/path, verdict, missing coverage, defect/next action and restoration status. Include operator verdict for scale, fusion, latency, clipping, combat predictability and fatigue. CPU matrices, video and synthetic tracking cannot replace headset feel.

The final gate requires all required A–K ledger entries to pass on the packaged candidate. Optional features and declared supported-runtime variants must be identified before qualification. An unresolved core requirement stays open; the agent cannot remove it to declare completion.

### Native response capture extension (26 October source checkpoint)

New native_combat_diagnostics payload version 1 retains raw record +54 and
+168/+169 before/after the SAME natural dispatch. Pair it by existing
thread/sequence/record identity and keep each independent read-validity bit.
Require native actor/context/equipment continuity and labelled independent
HP/stamina/reaction observations before attributing an outcome. A changed
response byte, type 2/64, an HP delta or a case label alone is NOT a successful
block/parry/hit. Old captures remain readable with response fields unknown.
Decoder and writer fixtures are source evidence only; all native encounter
and interaction acceptance rows above retain their original thresholds.
