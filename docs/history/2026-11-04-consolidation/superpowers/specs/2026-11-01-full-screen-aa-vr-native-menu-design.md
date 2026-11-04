# Full-Screen AA and VR-Native Menu Design

**Status:** Implemented for the mono OpenXR presentation path; build verified; headset acceptance pending.
**Date:** 2026-11-01

## Goal

Add optional, real full-screen antialiasing and CAS sharpening to the DSVR image, including game content and DSVR UI. Separate the mod UI from the game's backbuffer and make it feel like a stable, world-anchored part of the VR space. Preserve accessible text, controller navigation, the existing 16:9 game theater, and the current safe rendering fallback.

## Evidence and constraints

- `PresentDetour` receives the completed game image. Its observed DXGI swapchain has been single-sample. `ModMenuRenderTexture` currently draws the menu into that image before XR submission.
- `PrepareMonoSource` copies the final game image into a shader-readable D3D11 texture; `BlitToEye` then draws that texture into each OpenXR eye image.
- An OpenXR quad composition layer is composed by the runtime after DSVR's projection layer. Post-processing the projection image cannot affect a separately submitted quad. Therefore, whole-image processing requires DSVR to compose the game theater surface and independently rendered DSVR UI into its per-eye projection target before the final AA/sharpen pass.
- A completed 1x backbuffer does not contain the original scene's subpixel coverage. Resolving or filtering it later cannot truthfully be called MSAA. Proper MSAA requires control of the game scene's matching color and depth targets while its geometry is drawn.
- TAA requires a verifiable temporal path: camera jitter applied before scene rendering, motion/depth data suitable for reprojection, history invalidation on cuts/recenter/resize, and a way to avoid stale-image ghosting. DSVR's final-image hook alone does not provide these inputs.
- CAS can run in DSVR's final full-screen shader pass. The upstream FidelityFX CAS source is MIT licensed and requires attribution.

## Proposed architecture

### Final image and processing

1. Keep the game's swapchain untouched. Copy it to an app-owned, shader-readable source.
2. Keep DSVR UI in its own transparent texture and draw pass. Do not write it into the game backbuffer.
3. Render the game image as the existing world-anchored 16:9 theater surface when theater presentation is active. Render the DSVR UI shrine and readable settings slate as separate world-space geometry with the transparent UI texture. In immersive gameplay, draw the game image full-view and place the UI only when open.
4. Composite those surfaces into the app-owned per-eye projection image. Apply the selected AA path, then CAS, to the complete per-eye image before releasing the OpenXR swapchain image.
5. Keep a safe original blit path. If a resource, shader, history frame, or capability check fails, reset temporal history and submit the known-good unprocessed image for that frame. Never submit a partially rendered or cleared black eye because optional processing failed.

### AA capability and settings

- Antialiasing setting values: `Off`, `MSAA 2x`, `MSAA 4x`, or `TAA`.
- `MSAA 2x`/`4x` may be enabled only if offline/runtime discovery verifies a safe way to draw the game's full scene into matching multisampled color and depth targets before resolve. Device format/sample support must be checked. A final quad or UI-only MSAA path does not satisfy this requirement.
- `TAA` may be enabled only if a verified hook can jitter the actual scene projection and supply valid per-frame reprojection inputs. Reset history on camera cuts, OpenXR space changes, device/resource recreation, focus loss, and mode switches. If those inputs cannot be acquired safely, keep TAA unavailable; do not substitute unwarped frame blending.
- `CAS` has an independent enable and strength setting, applied after AA and after both game and DSVR UI are composited. UI and text must be inspected at low and high strengths for halos and legibility.
- If an AA mode cannot be supported, show its unavailable status and reason in settings/diagnostics. Do not persist or report an unavailable mode as active.
- Persist settings through the existing config parser and keep conservative defaults: AA off, CAS off.

### VR-native UI

- Use a small ash-dark, low-fantasy shrine/lectern as the menu's world-anchored home. Present a few 3D category markers around it; selecting a category opens a transparent settings slate with the existing controls grouped and legible.
- Anchor on menu open or recenter, then keep the menu steady in the world. Do not follow head motion every frame. Preserve the current controller ray and d-pad/gamepad paths, and keep mouse/keyboard operation available.
- Keep the existing 16:9 theater for the game front end, loading, cutscenes, and requested theater mode. Menu artwork and interactions are separate from that game canvas.
- Maintain accessible contrast, text size, clear selection/pressed feedback, and comfortable reach. Avoid requiring the player to lean or hold a pose.

## Implementation order

1. Add passive render-path capability observations that can report texture format, dimensions, sample count, depth pairing, and candidate scene lifetime without changing game render state.
2. Build the DSVR-owned UI texture and per-eye composition path with a safe fallback. Keep the current menu input contracts.
3. Add full-image CAS after the composite pass, including settings and config serialization.
4. Implement MSAA or TAA only if step 1 proves their complete required inputs and safe control points. Otherwise expose a clear unsupported reason and document the specific native dependency.
5. Update user-facing settings and diagnostics so effective state, fallback state, and unavailable modes are explicit.

## Acceptance

- The game's backbuffer remains unchanged by DSVR UI drawing.
- With the menu open, CAS affects both game imagery and the settings UI in the submitted eye image.
- The game theater remains 16:9; the settings shrine remains world-anchored and does not move with head translation/rotation after anchoring.
- Controller ray, d-pad/gamepad, and desktop inputs continue to operate settings.
- AA is labelled active only when its required native data path is present and selected. MSAA levels use supported color/depth sample descriptions and resolve correctly. TAA history resets on every listed discontinuity without visible stale frames.
- Optional-pass failures return to the existing valid image without black frames.
- No Steam launch, injected gameplay input, or headset comfort claim is made by offline build verification. Operator headset acceptance remains separate.

## References

- [Microsoft: CheckMultisampleQualityLevels](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-checkmultisamplequalitylevels)
- [Microsoft: DXGI_SAMPLE_DESC](https://learn.microsoft.com/en-us/windows/win32/api/dxgicommon/ns-dxgicommon-dxgicommon-ns-dxgicommon-dxgi_sample_desc)
- [AMD GPUOpen: FidelityFX CAS](https://gpuopen.com/fidelityfx-cas/)
- [AMD GPUOpen: FidelityFX CAS source](https://github.com/GPUOpen-Effects/FidelityFX-CAS)
- [Khronos OpenXR: Composition layer quad](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerQuad.html)

## Implementation record

- DSVR UI renders into an independent transparent 1920×1080 texture. The game backbuffer is no longer modified by the XR menu path.
- The game theater remains a 16:9 surface. Theater, menu/settings slate, lectern, calibration room, and floor grid are rendered into each per-eye projection image, then FXAA (when selected) and CAS (when enabled) run over the complete image.
- FXAA is the only selectable spatial AA mode. Old eye-buffer MSAA configuration values are migrated to FXAA because they did not multisample the game's already-resolved scene.
- The settings expose CAS enable and strength. The CAS path uses per-channel FidelityFX CAS neighborhood weights and its MIT notice is included in release packaging and the portable launcher resources.
- Native MSAA and TAA remain unavailable. The passive D3D11 probe now records per-frame output-target/depth pairs and dimensions/formats/sample descriptions; regular, instanced, automatic, and indirect draw counts; output binds and clears; immediate-context copies, copy regions, resolves, and command-list execution; and bounded resource-to-present copy/resolve paths. It also records `CheckMultisampleQualityLevels` results for the observed color/depth view formats at 2x and 4x. These capability results are kept distinct from render-path candidates: a matching multisampled color/depth pair with draw calls is only a candidate, while a supported format alone is not evidence of scene MSAA. The probe does not change render state or read resource contents. It still cannot prove a safe scene-target replacement, shader jitter, motion vectors, or temporal history lifetime; TAA remains unavailable. The next operator Steam launch is needed to populate candidate evidence.
- Optional room/UI and post-process failures preserve a complete game-image fallback when the source is available; when an immersive room has no usable source, the eye is cleared to the stable dark green room color instead of submitting an uninitialized target.
- Release builds completed for `dsvr_core` and `dsvr_setup`. No headset launch or visual acceptance was performed in this implementation pass.

