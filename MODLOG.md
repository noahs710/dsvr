# DSVR current handoff

### 26 Oct H1 evidence: 3 native activations decoded with row owners

Fog/door activations from the capture decoded (index, row handle
111/110/109, state 0x2100->3, result 1, validDist 70/60/60; flags 15).
Selected-row + PARAM range + acceptance observed; dummy/LOS/prompt
still open. Full table in docs/NATIVE_INTERACTION_CONTRACT.md.
Inner loop green. B/C/H open.

### 26 Oct submission-entry probe: bounded negative, static chain complete

140371200/360/BD0 are generic lookups (146-173 callers each) — cannot
identify 140490F40's callers; submission entry uses computed dispatch.
Static chain declared complete except live-elicitation items (submitter
identity, jumptable set, +150/+148, HP attribution). Next actionable
work is a targeted kind-2 capture (needs operator) or unrelated gates.
Full note in docs/NATIVE_MELEE_CONTRACT.md. No invocation; DB unsaved.

### 26 Oct kind assignment RECOVERED: PARAM vocabulary, gate identified

140529670 writes +0x54 = uVar4 in {0,1,2} from PARAM-row byte +0x6a
(1->1, 2->2, else 0), gated on weight float > 0 AND (paired event IDs
valid OR record +200&0x10). Table resolved by 140528910 binary-search
PARAM lookup. Session kind-0 explained: all captured attacks had +0x6a
== 0 or failed weight gate; kind-2 needs a heavier/enemy PARAM attack.
Full static chain complete end to end; only kind-2 elicitation + HP
attribution remain. Full note in docs/NATIVE_MELEE_CONTRACT.md.
No invocation; DB unsaved. B/C/H open pending targeted capture.

### 26 Oct ObjDam correction: 141C7B1B0 gates the condition, not the dispatch

1404D0BB0 assigns NS_FRPG::FrpgEventConditionObjDam::vftable — an
event-condition constructor, not a dispatcher. 1404D0F20 is false when
DAT_141c7b1b0 == 0; owner 1404D0D40 runs the condition checks and fires
1404A7740(0x1b). Same global also gates the +0x460/+0x490 tail — one
global read in two places, not one mechanism. Kind-assignment hop
(1405290C0 tail / 140529670) still the next probe. Full correction in
docs/NATIVE_MELEE_CONTRACT.md. No invocation; DB unsaved. B/C/H open.

### 26 Oct kind-writer probe: creation chain placed, kind assignment one hop out

Creation chain recovered (3 bulk calls, read-only): 140490F40 resolves
two actor refs then 1403C9B50 -> 1403C93B0 builds payload locals ->
140528CE0 AtkParam lookup -> 1405290C0 payload builder -> 140529670
writes the event. 1403C6030 builds geometry only (never writes +0x54);
140327AD0 is an eligibility gate. Exact +0x54 store sits in unrecovered
1405290C0 past head / 140529670 / 142AC2A92 shim (one hop). 140490F40
has zero indexed callers — attack-submission entry unknown; kind-2
must be elicited via the submission entry, not play pattern. Full note
in docs/NATIVE_MELEE_CONTRACT.md. No invocation; DB unsaved. B/C/H open.

### 26 Oct native capture: first labelled session, kind-0 only, B/C/H stay open

Operator session under receipted transaction (run.json rev 19, Restored,
hash-verified): game PID 40312, 17:02:46–~17:09 (~380 s span), 30-trace
slot with 1 retained trace (footer missing on exit-to-title close —
flagged incomplete, not repaired).

Evidence `build/verification/native-melee-20261026-standby/`:
53,058 steps, 6,320 contact/identity/motion/diagnostics/context
quintuplets, 3 interaction activations, 8 self-excluded.

- All 6,320 diagnostics read mask 15 (all four groups succeeded) with
  kind +54 = 0 and response bytes 0x00/0x00 before AND after every
  dispatch. No kind-2 (or kind≠0) event observed this session — the
  `+0x490` 0x1D-guarded path did not fire on any captured record.
- 7 HP deltas, all side 1: T+108.9/109.8s 69->21->0 (first Hollow,
  2 strikes); T+246.9/249.9s 69->12->0 (second Hollow kill, incl.
  riposte; final on obj 15f020); T+211.5s side1 659->481 flags 10
  (your damage from the environmental trap — before-only valid, no
  after read); T+239.2/242.0s 481->441->360 (your blocked strike +
  parry/attack sequence). Flags 15 = before+after valid both sides;
  flags 10 = after read refused.
- All 3 interaction activations on obj d654240: T+42.0s (fog gate),
  T+347.0s, T+382.2s. ObjAct rows unmapped to prompt/selected-target
  owner — observed activations, not a prompt chain.
- HP deltas coincide with your labels but attribution is NOT claimed:
  deltas alone don't identify the attack context or the `+0x480`
  dispatch owner. B/C/H remain open.

Runner fix committed in this slice: `run_combat_capture.ps1:411`
`$r.unclosed_traces=` threw on PSCustomObject restore shape (before
RestoreRoots), fixed via existing SetReceiptValue helper; full restore
then verified. SelfTest PASS after fix.

### 26 Oct Option A re-qualification: fresh LAB build, CHECK PASS

Fresh LAB observer qualified from current source (HEAD 4ff5340):
configure exit 0, Release build exit 0, ctest 98/98 (LAB excludes ReShade
addon tests; registered count confirmed via ctest -N). New receipt
build/verification/native-melee-20261026-standby/candidate.json seals the
same 226-file set + 4 binaries (LAB DLL
357d057489084e18daf59ab4aaeaab5f2459cdcc5e61bf76c53a45948bfe0e41f).
Check PASS: pinned candidate, Steam offline/logged-off observed,
exact-game block, disposable seeds, both save roots inventoried. No Run.

AppData root note: live %APPDATA%/DarkSoulsRemastered was absent at Check
although the 23:50 backup held full content; no game run in between.
Restored byte-identical from saves-20261025-235056 via robocopy;
Documents root untouched.

Still required before Run: install current package (preflight BLOCKED on
stale installed DLLs), Steam state re-verified at Run time, operator owns
gameplay, 30-min deadline. B/C/H unchanged.

### Project-wide efficiency pass, 26 October 2026 (all verified, offline only)

1. MODLOG split (context tax): 3,256 -> 1,225 live lines. The closed
   `## Current task and candidate` block (2,033 lines) moved to
   `docs/history/2026-10-26-modlog-task-block.md` via `tools/split_modlog.py`
   (idempotent, refuses re-run). AGENTS.md step 1 points at the archive.
   Only file-level links point at MODLOG (README/MODDING_PLAN/AGENTS/package),
   so no anchor repair needed.
2. Disk prune (~11.6 GB): deleted unreferenced 10 GB
   `physical-damage-pass40-pointer-ranges.tmp`, ~1.3 GB stray root
   `DSVR-trace-*.jsonl`, and 140 MB superseded `DSVR-native.log` files.
   All remaining trace refs are `traces_before` inventory snapshots (named
   pre-existing files explicitly NOT copied as evidence), verified by script.
   New `tools/prune_build.py --dry-run/--apply` keeps this state: prunes
   unreferenced root strays, never evidence.
3. Shared test libs (duplicate compilation): new `dsvr_test_support` +
   `dsvr_test_support_lab` in tests/CMakeLists.txt. vr_trace/lab_runner/
   lab_scenario/combat_native compiled 2x total (once per LAB variant), down
   from 9x spread across per-target lists. First cut wrongly used one
   no-LAB lib (combat_trace/lab_opt_in/vr_trace failed); root cause was
   LAB-gated bodies compiling out. Fixed with the LAB variant. Both configs
   100/100.
4. Trace hot path (`RecordBatch`, vr_trace.cpp): admission now atomics +
   pointer compares (literals pooled by MSVC, strcmp fallback kept), then
   try_lock BEFORE QPC/thread-id syscalls and the recorder scope — contended
   drops skip all three. Refusal semantics unchanged (domain filter, capture
   id, state checks, recorder-guarded close all preserved).
5. `auto_verify.ps1 -SkipDoctor -SkipPackage` for code-only turns (build +
   full CTest, ~5 s total). Default path unchanged (doctor + package refresh).
   Verified: skip path 100/100, full path doctor PASS + ZIP refreshed.

### 26 Oct: diverted path converges on +0x480 — guard selects how, not whether

Bulk probes: 142ffae49 = MOV RAX,[RCX]; MOVZX R8D,R9B; PUSH R10/POP RDX;
PUSH [RAX+0x480]/RET — the non-0x1D side re-enters target +0x480 via
indirect tail-jump, so BOTH guard outcomes invoke +0x480. Correction to
prior "separate continuation" label recorded in docs/NATIVE_MELEE_CONTRACT.md.
Source tail: 142d805f9 NULL-checks RDX (JZ->140488694->jumptable 142d5f79f,
unrecovered), else 1404f5ab2 selects jumptable vs 140c5fb52->1404885d3 on R8.
140420a20 is a second caller of thunk_142134a3b (identity open). B/C/H
unchanged: static shape, no submission binding. No invocation; DB unsaved.

### 26 Oct follow-up: +0x490 guard exact, tail flag write pinned

14284f926 = CMP [R8+0x160],0x1d / CMOVNZ: 0x1D continues to +0x480
forward (1403589A1), else diverts to split entry 1403589C0
(JMP 142FFAE49 — deferred). Tail 142ed722a sets bit12 |0x1000 on module
field +0x1DC, branches on [RBX+0xB4] to 140316af2 vs thunk_1402110f8.
142134a3b = reg-save prologue into 142d805f9 (R9->RBP,RDX->RDI,RCX->R14,
NULL-check to 140488694 else 1404F5AB2; roles open). Full note in
docs/NATIVE_MELEE_CONTRACT.md. B/C/H unchanged. No invocation; DB unsaved.

### 26 Oct bulk probe: +0x490 slot target named, 14031D958 tail placed

Single bulk get_functions call (3 targets), read-only, no native calls.
Contract result: PlayerIns +0x490 slot 1403589B0 = 5-byte thunk to
FUN_14284f926 (body still unrecovered — one hop remains);
14031D958 = interior of FUN_14031d953 (calls 142134a3b then 142ed722a);
FUN_14003d493 (sole caller, itself called only by 14295b2ac) places the
tail on the source-side completion path 14031D939->14295b2ac->14003d493
->14031d953. DAT_141C7B1B0 branch, +150/+148 predicates, HP/stamina
attribution still open. B/C/H gates unchanged. docs/NATIVE_MELEE_CONTRACT.md
holds the full note. No game launch; DB unsaved.

### Quick loop: sub-second inner ring + configure skip, 26 October 2026

Inner loop is now `ctest -C Release --parallel -LE slow`: 93/93 in 0.58 s.
The 7 excluded tests are labeled `slow` in tests/CMakeLists.txt because they
are timing-honest, not trimmable fat: 5x diagnostics waits on the real 350 ms
settings debounce + 40 ms worker pump, combat SelfTest's real 1 s case
windows + pwsh/python startup, package's real zip/hash I/O. TESTING.md records
the gate discipline: `-LE slow` is the inner loop only, the unfiltered
100/100 (~3 s) is still the gate. auto_verify.ps1 skips its 3.6 s
re-configure when the cache already matches generator/platform/key options.
Verified this pass: re-configure exit 0, Release build exit 0, quick 93/93
(0.58 s), full 100/100 (~3.2 s), auto_verify + doctor + package refresh all
green. No game launch/install/headset; offline only.

### Test-loop efficiency pass, 26 October 2026

Full suite now ~3 s per config (was ~27 s): `/MP` added to root MSVC flags
(TU-level compile parallelism; no PCH/Gm/import conflicts), `ctest --parallel`
(bare flag = all 16 cores; every temp-output test verified PID-unique —
`test_config.cpp` fixed from shared `DSVR-test-config.toml` to PID-suffixed),
package.py AV-lock retry sleep 2 s -> 0.25 s (saves ~10 s in the `package`
lock-contention case), `auto_verify.ps1` uses bare `--parallel` for both
build and ctest. Verified: Release + Debug builds exit 0, both suites 100/100
in ~3.0 s. Commands updated in AGENTS.md / TESTING.md / execution-guide
section 6 / auto_verify.ps1. One transient note: the first Release rebuild
after the test_config edit exited 1 on a stale locked artifact; clean rebuild
exit 0, no code issue. No game launch/install/headset; offline only.

Current checkpoint (26 Oct): twenty-one offline slices landed and verified —
AtkParam 0x73 named (kMapHitType/NativeMapHitType, cache transcription only),
launcher theme test registered (setup_theme), untargeted reach refused by
default (interaction eligibility gate + per-class range table with Lever at
0.0/unverified), calibration floor note profile-aware (first launch says
setup height until capture succeeds; hold-reset rule named) with shipped-WARP
render coverage in both modes, restart-pending footer indicator
(applied-vs-attached binding comparison via frozen attach-time signature;
WARP asserts present after Apply, absent for live-only changes), launcher
install-gate hardening (Install disabled on failed Verify in UpdateBrief +
CTL_INSTALL refuse; DoInstall:522 untouched; fail/pass Verify test), and
read-only B/D static writeups (14032F548 verdict + first-half probe;
D-probe second window with RDI/R14 producers and renderer+10 store pinned,
plus re-read addendum with ctor identity and outer caller seed, plus third
window with Begin-payload borrow-appearing verdict and derived slot set,
plus fourth window with queue/teardown shape and bracket scout), E locomotion
owner table (docs-only, six UNKNOWN owners), E2 XInput poller probe (6 call sites, 2 per-frame candidates, mapper still open), F2 per-variant coverage matrix
(docs-only, one CAPTURED row), and H1 lever-range cache search (no citable
value: validDist 150 default conflicts cm-vs-metres, lever rows name-only;
0.0 retained with search paths documented), B type-64 static probe
(negative-only: 64 excluded from durability chain, zero ==0x40 consumers),
and launcher chain-conflict messages carrying the full colliding path
(message text only; test asserts stub mirror — production text reviewed in
diff, suites green both configs), E2 follow-up (consumers unpinned,
slots decoded as self-RVA pairs not vftables, candidate #3 unproven; sibling
walk: trigger/event siblings only, no float reader, VA decode corrected; byte-pattern retry: axis getters pinned (idx5 reads by index, idx7 mirror setters, idx6 button bits), scaling consumer behind zero-xref computed dispatch), and
package mid-refresh atomicity (comment corrected + mid-loop failure test,
old zip/verification pinned), and B swing-producer xref probe (no swing
caller in either xref set; builder has zero code xrefs; ObjDam wrapper
excluded), and B record-stride decode (1417991F0/224 are PE unwind metadata,
not dispatch records; exactly 3 DATA refs; no code owner; correction supersedes
the earlier table gloss).
Full Debug 101/101, Release 101/101, runner SelfTest both engines PASS, doctor PASS, dev
package refreshed (DSVR.dll 3a136a39…, built 2026-10-26T01:25:42Z). Stale
hardcoded hash prefixes removed from the runbook; verify_preflight compares
live full hashes against the verification JSON. B-probe 14032F548 verdict:
instruction candidate only (byte-pinned CALL [RAX+0x640] in .text1 split gap,
zero xrefs, receiver unrecoverable; not a damage classification — see
contract). +0x640 slot comparison closed 26 Oct: EnemyIns slot holds the
negative-HP path, PlayerIns slot a self-stub, neither links to 548.
C/D/E/F native gates unchanged and open; no game launch, install,
or headset run. Inherited controller/weight work preserved.
[Authority](README.md#authority-and-maintenance). Resume
solely from the [execution guide](docs/superpowers/plans/2026-10-21-native-conversion-execution.md);
this handoff records evidence/state, not a competing task order.

### B slot-inhabitant comparison closed (second half, first part), 26 Oct 2026

Read-only GhidraMCP: EnemyIns vftable +0x640 slot holds 0x14034BF70 (negative-HP min path), PlayerIns same slot holds the 140339600 self-stub; each DATA-xrefed from its own table only, zero xrefs to 14032F548. Closes "is +0x640 a damage slot" (class-divergent: negative-HP path vs no-op) without typing 548's receiver. Remaining open half is live hit/miss correlation under TESTING; never invoke 548. Contract holds bytes/arithmetic. No build/test/launch/commit.

### Read-only 548-verdict verification pass, 26 Oct 2026

Read-only GhidraMCP: EnemyIns vftable +0x640 slot holds 0x14034BF70 (negative-HP min path), PlayerIns same slot holds the 140339600 self-stub; each DATA-xrefed from its own table only, zero xrefs to 14032F548. Closes "is +0x640 a damage slot" (class-divergent: negative-HP path vs no-op) without typing 548's receiver. Remaining open half is live hit/miss correlation under TESTING; never invoke 548. Contract holds bytes/arithmetic. No build/test/launch/commit.

Re-checked the dirty-tree 14032F548 verdict subsection against the live GUI (PID 17320, `DSRFull`, `/DarkSoulsRemastered.exe`): re-ran the tasked first half, `get_assembly_context` 12 each side at 142756FC5 / 1421B6015 / 14032F471 — all three match the recorded outcomes (CMOVNZ return-selector toward 14032F584/594; CMOVZ immediate 14032F54E; JMP-island before 14032F480). No contract edit needed: verdict, bytes/xrefs/split-gap/.text1/forged-JMP/forward-JMP+TEST-ESI/+0x638/+0x640-inhabitant evidence, limitations and exact next probe are all present, plus the +0x73 linkage with the 1403C72D0 read-verification caveat. Cache citations spot-checked (raw wiki line 55 mapHitType 0x73, ATK_PARAM_MAP_HIT 0..3; Paramdex DS1R Defs AtkParam.xml u8 mapHitType). Read-only; no repair/save/launch/commit.

### Launcher install-gate hardening (Option A), 26 Oct 2026

RED: rebuilt test_setup_theme, `ctest -C Debug -R setup_theme` exit 1 (FAIL line 117, Install button enabled after failed Verify). GREEN: `g_install` HWND stored at create plus reset in RunGui; UpdateBrief gates Install on path resolved plus verified exe match plus verified-ok; CTL_INSTALL refuses without verified build (DoInstall:522 untouched defense-in-depth). Test stubs VerifyExe fail-once plus DoInstall counter; fail-Verify disables Install and blocks DoInstall, pass-Verify re-enables and installs. Full `cmake --build`/ctest Debug plus Release 101/101. Files: src/setup/gui.cpp, tests/test_setup_theme.cpp. No game launch/install/headset; offline only. No commit.

<!-- task-block-archived: docs/history/2026-10-26-modlog-task-block.md -->

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

### Animation ownership and the +0.2 m calibration height trim, 25 October 2026

Operator request, two parts, both offline-verified only.

**Height trim.** Every standing calibration read short in-headset. One named
trim, `kCalibrationHeightOffsetM = .2f` in `src/core/calibration_fit.h`, raises
the applied view height and the head-fit denominator together, so the camera
stays on the native Head bone while the whole avatar and world grow together
relative to the wearer's body. It is applied inside `ResolveCalibrationMapping`
only: the saved `UserCalibration.eye_height_m` still reports what was captured,
older profiles stay usable, and head, hands and roomscale keep consuming the one
applied snapshot. `tests/test_calibration_fit.cpp` and
`tests/test_camera_observer.cpp` expectations moved with it; the head-fit
assertion `eye_height == native_head_height` still holds, which is the proof the
trim does not float the camera above the head shell. Side effect to watch: the
avatar's limbs lengthen relative to the wearer's body, so the IK naturalness
budget absorbs the difference - reach-gate refusals are the symptom.

**Animation ownership.** New startup-tunable `arm_ik_block_animation` (default
true, `[combat]` in DSVR.toml, plus a Settings row). In `RetargetSourcePose` the
IK-owned rows are reset each frame to `g_source_reference`, the first palette of
a rig revision whose owned rows are all well-formed, read before any correction.
The native writer still runs, so animation state, event counters, legs,
equipment and every non-owned row stay native and the natural-copy verification
is unchanged. `animated` keeps the true palette image for the after-read
re-verification, the rollback and `g_pending_copy.source`; the commit now writes
an owned row whenever it differs from what is in memory, not from what the
animation asked for.

Two guards found while wiring it, both worth keeping:

- Substituting before the guards made a zeroed native Head row invisible: the
  torso solve accepted the frozen row and published over a palette the fixture
  expected to be refused (`arm_pose_native` FAIL 783). Only a well-formed
  `animated` row may be replaced.
- Freezing the source inside the grounded-upright stature gate meant the block
  never engaged at all: the memory fixture's palette fails that gate, so
  `g_source_reference_valid` stayed false, and the fixture's exactly-one-write-
  per-published-frame invariant proved the substitution was inert. The gate is
  now reported in the freeze log rather than required.

Receipts: `cmake --build build --config Debug --parallel 4` exit 0;
`ctest --test-dir build -C Debug` **94/94**; Release rebuild then
`ctest --test-dir build -C Release` **94/94**. No commits, deployment, game
launch or headset qualification.

Experiment before believing either change. Animation: set
`arm_ik_block_animation = false`, watch the arms through a canned RB/RT swing -
with the block on they must stay on the controllers for the whole animation, and
the freeze log line must name the rig revision. If that line never appears the
block is inert for that rig, and the owned-row set is the first thing to check.
Height: find a doorway the operator passes standing; at +0.2 m it should need a
slight duck where +0.0 did not. An avatar that still reads short with the trim
on means the fit is not head-anchored (check `embodiment_fit` and the
`CalibrationFit::Arm` fallback), not that the trim is wrong.

### The neck question: keep the live neck solve, delete the head rider, 25 October 2026

Operator asked whether the neck bone system earns its keep. Two things carry the
word "neck" here, and only one of them was live.

**Kept.** `SolveTorsoPose` (`src/core/body_pose_math.h`) bends the authored
spine->chest->neck chain for lean and finishes with a rigid rotation about the
native Neck pivot that carries the whole Neck subtree, so the captured
Neck->Head link length is preserved and the actual skull axes turn. It runs every
frame from `src/core/arm_ik.cpp`, and Neck is in the owned-bone set, so under
animation ownership the neck is moved only by the wearer's own head. Its
regressions are `CheckNativeNeckFrame` (`tests/test_body_pose.cpp`) and the
Neck->Head link-length assertion in `tests/test_arm_pose_native.cpp`. Worth
keeping: it is the only thing that makes a visible neck follow the head at all.

**Removed.** `src/core/head_rider.h` (`HeadRiderOffsetModel`, `HeadEyePoint`)
translated the solved Head bone alone onto an eye point dropped below the
rendered camera. Its own header already said it was unused since 23 October and
that the approach was unfixable in place: the tracked eye is not a measured skull
pivot, so the shift stretched the authored Neck->Head link by up to its .35
clamp, and the eye offsets were guesses rather than captured geometry. Nothing in
`src/` called it; only `tests/test_head_rider.cpp` kept it green. A passing suite
for a superseded approach is how the next session gets talked back into it, so the
header, its test and both CMake entries are gone.

The matching producer went with it: `ArmIkHeadWorldDelta`, its
`HeadDelta`/`g_head_delta` state, the per-commit publish block in
`RetargetSourcePose`, and the `tests/test_camera_writes.cpp` stub plus its
`head_delta_reads` guard. That getter existed to hand the camera the solved
head's world displacement; the superseded history records that doing so detached
the viewpoint from the body in live testing, and the surviving header comment
said "must not feed the camera" while still exporting the symbol. An unused
export whose only documentation is a warning is a footgun, not an API. The
camera-side guard is now structural: the symbol does not exist.

Receipts: Debug and Release rebuild clean; `ctest --test-dir build -C Debug` and
`-C Release` now **93/93** (was 94/94 - `head_rider` was the deleted test, and
`ctest -N` is the source of registered names).
`git -c core.whitespace=cr-at-eol -c core.safecrlf=false diff --check` clean. No
commits, deployment, game launch or headset run.

Experiment before trusting the deletion: stand still and turn your head 90
degrees. The model's neck and skull must turn with the gaze - spine and chest
carry their shares, the Neck-subtree rotation finishes the turn - with no
stretched Neck->Head gap and no pop at the snap cap. If the neck now stays frozen
while the view turns, what is missing is not the rider; it is the Neck-subtree
rotation, and the first thing to check is whether `g_rig.neck` bound at all
(`ValidRig` requires `parents[head]==neck`), then whether the torso solve refused
that frame.

### Gaze-locked torso, torso/leg hiding, and the menu pointer beam, 25 October 2026

Operator request: the physical body, arms and weapon must face where the head
looks, the right stick turns the body the way the left stick already orients
locomotion, small head tremor must not drag the character, the torso and legs can
be hidden while arms and weapon stay, and the spatial menu pointer needs a visible
beam and smoothed motion.

**Gaze lock.** `TorsoTargetYaw` (`src/core/body_pose_math.h:456`) now maps the
head's planar yaw relative to the heading basis through
`kTorsoLookDeadbandRad = 0.035` (~2 degrees) of slack and a soft ramp that reaches
`kTorsoTwistLimitRad = 0.8` exactly at the twist cap, then `DampedAngle`
(`kTorsoDampTauMs = 90 ms`, `kTorsoDampDeadbandRad = 0.003`) approaches it. Below
the deadband a glance does not move the body at all; the ramp is what keeps a
90-degree glance from snapping the spine straight past its authored limit. The
right stick does not fight it: turning still goes through `TurnTracker` ->
`VrHeading::ApplyTurn`, the same latched path the left stick's head-directed
locomotion consumes, so the stick re-points the heading frame and the gaze keeps
the body facing inside that frame. Arms and weapon inherit the result rather than
tracking yaw separately, because `WeaponGripSolveTwoHand` and
`OrientModelHandPose` consume the applied body basis.

**Torso and leg hiding.** `hide_torso_and_legs` (`src/common/config.h:114`, menu
row "Hide torso and legs (arms and weapon stay)") sets native part bits 3 (body)
and 5 (legs) in `FilterMask` (`src/core/body_visibility.cpp:100`). Bit 4 (arms) is
never set and the primary weapon model is not read by this filter, so arms and
weapon keep drawing. `hide_head` stays a separate key: face, head gear and hair
(bits 0, 1, 2, 6) are not entangled with the torso request. The dirty-marking gate
previously tested `applied.filtered`, which conflated "some part was filtered this
frame" with "this part is hidden", so a mask change could go unmarked; it now keys
on `applied.mask`.

**Pointer beam and smoothing.** `ProjectMenuPoint` (`src/core/menu_presentation.h:101`)
projects the aiming hand onto the panel plane in the panel's own u/v, clamped to
the quad; `ProjectMenuPointer` publishes it as `beam_x`/`beam_y`/`beam_valid` and
`DrawPointerBeam` (`src/core/mod_menu.cpp:266`) draws the segment from that tail
to the cursor. The beam has to live in the panel's own texture because the menu is
a compositor quad - there is no world-space line to draw. `MenuPointerSmoother`
damps pointer motion with the same exponential the torso filters use: sub-pixel
tremor under `kMenuPointerEpsilonPx = 1.2` is dropped, a move past
`kMenuPointerSnapPx = 64` lands at once, and a gap past `kMenuPointerGapMs = 200`
re-primes instead of sliding across the panel.

Root cause worth keeping: the first smoother returned early when
`now_ms <= last_ms_`, so two frames inside one millisecond tick never advanced the
filter and the cursor froze mid-sweep. `mod_menu_render` caught it as a missed
Save click, not as a visual bug, which is why the failure looked like a UI
hit-test problem. The clock now advances on every sample and the epsilon/snap
tests run before the elapsed-time guard.

Receipts: Debug and Release rebuilds clean; `ctest --test-dir build -C Debug` and
`-C Release` **93/93**. Release core deployed to
`C:\Program Files (x86)\Steam\steamapps\common\DARK SOULS REMASTERED\DSVR.dll`
(SHA-256 `04cd62d725312ae05ec5bfa4b5cf6e3d1e8007ed46263ed4c51f4ac85e15a154`),
previous DLL backed up to `%USERPROFILE%\.dsvr-backups\pointer-lock-20261026\`.
`build\src\setup\Release\DSVR-Setup.exe --doctor` exits 0, so the shared-header
changes left no stale Release object. `tests/test_config.cpp` now round-trips
`hide_torso_and_legs` and `arm_ik_block_animation` away from their defaults, so a
dropped parse or save line fails the suite instead of silently reverting to the
default. No commits, no game launch, no headset run. An untracked
byte-identical copy of `src/core/arm_ik.cpp` sits at the repository root
(`arm_ik.cpp`, `fc /b` reports no differences); it is outside every CMake target
and was left alone for whoever made it.

Experiment before believing any of it: look 45 degrees off your travel direction
and hold it - the body must rotate toward the gaze and settle inside the twist
cap, not oscillate, and a 1-degree head tremor must move nothing. Then flick the
right stick: the heading, not the torso twist, has to move. With
`hide_torso_and_legs` on, the arms and weapon must stay tracked while the torso
and legs are absent; if the arms go limp, the owned-bone set is dropping arm bones
with the spine rather than the filter excluding only bits 3 and 5. For the menu,
hold your hand still and watch the cursor: it must not crawl, then sweep across
the panel: the beam must stay attached to the cursor and end at the projected
hand, never lag behind it.

### Live part-visibility toggles from the mod menu, 25 October 2026

Operator request: unhiding the IK torso and legs has to be possible on the fly
from the mod menu, not only by re-applying a saved profile.

**What was missing.** The two visibility rows were draft edits: the tick only
reached the live config through the footer Apply/Save buttons, and the restore
depended on `ModelUpdateDetour` happening to notice that the mask changed on a
model it already knew. Hiding looked instant because the queue command that hid
the parts was already in flight; un-hiding had to wait for a different native
update, so it read as one-way.

**Added.** `BodyVisibilityRequestRefresh()` (`src/core/body_visibility.cpp:226`)
asks the native updater for a fresh parts command for the local player right
away. A queue command already pending (the dirty bit set) counts as refreshed,
because the detour applies whatever mask is live when the native updater
re-reads the records. The Settings rows now apply on tick
(`DiagnosticsRequestConfig(g_draft,false)` plus the refresh request), so hide and
unhide both land on the next native update with no reload and no reopening the
menu; Save still persists. The panel prints `filtered / policy refreshes /
refused` so the transition is visible instead of inferred.

**Tightened while there.** The re-verification inside `QueuePartsDetour` tested
`!FilterMask()`, so a toggle between reading the part records and handing the
scratch buffer to the native queue passed a buffer built for the previous mask.
It now requires `FilterMask() == mask` and otherwise lets the native game have
its own unfiltered copy. The block's indentation also made the re-verification
`if` look like the body of the preceding `for`; it never was.

**Unchanged on purpose.** The menu-canvas gate in `FilterMask` stays: while the
spatial canvas is up (title screen, DSVR menu, calibration) the filter is
intentionally off and the whole body draws, which is what the pinned
`theater = true` assertion in `tests/test_body_visibility.cpp` records. The
change therefore appears the moment the menu closes, and the panel text now says
so rather than leaving the operator to guess why nothing changed on screen.

Receipts: Debug and Release rebuilds clean, **93/93** in both. New coverage in
`tests/test_body_visibility.cpp`: part slots 3 (body) and 5 (legs) masked on their
own, slot 4 (arms) and every other record intact, the head set and torso/leg set
combining, untick restoring the geometry, the live records never written, and the
explicit refresh producing a dirty-flag write. Release core deployed to the game
directory, SHA-256
`92f4e0052caa1de2c0e58e8df15f477a2747aae4ff633c38744dd59ee629fad8`; the previous
DLL is at `%USERPROFILE%\.dsvr-backups\pointer-lock-20261026\DSVR-before-unhide.dll`.
`DSVR-Setup.exe --doctor` exits 0 after the shared-header change. No commits, no
game launch, no headset run.

Operator standing direction this session: the latest build always lands in
`dist/`. `pwsh tools/auto_verify.ps1` ran after the green Release suite and
refreshed `dist\DSVR-latest\`, `dist\DSVR-latest-x64.zip`,
`dist\DSVR-latest-verification.json` and `dist\DSVR-latest-ctest.txt`
(`mode=latest-dev-test`, `ctest=93/93`, `doctor=pass`, built
`2026-10-25T05:25:35Z`). `dist\DSVR-latest\DSVR.dll` hashes identical to the DLL
deployed to the game directory, so the test package and the installed mod are the
same bytes.

Experiment: tick "Hide torso and legs", close the menu - torso and legs gone,
arms and weapon still drawn and still tracked. Reopen, untick, close: the same
geometry returns with no reload. If it does not return, read the status line
first: a rising `refused` means the actor binding changed under the toggle, not
that the mask is wrong, and the thing to chase is `ReadBinding` identity, not the
part bits.

### The physical attack and defence bridges: swings and shields drive native buttons, 25 October 2026

Operator request: "Get Physical attacks working." Nothing in this log recorded
that unit, so it is written up here together with the defence half.

**What was actually missing.** With IK arms applied and animation ownership
taken, `MapVrPad` nullifies every canned combat output
(`src/core/pad_math.h:166-177`): weapon squeeze/trigger RB/RT, off-hand squeeze
LB, off-hand trigger LT. That suppression is correct - a canned animation drives
the same bones the IK solver owns - but it left the player unable to attack,
block or parry at all. The arms were solved and the game never heard them. The
gap was not native contact damage, which is still an open gate; it was that a
physical arm produced no input.

**Two bridges, one shape each.** `src/core/swing_attack_math.h` turns a
qualified weapon-hand swing into exactly one press of the native light/strong
attack: rest, then armed, then a swing above the configured band, one press,
then a cooldown; a swing that never comes back to rest never re-fires.
`src/core/shield_guard_math.h` is its mirror for the shield hand: a raised hand
held still for `guard_settle_ms` holds the native shield button for as long as
it is held, and a short fast impulse that returns to rest taps the same button.
DSR already separates a block from a parry by press length alone, so neither
bridge invents a native binding - only press windows. Guard windows, stamina,
chip damage, parry judgement and ripostes stay the game's and stay unqualified.

**The guard is a height band, not a pose.** `src/core/input.cpp:67-72` measures
the shield hand head-relative (`kGuardHandBelowHeadM` 0.85,
`kGuardHandAboveHeadM` 0.5), so the block follows the player wherever they hold
the shield, overhead or at the belt. What makes it a guard rather than a wave is
that it is held still, so the settle window is the entire pose test and no
canonical guard pose is encoded anywhere. Hand velocities are head-relative, so
roomscale walking moves head and hands together and cannot read as a flick.

**Root causes worth keeping.**
- The first attack trigger re-fired repeatedly on one continuous swing: the
  press was released after its window while the swing condition was still true,
  so a single swing became a stream of attacks. One press per swing needs a
  per-swing latch, not a per-press cooldown.
- The guard required the shield hand to be raised on the frame the flick
  *ended*, which silently rejected every downward flick. A downward flick is a
  parry in a direction like any other, and the hand is usually still coming down
  when the impulse finishes. The raise test now happens at flick start
  (`flick_raised_`, `src/core/shield_guard_math.h:104`).
- A parry press may only begin from a released button (`release_ms` after
  `block_off_ms_`). Without that the game reads a continuous hold, and the
  "parry" is a block with extra steps.
- A pending parry whose released button never arrives expires rather than
  queueing: a stale tap landing during a later guard is worse than the parry
  that never happened.
- Near-opposite flick directions and single-frame tracking spikes are handled by
  `flick_settle_ms`/`flick_return_ms` plus a peak band, so a 12 m/s backhand is
  classified as a swing with the shield in hand, not a parry.

**Configuration and menu.** Five keys, default off, parsed/saved/validated at
`src/common/config.cpp:223-231`, `:479-485`, `:528-531`:
`physical_defence`, `guard_settle_ms`, `guard_flick_speed_min`,
`guard_parry_press_ms`, `guard_parry_cooldown_ms`. `GuardParamsFromConfig`
(`src/core/input.cpp:392-405`) keeps the bands ordered no matter how the sliders
are crossed: the flick bar stays above the still band, the ceiling above the
floor, the cooldown at least twice the tap. The Combat panel gained the toggle
plus four sliders and a plain-language description of what the body has to do
(`src/core/mod_menu.cpp:498-506`).

**Receipts.** `cmake --build build --config Debug --parallel 4` then
`ctest --test-dir build -C Debug --output-on-failure`: 95/95. Same pair for
Release: 95/95. No new compiler warnings from the changed files. New registered
test `shield_guard`.
`tests/test_input.cpp:664-800` (`PhysicalDefence`) drives the whole chain
through the published pad rather than the state machine alone: guard held open,
hand at the side cannot guard, one flick is one tap and then the guard returns,
a second flick inside the cooldown cannot tap, a hand that keeps moving is a
swing, motion between the still and flick bands drops the guard without
parrying, roomscale walking neither drops the guard nor flicks, tracking loss
releases and re-engages, the weapon hand cannot tap the shield button, the
left-hand shield behaves identically, and eleven gates (disabled, uncommitted,
log-only, no animation ownership, wrong epoch, sanctuary, menu capture,
calibration capture, camera not gameplay-ready) emit nothing and the guard
resumes by itself once a gate is released. `tests/test_pad_math.cpp` pins that
the guard survives `suppress_weapon_attacks`, never reaches the canned LB block,
and is dead during native menu navigation. `tests/test_config.cpp` round-trips
the five keys and pins their defaults. `pwsh tools/auto_verify.ps1` ran green
afterwards and refreshed `dist\DSVR-latest\`, `dist\DSVR-latest-x64.zip`,
`dist\DSVR-latest-verification.json` (`mode=latest-dev-test`, `ctest=95/95`,
`doctor=pass`, built `2026-10-25T06:42:13Z`). The Release core was deployed to
the game directory and hashes identical to `dist\DSVR-latest\DSVR.dll`
(`c004d57a…02b00e`); the replaced DLL is at
`%USERPROFILE%\.dsvr-backups\physical-defence-20261025\DSVR-before-defence.dll`.
No commits, no game launch, no headset run.

**Still open, and the probe for it.** No native damage is bound and nothing here
is headset-verified. The presses are ordinary XInput-shaped lines at the same
seam as every other motion button; whether DSR's own guard windows accept a
90 ms tap as a parry is a headset question. The log line
`Combat: shield flick at %.2f m/s -> parry tap` is the probe. Experiment: enable
both bridges with committed IK arms and animation ownership, raise the shield
hand and hold it still - the block should engage within ~120 ms and stay while
held; then flick that hand and stop. If the game blocks instead of parrying,
lengthen `guard_parry_press_ms` before touching the state machine, and if the
guard never engages check the hand height against the head before assuming the
settle window is wrong.

### The arm-swing gait detector: swinging the arms asks the game to run, 26 October 2026

Operator request, in the same breath as the combat bridges: sprinting should
also be possible by "swinging the arms in loose alternation in a running
motion repeatedly", while holding the dash as normal keeps working.

**What landed.** `src/core/arm_swing_math.h` is a head-relative two-hand gait
detector, the movement counterpart of `swing_attack_math.h`. A fast phase on
one hand counts as a stride only if it lasted `stride_ms_min`, stayed inside
the speed band, and started while the other hand was quiet; the swap count only
advances when consecutive strides change hands inside `window_ms`. Output is
one `sprint` flag plus a 0..1 `strength` ramped by `ramp_alternations`, so the
caller composes it with whatever locomotion axis it already has.

**Why that shape.** Three movements look like running to a speed threshold and
are not: one hand flapping (no swap ever happens), a two-handed swing or stretch
(both hands move together, which a running gait never does), and a weapon swing
(above `stride_speed_max`, and the attack bridge already owns it). The overlap
test is taken at stride *start* and marks the already-open partner phase too,
because a crossover inside one frame would otherwise cancel a legitimate stride
while a staggered two-handed stretch would otherwise pass. Same-hand repeats
and over-long gaps clear the count rather than carrying stale strides.

**Not wired yet.** Nothing feeds it and nothing consumes it: no `VrPadInput`
field, no config keys, no menu control. That is deliberate -- the composition
point for the forward axis and the dash button has to be chosen together with
the roomscale sampler so one applied snapshot owns both, and that is the next
unit, not this one.

**Receipts.** `cmake --build build --config Debug --parallel 4` then
`ctest --test-dir build -C Debug --output-on-failure` 96/96, and the same for
Release, both 96/96. `ctest -N` now lists `arm_swing` (#60). New test
`tests/test_arm_swing.cpp` covers: alternation engages and ramps to exactly 1.0
and never past it; one-hand flapping, both-hands-together (same-frame *and*
staggered onsets), below-band, above-band, and a fast phase held past
`stride_ms_max` all stay out; the run lingers for `hold_ms` then releases and
clears its count; one stride after a long gap does not resume it; a backwards
clock, a NaN and a negative speed clear the run and it recovers afterwards;
four impossible parameter sets refuse it; and the same hand history always
produces the same per-frame output. No commits, no deployment, no game launch,
no headset run.

**Next probe.** Add `arm_sprint` to `VrPadInput`, feed it from the existing
head-relative hand speeds at the same seam that feeds `swing_buttons`, and
compose it into the forward axis next to `roomscale_x/y` in `pad_math.h`. The
first thing to check in-headset is whether a real running arm motion actually
keeps the partner hand under `quiet_speed_max` at the crossover; if it does not,
lengthen `quiet_speed_max` before touching the state machine.

### Frame pacing is now measured, not assumed, 26 October 2026

Refresh rate is the display's active Hz; frame pacing is the *consistency* of
the interval between successive displayed frames. Matching the runtime's
requested cadence is only half of a smooth result: a title can average exactly
90 Hz and still judder if the intervals alternate 11.1/22.2 ms.

**What landed.** `src/core/frame_pacing.h` is a fixed-storage interval
distributor: 400 x 0.25 ms buckets to 100 ms, nearest-rank p50/p95/p99 read at
the bucket midpoint, mean/min/max, plus `long_frames`/`short_frames` counted
against 1.5x the window median and an `Evenness()` of
`1 - (p95-p50)/p50`. Absurd samples (non-positive, non-finite, over 100 ms: a
paused game, a modal menu, a session gap) are refused and counted, never
averaged in. `PacingMatchesTarget()` compares the measured mean against
`PeriodNsForFps()` from `display_rate.h` rather than inventing a second period
formula. `src/core/frame_rate.cpp` records the wall interval between
successive entries of the presentation-owner detour on the render thread (the
only writer, so no lock) and emits one `Pacing:` summary every five seconds,
tagged `NOT-MATCHING-RUNTIME-CADENCE` when the mean is not the runtime's.
Reset happens on owner change and uninstall, so a new session cannot inherit
the previous session's distribution.

**Why that shape.** K1 asks for p50/p95/p99 and frame drops; `cpu_frame_timer.h`
only carries mean and max, and `display_rate.h` proves the requested *value*
reached the native setter, not that the resulting intervals are even. Bucket
medians make one exact cadence read as that cadence instead of its lower bucket
edge. Classifying long/short at read time against the median, instead of at
Record time, keeps the result independent of arrival order. The detour measures
the game's submission cadence; the XR-reported refresh stays the display's
cadence, and the two are compared rather than conflated.

**Also fixed here.** `PresentOwnerDetour` forwarded four arguments into a
native callee whose full arity is not pinned. `tests/test_frame_rate_native.cpp`
already asserted that the fifth (stack) argument survives; it did not. The
detour and `PresentOwnerFn` now carry and forward the fifth argument.

**Receipts.** `cmake --build build --config Debug --parallel 4` then
`ctest --test-dir build -C Debug --output-on-failure` 98/98, and the same pair
for Release, 98/98. `ctest -N` lists `frame_pacing` (#64); `frame_rate_native`
(#4) and `display_rate` (#63) pass. New `tests/test_frame_pacing.cpp` pins: an
empty window invents nothing; a perfectly even 90 Hz window reports 90 Hz with
`Evenness()==1.0` and no long/short frames; 2:1 judder averages to a clean
60 Hz and *still* fails (`p50 < 15 ms`, `p95 > 20 ms`, 500 long frames,
`Evenness() < 0.2`) - that is the case a mean-only check would wave through; one
held present inside 999 exact ones is counted while p99 stays put; early
presents count as short, not long; five absurd samples are refused and counted
without moving the mean; min/max are real samples and the first sample seeds
min; the log interval drains while the cumulative window keeps running; reset
clears both; out-of-range percentile queries refuse. No commits, no deployment,
no game launch, no headset run.

**Next probe.** In-headset, read the `Pacing:` line at a steady scene. The pass
condition is `target=<runtime cadence>` with no
`NOT-MATCHING-RUNTIME-CADENCE`, `even` at or near 1.0 and `long=0`; a matching
mean with `even` far below 1.0 is judder, not a rate fault, so look at what is
holding presents (GPU time in the `RenderMetrics` line, the native limiter's own
history after a setter call) before touching `display_rate.h`. A
`NOT-MATCHING-RUNTIME-CADENCE` tag with a correct `RateMatch:` line means the
setter did not take effect on the owner - re-check `owner+0x60` and the
`ShouldApplyNativeRate` re-apply path rather than the measurement. `rejected`
growing while `frames` is steady means the game is stalling past 100 ms and the
window is honest about refusing to average it in.

### Menu/input failure and source recovery checkpoint

Latest supplied DSVR.log (06:07:46): both inline XInput hooks fail with MH_ERROR_MEMORY_ALLOC; GetState IAT fallback succeeds and is polled, but capabilities fallback refuses because no unique owned import exists. This supports a front-end controller-enumeration failure, not proof that locomotion math is the only cause. Right-stick look and reported smoothness do not qualify buttons/menu navigation. Next probe: establish the real capabilities provider/import ownership from pinned executable and existing captures; preserve refusal for foreign/ambiguous imports.

An assistant shell edit removed newlines from mod_menu.cpp. Reconstruction restored compilation, but subsequent token reconstruction altered formatting and some user-visible strings. Semantic recovery is NOT certified; audit this file against pre-incident history before deployment. No installed DLL was changed in this recovery turn.

Observed commands: cmake --build build --config Debug --parallel 4 (initial recovery attempts FAIL, final PASS); ctest --test-dir build -C Debug --output-on-failure PASS 98/98. Release build PASS and ctest -C Release PASS 98/98. pwsh -NoProfile -File tools/auto_verify.ps1 PASS, Release 98/98, doctor PASS; dist/DSVR-latest-x64.zip refreshed (1405900 bytes). Logs: build/menu-recovery-debug.log, build/menu-recovery-debug-tests.log, build/menu-recovery-release.log, build/menu-recovery-release-tests.log, build/menu-recovery-auto-verify.log. No game launch or new native evidence; no commit/push. Package is an offline candidate, not qualified release. H remains blocked and all native combat/hitbox acceptance remains open.

### Menu label regression and correction to capabilities diagnosis

Restored five damaged strings in CalibrationPanel, SettingsPanel, CombatPanel, DiagnosticsPanel and DrawMenu, including the hide-head checkbox label (also its ImGui ID). A source probe first failed for the missing original label. The real WARP-render test now captures shipped ImGui text from inside the body panel: reintroducing the corrupted label fails mod_menu_render 0/1; restoring it passes. Early experimental logging outside an active window caused two segfaults; that approach was removed. Buffer logging also proved unsuitable because End finishes and clears the buffer. The retained test uses file logging inside the body panel and clears its own temporary capture to prevent appended stale text from passing. No receipts under build/verification were modified.

Correction to the previous capabilities diagnosis: live Ghidra GUI inventory (DarkSoulsRemastered.exe) exposes XInputGetState and XInputSetState imports, but no XInputGetCapabilities import or defined string. GetState thunk 14082f790 references IAT 142017ed4. Callers 140E1C180 and 140E1C200 enumerate users 0..3 via GetState and add successful users via 140E1E5A0. Consumer calls 140C9F6EF, 140C9FB7A, 140E27FF6 and 140E28986 also use GetState; the first stores success in object +0xC. Thus the capabilities refusal is real but is NOT established as the cause of failed menus. Do not weaken ownership checks or invent a missing capabilities import. Receipt: build/xinput-owner-static-current.json. Next read-only probe: trace the enumerated-user records into button/menu consumers and correlate capture mode with the failed session. No game-memory writes or input injection occurred.

Final commands this turn: cmake --build build --config Debug --parallel 4; ctest --test-dir build -C Debug --output-on-failure; matching Release pair; pwsh -NoProfile -File tools/auto_verify.ps1. All exit 0; fresh full Debug 98/98 and full Release 98/98; verifier Release 98/98 and doctor PASS. Exact output: build/unresolved-debug-build.log, build/unresolved-debug-tests.log, build/unresolved-release-build.log, build/unresolved-release-tests.log, build/unresolved-auto-verify.log. Package refreshed, not deployed. Full source recovery, native controller usability, stereo, physical combat, mesh hitboxes and crouch remain unqualified; no plan checkbox closed.

### VR menu attempts and lifecycle failure, 25 October 2026 07:28–07:40 local

Formatted mod_menu.cpp with clang-format, disabling include sorting and string splitting; 9825 non-comment tokens exactly match the pre-format candidate. Current complete builds and suites passed: Debug 98/98, Release 98/98; tools/auto_verify.ps1 exit 0, Release 98/98, doctor PASS, development ZIP 1407772 bytes. Logs: build/route-debug-build.log, build/route-debug-tests.log, build/route-release-build.log, build/route-release-tests.log, build/route-auto-verify.log. This last candidate adds once-per-second InputRoute and XInputRoute diagnostics only, not a claimed input fix. It is not deployed while the game is active.

The operator accepted VR testing. The preceding verified core was snapshotted and deployed without setup or backup-vault access. Sessions live outside build/verification: build/vr-menu-session-20261025-072840-be2bb72d and build/vr-menu-session-20261025-073548-065d1cdf. Both save roots and Steam app/config metadata were copied and hashed. No backup-vault files or existing verification receipts were changed.

SAFETY FAILURE: the first launcher PID 18592 exited after about five seconds. The initial runner mistakenly treated its exit as game completion and restored roots at 07:28:52, before the real Steam-launched game ran. Thus the later operator feedback is NOT a qualified disposable-save transaction, and subsequent original-save writes cannot be claimed restored. The modified runner held its deadline across bootstrap exit, but the second attempt rejected replacement-process ownership and exited before its automatic finally recovery completed. After the game closed, manual recovery moved both held original roots back and individually verified their SHA256 values; test outputs were retained, and manual-recovery.json records this narrower claim. No newer user/Steam metadata was overwritten.

An unowned game PID 15888 subsequently started at 07:38:36, parent 7992, outside either runner. A later inventory verification refused because that game still existed even after the operator said the window was closed. Do not kill unowned processes or claim present root equality from the earlier hash checks. Do not reuse this experimental runner until process ownership and lifecycle are independently qualified. No additional launches planned.

Operator reports: native menus fail; DSVR panel initially worked but also failed on retry. Native menus already started in theater, and manually focusing the desktop game did not fix controls. Those hypotheses are ruled out as sufficient fixes. Live logs showed controller triggers and completed calibration, GetState IAT polling, and only about 30–33 measured game FPS at requested/runtime 90 Hz. This is not a performance or controller acceptance PASS. Next bounded investigation uses InputRoute publication/capture/rearm flags and XInputRoute merged buttons/axes/foreground on a correctly owned session; no more blind headset retries. A3/H4 remain blocked/failed and no checkbox was closed.

### Post-run reconciliation, 25 October 2026 07:43 local

DSR eventually exited (NO_GAME_PROCESS observed). A fresh full root inventory compared to the second session snapshot reports Documents: 5 files, original_inventory_equal=False; AppData: 5 files, original_inventory_equal=True. Preserve newer Documents state; no automatic overwrite/rollback was attempted. Therefore do not claim that all post-test saves equal pre-test state. Original snapshot and retained test output remain in the session directory; both held-root paths are absent and both original-root paths exist.

With the game confirmed closed, the verified diagnostic package was snapshotted/deployed: build/vr-menu-session-20261025-074315-ac471cd9/deployment-intent.json and deployment-complete.json. This is deployment only, not a new launch or accepted native fix. Do not run the experimental launcher again until its Steam handoff/ownership failure is fixed and tested.

### Quest 3 controls: two reproduced regressions fixed, 25 October 2026

Root cause 1: the capture-release latch required every stick, grip, trigger and button on both controllers to become neutral simultaneously. A grip or menu-navigation stick still held after Home/calibration therefore muted a newly pressed A, walking and turning indefinitely. tests/test_input.cpp exercised the actual InputPollFrame -> InputLatestPad seam: newly pressed A with an old navigation stick held, and new confirm/walking with an old grip held, both failed on the old implementation. ReleaseCapturedHand now masks only channels held during capture, until their own release. It still prevents the calibration-completion trigger, closing-button and held-navigation leaks; unrelated fresh controls are not blocked. Theater/front-end and calibration cases extend this regression matrix.

Root cause 2: MenuGamepadSelect ignored controller_select whenever spatial_menu was true, relying on navigation_select from the rendered tracking snapshot. A valid raw A was discarded when that snapshot was missing. A new WARP test drove ModMenuControllerInput(A) with spatial presentation and an invalid ray: mod_menu_render failed before the fix, then passed. Input now publishes only A as focus confirm, independent from pose; the trigger remains solely on the valid spatial-ray mouse route. FeedInput switches to navigation on raw A. Existing trigger/ray click and no-phantom-mouse tests remain green; the two old raw-trigger-as-confirm assertions were replaced to enforce the intentional separation, not to waive a failed control requirement.

Additional regression coverage exercises the actual IAT detour with native_result=ERROR_DEVICE_NOT_CONNECTED and no capabilities export: Quest A+D-pad reaches user 0, stale original output is cleared, repeated unchanged polls retain the packet, release changes it, and unavailable motion returns disconnected. This is a synthetic bridge seam, not proof of game enumeration or native/headset success. Removed the misleading capabilities warning that presented its missing import as an established front-end failure.

Verification commands: cmake --build build --config Debug --parallel 4; ctest --test-dir build -C Debug --output-on-failure; matching Release build/CTest; pwsh -NoProfile -File tools/auto_verify.ps1. Final results: Debug 98/98, Release 98/98, verifier Release 98/98, doctor PASS, ZIP 1409577 bytes. Focused input|mod_menu_render|menu_presentation|xinput_iat: 4/4. The new test matrix initially had an unqualified camera-stub namespace compile error, corrected before testing; no stale executable was trusted. project(check,file=input.cpp) separately failed to locate src/core/build (harness working-directory error, no tests executed); the root CMake ladder above owns the observed results. Logs: build/quest-controls-debug-build.log, build/quest-controls-debug-tests.log, build/quest-controls-release-build.log, build/quest-controls-release-tests.log, build/quest-controls-auto-verify.log. Optional WARP render emitted build/quest-controls-menu.bmp and passed.

Deployment: verified game closed; snapshotted prior DSVR.dll, copied the packaged DLL, and compared its SHA256 to package metadata: a92b2a7436ab12a9d7a36795796e9ce0707a675eb31c70a24888d2624d8df3c1. Unique deployment receipts: build/quest-controls-deploy-20261025-075834-01c8ceb1/intent.json and complete.json. This operation touched no saves, config, vault or existing verification receipts, and launched nothing. Native/headset retest remains UNVERIFIED; earlier native failures are not erased, and no H gate is closed. Next protected experiment: captured grip/stick held across Home/calibration close, new A/walking, then A with ray lost, retaining InputRoute/XInputRoute evidence. Do not reuse build/vr-menu-check.ps1.

### Physical-combat investigation blocked by native analysis availability

Read the current contract, CombatNative adapter, AtkParam qualification and preserved native contact evidence. Production CombatNative currently reads and validates equipment only; physical_damage_bound/shield_contact_bound remain false. Cached contact dispatch still has a split type-1 continuation and an indirect target dispatch, not a verified end-to-end physical damage ABI. Selected rows and a plausible HP setter do not qualify contact submission, lifetime, thread ownership, target volumes or defenses. The retained physical-tip path also still assumes .9m and is not independent weapon geometry. No placeholder implementation was added.

Current tools expose neither GhidraMCP nor REA MCP. Exact tool discovery failed to find GhidraMCP/check_connection. Following the installed REA guide, npx --yes rea-agents@latest doctor exited1: REA6.8.0 readiness failed, including Node26.3.1 outside its supported range and unavailable configured engines. The installed/pinned CLI npx --yes rea-agents@4.0.1 analyze C:\Tools\DarkSoulsRemaster_De\analysis-full\DSRFull.gpr --provider ghidra --format json exited1 with target_unavailable. A targeted inspect-native-api request for 0x1403C4020 against the pinned executable, first with process-scoped GHIDRA_INSTALL_DIR, returned runtime_missing. Resolving the existing JDK through process-scoped JAVA_HOME=C:\Tools\DarkSoulsRemaster_De\jdk-21 reached the decisive failure: unsupported_host, verified Windows Job Object ownership/private DACL/reparse-safe path admission not implemented. No permission/security bypass, global config edit, DB repair or headless shim was attempted.

Receipts: build/physical-combat-rea-probe.json, build/physical-combat-rea-api.json and build/physical-combat-rea-java-api.json plus corresponding stderr files; .empryo/jobs/job-59.log retains the readiness output. Existing cached reads at C:\Tools\DarkSoulsRemaster_De\combat-evidence\physical-combat-20261024 (contact-dispatch,1425ba7dc,1403c4162) provide no missing execution/lifetime proof and were not rewritten.

B/C remain blocked; no checkbox closed. No C++ changes, no CMake/CTest/package/DSVR doctor run, no game launch, memory write, input injection, save/vault operation or commit. Next prerequisite is supported read-only GhidraMCP access to the already-open pinned DSRFull database; then trace the actual contact/defense consumers and correlate native outcomes before implementing any dispatch. Do not substitute canned attacks or direct HP subtraction.

### Quest 3 D-pad channel bound for real; rotational blade sweep closes its own gap, 25 October 2026

**D-pad root cause.** The Quest profile suggested `/user/hand/left/input/x_axis` and
`y_axis` for a vector2 `dpad` action. Those are touchpad axes; the Oculus Touch
controller profile does not carry them, so the action never became active. The
channel looked bound and silently never answered, in the spatial menu and in game
alike. Meta's controller mapping reference publishes the thumbstick's four discrete
directions (Thumbstick Up/Down/Left/Right, both hands) as the Touch D-pad
equivalent, and Quest 3's Touch Plus controllers have no physical D-pad.
`src/core/input.cpp` now creates four boolean actions `dpad_up`/`dpad_down`/
`dpad_left`/`dpad_right` bound to
`/user/hand/{left,right}/input/thumb_up|thumb_down|thumb_left|thumb_right`, and
`ReadDpad` synthesizes signed axes from them with opposite directions cancelling.
Downstream consumers were already correct once the channel went live: `MapVrPad`
D-pad bits into the game's XInput packet, ImGui `GamepadDpad` keys for the spatial
menu, and the capture-release latch.

**Test seam.** `ProfileBindings` now requires eight direction bindings and zero
phantom paths. `DigitalDpad` drives the real `InputPollFrame` -> `InputLatestPad`
seam: gameplay D-pad reaches the game, a new direction replaces the previous one,
D-pad beats stick-synthesised navigation, the spatial menu navigates, the D-pad
cannot leak when the menu closes, and it works again after the latch releases.

**Blade sweep.** `BladeSweptOverlapsSphere` only covered the straight
`prev_tip -> tip` chord. A fast wrist rotation carries the tip along an arc, so a
small target on the arc's outer side sits inside the volume the blade actually
occupied yet outside both capsules and outside the chord: a fast slash through a
small target could miss. `BladeSweptOverlapsSphereArc` subdivides the rotation
(bounded `kSweepSteps = 4`), tests each sampled capsule plus the tip-to-tip
segment, refuses to invent a rotation plane for near-parallel or near-opposite
pairs, and fails closed on non-finite input. `BladeKinematics` gained `prev_grip`
and the bench in `src/core/arm_ik.cpp` feeds the previous frame's native grip;
`EvaluateBladeContact` now uses the arc test. New `tests/test_combat_math.cpp`
cases: chord misses while the arc hits at the mid-arc target, no coverage beyond
the band, pure translation agrees with the chord, a 180-degree flip falls back to
the chord, NaN and negative-radius refusals, and end-to-end `EvaluateBladeContact`
reports the arc-only contact. This is geometry only: `physical_damage_bound` is
still false and nothing was submitted to the game.

Verification commands, all run this session:
`cmake --build build --config Debug --parallel 4`;
`ctest --test-dir build -C Debug --output-on-failure`;
`ctest --test-dir build -C Debug -R "input|mod_menu|pad_math|menu_presentation|xinput_iat"`;
`cmake --build build --config Release --parallel 4`;
`ctest --test-dir build -C Release --output-on-failure`;
`cmake --build build --config Release --target dsvr_setup -- /t:Rebuild /m:4`
(forced full Rebuild because `BladeKinematics` is a shared-header struct change);
`pwsh -NoProfile -File tools/auto_verify.ps1`.
Results: Debug build clean, **Debug 98/98**; focused input/menu subset **9/9**;
Release build clean, **Release 98/98**; verifier Release **98/98**; doctor PASS
(pinned build supported, App 1.03.1 / build 10943698);
`dist\DSVR-latest-x64.zip` **1414197 bytes** + `DSVR-latest-verification.json`.
Logs: `build/q3-dpad2-debug.log`, `build/q3-dpad2-ctest-debug.log`,
`build/q3-dpad2-focused-debug.log`, `build/q3-dpad2-release.log`,
`build/q3-dpad2-ctest-release.log`, `build/q3-dpad2-setup-rebuild.log`,
`build/q3-dpad2-autoverify.log`. An earlier packaging attempt failed on a locked
`dist\DSVR-latest\DSVR-Setup.exe` held by an open setup window; the process was
not killed and the refresh succeeded after the operator closed it.

No game launch, no memory write, no input injection, no save or vault operation.
Native/headset D-pad behaviour remains **UNVERIFIED**: the profile-path claim
rests on Meta's published mapping reference, not a captured Quest 3 session, and
no H gate is closed. Next headset probe: enumerate the active Oculus profile's
available paths and confirm `thumb_up`/`thumb_down`/`thumb_left`/`thumb_right`
become active, then re-run the menu and XInput D-pad rows. For the blade, the
next probe is a recorded slow-versus-fast wrist rotation past a small static
target: the arc test must report contact on the fast pass where the chord test
did not, and neither may report contact when the blade stays clear.

## 26 October read-only melee subagent: 14032F548 verdict + next-probe first half

Task-owned files: `docs/NATIVE_MELEE_CONTRACT.md` (new 26 Oct verdict
subsection at the end: verdict, evidence, limitations, exact next probe,
probe outcome, +0x73 linkage), this handoff line. No game launch, repair,
save, write, commit, or native call.

Evidence added: `14032F548` recorded as instruction candidate only —
byte-pinned `CALL [RAX+0x640]` in a `.text1` split gap (forged-JMP island),
zero xrefs, RAX/receiver/args unrecovered; adjacent `140322910` has 27 CALL +
3 DATA refs with `548` contributing none; forward JMP `1425C31F0` + TEST ESI
sign branch, parallel `+0x638` slot lead, known `+0x640` inhabitants
EnemyIns `1413234A8->14034BF70` vs PlayerIns `140339600` with no xref link to
`548`.

Probe outcome (read-only GhidraMCP, GUI reachable, `get_assembly_context` 12
each side): `142756FC5` = `TEST AL,AL` in a thunk/return-pool island with a
`CMOVNZ` return-address selector toward `14032F584/594` (not a producer);
`1421B6015` = `MOV R14,RAX` with a `CMOVZ` selector taking `14032F54E`
(`548`+6) as a conditional return immediate (no xref; producer still
unrecovered); `1411B6015` = `MOV RCX,[R10+0x30]` negative control (split gap,
no `548` link); `14032F471` = `JMP 0x142656c78` in garbage/forged island
before `14032F480`, not a trustworthy linear producer. DB unsaved/unmutated.

+0x73 linkage appended as cache transcription only: `kMapHitType` /
`NativeMapHitType` exist in code; contract notes `1403C72D0` 23/24/25 still
needs DSR verification that it reads `0x73`.

Exact next probe (second half still open): compare `+0x640` slot inhabitants
+ natural hit/miss correlation under TESTING; never invoke `548`.

## Historical evidence recovery

The [history index](docs/history/README.md) retains the complete previous MODLOG and other replaced documents, including run IDs, crash observations, capture paths and restoration receipts. They are dated evidence only. Verify the candidate and underlying artifact before reusing any finding. Historical next actions and approval language are superseded.

## D stereo read-only next probe, 26 October 2026

Live-GUI `DSRFull` D-probe handoff (read-only; no game launch/repair/save/replay/write/commit): backward trace from `1411B4950`-class seeds in three bounded windows recovered the RDI/R14 producers at the `1411B46C0` ctor head (`RDI=RCX` at `46DF`, `R14=RDX` at `46DC`; span `[1411B47CE,1411B491A]` still uncovered); renderer+10's sole observed store is `1411B494E` (`140CC5B80` return with `"HighGraphics"` name arg, or `RSI` null-alloc fallback); `140CC5E60` (5B `MOV [RCX+0x18],RDX; RET`, 5 xrefs) and `140CC5EB0` (42B 11-insn body, 27 xrefs) byte-pinned by read; full windows, bytes, and limits in `docs/NATIVE_STEREO_CONTRACT.md` ("26 October D-probe, second window"). Exact next probe: Begin-payload retain/release + derived dispatcher vftable, then close `[1411B47CE,1411B491A]` and type the R14 source via `1411B46C0` callers; fallback is the main-scene bracket via `1411BEA27/1411C5049/1411C2F6C`. Dirty tree preserved; GHIDRA_MCP_ALLOW_SCRIPTS untouched; nothing saved or committed. D1/D2 still open.

### E locomotion docs-only owner table, 26 October 2026

Created `docs/NATIVE_LOCOMOTION_CONTRACT.md`: status UNRESOLVED, no native motor binding (orientation done, motor attribution open). Six owner rows all UNKNOWN with file:line evidence pointers (XInput poller, stick mapper, motor/collision resolver, authoritative root, forced-motion source, head/solid query); three refusals (mixed-input non-attribution, forced-motion no-credit, blocked-residual heuristic bound per ENGINEERING.md:29); next probe is the pinned XInput poll caller first per E2 order. Scope: only `docs/NATIVE_LOCOMOTION_CONTRACT.md` + `MODLOG.md`. No build (docs-only); `ctest -N` count unchanged (101). No game launch/install/headset; no commit.

### D stereo re-read: RDI/R14 ctor-head producers re-confirmed + outer caller seed, 26 October 2026

Read-only GhidraMCP (live GUI DSRFull; no launch/repair/save/replay/write/scripts/commit): re-recovered the three backward windows from `1411B4950`-class seeds (stop at `46DC/46DF` producers), confirmed sole renderer+10 store `1411B494E`, byte/xref re-pinned `140CC5E60` (5B, 5 xrefs) + `140CC5EB0` (42B 11 insns incl `CALL 140CCE4D0` + tail `JMP [RAX+0x20]`, 27 xrefs), added `FUN_1411b46c0` identity + outer caller seed `1411B3FA5` (`0x300` alloc renderer; R14-source = outer RCX, type open); deltas appended to `docs/NATIVE_STEREO_CONTRACT.md` as the 26 Oct D-probe addendum (existing second-window section left intact). Exact next probe: type outer RCX at `1411B3F79` upward, then Begin-payload retain/release + derived vftable; fallback main-scene bracket `1411BEA27/1411C5049/1411C2F6C`. Span `[1411B47CE,1411B491A]` still uncovered. D1/D2 still open.

### Lever interact-range cache search: no citable value, 0.0 retained, 26 October 2026

Searched `build/research/soulsmodding-20261024` for a citable lever interaction range. Found only a cross-class `validDist` field default (`u16 validDist = 150` in `primary/soulsmods__Paramdex/files/{DS1,DS1R}/Defs/ObjActParam.xml`, DisplayName `[cm]` = 1.5 m) versus the raw wiki boilerplate "In metres" (`raw/ds1-refmat__param__objactparam.wiki` line 7; `tables.tsv` row `ds1-refmat:param:objactparam` 7). Lever rows in `primary/soulsmods__Paramdex/files/DS1R/Names/ObjActParam.txt` (1200/1202/3011/3012/4560/6100/6101/7201/7202/16010) and object-list lever names are name-only; the cache holds defs + names, no per-row regulation.bin values. Unit conflict + no per-lever value = not citable: applied no number, kept the Lever row at 0.0/refuse, added a code comment naming the exact cache paths searched. Build/tests: `cmake --build build --config Debug --target test_interaction_target` + `ctest -C Debug -R interaction_target` GREEN; same in Release GREEN. Scope: only `src/core/interaction_target_math.h`, `tests/test_interaction_target.cpp` (unchanged, still pins 0.0), `MODLOG.md`. No game launch/install/headset; no commit.

### D stereo third window: Begin-payload borrow-appearing + derived slot set pinned, 26 October 2026

Read-only GhidraMCP (live GUI DSRFull; no launch/repair/save/replay/write/scripts/commit): poster `1411BE560` maps RDI=dispatcher, R14=ctor-R14, RBP=`&renderer+20`, RSI=`&renderer+28` into msg `+40/+48/+50` (vftable `141609970`, queued via `140CC5E90`, sync fallback `[RAX+0x10]`); pump `1411BF1F3 CALL [RAX+0x10]` passes msg+40/48/50 as RDX/R8/R9; handler `1411BEF10` renames them R14/RDI/RSI with RBP=dispatcher. Verdict: +48/+50 borrow-appearing (plain `MOV RCX,[RSI]` / `[RDI]` into dispatcher +0x20/+0x28 at `1411BF01E..030`, no interposed AddRef; `140CD3040` untyped so hidden retain unexcluded; reverse store `1411BEF66 [RDI],RAX` mutates the renderer field), +40 open (untyped `140CCF680/5A0/5C0` + virtual +0x10/+0x8, none typed retain/release). Derived vftable: overridden set {+0x28,+0x30,+0x40,+0x48} pinned (`2DB/2E3/2EB/2F3` with `CMP RAX,R15` / `JZ retire` skip-on-base; R15=`1416098B0` at `1411BF175`); +0x38 shared (no base check); base 96B decodes to 12 pinned slots incl +0x10 `1411BEF10`, +0x50 `1411BE560`; same compare idiom for all four slots in `1411BE100/1B0/340/3F0`; derived table address/targets OPEN (`get_xrefs_to 1416098B0` = 11 DATA refs, all base sites, no second table loaded). Limits: `1411BEF3A/BF00D/BF34B` no-instruction gaps (BF00D superseded by observed BF029/030), listing slices only, no prototypes/decompile, `ram:14011BF2FC` still unpinned. Exact next probe: queue completion/teardown (`140CC5E90`, `1411BF2AE/140CC5920`, tear-down `1411BF1AE`, `140CC5600/140CC5410`) + main-scene bracket `1411BEA27/1411C5049/1411C2F6C`, fallback bracket alone if blocked. Details in `docs/NATIVE_STEREO_CONTRACT.md` ("third window"). Scope: only `docs/NATIVE_STEREO_CONTRACT.md` + `MODLOG.md`. D1/D2 still open.

### E2 XInput poller static probe: 6 call sites, 2 per-frame candidates, 26 October 2026

Read-only GhidraMCP (live GUI DSRFull, `/DarkSoulsRemastered.exe`; no launch/repair/save/replay/write/scripts/commit): `XInputGetState` thunk `0x14082f790` = `JMP [0x142017ed4]` (import `Ordinal_2`); exactly 6 `UNCONDITIONAL_CALL` callers, all classified. Top per-frame candidates: `FUN_140e27aa0` (site `140e27ff6`, `ECX=[RDI+0x264]`, thumbs state `+8/+0xA` → `/65535.0*2` floats at `+0x26c/+0x270`, zero code callers) and its mirror `FUN_140e28430` (site `140e28986`, `ECX=[RDI+0x204]`, floats at `+0x20c/+0x210`). One-shots: `FUN_140c9f6c0` (connected flag only), `FUN_140e1c180/200` (4-pad bind enumerators), `FUN_140c9faa0` (remap layer with 7849/8689 deadzone, cadence unproven). Stopped at the poller→mapper edge: no velocity mapping in the same windows, motor/resolver unclaimed. Details in `docs/NATIVE_LOCOMOTION_CONTRACT.md` ("E2 static probe"); exact next probe: consumers of `+0x26c/+0x270` / `+0x20c/+0x210` = mapper→motor boundary, plus 8 DATA-ref slot resolution. Scope: only those two files. All six owners still UNKNOWN; no commit.

### D stereo fourth window: queue/teardown shape + bracket scout, 26 October 2026

Read-only GhidraMCP (live GUI DSRFull; no launch/repair/save/replay/write/scripts/commit): `140CC5E90` is a 5-insn enqueue tail-thunk (`ADD RCX,0x78; JMP 140CC55A0`), not a completion point; retire `1411BF2AE` calls `140CC5920` then BPL decides loop (`JNZ 1411BF180` → `140CC5600`) vs exit; `140CC5920` branches on msg+0x10 bit0 (free path OPEN); teardown `1411BF1AE` is pump-owned (+0xbb gate, virtual +0x18, BPL=0, retire); wait `140CC5600` calls `[queue]+0x30`-object virtual +0x10 (identity OPEN); type `140CC5410` is `MOV EAX,[RCX+0x14]; RET` with >7 retiring. Bracket: `FUN_1411c2f50` (49B, 5-arg forwarder, result halves) is the recommended live-observation bracket; `FUN_1411c4fa0` (222B) secondary for alloc lifetime (stack-local, unsupported past return); `ProcessDantelionRenderFrame` (1547B) explicitly not a bracket. Details in `docs/NATIVE_STEREO_CONTRACT.md` ("fourth window"). Scope: only those two files. D1/D2 still open.

### Type-64 (parry-context) static probe: type-2 dispatch + gates, 26 October 2026

Read-only live GhidraMCP against GUI `DSRFull` (PID 17320; token `1792646701740:fa2c6e65:3542`, unsaved true before/after; no save/repair/script/launch/write/commit): `1403C4210` (sole code caller `1403C4020`, +DATA `141d53e64`) routes kind 0 -> `1402F84F0` (notify `1403CA1D0` + effect chain `1403E6450`, return 1, no +34 read), kind 1 -> thunk `1402F8410`/`1429E5D7E` (forged-return selector on `[RDX+0x524]` bit to `140045643`/`1402F8494=JMP 142C58024`; meaning open, never step through), kind 2 -> `1402F84B0` (`target!=source && [record+0x34] < 0x40` then `1402FC360`, else 0 — attack_type 64 fails the gate). Type-3 consumers `140420A20`/`140420B30` share the `< 0x40` gate; `1402FC360`/`1403E6450` bodies have no +34 read. Producer re-read: `1405290C0` stores attack type at payload +34 and fills +30 via `1402DEB20` only when `+34==0x40`; which wrapper argument values pass 64 vs 0/2 is unrecovered (wrappers `140528CE0/140528E90/140528F70` pinned). Zero `== 0x40` consumers found: +34-xref not field-indexable (timeout-limited enumeration, not proof of absence). Details in `docs/NATIVE_MELEE_CONTRACT.md` ("26 October type-64 static probe"). Exact next probe: natural parry correlation under TESTING. Scope: only those two files. No binding proposed.


### Launcher chain-DLL FAILs print full colliding path, 26 October 2026

RED first in tests/test_setup_theme.cpp (stubbed chain-present/chain-move-fail conditions driven through CTL_INSTALL, log asserted for full game-dir path; RED confirmed as FAIL 154) then GREEN: src/setup/setup.cpp DoInstall chain-conflict message logs the full chain_dest path and the move-failure message logs dest_loader to chain_dest. DoInstall:522 and all behavior untouched, message text only. Verify: setup_theme PASS both configs; full ctest 101/101 Debug + 101/101 Release. No game launch/install/headset; no commit.

### Package mid-refresh atomicity documented + tested, 26 Oct 2026

K-audit follow-up (code+test, offline only): `tools/package.py` latest-mode comment corrected — the live `DSVR-latest/` refresh is intentionally non-atomic (per-file for Explorer locks; kill/lock mid-loop may leave a mixed stage) while the zip (replaced last) + verification.json (written after) stay on the old build, so a failed refresh never claims the new build. New `test_latest_mid_refresh_failure_keeps_old_zip_and_verification` locks a live destination mid-refresh, asserts nonzero exit and byte-identical old zip + verification. `test_package.py` 9/9 GREEN (unittest OK), `ctest -R package` PASS. Scope: tools/package.py, tests/test_package.py. No game/install/headset; no commit.

Read-only GhidraMCP (live GUI DSRFull, `/DarkSoulsRemastered.exe`; no launch/repair/save/replay/write/scripts/commit): full decompiles confirm both candidates take the device struct as param_1/RCX (dynamic heap base, so no concrete field address exists to xref). `read_memory` 8B each decoded all 8 DATA-ref slots: per-candidate duplicate RVA pairs plus one VA+1 slot each -- not vftables, vftable claim refused. `FUN_140c9faa0` sole caller is `thunk_FUN_140c9faa0` (`140c9f800`), dispatched from table `14149dc40` (thunk at index 6); table owner unclaimed, candidate 3 cadence stays UNPROVEN. Consumer search (whole-program plus function-scoped `search_instructions`, bulk `get_xrefs_to`) timed out with 0/3 windows completed; no velocity-scaling reader recovered, mapper/motor UNKNOWN. Details in `docs/NATIVE_LOCOMOTION_CONTRACT.md` (E2 follow-up static probe). Exact next probe: retry the field-pattern search or walk sibling tables `1414dde70`/`1414ddd20`; type table `14149dc40`. Scope: only those two files. No commit.


### E2 sibling-table walk: no float-field consumer, VA decode corrected, 26 October 2026

Read-only GhidraMCP (live GUI DSRFull, `/DarkSoulsRemastered.exe`; no launch/repair/save/replay/write/scripts/commit): 2 entry windows + <300 B decompiles each table at index 0 (trigger reader: `140e26780` 181 B / mirror `140e26840` 181 B, neighbouring `+0x3dc/+0x3ec` / `+0x37c/+0x38c` fields) and index 3 (event dispatcher: `140e28dc0` 169 B / mirror `140e28e70` 169 B, rumble-ish/`0x219` branches, no float reads) -- bound hit, no sibling reads `+0x26c/+0x270` / `+0x20c/+0x210`, mapper/motor UNKNOWN. Correction: `1414dde90`/`1414ddd40` are full-VA poller slots (index 4 of each 8-slot table), not VA+1. Details in `docs/NATIVE_LOCOMOTION_CONTRACT.md` (E2 sibling-table walk). Exact next probe: idle-bridge field-access byte-pattern search for the scaling reader (mapper->motor boundary). Scope: only those two files. No commit.

### B swing-time producer xref probe: constructor chain confirmed, no swing caller, 26 October 2026

Read-only live GhidraMCP against GUI DSRFull (token 1792646701740:fa2c6e65:3542, unsaved true before/after; no launch/repair/save/replay/write/scripts/commit): get_xrefs_to 1403C9B50 -> exactly 2 refs (DATA 141D54398, CALL 1404910AD) and 1403C7460 -> exactly 3 DATA refs (141D5417C, 1417991F0, 141799224); zero new xrefs vs priors. 1404910AD = CALL in FUN_140490F40 (ObjDam condition wrapper: resolves two objects via 140371200/140371360/140371bd0, gates nonzero, calls 1403C9B50(manager, param_2, lVar3); no animation/weapon/swing argument). Its own refs: DATA 141D612F8 + JMP-island 1404B0EC7 (split, not swing). DATA holders return no-instruction; RVA-decode confirms address tables (003C9B50/003C7460); bulk xref-back on all four is empty, so no indirect code owner is indexed. Verdict: no swing-plausible producer in either xref set; builder has zero code xrefs. Details in docs/NATIVE_MELEE_CONTRACT.md (B swing-time producer probe). Exact next probe: decode the 1417991F0/224 record stride to name the builder's dispatch owner, else natural-swing correlation under TESTING. Scope: only those two files. No commit.

### E2 byte-pattern retry: axis getters pinned, scaling consumer behind unindexed dispatch, 26 Oct 2026

Read-only GhidraMCP (live GUI DSRFull; no launch/repair/save/replay/write/scripts/commit): masked MOVSS/dword byte-pattern search (patterns from candidates' own RDI stores) completed cheaply where instruction search timed out. Direct paired readers are other-struct copy-out/clamp/smoothing, not this device layout. Family sweep pinned indexed axis getters 140e269a0/140e269c0 (table idx 5, read +0x26c/+0x20c by index) and mirror setters idx 7; all 16 dispatch-table slots have zero code xrefs, so the velocity-scaling caller is behind computed dispatch. Mapper/motor UNKNOWN. Details in docs/NATIVE_LOCOMOTION_CONTRACT.md (E2 byte-pattern retry). Exact next probe: byte-pattern for table VAs as immediates to type the dispatch owner, or outer-RCX device-struct typing. Scope: only those two files. No commit.

### B static probe: 1417991F0/224 are unwind metadata, no dispatch owner, 26 October 2026

Read-only live GhidraMCP against GUI DSRFull (PID 17320; no launch/repair/save/replay/write/scripts/commit): read_memory 64B at 1417991F0/141799224/141D5417C + neighbours decodes a 12-byte RUNTIME_FUNCTION triad (Begin 0x1403C7460, End 0x1403C749D = 61B body, UnwindInfo 0x1417991D8); 1417991F0/224 are mid-UNWIND_INFO offsets in .rdata (header 01/17/06/00, codes, handler RVA 0x00020521 + chained 0x000A6405 = the prior 'small-int fields'), not dispatch records -- the 12-/16B dispatch-record premise is refuted. get_xrefs_to (cap 20) on the three tables + +/-0x10/stride neighbours: only .pdata DATA self-refs, zero code indexed dispatch; 1403C7460 keeps exactly its 3 DATA refs. No dispatch owner found, no indirect invocation recorded, no swing producer claimed. Details in docs/NATIVE_MELEE_CONTRACT.md (B static probe subsection). Exact next probe: walk 14024F6B0 -> 1403C8DD0 -> 1403C6030/1403C6850/1403C7D30 -> 1403C7520 backward to find which first references the 1403C7460 fragment range and name its outer caller; else natural-swing correlation under TESTING. Scope: only those two files. No commit.

### Mod-menu gate consistency: restart footer extended + stay-editable sliders gated, 26 Oct 2026

Prior audit's described footer restart block / ModMenuCaptureTextArmed hook did not exist in this tree (verified by grep; the applied-vs-attached binding comparison lived only in input.cpp's frozen InputLiveBindingSignature). Implemented: DrawMenu footer now prints "Restart needed: <parts> (restart the game to apply)." comparing applied bindings vs InputLiveBindingSignature plus applied reshade_host/render_scale_percent vs a first-draw baseline snapshot in mod_menu.cpp (render fields have no attach-time freeze; baseline frozen on first DrawMenu, honest session-start reference, not a production attach snapshot). Gated with BeginDisabled following the controller-hand pattern: Snap angle on snap_turn, Turn speed on smooth_turn, Vignette strength on comfort_vignette, sparring distance on proving ground, swing speeds/cooldown on physical_attacks+motion_attacks, contact speed/re-hit on haptics, support offset/rotation on Enable support grip; correctly-conditional groups (arm-swing/reach/defence) untouched, no label/ID removed. RED first: new WARP asserts failed at the disabled-slider check (FAIL item_was_disabled) before gating; footer asserts then drove the capture-seam fix (footer hook must start before DrawMenu — armed via pending flag in BodyVisibilityGetStatus + started in CalibrationGetStatus, since a log binds at LogToFile time and LogFinish fires at the panel child's End). Test seams: fixture stubs InputLiveBindingSignature to the Config defaults and mod_menu exposes probe_disabled_item/item_was_disabled (never armed in production). GREEN: ctest -R mod_menu_render PASS Debug+Release; full suites 101/101 Debug + 101/101 Release. Files: src/core/mod_menu.cpp, tests/test_mod_menu.cpp. No game launch/install/headset; no commit.

### Mod-menu everyday/advanced separation: default-open Advanced tuning groups, 26 Oct 2026

RED first: extended tests/test_mod_menu.cpp with a WARP assertion that an "Advanced tuning" grouping exists and default-open shows advanced controls (arm-swing sliders visible when enabled) — rebuilt test_mod_menu, confirmed RED (no grouping yet). GREEN: src/core/mod_menu.cpp adds one labeled always-visible "Advanced tuning" text row inside SettingsPanel (above the arm-IK rows) and one inside CombatPanel (above the per-hand rows; grip editor reads under the same label), plus a comment row cross-referencing them — same panels, same IDs/labels/behavior, grouping only; plain Text row (not SeparatorText/CollapsingHeader) so the label stays greppable in the LogToFile capture, no collapse so default renders show every advanced control, no new capture call above the footer capture point. Combat route asserted structurally via shipped next/prev-panel edges (0 -> 3 -> 0) with panel-ID re-resolve; no Combat-rows-in-capture assertion (one-shot hook has Settings-route-only stubs, shipped footer hook sits below the panel switch). Verify: cmake --build build --config Debug --target test_mod_menu + ctest -C Debug -R mod_menu_render PASS; same Release; full ctest 101/101 Debug + 101/101 Release. Files: src/core/mod_menu.cpp, tests/test_mod_menu.cpp, MODLOG.md (this entry). No game launch/install/headset; no commit.

### 25 October continuation: F2/C2 committed-pose lifetime and B update-owner evidence

Resumed the existing plan's later IK/physical-combat steering. HEAD
`ca989d74ae388f1b41d33b14ecfea2833ed19b71`; all pre-existing dirty work
preserved. Existing CMake cache: Visual Studio 18 2026/x64, LAB OFF,
ReShade addon ON. Graph/source-harness tools are unavailable in this chat,
so targeted direct source reads followed tool discovery. No delegation.

Reproduced a source defect after successful native-copy verification:
`cmake --build build --config Debug --target test_arm_pose_native --parallel 4`,
then `ctest --test-dir build -C Debug -R '^arm_pose_native$' --output-on-failure`
failed with `post-publication reader 0 mutation 1 returned stale poses`.
Losing focus between native callbacks still exposed the old valid hands.
`ArmIkHands` now stamps its committed calibration revision, and both public
readers share a lifetime check for focus/HMD/controller tracking, current
tracking timestamps, camera sequence/epoch, calibration, modes and capture.
Refusal ends the receipt and clears diagnostic motion history; restoring
external state cannot reopen it. A fresh verified natural copy reprimes at
zero speed. No new native call/write or hook was added.

Regression: 19 transitions independently through each reader (38 cases),
empty outputs, no native write/callback during reads, latched refusal,
fresh-copy recovery, calibration stamp and acceptance of a newer valid XR
packet with the same camera-bound native pose. The first shared-guard draft
erased a provisional source publication; existing CheckCombatBench caught
it before acceptance. An unverified-copy early return now preserves the
pending snapshot. Head-only/one-hand and duplicate-callback tests remain
active. This is adapter evidence, not modified-candidate native skinning.

Native B investigation: the installed GhidraMCP bridge still reaches GUI
DSRFull despite Empryo's current MCP config listing only REA. An inline SDK
client listed advertised tools before read-only calls; no shim/headless
wrapper or Ghidra script was created. Update owner `1402500D8` is named;
`1403C8DD0` calls `6030` and `6850`, the latter reaches `7D30` and directly
`7520`. Corrected the older `6030 -> 7520` claim. No indexed entry from those
paths into `7460`, including its unwind fragments. Independent EXE comparison
pins all 338 bytes of `1403C72D0` and `1403C7460`; `1403C733A` reads selected
AtkParam +0x73 and chooses messages 23/24/25. Message meaning, damage,
defenses, lifetime and thread correlation remain unresolved. Raw calls and
primary-byte-verification.json are outside distributable sources at
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\physical-combat-20261025-update-owner\`.
Executable SHA256 remains `a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`.

Final full commands: `cmake --build build --config Debug --parallel 4`;
`ctest --test-dir build -C Debug --output-on-failure`; matching Release pair.
Both builds exit 0, **Debug 101/101 and Release 101/101**. Logs:
`build/continue-20261025-final-{debug,release}-{build,tests}.log`.
Focused red/green logs use `build/continue-20261025-publication-*`.

The first `pwsh -NoProfile -File tools/auto_verify.ps1` passed its tests and
doctor but packaging exited 1 on locked `dist/DSVR-latest/DSVR-Setup.exe`
(PID 33496); log `build/continue-20261025-auto-verify.log`. The old ZIP and
verification remain the old candidate; the live extracted directory may be
mixed after the refused refresh. Do not call that directory a verified new
package. Final `pwsh -NoProfile -File tools/auto_verify.ps1 -NoLatest` exits
0: Release 101/101, doctor PASS; log
`build/continue-20261025-final-auto-verify.log`.

Separate development package completed after midnight on 26 October:
`python -B tools/package.py --build build --out dist/continue-20261026-pose-lifetime --version 0.16.0-render-host-preview --latest --reshade-addon --ctest-log build/latest-ctest.txt --doctor pass`
exits 0. Output `dist/continue-20261026-pose-lifetime/DSVR-latest-x64.zip`
with its adjacent verification JSON, Release 101/101 and doctor PASS.
Core DLL SHA256:
`4c4e33c91adb82ebd240349b888da32ff360fc68425e7202278f476e6c135ab7`.
The development package is refreshed once more to include this completed
handoff; this is not a formal release, installation or native acceptance.

After the user confirmed Setup was closed, verified no DSVR-Setup process
remained and ran the same gated package command with `--out dist`.
Standard `dist/DSVR-latest-x64.zip`, extracted files and verification JSON
now contain the tested candidate. Log:
`build/continue-20261026-latest-package.log`. The standard package is refreshed
with this note and checked against its binary hashes; the earlier lock no
longer blocks packaging. No game deployment was performed.

Read-only preflight found an already-running, unowned game PID 24412 and
active desktop use. Its installed DLL SHA256 is
`3a136a395a55b7b3bd8678f05d0c47d80af7cd8916de3c7b5e8186211b52a880`,
not this fix. No installation, game launch/stop/input, process-memory read,
save/config/vault mutation, or restoration claim. Preflight log:
`build/continue-20261025-preflight.log`. Native/body/headset acceptance and
B/C stay open. Next experiment: after the active session closes, qualify
the protected transaction, then test lost focus/controller tracking and
recovery on the exact new candidate; observe source-to-body/armor propagation
and separately labelled native selected-context hit/miss/block/parry-only
outcomes. Do not substitute getter tests or the message field for damage proof.

### 26 October resume: release-blocking combat/interaction evidence and interrupted source fixes

User priority: finish physical native attack/block and interactions for the release.
B/C/H are release blockers; no playable release or native acceptance is claimed.
Existing dirty work retained; no commit, install, game launch/input, game-memory
access, backup-vault/save/config mutation or restoration claim in this resume.

The interrupted source candidate now builds and passes full Debug **101/101**
and Release **101/101**. Commands: `cmake --build build --config Debug --parallel 4`
then `ctest --test-dir build -C Debug --output-on-failure`, matching Release
pair; final logs `build/resume2-current-{debug,release}-{build,tests}.log`.
This validates the existing adjustable seated lift, immediate live menu apply /
automatic background save and mono camera-bound source-pose/FOV correction.
Headset smoothness, live body visibility and native combat are NOT established.

Extended the actual WARP/OpenXR submission seam with both-eye center-pose checks
and **13** independently mutated camera-source refusal/recovery cases: missing
sequence, epoch mismatch, old/future capture time, missing/future prediction,
focus/orientation/position invalidity, FOV boundaries, invalid aspect and a
sequence-only diagnostic tag. Each refusal preserves fresh tracking and releases
the XR frame/images; a fresh camera receipt recovers. Focused command:
`ctest --test-dir build -C Debug -R "^xr_mono_recovery$" --output-on-failure`
passes **1/1** (`build/resume2-mono-test.log`). This extends the earlier logged
red/green pose-mismatch regression, not a new in-headset performance result.
The syntax index cannot parse OpenXR calling-convention macros; real MSVC builds
are authoritative. `project check` scoped to tests incorrectly sought tests/build;
that harness error is retained, not reported as a source failure or bypassed test.

Forced `cmake --build build --config Release --target dsvr_setup -- /t:Rebuild /m:4`
before doctor (`build/resume2-setup-rebuild.log`, exit 0). Then
`pwsh -NoProfile -File tools/auto_verify.ps1` exits 0, Release **101/101**,
doctor PASS and refreshed the development ZIP; log `build/resume2-auto-verify.log`.
Final documentation/package refresh also exits 0: Release **101/101**, doctor
PASS, development ZIP 1,436,019 bytes at that refresh. Log
`build/resume2-final-auto-verify.log`. A final docs-only package refresh includes
this completion note; use its adjacent verification JSON for exact ZIP size/hash.

Native B progress: GUI DSRFull PID 17320 remains reachable through advertised
read-only tools of the installed inline MCP SDK client; no reusable shim or
Ghidra script. REA 6.8.0 now reports Windows PE analysis available, but that is
not a binding to the live primary .gpr. Corrected the false "atomic failure / no
handler" interpretation: selector flags come from target +0x524 comparison.
The type-1 accepted path reaches target virtual +0x480 then source +0x460;
PlayerIns/EnemyIns differ. Independently compared **336 bytes** of DB observations
with the pinned EXE. Native kind-2/0x40 state branches recovered but not classified
as damage/block/parry. Actor generation, record lifetime, simulation owner and
independent outcome still unknown. One interior-instruction walk is explicitly
rejected in its receipt. See docs/NATIVE_MELEE_CONTRACT.md. Exact next static
probe: successful PlayerIns +0x480 kind-2 branch from 140095D94, then +0x460
notification/context owner; do not invoke a speculative function to test it.

H1 progress: parsed the installed bounded DCX/BND3 ObjActParam declarations,
not just Paramdex defaults. **49** real rows; selected cached lever/door rows
have raw validDist **60/90/110**, not the schema default 150. Kept class-wide
physical reach disabled: native selected-row/prompt/LOS, distance origin and
object generation are still unbound. Carrier/member/schema/row hashes and raw
observations live outside distributable sources; see
`docs/NATIVE_INTERACTION_CONTRACT.md`. The bridge ObjAct function-name query
returned "No complete response ... timed out"; subsequent string collection
hit the bounded SDK client deadline. No identical query retry, DB repair or
negative claim about missing interaction code.

Observation readiness: old runner Check correctly refused changed config.cpp
(`build/resume2-combat-check.log`). Rebuilt current LAB observer. Its inherited
cache had DSVR_BUILD_TESTS=OFF: initial CTest registered **0 tests**, which is NOT
qualification. Configured the same VS2026/x64 cache with LAB ON, tests ON and
ReShade addon OFF; rebuilt and ran actual registered **99/99** Release tests,
then source-stable recheck **99/99**. Commands and logs:
`cmake -S . -B build/combat-lab -G "Visual Studio 18 2026" -A x64 -DDSVR_ENABLE_LAB=ON -DDSVR_BUILD_TESTS=ON -DDSVR_BUILD_RESHADE_ADDON=OFF`,
`cmake --build build/combat-lab --config Release --parallel 4`,
`ctest --test-dir build/combat-lab -C Release -N`, then full CTest.
Logs `build/resume2-observer-{configure,qualified-build,registered,qualified-tests}.log`
and `build/resume2-observer-recheck-{build,tests}.log`.

Created a NEW immutable offline qualification receipt (did not rewrite old ones):
`build/verification/combat-observer-20261026-resume/candidate.json`, binary hashes,
226 sealed source/build/test/tool files, 209-file pre/post equality check, actual
normal Debug/Release and LAB test-log hashes. Command
`pwsh -NoProfile -File tools/run_combat_capture.ps1 -Mode Check -CandidateReceipt build/verification/combat-observer-20261026-resume/candidate.json -Receipt build/verification/resume2-combat-ready/receipt.json`
exits 0: **CHECK PASS**, offline/disconnected Steam, exact-game block, qualified
candidate/seeds/current roots. It is read-only readiness, not a transaction or
native outcome. Log `build/resume2-combat-ready-check.log`. No Run was started.
Next native evidence is separately labelled ordinary hit/miss/block/parry and
selected-object/prompt activation under TESTING, with independent outcomes and
current ownership/restoration receipts. The no-shield/passive-enemy historical
comparison cannot supply defense acceptance. Plan and A/B/D/F/H cockpit evidence
updated together; no native gate checked complete.

### 26 October native qualification request: response observability fixed; gameplay not qualified

Requested scope is native attacks, defense and interactions on this build. These
are NOT merely missing signatures on a test receipt: combat_native still has no
production physical-damage/shield-contact binding, and physical reach still has
no native prompt/LOS/selected-object producer. Those gates remain BLOCKED.
No native/gameplay PASS, release, game launch, input injection, direct game-memory
write, vault change, receipt replacement or restoration is claimed.

Advanced the pinned PlayerIns +480 kind-2 continuation. It performs a horizontal
direction dot/Cosf comparison (observed degree multiplier and 120-degree constant)
and a later accepted path writes native state 1006/1007, +31C bit 2 and actor+404
1.0f. These are shipped-code observations, not an HP write or a block/parry label.
Pinned 125 DB bytes across five windows against the EXE. External evidence:
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\physical-combat-20261026-qualify\`.
For interactions, UTF-16 ObjActParam at 141372BE8 reaches name-list registration
in 140531BC0; no selected-target owner follows from it. Four exact debug-string
xref/RIP-LEA probes are bounded negatives, not proof that code is absent.
See NATIVE_MELEE_CONTRACT and NATIVE_INTERACTION_CONTRACT.

A concrete observation defect was reproduced at the REAL contact detour/writer:
record+54 and +168/+169 changed during the natural callback, while diagnostics
always serialized zeros. Command
`cmake --build build --config Debug --target test_combat_trace --parallel 4`, then
`ctest --test-dir build -C Debug -R "^combat_native_trace$" --output-on-failure`
failed **0/1** with the missing response-payload assertion. Logs
`build/qualify-response-red-{build.log,log}` (actual test log is
`build/qualify-response-red.log`). The Python consumer independently failed with
`raw native response fields rejected`; log
`build/qualify-response-parser-red.log`.

Source fix: retain exact raw before/after kind and status fields in the EXISTING
five-event atomic diagnostics batch, with independent read bits and payload
version 1. Coalesced pair/handle/status reads retain the authored **18-query cold
permission budget**; no scope crosses the original native callback. Extra-field
read failures preserve the previously valid actor/context fields through bounded
fallbacks. No native function binding or gameplay write was added. The strict
Python analyzer exposes raw native_response only, preserves legacy unknowns,
rejects malformed/missing-read-bit payloads and NEVER turns it into native combat
acceptance. Tests cover all **16** partial-read masks, full unsigned 32-bit values,
failed optional reads, identity/capture transitions and whole-batch drops.
Focused **3/3 PASS**: combat_native_trace, combat_native_trace_filtered and vr_trace
(`build/qualify-response-partial.log`). This is synthetic seam evidence.

Current full verification, observed this turn:
- `cmake --build build --config Debug --parallel 4`, then
  `ctest --test-dir build -C Debug --output-on-failure`: **101/101 PASS**;
  `build/qualify-final-debug-{build,tests}.log`.
- Matching Release pair: **101/101 PASS**;
  `build/qualify-final-release-{build,tests}.log`.
- Current LAB observer Release build + full CTest: **99/99 PASS**;
  `build/qualify-observer-{build,tests}.log`.
- `pwsh -NoProfile -File tools/auto_verify.ps1`: exit 0, Release **101/101**,
  doctor PASS; development ZIP only (`build/qualify-auto-verify.log`).
No shared struct/header changed in this turn (only vr_trace.cpp, vr_trace.py and
their two tests), so no new setup-layout rebuild dependency was introduced.
`project check` built successfully but its unconfigured CTest invocation lacked
-C: **101 NOT RUN**, a harness failure, not 101 executed failing assertions.
The configured runs above are the actual results; no threshold was relaxed.

Fresh NEW immutable records:
`build/verification/native-qualification-20261026-response/candidate.json` seals
current source/binaries/test-log hashes. Adjacent qualification.json explicitly
reports attacks, blocking, parrying and targeted interactions **BLOCKED**, with
zero live observations. Old receipts remain unchanged. Command
`pwsh -NoProfile -File tools/run_combat_capture.ps1 -Mode Check -CandidateReceipt build/verification/native-qualification-20261026-response/candidate.json -Receipt build/verification/native-qualification-20261026-response-run/receipt.json`
exits 0; `build/qualify-native-check.log` says CHECK PASS, no changes yet.

Native Run was not started: runner lines 724-725 create data inside the backup
vault, and SaveReceipt replaces its active verification receipt. Both contradict
the current hard limits. Actual mod/gameplay qualification also needs the native
hooks/callbacks boundary and operator-provided gameplay (no input injection).
Resolve that boundary with a fresh transaction OUTSIDE the vault and append-only
receipts, then obtain labelled ordinary hit/miss/block/parry and prompt/activation
observations before implementing/qualifying the missing production bindings.
The plan ledger and A/B/C/H lane files were updated together; no checkbox closed.

### PowerShell 7 development tooling, 26 October 2026

All active PowerShell scripts are development/operator tools, not the product installer. Added `#requires -Version 7.0` to `tools/auto_verify.ps1`, `tools/verify_preflight.ps1` and `tools/run_combat_capture.ps1`; modernized the capture runner hashing/path handling and preflight list construction. CTest now requires and invokes `pwsh` on Windows and no longer registers a Windows PowerShell 5.1 path. `docs/TESTING.md` records the boundary. Product installer remains independent and portable.

Verification: PowerShell 7.6.5 parser PASS for all three scripts; `pwsh -NoProfile -File tools/run_combat_capture.ps1 -Mode SelfTest -WindowMinutes 30` exit 0. Debug build exit 0; full Debug CTest **99/100**, with unrelated `combat_native_trace_filtered` failing at `unrelated_signatures==0` (test #9). Release build exit 0; full Release CTest **100/100**. `ctest --test-dir build -C Release -N` reports 100 registered tests and only the `pwsh` capture transaction entry.

Next exact probe for the unrelated Debug-only red test: `ctest --test-dir build -C Debug -R '^combat_native_trace_filtered$' --output-on-failure`; if reproducible, inspect the modified fixture around `tests/test_combat_trace.cpp:159`. No installer/game/save transaction was run.

### 26 October operator combat/interaction capture and automatic ten-minute observer

Task: advance B/C/H using protected ordinary-controls comparisons; eliminate F9
capture burden and investigate the operator's contact hitch. HEAD remains
3c26ed95de5dd669925d704e6a7d01d3ad7ddb8c. Existing dirty work preserved, no
staging/commit/reset/delegation. The parallel user-owned PowerShell chat's PS7
changes are retained. Its removal of the PS5.1 CTest explains current 100/98
counts; no gameplay assertion/threshold was relaxed.

Source changes in this slice: vr_trace.cpp, diagnostics.cpp/core.cpp,
run_combat_capture.ps1, test_combat_trace/test_diagnostics_mode,
vr_trace.py/test_vr_trace.py. Cold synthetic permission-query budget tightened
18 -> 10 (red then green). Permissions are reused only inside one pre/post
snapshot; every scope ends before the natural original call. Positive local
PlayerIns self contacts are counted separately and excluded from detailed actor
reads, with original dispatch preserved. Short captures retain the 256 ceiling;
long captures stream details to the bounded 4096-event queue. Footer counters
explicitly account total = non_player + self + refused + detail. Analyzer validates
that equation and bounds without promoting raw observations to native acceptance.

The combat-only diagnostic thread now waits for the verified local player, then
starts exactly one automatic 600000-ms capture per process. F9 is unnecessary
and cannot restart this recorder. Title/bootstrap time does not consume it.
Tests first failed on long duration and absent automatic start; now verify loaded
player admission, one attempt, exact duration and no automatic restart. No XR,
input injection, physical damage binding or direct gameplay write was enabled.

A new LAB-only natural 140313470 interaction observer forwards original arguments
once, preserves all return bits, and captures selected group/row/PARAM/handle/state
before and after. Identity certification is callback-local only. Strict parser,
changed-handle and combat-domain regressions pass; the domain regression was red
until the new event was admitted. See NATIVE_INTERACTION_CONTRACT for pinned
ObjAct owner chain, raw payload and native raw * float32(0.01) * 2 range helper.
External native evidence is C:\Tools\DarkSoulsRemaster_De\combat-evidence\native-combat-20261026-finish.
No decompiled source/game data entered distributable sources or primary DB edits.

Protected transaction repairs: the first staging attempt failed inventory before
launch; restoration succeeded. Replaced provider Move-Item root staging with
atomic Directory.Move plus held inventory/source-absence/empty-root checks; later
staging succeeded. This does not establish the provider failure's exact cause.
The first automatic retry saw transient overlapping Steam bootstrap processes;
strict steady-state ownership refused it. Both exited and explicit Restore
succeeded. Startup now requires one exact PID/start-time/path generation stable
for 1000 ms, within a 90-second/deadline bound. Steady-state multiple/foreign
process refusal remains. SelfTest covers overlapping/empty/changing/stable startup
and pre-existing-generation rejection. Fresh append-only journals remain outside
original backup vaults; old receipts/candidates were not overwritten.

Native run 1: build/verification/native-finish-20261026-live-retry1/receipt.json,
PID30784, RESTORED and independently hash-checked. Three intact captures:
- 270943343-1: mixed wall then enemy hilt hit, not two clean F9 cases. 146 detailed
  contacts, zero loss/cap; seq24 stable EnemyIns HP44->31, weapon212000,
  Atk2301000/table1, behavior102301000, kind2, statuses0/0.
- 271445750-2: operator failed parry/hit, 256 details and 32 refused. Six incoming
  contacts include HP557->546->524->502->491->480->458. No successful parry proof.
- 271480312-3: operator successful block then other contact, 256 details and643
  refused. First incoming target HP403->403/stamina84->76; later HP403->369.
  The entire capture is not a clean block case. Both defense traces hit the old
  detail ceiling. Operator reported 1-2-second hitches. Raw captures, reports and
  operator notes are retained beside the receipt.

Native automatic run: build/verification/native-finish-20261026-continuous-run2/receipt.json,
PID48220, candidate ../native-finish-20261026-continuous/candidate-settled.json
(SHA256 55861ecd97df9cc39d7d79884ced3756a0b0aebe9c1185c511d80265a9ae6fda).
LAB DLL cafbca0862bb54c4cba9113f7498fdc79a8ae86d105a6ebd32c1b771b242bc87.
Command: pwsh -NoProfile -File tools/run_combat_capture.ps1 -Mode Run
-WindowMinutes 15 -CandidateReceipt build/verification/native-finish-20261026-continuous/candidate-settled.json
-Receipt build/verification/native-finish-20261026-continuous-run2/receipt.json
-SeedVault build/native-transactions/combat-observer-20261026-133218-f14e751b069b4f858471133e38547fc1.
Automatic capture ran 14:06:02.798 through14:16:02.842 New York time, before the
14:20:47 deadline. Operator returned normally to title; finish marker at14:16:32;
runner exit0, journal revision19 state RESTORED. Independent check confirms both
original roots (five files each) and three install/config hashes exactly equal.
Evidence: independent-restoration-check.json, native-finish-auto2-live.log.
No game remains running; Steam left Offline. Disposable progress is preserved
in the transaction test roots, not merged into personal saves.

DSVR-trace-48220-272695343-1.jsonl: 19,515,758 bytes, SHA256
ff5856750948eac9e120f081b18bf39e9da251c6f0ebb82336d96a9b77eb7fe6.
78023 events, zero dropped/contended, queue_peak105. 4681 callbacks =270 nonplayer
+3141 self +0 refused +1270 detailed; no detail cutoff. Capture and combat-integrity
PASS; global VR verdict remains incomplete/UNVERIFIED, not a truncated capture.
Operator: two swings missed enemy but HIT WALLS; block; two successful parries
each followed by a separate ordinary non-riposte attack; then kill. Seq442/513
are stable player->enemy type64/Atk4800450/shield1452000 with status[0,0]->[1,1],
no HP/stamina change. Following ordinary Atk2301000/weapon202000 contacts at460/514
reduce enemy HP69->12->0. Incoming contacts319/320/321 keep player HP659 while
stamina changes -24/-8/-8. These are correlated mixed-sequence observations,
not independently timed clean cases or proof that status bytes exclusively mean
parry. NPC contact554 has HP1->0 and statuses[0,0]->[0,1].

Operator also tried two locked doors, killed NPC, picked up its item and opened a
keyed door, reporting expected behavior without noticeable FPS damage for those
actions. One natural ObjAct callback at QPC2731792790240, thread43532, selected
index27, actor handle268713984, object handle553648238, state0->3, return1,
raw validDist60, flags15. This correlates with the sequence; it does not cover
locked-door refusal, pickup ownership or physical reach/LOS for all classes.

Performance remains FAIL/unresolved: operator still felt the hitch, including on
a jump. Detailed read/assembly ELAPSED wall time median2.4618ms,p99 6.1289,max9.6132;
native median.0042ms,p99.0866,max.2283. Smaller query budget did NOT solve it.
Do not call QPC intervals pure CPU work: descheduling remains possible. Most
rejected actor reads are unqualified types (1142), with11 stat-range failures.
Raw Step samples are tiny dispatch calls, not full-frame/simulation timing.
Next discriminating probe is CPU stacks or a labelled guarded-read-disabled
comparison, not another speculative cache across native callbacks.

Current exact-build checks all exit0:
cmake --build build --config Debug --parallel4; configured full CTest100/100
(build/native-finish-auto2-debug-{build,tests}.log); matching Release100/100
(native-finish-auto2-release-*); LAB Release98/98(native-finish-auto2-lab-*).
ctest -N owns these current counts. New 228-entry source manifest equal before/
after those builds. Runner SelfTest PASS(native-finish-settle-selftest.log).
pwsh -NoProfile -File tools/auto_verify.ps1 exit0, Release100/100 and doctorPASS,
refreshes DEVELOPMENT latest ZIP only(native-finish-auto-verify.log).
No formal release, upload or native physical-combat acceptance. B/C/H remain OPEN:
production physical_damage_bound/shield_contact_bound still false, native context
owner/generation/simulation phase and qualified target/geometry/LOS unresolved.
Continue from correlated status setters and verified creation wrappers, with the
saved exact raw traces available; do not repeat manual F9 captures.
