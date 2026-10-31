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
