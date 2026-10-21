# DSVR — Dark Souls: Remastered in first-person VR

**Master product plan · 16 October 2026 · Windows/Steam · offline only**

Build a game worth playing: true stereo, independent 6DoF head tracking,
controller-driven native arms and weapons, physical melee, haptics and readable
VR interfaces. Use ERVR's mechanisms as a reference, with fresh DSR-specific
code. Do not turn controller swings into button-triggered attack animations.

## Reality now

**This remains a development candidate.** Current source adds calibration once per launch,
vertical elbow tracking, weapon-local grip profiles, grip-held visual two-handing,
a Home-key control/debug/log menu, native UI suppression and reversible motion-blur
enforcement. Measurements and the physical tracking origin survive deaths and loads.
The integrated offline suite passes 76 tests. Forearm-only visibility is currently
unavailable: its bone-collapse implementation stretched mixed-weight upper-arm skin
and has been removed while forearm geometry filtering is investigated. A native
face/headgear/hair draw-record filter replaces head-bone collapse; its current
candidate still requires headset verification.
Native stereo/AFR and native physical damage/block/parry remain unbound; no
72/90 FPS or headset-alignment acceptance is claimed. See the current execution
status in [engineering](docs/ENGINEERING.md) and evidence in [testing](docs/TESTING.md).

PC captures verified the disposable character's 80-bone layout and native
animation-to-body copy order. The latest protected Quest 3 run confirmed improved
forearm/wrist feel but failed body visibility: stretched upper arms and intermittent
body/camera obstruction remain unacceptable. Gameplay measured about 15–16 FPS;
disabling IK alone did not resolve it. New CPU stage timings separate native camera,
retargeting, XR host work and presentation. Full-body retargeting, reliable head/helmet
exclusion, true stereo and physical combat remain unfinished.
Details: [handoff](MODLOG.md).

User build/test default: **90 FPS game target and 90 Hz OpenXR target**. New
configs request 90; when a session is live the runtime's actual period is
the pacing authority (see `RateMatch`/`RateEvidence` in `DSVR.log`) and the
config value is only the fallback. Each native run must record actual game FPS and XR period.
Runtime rate selection is implemented as measurement + effective-rate
authority; a pinned native limiter hook is implemented but awaits runtime qualification; a requested value is not evidence of 90 FPS.

**Test the current build:** run `.\tools\auto_verify.ps1` (configure +
build + CTest + doctor). On a fully green run it always refreshes
`dist\DSVR-latest\` + `DSVR-latest-x64.zip` with
`DSVR-latest-verification.json` (hashes, test totals) and
`DSVR-latest-ctest.txt`. Formal versioned archives stay immutable and are
minted only with `-Package -Version <new-version>` after their gate.

| Area | Implemented | Still missing |
|---|---|---|
| Rendering | D3D11/OpenXR mono, optional single-owner ReShade adapter; separate XR eye-frame API and native projection math | Connecting prediction and two native eye renders; safe replay and pacing |
| View/input | First-person hook, independent heading, coherent head/hand samples, controller-to-XInput bridge | Resolved camera ownership, measured walking stability, full 6DoF acceptance |
| Body | Post-animation position/grip adapter, weapon-grip offsets, measured skeleton mapping, selective head hiding; disabled/log-only by default | Correct rendered arm/wrist/equipment alignment, stance changes and torso correction |
| Combat/haptics | Kinematics/contact math and OpenXR vibration output API | Native contact/damage/block/parry integration and event-driven feedback |
| UI/setup | Home-key extensible native menu, controller navigation, calibration screen, bounded logging GUI, reversible blur preference and pinned UI draw suppression | Native/headset qualification, separate VR HUD, protected automated lab |

Offline tests do not establish these missing features. The reported walking
fusion/lag remains unresolved. Older headset approval of look axes/FOV does not
validate newer builds, true stereo or physical combat.

Developer progress: desktop observer now activates on F9 after startup; early
native-hook activation crashed, while deferred activation reached a loaded scene
and completed two native captures. Gameplay adapters now wait for the selected
host's first accepted frame; subsequent runs reached the disposable scene.
The latest source adds a tested game-local XInput IAT fallback for persistent
trampoline allocation failure, retains safe callbacks across shutdown, and
discards queued turning when its single right-stick query fails. Native pad
recovery remains unqualified. Mono image waits/releases now retain ownership
through errors and bounded teardown retries. Release build and **53/53 CTest PASS** are source
evidence, not headset acceptance.
Invalid legacy writes
past the CameraMan container have been removed; follow-source edits remain.
RenderDoc now captured
and replayed one real Asylum gameplay frame: main color/depth targets, VS slot-0
camera matrix and separate late HUD draws are identified. Next establish the safe
native eye-render bracket; projection math now accounts for the captured off-axis
terms and preserves native depth. It is not connected to native drawing. Broad tracing is retired
for this investigation. Saves/install restored; Steam remains running between runs.
Active camera lifetime and stereo replay remain unverified; this is still mono.

## The product contract

- **See:** runtime-derived left/right views and asymmetric projections at the
  same simulation instant; correct scale, occlusion, lighting and shadows.
- **Move:** head direction independent of native movement-facing and lock-on;
  snap/smooth turning, head/controller-directed movement, seated/standing
  calibration. Preserve real head roll; suppress artificial animation camera roll.
- **Embody:** actual player arms, held native stance, tracked hands, torso/spine
  correction and native weapons/shields. Preserve game collision/root movement.
  Held/inferred legs are not measured full-body tracking or terrain-aware foot IK.
- **Fight:** hand-led slashes/thrusts, native collision/damage, stamina/poise,
  directional shield blocking and physical parrying. No resting-contact damage,
  frame-rate damage farming or repeated stagger every render.
- **Feel:** distinct, adjustable hit/block/parry/hurt feedback on the correct hand;
  feedback follows confirmed outcomes, not merely a fast controller movement.
- **Use:** controller-accessible menus, stable readable UI, separate HUD,
  recenter/calibration, reliable tracking recovery and safe cinema fallback.

No online play, DRM/anti-cheat bypass, game-asset redistribution, full physics
enemy reconstruction, mesh slicing or dismemberment is in scope. Commercial
quality means dependable supported mechanics, not promising every VR feature.

## Delivery: three meaningful test releases

These are feature gates, **not existing builds or promised dates**. Assign a new
version only when its gate passes. Internal experiments are not user releases.

| Release | Playable package | Why ask for headset time? |
|---|---|---|
| **R1 — First playable** | True stereo; stable independent view; tracked native arms; one supported sword + shield; slash/thrust damage, stamina, block/parry and hit haptics; stick locomotion/turning; calibration; readable automatic menus/HUD; load/death/tracking recovery | A short complete encounter: does looking, moving, hitting and defending feel coherent and comfortable? |
| **R2 — Combat breadth** | R1 plus verified weapon-family geometry, dual wield/two-hand grip, bows/crossbows/catalyst aiming, equipment changes, left-handed parity, quick slots and differentiated feedback | Do varied loadouts and physical interactions remain usable and balanced? |
| **R3 — Campaign candidate** | R2 plus area/boss/menu compatibility, ladders/rolls/backstabs/ripostes/cutscene handling, comfort/accessibility presets, performance tuning, install/rollback and support bundle | Is this a dependable longer-session game rather than an arena demo? |

QoL priorities: R1 one-action recenter, seated reach/height, remappable controls,
adjustable haptics and stable loading presentation; R2 wrist/quick-slot UI,
off-hand grip calibration and mirror options; R3 optional auto-resolution with
hysteresis, reduced-motion settings and local diagnostic export. Collision-safe
room-scale body movement is an investigated extension, not permission to walk
the camera through walls. Core combat/comfort comes before decorative extras.

## Build order — game-modding loop

1. **Recon → safe lab:** use the pinned game and existing loader. Establish an
   offline disposable profile and reversible deployment; resolve camera-object
   identity and capture actual main-scene GPU view consumers in one bounded
   observational experiment. Do not spend another milestone on generic tooling.
2. **One stereo scene:** predict before command production, render two eyes with
   no duplicate simulation/events/Present, and prove image/view correspondence.
   Set pacing from measurements, not an untested FPS-unlock patch.
3. **One native body/loadout:** verify post-animation pose/attachment order; drive
   the player's own arms, sword and shield in the same render frame as the view.
4. **One complete encounter:** contact → native damage/stamina/reaction → haptics;
   finish UI/lifecycle recovery, then qualify **R1**. Widen only after this works.
5. **Broaden → campaign → package/showcase:** R2/R3, repeatable native scenarios,
   a reviewed 20–45 second gameplay clip, dependency notices and clean install.
   Publishing needs permission; implementation and useful protected launches do not.

**Stop rules:** change approach after three identical failures; do not suppress
native assertions, guess offsets, fake readiness or ask the user to diagnose
something an automated capture can answer. Summon them for real blockers,
credentials that cannot be automated, or a ready comfort/feel assessment.

## The only supporting docs

- [MODDING_PLAN.md](MODDING_PLAN.md) — recon, source-backed findings and unresolved native contracts.
- [Engineering](docs/ENGINEERING.md) — implementation order, ownership and feature acceptance.
- [Testing and operation](docs/TESTING.md) — automation, release gates, build/install/support.
- [MODLOG.md](MODLOG.md) — compact current handoff, evidence and next action.

Maintain these five documents in place. No parallel specs, approval chains,
task-number backlogs or additional handoffs. Source/tests define exact APIs and
config defaults; machine logs live under `build/`, immutable archives in `dist/`.
Third-party license notices remain separate. Original code: MIT, AI-assisted;
see `LICENSE` and `third_party/README.md` for attribution.
