# DSVR native locomotion contract

Status: **UNRESOLVED — no native motor binding.** E1 orientation selection is
finished (Head default, or selected/non-dominant hand with head fallback); the
follow path and every owner below remain unattributed. No direct root or
player-facing write is enabled. Task order and gates belong to the
[cockpit plan](../MODDING_PLAN.md) and lane E
(../.empryo/cockpit/lanes/E.md, E1–E6); architecture to [ENGINEERING](ENGINEERING.md); thresholds to the
[runbook](VERIFICATION_RUNBOOK.md).

## Owner table

28 October source checkpoint: tests/test_input.cpp exercises InputPollFrame /
InputLatestPad at five non-cardinal angles during the deferred B tap and its
release. Both axes preserve continuous angle; this is input delivery evidence,
not a native dodge fix. The requested 360-degree dodge (including lock-on)
requires locating the roll-direction selection/root-motion consumer after the
pad seam, then verifying resolved displacement, collision, stamina and native
invulnerability. No roll-motor address or write is inferred here.

| Owner | Status | Evidence pointer |
|---|---|---|
| XInput poller (native call site consuming the merged thumb axes) | UNKNOWN | `src/core/xinput_bridge.cpp:246-282` — the bridge proves the game polls `XInputGetState` (first-override log), but the native consumer of `thumb_lx`/`thumb_ly` is unpinned |
| Stick-to-velocity mapper (native analog-speed mapping, units, update timing) | UNKNOWN | `src/core/pad_math.h:215-234` — mod-side `MapVrPad` emits thumb axes; no native speed-mapping owner recovered |
| Motor/collision resolver (what moves the capsule, collision response) | UNKNOWN | `src/core/camera.cpp:35-106` — `ReadPlayerPos`/render-facing reads are read-only; no resolver ABI pinned ([ENGINEERING](ENGINEERING.md):29) |
| Authoritative root (which position the sim owns) | UNKNOWN | `src/core/camera.cpp:35-106` — logic/physics/render reads exist (`ChrPosData` yaw at `+0x4`), no owned write; direct root translation stays out of policy |
| Forced-motion source (knockback/forced-displacement producer) | UNKNOWN | `src/core/camera.cpp:354-377` — follow credits only attributable resolved motion; the forced-motion producer is unidentified ([runbook](VERIFICATION_RUNBOOK.md):143) |
| Head/solid query (native solid/support/head-volume query) | UNKNOWN | `src/core/camera.cpp:421-449` — the eye clamp is a mod-side heuristic band, not a native solid query ([ENGINEERING](ENGINEERING.md):31) |

Mod-side chain, file-mapped but not attribution: basis publish
`src/core/camhook.cpp:153-157,165` + `src/core/camera.cpp:584-649`; sampler
`src/core/input.cpp:608-670`; axes `src/core/pad_math.h:141-159`;
merge/overlay `src/core/pad_math.h:279-289` +
`src/core/xinput_bridge.cpp:246-282`; publish `src/core/input.cpp:1542`;
follow heuristic `src/core/camera.cpp:354-377`; heading basis
`src/core/vr_heading.h:68-99`; observer detour `src/core/camhook.cpp:51-63`.

## Refusals

- Mixed-input non-attribution: a blended stick-plus-roomscale analog vector
  (`pad_math.h:232-233`, `xinput_bridge.cpp:260`) does not by itself identify
  physical motion; without a better command/result seam, reconciliation stays
  disabled (E3, [ENGINEERING](ENGINEERING.md):29).
- Forced-motion no-credit: knockback/forced displacement is never credited to
  the physical path (`camera.cpp:354-377` follow gate).
- Blocked-residual bound: follow per update is clamped to
  `[0, speed * 0.15]` (`camera.cpp:366`) — a heuristic against stored-debt
  lunges, not proof of attribution (E4, [ENGINEERING](ENGINEERING.md):29).

## Next probe

Pinned XInput poll caller first, per E2 order: recover the native call site
that consumes the merged thumb axes (`xinput_bridge.cpp:261`) through to
resolved root motion — units, analog-speed mapping, update timing, collision
response, root ownership. Then the path/collision/latency matrix plus the
rewritten reference-stability headset row
([runbook](VERIFICATION_RUNBOOK.md):134-143; E2–E6).

## E2 static probe — native XInput poller candidates, 26 Oct 2026

Read-only GhidraMCP against the live GUI (`DSRFull`,
`/DarkSoulsRemastered.exe`); no launch/repair/save/replay/write/scripts/commit.
IAT identity: `XInputGetState` is an external import (`Ordinal_2`); its thunk
at `0x14082f790` is a single `JMP qword ptr [0x142017ed4]`. An
external-location lookup on the slot address returned no naming, so slot
identity rests on the thunk jump target plus the import ordinal only.
`get_xrefs_to` the thunk (cap 30): exactly 6 callers, all
`UNCONDITIONAL_CALL`. Assembly windows (5 instr each side, 12 for the last)
plus decompile, one parent level; no motor/resolver claimed.

| Call site | Containing function | Register flow (ECX=slot, RDX=state) | Classification |
|---|---|---|---|
| `140c9f6ef` | `FUN_140c9f6c0` (73 B) | `ECX=[RCX+0x480]`, `RDX=[RSP+0x20]`; `TEST EAX,EAX` / `SETZ` → connected flag `[RBX+0xc]` only, no sticks read | One-shot: 4 event-gated callers (`FUN_140c930a0/3110/34a0/3970`); checked parent `140c930a0` fires on `param_2==0x8000 && *(param_3+4)==5` list event with `*(entry+8)==3`, not per-frame |
| `140e1c1ba` | `FUN_140e1c180` (104 B) | `ECX=EBX` loop 0..3, `RDX=[RSP+0x20]`; on `EAX==0` → `FUN_140e1e5a0(param_1+0x50, &DAT_1414db840+idx*0x10)` | One-shot bind-time 4-pad enumerator; sole caller `FUN_140e29e00` gated on `*(+0x1c8)==0 && *(+0x3dc)<0` (device-acquire, then `FUN_140e1aeb0` + slot select `FUN_140e1c500`) |
| `140e1c23a` | `FUN_140e1c200` (104 B) | Same loop shape, `+0x30/+0x20` offsets | Mirror bind-time enumerator (second device class); sole caller `FUN_140e29ef0`, same gate shape (`+0x168/+0x37c`, then `FUN_140e1b0d0` + `FUN_140e1c540`) |
| `140e27ff6` | `FUN_140e27aa0` (2430 B) | `ECX=[RDI+0x264]`, `RDX=[RBP-0x79]` stack `XINPUT_STATE`; `TEST EAX,EAX` / `JNZ` skip-store; thumb words at state `+8/+0xA/+0xC/+0xE` (`local_d0/ce/cc/ca`) → `(float)(int)s / 65535.0 * 2` into `[param+0x26c/+0x270]` (left) and `[+0x278/+0x27c]` (right), no deadzone; triggers `/255.0` → `[+0x28c/+0x290]`, buttons → `*(u2*)(param+0x260)` | Top per-frame candidate #1: same function also handles the deadzone-device branch (vtable `+0x48` calls, `cosf/sinf` heading at `+0x284/+0x288`); zero code callers, 4 DATA refs (`141df9ed8`, `1419518a0`, `1419518d4`, `1414dde90` — slots unresolved) |
| `140e28986` | `FUN_140e28430` (2430 B) | Identical shape, `ECX=[RDI+0x204]`, floats → `[param+0x20c/+0x210/+0x218/+0x21c]`, triggers `[+0x22c/+0x230]`, buttons `+0x200` | Top per-frame candidate #2 (mirror device class); zero code callers, 4 DATA refs (`141df9f14`, `141951670`, `1419516a4`, `1414ddd40` — slots unresolved) |
| `140c9fb7a` | `FUN_140c9faa0` (1107 B) | `ECX=[RSI+0x480]`, `RDX=[RSP+0x20]`; post-call `MOVSX` of all four thumb words (`RSP+0x28/2a/2c/2e`) → `CVTDQ2PS`, directional deadzone (7849 left / 8689 right, `ABS` projection) then per-index `0x18`-entry dispatch storing raw stick values (`param+0x324…0x3cc`), ending in `FUN_140c9f810` + virtual `+0x10` on `[param+0x4d0]` | Binding/remap-layer consumer, gated on the connected flag (`*(param+0xc)!=0` set by the `140c9f6c0` family); per-frame status unproven — sole caller is `thunk_FUN_140c9faa0`, unexamined |

Poller→mapper edge (stop here): in candidates #1/#2 the normalized stick
floats are stored to the device struct and the function ends — no
stick→velocity mapping or motor call in the same window, so the mapper/motor
stays UNKNOWN and unclaimed. No velocity, unit, timing, collision, or root
finding.

Limits: listing-slice assembly + decompile only; `FUN_140c9f6c0`'s three other
callers unchecked; DATA-ref slots not resolved to vtable+offset; DB revision
observed with pre-existing unsaved changes (nothing saved).

Exact next probe: find code consumers of the normalized float fields
(`+0x26c/+0x270` for the `140e27aa0` class, `+0x20c/+0x210` for the
`140e28430` class) — the first reader that scales them into a velocity is the
mapper→motor boundary; in parallel resolve the 8 DATA-ref slots to vftables
and the caller of `thunk_FUN_140c9faa0` to settle candidate #3's cadence.
Then mapper→motor→collision→root per the pinned E2 order.

## E2 follow-up static probe — consumers unpinned, slots decoded, 26 Oct 2026

Read-only GhidraMCP against the live GUI (`DSRFull`,
`/DarkSoulsRemastered.exe`); no launch/repair/save/replay/write/scripts/commit.
Struct base from the fresh decompiles: both candidates take the device object
as `param_1` (`RCX` on entry, `MOV RDI,RCX`) — a dynamic heap object, so there
is no concrete field address to xref; only a field-access pattern search can
find consumers. That search did not complete (see Limits).

Consumer verdict (bounded 3-windows-per-field plan; 0 windows completed):

| Field | Result |
|---|---|
| `+0x26c` (`140e27aa0` class, left-X) | UNPINNED — writes at `140e28003`-area (`[RDI+0x26c]`); no consuming reader recovered |
| `+0x270` (`140e27aa0` class, left-Y) | UNPINNED — same window, no reader recovered |
| `+0x20c` (`140e28430` class, left-X) | UNPINNED — mirror write, no reader recovered |
| `+0x210` (`140e28430` class, left-Y) | UNPINNED — mirror write, no reader recovered |

No function scaling these floats into a velocity was found. Mapper/motor stay
UNKNOWN and unclaimed.

Slot resolution (`read_memory`, 8 bytes each, all exit 0):

| Slot | Bytes | Decode | Verdict |
|---|---|---|---|
| `141df9ed8` | `a0 7a e2 00 ff 7a e2 00` | RVAs `0x00E27AA0` (= `140e27aa0` entry) + `0x00E27AFF` (entry+`0x5F` interior) | NOT a vtable pointer (no `0x140` high bytes); RVA pair |
| `1419518a0` | identical | identical | Duplicate of above, NOT a vtable |
| `1419518d4` | identical | identical | Duplicate of above, NOT a vtable |
| `1414dde90` | `a0 7a e2 40 01 00 00 00` | VA `0x140E27AA0` (candidate entry itself) + u32 `1` | Full VA but target is the candidate, not a vtable; trailing `1` uninterpreted |
| `141df9f14` | `30 84 e2 00 8f 84 e2 00` | RVAs `0x00E28430` (= `140e28430` entry) + `0x00E2848F` (entry+`0x5F`) | Mirror RVA pair, NOT a vtable |
| `141951670` | identical | identical | Duplicate, NOT a vtable |
| `1419516a4` | identical | identical | Duplicate, NOT a vtable |
| `1414ddd40` | `30 84 e2 40 01 00 00 00` | VA `0x140E28430` + u32 `1` | Mirror of `1414dde90`; not a vtable |

Context: neighbours `1414dde70`/`1414ddd20` (64 B each) decode to full-VA
sibling-function tables (`140e26780/140e33e90/140e341d0/140e28dc0…` and
`140e26840/140e33ea0/140e341e0/140e28e70…`), and `141df9ec8`/`141951890`
show interleaved `[u32, RVA]` pairs around the tasked slots — but the 8 tasked
slots themselves are per-candidate duplicate RVA pairs plus one VA+`1` each,
not vftables. Vftable claim refused.

Candidate #3 cadence: `FUN_140c9faa0`'s sole code caller is
`thunk_FUN_140c9faa0` (`140c9f800`, `UNCONDITIONAL_JUMP`); the thunk's sole
xref is DATA from table slot `14149dc70` (`read_memory` 32 B there opens with
`00 f8 c9 40 01…` = VA `140C9F800`). The table at `14149dc40` (96 B = 12
slots) holds the thunk at index 6 among sibling `140c9fxxx` targets plus one
data-looking entry (`1416B2BC0`). Dispatch is via that table — no direct code
caller recovered, table owner unclaimed. Per-frame status stays UNPROVEN
(connected-flag gated `*(param+0xc)`, 0x18-entry event-table shape); not
promoted to poller.

Limits: `get_functions` decompiles for all three functions succeeded first;
then whole-program `search_instructions` (`0x26c/0x270/0x20c/0x210`), all
function-scoped sibling probes, bulk `get_xrefs_to` (15 addresses), and later
single `get_xrefs_to`/`read_memory` calls timed out (`No complete response …
may still be running`), so 0/3 consumer windows completed and table-owner xrefs
are open. No save/repair/scripts; unsaved-changes token not re-observed this
pass.

Exact next probe: unchanged — consumers of `+0x26c/+0x270` / `+0x20c/+0x210`
(retry the pattern search when the bridge is idle, or walk the
sibling-function tables at `1414dde70`/`1414ddd20` whose members share the
class layout and may read the fields); in parallel type the table at
`14149dc40` (owner + dispatch site for the `140c9faa0` thunk). Then
mapper→motor→collision→root per the pinned E2 order.

## E2 sibling-table walk — trigger/event siblings, no float-field consumer, 26 Oct 2026

Read-only GhidraMCP against the live GUI (`DSRFull`,
`/DarkSoulsRemastered.exe`); no launch/repair/save/replay/write/scripts/commit.
Bounded plan per task: 2 entry windows per table (12 instr each side) +
`get_functions` signature; full decompile only because both probed functions
are <300 B. Bound hit — stop, no mapper decompile.

Table re-decode (correction of the 26 Oct follow-up "VA+`1`" rows): the 64 B
reads at both bases are 8 full-VA slots each, and index 4 of each table IS the
poller candidate — `1414dde70+0x20` = `1414dde90` holds u64 `0x140E27AA0`
(the `01` byte is the VA high byte, not a trailing flag); likewise
`1414ddd40` = u64 `0x140E28430`. So each table is a per-class 8-slot dispatch
array containing its own poller at index 4:

- `1414dde70`: `140e26780` / `140e33e90` / `140e341d0` / `140e28dc0` /
  `140e27aa0` (#1) / `140e269a0` / `140e270c0` / `140e277a0`
- `1414ddd20`: `140e26840` / `140e33ea0` / `140e341e0` / `140e28e70` /
  `140e28430` (#2) / `140e269c0` / `140e270f0` / `140e277c0`

Probed (index 0 = trigger reader, index 3 = event dispatcher, each class):

| Sibling | Size | What it does | Float-field read? |
|---|---|---|---|
| `140e26780` (idx 0) | 181 B | Trigger reader: gates `+0x3dc`/`+0x3ec`/`+0x3ed`, writes `(byte/255.0)*2` floats to an out-param | NO `+0x26c/+0x270` read (different fields, same class-layout family) |
| `140e28dc0` (idx 3) | 169 B | Event dispatcher on `param_3`: `6` → rumble-ish via `FUN_140e1c660`; `0x219` → vtable `+0x10` call, flag `+0x269` | NO float-field reads |
| `140e26840` (mirror idx 0) | 181 B | Identical trigger reader at mirror offsets `+0x37c/+0x38c/+0x38d` | NO `+0x20c/+0x210` read |
| `140e28e70` (mirror idx 3) | 169 B | Identical dispatcher (mirror DAT/call targets) | NO float-field reads |

Consumer verdict (negative): no probed sibling reads the normalized stick
floats, and none scales them into a velocity. Mapper/motor stay UNKNOWN and
unclaimed. The trigger-reader siblings confirm the tables share the device
class layout (neighbouring-field access), but the stick-float consumers are
elsewhere.

Limits: entry addresses are function starts, so `context_before` windows bled
from preceding function bytes; classification rests on the <300 B decompiles.
Remaining indices 1, 2, 5, 6, 7 unexamined (bound). Unsaved-changes token
pre-existing (`1792646701740:fa2c6e65:3542`, unsaved true); nothing saved.

Exact next probe: idle-bridge pattern-search retry with a field-access byte
pattern for `+0x26c/+0x270` (`+0x20c/+0x210` mirror) consumers — the first
reader that scales them into a velocity is the mapper→motor boundary. Then
mapper→motor→collision→root per the pinned E2 order.

## E2 byte-pattern retry — axis getters pinned, scaling consumer still open, 26 Oct 2026

Read-only GhidraMCP against the live GUI (`DSRFull`,
`/DarkSoulsRemastered.exe`); no launch/repair/save/replay/write/scripts/commit.
Probe shape changed per task: cheap `search_byte_patterns` (which completed
where whole-program `search_instructions` timed out bridge-side), patterns
derived from the candidates' own stores, not guessed.

Store encoding (from `disassemble_function` on both candidates): entry
`MOV RDI,RCX`; XInput branch stores `MOVSS dword ptr [RDI+0x26c],XMM0` at
`140e28022` (+`0x270` at `140e2804c`), mirror `+0x20c/+0x210`; byte-axis
branch stores `MOVSS [RDI+0x26c]` at `140e27d32` / `[RDI+0x270]` at
`140e27d4c`, deadzone zeroing `MOV dword ptr [RDI+0x26c],ECX` at `140e27c22`.
So the device base register in the writers is `RDI`; a consumer may use any
register, hence the masked search below.

Load search (ModRM masked to displacement only — any base/reg):
`F3 0F 10 ?? <disp32>` (MOVSS load) + `8B ?? <disp32>` (dword load):

| Field | MOVSS hits | Verdict |
|---|---|---|
| `+0x26c` | `14022b1fd`, `140237ffe`, `1403a5235`, `1407422d4` | None paired+scaling on this struct: `14022b1f5/fd` reads `[RCX+0x25c/268/26c/270]` as a packed 4-vector copy-out (`UNPCKLPS`→`[RDX]`); `1403a5235` is a read-modify-write clamp accumulator on a different struct (`[RDI+0x13c/140/150/161]`) |
| `+0x270` | `14022b1f5`, `140237f16`, `1402380ae`, `1402d4d60`, `140a25f75`, `140b6b155` | Same `14022b1xx` copy-out; rest unpaired, not followed (bound) |
| `+0x20c` | 10 hits incl. `140236f2c`, `1406d6d74…`, `140729db9…` | `140236f2c/68` reads `[RSI+0x20c/210]` amid `[RSI+0x184/188/1fc/204/208/22c/230/320]` smoothing on a different struct; not this device layout |
| `+0x210` | 20 hits incl. `140236f68`, `140742382/43b` | Same `140236xxx` struct; `140742xxx` pairs with the `+0x26c` copy-out family, different struct |

Dword-load search is displacement-collision noise (hundreds of hits for
`+0x270/+0x20c/+0x210` across unrelated structs); only the `140e2xxxx`
family hits are meaningful, below. No MOVSS reader both pairs
`+0x26c/+0x270` (or `+0x20c/+0x210`) on the poller device layout and scales
it — the velocity-scaling reader is NOT among the direct-load hits.

Family sweep (remaining dispatch-table indices 1, 2, 5, 6, 7 — decompile
only, bound: 6 functions):

| Sibling | What it is | Float-field access |
|---|---|---|
| `140e269a0` (class-1 idx 5, 28 B) | Indexed float getter: `param_2-3 < 0x1c` → `return *(float*)(param_1+0x26c+(param_2-3)*4)` — index 3 ⇒ `+0x26c` (left-X), 4 ⇒ `+0x270` (left-Y) | READS all four stick floats by index — the consumer-facing axis accessor |
| `140e269c0` (class-2 idx 5, 28 B) | Mirror getter at `+0x20c` (same indexing) | READS mirror stick floats by index |
| `140e277a0` (class-1 idx 7) | Indexed float setter: `MOVSS [RCX+RDX*4+0x26c],XMM2` (`140e277ae`) | WRITES by index (synthetic/override input path) |
| `140e277c0` (class-2 idx 7, 24 B) | Mirror setter at `+0x20c` | WRITES by index |
| `140e270c0` (class-1 idx 6) | Button-bit getter (`param_1+0x27c` mask test, per `140e270f0` mirror shape) | NO float read |
| `140e270f0` (class-2 idx 6, 34 B) | Mirror button-bit getter at `+0x27c` | NO float read |
| idx 1/2 both classes (`140e33e90/ea0`, `140e341d0/e0`) | Not decompiled (bound; prior walk left them unexamined) | OPEN |

Dispatch fact: `get_xrefs_to` on all 16 table slots (`1414dde70…` /
`1414ddd20…`) returns zero references, and the getters themselves carry only
DATA xrefs (their own table slots). So the tables — and therefore the axis
getters — are reached by a computed dispatch with no indexed code xref; the
caller that invokes getter index 5 (and then multiplies/clamps/deadzones the
returned axis) has no static edge to follow from here.

Consumer verdict: the device struct's stick floats are consumed only
through the indexed getters `140e269a0`/`140e269c0`; the first function that
*scales* a returned axis into a velocity is behind that unindexed dispatch
and was not recovered. Mapper/motor stay UNKNOWN and unclaimed. This is a
boundary, not a negative proof: direct-load readers on other structs exist
but none matches this device layout.

Limits: MOVSS-load hits beyond the paired/context-checked ones above were
not each decompiled (bound); idx 1/2 siblings unexamined; dword-load hit
lists are collision-dominated and were only family-filtered. Unsaved-changes
token unchanged (`1792646701740:fa2c6e65:3542`, unsaved true); nothing saved.

Exact next probe (one of): (a) idle-bridge whole-program instruction search
for call sites of the getter *call shape* is not available — the getters have
no code xrefs — so instead type the dispatch-table owner: find who loads
`1414dde70`/`1414ddd20` (or the enclosing struct) via a byte-pattern search
for those VAs as immediates/displacements, then follow that owner's index-5
dispatch to the scaling consumer; (b) equivalently, outer-RCX typing: find
constructors/assignments of the device struct (writers of `+0x264/+0x204`
slot indices or `[obj+0x1c8]`) and walk their callers to the per-frame
consumer. Then mapper→motor→collision→root per the pinned E2 order.

## E2 dispatch-owner typing — vftable base found, PadDevice ctor/dtor/factory, 30 Oct 2026

Bridge idle first, unlike pass 5: `list_instances` ok, `get_function_count`
answered in 58 ms, single-address `get_xrefs_to` in 27 ms. No queued scan.
Everything below completed; no timeouts.

Dispatch-region map (read-only `read_memory`):

- A contiguous run of function-pointer VAs starts at exactly `1414ddc68`
  (below it: flag/pair data, and `1414ddc60` = data pointer `0x1416c3f70`)
  and runs past `1414ddef0`. One data outlier inside the run: `1414dddb0`
  = `0x1416c3ff8`. Single-class vs adjacent sibling vftables not pinned.
- The 26 Oct "8-slot tables" are interior views of this run. View A base
  `1414dde70` (run offset `+0x208`): `140e26780` trigger reader, `+0x18`
  `140e28dc0` event dispatcher, `+0x20` `140e27aa0` poller. View B base
  `1414ddd20` (run offset `+0x0b8`): `140e26840`, `+0x18` `140e28e70`,
  `+0x20` `140e28430` poller mirror, `+0x28` `140e269c0` axis-getter mirror.
- Completed calls close the pass-5 question: `get_xrefs_to(1414dde70)` = 0
  and `get_xrefs_to(1414ddd20)` = 0 — interior subviews carry no references,
  as expected when objects store the run-start base.
- `get_xrefs_to(1414ddc68)` = 4, all DATA, paired stores: `140e263f6`/
  `140e263fd` from `FUN_140e263c0`, `140e26607`/`140e2660e` from
  `FUN_140e265f0`.

Table typed by its real name: the decompiler resolves the stored value as
`DLUID::PadDevice<DLKR::DLSingleThreadingPolicy>::vftable`. The
`NS_FRPG::PadDevice::vftable` label was never part of this typing.
`list_class_members("DLUID::PadDevice")` returns 0 members, namespace
absent — same negative shape as the `NS_FRPG` retry; slot names are not
recoverable from RTTI in this database.

Constructor `FUN_140e263c0` (384 B): base `FUN_140e33520`, vftable store,
lock helpers `FUN_140e148f0`/`FUN_140e14b80`, then field init: `+0x148` =
param_2 (u32); `+0x14c/+0x154` = the two qwords of param_3; `+0x160/+0x168`
= param_5/param_4 (`+0x168` cleared when `FUN_140e290d0` bind fails);
unbound markers `+0x204 = -1` and `+0x37c = -1`; 28 dwords zeroed at
`+0x20c..+0x27b` — this block contains the pad index `+0x264` and the
normalized left-stick floats `+0x26c/+0x270`; 0x70-byte memsets at `+0x280`
and `+0x2f0`; qword zeroes `+0x364/+0x36c/+0x374`, `+0x384/+0x38c/+0x394`,
marker `+0x37c = -1`, `+0x39c = 0`; byte/word flags at `+0x200`, `+0x209`,
`+0x3a0/+0x3a1`, `+0x3d0`, and qwords `+0x3a8..+0x3c0`. Device layout is at
least `0x3c8` bytes.

Destructor `FUN_140e265f0` (168 B): re-stores the vftable, lock teardown
(`FUN_140e14a90`/`FUN_140e14930`), a conditional singleton path
`DAT_141cfa698` (fallback `FUN_140e15450`) feeding four dwords from
`+0x14c..+0x15b` into `FUN_140e1cf80`, then base dtor `FUN_140e33b00`. Its
sole caller is `FUN_140e266e0` = deleting destructor, vftable slot +0,
delete flag = low bit of param_2.

New factory edge: direct `CALL FUN_140e263c0` from `140e1bc7a` and
`140e1bd79`, both inside one statically anonymous function — no function
object contains those addresses. Region spans at least `140e1bc50..140e1be3d`
(RBP frame, RDI = owner). Shape: allocation thunk `FUN_140e1fa10` (5-byte
JMP) fed from owner member `[RDI+0x80]`; ctor args RCX = object, EDX = ESI
= pad index, with `CMP ESI,0x1`/`CMP ESI,0x2` branches forming four paths =
four pads; R8 = 16-byte-aligned param block `[RBP-0x39]`; result to
`[RBP-0x49]`. The known bind enumerators `FUN_140e1c180`/`FUN_140e1c200`
(104 B, bodies `140e1c180..140e1c1e7`/`140e1c200..140e1c267`) are distinct
and sit above the hole. Function-creation to type the factory is a database
write — not done.

Ctor and dtor each also carry DATA xrefs from two table regions:
`141df9df4`/`141951530` (ctor), `141df9e0c`/`141951568` (dtor). The
`141df9xxx` region holds entry+interior RVA pairs (same shape as the
`141df9ed8` pair for poller `140e27aa0`/`+0x5f`); `141951xxx` is compact
u64 entries. Resemblance to guard/unwind/registration metadata is noted;
these enumerate, they are not call evidence, and no interpretation is held.

Verdict: the device object is now typed on its construction/destruction side
and the 26 Oct "computed dispatch with no static edge" claim is retired —
it was an artifact of probing interior table views instead of the run start.
The poller slot `+0x228` (view A `+0x20`) still has no found call site;
mapper→motor→collision→root stay unattributed, motor attribution stays
UNKNOWN, E2 stays unchecked.

Exact next probe (one of): (a) masked byte-pattern search for the poller-slot
call shape `CALL qword ptr [reg+0x228]` — the byte-pattern path completed
cheaply in earlier passes where instruction search timed out; or (b) walk
the anonymous factory upward from `140e1bc50` with `get_assembly_context`
windows to find its entry, then `get_xrefs_to` on that entry — both are
cheap read probes. No function creation, renames, comments or saves.

## E2 pass 7 — poller-slot call sites found, consumer cluster typed, slot offsets corrected, 31 Oct 2026

Option (a) was taken against an idle bridge; nothing timed out. No DB write.

Slot correction (`read_memory(1414dde90,0x10)`): run+`0x228` -> `140e27aa0`
poller (confirmed), run+`0x230` -> `140e269a0` axis getter, run+`0x238` ->
`140e270c0` = indexed bit-test (`LEA EAX,[RDX-3]; CMP EAX,0x1B; SHL EAX,CL`),
run+`0x240` -> `140e277a0` = indexed SETTER (`MOVSS [RCX+RDX*4+0x26c],XMM2`).
Pass-6's "axis getter = run+0x238" was wrong arithmetic; the getter is
run+`0x230` (xref `1414dde98` = base+`0x230`). Everything that searched
`+0x238` profiled the bit-test slot.

Retraction: the four "scaling consumer" hits `14032330f/3d1/49c/567` from the
first (wrong-slot) search are CFG guard-island fragments, not PadDevice
consumers. `get_assembly_context` shows bare-`JMP` islands whose entries are
referenced only by trampoline DATA (`14216ca5d..14216caa5`: `MOV RBP,imm64;
XCHG [RSP],RBP; LEA RBP,[island]; RET`; island entry `1403232f3` referenced
from `14216ca7f/14216ca86`), no containing function object, and the
`CVTDQ2PS/MULSS/CVTTSS2SI` sequence scales indices, not analog values.
Same for `140328d56`. `140328fd2` is a 12-byte stub thunk.

Call-shape searches (masked `ff 9f ff ff ff ff`):
`CALL [reg+0x228]` -> 8 hits, `CALL [reg+0x230]` -> 9 hits; all classified.

Consumer cluster (first positive device-consumer edge in any pass): the
`14045xxxx` family. Five poller-shape sites `14045a560/eb79/eba0/f0c2/f0f0`
and four getter-shape sites `14045c548/c58b/e572/ecb2`, plus `+0x238`-shape
in `FUN_14045edf0`. Shared shape: device at `[owner+0x90]+0x20`;
`MOV RCX,[obj+0x90]; MOV RCX,[RCX+0x20]; MOV RAX,[RCX]; CALL [RAX+slot]`;
poller call `(this, controller_id, 0, 0)` with `controller_id =
FUN_1401bc3f0(set,idx)+0x10`, then `MOV [padmgr+0x1e0], entity`.
`FUN_14045f080` (138 B, 4 callers) is the pump helper doing exactly this.
Cluster = handler family under `FUN_1404548e0` <- `FUN_14045f820`, and
`FUN_14045f820` has zero code callers (RVA in the `141d5c6d8` pair-table,
same shape as `141df9xxx`) — the top-level pump is table-resident and not
statically nameable, the same negative shape as `FUN_14024f6b0`,
`FUN_14038dae0`, `FUN_1403c7460`. Do not re-walk it.

Rejected non-PadDevice hits: `1411cxxxx` sites live in timing/render
functions (`FUN_1411cd8a0` callers include `ProcessDantelionRenderFrame`);
`14108d387` in `FUN_14108d2c0` is a frame-time accumulator (`*1000.0*0.5`).

Poller decompile (`FUN_140e27aa0`) completes the raw->normalized contract:
bound path `XInputGetState(this+0x264)` writes `+0x26c/0x270` =
`sThumbLX/LY / 65535.0 * 2`, `+0x278/0x27c` right stick, `+0x28c/0x290` =
triggers `/255.0`, button word `+0x260`. Unbound path (`+0x3dc < 0`) is a
DirectInput-style vendor branch: vcall slot `+0x48` fills raw block `+0x210`,
calibrated against min/max blocks `+0x2e0`/`+0x350` gated by flags `+0x430`
bit0..5; a short at `+0x230` becomes `cos/sin` at `+0x284/+0x288`
(`((raw+27000)/100/180)*pi`).

Index convention: getter `140e269a0` reads `+0x26c + (idx-3)*4` for
`idx-3 < 0x1c`; mirror `140e269c0` reads `+0x20c + (idx-3)*4`; setter
`140e277a0` writes the first array. Consumer calls pass
`(this, idx, flag, &out)`; e.g. `FUN_14045c2b0` calls idx 0 and 1 with
flag 1 (that call form reads the `+0x20c` mirror space or the wrapper's own
slots — the exact index-to-field mapping for idx 0/1 is NOT yet resolved,
because `140e269a0` with idx<3 returns 0; treat device vs wrapper index
spaces as open).

Direct-field readers closed negative: masked `MOVSS` load/store patterns for
`+0x26c/+0x270` (direct disp, SIB base+disp, scaled-abs, `LEA`) = zero hits
program-wide. Every consumer goes through the getter slots, so the vtable
call IS the device->consumer boundary, and the 14045 cluster is the first
found call-site evidence (seven sites). Pass-6 "no found call site of
+0x228" is superseded.

Not proven, deliberately: the object type at `[owner+0x90]+0x20` is inferred
by call-shape match only — no completed vptr store ties it to the
`DLUID::PadDevice` run. `FUN_14015e890` (15 B, the callee right after
getter site `14045c58b`) is a settings-singleton accessor
(`[DAT_141b68ec8+0x6f0]`), not the axis value. Motor chain (units, speed
mapping, update timing, collision, root ownership) stays UNKNOWN; E2 stays
unchecked.

## E2 pass 8 — poll inventory closed, per-frame mapper decoded, command/action layer typed, 31 Oct 2026

Pass-7 follow-ups against an idle bridge (searches 1-2 s, xrefs <=60 ms).
No DB write, no timeouts, no program-wide instruction scans.

XInputGetState inventory CLOSED (`get_xrefs_to` on thunk `14082f790` = 6
callers, all classified): `FUN_140e27aa0` (PadDevice poll slot run+`0x228`,
writes `+0x26c..+0x290`); `FUN_140e28430` (2430 B, a SECOND full poller,
DATA refs `141df9f14`, `141951670`, `1419516a4`, `1414ddd40` = run+`0xd8`
of the same vftable run): XInput branch writes the MIRROR float block —
`+0x20c/+0x210` left stick and `+0x218/+0x21c` right stick at the same
`/65535*2` units, `+0x22c/+0x230` triggers `/255`; the unbound branch
calibrates raw `+0x1b0..` against min `+0x280`/max `+0x2f0` gated by
`+0x3d0` bit0..5 via `((raw-min)/(max-min)-0.5)*±2`; a heading short
`+0x1d0` becomes `+0x224/+0x228 = cos/-sin(((raw+27000)/100/180)*pi)`.
This explains the mirror getter `140e269c0` (`+0x20c` space). The rest:
`FUN_140e1c180`/`FUN_140e1c200` (factory-region connection probes,
caller `FUN_140e29e00`), `FUN_140c9f6c0` (connection probe, `+0xc =
(result==0)`, pad idx `+0x480`) and `FUN_140c9faa0` (remap layer). There
is no poller outside the DLUID input-library region (`140e1..140e3xxxx`)
and the remap layer: the game side consumes only through the device
vtable and the command layer.

Per-frame mapper `FUN_14045a640` (2635 B, under dispatcher `FUN_1404548e0`)
fully decompiled — this is the stick->movement mapping for one view/pad
per frame:
- Settings singleton `DAT_141c6d598` supplies the command IDs: general
  `+0x88`/`+0x230` (alternate layout selected by a settings flag through
  `FUN_1401bc3f0(set,0xb)`), direction/dash IDs `+0x290..+0x2dc`, `+0x144`;
  tier gates `[param_1+0x2b0]`/`[param_1+0x2b4]` select up to four stick
  layouts per view.
- Command-slot binds via `FUN_14045eff0`/`FUN_14045ef50` (pool lookup
  `FUN_1411b3d50(holder+0x80, cmd_id)`; bound value read from pool entry
  `+0x30` or `+0x38 + i*8`), and the poll call `CALL [dev_vtbl+0x228]`.
- `FUN_14045e690` builds firestick RING tables (sqrt radii
  0.96/0.84/0.64/0.36 normalized by a screen-size scalar) into view-entry
  fields `+0x30..+0x0a0` — movement heading routes through 8-way analog
  ring commands, not raw vectors.
- `FUN_14045e290` (1016 B) is the analog-speed contract: sensitivity
  floats `[param_1+0x2c0/0x2c4/0x2c8]` are `powf(10.0, x)` exponents; it
  writes the view entry `+0x30 = 1/screenW`, `+0x34 = 1/screenH`,
  `+0x38/+0x3c` ring scalars, `+0x40 = 1920/screenW`, `+0x48 =
  (1 - pow10(s)*1920/screenW)^2`, and `1/(pow10(a)+pow10(b))`; then binds
  direction command slots 0..4 and reads device floats through
  `CALL [dev_vtbl+0x230](idx,1)`.
- The active view entry is `[[[holder+0x80]+0x80]+0x370]+0x28`.
- `FUN_14045edf0` commits the mapped command set (contains the bit-test
  slot `+0x238` shape).

Device slot semantics pinned by decompile: run+`0x218` = `FUN_140e341d0`
is a 3-byte NO-OP; run+`0x220` = `FUN_140e28dc0` is an enum-command method
(`param==6` -> `FUN_140e1c660(padmgr_singleton, flag)`; `param==0x219` ->
presence probe through `[this+0x15]` vcall +`0x10`, writes `+0x269`). So
the mapper's `+0x220(idx 0..4, 1, &val, 0)` binds either no-op on the
PadDevice run or belong to a different vtable: the consumer device at
`[holder+0x90]+0x20` is a PadDevice-derived or wrapper class sharing the
slot layout. Identity stays shape-inferred, NOT vptr-proven. Label route
closed negative: `list_globals(name_substring="PadProf"/"PadDevice")` =
0 globals; the vftable names are label symbols only.

Command/action layer typed (new region, `1404bxxxx`): a family of 25+
uniform thunks (mask-`+0x45a` readers confined to `1404b1f54..1404b2c34`).
Shape (`FUN_1404b1f50`): read override mask `+0x45a`; axis A =
`[state+0x238]` unless bit1 -> `*[state+0x128]`; axis B = `[state+0x260]`
unless bit2 -> `*[state+0x138]`; command ID = `[state+0x210]` unless bit0
-> `*[state+0x118]`; then tail-call `[param_1+8](state+0x588, id, A[, B])`.
`FUN_1404b6cf0` boxes the result into a variant: `+0x560` int64, `+0x468`,
`+0x460` double, type tag `&DAT_141c7b443`, flag `+0x568` (clear `&~0x7`,
set `|1`). Pump `FUN_14049e350` (table-resident, zero code callers): gate
`[state+0x458]==3`, predicate `FUN_1404ba050`, then the boxer. The command
state object (size >= `0x589`) carries the command ID at `+0x210` and
analog floats at `+0x238`/`+0x260` — the very slots the mapper feeds.

Pump entity store re-pinned by disassembly: `FUN_14045f080` stores
`[pool_entry+0x18]` into the pad-view CONTAINER at `[holder+0x90]+0x1e0`
(`MOV [RDI+0x1e0],RBX` @ `14045f0c8`; clear `MOV [RBX+0x1e0],0` @
`14045f0f8`). Pass-7's "[padmgr+0x1e0]" phrasing was loose — it is NOT
the pad-manager singleton.

Negatives this pass:
- Singleton-paired `+0x1e0` patterns (`e8 ?? ?? ?? ?? 48 8b 80 e0 01 00
  00`; RIP-load `DAT_141c6ab90` then `+0x1e0`) = ZERO hits. The loose
  `48 8b 8? e0 01 00 00` hits at `1403c5xxx/1403c7xxx` are a separate
  teleport/path-tracing class (dtor `FUN_1403c5490` releases refcounted
  `+0x1d8/0x1e0/0x1e8`; raycasts `FUN_1402b18c0`/`FUN_1402b1b90`;
  singleton `DAT_141c74870`) — rejected as coincidental offset.
- `141d64260..` = function-registration table, 24-byte entries
  `(code_start_RVA, code_end_RVA, param-blob RVA)` (e.g.
  `1404b1f50..1404b1fcf` + blob `0x17ca5b8`). The third qword is NOT a
  name string — reads give bytecode-like bytes (`01 06 02 00 06 32 02
  30...`); the name-table theory is refuted in-record.
- Full-VA qword searches for `14045`/`1404b` method addresses in vftables
  = zero hits (tables store RVAs). `get_xrefs_to(1401bc3f0)` = 13 refs,
  all in six functions of the `14045` handler family — the
  settings-index -> controller-id accessor is family-private (numeric
  address required; label-form query returned 0).
- `FUN_140e1ced0` (factory-region helper) is a keyed-erase scan over a
  0x14-byte-entry array (`[RDI+0x78..0x80]`, key-compares with
  `0x11cfd5a06f1d2b61/60` sentinels) — registration bookkeeping, not
  motor.
- `get_functions` by namespace-name patterns (`NS_FRPG::Player` etc.)
  errors out; RTTI naming route stays closed.

Motor attribution status: update timing (per-frame handler family), raw
units (pass 7) plus the analog-speed mapping form (pow10 sensitivity +
1920-based screen scale into view-entry `+0x30..+0x4f`) and the command
route (bind -> firestick ring -> direction command IDs + analog floats at
state `+0x238`/`+0x260` consumed by the `1404b` action thunks) are now
pinned statically. The walk-action handler behind `[param_1+8]`, the
player-side consumer of the state floats, collision response and root
ownership remain UNATTRIBUTED. Motor attribution UNKNOWN, E2 stays
unchecked, E open stays 5.

Exact next probe (was pass 8): decompile `FUN_1404ba050` (action gate
predicate) and `FUN_14045edf0` (command commit); locate pump
`FUN_14049e350` in its registration table by its RVA `50 e3 49 00`
(4-byte search); and hunt consumers of the view-entry floats
`+0x30..+0x4f` near the `[[X+0x80]+0x80]+0x370` chain. Read-only only.

## E2 static probe pass 9 (31 Oct 2026)

All three pass-8 next-probes executed, plus the `+0x370` view-chain
consumer hunt. Read-only throughout; no DB write, no save.

Tooling correction first: the `(p.matches||p.hits||[])` result extractor
silently produced FALSE ZEROS early this pass — `search_byte_patterns`
returns a BARE ARRAY (or `[{note:"No matches found"}]`). The earlier
"zero `+0x370` loads" readings this pass are retracted; the corrected
parse found 46 hits. `search_instructions(mnemonic=MOV, operand
"0x370]")` timed out program-wide once — do not retry that scan.

- `FUN_1404ba050` (gate predicate, 380 B) fully decoded: reads override
  mask `[state+0x45a]` (ushort); mask `== 0` → returns 1 (eligible);
  otherwise requires `[state+0x458] == 3`, and for each set override bit
  resolves a linked `(object, handler)` pair — bit0 `+0x110/+0x118`,
  bit1 `+0x120/+0x128`, bit2 `+0x130/+0x138` — through `FUN_1404b1cd0`;
  any unresolved pair → 0. `FUN_1404b1cd0` (80 B, 12+ callers across the
  `1404b` gate family) validates the pair's object against the type-tag
  registry `DAT_141c7b443` via `FUN_140d55e50(tag, state, ...)`, then
  walks the pair linked list (nodes `+8`) for identity with the registry
  entry and returns `pair[1]` — the handler slot. Command IDs and
  override handlers resolve through the script VM type system.
- `FUN_14045edf0` (command commit) decoded: per-frame commit path —
  view vtable slot `+0x40` is passed `[holder+0x90]`; anchor
  `[view+0x18]+0x28` is fed to device slot `+0x238` (wrapper bit-test
  slot). No movement arithmetic here — commit/anchor plumbing only.
- **Command dispatch is the FromSoft script VM.** `DAT_141c7b478` =
  VM current-object singleton, READ by hundreds of uniform 0x50-spaced
  thunks `140016c50..140021xxx`; `DAT_141c7b443` = VM type tag with
  DATA refs across the `1404bxxxx`/`1405axxxxx`/`1405b8xxx` families.
  The action thunks' tail-call `[param_1+8](state+0x588, id, A, B)` is
  the VM call entry: the walk-action handler is a VM-registered command
  handler, NOT a statically xref-able native function. The
  `141d61fd0` registration entries' third qword is VM bytecode, not a
  name — name route refuted twice now (pass 8 + this pass's
  `141354500/550/5b8` metadata-header targets, whose "name pointers"
  read back as self-referential headers, e.g. `14165a3d8` holds its own
  RVA `0165a3d8` at +0x10).
- Pump identity CLOSED (do not re-walk): `get_xrefs_to(14049e350)` =
  DATA `141d61fd0` (4-byte-RVA registration table; exact search
  `50 e3 49 00` = exactly 1 hit) + full-VA qword method block
  `1413545c0`. Full-VA 8-byte searches elsewhere = 0. The blocks at
  `141354500`/`141354550`/`1413545b8` are three class-method tables
  (`FUN_14049e290/e660/e3e0/e400/e470/e510`;
  `FUN_14049e2f0/e690/e3f0/e430/e4a0/e520`;
  `FUN_14049e350/e880/e460/e4d0/e530/e570`) each preceded by a metadata
  header pointer.
- **View-chain (`+0x370`) consumer sites classified** via corrected
  masked searches (`48 8b 8? 70 03 00 00` = 46 hits, `4c 8d 8? 70 03
  00 00` = 6) then per-hit `disassemble_function` membership + context:
  - The full mapper chain `[[X+0x80]+0x80]+0x370` exists only at the
    two known mapper sites `14045e552` (in `FUN_14045e290`) and
    `14045ec99` (in `FUN_14045e690`), each followed by `MOV reg,
    [obj+0x28]`.
  - Pre-resolved chain variant `[X+0x68]+0x370`: `14024fc9c` (inside
    top-level pump `FUN_14024f6b0`), `14028eaa4` (`FUN_14028ea00`),
    `1403fb91f`/`1403fc1d5` (`FUN_1403fb6d0`/`FUN_1403fbfe0`, then a
    dword read at `[+0x8]` = container count).
  - The single biggest reader cluster is the top-level pump
    `FUN_14024f6b0` itself: 7 `+0x370` sites (`14024f994`, `14024fc9c`,
    `14025012c`, `14025036d`, `140250780`, `1402508f7`, `140250bf7`),
    repeatedly loading the entry subobject (`+0x10` of the `+0x370`
    object) and calling per-frame helpers `FUN_1402d3970`,
    `FUN_1403cea70`, `FUN_1403cc7c0`, `FUN_1401fe070`.
  - Recurring view-entry idiom: null-test then `ADD/LEA +0x10` entry
    subobject; byte gates `[entry+0x70]==6`, `[entry+0x5b]&1`,
    `MOVSX [entry+0x1b]`; `FUN_140326b50` is the entry-vs-container
    comparator (also checks `[container+0x4b1]`).
  - New singleton to track: `DAT_141c7e3e8` (`14053c630` reads
    `[singleton+0x370]`).
  - Rejected garbage: `140f17280`/`140f2e723` = `RBP+0x370` stack-frame
    offsets; `140326431`/`1403264c1`/`140326551`/`140327189` =
    guard-island bytes NOT inside any function (same trap as pass 7);
    `1426cddb5`/`142edbe64` = 10-instruction padding thunks;
    `140e26e5c`/`140e26fb2` = PadDevice's OWN `+0x370` device field;
    `1409700ff` = vector begin/end compare (`+0x368`/`+0x370`), other
    class.
- First REAL consumer edge of the view entry from the top pump:
  `FUN_1402d3970` (258 B, 1 caller) takes the `+0x370` object and
  samples FIVE typed getters `FUN_1403e2340/23e0/2480/2520/25c0` (0xa0
  stride) off `[view+0x10]`, each smoothing into 16-byte short records
  at `[owner+0x1b0/0x1c0/0x1d0/0x1e0/0x1f0]` via `FUN_1401d7820(rec,
  val, 0.5f)` (coefficient-0.5 smoothing helper, 141 B, 7 callers).
  Decoding the getters shows these carry COMMAND/BIND BYTES, not stick
  floats: each getter calls `FUN_14051f7c0(entry_obj, 5,
  (short)[view_entry+0x10])` — the VM typed-parameter-block resolver,
  type 5 — then indexes the block by `+0x58` RVA and combines byte
  `[view_entry+0x0a]` with block byte `+7/+8/...`. So the view-entry
  transport carries command-layer values; movement MAGNITUDES continue
  to live only in the `1404b` command-state floats `+0x238/+0x260`
  (pass 8). This is consistent, and it closes the "view-entry float
  consumer" question as a typed negative for raw-stick routing.
- Motor attribution status: update timing, raw units, the analog-speed
  mapping form, the bind→ring→command-ID route, the eligibility gate
  semantics (override pairs validated against the VM type registry) and
  the VM dispatch shape (`[param_1+8]` VM call, `FUN_140d55e50` registry
  lookup, `FUN_14051f7c0` param-block resolver) are pinned statically.
  The walk-action handler itself (a VM command handler), the
  `1404b` state-float consumer, collision response and root ownership
  remain UNATTRIBUTED. **Motor attribution UNKNOWN, E2 stays unchecked,
  E open stays 5.**

Exact next probe (pass 10): stop vtable hunting; work the script-VM
command layer — decompile `FUN_140d55e50` (type-tag object lookup) and
`FUN_14051f7c0` (typed parameter-block resolver, type-5 usage), then
enumerate the VM thunk family at `140016c50..140021xxx` (0x50 stride,
`DAT_141c7b478` readers) for the direction/dash command handler that
reads state floats `+0x238/+0x260`; that handler is the motor edge.
Read-only only.

## ESD state layer — soulstruct tool pass (31 Oct 2026)

Tool-staging pass (not a Ghidra probe; no DB contact). soulstruct 2.6.0
(installed editable from the local clone, Python 3.14 venv at
`C:\Tools\DarkSoulsRemaster_De\modtools\venv`) decompiled the player
character state layer read-only into `modtools\data\`:

- `esd\c0000\` ESP: machine 0 = 225 states, machine 1 = 277 states.
  `esd\enemyCommon\` = 285 states. `m10.chresdbnd.dcx` contains only
  `chr\dummy.esd` (131 states). `params\` = all 41 GameParam tables as
  JSON (`Movement.json` = MoveParam; rows are ANIMATION SELECTORS, no
  speed floats). `esd\c0000_statelist.txt` = every state name +
  entry/exit/while script.
- Local soulstruct patch (editable install carries it):
  `base/ezstate/esd/ezl_parser.py decompile()` now coerces
  `byte_sequence = bytes(byte_sequence)` — fixes an unhashable-bytearray
  crash that otherwise blocks ALL CHR ESP output. PyPI 2.3.2 is unusable
  for DS1R ESD (Condition-reader `len()` TypeError, data-less wheel).
- Provenance: the game `c0000.esd.dcx` (54034 B, sha `85fc1eaa…`) differs
  from the `ds1_chresd_metadata` repo copy (51912 B — conditions
  merged/re-authored), but both parse to IDENTICAL state-id sets per
  machine (0..224, 0..276), and the repo `.meta` StateGroups map 1:1:
  group 0 `PassiveStateGroup` = machine 0 (225/225 overlap), group 1
  `ActiveStateGroup` = machine 1 (277/277). So the Zeditor names apply
  to the game file exactly; game bytes stay canonical.

Typed ESD locomotion vocabulary (soulstruct `functions.pyi` IDs — treat
as reconstruction-grade, verify against DSR VM dispatch before Ghidra
trust): `GetMovementType` = test chr[219] (c0000 conditions compare 0..3);
`IsInputDirectionMatch` = test chr[297] (args 1/2); `GetMoveAnimParamID`
= test chr[257]; `GetMoveAnimBlendRatio` = test chr[266];
`GetAnimIDFromMoveParam` = test chr[110]; roll states enter via
`ChangeGeneralAnimCategorized(2, GetAnimIDFromMoveParam(d), 6, 0.2, 0)`
with direction codes d: 0 Fwd, 1 Back, 2 Left, 3 Right, 5 Jump (1H
states 32-36/38, 2H 94-97, stamina −30). `Set4DirectionMovementThreshold`
= command chr[1][2014]; `IsTiltingStick` = test chr[1103] (DS3-era
number, verify). No raw stick ever appears in ESD: the script layer
consumes RESOLVED locomotion (movement type, blend ratio, anim param id)
and selects animations — consistent with every pass-1..9 native finding.

Pass-10 route addition: besides the VM thunk family, hunt the ESD/EZSL
interpreter's test-function dispatch — cheap first probe is
`search_strings` for `EzS` / `.esd` / `chresd` anchors, then the
interpreter's per-test-ID handler (jump table indexed by f_id; 219/257/
266/297 are the movement getters whose native bodies READ the state the
`1404b` command layer writes — those reads are the motor-attribution
edge from the ESD side).

Motor attribution status unchanged: UNKNOWN. E2 stays unchecked, E open
stays 5. Ladder job-121..125 stands.

## 31 Oct - DSVR-side spatial data layer for E3 roomscale walking (feature pass, not a native probe)

- Offline feature pass (records in MODLOG 31 Oct): theater and sanctuary
merged into one horizon-locked theater panel anchored at judged floor +
calibrated eye height; procedural floor-grid layer at the judged floor;
calibration v2 with natural standing/seated pose gates, wrist-any-angle
1:1 hand orientation, and `play_style` recorded into the calibration
profile.
- Relevance to this contract: the spatial data E3 (bounded physical
walking composed with stick input) needs — reach, judged floor, eye
height, play style — is now sampled continuously by
`SampleCalibration`/`CalibrationCapture` and persisted, and the
roomscale input gate in `input.cpp` consumes the seated/theater state.
This does NOT touch native attribution: the resolved-motion side (state
floats `+0x238/+0x260` → VM command handlers) and the E2 motor contract
remain as documented above.
- Ladder re-run after the pass: Debug exit 0 + 115/115, Release exit 0 +
115/115, `pwsh tools/auto_verify.ps1 -SkipPackage -NoLatest` exit 0 +
Doctor PASS. No game-dir install this pass; installed core remains the
presentation-session build.

## 31 Oct - E2 static probe pass 10: VM thunk family = script-command
registration table; `+0x210` command-id readers typed

- `FUN_140d55e50` = type-tag registry lookup (binary search, singleton
`DAT_141cf8c78`); `FUN_14051f7c0` = typed parameter-block resolver
(bytecode `+0x48`, per-type counts `+0x1c8+type*0x10`, type 5 = command
bytes).
- The 0x50-spaced VM thunk family `140016c50..140021400` (537 thunks) is
the FromSoft script-VM command REGISTRATION table: every thunk
tail-calls registrar `FUN_1404c78e0` (lazily constructs the VM
current-object singleton `DAT_141c7b478` via `FUN_1404b96b0`), and each
descriptor carries the command name string (14135xxxx) + 24-byte
qword-pair blocks at `141a2exxx` → bytecode at `14135bexx`. Names
recovered for the whole vocabulary; locomotion-adjacent:
`TurnCharactor` `140016d40`, `HoverMoveVal` `1400196d0`, `DisableMove`
`140019810`, `NoAnimeTurnCharactor` `14001a490`, `OnTurnCharactorEnd`
`14001a4e0`, `SetFirstSpeed(Plus)` `14001a8f0/14001a940`, `SetMovePoint`
`14001b160`, `SetCompletelyNoMove` `14001ff30`, `IsNoInputLeave`
`14001f120`. Typed NEGATIVE: ESD tests 219/257/266/297 are NOT in this
family — they belong to the ESD interpreter's test-ID dispatch.
- Masked search for command-ID reads `[reg+0x210]` found exactly 4
sites; 3 typed: `FUN_1404b2050` two-axis commit (override mask `+0x45a`
selects ID/axisA/axisB from pairs `+0x118/+0x128/+0x138` vs inline
`+0x210/+0x238/+0x260`, tail-call `[param_1+8](state+0x588,id,A[,B])`),
`FUN_1404b7110` one-axis + boxer (`+0x560/+0x468/+0x460`, tag
`&DAT_141c7b44d`, flag `+0x568`), `FUN_1404fea40` command-ID edge
detector (slots `+0x210/212/214`, prev `+0x216/218/21a`, fallback
`param_2+0x10/12/14`, push `FUN_1401d7680(param_1+0x138,...)`).
- Settings singleton `DAT_141c6d598` (148 xrefs): no `+0x290..+0x2ff`
field reads within 40 B of any load — accessors are elsewhere; pass 11
starts from the accessor family, not the load sites.
- Motor attribution status unchanged: UNKNOWN. E2 stays unchecked, E
open stays 5. Ladder job-121..125 stands.

## 31 Oct - E2 static probe pass 11: queue pusher = slot writer; script layer = EzState v2.11.2; test-ID string route CLOSED negative

- `FUN_1401d7680` decoded: three-entry record copier (`param_2/3/4` →
`[param_1+8..+0x34]`, each two ushorts + three dwords); 6 callers all in
the `1404f`/`14022` command-state zone. The edge detector's slot writer;
plumbing, no arithmetic.
- The pass-10 "script VM" is identified as **EzState v2.11.2**
middleware: banner `1414e7320`, source paths `..\Source\Res\EzState\
EsdResCap.cpp`, RTTI `EsdMan`/`EsdResCap`/`EzState23Loader`/
`EzStateRegisterSetEvent`. Player ESD loads via `FUN_140347010`
(`chresdpatch:/c0000.esd`). The 537-thunk family is the EzState COMMAND
registration set.
- Typed NEGATIVE: test IDs 219/257/266/297 and
`Set4DirectionMovementThreshold` have no name strings (`MovementType`,
`InputDirection`, `TiltingStick`, `Set4Direction`, `RegisterTest`,
`EzStateTest` = zero hits). Conditions are numeric-ID dispatched inside
EzState; string anchors reach commands only. `m_moveAnimPos/Dir/State`
format strings exist but carry zero xrefs; `EsdMan` COL RVA pattern = 0
hits. Do not re-run these routes.
- Next static route (pass 12): enumerate EzState
descriptor/condition entries adjacent to registrar `FUN_1404c78e0` for
bodies reading floats `+0x238/+0x260`, or widen the `DAT_141c6d598`
accessor window. Motor attribution UNKNOWN; E2 unchecked; E open 5.
Read-only; DB never saved; ladder job-121..125 stands (116/116 after
Electron phase 1).
