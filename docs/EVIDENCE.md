# DSVR evidence snapshot

Reviewed 4 November 2026 against the current source tree and the latest
`MODLOG.md` entries. This is a status reference, not another task list. Older
address-level investigations remain recoverable in Git history. A dated capture
describes its own candidate only; it does not qualify the current checkout.

## How to read a claim

| Level | What it establishes |
|---|---|
| Source | A code path or test exists. It may never have run in the game. |
| Static | A pinned executable or asset was inspected. It does not establish a live call or effect. |
| Native | The identified game candidate produced a trace or visible result. Incomplete captures limit the claim. |
| Headset | An operator checked the identified build in a headset. Simulator images cannot supply this level. |

`PASS` applies only to the stated criterion and candidate. Missing data is
`UNVERIFIED`; a failed precondition is `BLOCKED`. A trace footer, stable actor
identity, and independent outcome are required for a qualified combat result.

## Current source boundaries

| Area | Supported statement | Evidence |
|---|---|---|
| Game identity | The code pins one Windows executable: 50,286,344 bytes, PE timestamp `0x6344CA56`, image size `0x319B000`, SHA-256 `a45aaa36…f98d7b`. Setup and the protected runner refuse a different hash. The label is App 1.03.1 / Steam build 10943698. | `src/common/version.h`; `tools/run_combat_capture.ps1` |
| XR and view | The core has OpenXR session, camera, theater and calibration paths. Their presence is source evidence; headset comfort and end-to-end coverage remain unqualified. | `src/core/xr.cpp`, `camera.cpp`, `calibration.cpp` |
| Stereo | A stereo request is refused because no native eye renderer is bound. Eye scheduling and distinct XR targets do not establish two independent game scene renders. A 4 Nov headset session observed 2888x3088 per-eye views at 90 Hz with the same centered mono image submitted to both eyes. | `src/core/render.cpp`; `src/common/config.cpp`; latest `MODLOG.md` |
| Arms | The native arm IK adapter exists. Its default is disabled with `arm_ik_log_only=true`; visual alignment and attachment behavior are not qualified by the source or old CPU receipts. | `src/common/config.h`; `src/core/arm_ik.cpp` |
| Combat | Tracked swing and guard input paths exist, but physical attack and defense bindings still report log-only. Ordinary game inputs remain separate from tracked-contact damage. A 4 Nov headset session reached gameplay and captured 251 contacts, but `equipment=0` meant no committed IK hands and the swing bridge never fired. | `src/core/input.cpp`; `src/core/combat_native.cpp`; latest `MODLOG.md` |
| Locomotion | The mod supplies camera-relative pad and roomscale input. The game's authoritative motor, collision response and resolved dodge direction are not attributed by those input writes. | `src/core/input.cpp`, `pad_math.h`, `xinput_bridge.cpp` |
| Launcher | Electron UI and the native Setup JSON bridge exist. The Play tab shows the doctor's readiness checklist, a START VR button and the session log; Install, Options and Controls hold the rest. Confirmations are styled in-page panels (no blocking browser dialogs). FPS Unlock is a required dependency: Doctor fails without it and START VR stays disabled until it is installed and chained. Offline safety is Steam Offline plus the game's own Offline Mode; no firewall rules are created. START VR starts the pinned game through Steam; a headset-qualified release has not been shown. The portable payload keeps only the en-US Electron locale. | `launcher/src/renderer/app.js`; `src/setup/setup.cpp`; `src/setup/json.cpp` |
| Release | The formal publish path still expects `.empryo/cockpit/lanes/A-K.md`, which no longer exists. Development packaging and formal release qualification are separate. | `tools/auto_verify.ps1`; `tools/publish_release.py` |

## Dated observations and their limits

- The 28 October health-writer capture includes an EnemyIns HP change from
  44 to 31 paired with a contact record. Its footer is missing and its declared
  natural-hit window ended before the trace began. Re-running the offline analyzer
  on 4 November reports
  a contact-level `PASS` with `complete=false`; this is not proof of a tracked
  swing or an accepted protected run. See `MODLOG.md` and
  `build/verification/combat-health-writer-20261028/`.
- Run 21 on 4 November has a valid footer and two player-to-distinct-object
  contact details, but no admitted character target HP read. Re-running the
  analyzer reports `UNVERIFIED`, `combat_total=42`, `contacts=2` and no HP drops.
  The ledger's later RTTI audit identified that target as outside the admitted
  character classes; the RTTI work was not repeated in this documentation audit.
  Loosening the HP reader would risk reading an unrelated object as a character.
  The trace also reports dropped records. The run is `UNVERIFIED` for enemy damage; see
  `build/verification/auto-menu-20261104-21/` and the latest `MODLOG.md`.
- Earlier static AtkParam and collision notes reported candidate record builders
  and contact paths. They were not independently rechecked here. Their
  producer-to-consumer join, ownership, ABI and damage
  submission were not established. Old offsets are leads tied to the pinned
  executable, not writable structure definitions or callable APIs.
- The 26 October interaction capture recorded three native activation events,
  but lacks a footer and reliable case windows. Reach, line of sight, prompt
  ownership, object lifetime and physical manipulation remain unqualified.
- The local OpenXR Simulator setup pins upstream commit
  `692a66de570189b29c684f48c6ef306a07d5764f`. The dated upstream run
  passed 23 noninteractive tests; live UI assertions ran, while the full suite
  failed during AMD driver teardown. Simulator success does not qualify optics,
  comfort, natural hand alignment or physical melee. See
  `tools/hmd/setup_openxr_simulator.ps1` and its `upstream-lock.json` receipt.
- The first autopilot live run on candidate r12 reached gameplay through manual
  help but aborted before combat capture. The original
  `build/verification/autopilot-20261104-1352/receipt.json` remains in
  `Preparing`; its runner output ends in a two-instance refusal. The ledger
  reports that a separate manual Restore succeeded and hash-verified the
  original files. The original receipt alone does not prove that recovery.
  The `tools/hmd/autopilot.py` phase-order and foreground-gating edit now
  passes offline checks and is sealed in candidate r13; no live run has
  validated it.
- The mod's own keyboard reads are only F6/F8/HOME/F9 and config-reload
  (`src/core/diagnostics.cpp`); it binds no `H` or `R`. The autopilot's key
  presses are the game's own bindings, so a synthetic capture observes native
  contacts but never exercises the mod's tracked-swing path.
- The tracked-swing attack bridge is source-only and gated off:
  `src/core/input.cpp` clears the attack and defence gates on `arm_ik_log_only`
  before reaching `motion_attacks`/`physical_attacks`, and a fired swing only
  logs. Clearing the flag also enables the unproven arm IK native pose write, so
  the bridge cannot be tested without proving that write first. The 4 Nov
  headset session confirmed the deeper gate: the player equipment never bound
  (`equipment=0`), so no committed IK hands existed and the bridge stayed at
  `no-committed-hands`.
- The "Restoring the view" recovery room is the camera-provenance gate in
  `src/core/xr.cpp`. The 4 Nov headset session showed the game presenting one
  frame ahead of the XR frame, which the old gate rejected; the gate now
  tolerates one display period and logs the first failing gate once per second.
  The gates are pure and unit-tested in `src/core/mono_provenance.h`.

On 4 November the current tree passed the full main and combat-lab Release
CTest; the offline analyzer, package and `autopilot.py plan` checks and the
runner's `-Mode SelfTest` also pass. The simulator staged config sets
`desktop_present = true` so the game window stays visible during a supervised
run; the no-mirror default was the documented cause of the r12 input hijack.
Candidate r13 is sealed over this tree (252-file manifest, 0 mismatches). Its
read-only `-Mode Check` passed while Steam was offline; it now refuses because
Steam is Logged On with `WantsOfflineMode 0` (logon at 17:06:52), which is the
offline-only safety gate doing its job. No runner-driven live run has validated
r13.

## External reference boundary

Souls Modding Wiki and cached Paramdex/Soulstruct data supplied vocabulary for
AtkParam, animation, models and ESD. Those sources are dated research inputs,
not proof of a native DSR runtime contract. Recheck the exact asset revision,
schema and consumer before using a field. The former long reference and its
source links remain available in Git history.
