# Render core implementation receipt

Date: 2026-11-01

Status: Development compositor build installed. Native game-scene resolution control and headset acceptance are unfinished.

## Implemented

- Each eye owns its recommended/maximum dimensions, swapchain images, RTVs and full-image processing surfaces. At 100%, its exact recommendation is allocated and submitted; equal dimensions are no longer assumed. Native-eye targets carry their own extents. Runtime image dimensions/sample layout and returned indices are validated before drawing.
- The eye allocation helper preserves odd dimensions, checks impossible recommendations and respects D3D11 limits. Production allocation requires runtime recommendations rather than silently substituting desktop dimensions.
- Immersive drawing fills each eye image and maps its asymmetric rays to the source projection. The centered source FOV envelope covers both runtime frusta. The old immersive viewport subrect and theater source-pixel shrink were removed. A narrower source is clamped at its edge instead of introducing empty peripheral viewport regions; this does not create missing scene detail.
- The world room shader distinguishes opaque game color from premultiplied transparent UI. Game alpha 0, 0.5 or 1 no longer controls theater visibility. UI uses its own coverage and preserves the room behind it.
- Pixel constant-buffer slots b0 and b1, including D3D11.1 ranges, are restored. The observer refreshes the actual native output bindings after the internal pass instead of forgetting restored bindings until a game rebind.
- Source selection first inspects a display-color render texture bound at pixel slot 0 while the final output is bound. Discovery fallback considers display-color targets without depth, rather than selecting high-draw HDR/G-buffer intermediates. Aspect ratio is not a source-selection requirement.
- Source storage is owned, shader-readable and constructed transactionally. Typed/typeless color inputs and supported MSAA resolves are handled explicitly; failed copies/resource creation preserve the prior completed source storage. Subresource zero copies support a source with more mip levels without an incompatible whole-resource copy.
- Observation gaps retain completed app-owned pixels and their camera packet, rather than copying a black host source or silently tagging held pixels with a newer camera pose. Age/generation and per-eye/source dimensions are reported. Stale camera/source state and source-copy failures route to a world room with a recovery message. The menu texture API reports failure rather than marking an unrendered texture ready.
- The temporary synchronous black-path luma readbacks were removed from production frames. Settings show headset output dimensions separately from game-image dimensions.

## Verification

Release build succeeded for `dsvr_core`.

Command:

    ctest --test-dir build -C Release -R '^(d3d11_state_guard|depth_probe|xr_eye_resolution|xr_stereo_frame|xr_mono_recovery|mod_menu_render|render_host|xr_inactive_frame_pump|menu_presentation|menu_state|stereo_math)$' --output-on-failure

Result: 11/11 passed, 1.80 seconds. Receipt: `build/verification/render-core-20261101/ctest.txt`.

The WARP compositor fixture executes the production shaders and copy/submission path with a fake XR runtime. It checks actual image pixels, exact unequal 100% eye dimensions (100x100 and 103x97), full-eye coverage under asymmetric FOVs, typed/typeless sources, a 4x source resolve, unsupported-source preservation, owned-image retention after the original image becomes black, stale-frame recovery, malformed camera provenance, invalid image index recovery and image wait/release errors. Its measured source-copy count is one per fresh frame and periodic CPU readbacks are zero. These numbers describe the test fixture, not game performance.

The state guard regression was first run before the fix and failed on the b1 binding identity. It now passes binding identity and ranged-binding checks for both slots. The menu fixture renders the actual recovery text with the normal menu closed. The depth-probe fixture exercises real MinHook interception on WARP and checks final-bound input selection and binding preservation across internal passes.

## Distribution

- ZIP: `dist/DSVR-0.16.0-render-core-reliability-1-x64.zip`, 1,600,501 bytes.
- ZIP SHA-256: `22dfb4316cd052a12dc50fc1cde3b8e3c80bedf5dbb5545d6c08650586057d7a`.
- Payload manifest: 19 files verified against their archive contents.
- DLL SHA-256: `e132e18e11923e1faddae1430af12c698e7c40d75b448660d8024625005d9bff`.
- Installed Steam DLL matches the built/archive DLL. Installation checked that DarkSoulsRemastered was not running. Previous DLL was preserved as `DSVR.dll.pre-render-core-reliability-20261101.bak` in the game folder.
- No commit, push or game launch was performed.

## Remaining requirements

The native game scene is not yet rendered at the requested eye resolution. Existing evidence had a 1920x1080 scene image and 2144x2292 eye images. The new exact output allocation is not evidence of native scene detail at that resolution. Native color/depth allocation, viewport/projection and dependent passes still require a verified integration point.

The Ghidra MCP instance could not connect. Read-only headless analysis of the existing DSRFull project did succeed without changing the program. It found resolution log strings at 1413DD290 / 1413DD2B8 and native render-target creation/flush/clear draw-plan families, but not enough ownership/call evidence to resize game targets safely. Raw analysis output remains outside the repo at `C:/Tools/DarkSoulsRemaster_De/render-evidence/`; it is not part of the package.

The source bound at final-output input 0 is evidence of a binding, not a proof that the final shader consumes it unchanged or that it represents final color. Candidate fallback is still a discovery heuristic. Live source identity, freshness, gamma/tone mapping and menu/loading/gameplay transitions remain to be checked in SteamVR.

Next operator launch must verify calibration -> game menu -> loading -> gameplay, left/center/right head turns, pause/settings, respawn, recenter/focus recovery and effects on/off. Inspect `VRImage`, per-eye allocation and `DepthProbe: source evidence=` records. Do not claim the full design complete, true native eye rendering, true stereo or headset visual correctness from the automated results.
