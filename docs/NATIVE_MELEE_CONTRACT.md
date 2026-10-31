# Native melee contract investigation

## 28 October bounded health-writer caller observation

The retained Run 2 already has paired contact/context records and four HP-change
comparisons. It does not identify the instruction that requested those HP
values. The broad hit/miss/block/parry/dodge sequence is therefore not being
repeated.

GhidraMCP identifies `FUN_140322910` as an HP-state setter: RCX is the actor,
EDX the requested HP, and the body clamps to actor max HP, applies invulnerability
and keep-alive rules, writes actor `+0x3E8`, and may force it to one. Its Ghidra
return type is explicitly unresolved. At the known predecessor callsite
`14028E823 -> 14031F6B2`, RCX comes from RBX and EDX from `[RAX+0x14]`; this
provenance is still not a melee attribution by itself.

The pinned executable SHA-256 remains
`a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`.
Entry bytes at RVA `0x322910` match Ghidra:
`89 54 24 10 53 48 83 EC 20 F6 81 24 05 00 00 40 48 8B D9`.
The PE exception directory contains three contiguous runtime-function ranges:
`0x322910–0x32296C` (unwind RVA `0x177CCD4`),
`0x32296C–0x3229A8` (`0x177CCDC`), and
`0x3229A8–0x3229EF` (`0x177CCF0`). They cover the recovered 223-byte function.
Both trailing unwind records carry chain info back to the first range.
The probe verified raw entry and epilogue bytes against the installed image.
Ghidra's undefined return annotation is retained; the observer calls the
trampoline once with unchanged arguments and returns its raw 64-bit RAX.

The LAB-only detour records setter calls only while an admitted detailed natural
`ContactDetour` call is on this thread's stack. Each optional
`native_combat_health_write` row shares the contact's (capture, thread, seq,
record) key and is admitted in the same atomic queue batch. Schema 1 stores
actor pointer, caller return address, signed requested HP, zero-based call
index, total scoped calls and an overflow flag in 16 float-exact 16-bit limbs.
The per-contact bound is four; larger totals are explicitly partial. The
capture header advertises schema/hook state and the runtime image base. Legacy
captures remain `unknown_or_observer_unavailable`; no setter event means only
that this setter was not called synchronously during that detailed callback.

The adapter regression verifies signature refusal, inactive pass-through,
unchanged arguments, exactly-once forwarding, raw RAX, target pointer/request,
four-row overflow marking, old-capture unknown state and all-or-nothing queue
admission. Debug and Release full suites and the Release LAB suite passed on the
sealed source candidate. A later raw trace exists below, but its missing footer
and absent case labels make it incomplete, not accepted native evidence. A fresh
closed ordinary hit is still needed; a setter request alone does not prove a
tracked physical sword caused the result. Alternate/asynchronous health paths
remain outside this scope.

## 29 October partial observer trace: scoped HP setter during contact

Artifact: build/verification/combat-health-writer-20261028/DSVR-trace-22516-40378187-1.jsonl, SHA-256 1b74ed7abd32c06fe22f0a0b159954e3b74860a44b8c24adfb53d11ca3d506d7. Header time 2026-10-29 03:26:02 UTC, PID 22516, combat health hook/schema 1 enabled. It contains 17,963 valid JSONL records: 247 natural contact batches and exactly one native_combat_health_write. The trace has no footer; python tools/vr_trace.py reports complete=false, valid=false, “capture has no closing footer,” and combat observation integrity FAIL. The associated transaction receipt remains Preparing with no PID/save/file snapshots. Its combat-cases.json declares one natural_hit interval [403571273709, 403772494795], but the trace header starts at 403782190754, 0.9695959 s after the interval ended; the health-write event is 18.1740426 s after that end. The trace therefore has no overlap with the declared case and cannot label the setter event. Do not treat it as a completed natural hit.

The sole writer row shares QPC, thread 8752, sequence 221 and record 0x15f0a0 with the contact, identity, motion, diagnostics and context rows. The local player is actor side 0 (0xf783470, vtable 0x1413251f0, handle 0x10044000). Side 1 is the target (0xf72a970, vtable 0x141322e68, handle 0x10040008). Contact-side HP is 659→659 for the player and 44→31 for the target; target stamina remains 71. The health row actor pointer equals side 1, requests HP 31, has call index 0 / scoped count 1 / no overflow, and records caller return address 0x140322a9d. The same contact context decodes as attack type 0, BehaviorParam 505, AtkParam 305, table selector 1, geometry selector -1, weapon id 212000, event handle 0; the raw contact return is 1. On the same observed thread 8752, the contact QPC falls 14.167 ms after GameStep kind 2/seq 1876 and 6.090 ms before StayStep kind 1/seq 1877. This establishes ordering only, not the native update owner. This is strong same-callback/target correlation, but the missing footer, zero overlap with the declared natural_hit interval and absent tracked-hand context prevent attributing it to the physical VR sword or accepting causality.

Targeted GhidraMCP resolution: FUN_140322a9d lies immediately after CALL 0x140322910 in the 11-byte wrapper FUN_140322a98, which then calls FUN_1428e9d39. Its only caller is the 16-byte block FUN_142e191ae: load current HP from [RBX+0x3e8], add EDI, pass RBX as actor, then tail-jump to the wrapper. The setter FUN_140322910 clamps and stores actor HP at +0x3e8. Ghidra shows no direct caller for FUN_142e191ae; its inbound path is a trampoline at 0x140322a8d, with continuation setup around it. The source/value of EDI and update/thread owner remain unresolved. After the setter, the wrapper enters a continuation chain: FUN_1428e9d39 → FUN_142847d55 → FUN_142eafac2 → FUN_1410c1814 → FUN_140219ac6 → FUN_14293b49c → FUN_1427bf86c. The final block dispatches through an unrecovered target in RAX; this is not a recovered scheduler contract. FUN_142e191ae has one direct code xref, the trampoline at 0x140322a8d, and no direct caller. Independent game-side outcome remains unverified because the trace has no footer or case label. No submission binding follows from this observation.

## Case-window race in the protected observer runner

The retained combat-cases.json proves the natural_hit window ended 0.9695959 s before the trace header. Cause: tools/run_combat_capture.ps1 called RunCasePlan immediately after process startup, before the observer had created a trace header (it starts after the local player appears). The runner now waits for a new, unclosed trace header whose PID matches the live, receipt-owned game process before starting any case QPC window, and journals the header filename/PID/QPC. Its delayed-header SelfTest fails on the old ordering and passes on the fixed ordering. This repairs case capture timing only; it creates no combat outcome and does not qualify the stale candidate.

Evidence reference only; execution order belongs to the [cockpit plan](../MODDING_PLAN.md) (lanes B/C), operating policy to [TESTING](TESTING.md).

Status: **unresolved**. No native damage binding is verified by this note. Static paths below come from the pinned Ghidra project; the first bounded native observation is separately classified with its loss limitation.

## 28 October source continuation: continuous whole-blade geometry

Source evidence only. Regressions reproduced missed mid-shaft translation in
ClassifyWeaponContact and missed interior rotational contact in QualifyContact
for both sphere and capsule slots. Four intermediate poses and endpoint paths
were insufficient. Both now use SweepBladeContact conservative advancement:
linear grip/length, shortest-arc rotation, whole-segment distance, 1e-4 caller-
unit tolerance and 256 iterations. Ambiguous near-half-turns or budget exhaustion
report Unresolved; bool adapters accept only Hit. Analytic-path tests cover
eight partitions, combined translation/rotation/length change, off-plane misses,
duplicate queries and rehit refusal. Native volumes, geometry units, solid
interception and damage/context submission remain unqualified.

The existing kind-assignment evidence below already answers the previous plan's
1405290C0/140529670 question. No new binary trace was possible here: the live
GhidraMCP DSRFull GUI (PID 13564) connects and reports function/xref/program
groups loaded, but those analysis methods are not exposed to this chat after
loading. Only instance/group readiness was queried, no program bytes/functions.
The existing native contact ABI and outcome unknowns are unchanged.

The operator's wall-drag/impact, shield-surface and shield-motion-parry contract
is recorded in ENGINEERING and the runbook. It requires verified effect/audio,
cost/reaction and incoming-contact owners. The source sweep fix alone cannot
implement those native behaviors. B/C/G remain open.

## 27 October dummy-id owner probe: negative result, unknown recorded

Read-only live GhidraMCP against `DSRFull` (no save/repair/launch/invocation).
AtkParam row selection is pinned: `1403C93B0` obtains BehaviorParam via
`140528A30`, reads refId at +C, selects AtkParam via `140528910` (binary-search
row lookup, selector+7 table). The row carries tip/tail dummy-polygon ids at
`0x2C..`/`0x34..` (s16), but no consumer resolving those ids to world positions
was found: string search for dummy/GummyPoly/GetDummy/DummyId returns only
`CreateSfx_DummyPoly` (SFX path, single DATA xref from `14001C100`, no callers
— not the weapon-model resolver). The equipped-weapon model's dummy-id to
world-position owner remains UNKNOWN. Per the spec fail-closed rule, unresolved
or stale-model dummy = slot invalid, `QualifyContact` stays
`slot_resolved=false`; sphere fallback only on explicit caller opt-in. Next:
enumerate `140528910`'s 33 callers for a model-geometry consumer, or correlate
via footer-valid B1/H1 runtime captures.

**Evidence correction (latest):** Several 26 October conclusions below
overstate the evidence and are retained only as history. The v2 payload's
weight/ID/tag fields are read before native dispatch despite their `after`
names. Their values do not prove post-call state or the source PARAM byte.
The first and Asylum captures lack footers; their apparent correlations are
unqualified. Kind `record+0x54` and the separate `+0x160 == 0x1d` test are
not interchangeable. No proof establishes firebomb dispatch bypass, that
`1402FCD10` implements virtual `+0x150/+0x148`, or that either static chain
is complete. The selector at `140529670` reads the pointer *stored at*
`param_2+8`, then indexes that pointer at `+0x6a`; do not add 8 to the row
offset or infer a specific attack until its provenance is independently pinned.

## 24 October continuation: selected rows and event identity reach the contact record

Read-only live GhidraMCP against `DSRFull`, revision
`1792646701740:124420c4:3`, unsaved changes false. The requested
[DSR reference hub](https://soulsmodding.com/doku.php?id=ds1r-refmat:main)
links the [BehaviorParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:behaviorparam)
and [weapon](https://soulsmodding.com/doku.php?id=ds1-refmat:param:equipparamweapon)
tables. Their serialized fields guided the probes; the native observations
below come from this executable, not from treating wiki offsets as actor offsets.

Recovered producer/copy chain:

- `1403C93B0` obtains a BehaviorParam selection through `140528A30`, checks
  selected row `+9 == 0` (Attack reference), loads its **refId at +C**, and
  selects the AtkParam through `140528910`. An animation ID is not that refId.
- `1405290C0` stores attack type at payload `+34`, behavior ID at `+38`,
  selected AtkParam ID at `+3C`, table selector at `+40`, geometry selector
  at `+44`, and the selected equipment ID at `+48`. `140528910` uses selector
  plus 7 for its table lookup; `140528A30` uses selector plus 12.
- Both PlayerIns slots `+18`/`+28` return **1** (`140359CF0`/`140359D00`);
  the corresponding EnemyIns slots return **0** (`140301290`/`1403012A0`).
  Keep the raw selector in evidence; do not assume 0 means player.
- `1403C5620` copies the payload to event `+44..+113`;
  `1403C7520` copies it back to contact-record `+0..+CF` before natural
  dispatch. Record `+C8/+C9` are **payload flags**, not the serialized
  AtkParam `+7E` byte. The same builder copies the event's generated handle
  into record `+140`. This gives an identity token beyond the reused stack
  address, but does not prove pool lifetime or target generation.

Parry distinction: for attack type `64`, `1405290C0` obtains payload `+30`
through `1402DEB20`, actor virtual `+268`, PlayerIns `14035F1F0`, then
`1402DEB70`. The actual byte load there is weapon row **+F2**, converted to
float in XMM0. The reference calls it `attackBaseParry`; `parryDamageLife`
is a separate **signed-short +C2** field. The decompiler's void signature
hid the floating return. This identifies a parry attack value, not a timing
window, successful parry, or callable entry for physical defense.

Independent byte check: `python -B
C:\Tools\DarkSoulsRemaster_De\combat-evidence\physical-combat-20261024\verify_context.py`
passes **4,626 primary bytes** against EXE SHA-256
`a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`.
`context-verification.json` SHA-256:
`8151c7b52092d469d1b07948ad15cca5901e848eb7a349665ff1a66b955decda`.
The receipt includes unwind bounds, copy/selected-field assertions and the
parry-field check. Unanalysed split gaps are byte-compared, not linearly
decoded as if they were complete functions. No primary DB repair or script.

Source changes: `QualifyContact` now requires a separate
`NativeAttackSelection`; animation/behavior/AtkParam IDs cannot be silently
conflated. Parry/unknown attack types, invalid motion and zero-rounded native
corrections refuse with an empty output. Attack-slot overlap is a source
shape check, not a substitute for victim hurt volumes or wall interception.
The capsule solver now handles shallow interior crossings analytically;
invalid defense timestamps cannot manufacture block/parry classifications.

The LAB observer adds `native_combat_context` to each atomic contact batch.
`combat_context_schema=1` requires exactly one matching context per callback;
old context-free captures remain readable. Payload contains six full signed
dwords as 16-bit limbs, raw `+C8/+C9` bytes, and the full event handle.
Tuple/flags/handle reads have independent validity bits; absence is unknown,
not row zero. Reads occur before dispatch. Queue pressure drops all five
records together. The analyzer reports `native_context`, per-case
`attack_selections` and `event_handles`; duplicates, orphan records, native
return disagreements and declared-but-missing contexts invalidate the capture.

Next native discrimination: correlate these selected rows/handles with separate
operator-confirmed hit, miss, block, failed parry and parry-only captures,
then follow the type-64 `+30` consumer and ordinary character-contact path.
Do not repeat the already classified effect-like `+640` lead as if it were
a new attack API. B/C stay open until context lifetime, simulation ownership,
defense decisions and independent outcomes qualify a real submission seam.

### 27 Oct type-64 `+30` consumer probe: static dead end, value is inert in dispatch

Read-only live GhidraMCP against `DSRFull` (no save/repair/launch/invocation).
The builder's `+30` store is confirmed in the listing at `1405292A5..1405292B6`:
`CMP dword [RDI+0x34],0x40 / JNZ` skips `1402DEB20`, whose result lands at
`[RDI+0x30]` only for attack type 64. The value is `attackBaseParry`: actor
vtable `+0x10` must return exactly 1, then vtable `+0x268` (`PlayerIns
14035F1F0 -> 1402DEB70`) resolves the weapon row and returns byte `+0xF2` as a
float (0.0 on any refusal).

Field-offset xref is not resolvable, so readers of record `+0x30` were
enumerated by decompile/listing text across every dispatch consumer:
`1403C4020`, `1403C4210` (kind 0/1/2), `1402F84B0`, `1402F84F0`, `1402FC360`,
`140420A20`, `140420B30`. **None reads `+0x30`.** The only same-offset reads
found belong to the unrelated ObjAct row layout. So the type-64 parry value is
written at build time and has no consumer in the recovered dispatch tree: it is
inert data for hit/block/parry decisions, not a hidden parry handler. The
kind-1 thunk continuation (`1429E5D7E -> 1402F8494 -> 142C58024`) is an
unrecovered computed jumptable (Ghidra: "Too many branches"), unchanged.

Verdict: static follow-up on `+30` is exhausted — negative result. Parry/block
discrimination can now only come from runtime correlation (footer-valid
hit/miss/block/failed-parry/parry-only captures with selected PARAM/event
identity), exactly the B1 comparison. No binding proposed; B/C remain open.

## Real-character defense follow-ups: operator reports exceed trace coverage

24 October protected real-save-copy runs: PID7332 and21832; a15-minute setup-only
retry PID26804 produced no capture. Receipts under
`build/verification/native-combat-real-20261024-141757/`,
`build/verification/native-combat-real-parry-20261024-retry1/` and
`build/verification/native-combat-real-parry-20261024-30m/` are Restored.
Independent original-file/save hash checks pass. User explicitly
approved the30-minute cap after setup exhausted15-minute sessions; default15
and all restore/offline protections remain. Both rebuilt suites93/93 and
PowerShell5.1/pwsh transaction tests pass on the re-pinned runner candidate.

Operator reported chip blocking, taking ordinary hits/killing an undead, failed
parry attempts, and successful parries followed by ripostes. Latest correction
to the final attempt is **miss -> one hit taken -> successful parry -> riposte**.
Do not turn either mixed positive sequence into a parry-only observation.
All completed real-character captures hit the256-contact limit; no declared
clean-block sample was captured in the30-minute session. The last capture
`DSVR-trace-21832-104309390-6.jsonl` is invalid (59 triplets, no closing footer)
because the transaction was stopped during an active late capture. Zero drops
on earlier files do not repair these coverage/attribution gaps. Full reports,
raw hashes, requested sidecars and clock-preserving corrections are retained.

Read-only external equipment snapshots add a narrower native fact: on one
unchanged player/world/assembly/stance owner, operator-confirmed two-handed
stance had raw value3 and the confirmed return to one-handed had value1;
equipped IDs were left1401000/right500000. An earlier pre-transition snapshot
had another player allocation (same handle), so it is not a same-generation
1->3 proof. This is not native two-hand damage-rule or VR grip acceptance.

A bounded raw-stat sampler recorded98 coherent-identity samples, no refusals;
its file is named raw-stats-block.json, but actual trigger/QPC put it in the
**failed-parry** window. Raw+3F8 varied67..91, +3FC remained91. These raw data
do not justify relocating the existing stamina read or attributing a block.
External snapshots are not atomic native-update/contact observations.

Before further trials: handshake actual capture start with the operator action,
wait for a closing footer before stopping, and investigate unrelated contacts
consuming the global detail budget without weakening refusal/loss reporting.
Do not ask for repeated blind parry trials or enable a guessed native binding.

## 24 October: clean original-controls hit/miss comparison

Protected desktop-only run, PID **20680**, candidate
`build/verification/ik-combat-20261024-134111/candidate-final.json` SHA-256
`fd9538b6511cb4310d457061a6353904f13484a8b3cb68a64ede69808f6bc0d2`.
No XR, IK, automated input, damage dispatch or gameplay writes. Receipt under
`build/verification/native-combat-desktop-20261024-135521/` is **Restored**;
independent install/save hashes pass. The first restore hit a DLL sharing
violation; a subsequent guarded Restore succeeded. Keep that first exit-1
failure visible; the owner of the transient lock is unknown.

Operator: sword (exact weapon/PARAM ID unknown), **no shield**, passive tutorial
Hollow. Readiness was confirmed before F9; each capture ran 20 seconds within
its own measured QPC case window. All three have combat-domain headers, zero
drops/IO errors/contention, complete contact/identity/diagnostic triplets and
native Step records. Raw files are retained unchanged beside the receipt:

| Capture suffix (all `DSVR-trace-20680-`) | Paired / Step samples | Observation | SHA-256 |
|---|---:|---|---|
| `99504140-1.jsonl` | 146 / 2,348 | One operator-confirmed hit, one stable target HP loss | `2597f6b47f99cec75217dcf4135147aa96526b1138b45b9700c065ab4252ad0e` |
| `99672953-2.jsonl` | 196 / 2,336 | Operator-confirmed out-of-range miss; no observed HP change | `57d40c6ea6ff6d97647eda2a8deac4538f370e1891e1c0b9b7ba344020919b97` |
| `99847093-3.jsonl` | 256 / 2,320 | Roll with **no incoming attack**; detail ceiling reached | `475d648685f5a41483e8aca46fe86517f0d2244b5a57650ab8c1815c5770ffb1` |

Hit seq **24**, thread **10628**, stack record `0x15F0A0`, return 1:
source `0xD6F8690`, vtable `1413251F0` (PlayerIns), handle 268713984;
target `0xD6AC310`, vtable `141322E68` (EnemyIns), handle 268697608. All four
actor reads valid and both identities stable. Target HP **44 -> 31**; source
HP 659 unchanged. Stamina 71/50 unchanged *across this contact*, not evidence
that the whole attack had no stamina cost. The pointer is a reused stack
address, not a persistent attack identifier.

This replaces the first run's loss-limited hit/miss evidence with a clean,
operator-labelled two-case comparison, **not** complete attribution/context or
defense qualification. Unknown actor types remain explicit read refusals. The
third case was relabelled `roll_no_attack` after the operator reported no enemy
attack; original requested sidecar plus hash-linked correction are preserved
without changing times. It certifies neither dodge defenses nor invulnerability
and its detail limit prevents whole-window contact claims. No shield means
block/parry were **not exercised**. B1, B3-B5 and C remain open.

Read/assembly median/p99 ms: hit **3.25015/11.4865**, miss **3.02725/9.2873**,
roll **3.08175/10.0099**. Native-call medians are <=0.0009 ms. The candidate did
not change; scene/timing differences from run 2 are not an overhead fix or
90 Hz acceptance. `comparison-summary.json` and the per-capture `-report.json`
retain the full refusal/timing/coverage results.

## 24 October continuation: native endpoint geometry and contact-record lifetime

Read-only primary MCP, revision `1792646701740:124420c4:3`, unsaved changes
false. Four raw ranges / **1,084 bytes** match the pinned shipped EXE. Independent
Capstone decoding and PE unwind bounds are retained outside distributable sources:
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\ik-combat-20261024-134111\`.
Reproduce with `python -B <that folder>\verify_geometry_edge.py`; report
`geometry-edge-verification.json` SHA-256
`ecb5b557b757d0122fe4ebbfb71267df1ad35cb15004eb914cfb34b9aa2cde56`.

Pinned observations:

- `1403C7520` constructs a **stack-local** contact record at `RSP+0x20`, calls
  `1403C4020` at `1403C77D6`, tests **AL**, then performs event-list bookkeeping
  through `1403C7BC0` and `1403C4460`. The source resolved through `1402C4870`
  is stored at record `+0x130` (`1403C76C9`), target at `+0x138`
  (`1403C76CD`). A repeated stack record address is not a persistent attack ID,
  and nonzero high RAX bits are not the boolean acceptance result. Preserve the
  post-call bookkeeping: invoking the leaf dispatcher alone omits it.
- The geometry helper `1403C7B10` dispatches actor type 1 through a **tail jump**
  at `1403C7B97` to vtable `+0x448`. PlayerIns slot `141325638` contains
  `14035BBE0`. That override checks its fifth stack byte and actor `+0x850`,
  selects assembly index **7 when incoming R9D is -2, otherwise 8**, and
  tail-jumps to `140212670`. The other branch reaches `1403277D0` instead.
  The selector/branch is pinned; semantic left/right names are not yet qualified.

Decompiler-only follow-up, **not a callable ABI**: `1403C6030` resolves the event
owner and calls `1403C7B10` with event fields `+0xF0`/`+0xF4` and `+0x88`, selecting
an index at `+0x198` from 48-byte output transforms. It uses their origins to
update event `+0x160`/`+0x170` and optional two-endpoint geometry. `140212670`
selects assembly child/resource paths into `140227CB0`/`140227E00`; those helpers
search signed-short keys and transform 0x50-byte records into 48-byte outputs.
The fifth/seventh stack arguments and fallback split flow must be recovered
from actual callsite bytes before instrumentation. These are endpoint-transform
leads, **not proof of physical blade bounds or native damage submission**.

The callsite stack-argument probe is now closed by the follow-up below. The
desktop run above resolved the account prerequisite and hit/miss labels; active
incoming attacks, block/parry and complete attack ownership still remain open.
No new hook, native call or damage write is enabled.

### 25 October: the contact record's motion fields are in the builder's own decompile

Read-only primary MCP, revision `1792646701740:124420c4:3`, unsaved changes
false. `1403C7520` spans `1403C7520`-`1403C7871` (836 bytes). Its decompile
shows the record base directly: `1424B219B(&local_208)` initializes the stack
object at `RSP+0x20`, fields are copied in, then `1403C4020(&local_208)`
dispatches it. Relative to that base, the offsets the LAB motion record reads
are:

- `+0x130` source handle resolved through `1402C4870`, `+0x138` target object:
  the two offsets already proven at runtime by the hook.
- `+0xF0` a 16-byte SSE slot holding `normalize(point - actor position)`, with
  **w forced to zero**. The components are masked to zero when the source length
  is zero and set to `0x7fc00000` when that length is infinite, so a non-unit
  direction can mean a degenerate native delta, not only a wrong offset.
- `+0x150` four floats `(current - previous) * 60.0`, taken from the actor's
  current and previous position quads.

Two cautions. `+0xF0`/`+0xF4` are *also* the key fields of `1403C7B10`'s event
object described above; same offsets, different structure, never merge them.
And the `* 60.0` is the game's own fixed 60 Hz assumption, so that field is
**native world units per second**, not metres per second. Our measured tip speed
is metres per second. The analyzer now divides the native magnitude by the world
scale that was actually applied (carried by `combat_bench_provisional`) before
comparing the two, reports the raw unit count separately, and leaves the
comparison empty when no scale is present rather than assuming one.

What this does not close: no build has yet *observed* these fields in a real
capture. The first LAB quadruplet's `direction_unit` bit and
`native_speed_units_per_s` against our own tip speed for the same swing is the
next probe. Layout evidence is not runtime evidence, and neither is damage
attribution.

### 25 October autonomous continuation: endpoint resource lookup narrowed

Supported read-only GhidraMCP is available again: GUI PID 19116, project
`DSRFull`, program `DarkSoulsRemastered.exe`, revision
`1792646701740:9d9b1124:3`, unsaved changes false. This supersedes the earlier
tool-unavailable observation, not any unresolved native contract.

Decompiler-only findings (not an accepted ABI or lifetime): `140227CB0` and
`140227E00` binary-search signed-short keys in four-byte entries through
resource `+0x18/+0x20`. The selected entry's short at `+2` and the following
entry's short at `+6` delimit records; the requested output count is capped by
their difference. Records use a `0x50` stride through resource `+0x10`, whereas
output transforms use `0x30`. Do not conflate those arrays. Wrappers
`140228BD0/BF0` select the unremapped/remapped transform paths; `140228BF0` and
`140228C40` forward a fifth stack argument which their decompiler signatures
omit. `140225A30` shows a signed-short remap via map `+8/+0x10`; invalid map
indices become `-1`. These observations still require independent bytes and
the resource constructor/sentinel/lifetime before use.

One leaf is independently byte-qualified: all **47 bytes** at
`[140209BF0,140209C1F)` read with MCP `read_memory` match the pinned EXE
SHA-256 `a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`.
Independent pefile extraction and full Capstone x86-64 decode exit 0. It
sign-extends R8D, multiplies by `0x30`, adds `[RCX+0x10]`, and copies three
16-byte lanes to RDX. There is **no local bounds or negative-index check**.
The meaning/safety of upstream `-1` is therefore unresolved; never call this
leaf with a guessed index or assume that a remap refusal makes it safe.

Next: pin the `140227CB0/E00` search/count instructions and recover the
resource constructor, including the extra end/sentinel entry and valid record
counts; then connect that resource's lifetime to the selected equipment/actor
generation. Still no verified blade dimensions, victim geometry, world
obstruction or damage/defense submission; B/C remain open. No DB mutation,
game launch, native call or write occurred in this continuation.

### Endpoint argument follow-up after the clean comparison

Primary read-only MCP revision unchanged, unsaved changes false. **240 new
primary bytes** and **226 prior helper bytes** independently match the pinned
PE and decode on full instruction boundaries. External receipt:
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\native-combat-20261024-135521\`;
`python -B <folder>\verify_endpoint_arguments.py` exit 0; report
`endpoint-arguments-verification.json` SHA-256
`40b653dd1884f213f2118e439b88b2fae21e45f0f853900c793f6b2f412e1559`.

At calls **`1403C61F4`** and **`1403C639B`**, the actual argument tuple is
`1403C7B10(event, actor, key, selector, flag, rows, index+1)`: key comes from
event `+0xF0` / `+0xF4`, selector from `+0x88`, flag byte from `+0xF8`, output
is the stack array at RBP, count from `+0x198 + 1`. The array constructor creates
eight 48-byte rows. Upstream index bounds/meaning are not recovered, so this is
**not** permission to supply guessed counts to the native helper.

The type-1 tail changes that into virtual `+0x448(actor, rows, key, selector,
flag, count)`: count is forwarded from the seventh argument into the sixth
stack slot before its epilogue. PlayerIns `14035BBE0` then forwards
`140212670(assembly, rows, selector == -2 ? 7 : 8, key, count)` on its verified
assembly branch, or `1403277D0(actor, rows, key, count)` on the alternate branch.
Decompiler signatures had omitted these stack arguments; do not copy them.

`140228BD0` / `140228BF0` wrap `14021AB70` / `1402295D0`; decompiler-only next
producers are `140229630` / `140225A30` followed by conversion `140094520`.
Next: recover those resource/record owners and the event constructors that
populate the keys, flag and index. Prove lifetime/generation and attack-context
creation before choosing a tracked-contact seam. Endpoint transforms alone
still establish neither full blade bounds nor native damage/defense ABI.

## 24 October natural hit observation: useful lead, incomplete coverage

The operator reported one ordinary original-controls hit connected in the
protected desktop observer run (PID 38116). Receipt
`build/verification/native-combat-20261024/transaction.json` is `Restored`;
both original save roots and install files were hash-verified. No injected call,
damage write, input automation or XR was used. Loadout/enemy names are unknown.

One retained contact sample, seq 32 on thread 35160, record `0x15F0A0`, returned
`0x1`: source `0xD778460`, vtable `0x1413251F0` (PlayerIns), handle 268713984;
target `0xD72C0E0`, vtable `0x141322E68` (EnemyIns), handle 268697608. Both
identities were stable through the call. Target HP changed **44 -> 31**,
stamina 50 -> 50; source HP 659 and stamina 71 were unchanged. This links a
natural contact completion to an HP change consistent with the operator's report,
not a complete attack-context or damage/defense contract.

The capture is **not qualified for complete correlation**: 1,224,269 recorded
events, 795,660 dropped, queue peak 4096; actor-read/assembly median 9.26235 ms,
p99 14.575 ms. Native-call median .0019 ms, p99 .0554 ms. Unrelated constant
uploads dominated the 354,950,419-byte capture. Original SHA-256
`78fd62e3563e0dc06236ad115d2e83a48d3f96ca40b21fce1411440bfebf8522`;
raw capture and `hit1-native-summary.json` are beside the receipt. Selected
records are an explicitly incomplete subset, never a repaired complete trace.

Combat-only tracing now excludes unrelated hooks/events without weakening actor
read guards. A new pinned native repeat must establish zero loss and measured
cost before hit/miss/defense comparisons. First-run timing does not identify
which individual guarded operation is slow.

Read-only assembly follows source type 1 through `1403C4140 -> 1425BA7DC`, with
record in RDI, source in RSI and target in RBX. Target getter through `1403C4162`
and `142E3046E -> 142600E8C` selects `1403C41CA` for target type 1. That transfers
to `1428C7F11`: R8=RSI (source), RDX=RBX (target), RCX=RDI (record), restores the
outer frame, then tail-transfers to **`14031D5D0`**. `1403E2A83` is a stack-
transfer helper, not a normal two-argument gameplay ABI. Primary database remains
read-only/unsaved false.

**Superseded 26 October 2026:** the earlier atomic-failure/thunk-pool
classification was incorrect. The selector preserves a target-state comparison,
and its accepted path reaches class-specific response callbacks. See the
26 October resume below; do not discard a continuation merely because it is
entered through a forged return. Keep the primary database unchanged.

## Pinned analysis source and workflow

- Primary, read-only project: `C:\Tools\DarkSoulsRemaster_De\analysis-full\DSRFull.gpr` (`DarkSoulsRemastered.exe`, image base `0x140000000`, Ghidra 12.1.4).
- Repair workspace is separate: `C:\Tools\DarkSoulsRemaster_De\analysis-full-attack\DSRFull.gpr`. Do not use it as the source of primary observations.
- `npx -y rea-agents@4.0.1 analyze` treated the parent directory as a JavaScript application; the direct `.gpr` target was rejected. REA MCP `open_binary` also returned `target_unavailable`; `binary_session` reports the Windows Ghidra provider unavailable because process Job Object ownership, private DACL enforcement, and reparse-safe path admission are not proven. Consequently these Ghidra observations have no REA Evidence IDs.
- To continue the requested investigation, the existing `tools/ghidra/query.ps1` was used against the pinned primary project with `-readOnly -noanalysis`. Split recovery uses `RecoverSplitFunction.java`, whose transaction and analysis-only instruction patches were rolled back and byte-checked. Raw outputs are local-only under ignored `build/native-n1-*.txt`; they are not distributable ABI proof.

## Observed static paths

1. `0x1404B0EC7` is a direct jump to `FUN_140490F40`; the wrapper's call at `0x1404910AD` enters `FUN_1403C9B50`. The wrapper validates two actor IDs/handles and passes position/stat-like values into the event creation path.
2. `FUN_1403C9B50` calls `FUN_1403C93B0`, obtains a generated event handle, indexes a manager pool in `0x230`-byte slots for IDs below `0x80`, and calls `FUN_1403C6DA0` before returning the handle or `0xffffffff`. Meaning of the event fields and exact side effects remain unproven.
3. `FUN_14024F6B0` calls `FUN_1403C8DD0` at `0x1402500D8` on its update path. `FUN_1403C8DD0` walks an event list linked through record offset `+0x220`, calls `FUN_1403C6030` for records, removes/recycles records when it returns false, then calls `FUN_1403C6850` on retained records.
4. `FUN_1403C6030` validates an actor/context, builds contact-like data from event-record fields and the candidate actor, and calls `FUN_1403C4020`. If accepted, it updates per-event actor bookkeeping and calls `FUN_1403C7BC0`; it also consults/updates a secondary list through `FUN_1403C4460`. `FUN_1403C7520`, called from `FUN_1403C6850`, `FUN_1403C7D30`, and guarded helper `FUN_1403C7460`, rejects actor IDs already in three context lists (`+0x76`, `+0x78`, `+0x7A` via `FUN_1403C48C0`), assembles context from the event record, point/direction and actor, then calls `FUN_1403C4020`. The former claim that `1403C6030` calls `1403C7520` is corrected by the update-owner probe below. Accepted interactions add the actor ID to event bookkeeping. This is a contact/event candidate path, not by itself a damage result.
5. `FUN_1403C4020` dispatches on `+0x130`'s virtual type result: type 1 routes through split code beginning `0x1403C4140 -> 0x1425BA7DC -> 0x1403C4162`; type 2 routes to `FUN_1403C4210`; types 0 and 3 have separate handlers. `FUN_1403C4210` further dispatches on the target object's virtual type. For the observed `PlayerIns`-like vtable, `+0x490` is a split thunk that requires event `+0x160 == 0x1D`, then forwards to vtable `+0x480`; type 2 can also reach `FUN_1402FC360`, which has event-duration/target-state gates and effect/status/event work. Neither path is yet tied to ordinary melee HP damage.
6. Temporary recovery of the type-1 split body covered 428 instructions, with one unresolved instruction (`0x14031D61E`) and five terminal/return targets requiring review. The script reported `COVERAGE instructions=428 unresolved=1 terminal_targets_to_review=5`; no complete API contract was recovered. Recovery of the distinct PlayerIns `+0x480` split path (`0x1427EBED8`) covered 433 instructions with no unresolved edges and one terminal target under review; it shows geometry, flags, type-dependent callbacks and effects, but still no proven HP-damage computation/result contract.
7. The `PlayerIns`-like vtable observed at `0x1413251F0` has `+0x3B8 -> 0x140358C80` and `+0x490 -> 0x1403589B0` (the latter is a split thunk). Temporary recovery of the `+0x490` thunk guarded on an event field at `+0x160 == 0x1D`, then dispatched to the object's `+0x480` slot. This is a specific caller/callee lead, not proof that the event is damage. `+0x3E8 -> 0x140359240` remains an observation anchor; its split continuation `0x14280602C -> 0x14104C734` is not an attack API.
8. `FUN_140322910` clamps the supplied HP value to `[0, max HP]`, checks invulnerability/keep-alive state, and can force HP to at least 1. It has many callers. In `FUN_14024F6B0`, the observed call sets HP to zero only after an accumulated timer crosses a threshold under a matching state check. This callsite is not evidence for ordinary melee damage and must not be used as an attack dispatcher.

## Bounded HP-side classification, 22 October continuation

This narrows the search, not the missing damage contract. Primary program
revision `1792646701740:aeb52525:2664`, unsaved changes false, same pinned EXE
SHA-256 as [stereo evidence](NATIVE_STEREO_CONTRACT.md). MCP assembly contexts
and bounded raw-byte decoding were read-only. The local-only instruction
receipt linked there includes the split predecessor blocks below.

`get_xrefs_to(140322910)` lists 30 references: three data references and 27
direct callsites (not 30 ordinary-hit callers). `get_functions` lists 25 named
caller functions. Several apparent functions are call-sized split chunks.
Use instructions at the incoming branch, not their inferred void signatures.
In particular, some xrefs labelled CALL by analysis are actual JMP instructions.

| HP callsite / real predecessor | Observed EDX source | What it does **not** prove |
|---|---|---|
| `14032CA2B <- 14262F94E`, `14032CB6B <- 142653E32`, `14032CBB1 <- 14039B5A6` | Explicit zero before jumping to setter-call chunk. | Not an ordinary weapon damage calculation. |
| `14032CF4A <- 1405AF372`, `140359781 <- 1406284D2` | Actor `+0x3EC` (maximum HP). | Not a physical-hit submission path. |
| `140322A98 <- 142E191AE` | Actor `+0x3E8` plus EDI. | Delta sign/origin, attack context, defenses and outcome attribution remain unknown. |
| `14032F58F <- 14035F910` | Actor `+0x3E8` plus ESI. | Same missing delta-producer contract; HP arithmetic alone is not melee. |
| `14031F6B2 <- 14028E823` | Dword from incoming RAX `+0x14`, receiver carried in RBX. | Split register provenance and record meaning remain unknown. |
| `1403955BD` | Zero-extended ushort from `140504880` into R14D, then EDX; receiver RSI. Surrounding queued-state work resembles replicated state. | Inference of replication is not an established melee ABI. |
| `14036F241` | HP subtraction uses global short `141A2BF5C`. | No ordinary-contact correlation. |
| `14034B72F`, `140480618/1404806C0` | Maximum HP; nearby state restoration/init work. | Not a damage sink. |
| `14035C183`, unmapped `140349C87` | Saved/derived values; neighboring writes update several actor statistics. | Function/record ownership and actual execution remain unqualified. |
| `1403727E2` | Zero-extended word from `140353A20`'s returned record. | Record type and producer unknown; not automatically an attack result. |

Qwen: for static progress, follow EDI at `142E191AE` or ESI at `14035F910`
back through their real incoming split edges, preserving receiver and flags.
For a protected encounter, observe only natural setter/contact calls and bind
the result to attacker/target/update identity. Never invoke these continuations:
their register and stack state belongs to a larger function.

The whole-program MOV/`0x3E8` instruction search timed out with an explicit
"request may still be running" result. No exhaustive HP-write search completed.
Do not repeat that global query or interpret the timeout as a negative result;
use bounded caller instructions or an observed encounter instead.

## Unresolved questions and next probes

### 24 October: HP-delta lead also has an effect-like producer

Read-only primary MCP (`1792646701740:124420c4:3`, unsaved changes false) narrows
the `32F548` lead rather than qualifying it. `14032F480 -> 14217AFC4` loads the
receiver's maximum HP (`+0x3EC`); its split path preserves current HP, follows
receiver `+0x278`, and reaches `140403250` through `14032F4CD`. That helper calls
`140407110`, computes a maximum-HP percentage plus a fixed amount, negates the
sum and returns the integer delta. `1401901DE` copies its EAX result into ESI.
The primary listing identifies `140321549` as a caller of `14032F480`; its
predecessor at `1411F3EF5` restores RCX from RBX, not a recovered attack context.

`140407110`'s decompilation aggregates a linked list through `+0x40`, with
record flags, parameter pointers and two weighted value accessors. The accessors
read parameter `+0x9C` (float) and `+0xA0` (integer), multiplied by record weights.
This supports an **effect-like HP adjustment producer**, not an ordinary
weapon-contact producer; exact native types and runtime execution remain
unverified. Do not claim that this excludes all combat relevance, but do not
spend another pass assuming the `+0x640` call itself is a physical attack API.

Five primary raw ranges / **443 bytes** were checked equal to the pinned PE.
Local-only receipt directory:
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\melee-20261024\`;
`health-effect-primary-ranges.json` SHA-256
`5ee6da308ef8eb59c1a23c9027c8bdf527c261d150080a9a2e8b6e1ea769df59`,
with `health-effect-primary-verification.json`. These bounded byte checks do not
cover the entire linked-list helper or establish an attack ABI. REA MCP still
reports its Windows native authority unsupported; the primary Ghidra bridge
provided the observations without repair, analysis writes or a game launch.

Next discriminating evidence is the existing B1 natural encounter comparison,
using the lab contact observer at `1403C4020` and independent visible native
outcomes. Its three-part atomic sample preserves actor lifetime and all return
bits, reports read/refusal reasons and separate native/read costs. Missing
callbacks, capped detail or an unobserved outcome cannot certify hit/miss/block.

### Focused HP-delta producer trace, 22 October wrap-up

The EDI route is now traced back to a real negative-HP caller, but **not yet to
an observed ordinary weapon hit**. Read-only primary MCP observations were
cross-checked against hash-pinned PE bytes/RTTI. Local-only receipt:
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\melee-body-handoff-20261022T213421Z.json`,
SHA-256 `1eec629dc1e840974da52793aa43ac1ef29340060800d5b6ab0a96775e0925a9`.

- `140322A60 -> 142CB5171` is the entry preceding the EDI HP-delta path. Its
  prologue preserves RCX as receiver in RBX and incoming EDX in EDI, tests the
  delta sign, then enters the split branches already documented. Its natural
  callers include timer/state restoration/script-value paths; it is not itself
  a physical-melee submission API.
- The promising caller is `14034BF96`, reached through entry
  `14034BF70 -> 142304891`. Instructions select **signed `min(R8D,R9D)`**, negate
  it into EDX, clear R8D, and call `140322A60`. Thus this path really requests
  negative HP adjustment when the capped amount is positive. RDX was saved in
  RBX; its context/type and the origin/units of both amounts are still unknown.
- The continuation `142983004` is **not a return-only function**, despite its
  truncated pseudocode. It restores/forwards receiver, saved RDX, capped amount
  and flags through a stack-dispatched tail to `14032B080`. Preserve this tail
  when reconstructing effects/ownership; an isolated HP call omits it.
- Pinned RTTI identifies vtable `141322E68` as `EnemyIns`; its slot **`+0x640`**
  at `1413234A8` contains `14034BF70`. PlayerIns's same slot resolves instead to
  `140339600`; do not assume the enemy override is a universal player/enemy ABI.
- A bounded raw-byte scan for direct, non-SIB `CALL [register+0x640]` forms in
  executable PE sections found one candidate: **`14032F548`**,
  bytes `ff 90 40 06 00 00` (`CALL [RAX+0x640]`), followed by the jump to
  `1425C31F0`. This is an instruction candidate, not proof of its receiver or
  execution. The primary listing/decompiler omits this gap; nearby linear
  instructions and PE unwind bounds are not enough to reconstruct split flow.
- The ESI HP-add route's sign branch at `1425C3202` selects direct setter work
  for a nonnegative delta; the negative branch enters `142E44AAC`, which tests
  receiver `+0x2A7 & 4`, then can invoke virtual `+0x4F8`. This still needs the
  complete incoming producer/receiver path; do not label it ordinary damage.

**Qwen's next B probe:** start at `14032F548`. Recover the actual incoming
split edges and RCX/EDX/R8D/R9D plus stack arguments before that call; verify
RCX's dynamic class and link amount/context to the contact/attack producer.
Then correlate a natural hit/miss/block/invulnerable case under TESTING. The
enemy vtable link is stronger than a generic setter search, not permission to
call the override or substitute HP subtraction for native combat rules.

No new damage hook, native call, gameplay run or outcome acceptance was added
in this pass. The literal corpus search and the direct-call byte pattern do
not cover every indirect dispatch form. Keep B/C open.

- What producer populates the `0x1403C9B50` event object for an ordinary weapon collision, and how does it associate attacker, attack context, target generation, and contact geometry?
- What exact type-1 and type-2 event variants represent weapon hit, guard, parry, and non-combat interactions? Recover complete consumers and callsites; preserve the unresolved type-1 continuation and indirect dispatch targets.
- Which native callback/owner converts the relevant contact event into HP/stamina/poise/status changes, and what constitutes a confirmed result? Trace health-side consumers independently from the generic setter and compare ordinary hit, miss, block, parry, invulnerable target.
- The first runtime hit capture has dropped intervals; a clean controlled comparison remains required before execution milestones B/C can pass.

Do not bind or call any candidate native signature until argument flow, structure ownership/lifetime, simulation-thread semantics, and result correlation are independently corroborated. The older `physical-contact-recovery-verified-20261020.txt` and `physical-damage-outcome-verified-20261020.txt` logs end with `Analysis rollback mismatch` errors; their recovered pseudocode is not accepted as verified primary evidence. Execution milestone B remains open.

## Parameter-side readiness, 25 October 2026: the candidate vocabulary exists, the submit site does not

C5 asks for "one qualified contact on the native simulation owner". Two halves are
needed: a way to *state* the contact in the game's own terms, and the native owner that
accepts it. Only the first half was missing in code, and it is now present and
unit-tested in `src/core/combat_atkparam.h`, transcribed from the pinned research cache
(`docs/SOULSMODDING_REFERENCE.md` -> `build/research/soulsmodding-20261024`, pages
`ds1-refmat:attack-animation-schema` and `ds1-refmat:param:atkparam`):

- Attack identity `wepmotionCategory*10000 + hand + action`, `+5` at zero durability,
  with `Encode`/`Decode` round-trip. The `+5` is recoverable only because no two listed
  actions differ by exactly 5; the test asserts that over the enumeration instead of
  trusting a digit heuristic (QuickB is 1, so its worn form is 6; QuickARepeatWhiff is
  42, worn form 47).
- The 0x80-byte AtkParam row offsets as `static_assert`-checked constants, so a
  transcription slip fails at compile time rather than at a live read.
- Hit slots as sphere-or-capsule with priority, and `BladeOverlapsHitSlot` over a
  deterministic segment-segment / segment-point distance. This is the same primitive the
  row describes, so a candidate can name the slot it touched instead of approximating an
  enemy as a sphere.
- Damage in the native domain: base u16 times u16 percent (100 = 1.00x). Our swing-energy
  multiplier has exactly that shape, so it is expressed as a correction percent and the
  native owner keeps the base value plus every defense/cost rule. Encoding refuses 0,
  non-finite and >655.35 rather than clamping: a 0-percent submit would be a parameter
  edit, and a missed hit is expressed by never submitting at all.
- `QualifyContact` refuses an unverified hand/action, any whiff/deflect/kick row, an
  unencodable scale, and any touch the target row's own slot geometry vetoes. The
  sphere-only fallback is opt-in per call and is then recorded as unproven geometry,
  because dummy-polygon id -> endpoint resolution is still an open native question.

`BladeHit` now carries `tip_speed_mps`, so a candidate keeps the measurement that
produced the multiplier and a later review can tell a fast graze from a slow push.

**This closes nothing on the native side.** The header touches no game memory, selects no
live row, resolves no dummy ids, and submits nothing. C1 (real blade dimensions), C3
(native collision path), C5 (submit owner) and C6 (independent outcome) remain exactly as
blocked as before, and the C lane entry still requires the B contract. Verification for
this slice is offline only: `combat_atkparam` plus the full suite at 94/94 in Debug and
Release. The next live step is unchanged and is the operator's: a fresh combat-only
candidate receipt, then a labelled hit/miss/block/parry capture, read per case.

## 25 October Ghidra sweep: whole-program inventory and the contact-event dispatch model

Read-only pass against the live Ghidra GUI holding `DSRFull` (PID 19116, bridge
`http://127.0.0.1:8089`, program `/DarkSoulsRemastered.exe`). Session revision
token `1792646701740:9d9b1124:3` - same saved revision (modnum 3) as the token
pinned above; the epoch component is session-varying, so it is recorded here,
not substituted, for the pin. `unsaved_changes=false` before and after; the
`xref` tool group was loaded dynamically, no launch-config edit, no script, no
DB repair, no game launch. All decompilation was pulled through
`get_functions({address})`'s `decompiled_code` field, which runs the decompiler
without writing to the listing.

Whole-program inventory (one-shot, for future sizing of sweeps): 261,311
functions (260,545 internal), 766,333 symbols, 7,484 RTTI class names, 31,802
custom names (all infrastructure: `__security_check_cookie`, `eh_vector_*`),
11 segments including two `.text` blocks `140001000`-`14129c3ff` and
`142019000`-`14319afff` (the split-block region that produces the known
`halt_baddata` decompiler gaps, e.g. `1403C7ED0`). A global
`find_functions(is_thunk=true)` count does not complete inside the MCP timeout
at this scale; do not re-issue unbounded predicate scans.

Recovered dispatch model for a contact event record (all addresses decompiled
this sweep unless marked "prior"):

- `1403C4020` switches on the event's source kind (virtual read at `+0x10`):
  0 -> target vtable `+0x10`; 1 -> `thunk_FUN_1425BA7DC`; 2 -> `1403C4210`
  (target 0 -> `1402F84F0`, 1 -> `thunk_FUN_1429E5D7E`, 2 -> `1402F84B0`);
  3 -> projectile/object source, owner id at object `+0x9C` resolved through
  `1403714B0(DAT_141c77e50, ...)`, then a second-stage target switch
  (0 -> `140420B30`, 1 -> `thunk_FUN_142C9FC55`, 2 -> `140420A20`).
- The target-side accessors read a kind byte at record `+0x88` (1/2/3) and pick
  the sub-object slot `+0x2D8` / `+0x300` / `+0x328` accordingly, with
  `PTR_vftable_141a2dbd8` as the null-ish default:
  - `140425AB0` returns true only when the resolved object's byte `+0x9A & 7`
    equals 4 - a target state gate.
  - `140425870` calls sub-object vtable `+0x30`; if it returns nonzero and the
    object byte `+0x9B & 1` is clear it forces success (`1`), else it forwards
    the flag - an eligibility/ownership filter.
  - `140425990` forwards a virtual `+0x40` notification to the selected slot.
  - `140425B70` builds a contact frame: if the record-local direction
    `+0x30..+0x38` has squared length > 0.0005 it transforms the record-local
    point at `+0x20` via `1401E34D0`; otherwise it blends record fields
    `+0x1E4..+0x1EC` with `+0x14..+0x1C`. This is second-contact geometry in
    the same family as the prior `1403C7D30` finding.
  - `140425120` (size `0x5DC`) is the large SSE vector consumer of these
    frames; only its head was read this sweep - full read deferred.
- `1403C7460` is a guarded contact-builder entry: actor resolved by
  `1402C4870(*(manager + 0x40))` and `1402A9CC0(target)`, requires
  `actor+0x10 != 0` and kind `ushort actor+8 < 3 || == 4`, then an id from
  `1402A9E00` / `1402A9D60` (default `0xffffffff`), and finally calls the known
  builder `1403C7520`. The kind filter is the first hard evidence of an
  entity-kind discriminator on this path; its value domain is still unrecovered.
- `1403CA1D0` completes the completion-notify chain: resolves handle -> record
  (high 16 bits `< 0x80`: direct slot in the `0x230`-stride pool at
  `param_1[2]`; `== 0x80`: linked list through record `+0x88`), then writes a
  one-byte completion flag at record `+0x195`. So the byte written by the
  durability path (`1402FC360` notify, prior) is `+0x195`.
- `1403C4DA0` relinks the per-node handle lists under pool singleton
  `DAT_141c7a048`: walks list head `+0x18`, keeps nodes whose
  `1403C4360(node, param_2)` test passes, and when a node's flag word `+0x18`
  saturates (`(v-1^v)&0x3fffffff == 0` over the low 30 bits) recycles it
  through `1403C48F0`. Confirms the flag-word semantics inferred from
  `1403C5D20`'s refcount decrement.
- `1403C8DD0` builds two temporary pointer vectors with reserved capacity 0x20
  via allocator `140CC4940` and growth `1403C89F0`, after a list pass through
  `thunk_FUN_1403C4DA0(DAT_141c7a048)` - a snapshot-then-modify pattern over
  the handle lists.
- `1403C93B0` (event constructor, prior batch) zero-initializes a `0x3f0`-byte
  temp via `thunk_FUN_142F83184`, seeds sentinel `0xffffffff` fields, and reads
  the source component through its vtable `+0x28`. `1402FC750` (durability-zero
  consequence, 0x45B bytes, float-heavy, spawns the `z%09d` effect) was opened
  but its body was not fully read this sweep.

Interaction-subsystem anchors (not yet walked): RTTI names
`NS_FRPGWorldTalkManImpl`, `NS_FRPGWorldBlockTalk`, `NS_FRPGWorldAreaTalk`,
`NS_FRPGNpcTalkIns`, `NS_FRPGTalkAction`, `NS_FPPEzStateTalkEvent`,
`NS_FRPGFrpgEventConditionTalk`, `NS_FRPGFrpgMenuDlgTalk`. Name string
`.?AVWorldBlockTalk@NS_FRPG@@` sits at `141B0FAF8` (type descriptor at
`141B0FAF0`). A first vftable-recovery probe failed and its dead end is worth
recording: the listing region near `14197FXXX`/`141E05XXX` is not applied
MSVC-RTTI structure - it is a packed table of raw 32-bit RVAs without
relocation (values like `0x00EEB950`), Ghidra shows no xrefs into the type
descriptor, and hand-decoding "COL" fields there yields nonsense signatures.
Do not decode that region by hand. The clean next probe is a byte-pattern scan
of the `.rdata` range for the x64 COL shape - `01 00 00 00 | 00 00 00 00 |
00 00 00 00 | <chd rva32> | <td rva32>` matching the class's type-descriptor
RVA (e.g. `0x1B0FAF0` for WorldBlockTalk) - then read the vftable at
`COL-8` and enumerate entries with `list_class_members`.

What this sweep does NOT close: no producer of the `1403C9B50` event for an
ordinary weapon swing is yet identified; the kind `+0x88` value domain and the
actor kind `+8` domain are unrecovered; `140425120` and `1402FC750` bodies are
unread past their heads; the health-side owner is still untraced; no candidate
signature is bindable. B/C lane gates stay open and no native/headset gate
moved.

Next static probes, in order: (1) xrefs to `1403C9B50` / `1403C7460` to find
the swing-time producer; (2) full reads of `140425120` and `1402FC750`;
(3) the COL byte-pattern scan above for the Talk classes, then vtable walk of
`WorldBlockTalk`; (4) cross-check AtkParam row byte `+0x73` (between
`kDmgLevel=0x72` and `kAtkAttribute=0x75` in `src/core/combat_atkparam.h`)
against `build/research/soulsmodding-20261024` to name the field that selects
the event message ids 23/24/25 seen in `1403C72D0`.

### 26 October B-probe verdict: 14032F548 is an instruction candidate only, not a damage classification

Verdict (26 Oct): `14032F548` (`CALL [RAX+0x640]`) is byte-pinned as an
instruction candidate in a `.text1` split gap. It is not a damage
classification and not an HP-delta producer binding. The adjacent HP setter
`140322910` has 27 CALL + 3 DATA references per contract; `548` contributes
none of them.

Evidence (prior read-only pin, transcribed here; only the next-probe contexts
below were re-read this pass):
- Bytes `ff 90 40 06 00 00` (`CALL [RAX+0x640]`) at `14032F548`, followed by
  the forward jump to `1425C31F0` plus the `TEST ESI` sign branch.
- Zero xrefs to `14032F548`; RAX/receiver/arguments unrecovered.
- Split-gap classification: the primary listing/decompile omits this gap;
  nearby linear bytes and PE unwind bounds do not reconstruct split flow.
  Section `.text1`; surrounding bytes decode as a forged-JMP island, not a
  normal function body.
- Parallel `+0x638` slot function noted as a neighbouring virtual-slot lead,
  not a producer.
- Known `+0x640` slot inhabitants: EnemyIns `1413234A8 -> 14034BF70`
  (negative-HP `min(R8D,R9D)` path via `140322A60`) vs PlayerIns same slot
  `140339600`. No xref link from either inhabitant to `548`; do not promote
  the enemy override to a universal player/enemy ABI.

Limitations: no incoming split edges, no RCX dynamic class, no amount/context
link to a contact/attack producer, no runtime hit/miss/block correlation. The
effect-like HP path through `14032F480 -> 14217AFC4` (`+0x3EC` max-HP,
`140403250`/`140407110` percent+fixed, ESI delta at `1401901DE`) is adjacent
but not feeding `548`. Never invoke `548` or its neighbours: register/stack
state belongs to a larger split function.

Exact next probe (read-only; never invoke): recover the RAX/ECX producer via
the cross-region targets `142756FC5` / `1411B6015` / `14032F471` (plus
`1421B6015`, which selects `14032F54E` as an immediate - see outcome below),
compare the `+0x640` slot inhabitants above, then correlate a natural
hit/miss under TESTING. First half (assembly contexts) is done below; second
half (slot-inhabitant comparison + live correlation) remains open.

Slot-inhabitant comparison, second pass 26 Oct (read-only `read_memory`,
little-endian decode): `141322E68` (EnemyIns vftable) + `0x640` =
`1413234A8`, holding `0x14034BF70` — the `thunk_FUN_142304891`
(`min(param_3,param_4)` → `FUN_14034bf96(param_1,-min,0,param_4)`) negative-HP
path. PlayerIns same slot holds the `140339600` self-stub
(`thunk_FUN_140106e73`, 2 bytes, no-op). Each inhabitant is DATA-xrefed from
its own table only; zero xrefs link either table or inhabitant to `14032F548`.
So the `+0x640` slot is real and class-divergent, but `548`'s receiver cannot
be typed from it — the comparison closes the "is +0x640 a damage slot"
question (negative-HP path on EnemyIns, self-stub on PlayerIns) without
promoting `548` to either. Live hit/miss correlation under TESTING remains the
only open half; never invoke `548`.

Probe outcome this pass (read-only GhidraMCP against the live GUI holding
`DSRFull`, `get_assembly_context` with 12 instructions each side; no scripts,
no repair, no save, no launch):
- `142756FC5` = `TEST AL,AL`. Before: forged-return island (`JMP
  0x1428d0d28`, `PUSH/POP/XCHG/RET` shuffle). After: return-address selector
  `MOV RBP,0x14032f584` / `XCHG [RSP],RBP`, then `MOV RAX,[RSP+0x10]` /
  `MOV RCX,0x14032f594` / `CMOVNZ RAX,RCX` / `MOV [RSP+0x10],RAX` / `RET`.
  Same thunk/return-pool family as the `0x14031D5xx` pool; `584`/`594` sit
  `0x3C`/`0x4C` past `548`. Not a producer recovery.
- `1421B6015` = `MOV R14,RAX`. After `TEST RAX,RAX` at `1421B6018`: `PUSH RBP`
  / `MOV RBP,0x14007a815` / `XCHG [RSP],RBP`, then `MOV RDX,[RSP+0x10]` /
  `MOV RBX,0x14032f54e` / `CMOVZ RDX,RBX` / `MOV [RSP+0x10],RDX` / `RET`.
  `14032F54E` (the `548` fallthrough, `548`+6) appears here as a
  condition-selected return-address immediate (`CMOVZ`), not via xref.
  RAX/ECX producer still unrecovered; no invocation.
- `1411B6015` = `MOV RCX,[R10+0x30]` (as literally addressed). Window before
  ends in `RET` at `1411B5FE7` (split gap); after `CALL 0x1411ba160` etc. No
  link to `548`. Treated as a negative control for the similarly numbered
  `1421B6015` hit above.
- `14032F471` = `JMP 0x142656c78`. Before decodes as non-code/garbage
  (`IN EAX,0x81`, `ADC`/`SBB` with huge displacements); after `NOP` /
  `PUSH RSP` / `NOP` / `JMP 0x14217afc4` (the `14032F480` effect-like entry),
  then `INT1`/`OUT`/garbage. Same forged-JMP island immediately preceding
  `14032F480`; not a trustworthy linear producer path.

### 26 October AtkParam +0x73 naming linkage (cache transcription only)

`src/core/combat_atkparam.h` now names `kMapHitType = 0x73` and
`NativeMapHitType` (`Normal` 0 / `Precision` 1 / `NotOnMap` 2 / `MapNotAsset`
3), transcribed from the research cache (`ds1-refmat` `mapHitType`,
`ATK_PARAM_MAP_HIT`; cf. cached `ds1-refmat__param__atkparam.wiki` line 55
and Paramdex `AtkParam.xml` in `build/research/soulsmodding-20261024`). This
closes probe (4) above only as vocabulary: it is not DSR-verified, it is not
bound to any native call, and `0x74` (`guardCutCancelRate`) stays deliberately
unnamed. In particular the `1403C72D0` message-23/24/25 selection still needs
DSR verification that it reads row byte `0x73` before any hit-context claim.

### 26 October type-64 (parry-context) static probe: type-2 dispatch table, +34 gates, no ==64 consumer

Read-only live GhidraMCP against the GUI holding `DSRFull`
(`DarkSoulsRemastered.exe`; PID 17320 per `list_instances`). Revision token
`1792646701740:fa2c6e65:3542`, `unsaved_changes=true` before and after; no
save, repair, script, launch or write performed (`GHIDRA_MCP_ALLOW_SCRIPTS`
untouched). All decompilation via `get_functions` (`signature,callers,
decompiled_code`, no call context); listings via `disassemble_function` only.
Three calls timed out with no retry storm and are recorded as gaps:
`disassemble_function(1405290C0)`, `get_xrefs_to(1402DEB20)`, one
`get_assembly_context` call.

Type-2 dispatcher `1403C4210` (sole code caller `1403C4020`; plus one DATA ref
at `141d53e64`) switches on the target object's virtual `+0x10` result:

- kind 0 -> `1402F84F0` (sole code caller `1403C4210`; one DATA ref at
  `141d47b10`). Body, unconditionally: `1403CA1D0(pool, [record+0x140], 1)`
  (completion-notify, writes record `+0x195` per prior pin) then
  `1403E6450(target, record)`; returns 1. **No `+34` read.**
- kind 1 -> `thunk_FUN_1402F8410` (5-byte `JMP 1429E5D7E`; sole code caller
  `1403C4210`). The decompiler renders `1429E5D7E` as `LOCK/UNLOCK` only, but
  the listing is a forged-return selector: `CMP byte [RDX+0x524],0x80`, then a
  `PUSH/POP/XCHG/RET` shuffle choosing between `0x140045643` and `0x1402F8494`
  (`CMOVNC`), and `0x1402F8494` itself is `JMP 0x142C58024`. Continuation and
  the `+0x524` gate meaning are **open**; do not step through the forged RET.
- kind 2 -> `1402F84B0` (sole code caller `1403C4210`; one DATA ref at
  `141d47b04`). Body: `if ((target != source) && (*(int *)(record + 0x34)
  < 0x40)) { 1402FC360(target, record, 0); return 1; } return 0;`
  (listing: `CMP [RCX+0x34],0x40; JGE fail`). So attack_type 64 **fails**
  the damage gate (self-contact also excluded).
- else -> `return uVar & 0xffffffffffffff00` (no consumer).

The `< 0x40` gate is consistent across the type-3 consumers read this pass:
`140420A20` (type-3 target 2) requires `140425990(...) && [record+0x34] <
0x40 && ids differ` before frame build + `1402FC360` + `140424F90`; `140420B30`
(type-3 target 0) requires `140425870(...) && [record+0x34] < 0x40` before
`140425120` + `1403E6450` + notify. `1402FC360` itself (951 B; callers
`1402F84B0`, `14031D8DB`, `140420A20`) reads durability `+0x42`, floats
`+0x18`/`+0x18C`, flags `+0x45`, ids and effect fields, and calls `1402FC750`
on zero durability — but **no `+34` read of its own**; the gate lives in its
callers. `1403E6450` (532 B; callers `1402F84F0`, `14031DA7F`, `140420B30`) is
the parallel effect chain (same `+0x18C` timer shape, `z%09d` spawn), also with
no `+34` read.

Producer (re-read, not re-derived): `1405290C0` stores its attack-type argument
at payload `+0x34` and, only when `*(int *)(payload + 0x34) == 0x40`, fills
payload `+0x30` through `1402DEB20` (actor virtual `+0x268` -> PlayerIns
`14035F1F0` -> `1402DEB70`, weapon row `+F2` parry value per contract).
Wrappers `140528CE0` (callers `1403C93B0`, `1403C9670`), `140528E90` (caller
`1403C9240`) and `140528F70` (caller `14035ACBE`) forward the attack-type
argument through register/stack shuffles pinned in disassembly; **which
upstream passes 64 vs 0/2 was not recovered** in this probe. `1403C93B0`
itself is reached from `1403C9B50` (caller `140490F40`), preserving the event-
constructor chain; no new producer found.

`+34` offset xref is **not resolvable as an xref**: `get_xrefs_to` is
address-indexed, not field-indexed, so readers were enumerated by decompile/
listing text instead. Found: `1402F84B0`, `140420A20`, `140420B30` (all
`< 0x40`). Open: **zero `== 0x40` consumers found**; the payload `+0x30`
parry-value consumer is still open. No binding is proposed.

Parry-vs-damage static discriminator status: **negative-only**. A type-64
payload fails every `< 0x40` damage gate, so it is statically excluded from
the `1402FC360` durability/effect chain; the only non-gated type-2 consumer
(kind 0 `1402F84F0`) would still fire, but its body is completion-notify plus
the generic effect chain with no parry timing, window or selection logic, so
it cannot be claimed as a parry handler. Kind-1 (thunk) routing is unresolved.

Limits: no incoming-edge recovery for `1429E5D7E`/`1402F8494`, no `+0x524`
semantics, no `+30` consumer, no attack-type value-domain at the wrappers, no
runtime correlation. Never invoke any of these continuations; register/stack
state belongs to larger split functions.

Exact next probe: natural parry correlation under TESTING — operator-confirmed
parry-only vs hit/miss/block captures with `native_combat_context` and event
handles, read per case; static follow-up only if needed is the `1429E5D7E`
continuation (`142C58024` target, `+0x524` gate) and a `+0x30` consumer search.

### 26 October B swing-time producer probe: xref sets re-verified, no swing caller

Read-only live GhidraMCP against the GUI holding `DSRFull`
(`DarkSoulsRemastered.exe`). Revision token `1792646701740:fa2c6e65:3542`,
`unsaved_changes=true` before and after; no save, repair, script, launch,
write or commit (`GHIDRA_MCP_ALLOW_SCRIPTS` untouched).

`get_xrefs_to` (cap 100, totals below cap so nothing paged out):

- `1403C9B50` -> exactly **2 refs** (prior knowns re-verified, zero new):
  DATA `141D54398`, UNCONDITIONAL_CALL `1404910AD` in `FUN_140490F40`.
- `1403C7460` -> exactly **3 refs** (prior knowns re-verified, zero new):
  DATA `141D5417C`, DATA `1417991F0`, DATA `141799224`. No code caller.

`get_assembly_context` (12 instructions each side): `1404910AD` decodes as
`CALL 0x1403c9b50` (before: stack/XMM shuffle marshalling; after:
cookie-check `XOR RCX,RSP` / `CALL 0x1410e4080` epilogue). The four DATA
addresses return `No instruction at address` — they are data tables, not
code, so no caller classification applies beyond that.

| xref source | kind | classification |
|---|---|---|
| `1404910AD` -> `1403C9B50` | code CALL | event-condition (ObjDam condition wrapper, see decompile) — excluded as swing |
| `141D54398` -> `1403C9B50` | DATA | other (address table; first dword little-endian `50 9B 3C 00` = RVA `0x003C9B50`) |
| `141D5417C` -> `1403C7460` | DATA | other (address table; first dword `60 74 3C 00` = RVA `0x003C7460`) |
| `1417991F0` -> `1403C7460` | DATA | other (table holding RVA `0x003C7460` plus small-int fields `0x00020521`/`0x000A6405`; not code) |
| `141799224` -> `1403C7460` | DATA | other (same table, first dword again `0x003C7460`; not code) |

Bulk `get_xrefs_to` on the four DATA holders returns empty for each, so no
second-hop code owner is xref-indexed. No update-loop, event-condition-code
or swing-animation caller exists in either set.

Wrapper chain check: `FUN_140490F40` itself has exactly 2 refs: DATA
`141D612F8` plus `1404B0EC7`, whose listing is `JMP 0x140490f40` (type
reported UNCONDITIONAL_CALL). Its 12-each-side window is a split/forged-JMP
island (`JMP 0x14071abf0` / `JMP 0x14283a9f3` before, `PUSH RBP` /
`XCHG [RSP],RBP` / `JMP 0x142698597` after) — classified other/split, not a
swing-animation callsite.

Most swing-plausible caller (the only code caller, not the ObjDam path by
choice — there is no other): one-level `get_functions` on `FUN_140490F40`
(`signature,decompiled_code`, no call context). Signature
`undefined FUN_140490f40(void)` (decompiler renders 7 params:
`param_1..param_7`). Body: resolves `lVar3` from `DAT_141c77e50 + param_3`
and `lVar4` from `DAT_141c77e50 + param_4` via `140371200/140371360/
140371bd0` with a `10000` special case (`+0x68` direct vs `-0x2711`
adjusted lookup), then gates `if ((lVar3 != 0) && (lVar4 != 0))` and calls
`FUN_1403c9b50(DAT_141c7a050, param_2, lVar3)` — `lVar4` only gates, never
forwarded; stack `local_48` words (`param_5/6/7`) are staged but not passed
to the constructor. Passed arguments are therefore a manager/global, an ID
(`param_2`) and a resolved actor/context object (`lVar3`). No animation ID,
weapon row, motion category/hand/action, swing timing or geometry is
referenced anywhere in the body. Verdict: object-damage condition wrapper,
not attack-animation/weapon-swing. Excluded as the swing-time producer.

Producer candidates ranked swing-plausible first: **none** — the xref sets
contain no swing-animation, update-loop or second construction callsite.
The sole code path into the event constructor is the ObjDam condition
wrapper above; every other ref is a DATA address-table entry.

Limits: cap-100 enumeration is complete (totals 2 and 3); DATA entries
cannot yield assembly context; RVA-decode identifies the table contents but
does not prove unwind- vs dispatch-table ownership (the `1417991F0/224`
record shape is undecoded); live DB `unsaved=true` means the listing may
differ from the saved project; no runtime correlation performed.

Exact next probe: the builder `1403C7460` has zero code xrefs, so its
callers reach it indirectly — decode the `1417991F0/224` record structure
(step the 12-/16-byte stride past the RVA word) to name the dispatch owner,
and recover `param_2/param_3/param_4` provenance backward through the
`1404B0EC7` split island without invoking anything. If that stays indirect,
correlate natural swings under TESTING instead of widening the static
search.

### 26 October B static probe: 1417991F0/224 decode — PE unwind metadata, no dispatch owner

Read-only live GhidraMCP against the GUI holding `DSRFull`
(`DarkSoulsRemastered.exe`, PID 17320 via `list_instances`; no revision/save
state was asserted this pass beyond the live connection). No save, repair,
script, launch, replay, write to the DB or commit
(`GHIDRA_MCP_ALLOW_SCRIPTS` untouched). This corrects the preceding
subsection's table gloss (lines "table holding RVA ... plus small-int
fields"): there is no 12-/16-byte dispatch record here.

Raw `read_memory` (64 B each; little-endian dwords):

- `1417991F0`: `60743c00 9d743c00 d8917901 21050200 05640a00 9d743c00
  b9743c00 e8917901 21000000 9d743c00 b9743c00 e8917901 21000000
  60743c00 9d743c00 d8917901` = RVA `0x003C7460`, `0x003C749D`,
  `0x017991D8`, `0x00020521`, `0x000A6405`, then fragments repeating
  (`0x003C749D`/`0x003C74B9`/`0x017991E8`/`0x21`).
- `141799224` (= `1417991F0+0x34`): `60743c00 9d743c00 d8917901 19320700
  21014000 13f011c0 0f600e30 0d500000 743f0e01 e0010000 21110400
  11e43e00 04743f00 20753c00 55753c00 30927901` — the first triad recurs,
  then the byte stream leaves the RVA pattern entirely (compressed unwind
  codes / next scope). It is not a second record.
- `141D5417C` (contrast): `60743c00 9d743c00 d8917901 9d743c00 b9743c00
  e8917901 ...` — an exact 12-byte triad, then the next function's triad.

Stride decode: **12 bytes** = `RUNTIME_FUNCTION {BeginAddress u32 RVA,
EndAddress u32 RVA, UnwindInfoAddress u32 RVA}`. At `141D5417C`
(`.pdata`, `141D0B000`-`141E5D9FF`): `Begin=0x1403C7460`,
`End=0x1403C749D` (61 B body — the "174 B" in earlier notes is the
decompiler extent `1403C7460`-`1403C750D`, not the unwind span),
`UnwindInfo=0x1417991D8`. `1417991F0/224` lie in `.rdata`
(`14129D000`-`141A24BFF`), i.e. they are **UNWIND_INFO**, not records:
`1417991D8` = version/flags `0x01`, size-of-prolog `0x17`, count-of-codes
`0x06`, frame reg/offset `0x00`, then code array `17340C00 175213F0
11E00F50`, then padded handler RVA `0x00020521` (`1417991F0` itself) +
chained `0x000A6405`. The "small-int fields" `0x00020521`/`0x000A6405`
seen earlier are that handler/chained-info dword pair, not record fields.
Unwind infos repeat at `0x1417991D8`/`0x1E8`/`0x1FC`/`0x210`/`0x220`
(fragments `1403C749D`-`1403C74FA`/`1403C74FF` etc.), so both hit
addresses are mid-structure offsets, never table bases. The matching
`.pdata` triads sit at `141D5417C`/`141D54188`/`141D54194`/`141D541A0`/
`141D541AC`/`141D541B8` for the `1403C7460`..`1403C7575` fragment chain.

Xrefs (`get_xrefs_to`, cap 20/address, all totals below cap): to
`1417991F0`, `141799224`, `141D5417C` and neighbours
(`1417991E0/E4/EC/FC/200/214/218/234/244`, `141D5416C/88C/9C`) — only DATA
refs **from** `.pdata` unwind slots (`141D5419C->1417991FC`,
`141D541A8->141799210`, `141D541B4->141799220`, self-refs among the
`1417991D8` family); to `1403C7460` exactly the same 3 DATA refs
(`141D5417C`, `1417991F0`, `141799224`). Zero code READ/WRITE/COMPUTED_CALL
through any of them. No analysed function indexes these tables; they are
PE exception/unwind metadata consumed by the loader/`RtlLookupFunctionEntry`,
not a dispatch table. Task 3 is therefore void: no dispatch owner exists
here, no indirect invocation of `1403C7460` is recorded, and no swing
producer is claimed. The builder's zero code xrefs remain unexplained by
this lane — its callers are not reachable through these addresses at all.

Owner: **open (none found; the named tables have no dispatch owner).**
Limits: xref index only (raw immediate references to the table VAs would
not be indexed, same caveat as before); unwind-code semantics beyond the
header/handler dwords not decoded byte-by-byte; no runtime correlation.

Exact next probe: stop following `1417991F0/224` — instead recover the
callers of the update-path chain `14024F6B0 -> 1403C8DD0 ->
1403C6030/1403C6850/1403C7D30 -> 1403C7520` backward from `1403C7460`'s
sole known callee relationship (`1403C7520`, called by `1403C6030`,
`1403C6850`, `1403C7D30` per the observed-paths list) and ask which of
those, if any, reaches the guarded builder `1403C7460`; the outer caller
to name first is the caller of whichever of `1403C6030/1403C6850/1403C7D30`
first references the `1403C7460` fragment range. If none does, correlate
natural swings under TESTING instead of widening the static search.

### 25 October continuation: update-owner probe and pinned map-hit messages

Read-only GhidraMCP 7.0.0 against the already-open GUI `DSRFull`, PID 17320,
program `DarkSoulsRemastered.exe`. The tools were not exposed by this chat;
an inline MCP SDK client invoked the installed stdio bridge and checked
`tools/list` before using its advertised read-only tools. No reusable shim,
headless analysis, script execution in Ghidra, database repair/save or game
operation. Revision `1792646701740:fa2c6e65:3542`, unsaved true throughout
the returned function bundles. Empryo's current config lists REA only; the
working bridge executable is `~/.local/bin/bridge-mcp-ghidra.exe`, with the
documented default groups and automatic GUI connection at localhost:8089.

The preceding update-path probe is now bounded and answered:

- `14024F6B0` calls `1403C8DD0` at `1402500D8`, with
  `RCX=[141C7A050]` and `XMM1=XMM7`, gated by `R14B`. This names the
  outer update callsite, not the manager's lifetime or simulation thread.
- `1403C8DD0` calls `1403C6030` at `1403C8F0B` and `1403C6850` at
  `1403C8FC3`. The latter calls `1403C7D30` at `1403C68CE` and
  `1403C7520` directly at `1403C6B8D`.
- `1403C7520` has exactly three indexed code callers: `1403C6B8D`,
  `1403C7DD1`, `1403C74F0`. Thus the prior `1403C6030 -> 1403C7520`
  edge was wrong: `1403C6030`'s indexed callees include the direct
  dispatcher `1403C4020`, not that builder.
- None of these indexed update functions calls `1403C7460`. Xrefs to
  its unwind-fragment entries `7460/749D/74B9/74FA/74FF` contain only
  metadata and its own internal conditional branches. There is no newly
  recovered swing caller or dispatch owner. This is a bounded index
  result; computed/obfuscated entry and execution remain unknown.

One positive field boundary is independently established. `1403C72D0`
selects AtkParam via event `+0x84`/`+0x80` and `140528910`, then
**`1403C733A: MOVZX EBX,byte ptr [RAX+0x73]`** reads the selected row.
Its value 1 selects message 24, 2 selects 25, otherwise 23, sent through
the receiver's vtable `+0x18`; a missing row follows the default. This
closes the earlier question whether DSR actually reads `+0x73` here.
It does not identify the messages' effect, a victim outcome, a defense
window or a physical-damage submission ABI.

Raw MCP results and `primary-byte-verification.json`:
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\physical-combat-20261025-update-owner\`.
The 164-byte `1403C72D0` and 174-byte `1403C7460` windows match all
**338 bytes** of the shipped executable, SHA-256
`a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`.
No native execution or natural hit/defense correlation was observed.

Next probe: retain the now-named `1402500D8 -> 1403C8DD0` owner as an
observation anchor in a qualified disposable-save transaction. Correlate
ordinary hit/miss/block/parry-only selected contexts and generated handles
with independent outcomes before pursuing a submission seam. Do not repeat
the refuted unwind-table or `6030 -> 7460` caller premise. B/C remain open.

### 26 October resume: type-1 continuation and class-specific response gates

The earlier "atomic failure / no melee handler" interpretation above is refuted.
The recorded flags at the first selector originate at
`1427D78B5: CMP byte ptr [RDX+0x524],0x80`, not an atomic operation.
`14280BC9B` chooses the false-return epilogue only for the rejected comparison;
the other path reaches `142B8F84B`, then `1421048F9` compares target/source
pointers and selects the non-self continuation. A forged return is control flow,
not evidence that the destination cannot be a gameplay handler.

Retained `physical-combat-20261026-first` primary windows recover the ordinary
path through `140327AD0` eligibility, `1403256A0`, position/direction work and
the response callbacks. This resume independently pins **336 bytes** against
EXE SHA-256 `a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`:
`142F9534B` (128), `14031D712` (192), and two 8-byte vtable slots.

- The ordinary branch calls target vtable `+0x480` at `14031D727`, with
  RCX=target, RDX=record and R8D=0. A false result exits; the accepted branch
  calls source `+0x460` at `14031D741` with target/record in RDX/R8.
- The verified PlayerIns `+0x480` slot `141325670` contains `140358460`;
  the retained EnemyIns table slot `1413232E8` contains `14033A2B0`.
  The latter follows `14033A233 -> 140CE6C72` and returns AL=1. It must not
  be used to infer the PlayerIns defense policy.
- PlayerIns `140358460` captures the record in RSI, target in RDI, loads
  record `+0x54`, and branches on value 2. With R8B=0 it reads native
  target-state fields through target `+0x68 -> +0x48` (`+0x31C`, `+0xA0`).
  Bit meanings remain unknown. Other record-kind branches can set native
  state byte `+0x20D`; no damage/guard/parry label is assigned to that byte.
- The separate `record+0x34 >= 0x40` branch is not universally rejected:
  exactly 0x40 proceeds to target state `+0x1E4 == 99999` and source state
  `+0xA0` bit 2, then a source `+0x270` float-return comparison. One path
  invokes source `+0x478`; another reaches `140386F00` via `14031D85E`.
  This is a recovered branch, not a classified physical hit or an HP write.

Evidence: `C:\Tools\DarkSoulsRemaster_De\combat-evidence\physical-combat-20261026-resume\`
contains raw GUI MCP replies, primary-byte-verification.json and primary walks.
REA 6.8.0 reports Windows PE analysis available, but it does not bind the live
primary .gpr database. The installed GhidraMCP stdio client reached GUI DSRFull
PID 17320 without scripts, reanalysis, repairs or saves. Three requested listing
addresses remained undefined; the primary EXE supplied their bytes instead.
`primary-14031d73a.json` is an explicitly rejected interior-instruction seed,
not flow evidence. Walks stop at conditional/stack-selected branches; they are
not complete CFGs. No native call, game-memory access, input or deployment ran.

Next bounded probe: recover the successful PlayerIns `+0x480` kind-2 branch
from `140095D94`, then the source `+0x460` notification and context owner.
Correlate native selected-record identity and defense outcomes before enabling
submission. The actor generation, record lifetime, simulation thread and exact
HP/stamina/reaction attribution remain open. B/C and native defense stay OPEN.

### 26 October qualification continuation: raw response evidence, not gameplay acceptance

Continued the PlayerIns kind-2 branch from 140095D94. Pinned static flow uses
140321B70 for target position, 140322090 for the planar orientation, normalizes
both horizontal vectors, takes their dot product and calls the named Cosf at
140D30620. The multiplier at 141615C54 is 0.01745329238474369; the first angle
constant at 141A2BE38 is 120. A later accepted branch at 1425B9E0C writes target
module +1E4 to 1006/1007, sets +31C bit 2 and stores 1.0f at actor +404.
These are concrete state writes in shipped code, NOT a recovered HP write or a
qualified shield/parry interpretation. Neither these helpers nor the state writes
were invoked. Source +460 notification still depends on the target's virtual
+150/+148 predicates and native event identity; that owner remains open.

Primary GUI MCP results and bounded shipped-byte walks are retained at
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\physical-combat-20261026-qualify\`.
`primary-verification.json` compares 125 bytes across five DB windows to the
pinned EXE. Raw walks stop at conditional/stack-selected transfers and are not
complete functions. The exact ASCII registrations for CreateDamage_NoCollision
and OnCharacterHP_CheckAttacker still lead to the previously recorded event
registration path; they are not new damage APIs.

A real observer seam regression demonstrated missing response evidence: native
record +54 and +168/+169 could change across dispatch, but diagnostics serialized
only zeros. Added a version-1 response payload to the EXISTING diagnostics event;
the five-event atomic batch, one natural original call, actor/capture-generation
guards and 18-query cold permission budget are unchanged. No native gameplay
write or speculative binding was added. Coalescing the pair, handle and status
reads makes room for the after-kind read; failed extended reads fall back to the
established pair/context reads instead of erasing otherwise valid observations.

### 26 October: diverted path converges back on +0x480 — correction

Two bulk calls, read-only, no invocation. The prior note called the
non-0x1D side a "separate 9C0 continuation" with deferred recovery.
Its body is now recovered and the label was wrong:

- `1403589C0` is a split-jump head into `FUN_142ffae49` (6
  instructions): `MOV RAX,[RCX]` (object vtable), `MOVZX R8D,R9B`
  (the source-side R9B=`(AL==0)` flag from `1428447D3`), `PUSH R10 /
  POP RDX` (receiver register swap), then
  `PUSH qword ptr [RAX+0x480] / RET` — an indirect tail-jump into
  the receiver's `+0x480` slot. The `CMOVNZ` at `14284f951` chooses
  only the *return-address context*: `0x1D` continues through
  `1403589A1`, anything else re-enters `+0x480` via `142FFAE49`.
  Both guard outcomes invoke target virtual `+0x480`; the guard
  selects *how*, not *whether* `+0x480` runs.
- `1404f5ab2` (reached from `142d805f9` when RDX != 0):
  `TEST R8,R8; JZ -> 140488694; JMP 140c5fb52`. `140488694` is
  `JMP 142d5f79f`, whose body is an unrecovered jumptable (computed
  dispatch — execution unknown). `140c5fb52` forwards
  `(param_3+0x10)` to `FUN_1404885d3` (unrecovered here).
- `thunk_FUN_142134a3b` (`1404885a0`) has two indexed callers:
  `FUN_14031d953` (the `+0x460` source-side tail) and
  `FUN_140420a20` (second caller — identity and context unrecovered,
  not assigned). Sole indexed callee: the jumptable `142d5f79f`.
- Corrected chain: source-side `+0x460` completion
  (`14031d953 -> 142134a3b -> 142d805f9`) NULL-checks RDX
  (`JZ -> 140488694 -> jumptable`), else falls to `1404F5AB2` where
  R8 selects jumptable vs `140c5fb52 -> 1404885d3`. Register roles
  (R8/RDX provenance) remain open; no semantics assigned.
- Still open: `DAT_141C7B1B0 == 0` tail selection, `+150/+148`
  predicates, HP/stamina attribution, jumptable destination set.
  B/C/H gates unchanged — convergence on `+0x480` is still static
  shape, not a submission binding.

### 26 October follow-up: +0x490 guard semantics, tail flag write

Same read-only session, two bulk calls, no invocation:

- `FUN_14284f926` (the `+0x490` slot body, 14 instructions):
  `CMP dword ptr [R8+0x160],0x1d` then `CMOVNZ` selects the return
  address: `0x1D` continues into `1403589A1` (the `+0x480` forward at
  `14031D91F: CALL [R10+0x490]`-adjacent chain), anything else diverts
  to `1403589C0` (head: `JMP 142FFAE49`). So the guard is exact —
  record-field `+0x160 == 0x1D` admits the PlayerIns `+0x480`
  continuation, all other values take a separate `9C0` continuation.
  Neither continuation's gameplay meaning is classified; this is still
  not a damage submission binding.
- `1403589C0` head is `JMP 142FFAE49` (5 bytes), i.e. a split entry,
  not inline code — further recovery deferred to a later bulk probe.
- `FUN_142ed722a` (tail callee, 36 bytes decompiled): sets bit 12
  (`| 0x1000`) on `*([*([RDI+0x68])+0x48]+0x1DC)`. If byte
  `[RBX+0xB4] == 0` calls `FUN_140316af2`, else
  `thunk_FUN_1402110f8`. `140316af2` is only `TEST R14B,R14B`
  branching `JZ -> 14031d98f` vs `JMP 142d50195`.
- `FUN_142134a3b` is register-save prologue (`RBX`/`RBP`/`RSI`
  spills) then `JMP 142D805F9`; `142D805F9` spills RDI/R14, moves
  R9->RBP, RDX->RDI, RCX->R14, NULL-checks RDX (`JZ -> 140488694`)
  else `JMP 1404F5AB2`. Register roles at those two targets remain
  open — no semantics assigned.
- Still open: `DAT_141C7B1B0 == 0` tail-selection condition,
  `+150/+148` predicates, HP/stamina attribution, and the `9C0`/other
  continuations. B/C/H gates unchanged.

### 26 October bulk probe: +0x490 slot target named, 14031D958 tail placed

Single bulk `get_functions` call (no repeats), read-only, no invocation:

- PlayerIns `+0x490` slot `1403589B0` is a 5-byte thunk
  (`thunk_FUN_14284f926`) forwarding to `FUN_14284f926`. The slot
  target is now named; the body of `14284f926` (where the earlier
  `+0x160 == 0x1D` guard and `+0x480` forward are expected to live)
  is still unrecovered — one hop remains.
- `14031D958` lies inside `FUN_14031d953` (entry `14031d953`, body
  through `14031d991`). Decompiled body calls `thunk_FUN_142134a3b()`
  then `FUN_142ed722a()`; indexed callees also include
  `thunk_FUN_1402110f8` (`14031d913`).
- `FUN_14003d493` (size 13, wrapper) is the sole indexed caller of
  `FUN_14031d953`, forwarding `(param_1, unaff_RDI)`; its own sole
  indexed caller is `FUN_14295b2ac`. This places the tail on the
  source-side completion path: `14031D939 -> 14295b2ac -> 14003d493
  -> 14031d953 -> {142134a3b, 142ed722a}`.
- The `DAT_141C7B1B0 == 0` branch condition selecting this tail, the
  `+150/+148` predicate meanings, and HP/stamina attribution remain
  open. B/C/H gates unchanged: no damage submission binding exists.

Diagnostic values (all exact 16-bit limbs/bytes): 0=read mask; 1..2=unsigned
record +54 before; 3..4=unsigned +54 after; 5..6=+168/+169 before;
7..8=+168/+169 after; 9..14=reserved zero; 15=payload version 1.
Read-mask bits 1/2/4/8 independently certify those four groups. Failed reads are
unknown/null, not observed zero. Old all-zero diagnostics retain unknown response
fields. Malformed or unsupported nonzero versions are rejected by the analyzer.
The report's native_response fields stay RAW; they never label a successful
block/parry, override actor identity refusal or mark physical combat accepted.

The actual C++ detour/writer fixture fails before the change with missing response
payload; the Python consumer independently fails on the newly emitted payload.
Regression now exercises full 32-bit values, all 16 partial-read masks, malformed
limbs/bytes/versions, optional-field read failure with actor/context preservation,
exactly-once natural dispatch, unchanged capture generation and whole-batch drops.
Synthetic seam success is not native attack/defense qualification. Full Debug,
Release and observer results are recorded in the current MODLOG entry.

### 26 Oct kind-writer probe: creation chain placed, kind assignment one hop out

Three bulk get_functions calls plus one bulk disassembly, read-only, no
invocation. The kind field (+0x54) is written at record *creation*, not
in the contact/dispatch chain:

- 1403C6030 builds only geometry (sqrtps/divps normalizations into
  +0x130..+0x18c); nothing in it writes +0x54. The contact builder
  consumes kind, never assigns it.
- 140327AD0 is an eligibility gate (target-state/module-flag checks,
  returns 1/0), called from 14042099B (stub: gate then
  thunk_142e303c7), itself called from 14068BDCF. Gate, not writer.
- Creation path: 140490F40 (top: resolves two actor refs via
  140371200/140371360/140371bd0 against DAT_141c77e50, then 1403C9B50)
  -> 1403C93B0 (builds attack-payload locals, calls 140528CE0) ->
  140528CE0 (AtkParam lookup via 140528910, then 1405290C0 payload
  builder if local_10 != 0) -> 140529670 writes the event. 1403C9B50
  also validates the 0x230-slot pool and calls 1403C6DA0/1403C5C90
  on failure.
- The exact +0x54 store is inside the unrecovered split bodies
  1405290C0 (head: standard prologue, AtkParam row fetch via
  [RAX+0x28], then 140528A30/140528910 lookups), 140529670, or
  142AC2A92 (8-instruction split-jump shim). One hop remains.
- 140490F40 has zero indexed callers -- the native attack-submission
  entry (who calls it: weapon swing dispatch vs spell vs trap) is
  unrecovered. 1403C93B0 has two more indexed callers (1404D0BB0,
  1404D0F20) whose contexts are unclassified.
- Why the session saw kind-0 only: the captured records came through
  the trap/block/parry-riposte exchanges, and the kind selector sits
  below the still-unrecovered submission entries -- no defect in the
  observer (mask 15 genuine). Deliberately eliciting kind-2 requires
  the submission entry, not a different play pattern.

27 Oct bounded static follow-up (GhidraMCP, `DarkSoulsRemastered.exe`, read-only):

- `FUN_1404D0BB0` constructs `NS_FRPG::FrpgEventConditionObjDam`; on a matching object-type byte `6`, it calls `FUN_1403C93B0` and stores the returned context. This is an event-condition constructor, not a player damage submission.
- `FUN_1404D0F20` re-evaluates the object/geometry condition, may retire an old context through `FUN_1403C9FE0`, then rebuilds through `FUN_1403C93B0`. It does not show an HP write.
- `FUN_14068BDCF` is a register-preserving wrapper to `FUN_14042099B`, which reaches eligibility gate `FUN_140327AD0`; this is not a damage writer.
- The complete decompilation of `FUN_1405290C0` copies selected AtkParam/actor values into the event payload and returns success; no HP/health write is observed.
- The ASCII strings `OnCharacterHP_CheckAttacker` and `CreateDamage_NoCollision` xref to static registration functions `FUN_1400173D0` and `FUN_1400200C0`, each invoking a virtual `+0x58` registration call. That identifies event wiring only, not the registered handler's effect. Read-only vtable bytes at `141A2E0A0` and `141A2FD10` yielded pointer candidates, but `get_functions` did not resolve those targets as analyzed functions.

Vtable follow-up (same read-only GhidraMCP session): `CreateDamage_NoCollision`'s registered data address `141A2FD10` has a `+8` slot pointing at `1404B0EC0`; `read_memory` bytes at that slot show a tail jump to `140490F40` (`get_functions` xref at `1404B0EC7`). `140490F40` resolves two actor references and calls `1403C9B50`; `1403C9B50` obtains a context from `1403C93B0`, which selects PARAM rows and builds the payload, then conditionally calls `140529670` and a split continuation thunk. This is a newly supported event-handler-to-context path, not proof of a player swing dispatcher or HP mutation. The `OnCharacterHP_CheckAttacker` registration at `141A2E0A0` leads to event-condition methods (including `1404A9240/1404A92F0`), not a recovered HP writer. `140322910` still only serves as a clamping/keep-alive health-state setter; its broad caller set is not attribution evidence.

Next: follow the `1403C93B0` successful branch through the split continuation and determine which observer/callback applies the resulting context; separately connect that path to a concrete `0x3E8` health-field write with a caller/object identity. No native invocation, game-memory write, independent HP/stamina attribution or physical submission binding claimed; B/C/G remain open.

29 Oct read-only re-probe of the same three owners (program
`DarkSoulsRemastered.exe`, image base `140000000`, 310894 functions, revision
token `1793151129646:7d5a7905:3`): `get_functions`/`get_xrefs_to` on
`1403C93B0` confirms exactly four inbound references — the recorded
`1403C9B50` call plus `1404D0BB0` (at `1404D0CC1`) and `1404D0F20` (at
`1404D0FF0`), and one DATA reference from `141D54374`. `1404D0BB0` has no
in-function callers; its inbound set is two DATA slots (`141D66080`,
`1417CB768`) plus one `UNCONDITIONAL_CALL` from `14049B5C2`, and
`get_functions` reports **no analyzed function at or containing `14049B5C2`** —
that call site sits in unanalyzed territory, so the second owner is not yet a
resolved function and cannot be named. `1404D0F20` is reached from
`1404D0D40` (itself referenced only as DATA from `141D66098`, `1417CB7B0`,
`1417CB808`, `141364848`). Both condition owners also call the same small
helper `FUN_140481060` (34 bytes, 14 callers, callees `FUN_14030C1B0` /
`FUN_14030C290`), which is a shared accessor, not a writer. Net: no new HP write
found; the concrete blocker for B is now narrowed to *analyzing the region
containing `14049B5C2`* before any further owner claim. Still no native
invocation, no game-memory write, no HP/stamina attribution and no submission
binding; B/C/G remain open.

Same session, second probe — the three call sites themselves
(`get_assembly_context`, 12 instructions each side) and the resolver behind them:

- `1404D0CC1` (inside `1404D0BB0`): the call is preceded by
  `CMP byte ptr [RCX + 0x8], 0x6` on the singleton at `[0x141C7A050]`, and the
  surrounding code writes a fresh record through `RBX` at `+0x18`, `+0x24`,
  `+0x2c`, `+0x44`, `+0x48`. `1403C93B0`'s return value is stored at
  `[RBX + 0x2c]` and `RBX` is returned — a record constructor with a type-tag
  check, not a damage writer.
- `1404D0FF0` (inside `1404D0F20`): calls `FUN_1403C9FE0` first, then
  `1403C93B0`, stores the result at `[RDI + 0x2c]` and compares it against `-1`.
  Same record-field shape.
- `14049B5C2`: the instructions are real and disassembled — argument marshalling
  (`R9D = 0x1b`, three stack dwords, two `MOVSS` floats), the `CALL 0x1404d0bb0`,
  a null test on the returned pointer, then `CALL 0x1404d0f10`. What is missing is
  function *attribution*: the last analyzed function below is `FUN_14049AFD0`
  ending at `14049B012`, and probes at `14049B200`, `14049B400`, `14049B600` and
  `14049B800` all report no function. So this is an analysis gap of at least
  `14049B012..14049B5F5`, not a thunk.
- `FUN_1403C9FE0` (109 bytes, 6 callers: `14031F389`, `140424060`, `1404D0CF0`,
  `1404D0D40`, `1404D0F20`, `1404D1160`) is a handle-to-record resolver:
  `handle >> 16 < 0x80` indexes a slot table at `param_1[2] + index * 0x230` with a
  generation check (`*puVar2 != handle` means miss); `== 0x80` walks a linked list
  through `+0x88`. On a hit it calls `FUN_1403C5C90`, `FUN_1403C5D20` and
  `FUN_1403C7ED0`.
- Those three handlers are **release/reset**, not damage: `FUN_1403C5C90` unlinks
  the record from a `+0x1f0`/`+0x1f8` intrusive list; `FUN_1403C5D20` (737 bytes)
  resets fields to `-1.0f` (`0xbf800000`) and `1.0f` (`0x3f800000`), unlinks the
  `+0x7e` list, and decrements three refcount slots (mask `0x3fffffff`, freed via
  `FUN_1403C48F0` at zero). No `0x3E8` health write appears in any of them.
  `FUN_1403C7ED0` decompiles to nonsense ending in `halt_baddata()` — a
  misdisassembly, so its behavior is **unresolved** and must not be claimed.

Two offline static probes are now named and unstarted: function-start detection
over `14049B012..14049B5F5`, and pinned bytes at `1403C7ED0`. Neither was run this
session; both are read-only. Still no HP write, no native invocation, no
game-memory write, no HP/stamina attribution, no submission binding.

29 Oct third probe — the gap is resolved, and the second owner has a name.
`read_memory 14049B400,448` plus `get_functions` on the decoded boundaries:

- The second owner is **`FUN_14049B510`** (real prologue `MOV RAX,RSP` /
  `PUSH RDI` / `SUB RSP,0x80` at `14049B510`), called from `FUN_1404854D0`.
  Ghidra's recorded body is `14049B510..14049B551` (size 66) only, because the
  `CALL 0x140CC3E10` at `14049B54D` is treated as no-return — the same
  omitted-fallthrough cliff already documented for `1411CD8A0`. The body
  demonstrably continues: the `R9D = 0x1b` argument block sits at `14049B5B3` and
  the `CALL 0x1404D0BB0` at `14049B5C2`, both inside the truncated range. So the
  earlier "no function at `14049B5C2`" is a **body-extent artifact, not an
  unanalyzed region**, and the second owner is `FUN_14049B510`.
- Immediately before it, `14049B506` is a 10-byte stub Ghidra already holds as
  `FUN_14049B506` (0 callers): `MOV qword ptr [RSP],RAX` / `PUSHFQ` /
  `JMP 0x142B3D2E0`, and `0x142B3D2E0` has no function. That is a hot-patch /
  detour-style trampoline, and an INT3 at `14049B4D4` (right after an
  `FF 15` import call) is the padding that ended the previous function.
- `1403C9FE0`'s third handler is reached by a **tail call**, not a call:
  `1403CA022: JMP 0x1403C7ED0` after `ADD RSP,0x20` / `POP RBX`. The resolver's
  linked-list stride is `+0x220` (the decompiler's `puVar2 + 0x88` is `uint*`
  arithmetic) and the slot-table probe is `IMUL RBX,RAX,0x230` + `ADD RBX,[RCX+0x10]`
  with the generation compare `CMP dword ptr [RBX],EDX` / `CMOVNZ RBX,0`.
- Pinned bytes at `1403C7ED0` (`read_memory`, 64 bytes) are
  `04 57 29 AE 04 0E E9 33 AF 3B 99 42 40 DB 47 19 ...` — no recognizable
  prologue for the first 43 bytes; a real prologue (`MOV RAX,RSP` /
  `ADD RAX,-8` / `MOV RSP,RAX` / `MOV [RSP],RBP` / `JMP rel32`) begins at
  `1403C7EFB`. So the `JMP` target `1403C7ED0` is **not an instruction boundary**
  as Ghidra holds it, and `FUN_1403C7ED0`'s decompiled body is meaningless. Its
  behavior stays unresolved and unclaimed.

Actionable for the manual GUI pass (read-only from here, no program edits made):
extend `FUN_14049B510`'s body past `14049B551` to cover at least `14049B5F5`, and
re-examine the `1403C7ED0` entry boundary against `1403C7EFB`. Neither was done
this session.

29 Oct GUI pass actually run (program mutated, **not saved**). `clear_flow_and_repair`
on the seed `14049B510..14049B620` **refused** with
`Repair would delete defined continuation after no-return call(s):
{call_address=14049b54d, callee_address=140cc3e10, callee_name=FUN_140cc3e10}` —
`FUN_140CC3E10` is a genuine `noreturn` leaf with 1907 callers (stack-protector
failure path), so the truncation is not a wrong no-return flag and the repair
command is the wrong tool. `disassemble_bytes 14049B551..14049B620` decoded 207
bytes of real continuation, `get_xrefs_to 14049B552` returned **0 references**, and
`delete_function 14049B510` + `create_function 14049B510` reproduced the same
66-byte body — confirming the continuation is reachable only by fallthrough past a
no-return call, which Ghidra will never fold into the head function. The applied
action was therefore `create_function 14049B552`, which produced
**`FUN_14049B552`** (body `14049B552..14049B65F`, 269 bytes).

Decompiled `FUN_14049B552` — this is the owner the earlier entries were hunting:

```c
if (in_RAX == 0) { puVar2 = 0; }
else { puVar2 = FUN_1404d0bb0(in_RAX, unaff_RDI, unaff_EBP, 0x1b, unaff_ESI,
                               unaff_EBX, in_stack_...b0, ...b8, ...c0, ...c8,
                               ...d0, ...d8); }
if (puVar2 != 0) {
  FUN_1404d0f10(puVar2, in_stack_...e0, in_stack_...e8);
  plVar1 = *(longlong **)(unaff_RDI + 0x28);
  in_stack_00000060 = puVar2;
  lVar3 = FUN_14049c250((unaff_RDI + 0x20), plVar1, plVar1[1], &stack0x00000060);
  if (*(longlong *)(unaff_RDI + 0x30) == 0xaaaaaaaaaaaaaa9)
    std::_Xlength_error("list<T> too long");
  *(longlong *)(unaff_RDI + 0x30) += 1;
  plVar1[1] = lVar3;
  **(longlong **)(lVar3 + 8) = lVar3;
}
```

`FUN_14049C250` + the `0xaaaaaaaaaaaaaa9` sentinel + `_Xlength_error("list<T> too
long")` is an MSVC `std::list` `splice`/`push_back` at `this+0x20` with size at
`+0x30` and sentinel node at `+0x28`. So the second owner **constructs an object and
appends it to a `std::list`** — a contact/event queue, not a damage write.

`FUN_1404D0BB0` (301 bytes, now 1 caller: `FUN_14049B552`) is the constructor, and
it names the type: `*param_1 = NS_FRPG::FrpgEventConditionObjDam::vftable`
(vftable at `0x141364840`). It calls `FUN_1404C7C20` (base `FrpgEventCondition`
ctor), writes `param_5` at `+0x20`, `param_6` at `+0x24`, `-1` at `+0x2c`,
`param_10` at `+0x30`, `1.0f` at `+0x4c` and `+0x54`, then — only when
`DAT_141C7B1B0 != 0` and `FUN_140481060(DAT_141C7B1B0, param_5)` resolves a record
whose `[[7]+8]` byte is `0x06` — calls
`FUN_1403C93B0(DAT_141C7A050, param_7, param_6, param_9, plVar3, -1, 0)` and stores
the returned handle at `+0x2c`. Otherwise it sets `+0x18 = 1` and returns.

The vftable has exactly 6 xrefs from 3 functions, which closes the object's
lifetime: `FUN_1404D0BB0` (constructor), `FUN_1404D0CF0` (destructor — sets the
Dam vftable, calls `FUN_1403C9FE0(DAT_141C7A050, *(uint*)(this+0x2c))`, then
restores `NS_FRPG::FrpgEventCondition::vftable`), and `FUN_1404D1160` (deleting
destructor — same release, then `free(param_1)` when `param_2 & 1`).

That resolves the earlier open question about `FUN_1403C9FE0`: the handle-release
handlers traced on 29 Oct are the **`FrpgEventConditionObjDam` destructor releasing
its `1403C93B0` handle**, which is exactly why they only unlink and reset fields and
never write HP. Still no HP write anywhere on this trace; the queue append and the
handle register/release pair are event-condition plumbing.

29 Oct same session — `CreateDamage_NoCollision` reached from its own string, and it
is **not** a damage write either.

- `list_strings CreateDamage` → two copies, `14135C690` / `14135C710`. The first has
  exactly one xref: `1400200E1` in `FUN_1400200C0`, a 78-byte Lua/script binding
  registrar that calls `(**(*plVar1 + 0x58))(plVar1, &PTR_vftable_141a2fd10,
  "CreateDamage_NoCollision", L"CreateDamage_NoCollision", -2)` on the singleton
  `DAT_141C7B478` (created by `FUN_1404C78E0` if null) and then `atexit`.
  `OnCharacterHP_CheckAttacker` is the same shape at `FUN_1400173D0` with
  `&PTR_vftable_141a2e0a0`.
- The binding table at `141A2FD10` holds name-pointer / function-pointer pairs. Its
  first code slot `1404B0EC0` had **no function** — created it this session
  (`FUN_1404B0EC0`, 12 bytes). It is a thunk:
  `FUN_140490F40(DAT_141C7B1B0, float, int, int, undefined4, undefined4, undefined4)`.
- `FUN_140490F40` (383 bytes) resolves two entity ids: id `10000` means the player
  (`*(DAT_141C77E50 + 0x68)`), `id > 10000` goes through `FUN_140371BD0(id-0x2711)`,
  otherwise `FUN_140371200` + `FUN_140371360`. With both pointers non-null it builds a
  16-byte struct from `param_5/6/7` and calls
  `FUN_1403C9B50(DAT_141C7A050, param_2, attacker, target, struct, -1, 0)`.
- `FUN_1403C9B50` (187 bytes) is the same handle machinery as `1403C93B0` plus a
  lookup: it calls `FUN_1403C93B0(param_1, param_2, 0xffffffff, 0x3f000000, param_3,
  param_6, param_7)` (so `1.0f` as the third scalar), then resolves the returned
  handle through the slot table `param_1[2] + (handle>>16)*0x230` with the
  generation compare `*puVar4 == uVar1`, or the `0x80` linked list at `+0x88`, and
  finally `FUN_1403C6DA0(record, param_4)`.
- `FUN_1403C6DA0` (107 bytes, 3 callers: `1403C9AF0`, `1403C9B50`, `1403C9C20`) is an
  **assertion path, not damage**: `if (param_2 != 0) { if (*(param_1+0x118)) {
  (**(*DAT_141B68F20 + 0x68))(); *(param_1+0x118) = 0; } FUN_140CC3E10(0x20,0x10,
  DAT_141B68F20); }` — the noreturn stack-protector/report leaf again. It returns
  false only when `param_2 == 0`.

Net: the exported `CreateDamage_NoCollision` entry creates an event-condition handle
and validates the target slot; the damage, if any, is applied later by whatever
consumes the `std::list` that `FUN_14049B552` appends to. Two sibling slots in the
same binding table (`1404B0EE0`, `1404B0F00`) had no function either; both were
created this session as 12-byte thunks → `FUN_1404910D0` and `FUN_140491330`. Their
table entries are pointers rather than inline strings, so their exported names stay
unresolved and are recorded as an open item, not guessed. No HP write found; no
native invoked.

29 Oct caller context for the list-append wrapper. `FUN_1404854D0` (the 41-byte
wrapper around `FUN_14049B510`/`FUN_14049B552`) has 4 callers. `FUN_1404AD1A0` is a
227-byte script accessor that calls it then `FUN_1404E8350`. The substantive one is
**`FUN_1407D23C0`** (1377 bytes, 1 caller `FUN_1407CDCE0`): a 16-case dispatcher on
`**(param_3+0xb0)+4` that resolves an entity via `FUN_1407C9910(param_3+0x50, ...)`
and `FUN_1407D60E0`, then dispatches into the FRPG singleton `DAT_141C7B1B0` across
22 callees (`14047EE40`, `14047F040`, `14047FAB0`, `14047FD20`, `140480C20`,
`1404854D0`, `140485520`, `1404857A0`, `1404857E0`, `140485900`, `14048CF40`,
`14048CFB0`, `140490810`, `140491D60`, `140491DE0`, `140491E60`, `1404ABDC0`,
`1404ABE20`, `1404AD450`, …). Case 9 is the arm that reaches our wrapper: it calls
`FUN_1404854D0(DAT_141C7B1B0)` then `FUN_14047EE40(singleton, id,
*(int*)(param_3+0x60))`. So the `FrpgEventConditionObjDam` append is one arm of a
script-visible event dispatcher, and the damage-application candidate set is now that
callee list — none opened for an HP write yet.

29 Oct probe of that callee list (5 of the 22 decompiled: `14047EE40`, `140485520`,
`1404857A0`, `1404857E0`, `140485900`) is a **negative result**: all five are
script/entity accessors on the FRPG singleton, not damage application.
`14047EE40` is a `std::list` insert at `singleton+0x440` keyed by an int id (with the
same no-return-cookie and `_Xlength_error` shape). `140485520` resolves an entity by id
(`10000` → `DAT_141C77E50+0x68`, `>10000` → `FUN_140371BD0(id-0x2711)`, else
`FUN_140371200`/`FUN_140371360`) then compares/sets a 12-float transform via
`FUN_1402FBA70`/`FUN_1402FDA20`/`FUN_1402FBAA0`. `1404857A0` ORs bit `0x10` into a byte
at `entity+0x45`. `1404857E0` forwards to `FUN_1402FCE20(entity, id, value)`.
`140485900` reads an entity transform, converts it (`FUN_140094520`) and calls
`FUN_1402FBD80(entity, pos, a, b, float, float)`. None writes an HP field. So the
dispatcher arm that reaches the `FrpgEventConditionObjDam` append is script plumbing on
the entity/transform side, and the HP write is **not** in this dispatcher's direct
callee set — it must be downstream of the list consumer. Named next probe: find the
iterator over the `std::list` at `singleton+0x20` (the field `FUN_14049B552` appends
to, sentinel `+0x28`, size `+0x30`) rather than searching the dispatcher.

29 Oct follow-up on the wrapper's other callers: `FUN_1404AD290` (123 bytes) and
`FUN_1404AD320` (124 bytes) are both `if (DAT_141C7B1B0) return FUN_1404854D0(singleton)`
null-check wrappers taking no arguments of their own — script-API shims, not list
consumers. `get_xrefs_to DAT_141C7B1B0` returns **993** references, so a field-level
sweep of the singleton is not tractable through this bridge; the consumer must be found
by walking the class's own method neighbourhood instead. The static route from
`CreateDamage_NoCollision` to an HP write is exhausted for this session: every function
reachable from the exported entry and from the append wrapper resolves to handle
create/reset, entity-id resolution, transform accessors or assertion paths. Lane B's
remaining gates need the protected deployment evidence (LAB health-setter observer plus
one labelled natural hit), not more static reading. The Ghidra program is now saved
(modification 475, `unsaved_changes:false`), so the functions created this session
(`14049B552`, `1404B0EC0`, `1404B0EE0`, `1404B0F00`) are persisted.

### 26 Oct kind assignment RECOVERED: PARAM-driven vocabulary, gate identified

Two bulk calls, read-only, no invocation. The +0x54 store is closed:

- `FUN_140529670(param_1=record, param_2=table, param_3=index)`:
  `if (3 < param_3) return;` -- index vocabulary is 0..3.
  Reads `uVar5 = byte[base + index + 0x6a]`: 1 -> uVar4=1, 2 -> uVar4=2,
  else 0. Reads `fVar8 = float[base + index*4]` (the weight/delta
  array). The STORE is gated:
  `if ((0.0 < fVar8) && ((-1 < iVar7) || (bVar6 != 0)))` where iVar7 =
  short[+0x2c/+0x34 indexed] (event paired IDs, -1 = absent) and
  bVar6 = record +200 & 0x10 (the flag 1405290C0 sets from param_5).
  On pass: `+0x50 = fVar8; +0xac = iVar7; +0xb0 = iVar3;`
  **`+0x54 = uVar4`**; `+0xb4 = byte[base+0x7c]`.
- The table (`param_2+8`) is the row resolved by 140528910: a PARAM
  table lookup -- `1405322D0(param+7, 0)` selects the table, then a
  binary search matches row id. So the kind vocabulary (0/1/2 via the
  +0x6a byte) and the weight (float array) are per-attack PARAM data,
  consistent with the +0x73 message vocabulary (1 -> msg 24, 2 -> msg 25)
  in 1403C72D0. 140528A30 is the sibling resolver (table param+12).
- Why the session saw kind-0 only: every captured record had either
  uVar5 == 0 (PARAM byte 0 for that attack) or fVar8 <= 0 (weight gate
  failed, store skipped, field stays zero). The trap hits, Hollow
  swings, block and riposte all resolved to kind-0 PARAM entries.
  The +0x490 0x1D-guarded path requires a kind-2-creating attack whose
  PARAM entry carries +0x6a == 2 -- likely heavier/enemy-specific
  attacks, not the tutorial-Hollow exchanges captured.
- Static chain now complete end to end: submission entries
  (1404D0BB0/1404D0F20 ObjDam conditions + unknown 140490F40 callers)
  -> 1403C9B50 pool -> 1403C93B0 payload -> 140528CE0/1405290C0 row
  resolution -> 140529670 kind store -> 1403C72D0 message select ->
  +0x480/+0x490 dispatch -> +0x460 completion. Only HP/stamina
  attribution and live elicitation of kind-2 remain. B/C/H stay open
  pending a targeted capture against a kind-2 PARAM attack.

### 26 Oct kind-0 gate trace complete (Asylum demon hit, seq 912)

Post-hoc mining of the Asylum trace (no game): the demon-hit contact
(seq 912, flags 15, rate nonzero, HP 659->440 side1) carries v2 store
weight 1.0 + paired b0=12. Reading against the 140529670 gate
(`0.0 < fVar8 && (paired valid || +200&0x10)` then `uVar4 = +0x6a==2?2:+0x6a==1?1:0`):
weight 1.0 passes the float gate, b0=12 is a present paired event, and
the PARAM +0x6a byte for the demon's slam is 0 -> kind 0 stored. The
195-strong b0=12/flags-0 family around it (weights 3.0/1.0) are the same
gate passing with actor-unreadable contacts -- creation succeeded,
dispatch outcome unobserved. Kind-2 now reduces to: which PARAM rows
carry +0x6a==2. That is answerable from the installed GameParam
(AtkParam rows readable offline in param.parambnd) without any game.

### 26 Oct +150/+148 predicates resolved: flag-bit tests, not deep logic

One bulk call, read-only. The source-side dispatch predicates:

- `1402FCD10(record)`: if `record+0x45 & 2` return 1 (forced-true);
  else `1402F9270(record+0x10)`.
- `1402F9270(sub)`: if `sub+0x10 == 0` return 1 (null-linked =
  pass-through); else return `byte[link+9] >> 5 & 1` (bit 5 test).
- So the +0x460 notification fires unless explicitly vetoed: veto =
  +0x45 bit clear AND linked bit-5 clear. Both default to 1 when
  their structures are absent. The capture's all-pass behavior
  (6,320/6,320 dispatched, R9B derived from AL==0) is consistent:
  nothing vetoed because neither veto bit was set on kind-0 records.
- Whether kind-2 records set these veto bits (i.e. whether the guard
  path suppresses the +0x460 notification) is unknown without a
  kind-2 capture. Static item (c) closed; the question moves to live
  elicitation with the kind-2 PARAM attack.

### 26 Oct submission-entry probe: bounded negative, chain declared complete

One bulk call, read-only. 140371200/140371360/140371BD0 (the actor
resolvers inside 140490F40) are generic map/object lookup utilities
with 146-173 indexed callers each (lists truncated by the bridge).
They cannot identify 140490F40's callers -- the submission entry uses
computed dispatch with zero indexed callers, and walking 150+ generic
lookup callers is not a bounded probe. Declared end of the static
chain: everything from submission-adjacent creation through kind
store, message select, +0x480/+0x490 dispatch and +0x460 completion
is now mapped except (a) who calls 140490F40, (b) the 142d5f79f
jumptable destinations, (c) +150/+148 predicates, (d) HP/stamina
attribution. (a) needs live elicitation, not more static walking;
(b)-(d) need targeted native correlation. B/C/H stay open.

One bulk call, read-only, no invocation. 1404D0BB0 names itself in its
own decompiled body: `*param_1 = NS_FRPG::FrpgEventConditionObjDam::vftable`
-- an event-CONDITION constructor (ObjDam), not a contact dispatcher.
Its sibling 1404D0F20 returns false immediately when DAT_141c7b1b0 == 0;
its owner 1404D0D40 runs the condition (1403CA320/1403CA180/1403CA380
checks) and fires 1404A7740(0x1b,...) on success. Teardown is 1403C9FE0
(pool lookup + 1403C5C90/1403C5D20/1403C7ED0).

Correction to the prior note's framing: DAT_141c7b1b0 gates the ObjDam
*condition enable*, and separately the +0x460/+0x490 source-side tail
selection -- one global read in two places, not one mechanism. The
kind-assignment hop (1405290C0 tail / 140529670) is untouched by this
subtree and remains the next probe. No submission binding claimed;
B/C/H stay open.

### 30 Oct HP-writer caller fan-in (read-only)

`FUN_142cb5171` copies incoming `EDX` into `EDI` and `RCX` into `RBX`; `FUN_142e191ae` then adds `EDI` to the actor’s current HP before calling the clamping setter. The thunk has five code callers: `FUN_14024f6b0` supplies `EDX=999999` before a separate zero setter; the `ChrManipulator` method `FUN_14038dae0` receives an event object reporting ID 1000/subtype 2 and supplies a payload-derived delta; `FUN_14034bf96` and `FUN_140329b48` remain opaque wrappers; `FUN_1406981a0` supplies current HP minus its cached value while processing message `0x1f58`. Static callers narrow the setter’s inputs but do not attribute the retained hit to any path. The trace lacks caller identity and this pass did not run the game. Next: identify the producer and semantics of the `FUN_14038dae0` 1000/2 event; preserve the live-hit correlation as unresolved.

### 26 Oct selector continuation byte-trace correction

Static trace against the installed executable (SHA-256
`a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`),
using Capstone on the shipped bytes and the Ghidra callsite context; no
invocation:

- `14284F926` compares dword `[R8+0x160]` to `0x1d`, then selects the
  forged return target. Equal selects `140069F15`; non-equal selects
  `1403589C0`.
- `140069F15`: `xor al,al; lea rsp,[rsp+8]; jmp qword ptr [rsp-8]`.
  The original caller is `14031D91F: call qword ptr [R10+0x490]`, so
  the saved continuation is `14031D926`. It is `NOP; JMP 1421497F6`;
  that block sets up values and jumps to `14031D939`, which calls
  `1403CA1D0` and then jumps to `14295B2AC`.
- `1403589C0` jumps to `142FFAE49`: `mov rax,[rcx]; movzx r8d,r9b;
  push r10; pop rdx; push qword ptr [rax+0x480]; ret`. This is an
  indirect transfer through `[[RCX]+0x480]`; the runtime target is not
  present in the shipped bytes.
- Therefore the equality arm resumes the `+0x490` caller's continuation;
  the non-equality arm transfers through a runtime `+0x480` slot. The
  historical claims that both arms invoke `+0x480`, or that equality
  continues at `1403589A1`, are superseded. The two paths' eventual
  destinations are not proven equal or unequal without runtime pointer/
  stack values. No damage/outcome semantics are inferred.

The ignored local probe `build/selector_probe.py` reports the EXE hash,
selected arms and decoded bytes. This corrects the earlier static narrative;
it does not establish that either branch executed in a live encounter.

## 29 October 2026 — the attack-shape object: slot pool, owner, and param seed

Static only, from the pinned `DSRFull` analysis; nothing here was observed in a
live encounter, and no damage or HP semantics are claimed.

`FUN_1403c6e70` (815 B) builds a contact frame from a live attack object and hands
it to the shape object's own `vtable+0x468`. It has four xrefs: one call from
`FUN_1403c9c20` and three `DATA` references (`141d5411c`, `14179915c`,
`14179919c`), so it is itself a table entry.

`FUN_1403c9c20` is the only driver and shows the object's storage: a slot array at
`manager+0x10`, **stride `0x230`**, at most `0x80` slots, with the handle's bits
16-22 as the slot index, the low 16 bits as a serial, and the full handle stored at
object `+0x00` and re-validated on every access. Index `0x80` selects a linked
overflow list walked through object `+0x88`. A raw pointer is therefore not a
usable seam — an object must live in that array with a matching handle.

Per-frame branch: `FUN_1403c6da0` (lifetime/release; releases the `+0x118` holder
through `DAT_141b68f20` `vtable+0x68`) decides between `FUN_1403c5c90` (unlink from
a doubly-linked list at `+0x1f0`/`+0x1f8`) and `FUN_1403c6e70`. `FUN_1403c7900` is
the frame-skip filter and reveals **three** parallel tagged sets on the object at
`+0x1d8`, `+0x1e0`, `+0x1e8`, all queried with the *contains* helper
`FUN_1403c48c0`.

The seed is a param row: `FUN_1403c9670` resolves a `BehaviorParam` row
(registry index `param_3 + 0xc`), reads its `+0xc` as the AtkParam row id, resolves
that row in **`AtkParam_Npc`** (registry index `param_2 + 7`, no Pc/Npc branch —
the `vtable+0x18` selector belongs to `FUN_1403c93b0` alone), and copies five
dwords at row `+0x18..0x28` into an 8-slot array whose remaining three slots are
`0xffffffff`.

Those five offsets are independently corroborated outside Ghidra: the field walk in
`build/dsr-data/AtkParam_layout.json`, produced by `tools/dsr_paramdef.py` from
`AtkParam.paramdef` (71 fields), places `spEffectId0..spEffectId4` (all `s32`) at
exactly `0x18, 0x1c, 0x20, 0x24, 0x28`. A native field read and a paramdef parse
agree on the same five offsets.

Caveat carried forward: `FUN_1403c6da0` ends with Ghidra's "subroutine does not
return" and `return in_RAX & 0xffffffffffffff00`. That is the same no-return
fallthrough artifact already documented for `1411CD8A0` — the `char` result belongs
to the callee, and the mask is not a real computation.

Still open: which shape class owns `vtable+0x468`, and who writes the attack
object's `+0x118` / `+0x40`.

### Producer probe for `FUN_14038dae0` (31 Oct 2026, negative)

`FUN_14038dae0` reports `caller_count = 0`. Its only cross-references are three DATA
sites: `141d51368`, which `analyze_data_region` shows is typed
`_IMAGE_RUNTIME_FUNCTION_ENTRY` (the PE unwind entry for the function, not a dispatch
site), `14178f264`, the first slot of `IPToStateMapEntry_ARRAY_14178f264`
(`IPToStateMapEntry[221]`, pointed at by `DAT_14132e350`), and `14132e388` in the same
block. No xref exists to `14132e350`, `14132e348`, `14132e340`, `14178f258`, `14178f260`,
or anywhere in the `14132e200`-`14132e28f` block. These observations do not establish how the table is accessed;
the producer of event 1000 / subtype 2 remains unknown. The element type
`IPToStateMapEntry` is a name only - `get_struct_layout` rejects it as a non-structure -
so no field semantics back the map reading either. Treat only the tested xref result as negative
result; the named array semantics remain unverified, and the
attribution gate is still one closed labeled hit from an authorized game run.

### `IPToStateMapEntry_ARRAY_14178f264` dumped; contact `+0x130` / `+0x138` owners (31 Oct 2026)

Read-only `read_memory` of 1768 bytes at `14178f264` decodes as 219 eight-byte slots
`(uint IPoint RVA, uint state id)`, not the 221 the type name claims. Every IPoint lies
inside `0x14038dae0`-`0x140390c20`, i.e. one function region, and the state ids run
`0..0x5b` (0..91) with no gaps: 92 distinct ids, 91 of them nonzero, plus 64 slots holding
`0xffffffff` and 64 holding `0`. So the map is a per-type descriptor table (its owner block
`DAT_14132e350` carries the ASCII `ChrManipulator`), not a global id space, and it does not
reach the `caseD_*` selector range `0..0x93` — ids `0x5c..0x93` belong to other type
descriptors. The cheapest static handle named in the lane note therefore cannot bind a
`caseD_*` selector to a producer. Recorded as a closed negative probe; do not re-dump it.

The B3 shape-owner probe resolved the two offsets the LAB record reads:

- In `FUN_1403c7520` (836 bytes, `1403c7520`-`1403c7871`) the stack record base is
  `local_208`, so record `+0x130` is `local_d8` and `+0x138` is `local_d0`.
- `local_d8 = FUN_1402c4870(param_1[0x10])` — the source object resolved from the uint id at
  frame `+0x40`. `FUN_1402c4870` (83 bytes) dispatches on `id >> 28`: 0 →
  `FUN_1403ed0f0(DAT_141c7a0b0)`, 1 → `FUN_1403714b0(DAT_141c77e50)`, 2 →
  `FUN_14030c320(DAT_141c75dd8)`, 3 → `FUN_14042a7e0(DAT_141c7a488)`, else 0. It has 13
  callers, all inside the contact family (`1403c5190`, `1403c6030`, `1403c6850`, `1403c6e70`,
  `1403c7460`, `1403c7520`, `1403c7d30`) plus the `140421620`/`140423540`/`140423850`/
  `140426fa0`/`14042a1c0` eligibility group.
- `local_d0 = param_2`, the caller-supplied target. In `FUN_1403c7460` (174 bytes,
  `caller_count = 0`, reached only through the `1417991f0`/`141799224` DATA slots) the target
  is `**(FUN_1402a9cc0(handle) + 0x10)`, gated on `FUN_1402c4870(frame+0x40) != 0`,
  `FUN_1402a9cc0 != 0`, and the record type at `lVar4+8` being `< 3 || == 4`; the sixth
  argument is `FUN_1402a9d60(handle)` only when `FUN_1402a9e00(handle) == 1`, else `-1`.
- `FUN_1403c5190` (53 bytes, `caller_count = 0`) is a list validator: it walks a node list and
  returns 1 for the first node whose `+8` id resolves through `FUN_1402c4870`. It is not a
  writer of `+0x40`.

So `+0x130`/`+0x138` are resolved object pointers, not shapes, and neither is fed directly
from an AtkParam hit slot at this level. The unresolved owner is the writer of the source id
at frame `+0x40` (and of the handle consumed by `FUN_1402a9cc0`), which is the same open item
already recorded as "who writes the attack object's `+0x118` / `+0x40`". No function was
created, disassembled or renamed; the program is unmodified (`unsaved_changes:false`,
modification 3). No game-memory read of a live process, no launch, no commit.

