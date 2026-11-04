# Electron rewrite plan: DSVR setup / launcher

Plan date: 31 Oct 2026. Grounded in the tree as it stands after the 31 Oct
immersion pass (Offline ladder: Debug/Release 115/115, auto_verify PASS).
The launcher and native JSON seam are implemented. This file records the
implementation and remaining qualification: phases 1–3 are coded; phase 4
still needs an operator run, and phase 5's optional release path needs a
formal-candidate build and operator install/uninstall receipt.

Ponytail rule applied throughout: the existing `DSVR-Setup.exe` already does
every risky, security-sensitive thing correctly — hash-pinned downloads,
save backups, firewall rules, partial-install detection, Steam-routed
launch. None of that gets rewritten in JavaScript. Electron replaces only
the hand-drawn Win32 chrome in `src/setup/gui.cpp` (~1,700 lines of
message-loop layout code). The native binary stays the single engine.

## 1. What exists today (the surface the rewrite must cover)

Single binary `DSVR-Setup.exe` (`src/setup/CMakeLists.txt`):

- **CLI** (`setup.cpp` `wmain`): `(no args)`/`--gui`, `--verify`,
  `--doctor`, `--install [--yes]`, `--uninstall [--yes]
  [--remove-settings]`, `--firewall-block/-unblock/-status`,
  `--game PATH`, `--help`. Scripts already drive these
  (`verify_preflight.ps1`, `auto_verify.ps1`, the A-lane install
  receipts), so the CLI contract is frozen.
- **Shared UI-free core** (`setup_internal.h`): `LogFn`/`ConfirmFn`
  callbacks, `FindGameExe`, `IsGameRunning`, `VerifyExe`, `DoctorChecks`,
  `InstallState` (loader+core), `ProbeHardware`, firewall helpers,
  `FpsUnlockPin` (allow-listed GitHub release + ZIP/DLL SHA-256),
  `FpsUnlockState` (chain dll + ini + `chain_proxy`),
  `InstallFpsUnlock`, `DownloadTextureTools` (TPUP to Downloads, never
  launched), `DoInstall`, `DoUninstall`. Destructive confirms never
  auto-yes under `--yes`.
- **GUI** (`gui.cpp`): four pages (Home, Game, Settings, Activity),
  nav buttons + custom-drawn frames, one contiguous setting-id block
  130–149 (`WM_COMMAND` uses the range to tell user edits from
  programmatic updates), action ids 101–127 and 150–160, dirty tracking
  against disk, presets (Comfort/Perf/Veteran/Smooth/Res), safe defaults,
  dev mode (Ctrl+D) persisted, an advanced settings index that is
  already data-driven (`{group, label, key}` rows over `dsvr::Config`
  keys — includes `theater_override`, `theater_floor_grid`), and
  `LaunchGame` (Steam-routed `steam://rungameid/570940`, raw-exe
  fallback, partial-install refusal, `AllowSetForegroundWindow`).
- **Test coverage**: `tests/test_setup_theme.cpp` compiles `gui.cpp`
  itself with inert stubs for `CreateProcessW`/`MessageBoxW`/config
  setup layer, and asserts behavior (launch counted once, FPS-unlock
  button installs once and self-disables, control id 140 never exists,
  chain-conflict log text). Any rewrite must keep these behaviours
  testable.

Config model: `dsvr::Config` (`src/common/config.{h,cpp}`), stored as
DSVR.toml next to the game exe, with the headset-rate sidecar
`DSVR.toml.headset-hz` and the game-ini motion-blur force/restore.
In-game F7 reloads settings, so the launcher never needs to hot-swap
anything.

Packaging: `tools/package.py` stages `DSVR-Setup.exe` + `dinput8.dll` +
`DSVR.dll` + docs + licenses + SHA-256 manifest (formal immutable
candidate vs overwritable `DSVR-latest`); `DSVR-latest-verification.json`
hashes the three binaries. `tools/publish_release.py` refuses to publish
while lane checkboxes A–K are open and ships the zip + Setup exe +
manifest to a pre-existing tag.

## 2. Target architecture

```
launcher/                     new, Electron
  package.json                electron + electron-builder, pinned versions
  src/main.js                 spawn engine, expose typed IPC and folder picker
  src/renderer/              plain HTML/CSS/ES modules (no framework)
  engine/                     generated verified Setup exe/manifest inputs
  build/                       electron-builder output -> optional ZIP

src/setup/                    stays, gains one thing
  + json.cpp                  --json <sub> machine-readable mode
```

Principles:

1. **One engine, two fronts.** Everything the GUI does becomes a CLI
   call. The Electron main process spawns
   `DSVR-Setup.exe --json <verb>` and renders stdout JSON; the renderer
   is a dumb view. No installer, download, hashing, firewall or process
   logic in Node. The Win32 GUI stays in the binary as fallback until
   parity (phase 4).
2. **Frozen CLI contract.** Existing flags keep their exact behaviour
   (`verify_preflight.ps1`, `auto_verify.ps1`, docs, user habits). The
   `--json` flag is additive only.
3. **Elevation stays where it is.** Firewall block/uninstall-elevation
   already goes through `netsh`/`ShellExecute` elevation paths in the
   C++ core; Electron just surfaces the result. `requestedExecutionLevel`
   in the Electron app manifest stays `asInvoker` — the engine elevates
   per-operation like the Setup exe does today.

### 2.1 Machine-readable native seam

`DSVR-Setup.exe --json <verb>` prints one JSON document to stdout
(UTF-8, no BOM), human text untouched:

- `status` → `{exe, game_running, install_state, fps_unlock_state,
  headset_hz, version}` (exactly the structs `setup_internal.h` already
  exposes).
- `doctor` → `{checks: [...], ok: true|false}` reusing `DoctorChecks`.
- `verify` → `{ok, sha256, supported}`.
- `config get|set` → `ConfigLoad`/`ConfigSave` round-trip over the
  existing schema; `settings-index` dumps the same
  `{group, label, key}` rows `gui.cpp`'s advanced index uses, so the
  Electron form is generated from the config schema, never a second
  hardcoded list.
- `install`, `uninstall [--remove-settings]`, `fpsunlock-install`,
  `tpup-download`, `launch`, `launch-native`, and
  `firewall-block|unblock|status` reuse the existing C++ paths. Install,
  uninstall and downloads keep native confirmation prompts. These actions
  return `{ok, log, exit_code}`. `safe-defaults` and five preset verbs save
  through the existing config schema.

Test coverage moves to where the doubles already are: extend
`tests/test_setup_theme.cpp` (or a sibling `test_setup_json.cpp` if the
other tab's gui.cpp work stays in flight) to assert the JSON documents
and the refusal paths. ctest count will move from 115 — expected and
recorded when it happens.

### 2.2 Electron shell (`launcher/`)

- **main.js**: `execFile` the Setup exe from packaged `resources` (or the
  development build fallback). The portable package carries and verifies the
  complete adjacent install payload: `DSVR-Setup.exe`, `dinput8.dll`,
  `DSVR.dll`, and `DSVR-SHA256.txt`; Setup copies the DLLs from its own folder.
  IPC exposes an argv allow-list; the renderer cannot compose commands.
  Game-folder selection uses Electron's native picker and persists only the
  selected path under app user data.
- **renderer**: Home (status + Launch), Game (folder, verify/install/
  uninstall/FPS Unlock/TPUP/firewall), Settings (presets, safe defaults,
  generated advanced index and persisted Ctrl+D dev mode), and Activity
  (live log). Dirty tracking compares with `config-get`; Save sends one
  atomic `config-set` batch.
  The setting-id-block trick disappears: the form is generated from
  `settings-index`, so adding a config key touches C++ only.
- **No framework** (ponytail): vanilla ES modules + a small CSS file.
  The Win32 GUI's theme work in the other tab informs styling, it is
  not a dependency.

### 2.3 Packaging changes (DECIDED 31 Oct: separate optional asset)

- Decision (operator): the main `DSVR-latest-x64.zip` stays lean (~1.7 MB
  with the Setup exe as the shipped face); the launcher ships as its OWN
  zip asset (`dist/DSVR-Launcher-x64.zip`) so the Electron runtime never
  enters the core package. The `--json` seam is useful either way.
- `launcher/` builds to a portable dir (electron-builder `dir` target,
  not installer) → zipped separately.
- The launcher ZIP includes the Electron runtime plus all three verified
  native engine binaries and their package manifest under `resources/`.
  Setup installs the adjacent `dinput8.dll` and `DSVR.dll`; bundling only
  `DSVR-Setup.exe` leaves Install unable to complete.
- `publish_release.py` asset list gains `DSVR-Launcher-x64.zip` +
  `launcher_sha256` only when the launcher is built; the three-binary
  hash discipline for the core zip stays untouched. `package.py` does
  NOT gain launcher entries.

## 3. Phases (each independently shippable)

1. **JSON seam in C++** (small, zero-risk): `--json status/doctor/
   verify/settings-index`, tests extended. Win32 GUI untouched. This
   alone decouples any future front-end. **DONE 31 Oct:** `src/setup/
   json.{h,cpp}` + `--json` dispatch in `setup.cpp` + `tests/
   test_setup_json.cpp`; CTest 116/116 both configs; live smoke tests
   for all four verbs plus the `unknown_verb` error path PASS.
2. **Launcher skeleton**: Electron app reading `status`, live doctor,
   and Launch/Install/Uninstall via the seam; dev-only distribution.
   **DONE 31 Oct (dev-only):** `launcher/` (electron 45.2.1 pinned,
   vanilla ES-module renderer Home/Game/Settings/Activity, no framework);
   `src/engine.mjs` holds the whole spawn surface: exe resolved
   co-located-first then dev build fallback (no PATH search), SHA-256
   checked against `DSVR-SHA256.txt`, strict verb→argv allow-list.
   Live smoke (plain node AND `npm run smoke` through Electron main,
   both exit 0): status/verify/doctor/settings-index all return parsed
   JSON from the built Setup exe (verify `supported:true`, sha matches
   pinned build); dist-copy hash check against `DSVR-SHA256.txt` PASS;
   bogus verb rejected at the bridge (`verb_not_allowed`) before any
   spawn. Setup launch uses the same Steam-routed process start as the Win32
   UI. The packaged app refuses engine calls if any required Setup/DLL
   resource is missing or does not match the adjacent manifest. The Win32 UI
   remains the fallback. **1 Nov 2026
   recheck:** packaged Electron smoke PASS against the staged candidate;
   actual packaged window opened and Home/Game/Settings were visually
   reviewed. No install or uninstall action was invoked.
3. **Settings parity**: generated form from `settings-index` + presets
   + safe defaults + dev mode; dirty tracking; F7-reload note stays
   true (launcher never needs to signal the game).
   **DONE 31 Oct (dev-only):** engine side — a ~100-entry key→member
   table in `json.cpp` (`ConfigFields`, one owner for every editable
   key; `motion_blur_original` is read-only) powers `config-get`
   (`ConfigGetJson`, DSVR.toml value spellings) and `config-set`
   (`ConfigApplyKeyValue` + whole-batch `ConfigValid`: a bad key, a
   typo'd enum string, or an out-of-range combination refuses the
   entire batch, exit 1, disk untouched — proven live against an
   isolated `--game` fixture). `launch`/`launch-native` reuse the
   Steam-routed process-start extracted verbatim from gui.cpp into
   new UI-free `src/setup/launch.{h,cpp}` (steam://rungameid/570940 +
   `AllowSetForegroundWindow`); gui.cpp's `LaunchGame` now calls
   `setup::LaunchGameProcess`, and `test_setup_theme.cpp` compiles
   launch.cpp under its `CreateProcessW` double so the launch-count,
   Steam-route and work-dir assertions still hold. Launcher side:
   `engine.mjs` allow-list adds the four verbs with a `KEY=VALUE`
   payload guard (`payload_not_allowed` refuses non-string/whitespace
   payloads before any spawn), payload plumbed through `main.js` +
   `preload.cjs`; the Settings page is now a generated editable form
   (rows from `settings-index`, values from `config-get`, dirty
   tracking, Save = one `config-set` batch). Proof: `setup_json` +
   `setup_theme` focused pass; CTest 116/116 both configs; live
   config-get/config-set smoke (good batch persisted, bad key and
   out-of-range refused, re-read confirms atomicity); `npm run smoke`
   exit 0 with config-get parsed and both bogus inputs refused.
   Presets and Safe Defaults call C++ actions using `ProbeHardware`,
   `PresetForTier` and `FactorySafeDefaults`, so the launcher does not keep a
   second copy of those values. Folder browse is native; its selected path is
   persisted in Electron user data and passed as a separate `--game`
   argument. Ctrl+D toggles saved developer mode. **1 Nov 2026:** full Debug
   and Release builds PASS; Debug and Release CTest PASS 116/116;
   package tool checks PASS (16 tests); `npm run smoke` PASS; isolated fixture
   PASS for Safe Defaults → Comfort preset → persisted config-get. The
   packaged app smoke and interactive view also PASS. Setup Doctor PASS on
   supported build 10943698; evidence logs are under `build/launcher-*.log`.
 4. **Retire decision**: keep the Win32 GUI as fallback until fresh setup and
   launcher checks pass and an operator confirms the portable app on the real
   machine. Then mark `gui.cpp` maintenance-only and keep building it for the
   Setup exe and CLI scripts. **OPEN:** the final portable build has not been
   accepted by the operator, and no clean install/uninstall session was run.
5. **Packaging/publication**: `tools/package_launcher.py` verifies the staged
   Setup executable, `dinput8.dll`, and `DSVR.dll` against their manifest,
   builds the portable Electron directory, and writes
   `DSVR-Launcher-x64.zip` plus `DSVR-Launcher-SHA256.txt`. The core package
   stays unchanged. Formal `auto_verify.ps1 -Package -Version <version>
   -IncludeLauncher` rebuilds that asset from the exact immutable candidate;
   the publisher checks the outer ZIP digest and all three embedded engine
   hashes before attaching both launcher assets. Without `-IncludeLauncher`,
   the release keeps its existing three
   assets. **OPEN:** build against a formal candidate and record the operator
   clean install/uninstall receipt. **1 Nov 2026 latest local refresh:**
   core ZIP 1,824,418 bytes, SHA-256
   `ab8f781dfeb6902bb1bde33af857596998b9aa243ba4d146283227ac010fc1ee`;
   optional launcher ZIP 163,596,211 bytes, SHA-256
   `c5441a4c1f9c2675915845f24171aab90ebb9808a9a1ef1ce4f886f6aae6dc8c`.
   The portable package includes all three manifest-verified engine binaries
   under `resources/`; package-time and final-ZIP hash checks pass, and
   packaged Electron `--smoke` passes. Release DLLs match the current
   installation; no install or game launch was performed for this packaging
   fix. This remains a development package, not a formal release. Phase 4
   operator launcher acceptance and phase 5 formal-candidate/install-uninstall
   qualification remain open.

## 4. Risks / open decisions

- **Dist size** (Electron runtime ≈ 200–250 MB): RESOLVED 31 Oct —
  separate optional asset (2.3); the core zip never carries Electron.
- **Steam launch from Electron**: reuse the engine's `launch` verb —
  the steam:// routing + `AllowSetForegroundWindow` grant live in C++
  and are covered by the theme-test doubles; do not reimplement.
- **Unsigned portable app**: the project has no signing certificate. The
  optional launcher ZIP is unsigned and may trigger SmartScreen; record the
  operator's distribution decision before making it the shipped face.
- **Never two sources of truth**: any new config key must be reachable
  through `settings-index` (single C++ table) — the same discipline the
  31 Oct `theater_override`/`theater_floor_grid` rows already follow
  (config parse + alias + ini write + GUI row + test in one pass).

## 5. Non-goals

- No port of install/download/hash/firewall logic to JS — pinned
  digests and allow-lists stay in `fps_unlock.cpp`.
- No replacement of the CLI — it is the automation surface
  (`auto_verify`, `verify_preflight`, A-lane receipts).
- No new config format — DSVR.toml and the headset-hz sidecar stay as
  the core already reads them.
