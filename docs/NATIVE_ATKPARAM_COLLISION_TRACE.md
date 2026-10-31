# AtkParam and native collision trace

Static Ghidra observations, 29 October 2026. Program: `DarkSoulsRemastered.exe`
(`DSRFull` view); revision token `1793259089346:71ab2053:3`, saved state,
`unsaved_changes=false`. This note records address-level leads, not a qualified
native ABI or runtime result. Confirm executable identity against the pinned
hash before relying on these addresses in another database revision.

## AtkParam -> attack payload

`FUN_140528910` resolves an attack row from a selector and row ID. The caller
`FUN_140528CE0` then calls `FUN_1405290C0` to construct an attack record and
`FUN_140529670` once per slot. The slot builder reads:

- radius from selected AtkParam row `+ slot * 4`;
- tip/tail dummy IDs from `+0x2C + slot * 2` and `+0x34 + slot * 2`;
- hit type from `+0x6A + slot`;
- hit source type from `+0x7C`.

It writes these to output record offsets `+0x50`, `+0xAC`, `+0xB0`, `+0x54`,
and `+0xB4`, respectively. A slot is rejected for non-positive radius or a
negative first dummy ID unless an output-record flag bypasses that first-ID
check; the tail ID is copied without a corresponding validity check in this
function. `FUN_1403C93B0` reaches this builder through `FUN_140528CE0` and constructs
its local record. The success path calls a five-byte thunk at `0x1403C9DD0`,
which jumps to `0x142AC2A92`; the jump has no explicit arguments, so record
consumption is unproven. Ghidra's 34-byte target decompiles to LOCK/UNLOCK
pairs. Its instructions construct a stack transfer into `0x1421AE9BC`; that
block performs another stack transfer and RETs toward `0x1421C64B5`. Ghidra has
instructions there but no function boundary; subsequent stack shuffling reaches
`0x14307E0D7`, whose decompilation has an unrecovered indirect jump table. The
consumer is still unknown. This is a control-flow cliff, not proof of payload
consumption. Recover the real boundary from pinned bytes and PE unwind metadata
before drawing conclusions.

This proves selected AtkParam values are copied into a local native attack
record candidate. It does **not** resolve dummy IDs to model/world positions,
prove that this record enters the player melee path, or demonstrate that damage
is submitted or applied. Do not use these output offsets as a game-memory struct
declaration.

## Separate native contact/collision path

`FUN_1403C6030` is an update path that asks `FUN_1403C7B10` for contact-frame
candidates, converts candidate positions/directions into the contact record
consumed by `FUN_1403C4020`, and routes qualifying records through
`FUN_1403C7520`. `FUN_1403C7B10` dispatches by source actor kind: kind 1 uses an
actor virtual at `+0x448`; kind 2 calls `FUN_1402FD8F0`.

For kind 2, `FUN_1402FD8F0` obtains model/collision state through the object
virtual at `+0x18`, then calls `FUN_140227CB0`. That routine indexes a sorted
short-keyed table, selects a range of entries with `0x50`-byte source stride,
transforms each through `FUN_14021AB70` (via `FUN_140228BD0`), and writes
six-qword records to its caller's output array. `FUN_14021AB70` delegates to
`FUN_140229630`, which obtains matrix data by short keys and composes/applies
transforms; the six-qword records therefore carry transformed geometric data,
but their exact fields and owner remain unresolved. `FUN_1403C6030` consumes the
returned records as contact points and frame data.

This is concrete evidence of a native model-collision/contact query and a
contact-event pipeline. It is not yet evidence that these records are the
AtkParam weapon hit volumes, that `FUN_140529670`'s output is consumed here, or
that either path invokes the proven HP writer. The two chains remain separate
until a caller/data-flow edge or correlated native capture joins them.

## Next discriminating probes

1. Resolve the indirect target used by `FUN_14307E0D7` after the stack-transfer
   chain, with pinned executable bytes and PE unwind bounds. Until that computed
   dispatch target is attributed, the next consumer of the local attack record
   remains unknown.
2. Inspect `FUN_14021AB70` and callers of `FUN_140227CB0` to recover the
   collision entry fields, owner, coordinate space, and lifetime. Do not infer
   FLVER dummy resolution from the PARAM IDs alone.
3. Follow the exact payload or collision-contact consumer toward the attributed
   HP setter caller; only a correlated writer and independent hit/miss outcome
   can prove damage submission.

Static evidence only: no game launch, memory write, input injection, or runtime
capture performed in this investigation.
