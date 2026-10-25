# DSVR native research index

Current index: 22 October 2026. [Authority](README.md#authority-and-maintenance). This file catalogs evidence sources; it is not an execution plan. Use the [execution guide](docs/superpowers/plans/2026-10-21-native-conversion-execution.md) for all task order and route selection.

## Current evidence owners

- [Melee contract](docs/NATIVE_MELEE_CONTRACT.md): exact contact/health/event observations, split-continuation limits, unresolved ABI/ownership/outcome questions. No current native damage acceptance.
- [Arm evidence](docs/ARM_IK_EVIDENCE.md): captured model-space bone conventions, twist/shear findings, fixture provenance and current revalidation needs. Not an unimplemented-code claim.
- [Stereo contract](docs/NATIVE_STEREO_CONTRACT.md): recovered consumer tail, scratch/context cleanup, CPU-counter leads and unresolved replay ownership. No enabled eye-render binding.
- [Engineering](docs/ENGINEERING.md): invariants for camera, body, movement, stereo, interaction and lifecycle ownership.
- [Handoff](MODLOG.md): current checkout and next unmet execution gate.

## Primary static workspace

Primary project: `C:\Tools\DarkSoulsRemaster_De\analysis-full\DSRFull.gpr`, loaded program `DarkSoulsRemastered.exe`. Keep primary analysis read-only. `analysis-full-attack` is a repair workspace, not the primary source. Existing installation references are Ghidra 12.1.4 and JDK 21 under `C:\Tools\DarkSoulsRemaster_De`; verify actual paths/environment before tools run.

Routine native queries go through the `ghidra-mcp` MCP server configured in `%LOCALAPPDATA%\Empryo\config.json` (`mcpServers`, command `C:\Tools\DarkSoulsRemaster_De\ghidra-mcp\.venv\Scripts\bridge-mcp-ghidra.exe --no-lazy`, `cwd` at the ghidra-mcp checkout, `timeout: 1800000`). It auto-connects over TCP to the live Ghidra GUI bridge (`http://127.0.0.1:8089`, project `DSRFull`) and exposes function/string/decompiler tools directly; no external scripts are needed, but the Ghidra GUI with the bridge must be running. `--no-lazy` is deliberate: lazy mode advertises 101 tools and leaves the `xref`/`comment` groups unloaded, so `get_xrefs_to` and `get_assembly_context` answer `not_loaded`; `--no-lazy` advertises 239 and every tool is callable at once. `run_script_inline` stays disabled (`GHIDRA_MCP_ALLOW_SCRIPTS` unset). The headless `tools/ghidra/query.ps1` wrapper, `tools/ghidra/repair.ps1` and the `combat-evidence/melee-20261024/mcp_read.py` shim were removed on 24 October 2026 and must not be reintroduced while the GUI holds `DSRFull`; the `.java` probes in `tools/ghidra` remain as receipts of how earlier observations were produced. Verified 24 October 2026: bridge live (261,311 functions), seam functions `FUN_140357a20`/`FUN_140207f10`/`FUN_1408e06b0` confirmed by address and xref count through MCP, matching prior REST grounding.

### Full-utilization playbook (ghidra-mcp v7.0.0, 209 catalog tools)

Per the [upstream README](https://github.com/bethington/ghidra-mcp):

- Lazy loading is the default: only `listing,function,program` groups register on connect. Pull in the rest on demand with `load_tool_group({group})` — `xref` (xrefs, call graphs), `analysis` (P-code dataflow, completeness), `emulation`, `documentation` (function hashes, cross-version diff), `datatype`, `comment`, `symbol`. The tool catalog refreshes after a group load (confirmed in this harness).
- 7.0.0 tool consolidation: old `decompile_function` is `get_functions(fields=decompiled_code)`; `list_functions`/`search_functions` are `find_functions`; callers/callees come from `get_functions(fields=callers,callees)`. Ignore any pre-7.0 prompt or script that calls the old names.
- Grounding set this project uses read-only: `get_metadata`, `find_functions`, `get_functions(fields=signature,decompiled_code,callers,callees)`, `get_xrefs_to`, `get_function_call_graph` (xref group), `search_strings`, `list_program_items(kind=...)`. Deeper leads available: `analyze_dataflow` (P-code forward/backward value tracing for field semantics), `emulate_function` (isolated function execution), `get_function_hash`/`diff_functions` (patched-game-version comparison), `batch_string_anchor_report` (source-file strings to FUN_* map).
- Write-side tools (`rename_function`, `set_function_prototype`, `create_struct`/`apply_data_type`, `set_comment`/`apply_documentation`, `save_program`, function tags, property maps, bookmarks) are full-featured with convention enforcement and atomic batch transactions, but stay out of `DSRFull` under the read-only rule above. Annotate only in the repair workspace, and never rename grounded `FUN_*` targets — recorded evidence links to their addresses and auto-generated names.
- Health/schema: `check_connection` and `mcp_health` tools; the authoritative live tool list is `GET http://127.0.0.1:8089/mcp/schema`.
- While only `DarkSoulsRemastered.exe` is open, current-program fallback routing is safe. If a second program (e.g. a mod DLL) is opened in the same Ghidra, set `GHIDRA_MCP_REQUIRE_PROGRAM_SELECTORS=1` on the bridge and always pass `program=`.

First full-utilization findings (22 October 2026, MCP `get_xrefs_to`/`get_functions`):

- The `SkeletonParam` registry string (`0x141372b80`) has exactly two xrefs, both in `FUN_140531bc0` — the parameter-registration entry to read first for F4 skeleton/foot-IK work.
- The root/palette feed `FUN_140207f10` has exactly two listed callers: the grounded pose seam `FUN_140357a20` and `FUN_1407051d0`. The latter was examined in the native-contract continuation: it also invokes attachment propagation (`140207510/1402079b0`). Its runtime actor coverage remains unqualified; do not assume the player seam is the sole model feed. See ARM_IK_EVIDENCE.

Use working REA capabilities first. Historical Windows native-provider availability failed its security/ownership admission; do not bypass the gate. Check current availability once; do not retry an identical unavailable provider indefinitely. A .gpr launcher is not a standalone binary. Keep raw decompilation and extracted game data outside distributable mod sources.

## External reference material (permanent)

- [Souls Modding wiki — Dark Souls: Remastered reference material](https://soulsmodding.com/doku.php?id=ds1r-refmat:main): user-recommended permanent community reference, re-read **24 October 2026**. Useful for parameter names, attack data, animation conventions, parts/maps and tool vocabulary. **The DSR landing page deliberately links many DS1 pages**: do not mistake a shared reference for a verified DSR x64 object layout or callable ABI.

  Targeted entry points for future IK/combat work:

  - [AtkParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:atkparam): sphere/capsule radii, paired `hit*_DmyPoly1` / `hit*_DmyPoly2` references, `hitSourceType`, guard/stamina modifiers and guard/invulnerability-related flags. These are promising *data-schema clues* for the native endpoint-key investigation, not proof that event `+0xF0/+0xF4` has the same layout or that endpoint distance equals visible blade length.
  - [BehaviorParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:behaviorparam): attack/projectile behavior selection; consult while tracing legitimate context creation, not as permission to manufacture an event.
  - [EquipParamWeapon](https://soulsmodding.com/doku.php?id=ds1-refmat:param:equipparamweapon): equipment/weapon-motion vocabulary for correlating real loadouts and one-/two-hand observations. Match actual equipped PARAM IDs and extracted rows before applying a profile.
  - [Attack Animation Schema](https://soulsmodding.com/doku.php?id=ds1-refmat:attack-animation-schema): community conventions for category/hand/action animation IDs. Useful to label natural animation/context evidence; **not** a route to gesture-triggered canned melee.
  - [SkeletonParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:skeletonparam) and [MoveParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:moveparam): leads for native foot IK and movement configuration, not proof of render-only avatar scaling or safe collider writes.

  Evidence ladder: wiki clue -> exact DSR parameter schema/extracted row -> pinned native consumer/ownership -> controlled native comparison. The older 22 October registry-string observations (`SkeletonParam` `141372B80`, `MoveParam` `141372A70`, `AtkParam_Pc` `141372910`, `EquipParamWeapon` `1413727E0`, `SpEffectParam` `1413729C8`) establish names only, **not every wiki field/type/offset**. Keep raw game data/decompilation outside distributable sources. This reference adds no competing execution plan or permission to guess native calls.

## Retained research and its limits

The complete [pre-consolidation research record](docs/history/2026-10-21-before-consolidation/MODDING_PLAN.md.txt) retains addresses, source links, capture filenames and failed hypotheses. Consult only a targeted historical finding after identifying the current missing fact. Historical proposed routes, next actions, account setup and status claims are not instructions. Historical source URLs were checked at their recorded dates, not reverified by this cleanup.

Key leads to avoid rediscovering:

- Camera commands 0x3B/0x3E copy projection/transform data; CPU camera entity selection, draw-context preparation, constant upload and retained GPU captures exist. Those do not by themselves prove a simulation-free eye-render bracket or safe worker/resource lifetime.
- Six-view cubemap and legacy stereo code are multiview leads, not arbitrary OpenXR eye-render APIs. View history/presentation side effects must be isolated.
- Native post-animation captures identified an 80-bone player layout and model-space pose-copy order. Final skinning/attachment behavior and current rendered alignment require candidate-specific evidence.
- Native UI/blur/face filtering and XInput recovery have source/static observations and historical runs; their current coverage/defaults must be read from current code and qualified on the tested package.
- Generic AI/event builders and CreateDamage_NoCollision registration names are not proof of melee damage ABI. Health setter 0x140322910 is not an attack entry point.
- Historical save-path/storage research does not replace the current reversible same-account test policy. Do not reopen the superseded lab-account route.

For each new investigation state one missing fact, exact caller/consumer, expected discriminating observation, pinned game identity and evidence path. Update the relevant focused evidence owner after observation. Source/static/native/headset levels remain separate.
