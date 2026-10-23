# DSVR native conversion: execution guide

Date: 2026-10-21. Updated: 2026-10-23 (user-directed D1/D2 stereo feasibility first; focused work package below). Status: the only active execution plan, not implementation or acceptance evidence.

Documentation authority: README.md owns scope and authority; this guide owns task order and completion tracking; ENGINEERING.md owns architecture; TESTING.md owns operating policy; VERIFICATION_RUNBOOK.md owns acceptance procedures and numerical thresholds. Direct user instructions take precedence. History, research notes and handoffs do not override those owners. No superseded plan/specification adds a parallel sequence or approval gate.

## 1. Assignment and authority

Complete Dark Souls Remastered's native VR conversion in the existing C++20/D3D11/OpenXR mod. Deliver independent tracked 6DoF viewing, true stereo, native avatar embodiment, physical melee and defense, collision-aware roomscale movement, usable VR interactions/UI, ranged aiming, lifecycle recovery and campaign compatibility. Native conversion is the user's fixed direction.

Keep DSR's world, AI, progression, equipment, damage rules, stamina, poise, collision and actor lifecycle authoritative. Replace the local player's interfaces to those systems. Do not substitute gesture-to-attack buttons, direct HP subtraction, a duplicate avatar, synthetic enemies, mono copied into two eyes, or an engine remake for the requested result.

This guide supplies the sole execution order and concrete checkpoints. README.md owns the product contract; docs/ENGINEERING.md owns architecture; docs/TESTING.md owns protected operation; docs/VERIFICATION_RUNBOOK.md owns all acceptance cases and numerical thresholds; MODLOG.md owns the current handoff. MODDING_PLAN.md and docs/NATIVE_MELEE_CONTRACT.md index native research; docs/ARM_IK_EVIDENCE.md retains the arm investigation. Superseded plans/specifications are retired; their text is historical evidence only under docs/history. Do not create another competing roadmap.

Unknown native contracts are research tasks, not implementation details to invent. No fixed completion date is defensible until the native combat and stereo gates pass. This guide makes no claim about the model's capabilities or context limit; its bounded work units apply regardless of provider.

## 2. Starting facts and things to recheck

- Existing source includes XR mono presentation, camera/heading, calibration, native pose editing, equipment reads, input remapping, combat kinematics, a diagnostic contact bench, UI/config and verification tools.
- Production true stereo and native physical damage/block/parry are not accepted. XR eye scheduling APIs and projection math do not establish native eye rendering.
- The diagnostic blade length and target proxies are not production weapon geometry or enemy collision evidence.
- Native root follow, head queries, dynamic crouch collision and hurt-volume changes require verified ownership and native qualification.
- Head/Controller locomotion selection already has implementation and recent commits. Revalidate it; do not reimplement from an older plan's baseline table.
- Historical suite counts disagree because the documents describe different candidates. Obtain fresh results; never copy 76/84/86 as the current count.
- The checkout contains ongoing arm IK/pose code and test changes plus native-idle/naturalness fixtures. The earlier arm investigation is now docs/ARM_IK_EVIDENCE.md; the superseded combat plan's acceptance matrix is now in VERIFICATION_RUNBOOK.md and its exact prior text is in history. Preserve ongoing code work. Determine what already works before editing. Do not reset, clean, overwrite, or commit unrelated user work.
- Historical performance varies by runtime, scene and candidate. Establish a fresh comparable baseline; do not report the older 15, 35 or 53 FPS observations as current measurements.

## 3. Mandatory working loop

Use one narrowly scoped task per implementation turn. A task is one observed failure, one contract question, or one integration boundary, not an entire milestone.

1. Read the current handoff and this guide's next unmet gate. Inspect git status/diff and current files.
2. Write the task objective, prerequisite evidence, expected observation and rollback boundary in the active task record.
3. Discover code with codebase-memory-mcp: search_graph, trace_path, get_code_snippet, query_graph/search_code. Use get_architecture for orientation. Index the repository if absent; refresh after material edits before relying on graph snippets. Use rg for docs, logs, config literals or insufficient graph results.
4. Read the actual caller and consumer before changing a boundary. Identify thread, lifetime, coordinate space, units and update frequency.
5. For a defect or native adapter, add a meaningful regression at the actual integration boundary where feasible. A helper test alone cannot prove the adapter uses it correctly. Avoid redundant tests for trivial text/config edits.
6. Implement the smallest complete slice. Use existing modules. A new module is justified by a distinct owner/lifetime, not by a desire to reorganize the project.
7. Run focused checks. On success, run the milestone's integration checks. Record exact command, exit code and evidence paths.
8. Exercise native behavior where required using the protected procedure. Package hashes must match the candidate under observation.
9. Update the existing evidence owner and MODLOG with result, unresolved fact and exact next action. Commit only task-owned files when appropriate; stage explicit paths, never all changes blindly.

Do not repeatedly ask for permission already granted in the testing policy. Human headset comfort/feel requires the operator; publication remains separately authorized. Planning itself does not deploy or launch.

### Task and handoff record

Use this compact format in the existing handoff or a run's results.md:

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

Never infer PASS from missing errors. BLOCKED means a named prerequisite is unavailable, not that the feature is finished. Continue independent useful work when a gate cannot run. Before context rollover, record command completion, active processes, install/save restoration state, and exact next probe. Do not restart broad reconnaissance on resume.

## 4. Native investigation protocol

Each new native call or write needs: pinned executable identity and instruction bytes; calling convention and complete arguments; receiver/type; field meanings and bounds; constructor/destructor or owner; pointer lifetime and actor generation; simulation thread/update phase; side effects; refusal behavior; independent runtime correlation.

Primary static source: C:\Tools\DarkSoulsRemaster_De\analysis-full\DSRFull.gpr, program DarkSoulsRemastered.exe. The analysis-full-attack project is a repair workspace, not the primary evidence source. Preserve the database and shipped binary. Read the current native-melee contract before revisiting any address.

Use the installed REA route first where it works. Existing documentation records its Windows native-provider security gate as unavailable; do not bypass it or retry the identical unavailable path indefinitely. The existing tools/ghidra/query.ps1 read-only route and separate GhidraMCP GUI bridge are documented fallbacks. Verify current availability and command parameters before use. Do not pass the .gpr launcher or .rep directory as a standalone binary to a tool that cannot read Ghidra projects. Keep derived decompilation/game assets outside distributable sources.

Recover split continuations from instructions and callsites, not truncated pseudocode. Prior rollback-mismatch outputs are not verified ABI evidence. Health setter 0x140322910 is an observation anchor, never an attack API. AI AddTopGoal/SetAttackRequest and generic event queues are not proven player melee dispatchers.

After three materially identical failures, record the failure signature and change the evidence source, probe or hypothesis. Do not suppress assertions, widen guessed pointer scans, invent offsets, or report an unavailable binding as implemented.

## 5. Milestone order

Execute sequentially unless the user explicitly requests delegation. Read-only investigation can advance a later milestone without enabling its mutation path.

1. A: reproducible baseline and preserved current work; read-only D research need not wait for a native scene run.
2. D1/D2: resolve native stereo feasibility first, using the focused work package below.
3. F2: unblock actual source-to-body pose transfer; then B: observed native melee contract and C: first physical sword hit.
4. D3-D7: production stereo integration; E: collision-resolved movement; remaining F: coherent body/crouch; G: complete sword/shield encounter.
5. H: interactions/UI; I: weapon breadth and ranged aiming; J: campaign compatibility.
6. K: final performance, lifecycle, packaging and release qualification.

A first physical hit may be developed in the existing mono diagnostic route. It cannot qualify a playable VR release. The first integrated encounter requires C, D, E, reachable-pose/body alignment from F, and G. Full campaign completion additionally requires every remaining gate, including full F requirements. Do not confuse an intermediate milestone with completion of the mod.

## A. Establish a fresh baseline

Entry: checkout available. Owners: build/config/tests, existing protected-run tools and documentation.

- [ ] A1. Record git HEAD, status, task-owned diff boundaries, toolchain, executable hash, runtime route, graphics settings and candidate config. Read the current documents listed in README, including the native-melee and arm evidence notes. Inspect previous evidence rather than rerunning already settled experiments.
- [ ] A2. Build/test current Debug and Release. Separate pre-existing failures from new failures with a saved diff and reproducible command. Preserve ongoing arm changes; compare their actual implementation with docs/ARM_IK_EVIDENCE.md and milestone F before deciding what remains.
- [ ] A3. Verify protected save/install transaction readiness using current TESTING instructions. Resolve discrepancies before native deployment. Preserve both save roots and Steam metadata; retain exact restoration receipts. Do not restart Steam unnecessarily or take over active user input.
- [ ] A4. Capture one fixed native scene without the mod, then the same candidate/scene with mono and IK off/on. Record CPU stage distributions, full-scene GPU coverage, waits, copies, Present and actual XR period. Avoid mixing VDXR and SteamVR comparisons.
- [ ] A5. Select a disposable character, fixed enemy, weapon/shield and repeatable encounter. Record equipment/PARAM identity and initial actor/state conditions.

Exit: reproducible candidate, regression baseline and protected native evidence. No performance or capability claim from config alone.

## B. Recover real native melee from an actual encounter

Entry: A. Owners: combat_native.*, existing trace tools, docs/NATIVE_MELEE_CONTRACT.md.

- [ ] B1. Capture ordinary native hit, miss, block, parry and invulnerable/dodge cases with original controls as comparison experiments. Record simulation update, attacker/target identity, equipment, contact/event identity and observable HP/stamina/reaction changes.
- [ ] B2. Trace backward from a confirmed native outcome and forward from the actual weapon-contact producer. Prefer bounded observation of known callsites over another broad function-name search. Determine which event variant and actor-type dispatch belong to melee.
- [ ] B3. Recover attack context creation, attack/PARAM selection, ownership, target generation, contact geometry, defense flags, scheduling, outcome and cleanup. Establish where the attack animation supplies context versus merely supplies pose; physical combat must replace pose/contact dependence without losing legitimate native state.
- [ ] B4. Compare the five cases to identify block/parry/invulnerability decisions. Confirm native side effects and exactly-once bookkeeping, including stamina, poise, status, durability and aggro where applicable.
- [ ] B5. Choose the smallest evidenced integration seam: adapt tracked contact into native melee; otherwise construct a context through verified native constructors; otherwise replace the local melee update's animation-dependent geometry. Document why the preferred seam can or cannot work.

Exit: complete contract with static and native correlation, not just a plausible address. If unavailable, keep B open and pursue D feasibility or other independent work. Do not expose an enabled physical-damage option.

## C. One genuine physical sword hit

Entry: B contract accepted. Owners: combat_native.*, combat runtime/contact boundaries located through the graph, arm_ik.* publication and existing combat tests.

- [ ] C1. Obtain real equipped blade/handle dimensions and axes from verified native geometry/data. Include weapon/model replacement and actor generation. Remove production dependence on the .9 m diagnostic assumption.
- [ ] C2. Publish immutable committed weapon pose with tracking time/sequence, epoch, calibration revision, equipment/stance and native update ID. Bind physical swing energy to valid head-relative tracking; bind contact geometry to final rendered/native weapon pose. Render callbacks cannot advance combat twice.
- [ ] C3. Implement continuous swept geometry including fast rotational arcs, world obstruction and actual target volumes. Use the verified native collision path where available; justify and test any bounded subdivision. Prevent hits through walls and unrelated actors.
- [ ] C4. Add swing/contact episodes with rest rejection, deliberate slash/thrust qualification, per-target rehit rules and reprime after teleport, tracking loss, recenter, model/equipment/stance change, menu or death. Walking and artificial turning alone must not create swing energy.
- [ ] C5. Submit one qualified contact on the native simulation owner. Preserve native parameter selection, costs, defenses and effects. Ensure the original collision path cannot duplicate the same physical hit; do not disable unrelated actors or native gameplay updates.
- [ ] C6. Observe the native result independently; emit haptics only from a correlated confirmed outcome. Keep gameplay and outcome observation out of an expanding pose/render hook.
- [ ] C7. Run ten deliberate hits and ten misses, stationary overlap, walking with still weapon, wall interception, target replacement and duplicate render visits. Compare with B's native baseline.

Exit: one sword produces real attributable native outcomes, misses produce none, resting overlap never farms damage, and repeated rendering never duplicates costs/reactions. This is the first major feasibility proof.

## D. True native stereo and coherent 6DoF presentation

Entry: A; read-only feasibility may run while B is unresolved. Owners: xr.*, camera.*, native render hook/host modules found through graph, stereo tests and tools/native_stereo.py.

- [ ] D1. Reuse retained RenderDoc and command-stream evidence. Identify the active gameplay camera/view consumer, worker ownership and safe per-view render bracket. Prove which code advances simulation versus emits/executes draw work. Inspect the actual command consumer; projection math alone is insufficient.
- [ ] D2. Establish view-only replay feasibility with one bounded native experiment. Prove two camera/projection inputs can render one unchanged simulation state without extra animation, AI, collision, event delivery or Present. Record the result before building a larger replay abstraction.
- [ ] D3. Connect runtime prediction and asymmetric eye views/projections before command production. Use one physical/world scale and correct IPD; preserve captured depth conventions. Keep full physical head rotation plus artificial yaw and direct head translation.
- [ ] D4. Give eyes distinct color/depth targets and correct view-dependent resources. Audit culling, shadows, reflections, particles, transparencies, screen-space effects, temporal history and HUD. Shared resources require evidence of view independence. Render-only pose differences must not mutate gameplay state.
- [ ] D5. Wire existing XR frame tokens and image ownership to real rendered eyes. Submit only complete, matching, fresh pairs; release/cancel on failure, resize, focus loss or tracking epoch change. Preserve D3D11 state and exclusive XR frame ownership.
- [ ] D6. Capture left/right outputs with near/far occluders and asymmetric markers. Prove geometric disparity, eye order, scale, roll and physical translation; different texture handles alone are not proof. Exercise menu/cinema transitions and tracking recovery.
- [ ] D7. Measure complete eye-pair CPU/GPU time. Fix measured bottlenecks individually. Reprojection is an optional latency/performance tool; mono reconstruction or AFR cannot close the same-simulation stereo gate. If view replay is invalid, investigate an earlier native per-view emission seam rather than duplicating the whole game update.

Exit: two genuine native views of one simulation state with correct prediction, resources and lifecycle; headset fusion/scale accepted. Final 90 Hz performance remains mandatory at K.

### Stereo feasibility work package

Current user-directed focus, 23 October 2026. This refines D1/D2 inside the sole
execution guide; it does not authorize an unproven hook or mark either gate PASS.
Deliver one evidenced decision: a named two-view seam with a defensible lifetime,
or a demonstrated reason to reject this consumer and move earlier in native
command production. Rejecting one seam does not prove native stereo impossible.

- [ ] **D1a - Pin the evidence and preserve the candidate.** Record HEAD, dirty
  paths, shipped executable hash, current package/config and retained capture
  identities. Reuse existing RenderDoc/command-stream evidence only where its
  scene/build coverage is known. The saved 88/88 receipts are historical, not a
  fresh baseline for future instrumentation. Read the updated
  [stereo contract](../../NATIVE_STEREO_CONTRACT.md) before querying addresses.
  REA MCP currently lacks its configured Ghidra/Java environment; CLI doctor
  reports native admission healthy. The primary read-only query route succeeds.
  No database repair, import/reanalysis or game launch is
  needed to begin. Output: a provenance record and an explicit evidence-gap list.
- [ ] **D1b - Recover the natural executor and both completion-counter owners.**
  Reference classification is now static evidence: `0x141E370D8` is a PE
  unwind entry; `0x142BBA7FC/0x142BBA803` belong to one unanalysed
  stack-mediated control-transfer sequence, not a task table. Direct caller
  `0x1411C4FA0` is `HGDrawPlanEntity` vtable `0x14160EA80` slot `+0x50`
  and builds a stack-local `HGDrawPlanExecutant` with scratch. Follow the
  inline entity construction at `0x1411BFED3/DA` to its renderer input and
  the polled objects; see the current stereo evidence note. Recover the owner's
  constructor, enqueue/execute/cancel/retire paths and the meaning of the
  consumer result stored in task `+0x28/+0x2C`. For each object in
  `*(owner+0x68)+0x40/+0x48`, identify allocation, every initialization/reset,
  increment/decrement of its `+0x20` counter, worker callback and destruction.
  Track `0x140CC3F80` in that object flow; do not infer GPU completion from an
  atomic helper. Output: two separate counter-owner records and a task/worker
  timeline, including errors/cancellation. Missing indirect edges remain open.
- [ ] **D1c - Close view, storage and side-effect ownership.** Trace the actual
  main-scene camera/plan through the natural callsites `0x1411BEA27`,
  `0x1411C5049` and `0x1411C2F6C` into consumed constants and draws. Preserve
  the third-argument entity handle; it is not an eye index. Use pinned bytes and
  the complete unwind interval `[0x1411CD8A0,0x1411CE029)`, not truncated void
  pseudocode. Account for scratch cursor consumption, parser/items, constructed
  contexts, callbacks, return value and final destruction. Separate CPU worker
  retirement from D3D11/GPU readiness. Identify view-dependent culling, target,
  depth, shadow/reflection and temporal-history ownership; enumerate every
  simulation/event/presentation side effect. Output: an ownership table naming
  what can be retained, rebuilt per eye or cannot safely be replayed. Unknown
  fields are blockers, not guessed retention or camera-restoration points.
- [ ] **D1d - Observe one natural main-scene invocation, without replay.** Only
  after the candidate observation ABI/phase is known, prepare bounded diagnostic
  instrumentation using existing camera/render/XR trace owners. First rebuild
  and test that exact candidate, reconcile current preflight/package and follow
  TESTING's protected save/install transaction. Log simulation/load/tracking
  identity, task/camera handle, thread, command and scratch ranges, both counter
  lifecycles, camera consumption, CPU retirement and independently observed GPU
  readiness. Include final scene coverage, not merely a menu/shadow/leaf draw.
  Choose a capture deadline and exact-PID cleanup before launch; persist dropped
  interval/coverage flags. Output: a natural native trace that confirms or
  falsifies D1b/D1c. No repeated dispatcher, gameplay call or eye submission.
- [ ] **D2a - Attempt exactly one two-view transaction, only if D1 closes.**
  Predeclare independent observation points for input, physics, animation, AI,
  collision, combat/events, Present and temporal-history advancement. If their
  coverage or any owner/lifetime remains unknown, record BLOCKED rather than
  running replay. Retain or rebuild verified per-view inputs/resources before
  command production, using one predicted tracking/simulation identity and the
  existing applied world-scale mapping. Render distinct asymmetric eye views
  into separate color/depth resources, wait for all relevant workers and GPU
  work, copy each completed eye, then retire resources at the proven boundary.
  No extra simulation/event/history/Present advancement between eyes; normal
  frame/history commit occurs once afterward with verified per-eye history.
  Capture near/far occluders and asymmetric markers from both consumed views
  through final outputs. Missing geometry, stale/aliased resources, extra state
  advancement or incomplete trace coverage defeats the claimed result. On any
  error cancel the whole pair and restore only owned state; never submit a
  partial pair or retry an unproven call in a loop.
- [ ] **D2b - Record the decision and stop expanding the experiment.** A native
  feasibility PASS requires a complete evidenced two-eye transaction from one
  simulation state with correct lifetimes and no forbidden side effects. It is
  not production stereo, headset acceptance or a 90 Hz claim. FAIL identifies
  the observed side effect/lifetime/resource violation; BLOCKED names the
  unavailable owner, trace coverage or protected-run prerequisite. If this
  consumer is destructive or view data is already baked, trace backward from
  its lexer/task inputs to the nearest earlier per-view producer and repeat
  D1 there. Do not replay `0x1411BE710`, patch no-return flags, relax the
  safety gate or build a generic replay abstraction around an unknown ABI.

**First next probe (read-only, no game):** inspect inline
`HGDrawPlanEntity` construction and trace its `+8` renderer input. The old
wrapper DATA slot is verified unwind metadata. Native RTTI and a stack-local
executant lifetime are recovered; task dispatch, both counters and live
main-scene coverage remain unresolved.

```powershell
& ./tools/ghidra/query.ps1 -Address @('0x1411BFE80','0xD0') -Script DumpInstructions.java
```

**Implementation boundaries, after the evidence gate:** review the actual
camera consumer in `src/core/camhook.cpp` / `camera.cpp`, host lifecycle in
`render.cpp` / `render_host.cpp`, scheduling in `xr.cpp` / `xr.h`, and
`stereo_render.h` / `stereo_gate.h` before choosing a hook. These are existing
owners to reuse, not permission to modify every file or invent a render address.
Keep production replay disabled until a verified native bracket exists.

**Verification and receipts:** for source/instrumentation changes, discover
registered tests with `ctest --test-dir build -C Release -N`, then build Debug
before Debug CTest and Release before Release CTest using section 6's commands.
Exercise relevant stereo/XR/host/camera cancellation and lifecycle regressions;
`tools/native_stereo.py` and fake-XR tests validate synthetic behavior only.
Before native deployment, refresh candidate/package hashes and reconcile
`tools/verify_preflight.ps1` with TESTING's complete protected transaction;
its current readiness checks alone do not prove save/Cloud isolation. Record
native traces under `build/verification/<run-id>/`, derived disassembly outside
distributable sources, exact commands/exits/hashes and restoration receipts.
Update this ledger, the stereo evidence note and MODLOG after each bounded task.
Planning-only documentation changes do not rebuild, deploy or launch the game.

**After the feasibility decision:** resume F2's distinct source-to-body palette
transfer: capture the real bone map, correctly addressed assembly root and
proportion records before qualifying a before-copy or mapped final-body write.
Then pursue B/C's newer enemy damage-dispatch lead at `0x14032F548` with natural
hit/miss/block correlation. Do not replace either research gate with solver
retuning, generic health setters or UI/packaging work. D3-D7 remain required
before an integrated native-VR claim, regardless of the D1/D2 verdict.

## E. Native collision-aware locomotion and head obstruction

Entry: A; movement changes require their own verified motor contract. Owners: input.cpp, camera.cpp, heading/roomscale math, config/menu, native query adapter if needed.

- [ ] E1. Revalidate existing Head default and Controller mode, selected/non-dominant hand, vertical-axis hysteresis, neutral/rearm and persisted migration. Do not replace implemented behavior from stale plan text.
- [ ] E2. Trace XInput movement consumption through the real local-player motor to resolved root movement. Establish units, analog speed mapping, update timing, collision response, root ownership and forced motion. Existing XInput import/caller evidence alone does not prove this chain.
- [ ] E3. Compose bounded physical walking and stick input while sampling continuously during turns. Attribute resolved motion to the physical request; define clipping/saturation, blocked movement and knockback handling. A blended analog vector does not by itself identify physical motion: if the motor provides insufficient attribution, recover a better command/result seam before enabling reconciliation.
- [ ] E4. Reconcile only accepted physical follow against tracked camera offset. Prevent double travel, stored-debt lunges, orbit on snap turns and root motion mistaken for head movement. Turning pivots about current horizontal HMD position. Retain direct tracked view response; do not hide motor lag by delaying the camera.
- [ ] E5. Verify world solid/support queries. Sweep the head volume and weapon/interaction reach through thin walls, corners, doors and ceilings. Obscure invalid imagery with hysteresis and return guidance; comfort Off cannot disable obstruction protection. Test crossing entirely through a thin barrier and recentering while obstructed.
- [ ] E6. Qualify all movement thresholds/cases in VERIFICATION_RUNBOOK.md: heading, physical path, follow latency, walls, stairs, slopes, ledges, mixed input, full turns and native knockback. Record saturation and any revised threshold rationale before testing.

Exit: collision-resolved physical/stick movement with no doubled motion, unintended steering, wall reveal or through-wall reach. Direct actor/root translation is not an alternative completion route.

## F. Embodiment, calibration and real crouching

Entry: native pose mapping evidence from A; E required for final integration. Owners: arm_ik.*, arm_pose_math.h, body_pose_math.h, arm_ik_math.h, head_rider.h, calibration, weapon grips and visibility.

Solver strategy (user decision 2026-10-22): FABRIK is the primary reach-chain solver, replacing analytic-only two-bone IK. Implement FABRIK (Andreason & Baath) over the native model-space palette: forward/backward passes on joint origins along each bone chain, then per-bone rigid delta rotations applied to the pre-write native basis with subtree origins carried rigidly. Never rebuild a bone's basis by look-at: rigid deltas preserve scale, exact link lengths, authored twist-pair relationships and hinge conventions by construction. Bounded iterations (a pole-driven two-three bone chain converges within 2-3 full passes); deterministic per frame because the chain is always initialized from the fresh native palette. The bend plane comes from the existing head-space ArmPoleSmoother, which FABRIK's backswing consumes unchanged; optional FABRIK-J angular clamps stay about the authored joint axes (elbow hinge, clavicle share). FABRIK is position-only: end-effector orientation remains the separate exact pass (OrientModelHandPose and the grip contract). The hdFa style hand-axial naturalness failures live in that orientation pass at extreme wrist fold, not in the reach solve; FABRIK does not absorb them and they stay an F1 subproblem. Acceptance invariants are unchanged: arm_ik_naturalness keeps twist pairs <=6/5 deg, hand-forearm axial <=15 deg, hinge >=0.866, exact link lengths and <=12 deg per-frame basis delta. If FABRIK breaks one, the solver is defective; thresholds do not move. Keep SolveTwoBoneIk as a characterization reference/unit test, not a silent second production path.

- [ ] F1. Implement the FABRIK chain core in arm_pose_math.h (origin passes, rigid subtree delta application, pole-driven backswing, bounded iterations, authored-axis angle clamps), swap the arm solve call sites to it, and finish the in-progress pronation/naturalness fix against independent native idle/walk fixtures and final rendered armor. Preserve twist-pair relationships, bone lengths and hinge conventions. Recheck the untracked fixture provenance. A mathematically correct wrist target can still shear the mesh.
- [ ] F2. Prove final pose write/copy/skinning/attachment order. Define owned bones and rebuild affected descendants once. Preserve native root/physics and required gameplay animation updates; prevent later writers overwriting the committed body.
- [ ] F3. Separate stature, arm reach, floor, seated posture, avatar visual fit and world scale. Resolve conflicting old proposals for global/native collider scaling against the current contract: do not uniformly scale gameplay collision merely to fit visuals. Verify any collision-size adaptation at its native owner. Never change stereo scale to disguise bad hand reach.
- [ ] F4. Solve pelvis/torso/shoulders/arms with bounded twist and reach, using the FABRIK chain core where chains exceed two bones (clavicle share, and the leg chains replacing the bespoke leg solve once F1's core is accepted). Estimate legs/feet from native ground support and resolved movement; stabilize foot planting, steps, stairs and full turns. Estimated feet are not measured tracking. Body orientation cannot rotate the HMD view.
- [ ] F5. Independently verify native face/headgear/hair filtering and armor variants. Do not collapse bones to hide geometry. Forearm-only display is optional unless the supported configuration requires it; a broken option must be unavailable and explained.
- [ ] F6. Recover the movement collider's height/center setter, clearance query, and hurt-volume consumers. Apply physical crouch with floor-anchored collision, consistent visual body and native hurt geometry. Prevent expansion under ceilings. Looking down is not crouching; no synthetic invulnerability or view-only ducking.
- [ ] F7. Qualify the runbook hand/eye alignment and settled-foot thresholds, then crouched clearance and controlled projectile avoidance. Test unreachable poses, overhead/cross-body swings, full turns, armor swaps, two-hand support, death/ragdoll/scripted ownership and recovery.

22 October source checkpoint: FABRIK opposite-bend/conditioning and held-pose
stability fixes, transactional bend state, and coherent reach-based automatic
world/native-avatar sizing are implemented with offline boundary regressions.
The applied camera mapping now owns arm/grip/roomscale conversion; T/A capture
estimates anatomy from full controller geometry. F1/F3 remain open for rendered
armor/attachment qualification, native independent mesh-scale ownership and
full stature/reach fit; the existing stereo/collision gates are not closed by
these tests. See MODLOG for exact current candidate/results, not historical totals.

Native-contract continuation: the source now checks full model/assembly root
coherence and commit-time stability. Retained run-7 after-copy captures reveal
distinct model/body palettes, refused by the existing alias gate; those records
also misserialize the assembly root. F2 remains blocked on a qualified transfer
owner, not just a visual test. See ARM_IK_EVIDENCE before changing the hook or
removing a guard. D's omitted consumer tail and B's bounded HP-callsite findings
are in NATIVE_STEREO_CONTRACT and NATIVE_MELEE_CONTRACT respectively; neither
establishes an enabled native binding.

Exit: coherent rendered body/equipment, persistent calibration and demonstrated geometric crouch/duck behavior. Native visual pose data alone cannot establish collision/hurt-volume changes.

## G. Complete physical sword/shield encounter and comfort

Entry: C, D, E and reachable-pose alignment from F. Owners: native combat/defense, grip/stance, XR haptics and composition.

- [ ] G1. Map real shield orientation/coverage against verified incoming attacks; qualify native directional block, stamina drain, guard break and impact outcomes. Shield geometry and native defense decisions must agree.
- [ ] G2. Qualify a physical parry action/contact window against native parryable/unparryable attacks. Preserve timing/state rules and reaction attribution; holding overlap cannot repeatedly parry.
- [ ] G3. Connect grip-held support to legitimate native two-hand stance/stats and release recovery. Visual two-handing alone does not qualify native two-hand rules.
- [ ] G4. Provide adjustable per-hand hit/block/parry/hurt haptics from confirmed outcomes, with bounded mixing and cancellation on lifecycle changes.
- [ ] G5. Implement/verify dynamic vignette in actual eye composition: artificial movement/turning envelope, combined head-turn boost, feathering, attack/release and Off. Preserve physical-only movement by default and prioritize head obstruction. Preserve physical HMD pitch/roll; suppress native camera bob/shake/artificial tilt.
- [ ] G6. Complete the fixed encounter using physical slash/thrust, defense, both locomotion references, physical/stick movement, turning and recovery. Capture synchronized inputs, poses, native outcomes and eye outputs; obtain operator feel/comfort verdict.

Exit: complete coherent encounter, not isolated hit demos. Carry all F requirements forward even if initial encounter qualification precedes full lower-body/crouch completion.

## H. Native interactions, HUD and controller-only usability

Entry: stable input/camera lifecycle and verified scene reach queries. Owners: input/menu/UI suppression, native interaction adapters, XR presentation.

- [ ] H1. Inventory pickups, doors, levers, bonfires, NPC dialogue, summon/offline prompts, ladders, elevators, chests and inventory/equipment actions. Identify native eligibility/activation owners and states for each; do not assume a common ABI.
- [ ] H2. Implement a shared target-selection policy using tracked reach/aim, distance, line of sight, ownership and lifecycle identity. Use per-action adapters to legitimate native activation. A button/grip may confirm a selected native action; melee remains physical. Do not add fake object physics where the object only supports a scripted interaction.
- [ ] H3. Add targeted physical manipulation where supported, starting with one object class. Coordinate hand attachment/release with native animation and movement locks. Prevent repeated activation, through-wall use and simultaneous conflicting actions.
- [ ] H4. Finish readable depth-stable HUD/status, interaction prompts and quick slots. Keep native menus in a qualified theater presentation where appropriate. Preserve Home, calibration, recenter, settings persistence and logs without keyboard dependence. Avoid suppressing essential native prompts before replacement is functional.
- [ ] H5. Test title-to-game, equipment changes, dialogue choices, purchases, death/reload and every interaction class using controllers. Verify input capture/release never leaks attacks or confirms into the game.

Exit: routine progression and configuration require no keyboard and no inaccessible hidden UI. Record which objects use native scripted actions and which support physical manipulation.

## I. Weapon breadth, ranged combat and loadout changes

Entry: G and reliable H loadout controls. Owners: equipment/geometry profiles, native combat context, aiming/projectile adapters and grips.

- [ ] I1. Build a support matrix from actual stock equipment/PARAM families: one/two-handed swords, thrusting weapons, axes/hammers, polearms, large weapons, fist/small weapons, shields and dual wield. Classify special geometry/attack behavior instead of assuming shared meshes imply shared rules.
- [ ] I2. For each family qualify reach/axes, slash/thrust/contact rules, stamina/poise/status, walls, multiple targets, two-hand and off-hand parity. Cover different lengths and asymmetric equipment. Reset contact state atomically on swaps.
- [ ] I3. Recover native bow/crossbow aim, projectile origin/direction, ammo, draw/release and reload ownership. Drive those from tracked equipment and intentional controls while preserving native projectile spawning, damage and inventory. Qualify two-hand bow handling and crossbow behavior separately.
- [ ] I4. Recover catalyst/talisman/pyromancy aiming and cast state. Map tracked aim to verified native launch/target inputs; preserve resource/cast rules. Qualify lock-on interactions without forcing HMD orientation.
- [ ] I5. Test left-handed dominance, two equipped hands, quick swaps, empty slots, consumables, depleted resources and unusual equipment. Explicitly list unresolved exceptions; a broad-support claim requires representative native evidence per declared family.

Exit: the declared stock weapon/action matrix works in real encounters; unsupported cases cannot silently fall back to gesture attacks. Any excluded product requirement needs a user scope decision, not an agent's completion claim.

## J. Campaign compatibility and special actions

Entry: integrated encounter and usable progression controls. Owners: relevant native state adapters, camera/body ownership and lifecycle.

- [ ] J1. Create a campaign coverage matrix by mechanically distinct areas and encounters: interiors/exteriors, narrow doors, slopes/stairs/ladders, moving platforms/elevators, water/effects, large enemies/bosses, crowded combat and long-distance rendering. A single arena does not qualify the campaign.
- [ ] J2. Define and implement ownership for rolls/backsteps, kicks, jumps/falls, ladders, backstabs/ripostes, grabs, knockdowns, death and cutscenes. Preserve native gameplay motion; use bounded comfort/theater transitions where necessary. Never let scripted camera animation forcibly rotate the tracked view.
- [ ] J3. Test save/load, bonfires/warps, area transitions, respawn, NPC dialogue, equipment/model replacement and repeated runtime focus/session loss. Invalidate transient combat/interaction pointers while retaining valid user measurements/preferences.
- [ ] J4. Run representative boss and traversal encounters, then a longer progression session. Maintain a case-by-case results matrix, capture defects with exact reproduction and rerun affected integrations after fixes.

Exit: declared campaign scope and special-action matrix have native/headset evidence; remaining failures are fixed before final completion.

## K. Performance, release qualification and packaging

Entry: all required functionality integrated; performance profiling starts at A and D, not here for the first time.

- [ ] K1. Measure native baseline versus final conversion at fixed scene/settings/runtime. Record complete stereo CPU/GPU p50/p95/p99, actual game rate and runtime period, frame drops, XR waits and memory/resource trends. Use multiple steady windows, not title-screen FPS or average-only results.
- [ ] K2. Meet the user's 90 Hz target using the exact eye-pair budget, headroom and measured runtime-cadence criteria in VERIFICATION_RUNBOOK.md. Record exact settings. A separately qualified reduced mode cannot be reported as the target pass. Optimize dominant measured costs; avoid speculative rewrites or silent resolution reduction.
- [ ] K3. Run Debug/Release suites, doctor, protected native lifecycle regressions, exact package/hash verification, clean-install/uninstall and interrupted-transaction recovery. Check shutdown, inactive headset startup, disconnect/reconnect, image acquisition/release failure, resized resources and unknown executable refusal.
- [ ] K4. Keep VERIFICATION_RUNBOOK aligned with verified implementation and current requirements; never label an unexercised case PASS. Run the current VERIFICATION_RUNBOOK acceptance cases and this guide's ledger on the exact packaged candidate. Retain operator comfort verdicts and results for both supported runtime routes separately.
- [ ] K5. Refresh the development package through the gated verifier. Mint a new immutable version only after its release gate; never overwrite a formal release. Include original code/config/notices and install/rollback/support instructions, not game assets or decompiled sources. Publication/upload requires separate user authorization.
- [ ] K6. Produce a final support matrix, known limitations, exact build/package identities and a short real gameplay recording. Ensure every required gate is PASS; absence of operator access leaves headset gates UNVERIFIED and the mod incomplete.

Exit: complete supported native VR conversion with reproducible package, native/headset evidence and successful restoration/installation checks. Optional cosmetic extras are not required; unfinished core requirements cannot be relabeled optional.

## 6. Commands and test discipline

Run from the repository root. Verify tool availability and existing CMake cache before changing generators. The checked-in verifier uses Visual Studio 18 2026/x64; do not replace it merely because another version is more familiar.

```powershell
git status --short
git diff --stat
cmake --build build --config Debug --parallel 4
ctest --test-dir build -C Debug -N
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
pwsh -File tools/auto_verify.ps1
```

If build is not configured, use the configuration in tools/auto_verify.ps1, after inspecting available toolchain and current options. The verifier configures, builds Release, runs CTest/doctor and refreshes dist/DSVR-latest; it does not install or launch the game. It does not replace the separate Debug or native/headset gates.

For focused runs, inspect `ctest -N` and choose registered test names with `-R`; source filenames are not guaranteed CTest names. Relevant boundaries include input/roomscale/camera/heading, calibration/arm/body, combat/native contact, stereo/XR recovery, UI and config. Register a new necessary fixture in tests/CMakeLists.txt. Run meaningful rate-variation, duplicate-callback, stale-data, actor-reuse and lifecycle cases at mutation boundaries. Do not accept self-generated expected values as independent proof.

Before protected deployment, inspect and follow current tools/verify_preflight.ps1, TESTING.md and restoration receipts; historical fixed file counts or stale paths are not a complete inventory. Keep Steam offline and existing exact-executable outbound protection. Preserve original files, both save roots and Steam metadata. Restore and hash-verify after each transaction, including failure/crash. Never claim restoration for state that was not snapshotted.

## 7. Completion ledger

Maintain this table here as gates change. Link evidence, candidate identity and remaining failures; do not mark a row complete merely because code was added.

| Gate | Status at plan creation | Required evidence |
|---|---|---|
| A baseline | source-wrap checkpoint freshly rebuilt: Debug/Release 88/88 and doctor PASS; fixture provenance PASS; native scene UNVERIFIED | source-wrap receipts and exact package identity in MODLOG; protected comparable scene still needed |
| B melee contract | unresolved | complete ABI/ownership and genuine native outcome correlation |
| C physical sword | not accepted | ten hits/misses, rest/wall/lifecycle and native costs/results |
| D stereo | D1b reference classification/executant-to-entity edge recovered statically; D1 counter/resource ownership unresolved; D2 UNVERIFIED; production absent | stereo evidence note and D1b static receipt; both counter records, natural main-scene trace and conditional two-view decision still required; D3-D7 and headset fusion remain open |
| E locomotion | orientation selection finished 23 Oct: always one live reference (Head default, or selected left/right/non-dominant hand with head fallback on hand loss), settable in DSVR-Setup and the mod menu, offline 88/88 both configs; follow unqualified | native motor attribution, path/collision/latency matrix, plus the rewritten reference-stability headset row |
| F body/crouch | solver/root guards offline PASS; native distinct-palette transfer BLOCKED; fit/crouch open | qualified source->body transfer, rendered alignment, independent mesh-scale ownership, clearance and hurt geometry |
| G encounter | not accepted | physical offense/defense, haptics, movement and comfort |
| H interactions/UI | partial existing menu/input | progression actions and controller-only usability |
| I breadth/ranged | unqualified | equipment/action family matrix in native encounters |
| J campaign | unqualified | traversal/boss/special-action and longer-session matrix |
| K release | not qualified | exact final package, 90 Hz, recovery/install and all required gates |

## 8. Pasteable starting prompt

```text
Work in C:\Users\Gabrielle Monlea\Documents\DSVR. Read AGENTS.md and
docs/superpowers/plans/2026-10-21-native-conversion-execution.md first.
The user requires native conversion and physical combat; do not replace that
goal with gesture-triggered canned attacks, direct HP edits or mono stereo.
Inspect git status/diff and preserve all existing arm/pose/test work. Read
MODLOG.md and the current requirement/evidence docs named by the guide.
Use codebase-memory-mcp first for code discovery; refresh stale indexing.
Refresh the task-relevant A baseline, then start D1/D2 at the Stereo feasibility
work package per the current user priority; afterward F2, then B/C. Work on one
unmet gate. Use one bounded contract/implementation task at a time. Before a
native mutation prove ABI, owner, lifetime, thread and outcome correlation.
Use the existing protected native test procedure; do not launch as part of
initial planning. Obtain human headset evidence when a prepared gate needs it.
Keep implementing and qualifying required gates; preserve precise evidence
and a resumable next action. Do not claim completion from green offline tests,
diagnostic proxies, unverified bindings, or a future test plan. When blocked,
name the exact prerequisite and continue independent useful work. Update the
existing docs and this guide's ledger instead of writing another roadmap.
```
