# Native arm pose evidence and revalidation

Consolidated: 21 October 2026. Evidence reference only. [Authority](../README.md#authority-and-maintenance); implementation belongs to execution milestone F, with current status in MODLOG and the execution ledger. This file does not add task order, approval gates or restrictions on authorized testing.

The following measurements/findings were recorded by the preceding arm investigation. They have not been remeasured by this documentation cleanup. Their hypotheses describe the observed candidate; ongoing source changes may already address them. Inspect git diff and actual tests before treating any fix as pending or any test as expected to fail.

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
