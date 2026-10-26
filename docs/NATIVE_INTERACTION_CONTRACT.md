# Native interaction evidence

Status: H1 partially evidenced; the ObjAct group selection/activation chain is
recovered statically. Native capture 26 Oct supplies 3 labelled activations
with row handles + validDist + accepted results (below). Eligibility/LOS,
dummy position, prompt receiver, generation and physical reach remain
unqualified. Evidence contract under the existing conversion plan, not a
playable interaction claim.

## Native activations, 26 Oct session (PID 40312, receipt rev 19 Restored)

Detour layout (vr_trace.cpp:212-256): values[0..1]=index,
[2..3]=actor handle, [4..5]=row handle, [6..7]=row state before,
[8..9]=row state after, [10..11]=dispatcher result, [14]=raw
ObjActParam validDist. Flags 15 = before + after + identity-stable +
validDist read, all three events.

| T+ | index | row handle | state before/after | result | validDist | operator label |
|---|---|---:|---|---|---:|---|
| 42.0s | 1 | 111 | 0x2100 / 3 | 1 accepted | 70 | fog gate, went through |
| 347.0s | 27 | 110 | 0x2100 / 3 | 1 accepted | 60 | key-unlocked door opened |
| 382.2s | 23 | 109 | 0x2100 / 3 | 1 accepted | 60 | already-unlocked door opened |

All on group d654240, actor 0x10044000. Row handles decrease 111/110/109
with progression; state 0x2100 -> 3 across dispatch with result 1.
validDist 70/60/60 matches no lever row in the installed table below
(90/110/60 lever rows) -- consistent with doors, not levers; no generic
hand-reach gate follows. Selected-row handle + PARAM range + accepted
result are observed; dummy position, prompt/LOS/reach owner and
generation remain open. H1 stays partially evidenced.

### 26 Oct selection loop recovered: prompt/LOS owner placed

Two bulk calls, read-only, no invocation:

- `FUN_140314E30` is the ObjAct selection loop. It enumerates rows
  (`index * 0xf0 + base`), rejects state 3/4 (`*(row+0x20) != 3/4`),
  tests per-row eligibility via `thunk_14203FAB4(row, actor)`, collects
  the valid set, and picks `*piVar11` (first valid). It reads the
  selected row's prompt text via `[row+0x48]+0x80`, then branches on
  `*(param_1+0x24)`: nonzero tests LOS/reach via
  `thunk_1420BEFB7(row, actor)` -- failure shows the prompt
  (`14071BD30`), success activates via `140313470(group, index,
  actor+8)`. Zero takes the direct activation path instead.
- `FUN_1403162C0` is a bounds-checked wrapper: validates index
  against `*(param_1+0x28)`, then forwards to 140313470 on a
  `param+0x30` sub-array (stride 0x58, not 0xf0) -- a second group
  layout, owner unclassified.
- `FUN_140313470` (the detour target) marks the row
  (`+0x30 = 0x101, +0x32 = 1`), reads actor fields via
  `[row+0x48]` virtuals (+0x30/+0x18/+0x10/+0x20), and completes
  through `1403138E0`. Its two indexed callers are the selection
  loop and the sub-array wrapper only.
- The ELIGIBILITY predicate (`14203FAB4`) and LOS predicate
  (`1420BEFB7`) bodies are unrecovered -- named entry points, not
  semantics. But the fail-closed reach gate now has a native
  counterpart location: whatever 1420BEFB7 computes is the prompt/
  activation criterion our observer's 3 accepted rows passed.
- No prompt text, dummy position, generation or reach-distance
  attribution claimed. H1 advances on selection mechanics; LOS/reach
  owner named but unrecovered.

## Installed ObjActParam rows, 26 October 2026

Read-only installed `param/GameParam/GameParam.parambnd.dcx`: 498,505 bytes,
SHA-256 `cc4a81cc87f028534d965c908a8aa7dba2f9e235ac30f5e72ddf6bb45dc7b5ab`.
Bounded DFLT/zlib decode yields exactly its declared 3,189,209 bytes with no
remaining compressed stream. BND3 declares 41 members; ObjActParam is member
ID 37 at decoded offset 3,181,152, size 3,169, SHA-256
`68183e96d8c2c755371901f7aa97cc692ceb9da89cc11e4a90f6f23650cdefd7`.

The member declares OBJ_ACT_PARAM_ST, data version 1 and 49 row records.
All adjacent data offsets differ by 32 bytes. Cached DS1R Paramdex
Defs/ObjActParam.xml places u16 validDist at row +12 and labels it [cm].
Names below come from its separate Names/ObjActParam.txt, not runtime objects.

| Row | Cached label | Installed validDist, raw | Schema-based metres |
|---|---|---:|---:|
| 1200 | Church gate lever | 90 | 0.90 |
| 1202 | Church bell lever | 90 | 0.90 |
| 3011 | Cemetery lever, push | 110 | 1.10 |
| 3012 | Cemetery lever, pull | 90 | 0.90 |
| 4560 | Bell lever | 90 | 0.90 |
| 6100 | Sluice lever | 90 | 0.90 |
| 6101 | Abyss general-purpose lever | 90 | 0.90 |
| 7201 | Crystal tower general-purpose lever | 90 | 0.90 |
| 7202 | Alarm lever | 90 | 0.90 |
| 16010 | Lock gate lever door | 60 | 0.60 |

The metre column interprets schema units only. The native helper recovered below
also doubles that converted value; this column is not the native range gate.
The schema's default 150 is not these rows' value. Native distance may be
actor-to-dummy rather than hand-to-target, and a door is not interchangeable
with a manipulation lever. No generic 1.5-metre or 0.9-metre hand gate follows.

Raw observations, all 49 row hashes, carrier/member/schema hashes and limits:
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\physical-combat-20261026-resume\objact-parameter-observation.json`.
No game asset or extracted PARAM was written into distributable sources.

## Remaining native boundary

Recover the selected action row and dummy position, prompt/activation receiver,
native eligibility/LOS, object generation and owner update phase. Preserve event
flags, native animations and progression. Pickups, NPCs, bonfires, ladders,
elevators and chests require their actual owners, not these lever declarations.
The physical-reach gate stays fail-closed. Ordinary controller interaction is a
separate input route. Offline gate tests do not close H1-H3 or supersede the
retained native controller-only failure.

## 26 October targeted owner probe

ObjActParam does occur in the shipped EXE as UTF-16LE at 141372BE8 (not
ASCII). Primary Ghidra xrefs identify 140531E36/140531E3D in 140531BC0,
which loads this name into a local parameter-name list at RBP+100. This is
parameter registration/loading, not proof of a selected interaction or LOS.
Four exact ObjAction/ObjActCtrl/target debug strings have no indexed xrefs;
a bounded REX.W RIP-relative LEA scan of executable file sections likewise
found no candidate for those four addresses. This does not prove no owner
exists: other encodings/indirections remain unknown. Evidence is in the
physical-combat-20261026-qualify directory named in the melee contract.
The selected-object/prompt/activation owner was subsequently narrowed below.

## 26 October ObjAct owner and natural activation observer

Read-only primary GUI GhidraMCP observations and shipped EXE bytes, SHA-256
`a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`:

- RTTI identifies WorldAreaObjAct (vtable 14131B3E8), ObjActCtrl
  (14131B210), and WorldObjActManImpl (14131BF98). Manager global 141C75E40
  leads to 140312B00's array of 0x58-byte groups and update 140314D40.
- The group update obtains the local actor through 140371ED0 and compares its
  native area with the group descriptor. 140315180 consumes the previous
  candidate-present byte at group +25, asks the prompt/button path, and stores
  the button result at +24. These bytes are not object identities or LOS proof.
- 140314E30 walks the array at group +18, count +20, stride F0. It excludes
  row states 3/4, calls 14030D440 for eligibility, retains candidate indices and
  chooses the first positive candidate. Group +28 is prompt/text identity,
  not a selected object handle. Accepted button processing reaches 14030F210
  and 140313470(group, selected_index, actor_handle).
- Selected row +1C is the object handle, +20 its state, +40 the ObjActParam
  pointer, +48 the action-state object. 140313470 writes request bytes +30/+32
  and reaches 1403138E0. Its return is not proof of completed progression.
- The complete 41-byte helper 14030F050 reads unsigned word PARAM +C,
  multiplies by float32 0.01 at 1412DC48C, then doubles with ADDSS XMM0,XMM0.
  Thus it returns raw * float32(0.01) * 2 in native units. 14030D685 calls it
  inside eligibility. The actual actor/dummy geometry, angle tests and any
  LOS ownership still need complete recovery and live correlation. It is not
  a controller-to-object distance rule.

External raw replies and shipped-byte checks:
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\native-combat-20261026-finish\`.
`primary-verification.json` pins 1,993 bytes across eleven owner/dispatch windows;
`range-byte-verification.json` pins the additional 41 helper and four constant
bytes. Derived game data and decompilation remain outside distributable sources.
No unknown native action function was invoked.

The LAB combat-only observer now observes natural 140313470 calls, forwards all
arguments once, and preserves the complete native return value. It snapshots
the selected row before and after, with fresh page-permission scopes on each
side of the native call. Array/count/row/PARAM/handle equality certifies only
that callback's tuple, never object generation across callbacks. The bounded
trace queue and capture-generation checks still apply.

`native_interaction_activation` uses object=group, seq=natural callback counter,
duration=native call time. Its 16 float values carry exact integer limbs:
0..1=index raw32; 2..3=actor handle; 4..5=object handle before;
6..7=state before; 8..9=state after; 10..13=raw return64;
14=raw validDist u16; 15=reserved zero. Flags 1/2 certify before/after reads,
4 certifies the equal tuple and requires both reads, 8 certifies the PARAM range
read and requires the before snapshot. Missing fields remain unknown. Analyzer
results remain UNVERIFIED for native interaction acceptance even with an intact
event. This observer neither requests activation nor enables physical reach.

Synthetic detour/writer tests verify exact argument/return preservation, changed
handle refusal, no permission cache across native execution, and retention in
the combat-only event domain. They do not establish native door completion.
