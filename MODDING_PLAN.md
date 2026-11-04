# DSVR native conversion — cockpit orchestration plan

Date: 2026-10-21 as the superpowers execution guide; converted to this cockpit
plan on 2026-10-29 (the superpowers tree was retired to `docs/history/` and
deleted from the active tree). On 29 October the research index below was
merged in, so this single file is the orchestration/cockpit plan: it owns the
shared protocol, the completion ledger and the evidence index. The lane files
under `.empryo/cockpit/lanes/` own the gate checkboxes and are the source of
truth for gate state. Status: the only active execution plan, not
implementation or acceptance evidence.

Documentation authority: README.md owns scope and authority; this file plus
the lane files own task order and completion tracking; ENGINEERING.md owns
architecture; TESTING.md owns operating policy; VERIFICATION_RUNBOOK.md owns
acceptance procedures and numerical thresholds; MODLOG.md owns the current
handoff. Direct user instructions take precedence. History, research notes and
handoffs do not override those owners. No superseded plan/specification adds a
parallel sequence or approval gate. Do not create another competing roadmap.

## Current execution status (3 Nov 2026)

Latest operator feedback is implemented in the calibration room, loading
transition and neutral-hand lifecycle. The shared Ashen palette is now applied
to Electron, native Setup, and the in-game menu. The Release core and Setup
targets plus the Electron portable build were rebuilt; the current development
package is installed and Setup Doctor passes. The last full Debug/Release CTest
run was 116/116 before this color-only pass; tests were not rerun afterward.
Receipt: `build/verification/ashen-theme-20261101-154409/`. The game remains
closed.

OpenXR testing now uses Elliott Tate's unmodified upstream simulator at
`692a66de570189b29c684f48c6ef306a07d5764f`, with a project MCP server and
process-scoped runtime. The simulator's 23 noninteractive tests pass; 17 UI
fixtures record successful ray interactions, but all UI probe processes still
fail teardown in AMD `amdxc64.dll`/`amdxx64.dll` (`0xc0000409`). This is an
open upstream/driver verification defect, not a passing suite. DSVR Release
CTest passes 124/124 with Doctor PASS; Debug CTest passes 124/124. A live
DSVR HMD lab host under the upstream runtime submitted 7,347 frames, exercised
head movement, both trigger/squeeze/primary/secondary channels, right-stick
click and both controller pose pipelines; it reported missing left-stick-click
and right-menu channels. The host is a synthetic integration fixture, not a
protected DSR gameplay run. None of this qualifies headset optics, comfort,
natural arm alignment or physical melee. Native combat/campaign gates and
release prerequisites remain open.

## 1. Assignment and authority

Complete Dark Souls Remastered's native VR conversion in the existing
C++20/D3D11/OpenXR mod. Deliver independent tracked 6DoF viewing, true stereo,
native avatar embodiment, physical melee and defense, collision-aware
roomscale movement, usable VR interactions/UI, ranged aiming, lifecycle
recovery and campaign compatibility. Native conversion is the user's fixed
direction.

Keep DSR's world, AI, progression, equipment, damage rules, stamina, poise,
collision and actor lifecycle authoritative. Replace the local player's
interfaces to those systems. Do not substitute gesture-to-attack buttons,
direct HP subtraction, a duplicate avatar, synthetic enemies, mono copied into
two eyes, or an engine remake for the requested result.

Unknown native contracts are research tasks, not implementation details to
invent. No fixed completion date is defensible until the native combat and
stereo gates pass.

## 2. Starting facts and things to recheck

- Production true stereo and native physical damage/block/parry are not
  accepted. XR eye scheduling APIs and projection math do not establish
  native eye rendering.
- The diagnostic blade length and target proxies are not production weapon
  geometry or enemy collision evidence.
- Head/Controller locomotion selection already has implementation and recent
  commits. Revalidate it; do not reimplement from an older baseline table.
- Never copy an old test count. `ctest -N` owns registered names; the ladder
  in section 6 owns current counts. Historical FPS observations (15/35/53)
  are not current measurements.
- The checkout contains valuable unfinished work: never reset it or stage
  everything indiscriminately.
- Recon fingerprint (verified from this repo, 3 Nov 2026): install
  `C:\Program Files (x86)\Steam\steamapps\common\DARK SOULS REMASTERED`
  (Steam App 1.03.1 / build 10943698; exe SHA-256
  `a45aaa36…f98d7b` pinned in `tools/run_combat_capture.ps1:1582`).
  Native 64-bit FromSoftware build; DSVR route is its own `dinput8.dll`
  forwarder + MinHook + D3D11/OpenXR core (`src/loader/loader.cpp`
  `chain_proxy`), not the community DSR Mod Engine fork and not a UXM
  unpack. Verdict is offline-only: Steam Offline preference + logged-off
  state + exact-exe outbound firewall block + disposable seeds, enforced
  read-only by the capture Check; no anti-cheat clearance is claimed.
  Saves live under `Documents\NBGI\DARK SOULS REMASTERED` and
  `%APPDATA%\DarkSoulsRemastered` (Steam userdata `570940`), config is
  the game-dir `DSVR.toml`, logs are `DSVR.log` / `DSVR-trace-*.jsonl`.
  Lab work runs only inside receipted reversible transactions
  (`build\native-transactions`, `.dsvr-backups` vaults, exact-PID
  ownership). Unknowns stay in sections 4, 5 and 7.

## 3. Mandatory working loop

Use one narrowly scoped task per implementation turn: one observed failure,
one contract question, or one integration boundary — not an entire milestone.

1. Read MODLOG and the lane files' next unmet gate. Inspect git status/diff.
2. Write the task objective, prerequisite evidence, expected observation and
   rollback boundary in the active task record (format below).
3. Discover code with the harness graph tools (`find`, `grep`, `navigate`,
   `genome_query`, `code_script`); confirm a returned snippet is the named
   function before trusting it — an index refresh can retain old line ranges.
   Use grep for docs, logs, config literals.
4. Read the actual caller and consumer before changing a boundary. Identify
   thread, lifetime, coordinate space, units and update frequency.
5. For a defect or native adapter, add a meaningful regression at the actual
   integration boundary. A helper test alone cannot prove the adapter uses it
   correctly.
6. Implement the smallest complete slice. A new module is justified by a
   distinct owner/lifetime, not by a desire to reorganize.
7. Run the verification ladder (section 6). Record exact command, exit code
   and evidence paths. Write observed outcomes and counts into the lane
   frontmatter (dbg_build/dbg_test/rel_build/rel_test/verify/doctor/counts).
8. Exercise native behavior where required using the protected procedure.
   Package hashes must match the candidate under observation.
9. Update the existing evidence owner and MODLOG with result and unresolved
   fact. Replace section 5's entire "Next task" subsection in place with the
   single successor task; never append or retain old next-step blocks there.
   Commit only task-owned files when explicitly
   authorized; stage explicit paths, never all changes blindly.

Task record format (in MODLOG or a run's results.md):

```text
Task ID / objective:
Baseline commit + dirty files:
Prerequisites and evidence:
Hypothesis / exact missing fact:
Files and symbols touched:
Experiment / expected discriminating observation:
Result: source | static | native | headset
Commands, exit codes, candidate hashes, evidence paths:
Gate: PASS | FAIL | UNVERIFIED | BLOCKED
Remaining uncertainty:
Next exact action / alternative if it fails:
```

Never infer PASS from missing errors. BLOCKED means a named prerequisite is
unavailable, not that the feature is finished. Do not accept self-generated
expected values as independent proof, and do not mark a gate closed merely
because code exists. Before context rollover, record command completion,
active processes, install/save restoration state, and exact next probe.

## 4. Native investigation protocol

Each new native call or write needs: pinned executable identity and
instruction bytes; calling convention and complete arguments; receiver/type;
field meanings and bounds; constructor/destructor or owner; pointer lifetime
and actor generation; simulation thread/update phase; side effects; refusal
behavior; independent runtime correlation.

Recover split continuations from instructions and callsites, not truncated
pseudocode; check pinned bytes and PE unwind bounds before trusting an ABI or
lifetime. Health setter 0x140322910 is an observation anchor, never an attack
API. AI AddTopGoal/SetAttackRequest and generic event queues are not proven
player melee dispatchers; keep tracing `CreateDamage_NoCollision` and
`OnCharacterHP_CheckAttacker` until a concrete HP write is supported.

After three materially identical failures, record the failure signature and
change the evidence source, probe or hypothesis. Do not suppress assertions,
widen guessed pointer scans, invent offsets, or report an unavailable binding
as implemented.

## 5. Milestone order and current steering

Current steering (29 October; operator direction: full immersion first,
true stereo last). This order supersedes the earlier D1/D2-first sequence.
Stereo remains required for completion; deferral is not acceptance of mono.

### Next task

Protected DSVR integration using the official OpenXR Simulator/MCP route on a freshly qualified candidate.

- Complete the exact upstream simulator/MCP setup and keep its checkout unmodified. The current 23 unit tests pass; live UI tests perform their MCP actions, but the AMD teardown failure (`0xc0000409`) still blocks the full CTest PASS. Preserve it as an open verification defect, not a waived gate.
- Requalify the current source tree and seal its actual Release binaries, then run the read-only `run_combat_capture.ps1 -Mode Check -InputMode OpenXRSimulator` before any DSR launch. Keep per-process `XR_RUNTIME_JSON`, isolated `SIMXR_DATA_DIR`, Steam offline/exact-game firewall guards, disposable save roots and restoration receipts. Do not use the retired custom `set_controller_state` fork or claim unsupported native controller coverage.
- In a bounded protected DSR run, use the upstream MCP's documented screenshots, head pose, controller-ray hover/click/hold/scroll and frame diagnostics. Require DSVR load/submissions, a complete trace/footer, inspected eye captures and hash-verified restoration. All emulator actions and outcomes must be recorded from MCP/trace evidence.
- Label results synthetic. They cannot establish natural physical contact, damage attribution, comfort or headset optics. Keep B1 and all physical combat gates open, then continue B's native contact/shape-owner investigation from source and trace evidence.

### Path to full conversion

| Order | Existing lanes / work | Evidence needed before advancing the integration |
|---|---|---|
| 1 | A, then F2/C2: tracked view and committed body/weapon foundation | Current protected baseline; applied-camera identity; verified final palette/attachment order. C2 publication can advance independently, but C damage submission still requires B. |
| 2 | B/C plus H essentials: one real physical sword and controller-only progression | Complete native hit/miss/block/parry/dodge comparisons, legitimate context/geometry/thread/lifetime, confirmed outcomes; ten hits/misses and rejection cases. Native menus, calibration/recenter, equipment and one interaction class usable without keyboard. |
| 3 | E and remaining F: collision-aware presence and embodiment | Attributed physical follow, HMD-pivot turns, obstruction protection, support/clearance queries, floor-anchored crouch and hurt volumes; independent rendered armor/body fit. No guessed motor/collider writes. |
| 4 | G and remaining H: one immersive encounter | Native shield/parry/two-hand rules, outcome-driven haptics, comfort protection and tracked interactions/HUD; integrated encounter qualified in the diagnostic mono route only. |
| 5 | I, then J non-stereo cases: breadth and campaign robustness | Representative equipment families, ranged/casting, handedness, scripted actions, traversal, death/load/focus recovery and progression matrix. Record mono-specific limitations; headset comfort and stereo rendering cases remain open. |
| 6 | D1-D7: true stereo, final conversion feature | Resume preserved D1a-D2b ownership/feasibility gates; only then independent asymmetric eyes from one simulation state, distinct resources/history and complete-pair XR ownership. Prove disparity/occlusion/eye order and full-scene cost. |
| 7 | G/H/I/J stereo integration recheck, then K final acceptance | Repeat view-dependent UI/body/combat/campaign cases on stereo; exact packaged candidate, both runtime routes, 90 Hz budget, operator comfort, lifecycle/install/recovery and support matrix. |

“Stereo last” means last feature implementation, not skipping final stereo
integration, performance qualification or release checks afterward. Profile
mono/observer costs throughout; do not postpone existing hitch isolation to K.
Pre-stereo G/I/J work may advance individual cases, but cannot close a gate
whose acceptance explicitly requires true eye composition or headset evidence.
No lane checkbox is closed by this prioritization.

Preserved constraints:

- B/C/H remain release blockers. Explicit ordinary controls are a fallback
  during diagnosis, not physical-melee acceptance; never replace physical
  combat with gesture-triggered canned attacks or direct HP changes.
- Section 7 capture integrity and observer-hitch isolation remain measurement
  prerequisites. Finish missing comparison windows, not another capture system.
- Run 7's source/body palette alias and asm.root offset remain refusal gates.
  Verify the real native consumer; do not enlarge the solver to hide them.
- True stereo work is deferred, including feasibility experiments. Preserve
  docs/NATIVE_STEREO_CONTRACT.md and lane D's evidence; static edges never
  authorize guessing an ABI. No dispatcher replay or cosmetic mono-as-stereo.
- Keep C++, Win32 Setup and the in-game ImGui menu. No FLTK revival, engine
  rewrite, texture replacement work or unrelated UI architecture changes.
- Execute sequentially; delegate only when explicitly requested. No game-memory
  writes, input injection, commits, pushes, publication or backup-vault changes.

## 6. Commands and test discipline

Run from the repository root. The checked-in verifier uses Visual Studio
18 2026/x64; do not replace it.

```powershell
git status --short
git diff --stat
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug -N
ctest --test-dir build -C Debug --parallel --output-on-failure
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --parallel --output-on-failure
pwsh -File tools/auto_verify.ps1
```

Bare `--parallel` uses all cores; parallel ctest is safe (temp-output tests
are PID-unique). Report pass counts for BOTH configs separately. A test count
you did not run is not a result. After any shared-header struct change, force
a full Rebuild before trusting `DSVR-Setup --doctor` — MSBuild leaves stale
Release objects and the doctor access-violates on them.

For focused runs, inspect `ctest -N` and choose registered names with `-R`.
Register a new necessary fixture in tests/CMakeLists.txt. Run meaningful
rate-variation, duplicate-callback, stale-data, actor-reuse and lifecycle
cases at mutation boundaries.

Before protected deployment, inspect and follow current
tools/verify_preflight.ps1, TESTING.md and restoration receipts. Keep Steam
offline and existing exact-executable outbound protection. Preserve original
files, both save roots and Steam metadata. Restore and hash-verify after each
transaction, including failure/crash. Never claim restoration for state that
was not snapshotted. Never rewrite or delete receipts under
build/verification, and never touch the backup vault.

## 7. 26 October focused work package: capture, comparisons, hitch, bindings

Step 1 (autonomous VR session capture) is implemented and green
(autonomous_capture, session index/report, trace_sink seams; registered
tests pass). Step 2 (clean ordinary-control comparisons) is PARTIAL: Run 2
27 Oct is footer-valid (105,414 events, 0 drops, 6,891 paired) with
hit/wall/block/elig windows showing bound HP deltas; occl/repeat/retarget/
loadlife unobserved; B1 stays open. Step 3 (observer hitch isolation) is
open — perf FAIL on the retained session. Step 4 (implement only contracts
supported by those results) is BLOCKED behind steps 2-3 and the native
owners. See lane B/H evidence and MODLOG for the exact case windows.

## 8. Completion ledger

Maintain here as gates change. Lane frontmatter carries the live counts.
Do not mark a row complete merely because code was added.

| Gate | Status | Required evidence |
|---|---|---|
| A baseline | 31 Oct offline ladder PASS: Debug build exit 0 and Debug 114/114; Release `--target ALL_BUILD` exit 0 and Release 114/114; `tools/auto_verify.ps1 -SkipDoctor` Release 114/114, dist ZIP 1,717,742 bytes, packaged core `5b8803a9…4a00` — byte-identical to the `DSVR.dll` copied into the game folder this turn, so installed and packaged core now match (they differed before: installed B493BDCE… vs dist 9ce2a7ef…). Doctor was NOT re-observed this turn (`-SkipDoctor`; the JSON `doctor` field carries an older value). Operator's own launch reported the game running properly, which retires the black-screen report as operator-observed, not agent-verified. Latest full Debug/Release builds PASS; CTest 115/115 in each. Focused arm_pose_native PASS 1/1 in both configs with the same-position left-wrist regression. auto_verify -SkipPackage -NoLatest Release CTest 115/115 + Doctor PASS; package/latest refresh skipped. No current-candidate native/headset qualification by the agent. 31 Oct later session: Debug build exit 0 with `ctest -C Debug` 115/115, `tools/auto_verify.ps1` (doctor NOT skipped) Release 115/115 + doctor PASS + dist ZIP 1,744,211 bytes, then `dist\DSVR-latest\DSVR-Setup.exe --install --game "…DARK SOULS REMASTERED" --yes` with the game closed (6 saves backed up to `.dsvr-backups\saves-20261030-133148`), then `tools/verify_preflight.ps1` → `OK=13 WARN=3 BLOCKED=0`, `HEADSET RUN: READY TO START THE RUNBOOK`. The stale-install blocker is closed; no launch, no input injection, no live memory access, no gate closed. Presentation/aspect/progress session: Debug build clean with `ctest -C Debug` 115/115, Release build clean with `ctest -C Release` 115/115, `tools/auto_verify.ps1` Release 115/115 + doctor PASS + dist ZIP 1,746,265 bytes, install with the game closed (6 saves to `.dsvr-backups\saves-20261030-151449`), installed core SHA-256 `BC8475AAC2492D5037E0F347CDB3817D51FFE0C7F4CE8281A7594E58E101A0C2` equal to `build\src\core\Release\DSVR.dll`, `tools/verify_preflight.ps1` → `OK=13 WARN=3 BLOCKED=0`. Three root causes behind the operator's black-screen / too-wide-panel / calibration-stuck report are fixed at the source with tests: calibration vs canvas presentation policy split in `menu_state.h`, source-aspect theater extents plus the tested `stereo_math.h: TheaterRectForEye`, and sticky calibration progress with a drift-following stillness baseline. No headset qualification of any kind. 31 Oct Electron phase 1 (offline): `--json status/doctor/verify/settings-index` seam landed in `DSVR-Setup.exe` (new `src/setup/json.{h,cpp}`, additive dispatch, `gui.cpp` untouched), CTest 116/116 both configs, `auto_verify -SkipPackage -NoLatest` exit 0 + Doctor PASS, live smoke of all four verbs + `unknown_verb` error path PASS; not packaged/installed yet. 31 Oct Electron phases 2+3 (offline, dev-only): `launcher/` Electron 45.2.1 skeleton (engine.mjs verb allow-list, SHA-256 vs `DSVR-SHA256.txt`, `--smoke` exit 0) and phase 3 mutating verbs `config-get/config-set/launch/launch-native` with atomic all-or-nothing refusal proven live against an isolated `--game` fixture (bad key and out-of-range batch refused, disk untouched); launch extracted from `gui.cpp` into shared `src/setup/launch.{h,cpp}`; ladder re-green: Debug build 0 + 116/116, Release build 0 + 116/116, `auto_verify -SkipPackage -NoLatest` exit 0 + Doctor PASS, `npm run smoke` exit 0. Nothing packaged/installed/committed. | Exact-package protected native observation and rendered body/attachment acceptance; use section 5 current task. |
| B melee contract | OPEN: Run 2 footer-valid comparison remains the broad baseline. 29 Oct raw trace has one same-key target HP setter (44→31, request 31) nested in a natural contact; caller 0x140322a9d resolves through FUN_140322a98 / FUN_142e191ae, but the trace has no footer and its natural_hit interval ended 0.9695959 s before the header; the setter event is 18.1740426 s outside that window. EDI source traced to incoming EDX through FUN_142cb5171; five static thunk callers include the ChrManipulator method FUN_14038dae0 event ID 1000/subtype 2 path and a 0x1f58 state sync. 31 Oct: `FUN_14038dae0` itself reports caller_count 0 and only three DATA refs — `141d51368` (its PE unwind entry, not a dispatch site), `14178f264` (first slot of `IPToStateMapEntry_ARRAY_14178f264`, typed `IPToStateMapEntry[221]`, pointed at by `DAT_14132e350`) and `14132e388` in the same block — and no xref reaches that block, so the producer and update/thread owner are not statically resolvable in this database; that probe is closed as a negative result. Not physical-sword attribution or submission proof. First-test preflight 31 Oct: `pwsh -NoProfile -File tools/run_combat_capture.ps1 -Mode Check -Receipt build\verification\physical-combat-first-test-20261031\receipt.json -WindowMinutes 30` failed `Steam Offline preference is not set.` No transaction or game launch occurred. On operator report Steam Offline, a second read-only Check advanced past the Steam guard and failed `Source changed since qualified build: src\common\config.cpp`; the candidate is stale and must be requalified before another Check. Current requalification Debug build failed exit 1 in the other tab's claimed `src/setup/gui.cpp` / `tests/test_setup_theme.cpp`; no CTest/Release/verifier/doctor run, no candidate package. 30 Oct offline ladder PASS: Debug build exit 0 (job-116) with `ctest -C Debug` 115/115 (job-117), Release build exit 0 (job-118) with `ctest -C Release` 115/115 (job-119), `tools/auto_verify.ps1` exit 0 (job-120) Release 115/115 + Doctor PASS + dist ZIP 1,751,192 bytes. A third read-only capture Check still refuses `Source changed since qualified build: src\common\config.cpp`: the only candidate receipt on disk is the 24 Oct `build\verification\combat-observability-20261024\candidate-final-receipt.json`, `auto_verify` does not re-seal it and no re-qualification tool exists in the repo, so labeled capture needs an operator-approved new qualification, not another build. Static pass closed the record-manager chain: `DAT_141c7a050` attack/hit record manager, `FUN_1403c8dd0` per-frame list update, `FUN_1403c6030` per-record update (`+0x08` owner, `+0x40` source entity id, `+0x120` float timer, `+0x124` grace-frame counter that must be < 0 to process), `FUN_1402c4870` a third id space resolved by `id>>28` across four managers, driven from the local-player step `FUN_14024f6b0`, which has only DATA xrefs (`141d3e504` unwind, `141758528`, `141a294b8`) and no code caller — the top-level simulation step is table-resident and not statically nameable. No launch, no memory write, no gate closed. | Producer/owner probe for `FUN_14038dae0` closed NEGATIVE 31 Oct (bridge up, `check_connection` ok); remaining static handle is the 221-entry IPoint->state map at `14178f264` (match a state id to the `caseD_*` selector range 0..0x93), and the probe that gates B3 is the shape owners at `frame+0x130` / `frame+0x138`; correlate a closed, labeled native hit with tracked weapon/context after runs resume and candidate preflight passes. No submission or collision seam is established; no game run now. |
| C physical sword | BLOCKED: physical_damage_bound / shield_contact_bound false. Whole-blade continuous source query fixes reproduced shaft/arc gaps in classification and slot qualification; Debug/Release 113/113, code-only verifier 113/113. No native submission or outcomes. | Verified geometry/volumes, solid interception, context lifetime/thread, qualified submission, ten hits/misses; specified wall drag/impact effects and costs. |
| D stereo | DEFERRED: last feature after non-stereo immersion; no gate closed. D1b edges + counter retirement recovered statically; pump edge + worker error paths byte-pinned; 0x141A18B64 is .rdata; runnable edge named statically. D2 UNVERIFIED; production absent. Mono source checkpoint retained. | Remaining indirect edges (pump thread owner, derived dispatcher vftable), D1c ownership table, D1d natural trace, then the single D2 transaction. |
| E locomotion | Orientation selection finished (Head/Controller, offline receipts); analog intermediate directions survive B tap/release at the input seam. 31 Oct: right-stick-dead root-caused to a live config with both snap_turn and smooth_turn false and repaired at the ConfigLoad seam (config test covers both-off repair and snap-only left alone); Debug 114/114, Release 114/114. Left-stick model facing is native locomotion facing, not a DSVR write (root/physics rows are never written). Follow and native omnidirectional dodge unqualified; motor attribution UNKNOWN. E1 CLOSED 30 Oct: all six named behaviors revalidated through the real seam, and the one gap (returning past the near-vertical band follows the NEW heading with no neutral stick) is now covered by `MovementVerticalHysteresis` in `tests/test_input.cpp:742-810`, driven through `InputPollFrame`. That case was falsified before acceptance — same assertion with the blade at planar 0.35 instead of 0.2504 fails with the heading refreshed, so the 0.2 enter / 0.3 exit boundary is load-bearing. Offline ladder for that turn: Debug build exit 0 (job-121) with `ctest -C Debug` 115/115 (job-122), Release `ALL_BUILD` exit 0 (job-123) with `ctest -C Release` 115/115 (job-124), `tools/auto_verify.ps1 -SkipPackage -NoLatest` exit 0 (job-125) with Doctor PASS. Threshold VALUES are still unqualified on hardware (E6). E2 probe pass 6 (30 Oct): dispatch tables typed — the 8-slot views are interior of one PadDevice vftable run starting `1414ddc68` (`DLUID::PadDevice<DLKR::DLSingleThreadingPolicy>::vftable`); ctor `FUN_140e263c0` zero-inits the stick-float block `+0x20c..+0x27c` and a four-pad factory at `140e1bc50..140e1be3d` (statically anonymous) constructs the devices; "computed dispatch, no static edge" retired. Poller slot `+0x228` still has no found call site, so motor attribution stays UNKNOWN. E2 probe pass 7 (31 Oct): slot offsets corrected by `read_memory` (axis getter run+`0x230`, indexed bit-test `+0x238`, indexed setter `+0x240`); first real device-consumer edge found — the `14045xxxx` cluster calls poller shape `(this, controller_id, 0, 0)` and getter shape `(this, idx, flag, &out)` on the device at `[owner+0x90]+0x20` (nine classified sites); its pump `FUN_14045f820` is table-resident (zero code callers, same not-statically-nameable shape as `FUN_14024f6b0`); poller decompile pins raw→normalized units (`XInputGetState` thumbs `/65535*2` → `+0x26c..+0x27c`, triggers `/255`); masked searches show zero direct readers of the stick fields program-wide, so the vtable getter is the only consumer route; four earlier `1403233xxx` "scaling consumer" hits retracted as CFG guard-island fragments. Object identity of the consumer device is shape-inferred, not vptr-proven; motor attribution stays UNKNOWN. E2 probe pass 8 (31 Oct): XInputGetState inventory CLOSED (6 callers: PadDevice poll run+`0x228`, second full poller run+`0xd8` writing mirror block `+0x20c..+0x230` with DirectInput calibration and heading cos/sin pair, two factory connection probes, two remap-layer sites — no poller outside the DLUID library and remap layer); per-frame mapper `FUN_14045a640` decoded (settings singleton `DAT_141c6d598` direction/dash command IDs `+0x290..+0x2dc`, five command-slot binds, `FUN_14045e690` firestick ring tables — 8-way analog ring commands not raw vectors — and `FUN_14045e290` analog-speed contract `powf(10.0,sens)` + `1920/screenW` scale into view entry `+0x30..+0x4f`, active view `[[[holder+0x80]+0x80]+0x370]+0x28`); run+`0x218`/`+0x220` typed no-op/enum-command so the consumer device is PadDevice-derived or a wrapper; new `1404bxxxx` action-thunk family typed (command ID `+0x210`, analog floats `+0x238`/`+0x260`, override mask `+0x45a`, tail-call `[param_1+8]`, result boxer `FUN_1404b6cf0`, table-resident pump `FUN_14049e350` gating `[state+0x458]==3`); pump entity store re-pinned to `[holder+0x90]+0x1e0` pad-view container (not the padmgr singleton); `141d64260..` typed as a (start,end,param-blob) function-registration table, name-string theory refuted; `1403c5xxx`/`1403c7xxx` `+0x1e0` readers rejected as a separate teleport/raycast class. Walk-action handler behind `[param_1+8]`, state-float consumer, collision response and root ownership remain unattributed. E2 probe pass 9 (31 Oct): eligibility gate `FUN_1404ba050` decoded (override mask `+0x45a`; linked `(object,handler)` pairs `+0x110/+0x118` etc. validated by `FUN_1404b1cd0` against the `DAT_141c7b443` type registry via `FUN_140d55e50`); pump identity CLOSED (registration RVA entry `141d61fd0` + full-VA method blocks `141354500/550/5b8`; name-pointer theory refuted — targets are self-referential metadata headers); command dispatch identified as the FromSoft script VM (`DAT_141c7b478` current-object singleton behind hundreds of 0x50-spaced thunks `140016c50..140021xxx`; `[param_1+8]` is the VM call entry, so the walk handler is a VM-registered command handler, not statically xref-able native); all 46 `+0x370` view-chain load sites classified (biggest cluster: top-level pump `FUN_14024f6b0` 7 sites; stack-frame/guard-island/padding-device-field garbage rejected; new singleton `DAT_141c7e3e8`), and the top pump's per-frame consumer `FUN_1402d3970` carries VM-typed parameter-block COMMAND BYTES (resolver `FUN_14051f7c0`, type 5) smoothed into `[owner+0x1b0..0x1f0]` — typed negative for raw-stick routing through the view entry; movement magnitudes live only in the `1404b` command-state floats. Motor attribution UNKNOWN, E2 unchecked. Pass-10 executed 31 Oct: helpers decompiled (`FUN_140d55e50` type-tag registry lookup, singleton `DAT_141cf8c78`; `FUN_14051f7c0` typed parameter-block resolver, bytecode `+0x48`, counts `+0x1c8+type*0x10`, type 5 = command bytes); VM thunk family CLOSED as class — 537 script-command REGISTRATIONS (registrar `FUN_1404c78e0` lazily constructs VM singleton `DAT_141c7b478`), every command name recovered (`TurnCharactor` `140016d40`, `DisableMove` `140019810`, `SetFirstSpeed(Plus)` `14001a8f0/14001a940`, `SetMovePoint` `14001b160`, `SetCompletelyNoMove` `14001ff30`, `IsNoInputLeave` `14001f120`); typed NEGATIVE that ESD tests 219/257/266/297 are in this family; masked `+0x210` command-id reads = 4 sites typed (`FUN_1404b2050` two-axis commit confirming the pass-9 override-mask shape exactly, `FUN_1404b7110` one-axis + boxer, `FUN_1404fea40` command-ID edge detector pushing via `FUN_1401d7680`). Settings-singleton field scan inconclusive (148 xrefs, reads are in far accessors). Pass-11 next: `FUN_1401d7680` decompile + ESD interpreter test-ID dispatch for tests 219/257/266/297. Tool pass 31 Oct: soulsmodding stack staged under `modtools\` (soulstruct 2.6.0 editable + ezl_parser patch, Smithbox 2.2.6, DarkScript3 3.6.3, WitchyBND 3.0.1.0, Param Reloader v1.0.0 not game-dir deployed); read-only extraction of player c0000 ESP (225+277 named states), enemyCommon, dummy, 41 GameParam JSONs into `modtools\data\`; ESD locomotion vocabulary typed (`GetMovementType` chr[219], `IsInputDirectionMatch` chr[297], `GetMoveAnimParamID` chr[257], `GetMoveAnimBlendRatio` chr[266], `GetAnimIDFromMoveParam` chr[110]); no raw stick in ESD — script layer consumes resolved locomotion; pass-10 gains string-anchor route (`EzS`/`.esd`/`chresd`) to the ESD interpreter test-ID dispatch. Pass-10 (31 Oct): thunk family CLOSED as the script-VM COMMAND registration set (537 thunks, names recovered, locomotion-adjacent names pinned; ESD tests NOT in the family); `+0x210` readers typed (`FUN_1404b2050` two-axis commit, `FUN_1404fea40` edge detector). Pass-11 (31 Oct): `FUN_1401d7680` typed as the edge detector's slot writer (record copier, no arithmetic); script layer identified as **EzState v2.11.2** middleware (RTTI `EsdMan`/`EzState23Loader`, player ESD loads via `FUN_140347010`); typed NEGATIVE - tests 219/257/266/297 have no name strings, conditions are numeric-ID dispatched inside EzState, string-anchor route CLOSED; remaining static route: EzState descriptor/condition entries adjacent to registrar `FUN_1404c78e0` reading floats `+0x238/+0x260`. | Native motor attribution, continuous dodge heading with lock-on on/off, path/collision/latency matrix, reference-stability headset row. |
| F body/crouch | Offline publication-reader race fix PASS: targeted red 0/1 then green 1/1. 31 Oct gaze-follow gain (kTorsoLookGain 1.15, twist cap unchanged) plus a one-shot per-hand neutral-basis probe in arm_ik.cpp naming bind-pose vs T-pose fallback for the reported left-hand palm-up defect; Current DSVR.log shows the first neutral-hand lines were premature: both sides reported T-pose fallback with hand bone -1 before rig revision 1 froze the source; static logged flags then suppressed valid follow-up diagnostics. Logger now waits for a valid hand index and frozen source. Right-hand neutral is now canonical when valid; the left neutral hand basis mirrors it, preserving the left tracked position and native grip lever, with wrist-roll sign corrected for mirrored basis. Same-position synthetic left-bind quarter-turn regression passes arm_pose_native 1/1 Debug and Release. Full Debug/Release CTest 115/115 each; auto_verify -SkipPackage -NoLatest Release 115/115 + Doctor PASS. No headset confirmation and no gate closed. 31 Oct immersion feature pass (offline): calibration v2 — natural standing AND seated pose gates (`CalibrationCapture::Feed(seated)` accepts a natural lap posture, rejects the same pose standing, both regression-tested), wrist calibrates 1:1 at any wrist bend (`neutral_controller` records the exact bent orientation; new 60 deg wrist-pitch test), `CalibrationSample.play_style` plumbed through xr.cpp → calibration.cpp into the stored profile and instruction/hint text; floor/eye-height spatial data (for Reach, Height, Floor, roomscale walking) now sampled every frame, and the merged horizon-locked theater panel anchors y = judged floor + calibrated eye height. Offline ladder all green (Debug/Release builds exit 0, ctest 115/115 each, `pwsh tools/auto_verify.ps1 -SkipPackage -NoLatest` exit 0 + Doctor PASS). No headset confirmation, no gate closed. 31 Oct dist refresh on the immersion bytes (full auto_verify exit 0: core DSVR.dll b77d3f05…0f7101, loader dinput8.dll 2ec609c4…d567b973, DSVR-Setup.exe c4ed77bf…89f4c4fc, zip 1,774,502 bytes); installed core remains the 30 Oct presentation build BC8475AA…, so an authorized --install --yes on the fresh bytes precedes any F2/C2/headset acceptance. 31 Oct Electron plan phase 1 done offline: C++ `--json` seam (`status/doctor/verify/settings-index`, new `src/setup/json.{h,cpp}` + `test_setup_json.cpp`, count 115→116) green in both configs + live smoke PASS; launcher skeleton (phase 2) — dist-size decision RESOLVED same day by operator (separate optional asset `DSVR-Launcher-x64.zip`, core zip stays lean), then LANDED same day dev-only: `launcher/` Electron 45.2.1 shell, `src/engine.mjs` spawn surface only (co-located Setup exe first, dev build fallback, SHA-256 vs `DSVR-SHA256.txt`, strict verb→fixed-argv allow-list), vanilla ES-module Home/Game/Settings/Activity, headless `--smoke`; live smoke through node AND Electron main both exit 0 (status/verify/doctor/settings-index from the built phase-1 exe, `launch` surfaces `unknown_verb` pending phase-3 verb, bogus verb never spawns, dist-copy hash check PASS); zero C++ changes, ctest stays 116/116, gui.cpp untouched. No deploy/native/rendered acceptance. 1 Nov update: current calibration-first package 0.16.0-render-host-preview is installed; exact DLL hashes match, packaged Doctor passes, and final preflight is READY (12 OK/4 WARN/0 BLOCKED). Operator will launch through Steam/Quest Link/SteamVR; no headset visual acceptance yet. | Operator-led headset review of Calibration Room, one-second capture, stage grid, 16:9 menu/loading and recovery; then resume exact-candidate F2/C2 rendered alignment. |
| G encounter | Not accepted; G5 comfort mask implemented in mono with offline checks. Shield-surface interception and motion-toward-attack parry specified; native bindings absent. | Physical offense/defense, native block/guard-break/parry matrix, haptics, movement, native stereo composition, headset comfort. |
| H interactions/UI | SOURCE FIXED, native UNVERIFIED. Selection-owner chain closed read-only; InteractionSelectionStore feeds the reach gate which refuses untargeted reach. Qualification BLOCKED on native runtime evidence. | Case-aligned row/dummy/LOS observations; per-class range semantics; native controller-only matrix. |
| I breadth/ranged | Unqualified. | Equipment/action family matrix in native encounters. |
| J campaign | Unqualified. | Traversal/boss/special-action and longer-session matrix. |
| K release | NOT QUALIFIED. Formal distribution is automated for `auto_verify.ps1 -Package -Version`: all A-J/K1-K4 prerequisites, full CTest, Doctor, authenticated `gh`, and an existing pre-pushed `v<version>` tag required; it publishes ZIP/Setup/SHA without pushing tags. Current preflight BLOCKED with 67 prerequisites open; no formal package/release published. Latest offline verification: full Debug/Release builds PASS, CTest 115/115 each; auto_verify -SkipPackage -NoLatest Release 115/115 + Doctor PASS. `python -B tests/test_package.py` PASS 16 tests; latest formal preflight for version 0.17.0 BLOCKED 67 open prerequisites, no distribution. Latest full Debug/Release CTest 115/115 each and auto_verify -SkipPackage -NoLatest Release 115/115 + Doctor PASS. Offline ladder PASS 31 Oct (re-run after the setup-button / body-default / theater-resolution pass): Debug build exit 0 and Debug 114/114; Release build (`--target ALL_BUILD`) exit 0 and Release 114/114; `auto_verify` Release 114/114 with the pinned build supported; doctor NOT re-run this session (last PASS 30 Oct after a forced `dsvr_setup` rebuild). Setup now installs the frame-rate dependency itself: an `Install FPS Unlock` action button downloads the pinned GitHub asset over WinHTTP, re-checks the pinned URL against its own allow-list, verifies ZIP and DLL SHA-256, snapshots anything it would overwrite into `%TEMP%`, refuses while the game runs, and installs the payload as `dinput8-chain.dll` with `chain_proxy` set; it disables itself once installed and the handler re-checks state so a stray message cannot re-download. Both pinned digests were re-verified against the live release this session (ZIP `37797dd8…fbba`, 227755 bytes; inner `dinput8.dll` `e368f8a5…b00c`, equal to the `dinput8-chain.dll` already in the game folder). Body hiding is now the shipped default everywhere: `hide_torso_and_legs` defaults true in `Config`, `FactorySafeDefaults` and `DoInstall` (arms and weapon stay drawn; `hide_head` true, `hide_player_model` false), with two existing assertions re-anchored rather than relaxed. Theater presentation sizes the eye swapchains from the runtime maximum image rect instead of the recommended rect, and a presentation change that flips that hint rebuilds the swapchains (new `xr_eye_resolution` test; count 113 → 114). The operator reports the unlock was felt in a live run — anecdote, not a measured window. Rate ownership is now single-source: DSVR never caps or uncaps the game (`frame_limit_fps` default 0, keys dropped on save and ignored on load, perf presets carry scale/AA only); the loader hands the dependency INI `MaxFPS` from the last-known HMD refresh in VR or the current desktop refresh otherwise, and writes nothing when neither is known; the setup UI shows the rate as read-only text and its dead frame-cap control id was deleted. User-approved official FPS Unlock v1.9.0 installed as `dinput8-chain.dll` + INI; GitHub asset digest verified, x64 `DirectInput8Create` export present, DSVR `chain_proxy` set. Game remains closed; DSVR runtime chain load/INI synchronization, 90 Hz/headset performance and clean-install/uninstall/recovery remain unverified. No K gate closed. | Exact-candidate offline launch log with `forwarding DirectInput8Create` and `MaxFPS` source; headset 90 Hz qualification; recovery/install and all required gates. |

## 9. Evidence index

### Current evidence owners

- [Melee contract](docs/NATIVE_MELEE_CONTRACT.md): exact contact/health/event observations, split-continuation limits, unresolved ABI/ownership/outcome questions. No current native damage acceptance.
- [Arm evidence](docs/ARM_IK_EVIDENCE.md): captured model-space bone conventions, twist/shear findings, fixture provenance and current revalidation needs. Not an unimplemented-code claim.
- [Stereo contract](docs/NATIVE_STEREO_CONTRACT.md): recovered consumer tail, scratch/context cleanup, CPU-counter leads and unresolved replay ownership. No enabled eye-render binding.
- [Locomotion contract](docs/NATIVE_LOCOMOTION_CONTRACT.md): orientation selection status and the unattributed motor owners.
- [Interaction contract](docs/NATIVE_INTERACTION_CONTRACT.md): selection-owner chain and reach-gate status.
- [Engineering](docs/ENGINEERING.md): invariants for camera, body, movement, stereo, interaction and lifecycle ownership.
- [Handoff](MODLOG.md): current checkout and next unmet execution gate.

### Primary static workspace

Primary project: `C:\Tools\DarkSoulsRemaster_De\analysis-full\DSRFull.gpr`, loaded program `DarkSoulsRemastered.exe`. Keep primary analysis read-only. `analysis-full-attack` is a repair workspace, not the primary source. Existing installation references are Ghidra 12.1.4 and JDK 21 under `C:\Tools\DarkSoulsRemaster_De`; verify actual paths/environment before tools run.

Routine native queries go through the `ghidra-mcp` MCP server configured in `%LOCALAPPDATA%\Empryo\config.json` (`mcpServers`, command `C:\Tools\DarkSoulsRemaster_De\ghidra-mcp\.venv\Scripts\bridge-mcp-ghidra.exe --no-lazy`, `cwd` at the ghidra-mcp checkout, `timeout: 1800000`). It auto-connects over TCP to the live Ghidra GUI bridge (`http://127.0.0.1:8089`, project `DSRFull`) and exposes function/string/decompiler tools directly; no external scripts are needed, but the Ghidra GUI with the bridge must be running. `--no-lazy` is deliberate: lazy mode advertises 101 tools and leaves the `xref`/`comment` groups unloaded, so `get_xrefs_to` and `get_assembly_context` answer `not_loaded`; `--no-lazy` advertises 239 and every tool is callable at once. `run_script_inline` stays disabled (`GHIDRA_MCP_ALLOW_SCRIPTS` unset). The headless `tools/ghidra/query.ps1` wrapper, `tools/ghidra/repair.ps1` and the `combat-evidence/melee-20261024/mcp_read.py` shim were removed on 24 October 2026 and must not be reintroduced while the GUI holds `DSRFull`; the `.java` probes in `tools/ghidra` remain as receipts of how earlier observations were produced. Verified 24 October 2026: bridge live (261,311 functions), seam functions `FUN_140357a20`/`FUN_140207f10`/`FUN_1408e06b0` confirmed by address and xref count through MCP, matching prior REST grounding.

#### Full-utilization playbook (ghidra-mcp v7.0.0, 209 catalog tools)

Per the [upstream README](https://github.com/bethington/ghidra-mcp):

- Lazy loading is the default: only `listing,function,program` groups register on connect. Pull in the rest on demand with `load_tool_group({group})` — `xref` (xrefs, call graphs), `analysis` (P-code dataflow, completeness), `emulation`, `documentation` (function hashes, cross-version diff), `datatype`, `comment`, `symbol`. The tool catalog refreshes after a group load (confirmed in this harness).
- 7.0.0 tool consolidation: old `decompile_function` is `get_functions(fields=decompiled_code)`; `list_functions`/`search_functions` are `find_functions`; callers/callees come from `get_functions(fields=callers,callees)`. Ignore any pre-7.0 prompt or script that calls the old names.
- Grounding set this project uses read-only: `get_metadata`, `find_functions`, `get_functions(fields=signature,decompiled_code,callers,callees)`, `get_xrefs_to`, `get_function_call_graph` (xref group), `search_strings`, `list_program_items(kind=...)`. Deeper leads available: `analyze_dataflow` (P-code forward/backward value tracing for field semantics), `emulate_function` (isolated function execution), `get_function_hash`/`diff_functions` (patched-game-version comparison), `batch_string_anchor_report` (source-file strings to FUN_* map).
- Write-side tools (`rename_function`, `set_function_prototype`, `create_struct`/`apply_data_type`, `set_comment`/`apply_documentation`, `save_program`, function tags, property maps, bookmarks) are full-featured with convention enforcement and atomic batch transactions, but stay out of `DSRFull` under the read-only rule above. Annotate only in the repair workspace, and never rename grounded `FUN_*` targets — recorded evidence links to their addresses and auto-generated names.
- Health/schema: `check_connection` and `mcp_health` tools; the authoritative live tool list is `GET http://127.0.0.1:8089/mcp/schema`.
- While only `DarkSoulsRemastered.exe` is open, current-program fallback routing is safe. If a second program (e.g. a mod DLL) is opened in the same Ghidra, set `GHIDRA_MCP_REQUIRE_PROGRAM_SELECTORS=1` on the bridge and always pass `program=`.

First full-utilization findings (22 October 2026, MCP `get_xrefs_to`/`get_functions`):

- The `SkeletonParam` registry string (`0x141372b80`) has exactly two xrefs, both in `FUN_140531bc0` — the parameter-registration entry to read first for F4 skeleton/foot-IK work.
- The root/palette feed `FUN_140207f10` has exactly two listed callers: the grounded pose seam `FUN_140357a20` and `FUN_1407051d0`. The latter was examined in the native-contract continuation: it also invokes attachment propagation (`140207510/1402079b0`). Its runtime actor coverage remains unqualified; do not assume the player seam is the sole model feed. See ARM_IK_EVIDENCE.

Use working REA capabilities first. Historical Windows native-provider availability failed its security/ownership admission; do not bypass the gate. Check current availability once; do not retry an identical unavailable provider indefinitely. A .gpr launcher is not a standalone binary. Keep raw decompilation and extracted game data outside distributable mod sources.

### Soulsmodding data tool stack (staged 31 Oct 2026)

Under `C:\Tools\DarkSoulsRemaster_De\modtools\` (all outside the repo and the game directory):

- **Soulstruct 2.6.0** installed *editable* from `modtools\repos\soulstruct` into `modtools\venv` (Python 3.14.6). PyPI 2.3.2 is unusable for DS1R ESD (Condition-reader `len(int)` TypeError, wheel ships no data JSONs). Local patch in the clone: `base/ezstate/esd/ezl_parser.py decompile()` coerces `byte_sequence = bytes(byte_sequence)` — fixes an unhashable-bytearray crash that blocked ALL CHR ESP output. ESD decompile uses `write_esp_directory()` (`write_esp_file` is TALK-only); Binder entries expose `.path`.
- Clones: `modtools\repos\ds1_chresd_metadata` (annotated player `c0000.esd.dcx` + Zeditor `.meta`), `modtools\repos\DSEventScriptTools` (DS1R emedf JSON). ESDLang skipped (DS3/SEKIRO/ER only, no DS1R). UXM/Nuxe not needed (DSR unpacks to folders).
- Extracted releases in `modtools\tools\`: Smithbox 2.2.6 (OpenGL/Vulkan exes), DarkScript3 3.6.3, WitchyBND 3.0.1.0 win-x64, DS1R Param Reloader v1.0.0 (`ParamReloader.exe` + `PropertyHook.dll`). The Param Reloader is **not** staged into the game install — DSVR owns that directory; deployment requires explicit operator OK.
- Read-only extractions in `modtools\data\`: `esd\c0000\` ESP (machine 0 = 225 states, machine 1 = 277), `esd\enemyCommon\` (285), `esd\dummy\` (131; `m10.chresdbnd.dcx` contains only `chr\dummy.esd`), `params\` all 41 GameParam JSONs (`Movement.json` = MoveParam; rows are animation selectors, no speed floats), `esd\c0000_statelist.txt` (every state name + entry/exit/while script, meta-annotated). Working scripts: `dsvr_extract.py`, `meta_align.py`, `peek_meta.py`.
- Provenance: game `c0000.esd.dcx` (54034 B, sha `85fc1eaa…`) ≠ metadata-repo copy (51912 B, conditions merged/re-authored), but both parse to identical per-machine state-id sets (0..224, 0..276) and the `.meta` StateGroups map 1:1 (`PassiveStateGroup`↔machine 0, `ActiveStateGroup`↔machine 1), so Zeditor names apply to the game file exactly; game bytes are canonical.
- Use for E2: typed ESD locomotion vocabulary (soulstruct `functions.pyi` IDs, reconstruction-grade until DSR VM dispatch is grounded): `GetMovementType` chr[219], `IsInputDirectionMatch` chr[297], `GetMoveAnimParamID` chr[257], `GetMoveAnimBlendRatio` chr[266], `GetAnimIDFromMoveParam` chr[110] (codes 0 Fwd/1 Back/2 Left/3 Right/5 Jump), `Set4DirectionMovementThreshold` command chr[1][2014]. No raw stick ever appears in ESD — the script layer consumes resolved locomotion.

### External reference material (permanent)

- [Souls Modding wiki — Dark Souls: Remastered reference material](https://soulsmodding.com/doku.php?id=ds1r-refmat:main): user-recommended permanent community reference, re-read **24 October 2026**. Useful for parameter names, attack data, animation conventions, parts/maps and tool vocabulary. **The DSR landing page deliberately links many DS1 pages**: do not mistake a shared reference for a verified DSR x64 object layout or callable ABI.

  Targeted entry points for future IK/combat work:

  - [AtkParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:atkparam): sphere/capsule radii, paired `hit*_DmyPoly1` / `hit*_DmyPoly2` references, `hitSourceType`, guard/stamina modifiers and guard/invulnerability-related flags. These are promising *data-schema clues* for the native endpoint-key investigation, not proof that event `+0xF0/+0xF4` has the same layout or that endpoint distance equals visible blade length.
  - [BehaviorParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:behaviorparam): attack/projectile behavior selection; consult while tracing legitimate context creation, not as permission to manufacture an event.
  - [EquipParamWeapon](https://soulsmodding.com/doku.php?id=ds1-refmat:param:equipparamweapon): equipment/weapon-motion vocabulary for correlating real loadouts and one-/two-hand observations. Match actual equipped PARAM IDs and extracted rows before applying a profile.
  - [Attack Animation Schema](https://soulsmodding.com/doku.php?id=ds1-refmat:attack-animation-schema): community conventions for category/hand/action animation IDs. Useful to label natural animation/context evidence; **not** a route to gesture-triggered canned melee.
  - [SkeletonParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:skeletonparam) and [MoveParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:moveparam): leads for native foot IK and movement configuration, not proof of render-only avatar scaling or safe collider writes.

  Evidence ladder: wiki clue -> exact DSR parameter schema/extracted row -> pinned native consumer/ownership -> controlled native comparison. The older 22 October registry-string observations (`SkeletonParam` `141372B80`, `MoveParam` `141372A70`, `AtkParam_Pc` `141372910`, `EquipParamWeapon` `1413727E0`, `SpEffectParam` `1413729C8`) establish names only, **not every wiki field/type/offset**. Keep raw game data/decompilation outside distributable sources. This reference adds no competing execution plan or permission to guess native calls.

### Retained research and its limits

The complete [pre-consolidation research record](docs/history/2026-10-21-before-consolidation/MODDING_PLAN.md.txt) retains addresses, source links, capture filenames and failed hypotheses. Consult only a targeted historical finding after identifying the current missing fact. Historical proposed routes, next actions, account setup and status claims are not instructions. Historical source URLs were checked at their recorded dates, not reverified by this cleanup.

Key leads to avoid rediscovering:

- Camera commands 0x3B/0x3E copy projection/transform data; CPU camera entity selection, draw-context preparation, constant upload and retained GPU captures exist. Those do not by themselves prove a simulation-free eye-render bracket or safe worker/resource lifetime.
- Six-view cubemap and legacy stereo code are multiview leads, not arbitrary OpenXR eye-render APIs. View history/presentation side effects must be isolated.
- Native post-animation captures identified an 80-bone player layout and model-space pose-copy order. Final skinning/attachment behavior and current rendered alignment require candidate-specific evidence.
- Native UI/blur/face filtering and XInput recovery have source/static observations and historical runs; their current coverage/defaults must be read from current code and qualified on the tested package.
- Generic AI/event builders and CreateDamage_NoCollision registration names are not proof of melee damage ABI. Health setter 0x140322910 is not an attack entry point.
- Historical save-path/storage research does not replace the current reversible same-account test policy. Do not reopen the superseded lab-account route.

For each new investigation state one missing fact, exact caller/consumer, expected discriminating observation, pinned game identity and evidence path. Update the relevant focused evidence owner after observation. Source/static/native/headset levels remain separate.

## 10. Pasteable starting prompt

```text
Work in C:\Users\Gabrielle Monlea\Documents\DSVR. Read AGENTS.md,
MODLOG.md and the cockpit: MODDING_PLAN.md plus the lane files
under .empryo/cockpit/lanes/ — the lane checkboxes are the sole plan.
The user requires native conversion and physical combat; do not replace
that goal with gesture-triggered canned attacks, direct HP edits or mono
stereo. Inspect git status/diff and preserve all existing work. Use the
harness graph tools first for code discovery. Work on one unmet gate from
the board. Before a native mutation prove ABI, owner, lifetime, thread and
outcome correlation. Use the existing protected native test procedure.
Keep implementing and qualifying required gates; preserve precise evidence
and a resumable next action. Do not claim completion from green offline
tests. When blocked, name the exact prerequisite and continue independent
useful work. Update the lane files, the ledger in MODDING_PLAN.md and
MODLOG instead of writing another roadmap.
```

- [Melee contract](docs/NATIVE_MELEE_CONTRACT.md): exact contact/health/event observations, split-continuation limits, unresolved ABI/ownership/outcome questions. No current native damage acceptance.
- [Arm evidence](docs/ARM_IK_EVIDENCE.md): captured model-space bone conventions, twist/shear findings, fixture provenance and current revalidation needs. Not an unimplemented-code claim.
- [Stereo contract](docs/NATIVE_STEREO_CONTRACT.md): recovered consumer tail, scratch/context cleanup, CPU-counter leads and unresolved replay ownership. No enabled eye-render binding.
- [Engineering](docs/ENGINEERING.md): invariants for camera, body, movement, stereo, interaction and lifecycle ownership.
- [Handoff](MODLOG.md): current checkout and next unmet execution gate.

## Primary static workspace

Primary project: `C:\Tools\DarkSoulsRemaster_De\analysis-full\DSRFull.gpr`, loaded program `DarkSoulsRemastered.exe`. Keep primary analysis read-only. `analysis-full-attack` is a repair workspace, not the primary source. Existing installation references are Ghidra 12.1.4 and JDK 21 under `C:\Tools\DarkSoulsRemaster_De`; verify actual paths/environment before tools run.

Routine native queries go through the `ghidra-mcp` MCP server configured in `%LOCALAPPDATA%\Empryo\config.json` (`mcpServers`, command `C:\Tools\DarkSoulsRemaster_De\ghidra-mcp\.venv\Scripts\bridge-mcp-ghidra.exe --no-lazy`, `cwd` at the ghidra-mcp checkout, `timeout: 1800000`). It auto-connects over TCP to the live Ghidra GUI bridge (`http://127.0.0.1:8089`, project `DSRFull`) and exposes function/string/decompiler tools directly; no external scripts are needed, but the Ghidra GUI with the bridge must be running. `--no-lazy` is deliberate: lazy mode advertises 101 tools and leaves the `xref`/`comment` groups unloaded, so `get_xrefs_to` and `get_assembly_context` answer `not_loaded`; `--no-lazy` advertises 239 and every tool is callable at once. `run_script_inline` stays disabled (`GHIDRA_MCP_ALLOW_SCRIPTS` unset). The headless `tools/ghidra/query.ps1` wrapper, `tools/ghidra/repair.ps1` and the `combat-evidence/melee-20261024/mcp_read.py` shim were removed on 24 October 2026 and must not be reintroduced while the GUI holds `DSRFull`; the `.java` probes in `tools/ghidra` remain as receipts of how earlier observations were produced. Verified 24 October 2026: bridge live (261,311 functions), seam functions `FUN_140357a20`/`FUN_140207f10`/`FUN_1408e06b0` confirmed by address and xref count through MCP, matching prior REST grounding.

### Full-utilization playbook (ghidra-mcp v7.0.0, 209 catalog tools)

Per the [upstream README](https://github.com/bethington/ghidra-mcp):

- Lazy loading is the default: only `listing,function,program` groups register on connect. Pull in the rest on demand with `load_tool_group({group})` — `xref` (xrefs, call graphs), `analysis` (P-code dataflow, completeness), `emulation`, `documentation` (function hashes, cross-version diff), `datatype`, `comment`, `symbol`. The tool catalog refreshes after a group load (confirmed in this harness).
- 7.0.0 tool consolidation: old `decompile_function` is `get_functions(fields=decompiled_code)`; `list_functions`/`search_functions` are `find_functions`; callers/callees come from `get_functions(fields=callers,callees)`. Ignore any pre-7.0 prompt or script that calls the old names.
- Grounding set this project uses read-only: `get_metadata`, `find_functions`, `get_functions(fields=signature,decompiled_code,callers,callees)`, `get_xrefs_to`, `get_function_call_graph` (xref group), `search_strings`, `list_program_items(kind=...)`. Deeper leads available: `analyze_dataflow` (P-code forward/backward value tracing for field semantics), `emulate_function` (isolated function execution), `get_function_hash`/`diff_functions` (patched-game-version comparison), `batch_string_anchor_report` (source-file strings to FUN_* map).
- Write-side tools (`rename_function`, `set_function_prototype`, `create_struct`/`apply_data_type`, `set_comment`/`apply_documentation`, `save_program`, function tags, property maps, bookmarks) are full-featured with convention enforcement and atomic batch transactions, but stay out of `DSRFull` under the read-only rule above. Annotate only in the repair workspace, and never rename grounded `FUN_*` targets — recorded evidence links to their addresses and auto-generated names.
- Health/schema: `check_connection` and `mcp_health` tools; the authoritative live tool list is `GET http://127.0.0.1:8089/mcp/schema`.
- While only `DarkSoulsRemastered.exe` is open, current-program fallback routing is safe. If a second program (e.g. a mod DLL) is opened in the same Ghidra, set `GHIDRA_MCP_REQUIRE_PROGRAM_SELECTORS=1` on the bridge and always pass `program=`.

First full-utilization findings (22 October 2026, MCP `get_xrefs_to`/`get_functions`):

- The `SkeletonParam` registry string (`0x141372b80`) has exactly two xrefs, both in `FUN_140531bc0` — the parameter-registration entry to read first for F4 skeleton/foot-IK work.
- The root/palette feed `FUN_140207f10` has exactly two listed callers: the grounded pose seam `FUN_140357a20` and `FUN_1407051d0`. The latter was examined in the native-contract continuation: it also invokes attachment propagation (`140207510/1402079b0`). Its runtime actor coverage remains unqualified; do not assume the player seam is the sole model feed. See ARM_IK_EVIDENCE.

Use working REA capabilities first. Historical Windows native-provider availability failed its security/ownership admission; do not bypass the gate. Check current availability once; do not retry an identical unavailable provider indefinitely. A .gpr launcher is not a standalone binary. Keep raw decompilation and extracted game data outside distributable mod sources.

## External reference material (permanent)

- [Souls Modding wiki — Dark Souls: Remastered reference material](https://soulsmodding.com/doku.php?id=ds1r-refmat:main): user-recommended permanent community reference, re-read **24 October 2026**. Useful for parameter names, attack data, animation conventions, parts/maps and tool vocabulary. **The DSR landing page deliberately links many DS1 pages**: do not mistake a shared reference for a verified DSR x64 object layout or callable ABI.

  Targeted entry points for future IK/combat work:

  - [AtkParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:atkparam): sphere/capsule radii, paired `hit*_DmyPoly1` / `hit*_DmyPoly2` references, `hitSourceType`, guard/stamina modifiers and guard/invulnerability-related flags. These are promising *data-schema clues* for the native endpoint-key investigation, not proof that event `+0xF0/+0xF4` has the same layout or that endpoint distance equals visible blade length.
  - [BehaviorParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:behaviorparam): attack/projectile behavior selection; consult while tracing legitimate context creation, not as permission to manufacture an event.
  - [EquipParamWeapon](https://soulsmodding.com/doku.php?id=ds1-refmat:param:equipparamweapon): equipment/weapon-motion vocabulary for correlating real loadouts and one-/two-hand observations. Match actual equipped PARAM IDs and extracted rows before applying a profile.
  - [Attack Animation Schema](https://soulsmodding.com/doku.php?id=ds1-refmat:attack-animation-schema): community conventions for category/hand/action animation IDs. Useful to label natural animation/context evidence; **not** a route to gesture-triggered canned melee.
  - [SkeletonParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:skeletonparam) and [MoveParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:moveparam): leads for native foot IK and movement configuration, not proof of render-only avatar scaling or safe collider writes.

  Evidence ladder: wiki clue -> exact DSR parameter schema/extracted row -> pinned native consumer/ownership -> controlled native comparison. The older 22 October registry-string observations (`SkeletonParam` `141372B80`, `MoveParam` `141372A70`, `AtkParam_Pc` `141372910`, `EquipParamWeapon` `1413727E0`, `SpEffectParam` `1413729C8`) establish names only, **not every wiki field/type/offset**. Keep raw game data/decompilation outside distributable sources. This reference adds no competing execution plan or permission to guess native calls.

## Retained research and its limits

The complete [pre-consolidation research record](docs/history/2026-10-21-before-consolidation/MODDING_PLAN.md.txt) retains addresses, source links, capture filenames and failed hypotheses. Consult only a targeted historical finding after identifying the current missing fact. Historical proposed routes, next actions, account setup and status claims are not instructions. Historical source URLs were checked at their recorded dates, not reverified by this cleanup.

Key leads to avoid rediscovering:

- Camera commands 0x3B/0x3E copy projection/transform data; CPU camera entity selection, draw-context preparation, constant upload and retained GPU captures exist. Those do not by themselves prove a simulation-free eye-render bracket or safe worker/resource lifetime.
- Six-view cubemap and legacy stereo code are multiview leads, not arbitrary OpenXR eye-render APIs. View history/presentation side effects must be isolated.
- Native post-animation captures identified an 80-bone player layout and model-space pose-copy order. Final skinning/attachment behavior and current rendered alignment require candidate-specific evidence.
- Native UI/blur/face filtering and XInput recovery have source/static observations and historical runs; their current coverage/defaults must be read from current code and qualified on the tested package.
- Generic AI/event builders and CreateDamage_NoCollision registration names are not proof of melee damage ABI. Health setter 0x140322910 is not an attack entry point.
- Historical save-path/storage research does not replace the current reversible same-account test policy. Do not reopen the superseded lab-account route.

For each new investigation state one missing fact, exact caller/consumer, expected discriminating observation, pinned game identity and evidence path. Update the relevant focused evidence owner after observation. Source/static/native/headset levels remain separate.
