# Autonomous HMD testing

Decision updated 3 November 2026: the user selected Elliott Tate's OpenXR-Simulator and superseded every earlier driver. The active implementation and operating instructions are in [Autonomous HMD testing on Windows](../../AUTONOMOUS_HMD_TESTING.md).

Use the pinned Windows runtime and MCP server for actual OpenXR/D3D11 sessions, swapchains, composition, head/controller poses and actions, UI interaction and diagnostics. Keep the controller-state extension, focus/log freshness, isolated performance captures and Windows NumPy startup fix in the exact patch. Provision isolated x64 Python 3.11 dependencies from the lock and verify real stdio communication during setup.

Run production XR and input acquisition modules in a bounded host with diagnostic scene adapters. Retain eye captures, submission metadata, controller observations, performance captures, memory samples, artifact hashes and explicit agent visual review. Select runtime/data paths through child-process environment variables, preserve system OpenXR registration and clean up only owned processes and run-local commands.

One command provisions/builds, runs simulator/DSVR checks, drives the live MCP matrix and emits evidence. Required failures fail the run. Product packages exclude the host and telemetry layer. Synthetic success does not qualify DSR native damage, physical melee, comfort, real device bindings, headset optics or hardware performance. The modding plan retains those gates.
