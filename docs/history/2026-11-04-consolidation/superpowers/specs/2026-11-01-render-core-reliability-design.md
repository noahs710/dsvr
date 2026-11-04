# Immersive and theater render core

Date: 2026-11-01
Status: Approved. Compositor hardening implemented and development build installed; native scene-resolution integration and headset acceptance remain open. See ../../research/2026-11-01-render-core-verification.md.

## Required result

DSVR owns one reliable OpenXR/D3D11 composition pipeline for calibration, game menus, loading, immersive gameplay and settings. At 100% each submitted eye image has exactly that eye's runtime-recommended width and height. Native game rendering must supply sufficient actual scene detail at the requested resolution; copying a smaller desktop image into a larger eye image does not meet that requirement.

The work is complete only after automated graphics checks and an operator SteamVR run demonstrate the acceptance cases below. Reliability is a measurable release condition, not a promise that software has no possible bugs.

## Evidence from the current implementation

- `xr.cpp` reduces the runtime's per-view dimensions to one shared largest recommendation and one shared minimum maximum. All swapchains, post surfaces, viewports and submission rectangles use global width/height. This is correct for equal recommendations, but cannot exactly represent different eye recommendations.
- The last headset run used 2144x2292 eye images and a 1920x1080 game scene source. The 3440x1440 host Present image was black. The observer repeatedly missed a scene target and switched the XR copy to the black host. The installed retention patch avoids that immediate switch but does not prove source freshness or native rendering resolution.
- The source selector ranks observed single-sample 16:9 targets by draw count. Draw count and aspect ratio do not establish that a texture is the final game image. Its observer also records unobserved draws on other threads and command-list execution without the contents of those command lists.
- The room shader uses sampled alpha to blend both transparent DSVR UI and game theater imagery. Game output alpha is not a reliable coverage channel. Valid game RGB can therefore become invisible in the theater.
- `D3D11StateGuard` preserves pixel constant slot b0, while composition writes b0 and b1. It must preserve every state the shared path changes, including constant-buffer ranges where D3D11.1 is in use.
- The existing native stereo API schedules eye images but no native eye renderer is bound. Its common width/height contract must evolve with per-eye targets. This work must not claim true stereo by duplicating a mono source.

## Approach

Recommended: deepen the existing single frame owner and compose both presentations through a common, explicitly owned set of graphics resources. Preserve the established OpenXR session, thread and image-lease contracts. Replace source guessing and implicit global dimensions with explicit frame and eye descriptions.

A second renderer or separate theater frame owner would duplicate session, timing, cleanup and failure handling. A wholesale replacement would discard existing tested image-lease handling. Continuing with isolated black-screen patches would leave source identity, transparency and sizing ambiguous. Neither alternative is selected.

## Ownership and interfaces

The native source adapter owns source discovery, capture and the descriptor of what was actually rendered. It publishes an owned image plus dimensions, typed color format, sample count, color-space interpretation, content kind, native camera projection/pose identity when known, frame ordinal and resource generation. Other modules cannot claim a source is current merely because its COM pointer still exists.

Each eye owns its runtime configuration, requested extent, actual swapchain extent, images/RTVs and post-processing surfaces. All drawing, shader pixel-size constants, copies and submitted image rectangles use that eye's extent. Cleanup belongs to the same owner. Resource construction is transactional: a partial allocation is cleaned up before it can become the active resource set.

The compositor accepts a source frame, validated tracking/view data and one presentation description. Presentation describes immersive projection, opaque 16:9 game theater or transparent spatial UI. The compositor owns no native game simulation or calibration alignment state.

The frame owner remains responsible for wait/begin/end order, image leases, session lifecycle and submission. No presentation change creates a second XR call lane.

Use concrete modules with narrow responsibilities, native RAII/COM ownership and existing project conventions. Do not add a general render graph or plugin framework for hypothetical features.

## Resolution contract

1. Retain the runtime's recommendation and maximum independently for each of the two primary stereo views.
2. At 100%, use each view's exact recommended width and height, without source-size caps, rounding to even dimensions, using the other eye's dimensions or using the maximum as a base.
3. Other scale values apply to each eye's recommendation, using checked integer arithmetic and a documented rounding rule. Runtime maxima and D3D11 limits are ceilings. Invalid recommendations fail setup with a clear diagnostic; they must not silently turn 100% into a different resolution.
4. Runtime settings are authoritative. Re-query recommendations on a supported session/system recreation. Avoid rebuilding swapchains just because calibration, menus or theater became visible. Preserve the current restart-required scale setting unless a safe explicit rebuild is implemented and verified.
5. Allocate UI and post surfaces independently of desktop-window size. Whole-image effects process precisely the active eye image, with correct pixel increments and matching copy dimensions.
6. Log requested, allocated and submitted dimensions separately for both eyes, along with the native scene-source extent. The settings UI must distinguish eye output resolution from actual game-source resolution when they differ.
7. Exact native game scene resolution requires verified control over the game's scene color/depth allocation, viewports, scissor, projection and dependent screen-space passes. Trace the complete render chain before changing allocation. Resize the coherent set at a natural resource-recreation boundary. A blind texture-size detour is not acceptable.
8. For a verified native eye rendering path, each eye scene uses its own requested extent and asymmetric projection. Until that path is verified, a mono source must cover the union of both required eye frusta at adequate sampling density; report this honestly as mono composition. Do not present source upscaling as native eye rendering.

## Source capture and freshness

Identify the actual game output from verified rendering/consumption relationships, such as the source consumed by the final game output pass. Distinguish final color from depth, G-buffer, luminance, intermediate effects, UI masks and unrelated offscreen surfaces. Support arbitrary source aspect ratios and rotating/alternating resources.

Copy the final source into app-owned storage at a verified point after its completion and before resource reuse or overwrite. Handle typed formats and MSAA resolves explicitly. Check matching dimensions, mip/subresource and sample rules before issuing a copy. Do not copy a multisampled source into a single-sample destination with `CopyResource`.

Observation must account for the game's actual command-production and execution path. Do not make D3D11 immediate-context calls from worker-thread observation hooks or race shared bookkeeping. A final-output observation may be attached to a verified execute/output boundary instead of intercepting every draw.

Retain a completed owned frame through brief capture gaps, recording its real age and generation. Invalidate it on device/session/resource-generation changes where content is no longer trustworthy. Longer source loss shows an unobtrusive world-anchored recovery message in the immersive room and a bounded diagnostic event. Never silently switch to a source already established to be the wrong black host image.

Pixel brightness is diagnostic evidence only. Legitimate dark rooms, fades and black loading frames remain valid content; do not reject images just because their pixels are dark.

## Presentation and visual quality

Immersive gameplay draws every eye pixel through its own validated asymmetric frustum. Derive source UVs from that frustum and the projection that produced the source. Verify coverage before entering immersive mode. Avoid viewport subrects that leave arbitrary black peripheral regions. Reprojection uses the source camera pose/projection identity; a newer tracked pose must not be relabeled as the pose that rendered older pixels.

Game theater is an opaque 16:9 surface. Ignore the game texture's alpha and write opaque coverage. Contain arbitrary source aspect ratios within the panel without cropping HUD content. Use the exact same letterbox transform for pointer mapping.

Calibration and settings UI are separate transparent surfaces. Choose and enforce one alpha convention through their rasterization and composition. The game alpha policy must not change their transparency. Readable angular size and eye-level anchors remain governed by the approved VR-native UI design.

One presentation state selects calibration, game menu, loading or gameplay. Keep loading in the theater until a valid completed gameplay source with the correct projection is available. Require stable readiness across the handoff, and transition at a frame boundary. No head-locked loading frame should briefly become immersive gameplay.

All visible content is composed before the final supported AA/CAS pass. Optional effects can fail independently: submit the complete unprocessed eye if a post pass cannot run. Correct color-space interpretation and opaque eye output must be maintained through every pass. Hardware support alone must not enable unverified native MSAA or TAA paths.

## State restoration and failure handling

- Preserve all D3D11 state the compositor/UI/post passes modify, including both pixel constant-buffer slots, ranged bindings, shaders, resources, samplers, output/depth/UAV bindings, predication, viewport and any changed scissor state. Restore after success and every failure path.
- Use only acquired images whose waits succeeded. A wait timeout retains its lease for a later wait; never release an unwaited image or acquire over an outstanding lease.
- End every successfully begun frame exactly once. Submit both completed eyes or no projection layer. Never submit a half-built eye pair or out-of-range image index.
- A failed shader, allocation, copy validation or optional effect does not make a partially drawn target eligible for submission. Keep the last trustworthy complete content where appropriate, or render the recovery room.
- Device removal and session loss invalidate device-bound resources and source generations. Recreate only through the frame owner at a safe lifecycle boundary.
- Repeated failures produce bounded transition diagnostics, rather than logs on every frame. Include presentation, source generation/age, source extent/format, per-eye extent and image-lease state.

## Implementation order

1. Establish focused graphics regressions for opaque theater rendering with source alpha zero, transparent UI, D3D11 state restoration and asymmetric eye sizing. Capture baseline source/swapchain facts from the existing runtime log.
2. Fix explicit content alpha handling and complete state restoration, keeping the existing frame owner.
3. Introduce per-eye resource ownership and extents end to end, including native-eye API dimensions, post surfaces, viewport/shader sizes, copies, submissions, diagnostics and user-facing resolution reporting.
4. Replace aspect/draw-count source ranking with verified output capture and owned completed-frame storage. Add source generations and freshness/recovery behavior.
5. Verify and implement coherent native scene resolution control. Connect its target extents and projection to the runtime requests without changing simulation, unrelated render targets or desktop UI dimensions accidentally.
6. Unify presentation handoff, source-projection coverage and full-image processing. Remove replaced paths after their regressions pass.
7. Build, package, inspect payload hashes and install when the game is closed under the existing authorization. The operator launches through Steam for the headset acceptance run.

## Acceptance evidence

Automated checks must cover:

- Exact 100% dimensions for equal and unequal eye recommendations, odd dimensions, scale limits and invalid recommendations.
- D3D11 WARP-rendered theater images from known RGB with alpha zero, one and fractional alpha; the opaque game result remains visible in every case. Transparent UI retains correct coverage.
- Sentinel state in both pixel constant-buffer slots restored after composition, including D3D11.1 ranges.
- Source rotation, capture gaps, legitimate black content, resource resize/recreation, unsupported copy/resolve combinations and generation invalidation.
- Asymmetric FOVs with complete immersive coverage and the source projection mapping preserved. Theater letterbox and pointer transforms agree.
- Simulated acquire/wait/release failures, shouldRender=false, partial resource construction and post-processing failure, with no invalid image release or partial eye submission.

The operator SteamVR acceptance run must demonstrate:

- Calibration -> game menu -> loading -> immersive gameplay; menus and loading remain visible and comfortable through transitions.
- Head yaw left/center/right, pitch and turning around, without direction-dependent black regions or image flashes.
- Readable game menus and HUD, 16:9 theater geometry, stable pointer mapping and transparent calibration/settings UI.
- At 100%, native scene render dimensions, allocated eye dimensions and submitted imageRect values match the resolution contract in logs/capture evidence. Eye-buffer allocation alone is insufficient evidence.
- Pause/menu/settings toggles, loading, respawn, recenter, focus loss and recovery.
- AA/CAS on/off and failure fallback, with no missing source image or UI.

Record observed results and outstanding failures explicitly. Do not mark this design implemented or visually qualified from a successful compilation alone.

## Primary references

- OpenXR per-view recommended dimensions: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrViewConfigurationView.html
- OpenXR image wait and timeout ownership: https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitSwapchainImage.html
- D3D11 copy dimensions, formats and sample requirements: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource
