# PowerShell 7 development tooling design

Date: 2026-10-26

## Goal

Run repository development scripts and test entry points under PowerShell 7 (`pwsh`). Keep PowerShell 5.1 compatibility only where the portable product installer requires it. Convert the local capture/restore utility to PowerShell 7 as requested.

## Scope and behavior

- `tools/auto_verify.ps1`, `tools/verify_preflight.ps1`, and `tools/run_combat_capture.ps1` are PowerShell 7 tools. Each should fail early with a clear message when invoked by Windows PowerShell 5.1, and development/test documentation and CTest registration should invoke `pwsh` only.
- Remove compatibility branches and comments that exist solely for PowerShell 5.1/.NET Framework from these development scripts. Keep transaction safety and receipt behavior intact while simplifying to PowerShell 7/.NET APIs.
- Preserve PowerShell 5.1 compatibility in the portable product installer, if any installer code uses PowerShell. Do not impose a PowerShell 7 dependency on end users installing the product.
- Tests should verify the supported runtime boundary and meaningful script behavior without launching the game or changing user saves/install state.

## Alternatives considered

1. Enforce PowerShell 7 for all development/test scripts and retain PowerShell 5.1 only in the product installer. **Recommended:** gives one explicit development runtime and preserves installer portability.
2. Modernize script syntax without runtime enforcement. Smaller change, but Windows PowerShell can still launch the scripts and fail partway through a transaction.

## Validation

Run the script self-test under `pwsh`, configure/build and run the focused CTest registration, and inspect registered test names with `ctest -N`. Do not run the protected game transaction as part of this tooling change.

## Review

- [x] Scope is limited to development PowerShell tooling and its runtime/test documentation.
- [x] No native conversion milestone or unrelated dirty work is changed by this design.
- [x] Installer compatibility exception is explicit.
