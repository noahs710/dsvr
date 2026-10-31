# Native interaction evidence

**Evidence correction (latest):** The prior claim that our 3D hand-distance
gate is necessarily conservative relative to native 2D distance is unproven:
the endpoints, thresholds and transforms have not been shown equivalent.
The selection-loop branches, prompt/LOS labels and individual activation
labels below are hypotheses, not qualified native verdicts. The capture has
no footer and no reliable case windows. H1 remains open; do not enable reach
from these observations.

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

### 26 Oct predicate shims mapped; NULL paths are refusals

Two bulk calls, read-only. Both predicates are split-entry shims:

- Eligibility `14203FAB4`: clears `[RCX+0xB8]` bit 7, NULL-checks RDX
  (actor): nonzero continues to `14238E1C6` (unrecovered, one hop);
  zero diverts to `14030D903` (`JMP 142F52810` split entry,
  unrecovered). The NULL-actor path is a refusal, not gameplay.
- LOS `1420BEFB7`: spills regs, RCX->RSI (row), RDX->R14 (actor),
  NULL-checks RDX: nonzero continues to `142B32AD7` (unrecovered,
  one hop); zero diverts to `thunk_142EEE285` (8 indexed callers,
  shared forged-return utility, not interaction-specific).
- Accepted continuations (14238E1C6, 142B32AD7) remain unrecovered,
  but the predicate *structure* is closed: eligibility = bit-clear +
  actor-present + 38E1C6; LOS = actor-present + B32AD7 decides
  prompt vs activation. No distance formula or prompt text claimed;
  H1 static mapping complete to the same one-hop boundary as melee.

### 26 Oct continuations placed via raw bytes (bridge timeout worked around)

get_functions timed out twice on 14238E1C6 (heavy xref compute; request
may still be running in Ghidra -- no state trusted from it). Recovered
both continuations with cheap disassemble_bytes instead:

- Eligibility-accepted `14238E1C6`: register-save prologue (R15/XMM6-9/11
  spills, RBP-frame) then `JMP 14030D4A0` -- next hop unrecovered.
- LOS-accepted `142B32AD7`: `MOV RCX,[RCX+0x48]`, NULL-checks, then
  forged-return selection between `140F8CFD5` (present) and the shared
  `thunk_142EEE285/14030F40F` (absent) via CMOVZ. Fallthrough is
  `JMP 140F8CFD5` + downstream block (`142B494B9` region) --
  walked 27 Oct (see below). Same presence/absence idiom as every
  other forged-return selector in both chains.
- No distance formula, prompt text or dummy position in either head.
  H1 static work stops at the same boundary: named entries, open bodies.

  ### 27 Oct LOS-accepted continuation walked past the selector (read-only)

  Read-only `disassemble_bytes` + `analyze_function_complete` against live
  GUI `DSRFull` (no save/repair/launch/invocation). The LOS-accepted head
  `142B32AD7` was walked instruction by instruction past the forged-return
  selector:

  - Present path `140F8CFD5`: `MOV RAX,[RCX]` then `JMP 14030F249` — a
  virtual dispatch on the action-state object (`[row+0x48]`), not a
  distance or prompt computation.
  - `14030F249`: `CALL [RAX+0x50]` then `JMP 140040093` — a second virtual
  call on the same object, then into the computed-dispatch mesh. The
  static wall: past this second virtual call the bytes are JMP-island
  garbage/forged returns, same as melee's jumptable.
  - Absent path `14030F40F`: `JMP 142EEE285` (shared forged-return
  utility, not interaction-specific). The selector's fallthrough
  `142B494B9` block: `MOV RAX,[RAX]` / `MOVZX EAX,byte [RAX]` /
  `AND EAX,0x7F` (a masked flag byte), counter increment at `[RBP+0x30]`,
  then `JMP 140F4D143`.
  - `140F4D143`: `CMP EAX,0x80` then `JMP 140F08A73` — a flag-bit test
  routing into the mesh. `140F08A73` -> `142D9ECE8` -> `1410317E5` ->
  `14280FCB9` -> `142968F1B` -> `142039854` -> `142FBE5E3` ->
  `1420A54C4` -> `142C5A692` -> `142B5E535` -> `1426BDC47` ->
  `1405B4366` (stub) -> `142B54BE0` (wrapper) -> `1423B020A`
  (UNRECOVERED_JUMPTABLE, too many branches) — the computed-dispatch
  mesh proper. Static recovery ends here; no distance formula, prompt
  text or dummy position appears anywhere on this path.
  - Sibling path `14030F249`-adjacent `14030F570` (called from the
  `14030F285` mesh entry): resolves the world singleton via
  `1402C8940` (`return DAT_141C74DF8`, 100 xrefs — a shared global
  getter, not interaction-specific), then `1404F57A0` ->
  `1404F23D0` -> `1404F39A0`: a PARAM-id decoder
  (`(param/10000000)%10`, `(param/1000)%10`, `(param/10000)%10`,
  `(param/100000)%100` digit extraction with the 99-bypass) gating a
  bit test at `param_1+0x224`. Same computed-dispatch family, not a
  prompt/LOS owner.

  Verdict: the LOS-accepted continuation is virtual-dispatch into the
  computed mesh, not a distance/LOS computation returning to an indexed
  caller. The static wall stands one hop past the selector on both the
  present and absent paths. What remains is exclusively runtime:
  selected row/dummy/LOS correlation from a footer-valid capture.

### 26 Oct proximity shape recovered: yaw-frame 2D distance, not 3D reach

Six bulk calls, read-only. Eligibility-accepted continuation walked end
to end (entry per hop, bodies at the leaves):

- `14238E1C6` -> `14030D4A0` (stub: `140321B70` position read, then
  `140EF6C54`) -> `14030D4AD` (stub: `140321F80` full-transform copy,
  then `142ED2141`) -> `142ED2141` (wrapper: `14030D4D2(buf, 0x40490FDB)`
  -- 0x40490FDB is float pi) -> `14030D4D2` (stub: `140195010` basis
  build, then `1428030FA`) -> `140195010` (worker, 22 callers: builds
  orthonormal yaw frame from the angle via `1401951E0`; X/Z from
  sin/cos pair, Y=(0,1,0)) -> `1428030FA` (wrapper:
  `142C0E4EB(world, [row+0x1c], 0)`) -> `142C0E4EB` (wrapper: squares
  two components, calls `142F9D1FC`) -> `142F9D1FC` (7 instructions:
  SHUFPS horizontal add, SQRTPS, CMPNEQPS NaN guard vs zero at
  141608F90, JMP 142D9E6E9) -- a planar 2D distance.
- So the native proximity test projects onto the yaw frame and measures
  2D distance, gated by the row's +0x1c field through the world
  registry 141C75DD8. Our fail-closed gate uses 3D hand-to-target
  distance against validDist -- conservative relative to native (2D <=
  3D always, so native accepts a superset of what we accept; refusing
  by default cannot admit what native would reject on distance --
  orientation/LOS terms still unrecovered).
- `140321B70` (201 callers) and `140321F80` (33 callers) are shared
  position/transform primitives, also used by the melee range
  geometry. `142D9E6E9` (distance consumer) unrecovered -- one hop.
  No distance threshold value, prompt text, or LOS term claimed.

### 26 Oct prompt assembly walked to the dispatch mesh

Five bulk calls, read-only. Distance consumer chain from 142D9E6E9:

- `142D9E6E9` (normalize via divps, call `140FA9704(f, mask)`) ->
  `140FA9704` (stub: `1421C0DEC(f, ~mask & 0x7fc00000)`) ->
  `1421C0DEC` (7 ins: ORPS/SHUFPS/MULPS lane work, JMP 1426A5101) ->
  `1426A5101` (SSE matrix-vector accumulation through basis matrices
  + constant 1414B6A90, JMP 14030D577) -> `14030D577` (stub:
  `14030C320` row-string lookup, then `142CCDD82`) -> `142CCDD82`
  (8 ins: NULL-check RAX, forged RET selecting 14030D8FD vs
  14292C96A) -> `1422D3EA2` (forged-RET mesh entries, counter
  decrement at [RBP+0x44], JGE/JMP computed branches).
- Reading: the normalized proximity vector is transformed through
  basis matrices, the row's display string is resolved (14030C320,
  same loader used in 1403138E0), and the prompt enters the standard
  forged-return dispatch mesh. Computed dispatch past this point --
  static stops here, same boundary as melee's jumptable.
- H1 static chain complete: selection -> predicates -> 2D distance ->
  string resolve -> prompt mesh. Remaining: dispatch destinations,
  threshold value, LOS term, dummy position -- all need live
  correlation or deeper computed-dispatch recovery. No claims beyond
  shape.

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

### 27 Oct eligibility continuation walked to its terminal: distance is compared, never returned

Read-only live GhidraMCP against `DSRFull` (no save/repair/launch/invocation).
The eligibility-accepted hop `14030D4A0` was walked leaf by leaf:

- `14030D657` reads the actor position (`140321B70`) into a stack buffer, then
  `1427BB313` squares the 4-component delta against XMM12 and `sqrtps`es it —
  a 4D magnitude, not the yaw-planar 2D shape of the sibling path.
- `14030D685 -> 14030F050` computes the row range `validDist * 0.01 * 2`
  (PARAM `+0xC` via row `+0x40`), then `142C601DE -> 1406ADA00` does
  `COMISS XMM4,XMM6` (the eligibility boolean) and carries the two points
  through `1407E0A41 -> 14034BF0E -> 141001762 -> 142F22992`, which normalizes
  the delta (zero/NaN-guarded) and calls `1425B7AD2`: it writes a transformed
  point at `[RDI+0xD0..0xDC]` and forged-RETs to either `14273403D` or
  `14030D89B`. `14030D89B` itself forged-RETs via `[RCX+0x3C]`-indexed code.
  `14273403D` normalizes two direction vectors and, when row `+0x40` is
  present, forged-RETs through `[RAX+0x18]` (`142805F5C`) into `14030D7D5`;
  otherwise it falls to `14030D7D1 -> 14030D753`, an unrecovered continuation.
- The prompt path is now fully named: `140314E30` calls `14071BD30(uVar17,1,1)`
  on LOS failure, where `uVar17` is the selected row PARAM's `+4` dword read
  inline in the selection loop. `14071BD30` maps `(1,1)` to message ids
  `(2,1)`/action `7` and pushes them through the `140686BC0/C20/BF0/1406871D0`
  prompt builder, clearing `DAT_141C88D98+0x168`.

So eligibility = distance-compared-and-normalized geometry consumed entirely
inside forged-return dispatch; no boolean, dummy position or distance ever
returns to an indexed caller. The prompt *text* identity (`+4`) and the
prompt-vs-activation branch are recovered; dummy position, generation and a
callable activation owner are not, and the computed-dispatch mesh past
`1425B7AD2`/`14030D7D5` is the same static wall as melee's jumptable.

Verdict: H1 static chain is closed to the computed-dispatch boundary. What
remains is exclusively runtime: selected row/dummy/LOS correlation from a
footer-valid capture. The physical-reach gate stays fail-closed; no range,
prompt or activation binding follows from this walk.

### 27 Oct selection-owner chain recovered read-only (DSRFull, no save/repair)

Read-only `analyze_function_complete` against the live GUI `DSRFull`
(`/DarkSoulsRemastered.exe`, no launch/save/repair/write/commit). The
per-frame group update and the controller-driven activation path are now
named end to end:

- `140314D40` (sole caller `140312B00`): resolves the local actor via
  `140371ED0`, compares its area id against the group descriptor, then runs
  prompt (`140315180`), selection (`140314E30`), mark (`140314DC0`) and
  completion (`140315200`) in order. Mark = `140314DC0` (sole caller
  `140314D40`): per-row `14030D910` sweep — a state machine on row
  `+0x10/+0x20` with a `DAT_141C75DE4 % 0x32` counter gate, not an
  activation. Completion = `140315200` (sole caller `140314D40`):
  per-row `14030DA40` timer sweep — states 1/2 are float-timer
  countdowns on row `+0xC/+0x10/+0x12` (with the
  `[MyListIdx][KickEventID][ObjHandle][TriggerState][Kick][Change]
  [SendNet]` log on expiry); state 3 is the PARAM-gated branch
  (`14030C320` on row `+0xE`, `1402FC080` check, `14030EA40` on
  failure, `140318DC0` on the `+0x14`-resolved actor when the
  `+0x2E8` chain is live). Neither mark nor completion activates;
  activation is exclusively `140313470 -> 1403138E0`. `140318DC0`
  (1 xref) is its commit flag (`+0x39 = 1` when the row is nonzero) —
  trivial, not an interaction owner. `14030E500` (4 xrefs) is its
  commit logger (`+0x78` list walk + `14030ED10`/log emit) — generic,
  not an interaction owner. `14030ED10` (7 xrefs) is its log emitter
  (`+0x20` state reset + `1400C5C00` log emit) — generic, not an
  interaction owner. `1405CB8C0` (10 xrefs) is its commit resolver
  (`+0xF8` virtual + `+0x230` read) — trivial, not an interaction owner.
  `14030F5F0` (2 xrefs) is its log formatter (`140491840` dispatch +
  `1400C5C00` format) — generic, not an interaction owner. `140491840`
  (1 xref) is its log dispatcher (`+0x38C/+0x390` write) — trivial, not
  an interaction owner. `1400C5C00` (100 xrefs) is its log formatter
  wrapper (forwards to `1400C5C30`) — generic, not an interaction owner.
  `1400C5C30` (2 xrefs) is its log formatter core (`140D8EDD0` format +
  `1400C6450` append) — generic, not an interaction owner. `140D8EDD0`
  (20 xrefs) is its format wrapper (forwards to `140D8F820`) — generic,
  not an interaction owner. `140D8F820` (9 xrefs) is its format core
  (printf-style `%`-engine) — generic, not an interaction owner; the
  log descent stops here. `1400C6450` (9 xrefs) is its string-append
  leaf (std::string fill + `1400BCD90` grow) — generic, not an
  interaction owner. `1400BCD90` (18 xrefs) is its string-grow leaf
  (std::string realloc + memcpy) — generic, not an interaction owner.
  - `1403138E0` (callers `140313470` + `1403135A0` + `140313740`):
  resolves the actor via `1403714B0`, the PARAM via `14030C320` on
  row `+0x1C`, and the `1402FC080` gate; on failure writes row
  `+0x2C` and runs `14030ECB0`/`14030EA40` (the refusal path;
  `14030ECB0` (2 xrefs) is its refusal sibling (`+0x20 = 3`,
  `+0x24 = 1.0`, `+0x18 = 0`) — trivial, not an interaction owner); on
  success emits the `Unknow` + name-string log pair and drives the
  `142BE029A` thunk (1 xref: `R9 != 0` → `1401B57A2`, else
  `140316220` tail (empty stub — unrecovered; branch stops here);
  `1401B57A2` (1 xref) is its thunk leaf (`RBX != 0`
  → `1409CD0CB`, else `140316220` tail; `1409CD0CB` (1 xref) is its
  thunk target (`stack+0x130 != 0` → `142A95A12`, else `1420CA0D6`
  dummy path; `142A95A12` (1 xref) is its lock wrapper (LOCK/UNLOCK +
  `14096ED2F` — unrecovered stub (returns stack garbage); branch
  stops here)), the `14032C470`/`14032C130`
  actor-state writes (`14032C130` (11 xrefs): `+0x68/+0x38` virtual
  pair + `+0x16C/+0x174` stores + `140388670` flag (`140388670`
  (14 xrefs): `+0x98` list + `+0x4B0/+0x440` clears +
  `14038A4E0/5E0/730` ops (`14038A4E0` (2 xrefs): `+0x8` bit clear
  + `+0x108..0x13C` zeroing — trivial; `14038A5E0` (2 xrefs):
  `+0x35C..0x390` + `+0x284/+0x144` bit clears — trivial;
  `14038A730` (1 xref): `+0x144` bit clear + `+0x104/+0x3A8` zeroing —
  trivial; `140321B20` (26 xrefs): `+0x68/+0x298` else `+0x68/+0x88`
  — trivial)),
  the `140316D40`/`140508AE0` and `140316D20`/`140508AA0` message
  pairs (`140316D40` (1 xref): forwards `141C7D010` to `14050CDE0` —
  trivial; `14050CDE0` (58 xrefs) is its message leaf (`+0xF8 == 3`)
  — trivial; `140316D20` (6 xrefs) is its message sibling
  (`14050BD90` count > 0; `14050BD90` (25 xrefs): `(+0x1F8 − +0x1F0)
  >> 6` — trivial; `140508AE0` (1 xref): forwards to `14050B880` —
  trivial; `14050B880` (56 xrefs) is its queue writer (`+0x1F8/+0x1F0`
  scan + `1410666A0`/`14050B6B0` match (`1410666A0` (28 xrefs):
  `+0x10/+0x20` virtuals + `141074A90` check (`141074A90` (2 xrefs):
  `+0x10` byte — trivial) — generic; `14050B6B0` (44 xrefs) is its
  queue writer sibling (deep `14107xxxx`/`14050xxxx` chain — generic;
  descent stops here); `14050B540` (75 xrefs) is its queue matcher
  (deep `14107xxxx`/`14050xxxx` chain — generic; descent stops here))
  — trivial),
  and the
  `140316D60`/`140316230`/`140316AC0` row re-resolve (`140316D60`
  (1 xref): forwards `141C7D010` to `14050CE80` — trivial;
  `14050CE80` (67 xrefs) is its row commit leaf (`+0xF8 == 6`) —
  trivial; `140316230` (3 xrefs) is its row resolver (bounds check +
  `1403133D0` sub-array resolve; `1403133D0` (5 xrefs) is its
  sub-array resolver (bounds check + 0xF0-stride address) — trivial;
  `140316AC0` (1 xref) is its timer-queue writer (`+0xAF8` list scan +
  `*row = 0x41200000`) — trivial).
  The return value is the native activation result the observer
  already records; its meaning (progression vs acceptance) is still
  runtime-bound, not statically classified.
  - `140313740` (callers `1403165B0` + `1403162F0`): the sibling
  activation entry — same `14030EF00/FE0/F000/F0C0` tuple reads and
  same `1403138E0` completion as `140313470`, but entered from the
  manager sweep (`1403165B0`) rather than the selection loop. Marks
  the row `+0x30 = 0x100` (vs `0x101` in `140313470`) and calls the
  `[row+0x48]+0x28` virtual first. Two activation entries, one
  completion: the static chain is closed on the activation side.
  `14030EF00` (8 xrefs) is its tuple reader (`1403714B0` actor resolve
  + `+0x48/+0x38` + `+0x140/148/160` virtuals + `+0x40` PARAM field) —
  generic, not an interaction owner. `14030EFE0` (5 xrefs) is its tuple
  sibling (`[row+0x48]+0x48` virtual, same bone-index source) —
  per-row virtual, runtime-bound. `14030F000` (5 xrefs) is its tuple
  sibling (`[row+0x48]+0x40` virtual + `+0x40/+0x17` PARAM byte) —
  per-row virtual, runtime-bound. `14030F0C0` (4 xrefs) is its tuple
  sibling (`+0x40/+0x1C` PARAM byte) — trivial, not an interaction
  owner.
  - Manager sweep `1403165B0` (sole caller `140316528`, after the
  per-group `140312B00` loop): the `+0xB04/+0xB08` float-timer gate,
  then the `140316870` iterator driving `140313740` per candidate
  (with the `14050CE80` gate and the `+0x70/+0x8` handle re-resolve;
  `140508B00` (5 xrefs) is its queue reader (`14050B950` queue +
  `14050B540` match; `14050B950` (40 xrefs): `+0x1E8` — trivial) —
  generic),
  then the `140508BC0`/`140508C80` message-drain loops writing row
  `+0x20` and queuing `1403169C0` (`140508BC0` (6 xrefs): `14050B950`
  queue + `14050B540` match — generic; `140508C80` (6 xrefs) is its
  message drain sibling (same queue + match — generic); `1403169C0`
  (2 xrefs) is its timer-queue writer (`+0xAF8` list scan + row copy —
  generic; `140316C20` (2 xrefs) is its list unlink (`+0xAF8` match +
  `140CC29C0` free (`140CC29C0` (100 xrefs): `140CC3AE0` alloc +
  `140CC3380` init (`140CC3380` (17 xrefs): system-heap init —
  generic; descent stops here) + `140CC3200` free (`140CC3200`
  (4 xrefs): heap free core — generic; descent stops here; `140CC3AE0`
  (17 xrefs): `140D96B10` + `140CC3090` alloc (`140CC3090` (3 xrefs):
  `140CCCFB0`/`140CCC930/9D0` init — generic) — generic) — generic) —
  trivial))).
  `140316528` itself (no indexed
  callers — reached via computed dispatch) runs the per-group loop,
  then `1403165B0`, then `140316B30`, then the `+0xB10/+0xB20`
  flag write (`140317710` (6 xrefs): LOCK/UNLOCK + returns 0xB3 —
  trivial). The per-frame update order is fully named:
  `140312B00` (groups) -> `140314D40` (prompt/select/mark/complete)
  -> `1403165B0` (manager sweep) -> `140316B30` (timer queue drain).
  - `140316B30` (sole caller `140316528`): walks the `+0xAF8` timer
  queue, decrementing each entry by the frame delta; on expiry
  re-resolves the row via `140313400`, compares the `[row+0x48]+0x30`
  virtual against the queued value, and on mismatch fires the
  `[row+0x48]+0x28` virtual then `1403135A0` (the third activation
  entry — clears the row `+0x30/+0x32`, same tuple reads, same
  `1403138E0` completion with `param_3 = 1`). Three activation
  entries (`140313470` selection-loop, `140313740` manager-sweep,
  `1403135A0` timer-expiry), one completion. The static activation
  side is fully closed; only the runtime row/dummy/LOS binding is
  open.
  - Row resolvers typed: `1403133D0` (0x58-stride sub-array ->
  0xF0-stride row, bounds-checked) vs `140313400` (direct 0xF0-stride
  row, bounds-checked) — the two layouts' indexers, not duplicate
  code. Manager iterator `140316870` (sole caller `1403165B0`):
  drains the `140508B00` message queue, else consumes the
  `+0xB18/+0xB19` pending byte (0xFF = resolve actor area via
  `140371ED0`), packs group-index/row-index into the out-param, and
  resolves the `[row+0x48]+0x30` virtual. No distance, prompt or LOS
  term in any of the three; the static wall stands.
  - Dummy resolvers typed: `1402FDA90` (20 xrefs, 16 callers) reads
  `param_1+0xB0` (model object); NULL yields identity
  `(0,0,0,0,0,0,1.0)` — the fail-safe dummy, not a measured position;
  non-NULL delegates to `14021E420` -> `14021CC60`, which builds a
  `TempMdlMtx`/`DLTemporaryHeap` frame and calls `140228830` with the
  `param_1[3]+0xB8` bone table, the `param_2` matrix slot and
  `param_1[3]+0x100` — a skinned-bone transform read, three vftable
  branches (`[param_1+8]` = 0/1/2 select `14022C8A0`/`14022CBF0`/
  direct). `1402FBA70` (46 xrefs, 39 callers) is the sibling: a pure
  48-byte struct copy (`param_1+0x80..0xA8` -> out, 6 limbs) with no
  transform — the static-pose path. `14030E8C0` selects between them
  on `param_3` (1 = skinned via `1402FDA90`, 0 = static via
  `1402FBA70`). The dummy is a bone-pose read, never a hand-distance
  or LOS term; which bone index feeds it is still runtime-bound.
  - Bone-index consumer `140228830` (13 xrefs, 3 callers): binary
  search on the `param_1+0x20` short table for `param_3` (the bone
  id), then `140228C70` per-matrix compose into the `param_2+0x20`
  output slot, `param_6`-capped. The bone id arrives as `param_3`
  from `14021CC60` — which receives it as its own `param_3` from
  `14021E420` — which receives it as its own `param_3` from
  `1402FDA90` — which receives it as its own `param_3` from
  `14030E8C0` — which receives it as `SIL` (the low byte of the
  `[row+0x48]+0x48` virtual's return) in `14030F4ED`. So the dummy
  bone index is a per-row virtual return, not a constant: the static
  chain ends at that virtual call. Which bone each row selects is
  runtime-bound; the reach gate stays fail-closed.
- `140315180` (sole caller `140314D40`): reads candidate-present at group
  +0x25, gates on `14071AC10` mask `& ~0x37`, and writes the button result
  at group +0x24 via `1404E7070` — the prompt/button bytes, not object
  identities or LOS proof. `14071AC10` itself is a global UI-state bitmask
  (29 xrefs binary-wide, menu/loading/dialog flags) — generic gating, not
  an interaction owner. `1404E7070` itself is table-driven button plumbing
  (24 xrefs, binary-search over `141367940` + `1404E7290/6D00/6F40` chain) —
  generic, not an interaction owner. `1404E7290` (2 xrefs) is its
  precondition gate (PARAM-id check + actor-state reads) — generic gating,
  not an interaction owner. `1404E6D00` (2 xrefs) is its slot allocator
  (triple-list search + ordered insert) — generic list plumbing, not an
  interaction owner. `1404E6F40` (7 xrefs) is its flag writer (slot-flag
  checks + float-constant stores) — generic, not an interaction owner.
  `1404E7230` (1 xref) is its PARAM-id lookup (binary search over
  `141367940`) — generic, not an interaction owner. `1404E73E0` (6 xrefs)
  is its flag precondition (bitmask over actor/session state) — generic
  gating, not an interaction owner. `140480A50` (7 xrefs) is its
  PARAM-gated actor-state check (`14030C1B0/290` + `1402FC080` gate +
  `140371200/360` actor resolve) — generic gating, not an interaction
  owner. `140480EB0` (87 xrefs) is its actor-resolve sibling (same
  `140371200/360/BD0` resolve family) — generic, not an interaction
  owner. `1404F57A0` (69 xrefs) is its PARAM-id stub (forwards to
  `1404F23D0`) — generic, not an interaction owner. `1404F23D0` (44
  xrefs) is its PARAM decoder (`+0x224` gate + `1404F39A0` bit test) —
  generic, not an interaction owner. `1404F39A0` (12 xrefs) is its
  digit-extraction decoder (`/10000000`, `/1000`, `/10000`, `/100000`
  with the 99-bypass) — generic, not an interaction owner. `1404F5580`
  (4 xrefs) is its pair-table search (static int-pair scan) — generic,
  not an interaction owner. `1403DDEB0` (8 xrefs) is its record-table
  search sibling (0x40-stride scan + `1403D2FD0` match) — generic, not an
  interaction owner. `1403D2FD0` (5 xrefs) is its record matcher
  (0x70-stride scan + `1400D7630` field compare) — generic, not an
  interaction owner. `1400D7630` (51 xrefs) is its field reader (one
  dword copy from `+0x8`) — trivial, not an interaction owner.
  `14032C470` (23 xrefs) is its actor-state check (`+0x68/+0x20` chain
  + `140439F30`/`1402BF4F0` gates) — generic, not an interaction owner.
  `14032C5D0` (9 xrefs) is its actor-state stub (forwards `+0x68/+0x48`
  to `140386E60`) — trivial, not an interaction owner. `140386E60`
  (4 xrefs) is its flag-compare leaf (`+0x30/+0x50` identity + `+0x1DC`
  bit test) — generic, not an interaction owner. `140E5AAF0` (16 xrefs)
  is its identity stub (forwards `+0x28/+0x20` to `140E5E030`) —
  trivial, not an interaction owner. `140E5E030` (2 xrefs) is its
  index-scan leaf (0x48-stride pointer match) — generic, not an
  interaction owner. `140439F30` (43 xrefs) is its effect dispatcher
  (0xA8-stride match + `14043BC70` call) — generic, not an interaction
  owner. `14043BC70` (8 xrefs) is its effect leaf (id match +
  `14043C840` call) — generic, not an interaction owner. `14043C840`
  (2 xrefs) is its effect executor (`+0x138` gate + `140436670` call) —
  generic, not an interaction owner. `140436670` (7 xrefs) is its effect
  worker (0x78-stride id match + `14043EFE0` call) — generic, not an
  interaction owner. `14043EFE0` (2 xrefs) is its effect applier
  (`+0x50/+0x60/+0x4C` flag gate) — trivial, not an interaction owner.
  `1402BF4F0` (7 xrefs) is its actor gate (`+0x18/+0x20` flag + `+0x10`
  state + `14031D4F0` check) — generic, not an interaction owner.
  `14031D4F0` (85 xrefs) is its flag-bit leaf (`+0x30/+0x14` bit 0) —
  trivial, not an interaction owner. `1402FC080` (6 xrefs) is its PARAM
  gate (`+0x50` id match + `+0x46` bit 3) — trivial, not an interaction
  owner. `14030C1B0` (50 xrefs) is its PARAM index search (0x20-stride
  scan + `140303EF0` match) — generic, not an interaction owner.
  `140303EF0` (2 xrefs) is its PARAM matcher (0x160-stride scan +
  `140305C50` match) — generic, not an interaction owner. `140305C50`
  (1 xref) is its row matcher (binary search over `+0x80` id table) —
  generic, not an interaction owner. `14030C290` (49 xrefs) is its PARAM
  resolver sibling (id-split index + bounds check) — generic, not an
  interaction owner. `140371200` (100 xrefs) is its actor search
  (`+0xA8/+0x38` scans + `14036AD00`/`14033EC10` match) — generic, not
  an interaction owner. `14036AD00` (1 xref) is its actor matcher
  (binary search over `+0x78` id table) — generic, not an interaction
  owner. `14033EC10` (76 xrefs) is its actor accessor (0x38-stride slot
  read) — trivial, not an interaction owner. `140371360` (100 xrefs)
  is its actor resolver (id-split index + `14033EC10` slot read) —
  generic, not an interaction owner. `140371BD0` (100 xrefs) is its
  actor resolver sibling (`+0x38` scan + `+0x570` id match) — generic,
  not an interaction owner. `140371ED0` (43 xrefs) is its local-actor
  entry (`141C77E88+0xF0` else `+0x68`) — trivial, not an interaction
  owner. `1403714B0` (100 xrefs) is its actor entry sibling (id-split
  index + `14033EC30` slot read) — generic, not an interaction owner.
  `14033EC30` (3 xrefs) is its slot reader (0x38-stride address) —
  trivial, not an interaction owner.
- `140314E30` (sole caller `140314D40`): the ObjAct selection loop. Walks
  `count` rows at stride 0xF0, rejects state 3/4, tests eligibility via
  `14203FAB4`, collects the first valid index, resolves its prompt text via
  `[row+0x48]+0x80`, then branches on group +0x24: nonzero tests LOS via
  `1420BEFB7` (failure shows the prompt via `14071BD30`, success activates
  via `140313470`); zero takes the direct activation path.
- `14071BD30` (7 xrefs, 6 callers incl. `140314E30`): maps `(1,1)` to
  message ids `(2,1)`/action 7 through the `140686BC0/C20/BF0/1406871D0`
  prompt builder, clearing `DAT_141C88D98+0x168`. Prompt text identity is
  the selected row PARAM `+4` dword read inline in the selection loop.
  `140686BC0` itself is a shared message-buffer initializer (100 xrefs
  binary-wide) — generic plumbing, not an interaction owner.
  `140686C20` (100 xrefs) is its message setter (getter + three thunk
  stores) — generic, not an interaction owner.
  `140686BF0` (5 xrefs) and `1406871D0` (14 xrefs) are its message
  wrappers (both forward via `14071BD10` to `140688xxx` thunks) —
  generic, not interaction owners. `14071BD10` (76 xrefs) is its
  shared getter (`141C88D98+0xA38` read) — trivial, not an
  interaction owner. `140688150` (1 xref) is its thunk target
  (three-dword store) — trivial, not an interaction owner.
  `140688260` (1 xref) is its thunk target (`+0x14` byte store) —
  trivial, not an interaction owner. `140688210` (1 xref) is its thunk
  target (`+0x34` dword store) — trivial, not an interaction owner.
  `1406880E0` (1 xref) is its thunk wrapper (`+0x38/+0x3C` writes +
  `14071ABF0` call; `14071ABF0` (100 xrefs) is its slot writer
  (`141C88D98+0x30[idx]` dword store, idx < 500) — trivial, not an
  interaction owner) — generic, not an interaction owner.
  `140688340` (2 xrefs) is its thunk setter (`+0x10/+0x20` writes +
  `14071ABF0`/`140681EA0/BB0` calls) — generic, not an interaction
  owner. `140681EA0` (100 xrefs) is its flag wrapper (`14071ABD0`
  gate + `141C847B0` check; `14071ABD0` (100 xrefs) is its slot
  reader (`141C88D98+0x30[idx]` dword read) — trivial, not an
  interaction owner) — generic, not an interaction owner.
  `140681BB0` (3 xrefs) is its event emitter (`+0xD8` virtual ×2) —
  generic, not an interaction owner.
- `140313470` (callers: `140314E30` + `1403162C0` only): marks the row
  (`+0x30 = 0x101`, `+0x32 = 1`), reads actor fields via `[row+0x48]`
  virtuals, and completes through `1403138E0` after the
  `14030EF00/FE0/F000/F0C0` tuple reads.
- `1403162C0` (sole caller `140312160`): bounds-checks the index against
  `+0x28`, then forwards to `140313470` on the `+0x30` sub-array (stride
  0x58, not 0xF0) — a second group layout, owner unclassified.
- `140312160` (sole caller `1405C5FE0`): resolves the group index through
  the `+0x10/+0x18` id array, then forwards to `1403162C0` on manager
  global `141C75E40`. `1405C5FE0` (5 xrefs, no indexed callers) resolves
  the area id via `140318DA0(param+0x5D, slot)` and the actor handle via
  `[*param+0x50]` — the controller-driven entry into the same activation
  path. `140318DA0` (3 xrefs) is its slot reader (`param_1+0x10`,
  5 slots max, else -1) — trivial, not an interaction owner;
  `1405C5F50` (2 xrefs) is the sibling entry (same slot/group
  resolve, then `140312070` boolean mark) — generic, not an
  interaction owner. `140312070` (3 xrefs) is its mark path
  (`140316230` resolve + `140316290` mark) — generic, not an
  interaction owner. `140316290` (1 xref) is its marker wrapper
  (bounds check + `14030EC80` mark) — generic, not an interaction
  owner. `14030EC80` (1 xref) is its marker (state 2, `+0x24 =
  1.0`, `+0x18 = 0`, `+0x28 = handle`) — trivial, not an interaction
  owner. `140628FB0`
  (4 xrefs) is its group resolver (bounds check + 0xD0-stride slot
  read) — trivial, not an interaction owner. `1405CA640` (6 xrefs)
  is its controller sweep (case 0x74/75/78: slot/group/actor resolve
  + `140312070` mark + `140312120` resolve + `14030C320` PARAM +
  `14030EFE0`/`1402FDA90` dummy) — generic, not an interaction owner.
  `140312120` (2 xrefs) is its resolver wrapper (bounds check +
  `140316230` resolve) — generic, not an interaction owner.
  `1405C7280` (26 xrefs) is its actor accessor (`+0x8/+0x10`
  virtuals + `14037A630` read) — generic, not an interaction owner.
  `14037A630` (95 xrefs) is its position reader (`+0x28` pair copy)
  — trivial, not an interaction owner.
- `140312070` (callers `1405C5F50` + `1405CA640`): resolves the row via
  `140316230` (bounds-checked `1403133D0`: `index * 0xF0 + base+0x18`),
  then marks it via `140316290 -> 14030EC80` (state 2, `+0x24 = 0x3F800000`,
  `+0x18 = 0`, `+0x28 = handle`). Row resolver and activation marker,
  not a prompt/LOS owner.
- Nearest-row sweep `140318AF0` (sole caller `1405C3B40`; `1405C3B40`
  (4 xrefs) is its sweep entry (deep per-frame update — generic;
  descent stops here; `1405C42D0` (6 xrefs) is its per-frame tail
  (deep `1405CA640/1405C9470/1405C9930` chain — generic; descent stops
  here; `1405C9470` (18 xrefs) is its per-frame leaf (deep
  `1405C9070`/`14037A500` chain — generic; descent stops here;
  `1405C9930` (14 xrefs) is its per-frame tail (deep
  `1405CA640/140194xxx/140195xxx` chain — generic; descent stops
  here; `140194F60` (8 xrefs) builds a matrix using the sincos
  helper `1401951E0` (6 xrefs); `140195010` (27 xrefs) builds a
  basis with that helper; `140194E80` (11 xrefs) multiplies 3x4
  float matrices; `1401950C0` (6 xrefs) builds an axis-rotation
  matrix. These are generic math helpers; descent stops here);
  `1405C9070` (47 xrefs) is its slot resolver (id-switch +
  `14061D860/14061AFC0/14061E050` (`14061D860` (9 xrefs): deep
  `140621xxx/140619560` chain — generic; descent stops here;
  `14061AFC0` (2 xrefs): `140619580` gate + `140619560` read
  (`140619580` (14 xrefs): computed dispatch — unknown; branch stops
  here; `140619560` (27 xrefs) is its read leaf (`+0x8` switch →
  `+0x28`/`+0x40`) — trivial, not an interaction owner) — generic;
  descent stops here; `14061E050` (2 xrefs):
  `140622B60` + `140619560` read (`140622B60` (3 xrefs): `+0x8`
  list scan + `+0x20` — trivial, not an interaction owner) — generic;
  descent stops here) —
  generic; descent stops here)));
  `1405C4870` (7 xrefs) is its slot reader
  (`+0x58` virtual else `+0x8` dword) — trivial, not an interaction
  owner; `140628F80` (2 xrefs) is its group resolver (bounds check
  + 0xD0-stride slot read) — trivial, not an interaction owner;
  `1405C72F0` (23 xrefs) is its actor accessor (`+0x10` virtual) —
  trivial, not an interaction owner; `140321B70` (100 xrefs) is its
  position wrapper (`+0x68` + `14037A500` read) — generic, not an
  interaction owner; `14037A500` (91 xrefs) is its position reader
  (`+0x28/+0x10/+0x18` pair copy) — trivial, not an interaction
  owner; `140229630` (9 xrefs) is its matrix core (deep
  `140209BF0/140094520/140194030/1400ADD90` chain — generic; descent
  stops here)):
  for each of 5
  slots resolves the group via `140628F80`, the actor position via
  `140321B70`, and each candidate row's dummy via `14030E8C0(row, out, 1)`;
  keeps the minimum squared distance in `param+0x14/0x18` pairs and sets
  `param+0x38 = 1` when any candidate matches. `140314150` (5 xrefs)
  is the same sweep entered per-group (state-0 rows, `14030E8C0`
  dummy, 3.0 m cap) — generic, not an interaction owner. `14030E8C0`
  resolves the dummy from the row's `+0x1C` object id through
  `14030C320` + `1402FDA90` (or the `1402FBA70` path when `param_3 ==
  0`). `140314620` (2 xrefs) is its commit sweep (actor resolve +
  position + `140314150`) — generic, not an interaction owner.
  `140316450` (1 xref) is its commit wrapper (bounds check +
  `140314620`) — generic, not an interaction owner. `1403120E0`
  (2 xrefs) is its commit resolver (`140316450` + `140316480`) —
  generic, not an interaction owner. `140316480` (1 xref) is its
  commit marker (bounds check + `1403146A0`) — generic, not an
  interaction owner. `1403146A0` (1 xref) is its row marker (`+0x10
  = 1`) — trivial, not an interaction owner.
- `1405CA640` case `0x74/0x75/0x78` is the same sweep entered from the
  controller path: `140318DA0(slot 0)` -> `140312070` (mark) ->
  `140312120` (resolve) -> `14030C320` (PARAM) -> `14030EFE0`/`1402FDA90`
  (dummy) -> commit at `LAB_1405ca8fb`.

So the full native chain is placed: controller entry (`1405C5FE0` /
`1405C7170` / `1405CA640`) -> group resolution -> selection loop
(`140314E30`) -> prompt (`14071BD30`) vs activation (`140313470`) branch
-> nearest-row sweep (`140318AF0` / `140314150` / `14030E8C0`) -> mark
(`140312070` / `1403146A0`). What remains is exclusively runtime:
selected row/dummy/LOS correlation from a footer-valid capture. The
physical-reach gate stays fail-closed; the offline `InteractionSelection`
struct now pins the row/object/generation/dummy identity the loop itself
consumes, but no range, prompt or activation binding follows from this
static walk.

A read-only parse of `build/verification/iteration-5-20261027/DSVR-trace-51904-348727796-1.jsonl` reports `valid: true`, no errors and capture-integrity PASS, with three `native_interaction_activation` callbacks. All have flags `0xF` (pre/post snapshot, stable row identity, PARAM range read); their row indices are 1, 27 and 0, raw ranges 70, 60 and 70, and return limbs encode 1, 1 and 1. The report is still `complete: false` and combat-case coverage is NONE: its QPC span overlaps none of the declared windows. These callbacks do not contain dummy coordinates or LOS observations and do not qualify target generation, prompt visibility or activation semantics; H1/H2 remain blocked.

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
