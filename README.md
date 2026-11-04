# DSVR development build

DSVR is an unofficial, offline-only VR mod for the Windows Steam version of
Dark Souls: Remastered. This package is a development snapshot, not a completed
or formally qualified release. Native stereo, physical combat, and headset
comfort remain under active qualification.

## This snapshot

- Adds passive D3D11 render-path diagnostics for multisampled color/depth
  targets, draw calls, clears, copies, and resolves.
- Reports 2x/4x format support separately from observed render-path candidates.
- Does not enable native MSAA or TAA. No game content or save files are included.

## Requirements

- Windows x64 and the Steam copy of Dark Souls: Remastered.
- Supported game build: App 1.03.1 / Steam build 10943698.
- A working OpenXR runtime. SteamVR and Meta Quest Link are supported host
  options; select the runtime you intend to use before starting the game.
- Offline play only. Do not use DSVR for online play.

## Install and remove

1. Close the game.
2. Extract this archive and run `DSVR-Setup.exe`.
3. Select the Dark Souls: Remastered install folder and follow Setup's prompts.
4. Start the game through Steam. To remove DSVR, close the game and run Setup's
   uninstall action; it restores files owned by the installer.

Keep the original archive so Setup can reinstall the same build. Do not delete
backup files created by Setup while installation is active.

## Troubleshooting

- Run `DSVR-Setup.exe --doctor` to check the install and pinned game build.
- The runtime log is `DSVR.log` in the game folder. The new render observer
  writes `DepthProbeFrame`, `DepthProbeTarget`, and `DepthProbeTransfer` entries.
- Include the log, `DSVR-VERSION.txt`, and the selected OpenXR runtime when
  reporting a launch or rendering issue. Do not send saves or game files.

## Credits and license

DSVR source is MIT licensed; third-party dependency notices are in
`THIRD_PARTY.md` and `licenses/`. Dark Souls: Remastered is owned by
FromSoftware and Bandai Namco; DSVR includes no game assets.

AI assistance: OpenAI Codex (GPT-6) contributed to code and documentation.
This snapshot has not received final headset acceptance.

See `docs/ENGINEERING.md`, `docs/TESTING.md`, and `docs/ATTRIBUTION.md` for
architecture, verification policy, and asset scope.
