# DSVR testing and operation

Current: 21 October 2026. [Documentation authority](../README.md#authority-and-maintenance). This is the sole operating/testing policy. Follow the [execution guide](superpowers/plans/2026-10-21-native-conversion-execution.md) for task order and the [runbook](VERIFICATION_RUNBOOK.md) for cases/thresholds.

## Authorization and run boundaries

The user has authorized useful protected installation, launches and testing autonomously. Use the existing Windows/Steam account with a reversible disposable-save transaction; do not reopen the superseded lab-account setup discussion. Planning and documentation edits do not launch/deploy. Human comfort/feel requires the operator, and publication/upload requires separate authorization. Do not seize active desktop input.

Same-account backups/swap provide recovery, not OS/account isolation. Steam metadata, shared account effects and synchronization need explicit handling. Historical confirmation of Steam Offline is not a live state check; verify current operating preconditions without repeatedly asking for already granted authorization.

Before each native transaction:

1. Inspect current preflight and transaction tools/receipts. Establish exact executable/build/config/runtime identity and a fixed disposable scene/character. Resolve a current BLOCKED preflight before deployment; a historical missing-root report is not a present blocker by itself.
2. Keep Steam Offline and exact-game outbound blocking. Preserve both Documents/NBGI/DARK SOULS REMASTERED and AppData/DarkSoulsRemastered save roots, active Steam userdata/570940 and relevant synchronization metadata. Check installed synchronization state; never assume store metadata proves Cloud absent or disabled. If it cannot be verified, retain offline protection and record the uncertainty; do not return Steam online with test state present.
3. Snapshot original saves/config/install files with SHA-256 and ownership manifests before any test-owned replacement. Roots can differ; do not treat one as a redundant copy. Enumerate current files rather than assuming the old 13-file count. Preserve foreign proxies, existing config and unrelated newer changes.
4. Confirm the game is closed before install. Use the established disposable-save route; backups alone do not demonstrate that the running process will write only the intended test state. Verify exact installed package/config hashes and actual process/save route before enabling the experiment.
5. Name one hypothesis, bounded capture/deadline, neutral-input cleanup and exact-PID stop path. Useful desktop observation comes before asking for human headset time. Do not manufacture readiness through guessed native writes or side-effectful getters.
6. After exit or failure, neutralize input, collect logs/dumps/results, restore test-owned files/saves/metadata through the receipt and hash-check originals. Do not overwrite unrelated newer changes; stop and reconcile such a conflict. Keep restoration state explicit. Leave Steam open when possible and offline until originals and mod deployment are reconciled.

No new native contract may be guessed to bypass a blocked gate. Protect runtime registration: prefer process-scoped selection where supported; snapshot/restore any deliberate global change and qualify runtime routes separately.

## Evidence and verdicts

Evidence levels: **source** (implementation/fixtures), **static** (binary/data observations), **native** (observed in DSR), **headset** (operator-reviewed). Never promote one level to another.

Verdicts: **PASS** means the named observation on the identified candidate met a predeclared criterion; **FAIL** means it missed it; **UNVERIFIED** means it was not exercised; **BLOCKED** means a named prerequisite prevents exercising it. BLOCKED/UNVERIFIED never mean complete. Incomplete or dropped trace intervals cannot support the affected claim.

Keep build/config/executable hashes, runtime/settings, commands/exit codes, raw evidence paths and restoration receipt per run. Offline tests, doctor, synthetic stereo schema, diagnostic targets and different texture handles are insufficient native/headset proof. Confirm damage attribution independently; simultaneous HP loss alone does not identify an attack.

## Build verification and packaging

Run from the repository root. Inspect existing CMake cache/toolchain before configuring. The checked-in verifier selects Visual Studio 18 2026/x64.

```powershell
cmake --build build --config Debug --parallel 4
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
pwsh -File tools/auto_verify.ps1
```

Use `ctest --test-dir build -C Release -N` to discover actual test names before focused `-R` runs. Register necessary fixtures in tests/CMakeLists.txt. Test mutation/adapter boundaries, invalid state and lifecycle, not only helper self-consistency. Run broader suites once the integrated candidate is ready; repeat when changes/failures justify it.

tools/auto_verify.ps1 configures/builds Release, runs CTest and setup doctor, then refreshes dist/DSVR-latest and its ZIP/verification/CTest receipts on success. It does not install or launch, and does not replace Debug/native/headset qualification. Never reuse historical suite totals as current results. sources/tests define config defaults.

With DSR closed, setup supports `--doctor`, `--install [--game <folder>]` and `--uninstall`; inspect current implementation/help before use. `--yes` acknowledges setup prompts; it does not prove run preconditions. Setup deploys loader/core; an optional ReShade host/add-on requires its own pinned matched binaries and reversible manifest. Select one host at startup; restart for host/action/resource changes rather than assuming reload fully reinitializes XR.

Formal versioned packages are immutable and require a passed release gate plus a new version. Use `pwsh -File tools/auto_verify.ps1 -Package -Version <new-version>` only after qualification. Development-package refresh is not a release claim. Include original mod code/config/notices and install/rollback/control/support instructions; exclude game assets, decompilation, credentials and saves. Upload/publication is separately authorized.

## Native/headset acceptance

Target game/runtime cadence is 90/90 at declared settings. Record actual runtime period, measured game rate, complete eye-pair CPU/GPU p50/p95/p99, misses/reprojection and full-scene coverage. Report runtime routes separately. Reduced rates, lower resolution and reprojection are explicit modes and do not satisfy the requested 90 Hz same-simulation stereo gate.

Use the runbook cases and the execution ledger A–K on the exact candidate/package. Declare thresholds before each run; do not relax them to convert a failure to PASS. Prepare a short controller/comfort route after machine-observable defects have been investigated. The operator stops immediately for discomfort; do not request repeated blind trials or ask them to recover native offsets.

Record a reproducible native session of at least ten minutes for an integrated candidate, plus paired eye captures and a 20–45 second gameplay clip before a user-facing playable claim. Final campaign qualification also requires the campaign/special-action matrix and a longer progression session; a ten-minute arena run alone is insufficient.

Support evidence includes DSVR.log, DSVR-crash dumps, doctor output, exact config/package identity and complete trace; tools/vr_trace.py can analyze captures. For black images isolate source/eye/compositor with bounded observation. For walking lag collect matched tracking/root/view timings. Uninstall test-owned extra DLLs before online use; Steam verification does not necessarily remove extra proxies.
