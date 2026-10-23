# Native melee contract investigation

Evidence reference only; execution order belongs to the [sole execution guide](superpowers/plans/2026-10-21-native-conversion-execution.md), operating policy to [TESTING](TESTING.md).

Status: **unresolved**. No native damage binding is verified by this note. The observations below come from the pinned Ghidra project and do not establish runtime execution or physical-hit outcomes.

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
- The available workspace has no accepted runtime capture for a real encounter; controlled in-game evidence remains required before execution milestones B/C can pass.

Do not bind or call any candidate native signature until argument flow, structure ownership/lifetime, simulation-thread semantics, and result correlation are independently corroborated. The older `physical-contact-recovery-verified-20261020.txt` and `physical-damage-outcome-verified-20261020.txt` logs end with `Analysis rollback mismatch` errors; their recovered pseudocode is not accepted as verified primary evidence. Execution milestone B remains open.
