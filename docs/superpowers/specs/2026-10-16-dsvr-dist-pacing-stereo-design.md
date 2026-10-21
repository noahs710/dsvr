# DSVR — always-testable `dist/`, runtime-driven rate matching, true native stereo

- **Date:** 16 October 2026
- **Status:** Approved direction; awaiting written-spec review (no implementation yet)
- **Follows:** user direction 2026-10-16 — “builds are always packaged into `dist/`
  nicely for the user to test; focus on true stereo and auto
  framerate/refresh-rate matching between OpenXR and DSVR.”
- **Prior-thread note:** the linked ChatGPT share could not be read (HTTP 403;
  no matching local session). This spec is grounded in the repository itself;
  if the thread contained extra constraints, they must be folded in explicitly.

## 1. Context (verified in source)

| Area | Current truth | Key files |
|---|---|---|
| Presentation | Mono only: predict at Present, blit one backbuffer to both eyes. Stereo API exists but no native caller. | `src/core/xr.cpp` (`XrSubmitMonoFrame`), `src/core/xr.h`, `src/core/xr_stereo_frame.h`, `src/core/stereo_math.h` |
| Pacing | `frame_limit_fps` (default 90) is requested only; core logs “native limiter patch is not applied yet, game remains capped at 60.” XR logs `predictedDisplayPeriod` but nothing consumes it as the authority. | `src/common/config.h`, `src/core/core.cpp`, `src/core/xr.cpp`, `src/core/tracking_frame.h` |
| Packaging | Manual and immutable-only: `tools/package.py` + `tools/auto_verify.ps1 -Package -Version` refuse overwrite; no build step touches `dist/`. Latest archive is `0.16.0-render-host-preview`. | `tools/package.py`, `tools/auto_verify.ps1`, `dist/` |
| Native stereo prerequisites | RenderDoc frame 2774 analysed; `NativeEyeClip` proven offline; selector `0x1411C9740` / collectors observed; safe two-eye bracket not yet proven. | `docs/ENGINEERING.md` §B, `MODLOG.md`, `tools/native_stereo.py` |

Repo invariants this design must not break:

- Immutable formal releases: no archive is ever refreshed under the same
  identity (`tools/package.py` refusal is a feature).
- No fake stereo and no HP-poke combat substitutes are shipped as features.
- A limiter patch alone is not a pacing policy; requested ≠ actual must stay
  visible until measured otherwise.
- Offline tests never certify headset behaviour; native acceptance needs eye
  images, timing, and comfort evidence per `docs/TESTING.md`.

## 2. Goals and non-goals

### Goals (in build order, one spec, three sequential workstreams)

1. **W1 — Always-testable `dist/`.** Every fully green Release build + CTest +
   doctor run leaves one obvious, current test package in `dist/` without
   minting a new formal version each time.
2. **W2 — Runtime-driven rate matching.** When an OpenXR session is live, the
   runtime’s actual display period is the pacing authority; DSVR derives one
   effective game target from it, applies it where a verified route exists,
   and reports requested vs. actual honestly everywhere else.
3. **W3 — True native stereo.** Two real DSR scene renders from runtime eye
   poses/projections at one simulation instant, submitted as one stereo
   layer — no mono-copy or synthetic-offset substitute.

### Non-goals

- Setting the headset/runtime refresh from DSVR (no standard OpenXR API;
  refresh stays chosen in SteamVR / Virtual Desktop settings).
- Raising simulation rate above DSR’s normal rate without a verified
  decoupling/interpolation policy.
- New formal version numbers for W1/W2 alone; formal `R1/R2/R3` gates in
  `README.md` still own version assignment.
- Combat, body, and UI work beyond what stereo integration strictly needs.

## 3. Workstream 1 — `dist/DSVR-latest` auto-packaging

### 3.1 User-visible behaviour

- Running the canonical command always ends in one of two states:
  - **Green:** `dist/DSVR-latest/` (staging folder) and
    `dist/DSVR-latest-x64.zip` contain the just-built binaries, and
    `dist/DSVR-latest-verification.json` + `dist/DSVR-latest-ctest.txt`
    record how they were produced.
  - **Red (build/test/doctor failure):** the previous `DSVR-latest*`
    artifacts are left byte-for-byte untouched, and the command exits
    non-zero naming the failed gate.
- Formal releases are unchanged: `auto_verify.ps1 -Package -Version <new>`
  still mints an immutable `dist/DSVR-<version>/` + zip and still refuses
  any existing identity, including `latest` passed as a formal version.

Canonical command (documented in `README.md` / `docs/TESTING.md`):

```powershell
.\tools\auto_verify.ps1            # configure + build + ctest + doctor + refresh DSVR-latest
.\tools\auto_verify.ps1 -Package -Version 0.17.0-<gate-name>   # formal immutable release (unchanged)
```

### 3.2 Components

| Component | Change |
|---|---|
| `tools/package.py` | Add `--latest` mode sharing the existing file map/manifest code. `latest` is the only overwritable identity; every other version keeps the current refusal. Stage into `dist/.DSVR-latest.tmp/`, verify, then replace `DSVR-latest/` + zip as the last step. Always write `DSVR-VERSION.txt` with the mod version plus `latest (dev test package, not a formal release)` and the build timestamp. |
| `tools/auto_verify.ps1` | Default path gains the latest-package step after CTest and `--doctor` pass; any earlier failure skips packaging. `-Package/-Version` validation unchanged. |
| `CMakeLists.txt` (root) | Add `dsvr_package_latest` custom target invoking `package.py --latest` (depends on loader/core/setup outputs, and on the ReShade addon when enabled) so IDE builds converge on the same packager. The gated guarantee lives in `auto_verify.ps1`; the CMake target is a convenience and must print that it does not run CTest/doctor. |
| `tests/test_package.py` | Cover latest-mode overwrite, formal-version refusal (including `--version latest` without `--latest`), temp-stage cleanup on missing inputs, and manifest correctness for the latest payload. |

### 3.3 Package contents (“nicely”)

Exactly the current formal payload (binaries, `DSVR-Setup.exe`, licenses,
`tools/vr_trace.py`, the five maintained docs) plus, as siblings of the zip
(not inside it, matching the existing `0.16.0` preview convention):

- `DSVR-latest-verification.json`: mod version, UTC build time, CMake
  config/generator, CTest totals (passed/total), doctor result, git-free
  source fingerprint (SHA-256 over the packaged binaries), and the pacing
  evidence fields from §4.5 (requested fps, last runtime period if any —
  `null` offline, never invented).
- `DSVR-latest-ctest.txt`: full CTest output for that build.

### 3.4 Error handling

- Missing build outputs → fail before touching `dist/` (reuse existing
  missing-file report); temp stage removed.
- Zip/stage replace failure → keep old `latest`, report the OS error; never
  leave a half-copied folder as `DSVR-latest/`.
- Concurrent runs: second packager invocation fails fast with a clear
  message instead of interleaving copies (lock via temp-dir existence).

## 4. Workstream 2 — Runtime-driven rate matching

### 4.1 Principle

One effective-rate authority. While a session is live it is derived from
`XrFrameState::predictedDisplayPeriod`; before any session (or after loss)
it falls back to `[vr] frame_limit_fps`. The config value becomes a
preference/fallback, never a claim about the running game.

Supported caps stay `60/72/90/120/144/240` (`src/common/config.cpp`).
Quantization picks the supported rate nearest the runtime rate; an
exact-half tie picks the lower rate (prefer under-running the display over
over-running it). Rates outside the supported span clamp and log.

### 4.2 New pure module — `src/core/display_rate.h` (header-only, tested)

```cpp
struct EffectiveRate {
  int requested_fps = 90;          // config fallback / preference
  long long runtime_period_ns = 0; // 0 = no live runtime sample
  double runtime_hz = 0.0;
  int effective_fps = 90;          // the single target everyone consumes
  bool from_runtime = false;
  bool mismatch = false;           // effective != requested (worth logging)
};
int QuantizePeriodToFps(long long period_ns); // 0 when period invalid
EffectiveRate ResolveEffectiveRate(int requested_fps, long long period_ns);
```

No XR, Windows, or file dependencies — directly unit-testable like
`stereo_math.h` / `combat_math.h`.

### 4.3 Integration points

| Site | Change |
|---|---|
| `src/core/xr.cpp` (mono `XrSubmitMonoFrame` and `XrBeginNativeStereoFrame`) | On every `xrWaitFrame`, resolve the effective rate from `state.predictedDisplayPeriod`. On change (including first live sample and loss→0), publish it and log once: `RateMatch: source=runtime period_ns=<n> runtime=<hz>Hz effective=<fps> requested=<fps>` (or `source=config-fallback …` when no session). Existing `XRTiming` period logging is folded into this line, not duplicated. |
| Publication | Render-thread write, atomic/snapshot read in the style of `TrackingFrames` (no lock across XR calls). Expose `bool XrEffectiveRate(EffectiveRate& out)` in `src/core/xr.h` for diagnostics/core consumers. `TrackingFrame::display_period_ns` remains the per-prediction record. |
| `src/core/core.cpp` | Startup log states the fallback and that the runtime will take over when live. The current “limiter not applied” warning is reworded to name effective vs. requested once known; it must never print “90 FPS” as achieved from config alone. |
| `src/core/render.cpp` | The existing ~5 s FPS counter becomes the measured-game-FPS evidence: log `RateEvidence: game_fps=<measured> effective=<fps> runtime=<hz>Hz` at the same cadence (not verbose-only), so every native run records actual game FPS + XR period as `README.md` requires. |
| Game limiter application | **Separate verified sub-step (R2 below).** Until the native rate route is traced, no limiter bytes are patched — in particular the known wait helper whose second argument is an absolute frame index must not be repurposed as an FPS value. W2 ships measurement + authority first; the patch lands only behind its own regression + native FPS proof. |

### 4.4 Lifecycle and edge cases

- Period changes mid-session (runtime switch, VDXR vs. SteamVR) update the
  effective rate without restarting the session; consumers re-read the
  snapshot, never cache a startup copy. To avoid flapping on noisy periods,
  a new quantized rate must persist across consecutive `xrWaitFrame`
  samples before it replaces the current effective rate (exact count
  declared in the implementation plan and pinned by `display_rate` tests).
- `predictedDisplayPeriod <= 0`, non-finite, or session loss → `from_runtime
  = false`, effective = requested, one log line; tracking invalidation rules
  are unchanged.
- F7 config reload may change `requested_fps`; it never overrides a live
  runtime-derived effective rate.
- Observer-only mode never creates a session and always reports the config
  fallback.

### 4.5 Tests and acceptance

- `tests/test_display_rate.cpp` (new CTest `display_rate`): period→fps table
  (72/90/120 Hz exact, near-miss, tie-to-lower, clamp, invalid), fallback,
  and mismatch derivation. Must fail if quantization or fallback regresses.
- Existing XR fixtures (`xr_stereo_frame`, `xr_mono_recovery`) gain an
  effective-rate publication check using their fake runtime periods.
- Native acceptance (protected run, disposable save): log shows
  `RateMatch` + repeated `RateEvidence` with runtime period ≈ 11,111,111 ns
  at a 90 Hz setting and measured game FPS reported beside it. A 72 Hz
  runtime run must show effective 72 — silently qualifying it as “90” fails
  the gate (`docs/TESTING.md`).
- **R2 exit (limiter applied):** measured game FPS tracks the effective rate
  within the pre-declared tolerance on the same scene, with simulation speed
  (animation/stamina/projectile timing) matching the 60 FPS baseline —
  otherwise the patch does not ship.

## 5. Workstream 3 — True native stereo

### 5.1 Shape

```
Present/render thread
  └─ XrBeginNativeStereoFrame  (predict once; borrow 2 eye targets)  [exists]
       └─ Native stereo bracket (NEW — the work in this workstream)
            eye 0: NativeEyeClip(center, eye0) → eye0 target → native main-scene draws
            eye 1: NativeEyeClip(center, eye1) → eye1 target → native main-scene draws
            (workers joined; no 2nd Present / input / physics / animation)
       └─ XrEndNativeStereoFrame(token, mask==3)  [exists]
  fallback: any failure cancels the pair → existing mono path continues
```

The existing `XrStereoFrame` acquire/wait/release discipline (bounded waits,
timeout ≠ ready, no stale-eye pairing) is reused unchanged.

### 5.2 Precondition gates (each proven before the next is attempted)

1. **Bracket ownership:** from the retained RenderDoc frame and selector/
   collector evidence, identify the narrowest native span that renders the
   main scene (color+depth) for one camera, excluding shadow/cubemap/HUD/
   auxiliary passes, and that tolerates being driven per-eye (targets +
   slot-0 camera constant) without replaying history/sorting side effects.
   Whole-dispatcher (`0x1411C9740` blind double-call) replay is forbidden.
2. **Pose/scale mapping:** derive XR-eye → center-view origin/basis in
   native units for `NativeEyeClip` (centre-relative model; bone palettes
   untouched so eye translation is not applied twice). Canted eyes and
   asymmetric tangents use runtime values only.
3. **Effect containment:** prove the bracket advances no input, physics,
   animation, combat, RNG, history rotation, resource retirement, or
   Present; join actual render workers and respect GPU lifetimes.

### 5.3 Components

| Component | Change |
|---|---|
| `src/core/stereo_render.h/.cpp` (new) | Owns the bracket state machine: `Begin(token) → RenderEye(0/1) → Finish/Cancel`, owned-edit save/restore with owner+expected-value checks, and refusal on thread/token/timeout/transition mismatch. Native draw invocation sits behind a narrow callback seam so fixtures drive the real state machine without the game. |
| Native hook site (proven in gate 1; expected near the main-scene draw span, installed like `camhook.cpp`) | Calls the bracket once per frame when a stereo frame is active; otherwise zero-overhead pass-through. Startup-only enable, same pinned-byte/vtable gating discipline as existing hooks. |
| `src/core/camera.cpp` | Supplies the consumed centre view/projection for the bracket from the same tracking packet as the head pose; no new CameraMan-container accesses. |
| `tools/native_stereo.py` + `tests/test_native_stereo.py` | Retain the 13-phase synthetic contract; extend only if the bracket’s real phase/identity evidence needs a field. Synthetic pass still reports `complete=false`, native UNVERIFIED. |

### 5.4 Error handling

- Any eye failure, stale token, >100 ms bracket overrun (existing native
  timeout), focus/space/session transition → cancel the whole pair, submit
  nothing for that frame, invalidate tracking as today; mono presentation
  continues. Never submit one new eye with one stale eye.
- Owned native edits restore only if owner and expected value still match;
  conflict invalidates the pair.
- Device/session shutdown follows the existing swapchain-destruction
  contract (completion marker, retained leases).

### 5.5 Tests and acceptance

- Offline: extend `test_stereo_math` (parallax/occlusion geometry),
  `test_xr_stereo_frame` (pair lifecycle), plus new `test_stereo_render`
  driving the real bracket state machine with a recording fake draw
  callback — ordering, single-prediction, mask enforcement, cancel paths,
  and “no second simulation advance” counters must fail before the fix
  pattern and pass after, per repo practice.
- Native (protected disposable-save run): captured left/right eye images
  from the same instant show near/far parallax and correct occlusion;
  different hashes alone fail. Record eye-pair CPU/GPU p50/p95/p99 vs. the
  11.11 ms (90 Hz) budget, missed/reprojected counts, and the §4
  `RateEvidence` lines. Comfort/fusion verdict is human-only.
- Exit = `docs/TESTING.md` “True stereo” row fully evidenced; only then may
  a formal test release claim stereo (R1 gate still owns the version).

## 6. Cross-cutting

- **Build order:** W1 → W2 (R1 measurement, then R2 limiter behind its
  gate) → W3 (gates 1–3, then bracket). Each workstream lands with its
  tests green (`cmake --build build --config Release` +
  `ctest --test-dir build -C Release --output-on-failure`) and refreshes
  `dist/DSVR-latest` via §3 — that is the “always packaged” loop in
  practice.
- **Docs:** update `README.md` (canonical build command), `MODLOG.md`
  (checkpoint per workstream), `docs/TESTING.md` (rate-evidence fields,
  latest-package convention), and `docs/ENGINEERING.md` (§B status) in the
  same changes that alter behaviour. No doc may claim stereo or an applied
  limiter before its acceptance evidence exists.
- **Risks:** native bracket may prove broader replay is required (then
  enumerate/suppress non-render effects selectively or stop — no assertion
  suppression); limiter route may not exist safely (then W2 stays honest
  measurement + XR pacing, reported as a reduced mode); runtime period may
  be unstable on some runtimes (quantization hysteresis: require the new
  period to persist across consecutive `xrWaitFrame` calls before
  switching the effective rate — threshold declared in the plan).

## 7. Open decisions deferred to the implementation plan

- Exact native bracket address/span (gate 1 evidence decides; not guessed
  here).
- Limiter mechanism (R2) once the native rate route is traced.
- Whether `verification.json` also embeds the full CTest test list or only
  totals (totals preferred; full log already ships as the sibling `.txt`).
