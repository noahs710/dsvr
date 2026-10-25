# Native stereo boundary evidence

Evidence reference, not another execution plan. Work order is milestone D in
the [execution guide](superpowers/plans/2026-10-21-native-conversion-execution.md).
Status: **unresolved; no native two-eye replay binding is enabled**.

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
