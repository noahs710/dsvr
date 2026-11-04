# DSVR project instructions

DSVR is a from-scratch, full OpenXR conversion of the pinned Windows Steam
Dark Souls: Remastered build (App 1.03.1 / build 10943698; the exact tuple lives
in `src/common/version.h`). One package, one launcher, one START VR button. The
game stays authoritative for combat, movement, collision, AI, saves and
progression. The tree is a rough from-scratch conversion: parity below is the
target, never a claim of current support.

The reference standard is [ERVR](https://github.com/IlyaMez/EldenringVR): match
its scope and feel as far as DSR's systems allow. Record the status of every
row in [docs/EVIDENCE.md](docs/EVIDENCE.md).

## Parity target — ERVR mechanics, translated to DSR

| ERVR mechanic | DSVR requirement |
|---|---|
| Per-eye stereo at headset resolution; both eyes from one simulation instant; menus, loading and cutscenes on a panel | Two independently rendered game scenes from each eye's predicted pose and asymmetric projection; gameplay, AI, collision and events advance once per pair |
| First person in the body; look, lean, duck; T-pose arm calibration | Head is the camera; the body and arms are visible and follow tracked hands at measured length; native animation stays authoritative |
| Your swing is the attack: the game's own attack fires along the tracked blade; damage, poise, status and stamina come from the game; faster hits harder; weapon weight slows the turn | A tracked weapon sweep fires the game's own attack event; the game resolves every outcome; a heavy weapon turns slower |
| Shield raised blocks; moving it into a blow deflects; trigger guards | Native guard, block, deflect and parry resolution from tracked shield geometry |
| Two-hand by gripping the free hand on the hilt; open to let go | Native two-hand state from the tracked grip. DSR has no Ash of War; do not invent one |
| Staff, seal or bow: trigger casts; the spell or arrow flies where the hand points; lock-on uses the game's aim | Native cast or projectile from the tracked hand; the game's own aim when locked on |
| Per-hand weapon wheel: grip held still opens the game's own weapon icons, release over one to switch | Game weapon slots switched from tracked hands, using the game's own icons and rules |
| Belt pouch with quick items; grab and press X | DSR estus and quick items taken from a tracked belt |
| HUD on its own panel at headset resolution | HP, stamina and menus composited sharper than the game frame, positioned in the headset |
| Settings inside the game's own menu, applied mid-game | DSVR settings without leaving the headset; every change takes effect in-session |
| One launcher: checks game, runtime and headset, installs, one START VR | One launcher, one button, offline; no manual file juggling |
| Optional body haptics on the side a blow came from | Optional and never required; haptics follow confirmed outcomes, never a predicted hit |

DSR has no horse and no co-op. Do not build a Torrent or online equivalent.
A mechanic with no native DSR system does not exist yet; do not fake it.

## Conjure from existing systems

Never invent functionality. Conjure it from what DSR already has, and bend those
systems to the player's tracked body:

- A swing fires the game's own attack event; damage, poise, status and stamina
  come from the game's own resolution.
- Two-handing, casting, guarding, parrying, item use and weapon switching go
  through the game's own states and params.
- Prefer existing data before adding a hook: params, TAE events, behavior rows,
  animation events, native state. A hook is the last resort and must drive the
  existing system, not bypass it.
- Match ERVR's shape where it fits (loader to core DLL, per-eye second pass, HUD
  layer, tracked hands into native inputs) instead of inventing a new one.

Forbidden: direct HP or stat edits, spawned or synthetic enemies, fake hit
events, gesture-to-button substitutes, teleporting the player, fabricated
animation or AI states, or computing an expected outcome and writing it. An HP
setter is not a melee API. If the game has no system for it, say so.

## Evidence discipline

Label every claim: **source** (code or fixture), **static** (pinned executable
or asset inspection), **native** (identified DSR run), **headset** (operator in
a real headset). `PASS` only for the predeclared criterion on the named
candidate; `UNVERIFIED` for missing coverage; `BLOCKED` for a missing
prerequisite. Cite the exact candidate, code path or artifact. A trace without a
clean footer, stable actor identity and an independent outcome is not a melee
proof. Correct a disproven claim; never carry it forward.

## Working rules

- `PLAN.md` is the only plan. Update its single next task in place; never create
  another roadmap, phase plan, lane plan or archived plan.
- Dated results go in append-only `MODLOG.md`; current claims in
  `docs/EVIDENCE.md`. Historical detail stays in Git history.
- `docs/ENGINEERING.md` is the native design contract; `docs/TESTING.md` holds
  the offline gate and the acceptance cases.
- Minimum code, minimum files (ponytail): deletion over addition, no speculative
  abstraction, no interface with one implementation, no config for a constant.
- Preserve other agents' uncommitted work.

## Build and proof

- Offline gate: `pwsh tools/auto_verify.ps1 -SkipPackage -NoLatest -SkipDoctor`
  — builds and runs the full CTest in seconds.
- In-game proof: the operator plays with the trace on, then
  `python tools/hmd/autopilot.py analyze` reads the newest capture.
- The keyboard synthetic loop is an observer regression, not physical-melee
  proof: its input bypasses the tracked-swing path.
- Never claim a result from source inspection or synthetic input alone.

## Safety (non-negotiable)

- Offline only. Never EAC or FromSoftware online play. Steam Offline plus
  the game's own Offline Mode stay on (no firewall rules; Offline Mode makes
  no online connections until turned back on in the game).
- Disposable saves; stage and restore; verify original hashes after every
  session, including failures.
- Ship no game assets, saves, keys or credentials.
- Do not run the game, automate desktop input, deploy, commit or push unless the
  user requests it. Follow `docs/TESTING.md` for a protected run.
