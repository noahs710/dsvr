# DSVR development snapshot

DSVR is an unofficial, offline-only VR mod for the Windows Steam copy of Dark
Souls: Remastered. This checkout is development work, not a qualified release.
The code pins App 1.03.1 / Steam build 10943698 and the exact executable hash
in `src/common/version.h`; other executable builds are refused.

The source contains an OpenXR path, first-person camera, theater presentation,
calibration, arm IK, native Setup, and an optional Electron launcher. Native
two-eye scene rendering, tracked-contact melee and shield damage, headset
comfort, and campaign play have not passed acceptance. The launcher's Play tab
shows a readiness checklist and a START VR button that starts the game through
Steam; a headset-qualified release has not been shown. [The evidence snapshot](docs/EVIDENCE.md) distinguishes code from
dated game and simulator observations.

## Requirements

- Windows x64, the pinned Steam game build, and an OpenXR runtime.
- DSR-FPS-Unlock (required dependency): install it from Setup before starting VR.
- Offline play. Do not use this mod in online sessions. Put Steam in Offline
  Mode and set Dark Souls to Offline Mode in the game; Offline Mode makes no
  online connections until you turn it back on in the game.
- The matching DSVR package and Setup executable; mixed build files are not a
  valid install.

## Install and remove a development package

Close the game. Extract one matching package and run `DSVR-Setup.exe`; use its
game-folder selection and verification before installing. Start the game through
Steam while offline. To remove the mod, close the game and use Setup's uninstall
action. Keep Setup's backup files until removal and restoration are confirmed.
Installation is not a headset or gameplay qualification.

`DSVR-Setup.exe --doctor` checks the installed state. `DSVR.log` in the game
folder is the main runtime log. When reporting a problem, include the log,
package version and selected runtime, but no saves or game files.

## Development and attribution

[PLAN.md](PLAN.md) is the only plan. [docs/ENGINEERING.md](docs/ENGINEERING.md)
describes design boundaries; [docs/TESTING.md](docs/TESTING.md) covers protected
runs and acceptance cases; `MODLOG.md` records dated results.

DSVR source is MIT licensed. Third-party notices are packaged with the code.
The game and its assets belong to FromSoftware and Bandai Namco; DSVR packages
no game assets. DSR-FPS-Unlock is a required third-party dependency, installed
separately and hash-verified; see [docs/ATTRIBUTION.md](docs/ATTRIBUTION.md). OpenAI Codex
contributed to this development snapshot.
