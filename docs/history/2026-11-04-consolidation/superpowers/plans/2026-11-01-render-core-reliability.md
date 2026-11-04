# Render core implementation

Approved design: ../specs/2026-11-01-render-core-reliability-design.md

1. Restore the existing WARP compositor fixture to the current API and add failing graphics/state regressions.
2. Fix opaque theater/transparent UI composition and pixel constant-buffer restoration.
3. Own resolution, swapchains and processing surfaces per eye; carry exact extents through native API, shader viewport and submission.
4. Capture the resource consumed by the final output pass when verified; keep an owned completed image across observation gaps; track source generation and freshness.
5. Trace coherent native scene allocation/projection control before modifying game target resolution.
6. Run focused WARP and XR lifecycle checks, build Release, package and install while the game is closed.
7. Operator launches through Steam for headset acceptance; record unverified requirements without claiming native source resolution or stereo.

Do not commit or launch the game. Preserve unrelated work in the shared checkout.

## Implementation receipt

- Steps 1–3: implemented. WARP regression demonstrated b1 restoration failing before the fix; both b0/b1 and D3D11.1 ranges now pass. Actual room shader alpha policies, per-eye requested/allocated/submitted dimensions, full-eye mapping and native-eye API extents are covered.
- Step 4: partially implemented. Final-output bound input 0 takes priority over candidate ranking. Gaps retain an owned image with its camera packet and age, and stale/provenance-invalid/copy-failed frames use a recovery room. Binding alone is not proof of shader consumption; normalized-color/no-depth candidate discovery remains a fallback pending live verification. HDR intermediates are excluded from that display-source ranking.
- Step 5: open. The live Ghidra MCP endpoint was unavailable. Read-only headless DSRFull discovery succeeded and found resolution log strings and native render-target draw-plan types, but did not establish a coherent scene-allocation/projection ABI. No native game render dimensions were changed.
- Step 6: done for this development build. Release compilation and 11 focused checks passed; 19 payload checksums passed. Versioned ZIP and installed Steam DLL match. See verification report for hashes.
- Step 7: pending operator Steam launch. Native scene resolution, final-source identity/color interpretation and headset comfort/transition quality are not qualified by WARP tests.

The full design remains incomplete until steps 4, 5 and 7 meet the acceptance contract.
