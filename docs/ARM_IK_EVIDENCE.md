# Native arm pose evidence and revalidation

Consolidated: 21 October 2026. Evidence reference only. [Authority](../README.md#authority-and-maintenance); implementation belongs to execution milestone F, with current status in MODLOG and the execution ledger. This file does not add task order, approval gates or restrictions on authorized testing.

The following measurements/findings were recorded by the preceding arm investigation. They have not been remeasured by this documentation cleanup. Their hypotheses describe the observed candidate; ongoing source changes may already address them. Inspect git diff and actual tests before treating any fix as pending or any test as expected to fail.

## 29 October retained-session audit

Read-only inspection of the installed DSVR.log completed the CamGate evidence
probe; no game launch, input, memory write, save/config edit or deployment.
The log at the game root has 646,108 bytes, last-write UTC
2026-10-29T00:22:18.5120389Z, SHA-256
F91E129CD974A8B5905CA383CB1F7A26B410C6AA4E59122AC61B6200E2458C06.
Its latest session header (line 3062) is 2026-10-28 20:20:52 local. Line numbers
below refer to that hash, not a future appended log.

Observed, not inferred:

- Lines 3074-3076 pin x64 game identity: timestamp 0x6344ca56, image size
  0x319b000, executable hash A45AAA36DD2F6CC151670A639EA5547043CF38EA79FF4178B963C6ED71F98D7B;
  App 1.03.1 / build 10943698, 50,286,344 bytes.
- Lines 3106-3110: SteamVR/OpenXR Meta compatibility mode 2.18.4,
  recommended eyes 2144x2292. Lines 3136-3140: ResizeBuffers returns success,
  actual swapchain/staging 1920x1080. This session does not reproduce the
  earlier failed-resize symptom.
- Line 3235, 20:21:13.261: camera closed for XR menu presentation. Lines
  3266 and 3273: sanctuary closes, then CamGate reports applied at
  20:21:17.360. The blanket "camera never applied" diagnosis is stale.
- Line 3278, 20:21:17.443: native body copy verified=1 refused=0 map=69
  body=80 distinct=1 seq=1506. Line 3284, 20:21:17.935: input bridge still
  reports attack/defence=no-committed-hands, hands=0, hands_epoch=0,
  space_epoch=2. The last value is the input observation, not a captured
  copy-publication epoch. The first successful copy is NOT a successful
  public getter observation.
- Lines 3668-3669: CPU visits=commits=2054, last_seq=bound_seq=2699,
  sequence_refusals=0; rendered submission samples applied sequence 2699,
  camera thread 6820, render thread 6936. Line 3676 reaches verified=2100,
  refused=0, seq=2726. These are CPU receipts, explicitly not GPU/attachment
  qualification. No claim of all intervening frames or final mesh correctness.
- Line 3285: equipment=1, weapons=1401000/500000, stance=1; native damage,
  shield and two-hand bindings all remain 0. True stereo remains unbound.

Current installed identity (read-only Get-FileHash audit):

| File | SHA-256 |
|---|---|
| DSVR.dll | B493BDCEAD2CD6E9D4072424029D15435C85D385B74B6ABB07C327E1BB2B0898 |
| dinput8.dll | F9ADFF97179DFDC58AC73B0A694D469844B3840CCABFB2313CDFD747BFEB5830 |
| DSVR-Setup.exe | 52141547B34B4771A00DB93918B22714351E3195D2C89CC25CA0F1EA71BBA048 |
| DSVR.toml | 2B8DCDE0C15C66AE4E109A1A33B1EF85981DAD766942CFBC2510C8EDF1DB5EDE |
| DSVR.toml.calibration | 9BC485010E52F59E5D1F35B9AFE0C88B47FABB52B038DDCFBE7FE33614503D71 |

All three installed binaries match dist/DSVR-latest-verification.json
(built_utc 2026-10-29T01:15:16Z). However core and Setup last-write times are
00:45:47Z, later than the log's 00:22:18Z; the log does not record its running
core hash. Therefore this is retained-candidate evidence, not qualification
of the installed B493BDCE candidate. Current config is also later than the
log (00:32:18Z). It requests headlook=true, gameplay_theater=false, seated
mode, frame_limit_fps=90, render_scale_percent=100, stereo=false, live IK,
hide_head/hide_torso_and_legs/hide_second_model=true. Do not infer those were
all the captured session settings; the log records an initial 72 FPS fallback.

At audit time: game and vrserver absent, Steam PID 16416; registered runtime
SteamVR steamxr_win64.json. This inventory is not protected-run preflight,
Steam Offline readiness, a live XR session or save-transaction qualification.
No backup vault or retained verification receipt was modified or enumerated.

### 29 October publication-reader interleaving regression

The actual ArmIkLatestHands and ArmIkCombatBench readers capture tracking
before acquiring g_hands_mutex. The native copy can publish newer hands in
that interval. PublishedHandsCurrent then compared that new receipt with the
older local tracking snapshot and irreversibly cleared copy verification,
hands and bench. This reproduces copy success followed by unavailable hands
at the real adapter seam; it is not proof this was the retained session's
only cause.

The arm_pose_native fixture now performs a natural camera/target/source/copy
transaction after the reader has captured its old tracking snapshot and
before that reader acquires the hand mutex. It asserts that the copy commit
counter increased and both public readers can obtain the new verified receipt.
Targeted Debug rebuild + CTest failed before the fix (0/1, job-96, CHECK
accepted && after.verified), and passed after it (1/1, job-97). Full Debug
and Release builds/CTest PASS 113/113 each (job-98); auto_verify repeats
Release 113/113, doctor PASS and refreshes dist (ZIP 1,584,277 bytes). New
core hash 9ce2a7ef6f95abaf86e59e13b2a97def70e0d4c1e6a8fdbea2ad880f9cf27ed1;
not deployed or tested in a native/headset session.

Fix: when the reader's nonzero tracking sequence predates the committed hands,
refresh tracking once under the existing hand mutex before applying the same
lifetime checks. No retry loop, native replay, extra pose write or relaxed
camera-sequence equality/epoch/calibration/100 ms freshness requirement.
Both readers use the shared check. An actually regressed tracking sequence
still refuses and irreversibly invalidates: added a positive older-sequence
case alongside the existing zero-sequence, loss, mode, epoch and calibration
cases. The provisional source-read refusal remains covered by the fixture.

Final rendered skinning/attachments, operator alignment and exact-current-
candidate input/native outcomes remain UNVERIFIED. The next task lives only
in MODDING_PLAN.md section 5; do not repeat camera instrumentation.


## Captured evidence

Measurements below come from the recorded native poses.

GhidraMCP 7.0.0 (GUI, `http://127.0.0.1:8089`, program `DarkSoulsRemastered.exe`
from `DSRFull`) traced the pose pipeline: `FUN_140357a20` (PlayerIns vtable
+0xA8, the detour target) -> `FUN_140320980` -> `FUN_140379f70` ->
`FUN_140435d10` copies per-bone 48-byte model-space entries (`FUN_140209a70`
composes each via quaternion->mat3 in `FUN_1408e06b0` + constant quaternion
corrections). There is **no parent multiplication in the writer**: each MdlMtx
entry is final model space, animation-hierarchy-baked, and the GPU consumes it
directly through the bone palette. This supports the model-space edit
convention, not unconditional safety: current root/copy/attachment ownership
and final rendered armor still need the qualifications below.

Statistics from the run-7 native captures (`build/native-pose-run7-snapshot.json`
32 idle samples + `build/native-pose-run7-walk.json` 32 walk samples; scripts
`native_arm_stats2.py`/`native_arm_stats3.py` in the temp dir):

| Authored invariant (c0000 arm) | idle | walk | per-frame max |
|---|---|---|---|
| UpperArmTwist(39/56) <-> UpperArm axial | ~const, span <=0.05 deg | span <=2.2 deg | 0.2 deg |
| ForeTwist(38/55) <-> Forearm axial | ~const, span <=0.01 deg | span <=6.0 deg | 1.2 deg |
| Hand <-> Forearm axial (offset-removed) | span 0.0 deg | span <=12.0 deg | 2.5 deg |
| Clavicle <-> UpperArm basis angle | span <=0.7 deg | span <=5.9 deg | 1.1 deg |
| bone scales (all arm bones) | exactly 1 | exactly 1 | - |
| long-axis convention | col 0 along bone (arm + twist) | same | - |
| UpperArm col 2 = elbow bend direction | dot = 1.000 in 64/64 | dot = 1.000 in 64/64 | - |
| link lengths | upper 0.282 m, forearm 0.232 m | same | 0 |

Skinning context (`build/native-mesh-filter-findings.md`): the base AM forearm
mesh weights to Forearm + ForeTwist + Hand (+fingers); BD torso weights to
clavicle + UpperArm. Divergence between bones sharing a mesh = visible shear.

## 2. Root causes (evidence-backed, ranked)

1. **`OrientModelHandPose` shears the authored twist pairs.** It rotates the
   Forearm subtree by the full wrist pronation angle about the elbow and only
   **half** that angle into ForeTwist about its own origin. Authored ForeTwist
   axial deviation never exceeds ~6 deg; a 90 deg controller pronation forces
   45 deg relative twist inside the AM forearm skin -> the reported forearm
   mesh twist/pinch "within the forearm itself".
2. **Full pronation lands as hand-vs-forearm axial shear.** The hand subtree
   receives only the wrist delta about the wrist, so the whole pronation also
   appears as hand twisting against the Forearm blend at the wrist joint ->
   joint pinch.
3. **`FromTo` aim parallel-transports the UpperArm basis.** The authored
   convention anchors UpperArm col 2 exactly along the elbow bend direction
   (|dot| = 1.000, 64/64 samples). Shortest-arc aiming ignores this, letting
   the upper-arm roll exceed the ~6 deg authored clavicle<->UpperArm tolerance
   -> armpit/BD torso mesh twist and an unnatural whole-arm roll look. This is
   gated by a hinge-consistency assertion; it is fixed only if it measures red
   after (1)+(2).

Legs reuse `SolveModelArmPose` (correct twist handling) and show no complaint;
the defect is arm-only in `OrientModelHandPose`, isolating the fault.


## Root/copy ownership, 22 October continuation

### Natural transfer capture, 23 October 2026

**New native evidence, not yet an implemented IK fix.** A protected, unmodded
flatscreen run captured 16 natural before/after body-copy pairs on PID 5660,
pose-owner thread 30388. The local PlayerIns and selected ChrModel were checked
at each pair. No pose/root/code bytes were written and no native function was
replayed; hardware debug registers were restored before detach. The sole recorded
non-single-step exception was the expected OS debugger-attach breakpoint.

Capture: `build/verification/embodiment-wrap/native-pose-transfer-baseline.json`,
SHA-256 `e3b459e6de9257028a62ab2019c5cfc481a4b83aa5b3160cb4ec1f194c21b866`.
The observer is retained at
`build/verification/embodiment-wrap/dsvr-pose-transfer-observe.py` (the cache
original under `hermes\cache\scratch\` may be purged); its exact SHA-256 is
embedded in the capture and was re-hashed identical to the retained copy. Unlike run 7, this observer reads
all twelve assembly-root floats at **assembly +0x20**, not +0x30. It also records
the actual mapping count/entries, resource header, proportion records, five
proportion inputs and selected source pose at both observation sites.

Observed on this character/model only:

- Source and body each have 80 palette rows, with **distinct data allocations**.
- The native copy map has **69 entries**, mapping source 0..68 to destination
  0..68. It does not copy the eleven trailing dummy/nub rows. This is measured
  identity for this capture, not permission to hardcode identity for other
  equipment/resources or assume all 80 rows propagate.
- All 16 after-copy **complete roots agree numerically**. The source palette is
  byte-identical across each copy. Body basis differences are native proportions,
  not an independently retargeted animation or a changed root.
- The resource's proportion flag (`+8 & 4`) is set; all five captured inputs are
  -0.5. Applying the recovered destination-indexed axis records reproduces all
  **13,248 mapped floats** with maximum absolute error `5.21540641784668e-8`.
  Origins are copied unchanged; authored basis-column proportions must survive.

### Per-variant transfer coverage matrix, 26 October 2026

Docs-only: the verified-copy gate (`PoseCopyValid` + `PoseCopyMatches`) accepts
any `Valid+Matches` layout, but only one resource has ever been captured. This
matrix records the one CAPTURED row and marks every other state UNCAPTURED —
a `Valid+Matches` accept on those rows is a shape check, not native
qualification. Fresh captures follow the §"Natural transfer capture" procedure
above (protected unmodded flatscreen run, no pose/root/code writes, no replay,
correctly addressed assembly root at +0x20, both palettes, map/count/entries,
resource + proportions + five inputs, before/after source pose, JSON + observer
+ receipt under `build/verification/<run-id>/`).

| Variant / resource | Map count/entries/order | Header flag + records/values | Capture SHA / status |
|---|---|---|---|
| 23 Oct resource (unnamed character/equipment) | 69 entries, identity 0..68; 11 trailing rows uncopied | `+8&4` set, 5x `-0.5`, 13,248 floats max err `5.2e-8` | `e3b459e6…c1f194b66` CAPTURED |
| Nude / any armor set / weapon+shield combo / model replacement / LOD with different counts | unknown | unknown | UNCAPTURED — fresh capture required; gate accept must not be read as qualified |
| Any resource with different map ptr/count/entries, `resource_owner`/`resource`/`records` ptr, header flag, proportion records/values, nonidentity order, holes, duplicates, or nonuniform proportions | unknown | unknown | UNCAPTURED — fresh capture required; gate accept must not be read as qualified |

Fresh read-only primary Ghidra queries and pinned instruction bytes establish:

1. PlayerIns pose slot +0xA8 is `140357A20`. It calls `140320980` at
   `140357A29`; that animation/source writer returns to **140357A2E**.
2. Only afterward does the caller obtain the selected pose (`140327A90`), copy
   the source root (`140321F80`) and invoke `140207F10` at `140357A7A`.
   The natural body-copy return is **140357A7F**.
3. `140320980` takes the actor in RCX, marks model dirty bits and tail-jumps to
   `140379F70`; its source pose is the model +0x60 and root is model +0x30.
4. `140207F10` uses the actual short-index array at map +0x10 with count +8,
   copies into assembly palette +0x60, and performs its optional native
   proportion branch. `140211940` returns `*(assembly+0x18)+0xE0` (inline
   resource); it is not another pointer dereference at +0xE0.
5. The pinned binary's proportion constants are `0.01f` at `1412DC48C` and
   `1.0f` at `141615C6C`. Translation is retained by the source-times-diagonal
   matrix operation; do not replace the body with unscaled source bases.

Raw queries remain outside distributable sources under
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\embodiment-pose-{boundary,accessors,instructions}-20261023.txt`.
The executable SHA-256 remains
`a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`.

**Implementation consequence:** solving after the outer PlayerIns update is too
late for a distinct body palette. The candidate seam is after the natural source
writer returns but **before** the existing native body copy, scoped to the
verified local-player outer update. Let native code perform its own mapping,
proportions and downstream propagation; never replay it or overwrite a final
body with guessed same-index source rows. Check source/resource ownership before
writing, and independently confirm body/root propagation after the natural
copy before publishing a successful committed pose. Preserve all stale tracking,
thread/lifetime/topology, log-only and invalid-resource refusals.

Still unverified: changed equipment/nonidentity maps, modified-candidate native
propagation, rendered armor/attachments, headset alignment/comfort and neck
behavior. The native capture does not close F1/F2/F3 or prove that a new hook is
safe. The protected-run receipt is
`build/verification/embodiment-wrap/native-capture-receipt.json`; it reports
state **Restored**, and its hashes were independently hash-checked and
recorded in the 23 October MODLOG wrap entry.


**Qualification blocker, not a green native result:** retained run-7 idle and
walk captures each contain 16 before/after pairs. All **32 after-copy samples**
have distinct model/body palette pointers. Current adapter gate `output data
mismatch` intentionally refuses that case. Its aliased CPU fixture is not proof
of working retargeting on this native copy path. Native reach measurement is
also downstream of that gate, so automatic fit may remain in fallback there.
Do not describe the source-only fit as observed native behavior.

Retained-capture audit:
`build/verification/20261022-native-contracts/retained-root-coherence.json`.
Its raw root-equality counts must **not** be read as native root divergence:
`build/native-pose-observe.py:85-91` serializes `root` from `object+0x30` for
both types, while the assembly root starts at `+0x20`. Thus `asm.root` omits
the first 16 bytes and includes unrelated trailing fields. All 32 after-copy
samples' first eight assembly floats exactly equal model-root floats 4..11,
consistent with that confirmed serializer error. The correctly addressed model
palette fixture remains byte-valid; the complete assembly root cannot be
recovered from these records. Do not rewrite captures or infer missing bytes.
The observer script was inspected only, not rerun or modified.

Primary Ghidra MCP, pinned executable/revision as in
[stereo evidence](NATIVE_STEREO_CONTRACT.md). These are static observations;
none establishes a render-only scale setter or a new native execution result.

- `140357A20 -> 140320980 -> 140379F70` rebuilds the model root from the
  animation/controller transform at `+0xA0`. `140321F80 -> 14020BEC0` copies
  `ChrModel+0x30`; `140207F10` separately feeds assembly `+0x20` and its palette.
  A shared palette pointer alone cannot establish a shared coordinate frame.
- `140209820` compares twelve floats numerically before root copying. Native
  `+0/-0` equivalence matters: a byte comparison between the two owners would
  falsely reject a root the native copy path legitimately left unchanged.
- `140209BD0` copies assembly `+0x20`; its callers include attachment
  propagation (`140207510/1402079B0`) and geometry-query wrappers
  (`140208E50/1402090D0`). These consumers prevent treating it as a proven
  visual-scale-only slot. This is not proof that a particular collider is scaled;
  the physics/query separation is still unknown.
- Second feed `1407051D0` also calls `140207F10`, `140207510` and `1402079B0`.
  Player propagation `140357AE0` invokes the latter two. Static caller lists
  cannot establish every actor/phase in which they execute.

`arm_ik.cpp` now checks model and assembly roots before retargeting, then
rechecks each owner's unchanged snapshot before palette commit. Compare the
**full `root_before`**, not `root`: the latter deliberately has its translation
removed for target inversion. The integration regression's nonzero root caught
this exact implementation mistake. The guard never writes either root.

Boundary regression covers root translation/scale/NaN mismatches, signed-zero
equivalence, and assembly-root replacement after the first palette snapshot.
First RED: `FAIL 421: writes==old_writes`; after the snapshot correction,
`arm_pose_native` and 50 focused repetitions pass. Current build receipts and
candidate hash live in MODLOG; these fixture results do not qualify live skinning.

Qwen: if live gate `assembly root mismatch` stays red, record both complete
roots, palette aliases and natural phase; do not delete the guard or copy one
root over the other. A visual-scale route must retain a verified render owner
through body, armor and attachments without altering collision/query owners.
For the retained distinct-palette path, first recover `140207F10`'s index map
(`param_4+8` count, `param_4+0x10` short-index array), source pose accessor and
optional proportion branch. Then choose a qualified before-copy source seam or
correctly mapped final-body owner. Equal counts/matrix values do not establish
identity ordering; writing the source after copying is not a propagation fix.

## Current revalidation requirements

### Committed-pose read lifetime, 25 October continuation

An adapter regression reproduced a gap after the natural-copy check: losing
focus before the next native/camera callback still let `ArmIkLatestHands`
return the previous valid weapon poses (`post-publication reader 0 mutation 1
returned stale poses`). This is separate from mutations during the copy.

Both public readers now share tracking/focus/controller, timestamp, camera
sequence/epoch, calibration revision, mode and input-capture validation.
`ArmIkHands` carries its committed calibration revision. A failed read ends
the receipt and clears diagnostic motion history; restoring external state
cannot revive it, and the next verified natural copy starts at zero speed.
Reads leave a provisional source snapshot intact while the native copy is
still in progress, and never write native memory or call native updates.

`CheckHandPublication` exercises 19 transitions independently through each
reader, checks empty outputs/no native writes or calls, then checks refusal
after external-state restoration and recovery through a fresh natural copy.
A newer valid XR publication is accepted with the still-current camera-bound
pose. Existing head-only, one-hand, duplicate-callback and proportion-copy
regressions remain active. This is source evidence only: modified-candidate
native propagation, armor/attachments and headset alignment remain open.

### Post-copy publication invalidation, 24 October 2026

The source guard did not cover the native interval between its write and the
outer hook's publication. Reproduced at the real two-hook adapter seam: changing
calibration revision after the natural copy, then restoring it before reading,
still exposed `ArmIkLatestHands` (`post-copy mutation 1 published a stale receipt`).
This was a source defect, not evidence that the hook has executed live.

`VerifyNaturalCopy` now rechecks tracking validity/freshness, applied-camera
sequence/epoch, calibration revision/world trim, enabled/log-only/observer modes,
menu/calibration ownership, source selection/space/count, rig resource/topology,
and equipment availability/IDs/stance before accepting the body copy. Native
code still owns the copy and all final-body/root bytes: a post-copy refusal
invalidates hand/head/bench publication, not an unsafe late palette rollback.

The adapter regression covers 22 mutations during that native interval, restores
the external state before querying publication, requires refusal, then requires
a new natural source solve to recover. Both fresh configurations pass all 93
registered tests; five relevant tests repeated 50 times in Debug also pass.
Authored naturalness limits and native write boundaries are unchanged. F2's
live propagation/armor/attachment/headset qualification remains open.

### Focused source/body proportion audit, 22 October wrap-up

Audited **32 after-copy palettes x 80 rows** from the retained idle/walk
captures, without executing the observer or modifying captures. Every one of
the **2,560 same-index row origins is numerically unchanged** across the copy.
51 rows per palette are byte-identical; 29 have changed bases. Per-column
scaling explains all sampled basis differences with maximum residual
`3.485143e-8` and maximum fitted-factor span over frames `6.731061e-8`.

Rounded compatible factors (not a new measured skeleton or remap table):

| Basis-column factors | Source/body same-index rows |
|---|---|
| `(1, .95, .95)` | 2, 16, 17, 18, 26, 43 |
| `(1, .925, .925)` | 3, 4, 5, 7, 8, 9, 10, 12, 24, 25, 38, 39, 41, 42, 55, 56 |
| `(.925, 1, 1)` | 6, 11 |
| `(.975, .975, .975)` | 19, 37, 54 |
| `(1, .975, .975)` | 23, 40 |
| `(1, 1, 1)` | Remaining 51 rows, including unused/ambiguous rows |

Pinned native transfer `140207F10` explains this shape: source index `i` maps
through `*(player+0x858)`'s short-index array (`+8` count, `+0x10` data) to a
destination row. `140327A90` tail-dispatches model vtable `+0x10`, whose pinned
`ChrModel` implementation `14020BF50` returns model `+0x128`, the selected pose.
Source accessor `140209BF0` copies a 48-byte row from that pose's data `+0x10`.

`140211940` resolves body resource `*(assembly+0x18)+0xE0`. When its `+8 & 4`
is set, `140207F10` reads destination-indexed four-byte proportion records from
that resource's `+0x18` pointer. A record's high nibble selects one of five
values at `player+0x860`; low bits select affected axes and signed coefficients
select the factor. `1400CCF10` constructs a diagonal matrix; instruction/dataflow
through `140194030 -> 140194E80` composes **source times diagonal scale**.
Translation is retained, matching the capture audit. Field lifetime/table
coverage and the actual live remap/records were not captured by run 7.

**Do not confuse this with stature/reach fitting.** This sampled native branch
changes bone-basis proportions/mesh shape, not joint locations or arm-link
lengths. Copying solved source rows over the final body would erase authored
proportions; changing these factors cannot by itself put the shoulders/wrists
at a different anatomical reach. It also has query/attachment consumers, so
render-only ownership is still unproven.

The same-index audit is compatible with identity mapping for these samples,
**not proof of the actual map**: zero/duplicate poses are ambiguous, and active
equipment/topology can change. Do not hardcode identity or infer missing root
bytes. Preserve native per-row Gram/scale when transporting rigid corrections.

Local-only numerical/static receipt:
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\melee-body-handoff-20261022T213421Z.json`,
SHA-256 `1eec629dc1e840974da52793aa43ac1ef29340060800d5b6ab0a96775e0925a9`.
Idle capture hash is recorded below; walk capture SHA-256 is
`9966969419b362afb8282f8279e6c29ad721d2999fa8b914c8060ad0c81fbef1`.

**Qwen's next F2/F3 probe:** obtain a correctly addressed, bounded natural
snapshot of selected source, assembly root `+0x20`, both palettes, map pointer /
count / entries, body resource/proportion records and five inputs before/after
copy. Prove mapping direction, extent and generation, then choose a qualified
before-copy source seam or final-body rigid-delta transfer. Test nonidentity
maps, holes/duplicates, changed resources and nonuniform native proportions.
Independent stature/reach fitting still needs a separate verified visual owner;
do not expand the alias guard or scale the collider to disguise that gap.

22 October numerical provenance recheck: the retained
`build/native-pose-run7-snapshot.json` exists (32 samples). Repacking all 960
single-precision values in `tests/native_idle_pose.inc` exactly reproduces
sample 0's 3,840-byte model-space palette. Reproduce with
`python tools/verify_native_pose_fixture.py build/native-pose-run7-snapshot.json`.
Capture SHA-256: `52f9170f8812e7bc5dcbfdb87740c14fdea3e8b3f7339e0ed6561adfcc48c747`;
palette SHA-256: `6d3e375a9b3da2f0f855216c3d155abfe431405db143645166bd4c68a3dff9c3`.
The retained walk capture also exists, but this comparison does not establish
walk replay, hierarchy provenance or a new native/headset qualification.

Current source fixes include deterministic capped antipodal bends, a nonzero
conditioning blend between opposite sides, fixed-point pole smoothing, and
transactional clavicle bend state. Duplicate callbacks cannot advance a pole
with zero elapsed time. Supplying resized calibrated lengths to a rigid-only
transport now refuses, rather than reporting a wrist the written palette did
not achieve. Existing naturalness bounds remain unchanged.

The preceding design proposed applying pronation consistently to authored forearm/twist pairs, then applying a twist-free wrist remainder, preserving bone lengths and hand target alignment. Upper-arm hinge correction was conditional on an independent failing hinge assertion. These are evidence-backed hypotheses to compare against the current implementation, not permission to overwrite current arm work or repeat a fix blindly.

Relevant fixture: tests/test_arm_ik_naturalness.cpp and tests/native_idle_pose.inc, based on captured run-7 idle matrices/parents. Verify provenance, current registration in tests/CMakeLists.txt and actual production path through wrist-target construction, clavicle/arm solve and hand orientation. Retain independent assertions for axial shear, link lengths, continuity, grip alignment and elbow hinge conventions.

Final acceptance requires focused adapter tests, integrated suites and rendered/headset inspection of arms/armor during pronation, overhead/cross-body motion, walking and equipment changes. Test-command names must come from current CTest inventory. Native runs follow TESTING; the old plan's additional run-approval restriction is superseded. Human visual/comfort verdicts cannot be replaced by CPU matrices.
