# Native stereo boundary evidence

Evidence reference, not another execution plan. Work order is milestone D in
the [execution guide](superpowers/plans/2026-10-21-native-conversion-execution.md).
Status: **unresolved; no native two-eye replay binding is enabled**.

## 25 October autonomous continuation: natural runnable dispatch edge

Read-only primary GhidraMCP, GUI `DSRFull`, program
`DarkSoulsRemastered.exe`, revision `1792646701740:9d9b1124:3`, unsaved false.
Independent reproducer:
`python -B build/verification/d1-pump-owner-20261025/verify_static.py` exits 0;
`static-report.json` beside it retains primary range identities and full
Capstone instruction decodes. **609 primary bytes** match the pinned EXE:
573 instruction bytes, 20 descriptor bytes, 16 dispatcher vtable bytes.

- The proposed `.CRT` discriminator is resolved: `141A18B64` belongs to
  `.rdata` (RVA `129D000`, virtual size `787C00`, characteristics `40000040`).
  Primary bytes at `141A18B60` are
  `a936280180f31b01ffffffffa0f31b0100000000`. The `FFFFFFFF` word follows,
  not precedes, the constructor RVA at `141A18B64`. This rejects the specific
  `.CRT` classification, not every possible static-initialization mechanism.
  Do not infer a second module-scope dispatcher from this slot.
- The renderer stores the constructed dispatcher at `renderer+68`, passes it
  to `140CC5E60` with receiver `*(renderer+10)` at `1411B49A5`, then calls
  `140CC5EB0` on that same receiver at `1411B49AE`. The five-byte setter is
  exactly `MOV [RCX+18],RDX; RET`. The dispatcher is therefore installed as
  that thread object's runnable, not passed as a second per-eye camera.
- `140CC5EB0` sets receiver `+C0=1`, calls `140CCE4D0(receiver+8)`, then
  tail-calls the embedded object's virtual `+20` at receiver `+30`. It is not
  the pump itself and must not be replayed as one.
- Shared thread constructor `140CC5B80` identifies `DLKR::DLThread` and calls
  `140CCE470`; the constructor observation remains decompiler-level. The
  independently pinned creator passes **`140CCE280`** to imported
  `_beginthreadex`, passes the supplied thread object as its argument, and
  uses creation flags 4. The verifier checks the actual IAT import name.
- At the thread entry, after TLS/name setup, `MOV RCX,[RBX+18]` obtains the
  runnable, and `CALL [RAX]` invokes its virtual slot zero with second argument
  `thread+78`. Primary dispatcher slot zero is pinned to **`1411BF150`**.
  On normal return a second `CALL [RAX]` is issued on the thread object with
  EDX low byte 1. Do not omit that cleanup/lifetime edge or retain its object
  after return without recovering the concrete destructor.
- The Begin-message poster is now named on this path. Renderer `1411B49C5`
  invokes dispatcher slot `+50` with R8=`&renderer+20`, R9=`&renderer+28` and
  RDX=R14 (R14's producer remains unpinned). Slot `141609900` points to
  `1411BE560`. Its full 175 bytes are independently pinned: with a dispatcher
  thread at `+18`, it allocates a `0x58` HGBeginThreadMessage and stores those
  three arguments at message `+40/+48/+50`, then queues through `140CC5E90`.
  With no thread it invokes dispatcher slot `+10` directly. The queued fields
  are pointers to the renderer's two fields, **not proof of independently
  retained refcounted values**. Renderer-field contents, the handler's exact
  retain/release operation, queue completion and teardown still require proof.

Coverage limit: this is a static runnable-dispatch chain, not live thread
identity, a complete concrete renderer thread constructor/destructor, or a
per-view render bracket. `get_functions(1411B498B)` finds no containing
function; the bounded listing/raw bytes supply that edge without DB repair.
The Begin-payload retain/release ownership, derived dispatcher type,
worker error/cancellation paths, every counter writer, retained view resources,
main-scene coverage and GPU readiness remain unresolved. D1/D2 stay open.

Next: recover the allocation/concrete construction of renderer `+10` and the
thread's return/destruction path, then trace the now-named Begin-message
payload's renderer-field producers and retain/release ownership
and derive a natural main-scene observation bracket. No game launch, replay,
camera write, database edit or save was performed.

### 26 October D-probe: renderer install window byte-pinned, +10 still open

Read-only GhidraMCP, GUI `DSRFull`, program `DarkSoulsRemastered.exe`.
Revision reported `1792646701740:fa2c6e65:30`, unsaved true (contract pinned
`...:9d9b1124:3`, false; this probe made no edit — treat epoch/modnum drift
as session-varying and re-pin before any byte claim is reused).

- `get_xrefs_to 0x1411BF380`: exactly 3 — DATA `141E36D60`, DATA
  `141A18B64`, UNCONDITIONAL_CALL `1411B498B` (no containing function,
  consistent with prior note). The install site assembly context confirms:
  `1411B498B: CALL 1411BF380` → `1411B4995: MOV [RDI+0x68],RAX`
  (dispatcher install) → `1411B4999: MOV RCX,[RDI+0x10]` /
  `1411B49A2: MOV RDX,RAX` / `1411B49A5: CALL 140CC5E60` (runnable install,
  receiver=`*(renderer+10)`) → `1411B49AA: MOV RCX,[RDI+0x10]` /
  `1411B49AE: CALL 140CC5EB0` (start/enable, same receiver) →
  `1411B49B3: MOV RCX,[RDI+0x68]` / `1411B49B7: MOV RAX,[RCX]` /
  `1411B49BA/BE: LEA R9,[RDI+0x28]` / `LEA R8,[RDI+0x20]` / `1411B49C2: MOV
  RDX,R14` / `1411B49C5: CALL [RAX+0x50]` (Begin poster, indirect).
  Raw bytes `1411B498B` (64B) decode to this exact sequence; `1411B49C5`
  (32B) opens `CALL [RAX+0x50]; MOV R8,[RDI]; ...; CALL 140CC3E10`.
  R14 (RDX for slot+0x50) producer not in window: unpinned.
- `1411BEF36: LEA RCX,[0x1416029E0]` confirmed in a push-heavy prologue;
  `read_memory 1416029E0` (64B) decodes to `HGSystemThread\0`,
  `HG_Worker\0`, `HGCommandDispatcher::Exec\0` — the thread-name string
  reference is pinned. `HGCommandDispatcher::Exec` remains a zero-xref
  naming hint, not an edge.
- `get_functions 0x141B49C5` fails (no containing function); renderer+10
  is only LOADED (`MOV RCX,[RDI+0x10]` at both `1411B4999`/`1411B49AA`),
  never stored, in this window. Preceding allocator edge: `1411B4959:
  CALL 140CC5EE0`, `1411B4966: MOV ECX,0xC0`, `1411B496B: CALL 140CC3E10`,
  null-tested before `CALL 1411BF380`. RDI (renderer) origin, RSI fallback,
  and renderer+10 ctor/dtor remain unpinned — do not infer.
- `140CC5E60` size 5 (runnable-install setter), `140CC5EB0` size 42
  (start/enable), `140CCE280` size 225 (thread entry candidate, DATA-only
  refs consistent with `_beginthreadex` function-pointer passing).
  `caller_count` undercounts raw `get_xrefs_to` totals (unanalyzed
  containing functions); signatures all `undefined (...)(void)`.
- Vtable `1416098B0` (96B) decoded: `+0x00=1411BF150`,
  `+0x10=1411BEF10`, `+0x50=1411BE560` — slot identities confirmed from
  raw bytes + DATA xrefs, not live type info. `1411BF150`/`1411BEF10` have
  only DATA xrefs (indirect entry).
- Limits: `read_memory ram:14011BF2FC` fails (jump-table type-index mapping
  not pinnable via this rail); no prototypes; no decompile performed.
- Exact next probe: (1) recover RDI/R14 producers via backward bounded
  assembly from `1411B4950` upward in 20–40 insn steps, then classify
  renderer+10 via its `[+0x10]` store sites; (2) cross-check
  `140CC5E60`/`140CC5EB0`/creator `140CCE470→140CCE280→_beginthreadex`
  bytes via Capstone; (3) then Begin-payload retain/release + derived
  vftable. D1 unresolved, D2 UNVERIFIED.

### 26 October D-probe, second window: RDI/R14 producers at ctor head, renderer+10 store pinned

Read-only GhidraMCP against the live GUI `DSRFull`
(`DarkSoulsRemastered.exe`). Only read-only rails used
(`get_assembly_context`, `get_xrefs_to`, `read_memory`,
`disassemble_bytes`, `get_metadata`); no edit, save, game launch, replay,
script, or file write. `get_metadata` carries no revision/unsaved fields
(program/arch/lang/base/counts only), so no revision re-pin is claimed this
session — treat the prior `...:fa2c6e65:30` vs `...:9d9b1124:3` drift note as
still open.

- Backward trace, three bounded windows (stop rule met: producers found):
  W1 install window (seeds `1411B4959/4966/4999/49AA/49B3/49C5`, context
  20) extends the pinned sequence back through `1411B491B: TEST RAX,RAX` /
  `491E: JZ 494B` / `4920: MOV RCX,[R14+0x38]` /
  `4924: MOV [RSP+0x28],RCX` / `4929: LEA RCX,[0x1416022D0]` /
  `4930: MOV [RSP+0x20],RCX` / `4935: MOV R9D,0x2` /
  `493B: MOV R8,[R14+0x30]` / `493F: XOR EDX,EDX` / `4941: MOV RCX,RAX` /
  `4944: CALL 140CC5B80` / `4949: JMP 494E` / `494B: MOV RAX,RSI` /
  **`494E: MOV [RDI+0x10],RAX`** (renderer+10 STORE) /
  `4952: MOV EDX,[R14+0x28]` / `4956: MOV RCX,RAX` /
  `4959: CALL 140CC5EE0`. Seed `1411B4950` itself holds no instruction
  head (expected: mid-instruction address, not a listing gap).
  W2 allocator edge (seeds `1411B47BF/47C9`, context 20):
  `47BB: MOV EBX,[R14+0x10]` / `47BF: MOV R8,[RDI]` /
  `47C2: MOV EDX,0x80` / `47C7: MOV ECX,EDX` / `47C9: CALL 140CC3E10`,
  falling through at `491B` (the `0x80` alloc return is null-tested;
  null takes the `RSI` fallback at `494B`).
  W3 function head (seeds `1411B4724/4732/474E/4760/4775/46C0`, context
  12–14): `46DC: MOV R14,RDX` / `46DF: MOV RDI,RCX` after the register
  saves; zero-init `[RCX+0x18..0xA0]=RSI(0)`; `472B: ADD RCX,0xA8`;
  `4732: LEA RAX,[0x14005C7C0]`; `474E: CALL 1410E3E00`;
  `4754: MOV [RDI+0x2C8],ESI`; `475A: MOV RAX,[R14]` /
  `475D: MOV [RDI],RAX` (vtable-ish install from the R14 chain);
  `4760: LEA RAX,[R14+0x8]` / `4764: MOV RCX,R14` /
  `4767: CMP [RAX],RSI` / `476A: CMOVNZ RCX,RAX` /
  `476E: MOV RAX,[RCX]` / `4771: MOV [RDI+0x8],RAX`;
  `4775: LEA RBP,[R14+0x50]` opening the `140CCF680/580/5E0`
  selection loop.
- Producers recovered at ctor-head level: **RDI = incoming RCX**
  (`1411B46DF`), **R14 = incoming RDX** (`1411B46DC`) of the
  `[1411B46C0,1411B4AA7)` constructor; R14 reaches `1411B49C2 MOV RDX,R14`
  with no R14 redefinition in any observed window. Upstream typing (what
  the callers pass as RDX) is still unpinned — do not infer. Coverage
  honesty: W2's after-context jumps `47C9→491B`, so byte span
  `[1411B47CE,1411B491A]` is not covered by these windows and an R14
  write there cannot be excluded from window evidence alone.
- Renderer+10 classified: the sole store in the observed listing is
  `494E`; value = `140CC5B80` return (normal path) or `RSI` (fallback
  when the `0x80` alloc is null). Name argument is `"HighGraphics"`
  (`read_memory 1416022D0`, 64B: `HighGraphics\0` + `INVALID` + wide
  `INVALID` + `HGDrawContext::~HGDrawC...`), passed at `[RSP+0x20]`
  into `140CC5B80` with `[RSP+0x28]=[R14+0x38]`, `R8=[R14+0x30]`,
  `R9D=2`. Enabled at `4959` (`140CC5EE0`), receives the dispatcher as
  its runnable at `49A5` (`140CC5E60`), started at `49AE`
  (`140CC5EB0`). Bounded contexts at the other `140CC5E60` callsites
  confirm a generic setter, not a camera slot: `1410C52D7`
  (`RCX=RAX, RDX=[RBX+0x48]`, followed by `140CC5EB0` on `[RBX+0x40]`),
  `1410E05EB` (`RCX=RAX, RDX=RBX`, then the `[RBX+0x48]`-derived object
  stored at `[RBX+0x40]`), `1411BF0FF` worker loop (`RCX=RBX, RDX=RAX`).
  Renderer+10 is the dispatcher's host thread object.
- Byte cross-check (`read_memory` + `disassemble_bytes`, noted here, no
  files): `140CC5E60` 8B read `48895118c34eb8bd` — body 5B
  `48 89 51 18 C3` (`MOV [RCX+0x18],RDX; RET`, trailing 3B belong to the
  next function). `140CC5EB0` 48B read
  `40534883ec20...31e40000cc9c` — body 42B, 11 insns
  (`PUSH RBX; SUB RSP,0x20; MOV RBX,RCX; MOV [RCX+0xC0],1; ADD RCX,8;
  CALL 140CCE4D0; MOV RAX,[RBX+0x30]; LEA RCX,[RBX+0x30]; ADD RSP,0x20;
  POP RBX; JMP [RAX+0x20]`); full hex
  `40 53 48 83 EC 20 48 8B D9 C6 81 C0 00 00 00 01 48 83 C1 08 E8 07 86
  00 00 48 8B 43 30 48 8D 4B 30 48 83 C4 20 5B 48 FF 60 20`, tail bytes
  are pad/next. Xref totals: `140CC5E60` = 5
  (`141004B8B, 1410C52D7, 1410E05EB, 1411B49A5, 1411BF0FF`);
  `140CC5EB0` = 27 (incl `1411B49AE`, `1411BF107`, DATA `141DE2814`).
- Limits: no prototypes; no decompile; install site still has no
  containing function (ctor unwind `[1411B46C0,1411B4AA7)` remains the
  boundary); `ram:14011BF2FC` mapping still unpinned (prior rail failure
  stands, not retried); windows are listing slices, not exhaustive
  coverage.
- Exact next probe: (1) Begin-payload retain/release + derived
  dispatcher vftable (message `+40/+48/+50` handlers on the `1411BE560`
  path); (2) close span `[1411B47CE,1411B491A]` via bounded disassembly
  and enumerate callers of `1411B46C0` to type the RDX/R14 source
  argument; (3) fallback if either fails: main-scene callsite bracket
  via `1411BEA27/1411C5049/1411C2F6C`. D1 unresolved, D2 UNVERIFIED.

### 26 October D-probe re-read: constructor identity + outer caller seed (addendum)

Independent re-read of the same windows (live GUI `DSRFull`, read-only;
`get_assembly_context` ±30/35/40, `read_memory`, `disassemble_bytes`,
`get_xrefs_to`, `get_functions`; no repair/save/launch/scripts/commit,
`GHIDRA_MCP_ALLOW_SCRIPTS` untouched). Confirms the second-window section
above: `1411B46DF: MOV RDI,RCX` / `1411B46DC: MOV R14,RDX`, sole +10 store
`1411B494E` (value = `140CC5B80` return or `RSI` null-alloc fallback),
`140CC5E60` 5B `48895118c3`, `140CC5EB0` 42B disassembled to 11 insns
(`CALL 140CCE4D0`, tail `JMP [RAX+0x20]`), xref totals 5 and 27 re-queried
identical (incl `1411B49A5`/`1411B49AE`, `1411BF0FF`/`1411BF107`, DATA
`141DE2814`). New deltas: `get_functions 0x1411B46C0` names
`FUN_1411b46c0` (size 270, body `[1411B46C0,1411B47CD)`, decompile params
`(RCX, RDX)` — renderer arg1, R14-source arg2); `get_xrefs_to` = 3 (DATA
`141E36364`, DATA `141A1728C`, `UNCONDITIONAL_CALL 1411B3FA5`;
`caller_count` reports 0, same undercount rail limit); `1411B3FA5: CALL
1411B46C0` context shows renderer = fresh `0x300` alloc (`3F79: MOV RSI,RCX`
→ caller RCX saved / `3F8E: CALL 140CC3E10` / `3F9F: MOV RDX,RSI` /
`3FA2: MOV RCX,RAX`, or `0` via `3FAF: MOV RDI,RBX`) and R14-source = that
frame's RCX (type still unpinned; `get_functions 0x1411B3FA5` fails — same
no-containing-function rail limit, so the outer RCX producer is one more
bounded window up). `1411B49C5` 32B =
`ff50504c8b07ba08000000b9a8070000e836f4b0ff48898424880000004885c0`
(Begin poster, then `MOV R8,[RDI]; MOV EDX,8; MOV ECX,0x7A8;
CALL 140CC3E10; TEST RAX,RAX`). `0x1411B4950` and `0x1411B48A0` hold no
instruction (mid-gap addresses, not paths). `1411B47CE`–`1411B491A` still
uncovered (listing jumps `47C9`→`491B`). Revision not re-pinned:
`get_metadata` carries no revision/unsaved fields (prior epoch/modnum
drift note stands). Exact next probe: type the outer RCX at `1411B3F79`
via bounded contexts upward from `1411B3F74`, then Begin-payload
retain/release + derived dispatcher vftable; fallback main-scene bracket
via `1411BEA27/1411C5049/1411C2F6C`. D1 unresolved, D2 UNVERIFIED.

### 26 October D-probe, third window: Begin-payload retain/release + derived vftable

Read-only GhidraMCP against the live GUI `DSRFull`
(`DarkSoulsRemastered.exe`). Rails used only: `get_assembly_context`,
`get_xrefs_to`, `read_memory`. No edit, save, game launch, replay,
script, or file write. No revision re-pin is claimed (prior
epoch/modnum drift note stands).

- Poster `1411BE560` mapping (prologue `57C MOV RSI,R9` / `57F MOV
  RBP,R8` / `582 MOV R14,RDX` / `585 MOV RDI,RCX`; caller at
  `1411B49BA BE LEA R9,[RDI+0x28]` / `LEA R8,[RDI+0x20]` /
  `49C2 MOV RDX,R14` / `49C5 CALL [RAX+0x50]`): `RDI`=dispatcher,
  `R14`=ctor-R14 value, `RBP`=`&renderer+20`, `RSI`=`&renderer+28`.
  Stores into the `0x58`-byte message (`RBX`): `5D9 [RBX],RAX` with
  `RAX=141609970` (message vftable, 96B read shows a distinct table plus
  a `HGVertexDeclarationEntitySeed` string tail),
  `5DC [RBX+0x40],R14`, `5E0 [RBX+0x48],RBP`, `5E4 [RBX+0x50],RSI`.
  Queued at `5EF CALL 140CC5E90` (`RCX=[RDI+0x18]`, `RDX=RBX`); sync
  fallback at `5F6 MOV RAX,[RCX]` / `5F9 CALL [RAX+0x10]`.
- Pump route into the handler (bounded 12-side context at `1411BF1F3`
  `CALL [RAX+0x10]`): `1CC CMP [RBX+0xbb],0` gate, `1D9 XOR ECX,ECX`,
  `1DB CALL [0x142017b3c]` (cursor import), `1E1 MOV RAX,[RBX]`,
  `1E4 MOV RCX,RBX`, `1E7 MOV R9,[RDI+0x50]` /
  `1EB MOV R8,[RDI+0x48]` / `1EF MOV RDX,[RDI+0x40]`,
  `1F3 CALL [RAX+0x10]`, `1F6 JMP 1411BF2AE` (retire). So handler
  `1411BEF10` receives `RCX`=dispatcher, `RDX`=msg+40,
  `R8`=msg+48, `R9`=msg+50.
- Handler entry (bounded 12-side context at `1411BEF10`): prologue
  pushes + `SUB RSP,0x48`, then `2A MOV RSI,R9` / `2D MOV RDI,R8` /
  `30 MOV R14,RDX` / `33 MOV RBP,RCX`. Hence `RBP`=dispatcher,
  `R14`=msg+40 value, `RDI`=msg+48 (`&renderer+20`),
  `RSI`=msg+50 (`&renderer+28`).
- Retain/release verdict per payload field:
  - msg+48 / msg+50 (`&renderer+20` / `&renderer+28`): **borrow-appearing,
    no proven retain.** Observed use is `1411BF01E MOV RCX,[RSI]` /
    `021 CALL 140CD3040` / `026 MOV RAX,[RDI]` /
    `029 MOV [RBP+0x20],RAX` / `02D MOV RAX,[RSI]` /
    `030 MOV [RBP+0x28],RAX` — plain single-deref loads straight into
    dispatcher `+0x20/+0x28` with no interposed AddRef/LOCK/copy call in
    the listing slice. `140CD3040` (takes `RCX`=`[RSI]`,
    `RDX`=`[R14+0xB0]`) is untyped, so a hidden retain inside it cannot
    be excluded — do not promote borrow to a lifetime claim. Reverse
    edge: `1411BEF66 MOV [RDI],RAX` (`RAX`=return of
    `5E CALL 14007D890`) writes back to `*(&renderer+20)`, so the
    renderer field itself is mutated by this handler; no matching
    `[RSI]` store is observed in-window.
  - msg+40 (`R14` value): **open.** It threads through untyped helpers
    (`140CCF680/5A0/5C0`, two virtual `+0x10` calls at `FDA/FE8`,
    virtual `+0x8` at `1411BEF5B CALL [RAX+0x8]` on `[R14+0x1F8]`
    with `RDX=R14`). None is typed as refcount inc/dec; do not call any
    of them retain or release.
  - Split-gap honesty: `0x1411BEF3A`, `0x1411BF00D`, `0x1411BF34B`
    return "No instruction at address" on this rail. The older note's
    `[RBP+0x20]/[RBP+0x28]` stores at `1411BF00D/017` are now observed
    at `1411BF029/030`; treat `BF00D` as a listing gap, not a second
    store site. All windows are listing slices, not exhaustive coverage;
    no prototypes, no decompile.
- Derived dispatcher vftable classification: **slot set pinned, table
  contents/address OPEN.**
  - `R15=1416098B0` pinned via `1411BF175 LEA R15,[0x1416098b0]`;
    gates at `282/295/29F/2A9 CMP RAX,R15` with `JZ 1411BF2AE`
    (skip when the object IS the base). Derived-only targets:
    `2DB MOV RCX,RBX` / `2DE CALL [RAX+0x28]`;
    `2E3 MOV RCX,RBX` / `2E6 CALL [RAX+0x30]`;
    `2EB MOV RCX,RBX` / `2EE CALL [RAX+0x40]`;
    `2F3 MOV RCX,RBX` / `2F6 CALL [RAX+0x48]`. Overridden set is
    therefore {`+0x28`, `+0x30`, `+0x40`, `+0x48`}; `+0x38`
    (`262..271 CALL [RAX+0x38]`) has a `+0xbb` gate but no base check,
    so it is shared, not derived-only.
  - Base `1416098B0` 96B read decodes to 12 slots:
    `+0x00=1411BF150`, `+0x08=14007D0B0`, `+0x10=1411BEF10`,
    `+0x18=1411BEED30`, `+0x20=1411BE710`, `+0x28=1411BE700`,
    `+0x30=1411BE6F0`, `+0x38=1411BE640`, `+0x40=1411BE630`,
    `+0x48=1411BE620`, `+0x50=1411BE560`, `+0x58=1411BE4C0`.
    Raw-pointer reads, not live type info.
  - Corroboration outside the pump: the same base-compare idiom
    (`LEA RCX,[1416098B0]` / `CMP RAX,RCX` / `JZ` skip /
    `CALL [RAX+slot]`) appears for `+0x48` at `1411BE100..10F`,
    `+0x40` at `1411BE1B0..1BF`, `+0x30` at `1411BE340..34F`,
    `+0x28` at `1411BE3F0..3FF`, each under a `[RCX+0x18]!=0`
    threaded gate.
  - Limitation: `get_xrefs_to 1416098B0` returns 11 DATA refs, all base
    install/compare sites (`1411BE1B0`, `1411BF175`, `14007D0CE/D5`,
    `1411BE100/3F0/340`, `1411BF347/34E`, `1411BF3A0/3A7`); no xref
    names a second (derived) table, and no `LEA` loads one. The derived
    table's address and its four replacement targets are therefore
    unreadable via this rail — record as OPEN, do not invent bytes.
  - Prior `ram:14011BF2FC` jump-table mapping limit stands (not
    retried in this window).
- Exact next probe: (1) queue completion/teardown — `140CC5E90`
  enqueue acknowledgement, `1411BF2AE` retire via `140CC5920`
  loop/exit conditions, teardown body `1411BF1AE` (`+0xbb` gate +
  `CALL [RAX+0x18]`), and the `140CC5600/140CC5410` wait/type edge;
  (2) main-scene bracket via `1411BEA27/1411C5049/1411C2F6C`. If (1)
  is blocked on this rail, fall back to (2) alone. D1 unresolved, D2
  UNVERIFIED.

### 26 October D-probe, fourth window: queue/teardown shape + bracket scout

Read-only GhidraMCP against the live GUI `DSRFull`
(`DarkSoulsRemastered.exe`). Rails used only: `get_assembly_context`
(12 each side), `get_functions` (fields `signature,callers`, plus
`decompiled_code` only for the two <300B bracket candidates). No edit,
save, game launch, replay, script, or file write. Revision token seen
this window `1792646701740:fa2c6e65:3542`, unsaved true — drift vs the
pinned `...:9d9b1124:3` is session-varying; no edit was made.

- Queue submit `140CC5E90` is a 5-insn tail-thunk, not a completion
  point: `E90 MOV RAX,RCX` / `E93 MOV R8,RDX` / `E96 MOV RDX,RAX` /
  `E99 ADD RCX,0x78` / `E9D JMP 140CC55A0`. Before-context is listing
  neighbours (`140CC5E60` 5B setter, RETs), not body interior. Verdict:
  enqueue routes queue+0x78 onward; acknowledgement/free unpinned here.
- Retire `1411BF2AE`: `2AE MOV RDX,RDI` / `2B1 MOV RCX,RSI` /
  `2B4 CALL 140CC5920` / `2B9 TEST BPL,BPL` /
  `2BC JNZ 1411BF180` (pump top: `MOV DL,1` / `CALL 140CC5600`) else
  restore RBX/RBP/RSI, `ADD RSP,0x20`, `POP R15/R14/RDI`, `RET`.
  Verdict: retire-then-maybe-loop; BPL is the loop/exit flag.
- Retire helper `140CC5920` head: `TEST [RDX+0x10],0x1` /
  `JNZ 592E` / `5926 MOV RCX,RDX` / `5929 JMP 140CC5470` vs
  `592E LEA RCX,[RDX+0x18]` / `5932 MOV DL,1` /
  `5934 JMP 140CCBA40`. Verdict: branch on message+0x10 bit0; which
  path frees the Begin message is OPEN (targets unclassified here).
- Teardown body `1411BF1AE` re-confirmed: `1AE CMP [RBX+0xbb],0` /
  `1B5 JNZ 2AE` / `1BB MOV RAX,[RBX]` / `1BE MOV RCX,RBX` /
  `1C1 CALL [RAX+0x18]` / `1C4 XOR BPL,BPL` / `1C7 JMP 2AE`.
  Verdict: pump owns teardown ordering — +0xbb gate, dispatcher
  virtual +0x18, clear BPL (exit, no re-loop), then retire.
- Wait `140CC5600` prologue: `MOV [RSP+8],RBX` / `PUSH RDI` /
  `SUB RSP,0x20` / `MOV RDI,RCX` / `MOVZX EBX,DL` /
  `MOV RCX,[RCX]` / `OR EDX,-1` / `ADD RCX,0x30` /
  `MOV RAX,[RCX]` / `CALL [RAX+0x10]` / then `MOV RCX,[RDI]` /
  `LEA RAX,[RCX+0xB0]` / `TEST BL,BL` .... Pump calls it with DL=1
  (`1411BF180 MOV DL,1` / `185 CALL 140CC5600`). Verdict: waker is
  `[queue]+0x30`-object virtual +0x10 with EDX forced -1; that
  object's identity and event-vs-poll semantics are OPEN.
- Type `140CC5410`: `MOV EAX,[RCX+0x14]` / `RET`. Pump uses
  `MOV RCX,RAX` / `MOV RDI,RAX` / `CALL 5410` / `CMP EAX,7` /
  `JA 2AE` / `MOVSXD RCX,EAX` /
  `MOV EDX,[R14+RCX*4+0x11BF2FC]` / `ADD RDX,R14` / `JMP RDX`.
  Verdict: type is dword [wait-result+0x14], range >7 retires; the
  index-to-body mapping stays unpinned (prior `ram:14011BF2FC` limit).
- Completion-signal shape, honest form: waiter wakes inside
  `140CC5600`'s indirect call (object OPEN); message type is read by
  `140CC5410`; each dispatched body converges on `1411BF2AE` retire
  via `140CC5920` (free path OPEN); teardown ordering is pump-owned
  (`1AE` body, BPL=0 exits). No proven "Begin message freed here"
  address is claimed.
- Bracket scout (`signature,callers`; decompile only <300B):
  `1411BEA27` = `ProcessDantelionRenderFrame` @`1411BE710`, size
  1547, `undefined (void)`, callers [] — NOT decompiled per rule.
  `1411C5049` = `FUN_1411c4fa0` @`1411C4FA0`, size 222,
  `undefined (void)`, callers [] — decompiled: scratch via virtual
  `+0x48`, stack-local executant (`HGDrawPlanExecutant` vftable),
  single `FUN_1411cd8a0()` consumer call, halves stored, freed via
  `140CC3D20`. `1411C2F6C` = `FUN_1411c2f50` @`1411C2F50`, size 49,
  `undefined (void)`, callers [] — decompiled: thin forwarder
  `1411CD8A0(*(p+8), RDX-in, *(p+0x18), *(p+0x20), *(p+0x2C))`,
  halves into `p+0x28/+0x2C`. All three report `caller_count 0` —
  same rail undercount limit as before, not proof of unused.
- Bracket recommendation: primary future observation bracket is
  `FUN_1411c2f50` — smallest (49B), no allocation, explicit 5-arg
  tuple plus result capture, least side effect. Secondary is
  `FUN_1411c4fa0` (222B) for allocation lifetime
  (alloc virtual +0x48 ... free `140CC3D20`), but its storage is
  stack-local so retention past return is unsupported.
  `ProcessDantelionRenderFrame` (1547B) is explicitly NOT a bracket:
  cursor/sync/presentation side effects, do-not-run-twice.
- Limits: 12-side listing slices only; neighbour interleaving at
  `140CC5E90/5600/5410/5920`; no prototypes; no `read_memory` this
  window; no decompile of the 1547B body; jump-table mapping still
  unpinned; upstream caller typing still OPEN.
- Exact next probe: natural main-scene trace when a live observation
  window opens — observe (never replay) `FUN_1411c2f50` during natural
  play, with `FUN_1411c4fa0` as the allocation-lifetime bracket;
  record camera handle, thread, scratch begin/end and retirement, and
  correlate with camera/constant-upload observations. A wait loop
  alone cannot certify coverage. D1 unresolved, D2 UNVERIFIED.

## Identity and scope, 22 October 2026

- Shipped executable SHA-256:
  `a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`.
- Primary `DSRFull` / `DarkSoulsRemastered.exe`, base `0x140000000`, Ghidra
  revision `1792646701740:aeb52525:2664`, unsaved changes false. Read-only MCP
  queries; no function repair, prototype edits or database saves.
- REA's Windows native admission was unavailable for the original receipt
  (see melee evidence); current availability is recorded in the continuation.
  These observations have no REA Evidence IDs. Ghidra was cross-checked against
  bounded Capstone decoding of the hash-verified executable and PE unwind data.
- Local-only instruction receipt:
  `C:\Tools\DarkSoulsRemaster_De\combat-evidence\native-contracts-20261022T211921Z.json`,
  SHA-256 `b59ab7ce0e9cbbb1e9370e7acdde082814e6d4e96d975631874f6adc051ef324`.
  It contains 13 bounded ranges, including the complete unwind range below;
  linear decoding is not a complete control-flow or execution proof.

## Observations that change the next experiment

| Boundary | Observed fact | Consequence / uncertainty |
|---|---|---|
| `ProcessDantelionRenderFrame`, `0x1411BE710` | Advances command cursors, dispatches entities and calls the lower consumer; also owns synchronization/presentation work. | Do not run this twice as an eye loop. The render argument is integer EDX, not a simulation timestep. |
| Consumer `0x1411CD8A0` | PE unwind range is `[0x1411CD8A0, 0x1411CE029)`; final `RET` is at `0x1411CE028`. Ghidra's listed body stops at `0x1411CDDDE`. | Decompiled `void` and missing fallthroughs are not a usable ABI. There is substantial omitted normal-path code. |
| Entry / lexing | At `0x1411CD8E6`, RCX is the local lexer, RDX the incoming renderer/owner, and R8D is preserved from the consumer's third argument. `0x1411CE460` uses that value as an entity handle. R9 at consumer entry is the scratch owner. | Preserve handle identity, not a guessed eye index. The second argument's full role is not established. |
| Scratch | Aligns `scratch+0x10` to 16, reserves `0x130` bytes for a parser, then advances the cursor to `scratch+0x18` while constructing an item buffer (`0x1411CDA35..44`). | Reusing the same scratch cursor for eye two is invalid without a retained/rebuilt ownership contract. |
| Context construction | The calls to `0x140CC3E10` at `0x1411CDC36` and `0x1411CDC79` have real fallthroughs. They test RAX and call `0x1411B80C0` to construct a context with allocation size `0x2300`, alignment `0x80`. | `0x140CC3E10` tail-dispatches to the allocator's virtual `+0x50`; a decompiler no-return warning must not erase construction. Do not reuse a context pointer after cleanup. |
| Draw execution | Consumer invokes parser/item virtual operations; the already traced camera/draw path is below this layer. | No proof yet that replaying all these operations is view-only or that one camera covers the full main scene. |
| Hidden cleanup | After `0x1411CDDDA`'s call to `0x140CC29C0`, instructions continue with context virtual cleanup/free, then poll two entries at `*(owner+0x68)+0x40/+0x48`, testing each entry's `+0x20` until zero (`0x1411CDE10..46`). `0x140CC3F80` is atomic compare-exchange. | This is a concrete completion-counter lead, not proof of GPU completion or coverage of every worker. Find their producer/increment/decrement before restoring eye edits. |
| Final lifetime | After polling, invokes item callbacks, frees container storage, destroys parser state and returns a value in RAX (tail through `0x1411CE028`). Low result is obtained from parser virtual `+0x10` / base parser field `+0x120`. | It is not a void callback. Counter polling precedes further destruction; neither "after draw" nor a selected leaf callback is automatically a retention boundary. Result semantics remain open. |
| Other callers | Natural callsites are `0x1411BEA27`, `0x1411C5049`, `0x1411C2F6C`. Last wrapper forwards object `+8`, incoming RDX, object `+0x18`, `+0x20`, `+0x2C`; stores RAX halves into `+0x28/+0x2C`. | Inspect the natural task/executant owner. No direct caller list does not mean unused: the wrappers have data/vtable references. |

The hidden tail begins at `0x1411CDDDF` (not a new callable function).
`0x140CC29C0` has a normal returning instruction path; its return is consumed.
Do not patch the primary database's no-return flags just to improve pseudocode.

## Planning refresh, 23 October 2026

The current user-directed priority is D1/D2 stereo feasibility before native
pose transfer and the physical-hit contract. The executable was freshly hashed
and matches the identity above. REA `open_binary` with provider `ghidra`
returned `provider_unavailable` / `unsupported_host`: Windows Job Object,
private DACL and reparse-safe admission proofs remain unavailable. No REA
Evidence ID is claimed; do not bypass that admission gate.

The documented fallback was rechecked with:

```powershell
pwsh -NoProfile -File tools/ghidra/query.ps1 -Address 0x1411C2F50 -Script DescribeFunction.java
```

Exit 0 against the primary `analysis-full/DSRFull` project with
`-readOnly -noanalysis`. The wrapper's listed instructions confirm the
argument/result transport already described above. Its reported references
are **DATA**, not verified runtime callers: `0x141E370D8`, `0x142BBA7FC` and
`0x142BBA803`. The first slot is the next bounded owner/table lead, not a
proven vtable, task type or live main-scene invocation. These observations add
no counter-owner, GPU-readiness or two-view feasibility proof.

Local-only raw receipt:
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\stereo-plan-wrapper-20261023T020151Z.txt`,
SHA-256 `c947da2dad1712b909dc1d1c58eeaeeff0709c8f0b07e77c0bc2cbd47d5e9f55`.
No function/prototype repair or database-save command was issued. No native
execution, replay, deployment or headset qualification occurred.

## Executor-reference classification, 22 October 2026 continuation

This continuation supersedes the proposed table probe above. The shipped EXE
was rehashed and matches the pinned identity. Primary queries used
`-readOnly -noanalysis`; PE parsing and Capstone 5.0.7 independently checked
the bounded bytes. No primary database repair/save or native execution occurred.

| Lead | Static observation | Meaning and limit |
|---|---|---|
| `0x141E370D8` | In `.pdata`: the 12-byte `RUNTIME_FUNCTION` entry is `{BeginAddress=0x11C2F50, EndAddress=0x11C2F81, UnwindData=0x1A192E0}`. | Unwind metadata, **not a task/vtable slot**. References to the slot and preceding aligned address returned zero; this does not establish that the wrapper is unused. |
| `0x142BBA7FC` / `0x142BBA803` | In the second executable `.text` region. `LEA RBP,[RIP-0x19F78B3]` forms `0x1411C2F50`; `XCHG [RSP],RBP` stores it on the stack. The following stack exchange/RET transfers first to `0x1428013C7`, which continues via `0x1407E8363 -> 0x142323D03`. | Two references within **one stack-mediated control-transfer sequence**, not two ordinary calls or table entries. The complete incoming ABI, eventual wrapper invocation and runtime use remain unknown. Ghidra has no containing function at these two reference addresses. |
| Executant type | Native RTTI `.?AVHGDrawPlanExecutant@DLHG@@`, vftable `0x141609898`, locator `0x1416E4958`. `0x14007D440` only installs this vptr. Deleting destructor `0x14007D450` reinstalls it and conditionally releases `0x30` bytes. | A concrete native executant type and size; this alone does not make it an enqueued worker task. The vtable does **not** contain wrapper `0x1411C2F50`. |
| Direct consumer caller `0x1411C4FA0` | Obtains scratch through allocator virtual slot `+0x48` reached from entity `+8`, constructs descriptor `{base,size,cursor,end}` at `RSP+0x40`, and an executant at `RSP+0x60`. Executant fields: `+8=*(entity+8)`, `+0x10=*(owner+0x28)`, `+0x18=*(entity+0x10)`, `+0x20=&scratch`, `+0x28=0`, `+0x2C=0x10`. Calls the consumer at `0x1411C5049`, stores returned halves, then calls `0x140CC3D20` on the allocation. | One statically demonstrated stack-local lifetime. Retaining this executant/scratch past the caller or reusing consumed storage is unsupported. The allocation-release helper's implementation and exceptional/worker lifetimes are not established here. |
| Entity owner | Native RTTI `.?AVHGDrawPlanEntity@DLHG@@`, vftable `0x14160EA80`, locator `0x1416E88C0`; slot `+0x50` at `0x14160EAD0` points to `0x1411C4FA0`. Constructor `0x1411C5320` stores its second argument at entity `+8` and third argument at `+0x10`. Destructor `0x1411C5280` calls container cleanup `0x1411C5090`. | Names the entity-to-executant edge. The constructor's renderer input, actual main-scene dispatch and counter owners remain unresolved. A separate vptr installation exists in unassigned code at `0x1411BFED3/DA`; inspect its surrounding instructions next. |

The returned `+0x28/+0x2C` halves are observed stores, not interpreted completion
or draw-count semantics. The two polled objects at `*(owner+0x68)+0x40/+0x48`
still need separate allocation/reset/increment/decrement/cancellation/destruction
records. Similar wait helper `0x1411C2E80` polls its receiver's `+0x20`; no
identity edge to those two objects has been proved. Neither wait proves GPU
readiness. D1b is partial, D1 remains unresolved, and D2 remains UNVERIFIED.

Tool availability changed from the earlier receipt: this session's REA MCP
`open_binary(provider_id="ghidra")` returned `provider_unavailable` because
its Ghidra/Java environment is not configured. A separate
`npx -y rea-agents@latest doctor` (CLI 6.7.0, exit 1 overall for registration/
optional-provider problems) reported Ghidra 12.1.4 and Windows native admission
healthy. Do not repeat the old Windows-admission diagnosis as current fact.
No setup/configuration changes, duplicate import or REA session were created.
These observations have no REA Evidence IDs.

Local raw receipts remain outside distributable sources in
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\stereo-*-20261022.*`.
The exact receipt paths/SHA-256 values and independent static assertions are in
`build/verification/d1-executor-20261022/static-checks.json`; entry provenance
and the saved mixed diff are beside it. Relevant commands, all query exits 0 (the headless `tools/ghidra/query.ps1` wrapper was removed on 24 October 2026; these lines are retained as receipts of how each observation was produced, not as available routes):

```powershell
& ./tools/ghidra/query.ps1 -Address @('0x141E370D8','0x141E370D0','0x142BBA7FC','0x142BBA803') -Script ListReferences.java
& ./tools/ghidra/query.ps1 -Address @('0x1411C4FA0','0x1411C2E80') -Script DescribeFunction.java
& ./tools/ghidra/query.ps1 -Address @('0x141609890','0x50') -Script DumpPointerRange.java
& ./tools/ghidra/query.ps1 -Address @('0x14160EA40','0x98') -Script DumpPointerRange.java
& ./tools/ghidra/query.ps1 -Address @('0x1411C5280','0x1411C5320') -Script DumpFunctions.java
```

The separate query requesting `0x1407E8363` explicitly reported no containing
function; exit 0 did not turn that missing analysis into recovered source.
No C++/tests/config/package/install/save changes or game launch. No fresh
build/CTest or native/headset result is claimed for this static/doc work unit.

## The shortest useful next probe

24 October continuation supersedes repeating the inline-construction probe.
Read-only stdio MCP queries against the live `DSRFull` GUI and independent
Capstone decoding of the pinned EXE recovered these omitted edges. Raw queries,
PE unwind bounds and decode are retained in
`build/verification/d1-owner-20261024/`; no Ghidra repair/save or game execution.

| Edge | Static result and limit |
|---|---|
| Inline factory `1411BFE80` | Unwind `[1411BFE80,1411BFF41)`. Allocates `0x40` bytes with alignment `0x10` from `*RDX`; omitted `1411BFEB4` copies RAX to RBX. Stores incoming RDX in entity `+8`, incoming R8D in `+0x10`, installs `HGDrawPlanEntity`, returns the allocation. Xrefs include DATA `141605E48`; its dispatch/descriptor role still needs classification. |
| Renderer to dispatcher | Complete constructor unwind `[1411B46C0,1411B4AA7)` includes allocation `0xC0`, constructor `1411BF380` and store of its result to renderer `+0x68` at `1411B4995`. The dispatcher receives that renderer in `+8`; the later virtual `+0x50` initialization call is at `1411B49C5`. |
| Two worker slots | Initialization `[1411BEF10,1411BF147)` loops twice from dispatcher `+0x50`. It allocates one `0x48`-byte `HGCommandWorker` per successful queue, calls `1411C2CE0` at `1411BF0E6`, stores workers at dispatcher `+0x40/+0x48` (`1411BF0F0`) and their queues at `+0x50/+0x58`. These are the two slots polled by the consumer. The slot-to-initializer table edge is pinned below (25 October); still no live identity claim. |
| Counter producer | `1411C2CE0` initializes worker `+0x20` to zero. `1411C2A20` constructs an `HGExecDrawCommandsThreadMessage` carrying entity/context at `+0x40/+0x48`, increments worker `+0x20` through `140CC3FE0` at `1411C2ABA`, then enqueues through `140CC5E70`. Synchronous mode dispatches virtual `+0x10` instead. |
| Hidden retirement | Full unwind `[1411C2AF0,1411C2B9B)` frees the draw context through its allocator, then calls `140CC4010` on worker `+0x20` at `1411C2B7E` when `worker+0x38` is enabled. Worker loop `[1411C2BB0,1411C2CB8)` has the corresponding call at `1411C2C88` before message retirement `140CC5920`. Both paths were hidden by no-return pseudocode. The helper's pinned bytes are `OR EAX,-1; LOCK XADD [RCX],EAX; RET`: atomic decrement returning the prior count. Complete cancellation/indirect paths remain to qualify. |

Dispatcher vftable is `1416098B0`. Its `+0x50` slot is **`1411BE560`**, which
either dispatches virtual `+0x10` synchronously or sends `HGBeginThreadMessage`
through `140CC5E90`. Do not call that slot a direct edge to `1411BEF10`.

### The receiver edge is closed, 25 October 2026

Read-only `get_xrefs_to`/`read_memory` on the live `DSRFull` GUI plus independent
Capstone decoding of the pinned bytes. No database repair, no game execution.

`1411BEF10` is dispatcher vftable **slot `+0x10`**. Its only caller is the
dispatcher's own message pump, vftable **slot `+0x00` = `1411BF150`**, body
`[1411BF150, ~1411BF2F8)`. The pump waits with `140CC5600(queue, 1)`, reads the
type with `140CC5410`, rejects types `>7`, then `MOV EDX,[R14+RCX*4+0x11BF2FC];
ADD RDX,R14; JMP RDX` over an eight-entry image-relative table. Byte-pinned
bodies (the table itself is not readable through the MCP memory rail — see the
limit at the end):

| Body | What it does |
|---|---|
| `1411BF1AE` | gate `dispatcher+0xbb == 0`; `CALL [RAX+0x18]` (teardown); `XOR BPL,BPL`; retire and return |
| `1411BF1CC` | gate `+0xbb`; `XOR ECX,ECX; CALL [RIP+0xE5895B]` (a user32 cursor import); then `MOV R9,[msg+0x50]`, `MOV R8,[msg+0x48]`, `MOV RDX,[msg+0x40]`, **`CALL [RAX+0x10]`** — the only route into `1411BEF10` |
| `1411BF1FB` | gate `+0xbb`; conditional cursor show on `+0xb8`/`+0xb9` (`CALL [RIP+0xE58915]`), set `+0xb9=1`, `CALL [RAX+0x20]` with `[msg+0x40]` |
| `1411BF23C` | conditional cursor hide (`CALL [RIP+0xE588EF]`), clear `+0xb9`, same slot `+0x20` |
| `1411BF262` | gate `+0xbb`; `CALL [RAX+0x38]` |
| `1411BF276`/`289`/`29C`/`2A6` | gate `+0xbb`, then `CMP [dispatcher],R15` against `1416098B0` and skip when the object **is** the base dispatcher — those cases require a derived vftable |

Every body converges on `1411BF2AE` (retire through `140CC5920`, loop unless the
flag was cleared); only the first body ends the pump. `1411BF150` has exactly
two DATA xrefs — the vftable slot itself and its unwind record at `141E36D48` —
so it is entered only as `CALL [dispatcher]`, never by a direct call.

Consequences that change D1c, not just D1b:

- The worker-creating handler is a side effect of a pump that also mutates Win32
  cursor state and dispatches window-shaped derived handlers. Replaying the
  consumer would re-enter that pump. This is the concrete reason D1c keeps the
  "trace backward to the nearest earlier per-view producer" fallback instead of
  a replay hook.
- The handler takes the dispatcher's `+0x20/+0x28` refcounted objects **from the
  message payload** (`MOV [RBP+0x20],RAX` / `MOV [RBP+0x28],RAX` at
  `1411BF00D`/`1411BF017`), so their lifetime belongs to whoever posts that
  message. D2a cannot claim retention without naming the poster.

Worker creation is now byte-pinned including its error paths. In
`[1411BEF10,1411BF147)` the loop at `1411BF050` runs `R15=2` times with
`RSI = dispatcher+0x50` stepping `+8`: allocate a `0xC8`-byte queue
(`140CC3E10`, align 8, allocator `*R14`), initialize it with
`140CC5B80(queue, 0, 0x4000, 0xC, 0, R13)` where R13 is a name pointer in the
string block at `1416029E0`, enable it with
`140CC5EE0(queue, 4)`, allocate a `0x48`-byte worker, initialize it with
`1411C2CE0(worker, dispatcher+8, dispatcher+0x10, queue)`, store it at
`+0x40/+0x48`, then `140CC5E60`/`140CC5EB0` and store the queue at `+0x50/+0x58`.
A failed queue allocation jumps to `1411BF128` and **continues to the next
worker**, leaving that slot pair null; a failed worker allocation frees the queue
through virtual `+8` and the owner's virtual `+0x68`. Neither failure returns an
error, so a partially initialized dispatcher is observable, not fatal.

Ghidra still lists `1411BEF10` as ending at `1411BEF8F` because `140CC3E10` is
flagged no-return; the pinned bytes show the real fallthrough at `1411BEF90` and
a full epilogue with `RET` at `1411BF146`. Do not repair the primary database to
"fix" this — decode the byte window.

Probe limit to record: `read_memory` cannot reach the jump table at
`ram:14011BF2FC` (nor `14011BF2F0`) in this program view, so the exact
type-index-to-body mapping is **not** pinned; the bodies above are listed in
listing order, not type order. Recovering it needs a defined-block read or the
GUI's own switch annotation — do not guess the mapping into a claim.

Two follow-up observations from the same read-only pass, both partial:

- The string block at `1416029E0` holds `HGSystemThread`, `HG_Worker` and
  `HGCommandDispatcher::Exec`. The handler loads the first through
  `LEA RCX,[0x1416029E0]` at `1411BEF36` before `CALL 140E44D20`, so the thread
  name is pinned. `HGCommandDispatcher::Exec` has **zero** xrefs in the current
  program, so it is a naming hint for the pump, not a pinned edge.
- Constructor `1411BF380` has exactly three xrefs: the renderer's
  `UNCONDITIONAL_CALL` at `1411B498B`, its unwind record at `141E36D60`, and one
  DATA slot at `141A18B64`. That slot holds RVA `0x11BF380` with `0xFFFFFFFF`
  immediately before it and RVA `0x11BF3A0` (which is **inside** the
  constructor, not a function entry) after it. A CRT static-initialization
  descriptor for a second, module-scope dispatcher is the leading reading; a
  thunk/registration table is the alternative. Not decided — the discriminating
  probe is which section contains `141A18B64`.

Next probe: the pump's only entry is vftable slot `+0x00`, so its thread owner is
not one xref away. Enumerate consumers of renderer `+0x68` (constructor
`1411BF380` xrefs) and the `HGSystemThread` name reference at `1411BEF36` to
find the `CALL [dispatcher]` site, then classify the derived dispatcher vftable
that the last four bodies require. Until that owner and the full per-worker
`+0x20` writer set are named, D1 stays unresolved and D2 stays UNVERIFIED.
Known increments/retirement sites do not prove every possible counter writer.
The older inline probe below remains a reproducible observation, not the next
unmet edge.

Decode the inline `HGDrawPlanEntity` vptr installation at `0x1411BFED3/DA`
and trace the renderer value stored in entity `+8`; the out-of-line constructor
is `0x1411C5320`. Pull the bounded listing through the Ghidra MCP rail against
the live GUI holding `DSRFull`; if a byte window is absent, cross-check pinned
bytes instead of repairing the database. `0x141E370D8` is already classified
unwind metadata and is no longer a table-owner lead.

```text
mcp__GhidraMCP__get_assembly_context(xref_sources="0x1411BFED3,0x1411BFEDA", context_instructions=12)
```

Run on 24 October 2026 this returns the inline installation directly:
`1411bfeaf: CALL 0x140cc3e10` (the allocator helper), then
`1411bfed3: LEA RAX,[0x14160ea80]` and `1411bfeda: MOV qword ptr [RBX],RAX`,
followed by `1411bfef7: CALL qword ptr [RAX + 0x18]`. `0x14160ea80` is the
`HGDrawPlanEntity` vftable already recorded above, so the inline store is the
vptr installation, not an unrelated table read. This is a static listing
observation only: it names no counter owner and proves no live dispatch.

Follow that owner to the two polled objects and identify who initializes,
increments and decrements their counters and how parser tasks reference the
scratch/item/context storage. The unanalysed stack-transfer path is another
bounded static lead; it is not an approved callable ABI or replay hook.
Correlate one natural main-scene invocation with existing camera/constant-upload
observations before selecting any replay hook. Record camera handle, thread,
scratch begin/end and retirement; a wait loop alone cannot certify coverage.

For a future protected D2 experiment, the discriminating observation is two
different consumed eye views with unchanged simulation/event/history/Present
counters, complete worker and GPU readiness, and retained valid resources until
both eye copies complete. Existing `tools/native_stereo.py` only validates
**synthetic claims**; it cannot provide those observations or authorize replay.

Still unknown: active main-scene plan/camera selection, camera history ownership,
all worker lifetimes, GPU completion, view-dependent culling/resources and a
render-only replay contract. No native run or headset qualification occurred.
