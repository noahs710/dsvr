# Native melee contract investigation

Evidence reference only; execution order belongs to the [sole execution guide](superpowers/plans/2026-10-21-native-conversion-execution.md), operating policy to [TESTING](TESTING.md).

Status: **unresolved**. No native damage binding is verified by this note. Static paths below come from the pinned Ghidra project; the first bounded native observation is separately classified with its loss limitation.

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

**Corrected 25 October 2026: that tail-transfer is not a handler call.** The
`1428C7F11` tail is a forged-return epilogue, read from pinned bytes rather than
the decompiler: `PUSH RSI/POP R8`, `PUSH RBX/POP RDX`, `PUSH RDI/POP RCX` move the
carried values into argument registers; `MOV RBX,[RSP+0x30]`/`MOV RSI,[RSP+0x38]`
plus `ADD RSP,0x20` restore the outer frame; then `PUSH RBP`,
`LEA RBP,[0x14031D5D0]`, `XCHG [RSP],RBP`, `RET` at `1428C7F3C` place the thunk
address on the stack as a *return address*. There is no `CALL`, which is why
`14031D5D0` shows zero callers and only DATA references. Following it gives no
handler: `14031D5D0` is a five-byte thunk to `FUN_1400baf52`, then
`FUN_142eca4c2 -> FUN_142e381b2 -> FUN_1427d78ad` (a `RAX ^ &stack` cookie write)
`-> FUN_142ebfbab` (`LOCK`/`UNLOCK`) `-> FUN_14280bc9b`, whose whole body is
`MOV RBX,[RSP+0x10]`, `MOV RAX,0x14031D863`, `CMOVNC RBX,RAX`,
`MOV [RSP+0x10],RBX`, `POP RAX`, `POP RBX`, `RET` — a return-address selector
driven by the carry flag left by that atomic operation. Its alternate target
`14031D863` decodes to `XOR AL,AL` then `JMP 140045BE3`, and `140045BE3` is
`MOV RCX,[RSP+0x40]`, `XOR RCX,RSP`, `JMP …`: a stack-cookie verification stub.
The same `LOCK`/`UNLOCK` + return-selector shape appears at `FUN_142e2b96f`
(called from `14031D7A8`), and `14031D85E` in the same page is a `CALL` to
`FUN_140386F00`, a 411-byte float/flag worker with one ordinary code caller
(`14037A405`). So the `0x14031D5xx`–`0x14031D8xx` page is a shared compiler-
generated thunk/return pool, not melee code, and Ghidra reports "no instruction
at `14031D863`" only because that block is not disassembled in the listing.
Consequence for the B lane: the type-1 continuation must be recovered from the
register state at the shuffle site itself (`1428C7F11`-`1428C7F2B`: RDI/RSI/RBX
and the derived R8/RDX/RCX), never by stepping through the forged `RET`. Do not
repair the primary database to make the CFG look continuous.

## Pinned analysis source and workflow

- Primary, read-only project: `C:\Tools\DarkSoulsRemaster_De\analysis-full\DSRFull.gpr` (`DarkSoulsRemastered.exe`, image base `0x140000000`, Ghidra 12.1.4).
- Repair workspace is separate: `C:\Tools\DarkSoulsRemaster_De\analysis-full-attack\DSRFull.gpr`. Do not use it as the source of primary observations.
- `npx -y rea-agents@4.0.1 analyze` treated the parent directory as a JavaScript application; the direct `.gpr` target was rejected. REA MCP `open_binary` also returned `target_unavailable`; `binary_session` reports the Windows Ghidra provider unavailable because process Job Object ownership, private DACL enforcement, and reparse-safe path admission are not proven. Consequently these Ghidra observations have no REA Evidence IDs.
- To continue the requested investigation, the existing `tools/ghidra/query.ps1` was used against the pinned primary project with `-readOnly -noanalysis`. Split recovery uses `RecoverSplitFunction.java`, whose transaction and analysis-only instruction patches were rolled back and byte-checked. Raw outputs are local-only under ignored `build/native-n1-*.txt`; they are not distributable ABI proof.

## Observed static paths

1. `0x1404B0EC7` is a direct jump to `FUN_140490F40`; the wrapper's call at `0x1404910AD` enters `FUN_1403C9B50`. The wrapper validates two actor IDs/handles and passes position/stat-like values into the event creation path.
2. `FUN_1403C9B50` calls `FUN_1403C93B0`, obtains a generated event handle, indexes a manager pool in `0x230`-byte slots for IDs below `0x80`, and calls `FUN_1403C6DA0` before returning the handle or `0xffffffff`. Meaning of the event fields and exact side effects remain unproven.
3. `FUN_14024F6B0` calls `FUN_1403C8DD0` at `0x1402500D8` on its update path. `FUN_1403C8DD0` walks an event list linked through record offset `+0x220`, calls `FUN_1403C6030` for records, removes/recycles records when it returns false, then calls `FUN_1403C6850` on retained records.
4. `FUN_1403C6030` validates an actor/context, builds contact-like data from event-record fields and the candidate actor, and calls `FUN_1403C4020`. If accepted, it updates per-event actor bookkeeping and calls `FUN_1403C7BC0`; it also consults/updates a secondary list through `FUN_1403C4460`. `FUN_1403C7520`, called from `FUN_1403C6030`, `FUN_1403C6850`, and `FUN_1403C7D30`, rejects actor IDs already in three context lists (`+0x76`, `+0x78`, `+0x7A` via `FUN_1403C48C0`), assembles context from the event record, point/direction and actor, then calls `FUN_1403C4020`. Accepted interactions add the actor ID to event bookkeeping. This is a contact/event candidate path, not by itself a damage result.
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
