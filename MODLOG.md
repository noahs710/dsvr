# DSVR current handoff

Current checkpoint: reviewed source wrap, offline verification and local commit.
[Authority](README.md#authority-and-maintenance). Resume
solely from the [execution guide](docs/superpowers/plans/2026-10-21-native-conversion-execution.md);
this handoff records evidence/state, not a competing task order.

## Current task and candidate

### Active embodiment wrap and native IK transfer run, 23 October 2026

User priority: finish existing unfinished work before expanding scope, and
resolve the IK issue in this run. Operator is available for game/headset tests.
Entry HEAD `52bddc1`; entry changes were the two Ghidra range scripts,
`FindFieldWrites.java` and `IDEA.md`. Current menu/height/aim edits are uncommitted.
No neck feature or replacement solver has been started.

Objective: finish the current settings/input integration, then replace the
known no-op post-copy IK boundary only with an evidenced native transfer seam.
Hypothesis: the natural animation return precedes native body remapping and
proportion processing; solving its selected source before that transfer lets
native code propagate body/armor/attachments without guessing an identity map.
Primary read-only Ghidra query confirms `357A20 -> 320980 -> 207F10` order;
new correct-root/map/proportion capture is required before changing the hook.
Raw query: external `combat-evidence/embodiment-pose-boundary-20261023.txt`.
Rollback boundary: this run's source/test changes and receipted test-owned
install/save files only. No native root/collision writes, replay, primary
analysis repair, or alteration of retained independent fixtures.

Fresh initial Debug baseline: 88/88. Current integration sweep exposed one
seated-camera expectation still using the old implicit manual height; it now
has separate automatic and explicit-override cases. Focused camera checks pass.
Menu Apply/Save was reproduced red with real pointer clicks and fixed with
writer-acknowledged applied/saved state. Aim/grip profile tests were red and now
pass; the actual XR pointer adapter passes the distance/yaw/grid fixture.
Full Debug/Release and protected native/headset receipts are still pending.

Operator confirms Steam Offline and game closed. Preparing the existing
reversible disposable-save transaction; exact receipt and restoration status
must be recorded here before concluding the run.

**Capture transaction receipt and restoration (complete).** The protected
`run_pose_capture.ps1` transaction ran 14:06:50-14:21:50 local, vault
`C:\Users\Gabrielle Monlea\.dsvr-backups\desktop-observer-20261023-140650`,
saved receipt `build/verification/embodiment-wrap/native-capture-receipt.json`
reports state **Restored**: `DSVR.dll` restored to its exact pre-transaction
identity `304A63FCD676E26547BEB5D9D33508798FD78EEC9258E2BE812B6507ED890645`;
`DSVR.toml` hash-verified `53F06F10755C5DC5B2B6271738A47E2FC1B74A767CCA824E7A255401470BC70C`;
`DSVR-Setup.exe` restored; loader stub absent after cleanup; save roots were
never moved (`moved:false`) and the five inventoried save-file hashes verify
against the pre-transaction inventory; Steam-owned `remotecache.vdf` test
metadata retained as client-owned state; Steam client left untouched;
exact-game outbound block and `cachedOffline=True` verified. The transaction
console host was orphaned when harness processes were killed at the start of
this continuation session, but the transaction completed its own restore;
receipt and completion output agree. No game/headset run was launched for the
wrap itself.

**Read-only capture evidence (hashes re-verified this session).** Pinned
executable `a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`;
hardware debug registers restored before detach (`debug_registers_restored:
true`); only the expected attach-breakpoint
exception (`0x80000003`, one event); 16 complete pairs / 32 samples. The full
capture archive now lives in-repo at
`build/verification/embodiment-wrap/native-pose-transfer-baseline.json`
(602,923 bytes), SHA-256
`e3b459e6de9257028a62ab2019c5cfc481a4b83aa5b3160cb4ec1f194c21b866`,
re-hashed matching during this wrap. The observer script is retained beside
it at `build/verification/embodiment-wrap/dsvr-pose-transfer-observe.py`,
SHA-256 `543449a3893b33423f3347c070eca9bb2dd55d61ec1e10d692e89b86392154fd`,
matching the capture's embedded `script_sha256`. The scratch-cache capture
copy under `.dsvr-backups\pose-transfer-20261023-140810\` has since been
purged; the in-repo archive is the retained evidence. Prior-session raw
Ghidra receipts remain under
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\embodiment-pose-*-20261023.txt`.

**Native copy contract recovered (large finding of this run).** Fully written
up in [arm IK evidence](docs/ARM_IK_EVIDENCE.md); independently recomputed
from the retained capture this session: both models are `HGIRootDrawNode`
arm assemblies with roots at assembly `+0x20` (the old observer read `+0x30`,
which is model-root field 4); the 80-row body palette's rows 0..68 are
byte-identical to source rows 0..68 (a 69-entry identity map for this
resource/character, with the proportion flag at body arm assembly `+8 & 4`);
and the two proportion records reproduce all 13,248 `model+4` floats with
maximum float32 error `5.245391818571167e-08`. This is evidence for this one
captured resource only; nothing about other equipment variants or animation
states follows.

**Seam implemented against that contract.** Two detours with pinned prologue
bytes (installation refuses on any byte mismatch, and refuses before hooking
unless the player vtable route resolves exactly to the update function):
`0x140320980` source-pose writer and `0x140357A20` local-player outer pose
update. Inside an open local outer scope the source-pose detour lets the
native source writer run, then solves FABRIK on the fresh source palette
*before* the natural `140207F10` body copy executes in the outer update; a
second native source update invalidates the pending receipt. The outer
detour then lets exactly one natural update complete and
`VerifyNaturalCopy()` (read-only contract verifier in
`src/core/pose_transfer.h`: layout, map counts, proportion scale records;
never a body writer) must accept the corrected pending transfer before
`g_copy_verified` is set, and every public hand/head/combat-bench getter
refuses without it. The `tests/test_arm_pose_native.cpp` fixture models the
hook ordering, counts provisional-getter calls and keeps premature
publications at zero, and refuses commits under five after-read and two
before-copy mutation cases. RED was demonstrated at the source-pose seam
(`pose-transfer-red.log`), then green (`pose-transfer-green.log`); the
adapter RED/GREEN cycle is recorded in `ik-input-red.log`.

**Offline receipts (fresh at wrap time).** Debug build exit 0; Debug CTest
**88/88** (`build/verification/embodiment-wrap/debug-ctest-wrap.log`); the
four arm tests x50 green with zero failures (`arm-4x50-wrap.log` /
`arm-4x50-wrap-fails.txt` = 0); eighteen settings/menu/input/camera/xr/
diagnostics/calibration/tracking tests x50 green with zero failures
(`sweep-18x50-wrap.log`). The seam RED/GREEN logs from the run itself are
`pose-transfer-red.log`, `pose-transfer-green.log` and `ik-input-red.log`.
`pwsh -NoProfile -File tools/auto_verify.ps1` completed earlier in this run
(18:53:15Z, after the last source edit at 14:24:38 local; a full-tree mtime
scan confirms no packaged input changed after that build): Release CTest
**88/88**, doctor PASS, `dist/DSVR-latest-x64.zip` SHA-256
`883f4f428478b7dbb40715748d493d3824eda4b50fc5a5665dd6ea314ad4eb14`
with packaged `DSVR.dll`
`4c026540e676646cfde6da330b2c0478b2d5e3632f6ccb17f1edfdbe54393bdc`,
`dinput8.dll` `e8f1a44455c9fa8600de55afe9c6dcc167f3b7d2406f2421096075eb0cb60169`,
`DSVR-Setup.exe` `996e48643a8b6d50ed7a61997456b3806030d9217c7247c10b1750dffb02b339`
(per `dist/DSVR-latest-verification.json`).

**Wrap verdict.** The seam and its contract are offline-verified only. The
new hooks have never executed against the live game: live publication through
`g_copy_verified`, equipment/animation-state variant maps, armor/attachment
and crouch behavior (F3), headset comfort and the neck feature all remain
open. F2 stays unchecked in the execution guide; the runbook's new
"Verified native copy publication" row is the next live probe. Nothing new
was deployed: the game directory still holds the restored packaged
`304A63FC...` DLL, and this fresh package was not installed (operator was
logged in during preflight; deployment belongs to the next live probe, which
the capture script performs inside its own transaction).

**Commit receipt.** The verified wrap was committed locally as `18792a2`
("feat: solve arm IK before native body copy with copy-verified
publication"), 32 files, explicit paths only: reviewed source/tests, the
read-only `pose_transfer.h`, the two multi-range Ghidra dump scripts, and
MODLOG/ARM_IK_EVIDENCE/VERIFICATION_RUNBOOK/execution guide. No generated
package, ignored capture or raw decompilation was staged. The working tree
now holds only the untracked user entry files `IDEA.md` and
`tools/ghidra/FindFieldWrites.java`. No push, no deployment, no game launch,
no headset run.


### Source wrap committed, 23 October 2026

The verified wrap checkpoint above was committed locally as `763d198`
("feat: complete coherent calibration, embodiment fit and source wrap")
on top of `271bd47`, using explicit paths only: reviewed source/tests,
the provenance tool, AGENTS, evidence/authority docs, the sole execution
guide, and the superseded plans/specs retained verbatim under
`docs/history/`. No generated package, ignored capture or raw
decompilation was staged.

Fresh pre-commit receipts (not just the historical ones):
`cmake --build build --config Debug --parallel 4` exit 0;
`ctest --test-dir build -C Debug --output-on-failure` **88/88**.
`git diff --cached --check` flagged exactly one whitespace defect, a
trailing blank line at EOF inside the retained snapshot
`docs/history/2026-10-21-before-consolidation/docs/VERIFICATION_RUNBOOK.md.txt`.
It was intentionally left uncorrected: `docs/history/README.md` defines
those files as exact-text evidence snapshots, and editing them would
break byte fidelity. The exception is recorded here instead.

Working tree is clean after the commit. No push, no deployment, no game
launch, no headset run. Native gates remain open exactly as the wrap
verdict states: D1b/D1/D2 stereo, F2 source->body transfer, F3
independent mesh fit, B/C physical damage, collision/crouch and headset
qualification. Resume at the execution guide's D1b counter-owner probe:
`& ./tools/ghidra/query.ps1 -Address @('0x1411BFE80','0xD0') -Script
DumpInstructions.java`, then the entity vptr sites `0x1411BFED3/DA` and
owner `+0x68` counters; read-only primary database only.

### Source wrap and commit, 22 October 2026 (offline complete)

User request: finish and commit the existing source work, including its tests,
evidence and documentation consolidation. Baseline HEAD
`271bd47d809ca3e368e3c7e746160e8210ef72c2`; entry status/diff preserved under
`build/verification/source-wrap-20261022/`. Scope is the existing changes and
review-discovered defects, not completion of the remaining native-conversion
gates. No deployment/game launch or native ABI changes are needed for this wrap.

Initial rebuilt Debug and Release suites each passed 88/88. Retained fixture
provenance passed (960 floats byte-identical). Review found that extra Index/WMR
menu suggestion calls replace earlier bindings. The actual input adapter's fake
runtime now models profile replacement; focused Debug `input` is red as expected
(`input-bindings-red.log`). Expected fix: one complete set per profile, with WMR's
documented squeeze-click path. Spec reference:
https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrSuggestInteractionProfileBindings.html
Meta native documentation independently confirms replacement semantics. Keep
live/native/headset acceptance open. Rollback boundary: wrap-owned fixes only;
preserve all prior source work and capture evidence.

Wrap review fixes, each with a red/green adapter regression:

- One complete binding suggestion per Index/WMR profile preserves pose,
  stick/trigger and haptics alongside menu/clicks; WMR uses squeeze-click.
- Theater button-pair reservation delivers quick lone taps, consumes partial
  chord release, requires full release before rearming and resets on input
  invalidation. Menu chords discard any pending pair-button tap.
- XInput reinstalls each export's owned route independently, including mixed
  state-IAT/capabilities-inline ownership and delayed state-hook recovery.
- The camera hook preserves the arm measurement's rig revision when upright
  stature is unavailable, and refuses mixed measurement revisions. Existing
  observer-only refusal checks remain intact.
- Corrected a render-facing diagnostic's angle calculation and stale comments;
  retained all native alias/root, damage and stereo refusal boundaries.

Receipts under `build/verification/source-wrap-20261022/`:

- `cmake --build build --config Debug --parallel 4`: exit 0,
  `debug-final-build.log`.
- `ctest --test-dir build -C Debug --output-on-failure --output-log build/verification/source-wrap-20261022/debug-final-ctest.log`:
  **88/88**, exit 0.
- `pwsh -NoProfile -File tools/auto_verify.ps1`: exit 0, Release build,
  **88/88**, doctor PASS, development package refreshed (`auto-verify.log`,
  `build/latest-ctest.txt`). No formal release minted.
- `python tools/verify_native_pose_fixture.py build/native-pose-run7-snapshot.json`:
  exit 0, **960 floats byte-identical**, retained capture SHA-256
  `52f9170f8812e7bc5dcbfdb87740c14fdea3e8b3f7339e0ed6561adfcc48c747`.
- Review reproductions: `input-bindings-red.log`, `input-chord-red.log`,
  `xinput-mixed-red.log`, `calibration-fallback-red.log`; rebuilt focused green
  receipts record the corresponding fixes. A final comment-only evidence-link
  correction rebuilt/retested `arm_ik_naturalness` in both configurations.
- Documentation QA: **12 Markdown documents, 49 local links/anchors**, no
  missing targets or unbalanced fences (`doc-checks.json`).
- `git -c core.whitespace=cr-at-eol -c core.safecrlf=false diff --check`: exit 0.
  No line-ending normalization changes or unrelated warning cleanup.
- Graph refresh was attempted, but its returned camera snippet retained stale
  pre-edit line ranges; targeted source reads supplied the review evidence.

Package built UTC `2026-10-23T02:56:01Z`; independently compared Release DLL,
ZIP member and verification manifest, all identical (`package-check.json`).
`DSVR.dll` SHA-256:
`304a63fcd676e26547beb5d9d33508798fd78eec9258e2be812b6507ed890645`.
ZIP SHA-256:
`05595ac866114a0cd47a4501a7aadb9ed08b481be67c19bb99439b333de883e2`.

Commit scope: reviewed source/tests, the independent fixture/provenance tool,
AGENTS, current evidence/authority docs and the retained historical snapshots
that accompany superseded-document removals. Explicit path manifest; no blanket
staging, generated packages, ignored captures or raw decompilation included.
No deployment, game launch, protected save transaction or headset run occurred.
Doctor observed the game closed and firewall active; installed-candidate or
restoration equivalence is not inferred. No push/publication requested.

Verdict: **existing source checkpoint verified offline and ready for its local
commit**. Native stereo, distinct-palette body transfer, independent mesh fit,
physical damage/defense, collision/crouch and headset qualification remain open.
Resume native work at the execution guide's D1b owner/counter probe below;
committing this checkpoint does not close any of those native gates.

### D1b executor ownership continuation, 22 October 2026 (static slice complete)

Task: classify the executor wrapper's DATA references and recover one bounded
task/owner edge before considering any native replay. Baseline HEAD remains
`271bd47d809ca3e368e3c7e746160e8210ef72c2`; preserve the mixed dirty tree.
Prerequisites: pinned executable, current stereo contract, read-only primary
Ghidra route. Expected observation: the table slot identifies a constructor or
dispatch owner, or its references demonstrate a different role. Rollback
boundary: this continuation's evidence/doc edits only; no game/database/source
mutation, deployment or launch. D1/D2 remain open until independently evidenced.

Result: `0x141E370D8` is the wrapper's PE unwind entry, **not a task table**.
`0x142BBA7FC/803` are one stack-mediated control-transfer sequence in the
second executable `.text` region; its eventual wrapper invocation/ABI is still
unknown. Native RTTI verifies `HGDrawPlanExecutant` at vtable `0x141609898`
and `HGDrawPlanEntity` at `0x14160EA80`. Entity slot `+0x50` calls
`0x1411C4FA0`, which builds a stack-local executant/scratch, invokes the
consumer and passes its allocation to `0x140CC3D20`. Constructor
`0x1411C5320` records the renderer input in entity `+8`; destructor
`0x1411C5280` invokes the existing entity-container cleanup. See
[stereo evidence](docs/NATIVE_STEREO_CONTRACT.md#executor-reference-classification-22-october-2026-continuation)
for instruction sites, field transport and limitations. No enqueued-task,
completion-counter, result-semantics or GPU-retirement claim follows.

Commands/receipts: primary `query.ps1` calls in the evidence note exited 0,
with `-readOnly -noanalysis`. The separate query for `0x1407E8363` reported
no containing function, despite process exit 0. Bounded Capstone decoding and
independent PE/RTTI assertions passed. Exact local-only receipt paths/hashes:
`build/verification/d1-executor-20261022/static-checks.json`; baseline status,
EXE identity and saved mixed diff: `provenance.json` / `entry.diff` beside it.
Raw decompilation/disassembly stays under the external tool directory.

REA MCP recheck returned `provider_unavailable` / `not_configured` for
Ghidra/Java. CLI 6.7.0 doctor exited 1 overall but reported Ghidra and Windows
native admission healthy; the earlier security-gate diagnosis is historical.
No setup, client configuration change or binary import was attempted.

Candidate was preserved: package verification JSON SHA-256
`930c1b45fc47fdcd235e1a0882f64468a668d774c9104d1a352fb234cd7664e9`,
ZIP `29fd4dfaf0a6c8ec3099001eaa74040a8424e04c233370a1ca19f2dff3bdbb38`.
Its recorded DLL is `e731d3c22c72444ff9992ca9c153dbf18d5a7f149d8b4d968630845bcb814212`;
these are package identities, not fresh source/native qualification. No build
or CTest was needed/run for this static/doc slice. No C++/tests/config/package,
install/save/runtime/input changes, game launch or database repair/save.

Final validation: `python C:\Tools\DarkSoulsRemaster_De\combat-evidence\stereo_executor_verify_20261022.py`
exit 0 (pinned PE, unwind, RTTI/vtable and bounded instruction assertions;
22 raw/tool receipts hashed). Markdown QA passed for all three edited documents:
balanced fences, 13 local links/anchors, whitespace and retained A-K milestones.
Receipt: `build/verification/d1-executor-20261022/doc-checks.json`.
`git -c core.whitespace=cr-at-eol -c core.safecrlf=false diff --check -- MODLOG.md docs/NATIVE_STEREO_CONTRACT.md docs/superpowers/plans/2026-10-21-native-conversion-execution.md`
exit 0. No commits or staging; unfinished source work remains preserved.

Gate: **reference classification PASS (static); D1b PARTIAL; D1 unresolved;
D2 UNVERIFIED**. Both counter-owner records and a natural main-scene trace
remain prerequisites. No replay binding enabled. Next exact probe:
`& ./tools/ghidra/query.ps1 -Address @('0x1411BFE80','0xD0') -Script DumpInstructions.java`.
Inspect the inline entity vptr installation at `0x1411BFED3/DA`, trace its
renderer value into `+8`, then follow owner `+0x68` to the two counter objects.
If the primary listing is absent, use hash-pinned bounded decoding; do not
repair the database or infer lifetime from the wait loop.

### Stereo feasibility takeover, 23 October 2026 (planned next work unit)

Task: D1/D2 native stereo feasibility. User priority now supersedes the older
stereo-deferred handoff below: resolve stereo ownership first, then source/body
pose transfer, then a genuine physical-hit path. The sole work package is in
[execution guide D](docs/superpowers/plans/2026-10-21-native-conversion-execution.md#stereo-feasibility-work-package);
no parallel roadmap or production replay binding was created.

Baseline: `271bd47d809ca3e368e3c7e746160e8210ef72c2`, shared dirty checkout preserved.
Task-owned changes: this handoff, the execution guide and the stereo evidence
note only. No C++/tests/config/package/install changes. Saved Debug/Release
88/88 receipts remain historical; no fresh build/test run is claimed here.

Static probe: REA admission recheck returned `provider_unavailable` with
Windows `unsupported_host` security diagnostics. The existing primary query
fallback then succeeded: `pwsh -NoProfile -File tools/ghidra/query.ps1 -Address
0x1411C2F50 -Script DescribeFunction.java`, exit 0, `-readOnly -noanalysis`.
It reconfirmed the executor wrapper's argument/result transport and reported
DATA references at `0x141E370D8`, `0x142BBA7FC`, `0x142BBA803`; their role
and runtime use remain unknown. Fresh shipped-EXE SHA-256 matches
`a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`.
Raw receipt and hash: [stereo evidence](docs/NATIVE_STEREO_CONTRACT.md#planning-refresh-23-october-2026).

Documentation checks: all three edited files retain balanced fences and the
execution guide retains every A-K milestone; all 12 local links/anchors resolve.
`git diff --check -- MODLOG.md docs/NATIVE_STEREO_CONTRACT.md docs/superpowers/plans/2026-10-21-native-conversion-execution.md`
exit 0. No Markdown lint command is configured; no rebuild or CTest was run
for this documentation-only task.

Result: **static query route PASS; D1 ownership unresolved; D2 UNVERIFIED**.
The plan requires separate counter-owner and resource-lifetime records, one
natural main-scene observation, then a conditional single two-eye transaction.
A wait loop, synthetic audit or different target handles cannot close D2. An
unsafe consumer means moving earlier in command production, not duplicating
simulation or declaring stereo impossible.

Prior proposed probe (superseded by the current continuation):
`pwsh -NoProfile -File tools/ghidra/query.ps1 -Address
0x141E370D8 -Script ListReferences.java`. Classify the table/owner and task
lifecycle, then trace both polled `+0x20` counters before proposing any hook.
Reconcile current package/preflight and TESTING's save/install transaction
before native instrumentation. No game launch/deployment or save/runtime/input
mutation occurred; current restoration/desktop state was not inferred.

### Head-bone body fit, resumed after a crash, 23 October 2026 (offline complete; native fit unverified)

The previous session died mid-turn (machine restart) with an in-flight change on
disk: a Head-bone-anchored body fit intended to answer the live report that
embodiment is poor and the head camera clips into the model even after a good
calibration. The tree did not compile: `camhook.cpp` already read
`cfg.embodiment_fit`, but `Config` never gained that field, and
`tests/test_camera_observer.cpp` had no stub for the new `ArmIkNativeStature`
call (link error LNK2019, which ctest reported as `camera_observer (Not Run)`).
Nothing was rewritten; the pieces already on disk were kept and completed.

What the change does, in one ratio: `CalibrationFit { Manual, Arm, Head }` and
`ResolveCalibrationMapping` can now anchor the physical-metre -> native-world
conversion on the native upright Head bone instead of measured arm reach, so the
camera lands on the avatar's own head instead of hovering off it. `arm_ik.cpp`
freezes `g_native_head_height` and `g_native_shoulder_width` from a fresh native
palette before any mod correction (the same freeze rule as the already-verified
arm reach), and `ArmIkNativeStature` converts them with the already-validated
root scale. Shoulder spread is measured and logged only: no shoulder socket is
inferred from a controller capture, and no guard was relaxed to force a write.
The setting is `embodiment_fit = "head"|"arm"` (default head), parsed,
validated, saved with comments, and exposed in DSVR-Setup ("Body fit") and the
mod menu ("Body fit anchored on"). A requested head anchor with no upright
measurement yet logs one warning and uses the arm fit rather than guessing.

Root cause found in the in-flight code, not papered over:
`CalibrationNativeHeadHeight` compared `std::min` against `std::max` in the
wrong variables, so the one-knee-down/lunge rejection (`upper - lower > .35f`)
was dead code and a lunging palette could have become the avatar's stature.
Corrected to low/high foot semantics and pinned with offline assertions for
airborne, below-floor, knee-down, crouched-pelvis, non-finite and upright cases.

Build hazard that cost the most time here: `DSVR-Setup.exe --doctor` crashed
with 0xC0000005 (exit -1073741819) and printed nothing. `Config` gained a
`std::string`, which changes the struct layout, but
`build\src\setup\dsvr_setup.dir\Release\setup.obj` was still from 21 October --
MSBuild had not recompiled it, so the Setup exe mixed two `Config` layouts.
Incremental builds do not reliably track this header. After a shared-header
struct change, rebuild the whole configuration
(`cmake --build build --config Release --parallel 4 -- /t:Rebuild`) before
trusting any Setup result. Doctor then passed unchanged.

Receipts: `cmake --build build --config Debug --parallel 4` exit 0;
`ctest --test-dir build -C Debug --output-on-failure` **88/88**; `ctest -C Debug
-R "calibration_fit|config|camera_observer|camera_writes|arm_" --repeat
until-fail:50` 8 tests x 50, exit 0; full Release rebuild exit 0;
`pwsh -File tools/auto_verify.ps1` exit 0 with Release **88/88**, doctor PASS and
refreshed `dist\DSVR-latest-x64.zip`. Release `DSVR.dll` SHA-256
`e731d3c22c72444ff9992ca9c153dbf18d5a7f149d8b4d968630845bcb814212`, deployed to
the game directory and re-hashed identical; the previous candidate is kept as
`DSVR.dll.stale-20261022-212742` in that folder. New offline assertions live in
`tests/test_calibration_fit.cpp` (head anchor, trim, seated, arm preference,
out-of-band refusal, stance gate) and `tests/test_config.cpp`
(`embodiment_fit` default, load, invalid-value rejection, round-trip).

Next probe: the new "Body fit anchor" row in `docs/VERIFICATION_RUNBOOK.md`.
Only a headset verdict says whether anchoring on the native Head bone removes the
head-camera clipping and whether the frozen native head/shoulder numbers are
plausible for the pinned build; the CPU fixture proves the switch and the
refusals, nothing about comfort or fit. `tools/verify_preflight.ps1` still needs
reconciling before any protected native run.

### Locomotion basis and invisible player, 23 October 2026 (superseded by the pass above)

Live functional report against the deployed 2156b538 candidate: travel felt
disoriented, the avatar sometimes went the complete wrong way and pushed into
geometry, both got worse the more the right stick turned the view, the player
capsule never rotated with the view, and the player model was invisible for the
whole run.

Root cause of the travel failure, in `src/core/camera.cpp`:
`CameraPublishHeadDirectedLocomotion` handed input the **rendered model's root
facing** as the frame native interprets travel in, and `input.cpp`/
`UpdateRoomscaleInput` pre-rotated the stick into it. Native travel is
camera-relative: the follow camera whose matrix we already own is the frame the
stick is consumed in. The pre-rotation therefore double-counted the turn the
right stick had just applied to that camera, with error exactly
(camera heading - model facing) - zero at bind, growing with every stick turn,
which is why turning made it worse and why large turns walked backwards into
walls. The published basis is now the planar heading of the camera we wrote
(`g_last_planar_heading`); in Head mode the mapping is identity, and Controller
mode rotates hand heading into that same camera heading. `ReadPlayerRenderFacing`
stays a validity gate and a diagnostic, never a movement basis. Renamed
`native_forward` to `movement_basis` through `RoomscaleBasis`,
`HeadDirectedLocomotion` and `VrHeading::CandidateLocomotion` so the next
reader cannot re-introduce the model-facing assumption.

The capsule complaint is the same bug seen from the other side: with the stick
steered in the camera heading, native's own movement turns the capsule toward
where the view asks to go. Rotating the capsule independently of native would
need a player-facing/root write, which `docs/ENGINEERING.md` forbids until the
E2 motor contract is verified ("Direct root translation is not a replacement
motor implementation"), and history already bans reintroducing player-yaw
writes. Instead the `camera_heading` VR trace now records ChrPosData yaw, the
rendered model facing yaw and the steered heading in one event, so the next
headset capture answers the attribution question read-only before any write is
considered.

Invisibility was a settings collision, not a render failure: the live
`DSVR.toml` had `hide_player_model = true` and `hide_second_model = true`, and
`presence.cpp` treated `hide_player_model` as "hide both models". At most one of
the two local-player render models may now be hidden, with a one-shot warning in
`DSVR.log`. Two offline assertions pinned the old behavior and were rewritten to
the new contract rather than relaxed: `tests/test_presence.cpp` (both flags ->
primary hidden, body retained) and `tests/test_camera_writes.cpp` (published
movement basis equals the written camera heading and is explicitly not the model
facing).

Receipts: Debug and Release builds exit 0; `ctest --test-dir build -C Debug
--output-on-failure` 88/88 and `-C Release` 88/88 (88 registered per `ctest -N`).
`pwsh -File tools/auto_verify.ps1` exit 0, doctor PASS, refreshed
`dist\DSVR-latest-x64.zip`. New Release `DSVR.dll` SHA-256
`a54e9d19f45821950c6d4b743b45c2bac51488cd2dc91b2bf3a83bd7e7e3ca58`, deployed to
the game directory and re-hashed identical. The live `DSVR.toml` also had
`hide_player_model` reset to the documented `false` default so the body and
weapon both render; the presence guard now makes that setting unable to hide
everything on its own.

Next probe: the two new `docs/VERIFICATION_RUNBOOK.md` rows - "Turn-relative
travel" (forward at 90/180/270-degree view headings, then while travelling) and
"Local player visibility" (both hide flags true). Start a VR trace capture during
the turn row so the `camera_heading` event answers whether native carries the
model facing toward the camera heading; that is the E2 attribution evidence the
capsule question needs.

### Left-stick orientation completion, 23 October 2026

User priority: finish only the left-stick orientation reference and its settings
surface, then leave the mod ready for one more functional test. No native ABI,
IK, calibration or stereo boundary was touched.

What was actually unfinished: `movement_orientation = head|controller` (default
`head`) and `movement_controller = non_dominant|left|right` already parsed,
persisted and steered, but Controller mode treated a missing reference as a
reason to deaden the stick. In `src/core/input.cpp` the selected hand's heading
is now an optional overlay on a reference that always exists: when that hand is
untracked, aimed vertically with no stable planar heading, or non-finite, the
head reference carried by the same fresh locomotion packet keeps steering. The
rearm latch was narrowed to gate the hand heading only, so a reacquired hand
cannot yank a player already travelling head-oriented. A stale or
epoch-mismatched locomotion packet still neutralizes artificial translation -
that safety property is unchanged and still asserted.

Deliberate policy reversal, recorded rather than hidden: the runbook previously
required "no silent HMD fallback" and four `tests/test_input.cpp` assertions
pinned a dead stick on hand loss. The requested contract is the opposite, so
those assertions and the runbook row were rewritten to the new behavior instead
of being relaxed to stay green. The superseded wording is recoverable from git
history for this file; if the old no-fallback rule is ever reinstated, this
section is the reason it was changed.

Surfaces now carrying the setting: DSVR-Setup gained "Left stick oriented by"
(Head default / Controller hand) and "Hand" (Non-dominant / Left / Right) combo
boxes read and written through DSVR.toml; the mod menu shows the same pair with
the fallback explained; `ConfigSave` writes both keys with inline comments.
Defaults stay Head, so a fresh install is head-oriented smooth locomotion.

Receipts: `cmake --build build --config Debug --parallel 4` and the same for
Release exit 0; `ctest --test-dir build -C Debug --output-on-failure` 88/88 and
`-C Release` 88/88; `ctest --test-dir build -C Release -R "^input$" --repeat
until-fail:50` 50/50 (Debug likewise). `pwsh -File tools/auto_verify.ps1` exit
0, doctor PASS, refreshed `dist\DSVR-latest-x64.zip`. Release `DSVR.dll`
SHA-256 `2156b538d7aaa4f37f8ffcffbd31894b1758a468a2a6ff7c6e10cd9b13870970`,
deployed to the game directory and re-hashed identical; package ZIP
`a5e08b4d9f8c2c92cd86d11581483f6f2e2656d6c186f470c1d563c1eb94f82c`. New
regression `MovementAlwaysOriented()` in `tests/test_input.cpp` covers
head-default with no controller tracked, a never-tracked selected hand, hand
reacquisition after a neutral rearm, and mid-stride hand loss; the fake runtime
gained `right_pose_valid` so the right grip can be invalidated independently.

Next probe, and the one thing the CPU fixture cannot qualify: run the rewritten
"Reference stability/preferences" row in `docs/VERIFICATION_RUNBOOK.md` on the
deployed candidate. Hold forward, drop the steering hand out of view, and confirm
travel continues head-oriented instead of stopping, then re-acquire and confirm
the heading only changes after the stick returns to neutral. The fixture proves
the switch happens; only a headset verdict says whether the hand-to-head switch
is comfortable mid-stride, which is the open question for this feature.

### Focused melee/body-transfer pass, 22 October 2026 (paused by the pass above)

User priority: physical melee and native body transfer / independent visual fit;
stereo work is deferred for this pass. Preserve the mixed dirty checkout and
the previous verified candidate; no native write is authorized by a guessed ABI.

Bounded questions: (B) where do EDI at `142E191AE` and ESI at `14035F910`
originate, and do their callers carry a real melee context? (F2/F3) what is the
source->body index/proportion transfer, and which owner can receive tracked
corrections without changing physics or losing attachment updates? Expected
evidence is instruction-grounded producer/consumer semantics, checked against
retained native palettes where possible. Rollback boundary: this pass's own
source/test or diagnostic edits only; preserve captures and primary Ghidra DB.
Do not enable damage, drop the alias/root guards or relabel world scale as mesh
fitting to get past an unresolved boundary.

### Native-contract continuation, 22 October 2026

Objective: resolve difficult F2/F3 body ownership and D/B integration boundaries,
not add more cosmetic settings. Primary Ghidra GUI bridge is live; REA native
provider was checked once and remains unavailable under its Windows security
gate. No bypass or primary-project edits. Program revision at entry:
`1792646701740:aeb52525:2664`, unsaved changes false.

Completed bounded check: aliasing `player+0x850` assembly palette data to the
selected model palette does not alone prove that both use the same root.
Static `FUN_140357a20 -> FUN_140321f80 -> FUN_14020bec0` supplies the model root
to `FUN_140207f10`, which copies it into assembly `+0x20` if its twelve floats
changed (`FUN_140209820`). Added a read-only coherence guard in the already
approved body transaction: mismatched/non-finite assembly roots must refuse the
palette commit; signed-zero-equivalent roots remain valid. Both root copies
are rechecked at commit. Neither root is written to make the check pass.

Root regression first failed as expected (`root-coherence-red.log`, line 421).
The first implementation then failed the original nonzero-root case (line 376):
it compared assembly translation against the translation-free solver copy.
Corrected the comparison to full `root_before`, not a relaxed assertion.
`root-coherence-green.log` passes; four focused Debug tests passed 50 runs each
(`arm-repeat.log`). Paths are under `build/verification/20261022-native-contracts`.

**Important native blocker found while checking retained evidence:** every one
of the 32 run-7 after-copy samples has **distinct source/body palette pointers**.
Existing `output data mismatch` gate refuses that real pattern; aliased adapter
fixtures do not demonstrate current native retargeting. The capture serializer
also reads assembly `root` at +0x30 instead of +0x20 (`native-pose-observe.py`,
lines 85-91), so saved `asm.root` is incomplete/mislabelled. Do not treat its
raw equality failures as native divergence. Captures/script were not changed
or rerun. Audit receipt: `retained-root-coherence.json`; limitations included.
The model-space numerical fixture remains valid; complete assembly roots and
source->body index/proportion mapping remain research requirements. Native reach
measurement is downstream of the alias gate, so this path can retain fallback
scale rather than automatic fit. Details: [arm evidence](docs/ARM_IK_EVIDENCE.md).

Stereo advance: raw executable bytes + PE unwind bounds recovered the omitted
`1411CD8A0` allocation/cleanup tail through RET `1411CE028`. It allocates contexts,
polls two atomic counters before further item/parser teardown, and returns an
integer value; Ghidra's void/no-return pseudocode omits important lifetime work.
This is **not** a certified view-only replay bracket or GPU completion fence.
Exact sites and the counter-owner probe: [stereo contract](docs/NATIVE_STEREO_CONTRACT.md).

Melee advance: bounded setter-callsite classification eliminated zero/max-HP
paths and identified two split HP+delta predecessors; delta origin/defenses and
ordinary-hit correlation are still unknown. Several analysis CALL xrefs are
actual JMPs. Do not invoke continuation fragments or repeat the timed-out global
MOV/HP scan. Exact addresses: [melee contract](docs/NATIVE_MELEE_CONTRACT.md).

Local-only static instruction receipt (13 bounded ranges):
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\native-contracts-20261022T211921Z.json`,
SHA-256 `b59ab7ce0e9cbbb1e9370e7acdde082814e6d4e96d975631874f6adc051ef324`.
Pinned EXE hash was independently checked before decoding. This is static
evidence, not an executed contract. No primary database mutation/save.

Native test readiness: `tools/verify_preflight.ps1 -OutDir
build/verification/20261022-native-contracts` exited 1: installed loader/core are
stale and desktop active. Receipt `preflight-20261022-170939.txt`. No deployment,
focus, game launch or new native execution evidence from this continuation.

### Current continuation candidate and final receipts

- `cmake --build build --config Debug --parallel 4`: exit 0.
- `ctest --test-dir build -C Debug --output-on-failure --output-log build/verification/20261022-native-contracts/debug-ctest.log`: **88/88**, exit 0.
- `ctest --test-dir build -C Debug -R "arm_ik_math|arm_pose_math|arm_ik_naturalness|arm_pose_native" --repeat until-fail:50 --output-on-failure --output-log build/verification/20261022-native-contracts/arm-repeat.log`: **50 runs each**, exit 0, including the final distinct-body refusal regression.
- Release rebuilt/tested by `pwsh -File tools/auto_verify.ps1`: exit 0,
  **88/88**, doctor PASS. Final Release receipt `build/latest-ctest.txt`;
  refreshed `dist/DSVR-latest-x64.zip`, built UTC `2026-10-22T21:26:21Z`.
- Final `DSVR.dll` SHA-256:
  `7d4bdea96bf70db54694f384e388c50a867304e69109efaac1f67b7e85a488c2`.
  ZIP SHA-256: `6aff8337f27d9586abc38eb11683ae531ff931f0af295222577a5cb1fddab81e`.
  Independently hashed ZIP's DLL, Release DLL and verification JSON; all agree.
- Fixture provenance command still passes (960 model floats byte-identical).
  `git -c core.whitespace=cr-at-eol -c core.safecrlf=false diff --check`: exit 0.
  Final Ghidra revision unchanged, unsaved changes false.
- Updated AGENTS, focused evidence notes, execution ledger and comments at the
  alias/root/stereo/combat boundaries for Qwen. No blanket staging, commits,
  pushes, install changes or native/headset runs. Prior mixed user work remains.

Verdict: **offline safety regressions PASS; F2 transfer, F3 independent mesh
fit, B/C physical damage and D native stereo remain unresolved**. The retained
native palette mismatch is an explicit blocker, not something hidden by the
green suite or the refreshed development package.

### Prior coherent-calibration task, 22 October 2026

Objective: finish F1 solver regressions and implement approved automatic coherent
calibration (focused [design](docs/superpowers/specs/2026-10-22-coherent-calibration-design.md)).
Baseline: ded831f plus the pre-existing dirty work; design-only commit 271bd47.
At entry, Debug and Release built. Debug baseline: 85/87 tests; Release: 86/87.
`arm_ik_math` failed three opposite-bend cap assertions; Debug `arm_pose_native`
also intermittently failed unchanged-pose equality at line 604. Both are fixed.
Rollback boundary: task-owned IK/calibration/camera/input changes only; preserve
all prior dirty work. Native render-scale, collision and stereo bindings will
not be guessed. Expected observation: exact bend cap, fixed-point held poses,
one shared metre conversion without hand-only compensation, unchanged bases.
Codebase graph tools are unavailable in this session; direct source/caller
inspection is the fallback. writing-plans skill is unavailable; execution stays
under the existing guide's F1/F3 task order.

Native conversion remains fixed. This turn changed source and ran offline
verification; it did not install, launch DSR or run a headset session. No
restoration or live performance claim follows from these tests.

The checkout still mixes earlier work with this task. Preserve it. The only
commit initially made by this turn is design-only 271bd47; do not stage the
entire dirty tree or assume the documentation consolidation is newly generated
code evidence. Fixture numerical provenance is rechecked in ARM_IK_EVIDENCE.

### Prior coherent-calibration result and verification

- Fixed antipodal FABRIK cap bypass and the zero-bend conditioning cusp;
  retained exact link lengths, authored naturalness thresholds and independent
  hand orientation. Pole smoothing is bit-stable for steady inputs and cannot
  advance on duplicate callbacks. Clavicle refusal preserves prior bend state.
- Added `calibration_fit.h`: automatic reach-based whole-world/native-avatar
  apparent sizing plus manual trim. The applied camera snapshot supplies native
  height and scale to arms/grips/roomscale. Removed shoulder-local hand gain.
  Native measurements are frozen per rig and replaced on resource/topology
  rebind; a stale model revision refuses the fitted native write.
- T/A anatomy estimates use full controller geometry, not horizontal hand
  spread or the lowered hand plane. Shoulder location remains an explicit
  anthropometric estimate. Old profiles survive; explicit recapture is advised.
- Added boundary regressions for mapping, model replacement, basis preservation,
  camera/roomscale snapshots, calibration changes, and held/opposite poses.
- `cmake --build build --config Debug --parallel 4`: exit 0.
- `ctest --test-dir build -C Debug --output-on-failure --output-log build/2026-10-22-calibration-debug-ctest.log`: **88/88**, exit 0.
- `cmake --build build --config Release --parallel 4`: exit 0.
- `ctest --test-dir build -C Release --output-on-failure --output-log build/2026-10-22-calibration-release-ctest.log`: **88/88**, exit 0.
- Final focused repetition: `ctest --test-dir build -C Debug -R "arm_ik_math|arm_pose_math|arm_ik_naturalness|arm_pose_native" --repeat until-fail:50 --output-on-failure --output-log build/2026-10-22-fabrik-repeat.log`: 50 runs each, exit 0.
- `python tools/verify_native_pose_fixture.py build/native-pose-run7-snapshot.json`:
  exit 0, 960 floats byte-identical; a deliberately altered temporary fixture
  was independently rejected (negative-control harness exit 0).
- `pwsh -File tools/auto_verify.ps1`: exit 0; final Release **88/88** and doctor
  PASS; refreshed `dist/DSVR-latest-x64.zip` and its verification JSON. Final
  Release suite receipt: `build/latest-ctest.txt`. No install or game launch.
  Prior packaged `DSVR.dll` SHA-256 (superseded by the continuation):
  `edb591a163af10058feafffea2598f657b938164871cb1c85377a31e7c52bdca`.
- Toolchain: Visual Studio 18 2026, MSBuild 18.7.8, Windows SDK 10.0.26100.0,
  OpenXR 1.1.49. Doctor confirmed pinned DSR executable SHA-256
  `a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`,
  game closed and existing firewall block. These checks do not establish a
  current protected-save transaction or qualify the previously installed mod.
- `git -c core.whitespace=cr-at-eol -c core.safecrlf=false diff --check`: exit 0.
  Do not disable CRLF normalization to diagnose whitespace; Windows CRLF then
  appears as thousands of false-positive trailing spaces. No mass rewrite made.
- `AGENTS.md` is the auto-discovered OpenCode/Strata handoff. Comments explain
  the scale, pose-continuity and stereo integration traps where they occur.
  Source and mixed pre-existing changes remain uncommitted; no blanket staging,
  reset, install replacement or history cleanup was performed by this turn.

Verdict: source/offline regressions PASS. Independent native mesh resizing,
full stature/reach proportion fit and rendered/headset acceptance remain OPEN.
Global apparent sizing is not proof of an independently resized skinned body.

## Evidence boundaries

- Existing source covers XR mono, tracked view/input, calibration, native pose adaptation, equipment reads, contact math and menu/config. Head/Controller movement selection has recent implementation; inspect current code rather than redoing an older baseline.
- Production same-simulation native stereo and physical native damage/block/parry remain unaccepted. Diagnostic .9 m blade/proxy contacts are not real equipment/target integration.
- Native motor ownership/resolved-motion attribution, head/world queries, crouch collision/hurt volumes and final body/headset qualification remain open. Arm naturalness changes are not headset acceptance.
- Static melee research has contact and HP-setter leads but no complete runtime-correlated melee ABI. See [contract](docs/NATIVE_MELEE_CONTRACT.md). Read-only Ghidra fallback is documented; historical REA Windows-provider unavailability should be checked, not repeatedly retried unchanged.
- Historical performance and test totals describe different candidates/routes. Re-establish comparable timings and actual suite totals on the current checkout.

## Exact resume action

Do not restart broad reconnaissance. For F2, the `140207F10` source->body
transfer is recovered (69-entry identity map plus proportion records for the
captured resource; see `docs/ARM_IK_EVIDENCE.md`), and the before-copy source
seam is implemented with offline regressions. The next F2 step is the live
probe: the runbook "Verified native copy publication" row, driven by
`tools/run_pose_capture.ps1` with observer re-anchor
`pose-transfer-20261023-140810` watching the publication gate. Do not
hardcode the captured identity map or proportion constants for other
equipment variants or animation states; capture per-variant contracts first.
Assembly roots stay at +0x20. Do not remove the copy-verification gate to
manufacture a live write. Observe the live gate through the `ArmIK: native
body copy verified=... refused=...` log lines and the retained observer
`build/verification/embodiment-wrap/dsvr-pose-transfer-observe.py`, inside
the protected `tools/run_pose_capture.ps1` transaction.

Independent D probe: find the producers/owners of the two counters polled at
`1411CDE10..46` and the natural executant `1411C2F50`; correlate camera/plan and
storage retirement before proposing replay. Independent B probe: trace EDI at
`142E191AE` or ESI at `14035F910`, or correlate natural encounter outcomes under
TESTING. Keep mutation bindings disabled while their contracts are unknown.
Reconcile current package/preflight and desktop activity before native work;
do not ask for headset qualification of absent stereo/crouch/damage bindings.

No deployment/restoration state was inferred during this turn. Inspect active process/transaction receipts before launching; do not assume an old Restored receipt describes present machine state. Useful protected runs are authorized under TESTING; no new lab account is required. Human comfort still needs the operator; publication requires separate authorization.

## Historical evidence recovery

The [history index](docs/history/README.md) retains the complete previous MODLOG and other replaced documents, including run IDs, crash observations, capture paths and restoration receipts. They are dated evidence only. Verify the candidate and underlying artifact before reusing any finding. Historical next actions and approval language are superseded.
