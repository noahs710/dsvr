# Testing and protected operation

[PLAN.md](../PLAN.md) owns task order. This file describes how to collect
evidence safely and defines the acceptance cases. Evidence edits and offline
tests do not authorize a game launch.
The latest user direction requires a fresh go-ahead before a game launch or
automated desktop input.

## Evidence

Use four levels: **source** (code or fixture), **static** (pinned executable or
asset inspection), **native** (identified DSR run), and **headset** (operator
review in the actual headset). `PASS` means the predeclared criterion passed
on the named candidate; `FAIL` means it did not; `UNVERIFIED` means missing
coverage; `BLOCKED` names a missing prerequisite. Never promote one level to
another. Report dropped records, an incomplete footer and unpaired contacts.

## Acceptance cases

These are required observations, not claims of completed work. Name the exact
candidate, runtime, scene, settings, actor/equipment and evidence for each
case. Use [EVIDENCE.md](EVIDENCE.md) for current status. A simulator result is
labeled synthetic, and a headset result requires an operator.

| Area | Required result |
|---|---|
| Physical melee | A tracked weapon sweep makes a native contact with an admitted enemy, causes an independently read HP or defense outcome, and produces one expected cost/reaction. Repeat deliberate hits and misses; no result from resting overlap, walking, recentering, lost tracking or a wall between blade and target. A complete trace and restored transaction receipt are required. |
| Defense | A shield surface intercepts eligible attacks with native block, stamina and guard-break behavior. A moving parry needs eligible contact and correct native outcome; stationary, moving-away and ineligible controls do not parry. |
| Controls and interaction | Title, calibration, settings, inventory, equipment, dialogue, combat and progression work from controllers without hidden essential prompts or leaked held input. Each supported physical interaction class needs eligibility, activation, cancellation and target-reuse cases. |
| Locomotion and body | Tracked head/body fit stays aligned while standing, seated, crouching, turning and walking. Native collision governs walls, stairs, ledges, forced motion and ducking; no stored movement debt, body penetration or view-only invulnerability. |
| Stereo | Both submitted eyes contain independently rendered game scenes from their own predicted pose/projection, with correct eye order, parallax and occlusion. Simulation, AI, collision and events advance once per pair. Different textures alone are insufficient. |
| Performance and comfort | At declared 90 Hz settings, measure actual runtime period, complete eye-pair CPU/GPU p50/p95/p99 and dropped/reprojected frames across steady gameplay. The 11.11 ms pair budget needs measured headroom; reduced-rate modes are labeled separately. An operator checks scale, alignment, motion and discomfort. |
| Lifecycle and campaign | Focus loss, headset disconnect, loading, death, equipment changes, cutscenes, traversal and representative encounters recover without stale input, pointers or XR images. A repeatable short session plus longer progression evidence is needed before a playable campaign claim. |
| Install and release | The installed files match one versioned candidate; clean install, upgrade, uninstall and interrupted rollback restore originals. The archive contains no game-derived payload. The launcher and player guide are tested on the same candidate. |

For every row record `PASS`, `FAIL`, `UNVERIFIED` or `BLOCKED` with the reason.
Predeclare thresholds before a run; do not relax them afterward. A missing
footer, dropped contact batch or unreadable target cannot be counted as a
clean miss or native damage proof. `tools/hmd/autopilot.py analyze` can report
contact-level `PASS` on a footerless historical trace, so check completeness
and the protected receipt separately before accepting a physical-hit result.

## Offline gate (one command, about 7 s)

```powershell
pwsh -NoProfile -File tools/auto_verify.ps1 -SkipPackage -NoLatest -SkipDoctor
```

That configures only when the cache disagrees, builds Release and runs the full
CTest on the current tree. It is the gate; do not hand-run the same
steps. Add a Debug build only for a Debug-only boundary. Build
`build/combat-lab` only for a staged synthetic run — an observer regression,
not the proof path.

The verdict step is one command with no path: `python tools/hmd/autopilot.py
analyze` reads the newest `DSVR-trace-*.jsonl` under the game folder. Pass an
explicit path to re-check an older capture.

`tools/auto_verify.ps1` builds and checks the development tree; it does not
install or launch the game. The protected capture runner stages the separate
`build/combat-lab` binary, so a normal `build/` result does not qualify a
combat-lab candidate. Seal the actual binaries and source identity used by
the runner before its read-only `-Mode Check`.

## Live transaction, only after user go-ahead

From boot and before any automated input, make the game and simulator windows visible and
confirm the user is away or explicitly watching. The r12 autopilot run showed
that a hidden game can consume the user's input. Keep Steam Offline plus the
game's own Offline Mode (no firewall rules), and use the disposable-save
route. Do not take over an existing game or runtime process.

Use the current `tools/run_combat_capture.ps1` Check/Launch/Finish/Restore
interface and its receipt. Confirm the exact executable, source, binary,
config, runtime and save roots before launch. The deadline includes loading.
Collect raw logs, trace and screenshots before cleanup. Stop only owned PIDs,
restore test-owned files and saves, then verify original hashes. A failed or
aborted run still needs an explicit restoration result. Never overwrite
unrelated changes to force a clean receipt.

The simulator uses a per-process `XR_RUNTIME_JSON` and `SIMXR_DATA_DIR`; the
local setup pins an upstream commit in `tools/hmd/setup_openxr_simulator.ps1`.
Its dated UI suite had an AMD teardown crash, so interaction assertions from
that run do not equal a passing suite. Simulator captures are synthetic and
cannot qualify physical contact or headset comfort.

Record candidate hashes, commands and exit codes, case timing, actor identity,
raw evidence paths, verdict limits and restoration receipt in `MODLOG.md`.

## One headset run (maximum coverage, one session)

One operator, one disposable save, about 35 minutes. It covers motion
controls, kinetic combat and physical world interaction in a realistic order:
Firelink Shrine for setup and movement, then the Undead Burg hollow soldiers
for combat. The combat proving ground (`combat_test_bench` in the Combat
panel) classifies swings against a fixed sparring target first, cheap and with
no native writes; only then do tracked swings run against real enemies with
the trace on. Kinetic feel comes from measured tip speed, the support-hand
separation flag, and the weapon mass/torque tuning rows: daggers answer near
1:1, great weapons lag and need commitment, and the bench damage scale must
rise with speed. Every physical class gets four cases: eligible hit, activated
effect, cancelled attempt, and target reuse. Stop after three identical
failures and revise from the trace instead of repeating.

Predeclare the thresholds before starting: light slash at or above
`swing_attack_speed_min`, strong slash at or above `swing_attack_heavy_min`,
parry flick at or above `guard_flick_speed_min`, stillness inside
`guard_settle_ms`, tip speed below `contact_speed_min` scores nothing. Do not
relax them afterward.

| # | Minutes | Do | Expect | Evidence |
|---|---|---|---|---|
| 0 | 5 | Desktop only: run the offline gate, then Doctor. Confirm FPS Unlock shows installed and START VR is enabled; if it shows required-missing, install it first. Back up saves. Put Steam in Offline Mode (logged off, `WantsOfflineMode 1`). | Gate green, Doctor has no `[FAIL]`, saves backed up, Steam logged off. | Gate output, Doctor output, backup path, Steam log line. |
| 1 | 3 | Launch through START VR. On the title screen confirm Dark Souls Offline Mode is on (it stays offline until turned back on in the game). Read the theater panel, open the DSVR menu with Home and with left Menu + right grip, walk every menu page, then close it. | Menus readable, both open paths work, no leaked held input on close (stick and buttons neutral). | Screenshot each path, `DSVR.log` menu lines, note any stuck input. |
| 2 | 5 | Calibrate: run startup calibration, note standing height, then seated height. Recenter with a quick both-stick tap and with F6. Look down at the body. | Head on the native head bone, arms reachable, body visible; recenter keeps reach and wrist fit. | Calibration values, recenter note, seated vs standing heights. |
| 3 | 5 | Firelink walk: left stick forward/back/strafe, right stick snap then smooth, toggle two-hand stance off. Walk into a wall and up stairs, ride any forced motion, turn 360 degrees. With arm-swing sprint on, swing arms loosely to run; then flap one hand only. | Head-steered default (hand-steered with head fallback when set), no movement debt, native collision stops walls, one-hand flapping is not a run. | Note per case, trace movement lines, any penetration or drift. |
| 4 | 8 | Combat proving ground on: light slash, strong slash, thrust at the sparring target; check tip speed, damage scale, hand separation, and the HIT/BLOCK/PARRY readout. Then deliberately miss wide, rest the blade on the target, and swing with a wall between. Then trace-on real swings at hollow soldiers: deliberate hits and misses. | Bench: scale rises with tip speed, heavy beats light, misses and resting overlap score nothing, wall blocks. Real: trace shows paired player-to-enemy contact, independent HP outcome, expected cost, complete footer, restored receipt. | Bench readings per swing, `autopilot.py analyze` verdict, footer/actor/HP lines, receipt. |
| 5 | 7 | Shield and hands: raise the off-hand still to block, hold through a hit, lower to cancel. Flick to parry an eligible attack, then try stationary, moving-away and ineligible timings. Grip the weapon with the support hand to two-hand, release to let go. Open the per-hand weapon wheel (grip held still), release over a slot to switch, release over empty to cancel, switch back. Grab estus from the belt pouch and press X, then cancel a grab. | Native block with stamina and guard-break behavior; parry only on eligible contact; two-hand state follows the tracked grip; wheel switches through the game's own icons and rules; belt gives the real item. | Block/parry/stance lines in log, wheel screenshots, belt note, cancel and reuse cases each. |
| 6 | 5 | Comfort and lifecycle: try vignette on/off, snap vs smooth turning, F8 theater during play and menus, menu distance. Die once or reload, change equipment, lose and regain tracking, alt-tab focus loss. | No stale input, pointers or XR images after any recovery; settings apply in-session; F7 reloads; scale and alignment feel right. | Comfort note (scale, motion, discomfort), recovery note per event. |
| 7 | 3 | Close the game, stop only owned PIDs, restore the disposable saves, verify original hashes, run `python tools/hmd/autopilot.py analyze` on the newest capture. | `Restored` receipt, hashes match, verdict recorded with candidate, runtime, scene, equipment and settings. | Receipt, hash check, verdict line, `MODLOG.md` entry. |

Simulator runs use the same table but stay synthetic: they prove the
observer and the loop, never physical contact or comfort. The keyboard
synthetic loop is an observer regression only; it bypasses the tracked-swing
path.

## Packaging

Development packaging does not establish a release. The formal publication
tool currently requires removed `.empryo/cockpit/lanes/A-K.md` files and is
unusable on this tree. Do not call its `-Package` path or publish until the
release gate is rebuilt and the user requests publication. Ship no game files,
extracted assets, saves or credentials.
