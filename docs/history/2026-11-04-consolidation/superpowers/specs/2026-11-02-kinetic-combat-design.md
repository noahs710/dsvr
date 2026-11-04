# Kinetic combat design (slices A–D)

Date: 2026-11-02. Status: Slices A, B, C1 and C2 implemented + tested.
C2 is the input side of lane C (aim-hand selection, bow steering); D still
blocked on lane-B/C native traces (aim override, AttackBehavior dispatch).

## Goal (user-set)

Swing speed affects damage and poise (daggers fast, great weapons need
commitment), physical two-handing, shields, skills, controller-directed
spell and bow aim, no attack-button animations. A swing fires the game's
own AttackBehavior event so damage, scaling, buffs, status, poise damage,
stamina cost and hitstop stay Dark Souls Remastered rules.

## What already existed (verified, not rebuilt)

- `swing_attack_math.h`: tip-speed bars → holds native light/heavy buttons.
- `weapon_weight_math.h`: load-scaled bars, damage scale, torque gating,
  reach invariance. Tested, but not wired into the live path.
- `weapon_grip.h/.cpp`: per-weapon grip profiles + offline mass table
  (`DSVR-mass.txt`, loaded at startup). Two-hand attach is a visual
  rigid-weapon constraint only — no native stance change.
- `shield_guard_math.h`: raised-hand hold = block (LB), flick = parry (LT).
- `combat_math.h` contact classification + `damage_scale`: bench-only.
  Line 686 states applying it to a native actor is the traced binding's job.
- `combat_native.h/.cpp`: read-only equipment/stance/player binding with
  signature gates. Explicitly dispatches nothing.
- `NATIVE_MELEE_CONTRACT.md`: no verified damage/contact submission seam.
  Lane B (melee contract) is OPEN; the next step needs an operator-led,
  footer-valid `natural_hit:20` capture. No launch or input automation by
  agents.

## Slice A — implemented: torque-gated swing speed

`SwingWeaponResponse(id, bound, fallback)` (new, `weapon_weight_math.h`):
equipped weapon's mass-table response, else `cfg.torque_response`
(default 1.0 = transparent). `input.cpp` feeds the swing trigger with
`TorqueGatedSpeed(tip, response)`. Daggers (≈2.0) reach the heavy bar on a
flick; great weapons (≈0.4) need a sustained, committed swing to clear the
light bar. Heavy selection flows through the game's own attacks, so damage,
scaling, buffs, status, poise damage, stamina and hitstop stay native.
Fail-closed: unknown equipment or a corrupt table entry gates to 0 speed
(rest band, never a false fire). Tests in `test_weapon_weight.cpp`
(resolution, fallback, corruption, hammer-vs-dagger commitment).

## Slice B — implemented: physical two-handing via native toggle

Attach state rides the verified commit (`ArmIkHands.two_hand_attached`,
set from the support-grip solve, whole-struct copied by
`VerifyNaturalCopy`). `input.cpp` servos the game's own two-hand toggle:
90 ms Y + weapon-side shoulder tap whenever attach state and native stance
(3 = two-handed, closed-loop while equipment is bound) disagree, only while
the player's own Y/triggers/squeezes are quiet. `physical_two_hand` config
gate, default off like the other physical bridges; toggle sets
`calibration_safe` false and marks the pad frame active. Ordinary input, no
memory writes. Tests: `MapVrPad` Y+shoulder synthesis (both hands, menu
suppression) in `test_pad_math.cpp`; `physical_two_hand` save/load
round-trip in `test_config.cpp`.

## Slice C1 — implemented: skill flourish tap (gesture picked, reversible)

Weapon-hand flourish (short impulse that returns to rest, below the swing
bars, not a thrust) taps LT for 90 ms; the game picks skill vs parry by
equipment. Collision rules: a firing swing suppresses the flourish, thrust
shapes are excluded, same reset discipline as the swing trigger,
`physical_skills` gate default off. `SkillFlickTrigger` reuses the parry
flick bars (no new tuning surface) but carries no guard semantics.
Cast-via-swing already works through Slice A (catalyst/bow in weapon hand
fires R1). True hand-directed projectile aim needs a native aim override —
unverified, scoped to lane C, not faked here. If the flourish gesture feels
wrong, it is one gated detector to retune or replace.

## Slice D — blocked: AttackBehavior dispatch seam

Firing the game's internal attack event from a physical swing (instead of
holding its buttons) requires the traced contact→AtkParam→event-token
submission binding. Per the contract note it is unresolved and must come
from operator-led footer-valid captures. No speculative native call is
added; the button-hold bridge stays until lane B qualifies the seam.

## Slice C2 — implemented: cast-aim input side (ERVR-derived, no native)

`cast_aim_math.h` (new): `CastAimSelector` (press-driven aim hand with
expiry window + passive ranged-hand fallback, -1 = head aims) and
`BowAimSteer` (TwoHandAimState reimplemented: grip-line separation steers
the weapon, 80 deg clamp, 45 ms exponential approach, antipodal hold,
shaft-twist removal, wrist stays 1:1). Design adapted from ERVR's
`two_hand_aim.h` / cast-aim selection (GPL-3.0, Ilya Mezerowsky);
reimplemented for DSVR types, no ERVR code copied. ERVR actuates aim by
relaxing the game's own SetBulletAimAngle TAE events — that half needs
DSR's traced aim seam and is NOT faked here. Live consumer seams (both
open, both documented, neither wired): the native aim seam takes the aim
hand; the avatar support-hand solve takes the steering correction when a
bow is two-handed. Gated by `cast_aim` / `cast_aim_seconds` (default off).
Tests in `test_cast_aim.cpp`.

## Wheel decisions — implemented (belt deferred with reason)

`equip_wheel_math.h` (new): fixed per-slot angles (120 deg, mirrored per
hand), deadzone pick with sticky hold, cycle-step planning that skips
empty slots, single-press landing. Design adapted from ERVR's
`weapon_wheel_logic.h`, reimplemented; one deliberate deviation:
`EquipWheelSlotBefore` refuses an empty target outright. The caller
supplies fill states (DSR slot-listing seam open) and turns picks into
the game's own switch presses. Belt deferred: DSR has no pouch; ERVR's
belt (waist-anchored pouch slots + Event-Action chord) has no DSR analog,
so porting it would invent behavior. Tests in `test_equip_wheel.cpp`.

## Bench follow-ups — implemented (logic only, seams open)

`blade_rearm_math.h` (new): spent-contact valley rearm adapted from ERVR's
`weapon_contact.h`, reimplemented. A spent contact ends at the tip's next
speed valley (or a timeout for long weapons) and the tip must speed up
again from its minimum before a new contact opens — the follow-through
cannot open a second hit. Three-phase machine (live/spent/rearming) with a
fail-soft non-finite feed. Consumer seam: `EvaluateBladeContact`'s rehit
gate; not wired until a headset pass approves the swap. Tests in
`test_blade_rearm.cpp`.

`weapon_inertia_math.h` (new): inertial lag adapted from ERVR's
`StepWeaponOrientation`, reimplemented on DSVR AimQuat/Vec3. Weight
resists bending about the grip, roll about the shaft stays under control,
no stored spring velocity (monotone approach, tracking wins, 45 deg held
lag clamp). Consumer seam: the avatar support-hand/weapon solve; not wired
until feel-tuning with a headset pass. Tests in `test_weapon_inertia.cpp`.

Not ported: ERVR gestures (DSVR's swing bars cover it), movement facing
(needs the native action-cancel seam), belt (no DSR pouch analog), chord
timing (DSVR's ButtonPairChord + 90 ms taps are the same shape already),
haptics events (need the native contact seam; bench HitHaptic exists).

## Stereo — deferred (engine-divergent)

ERVR renders per-eye cameras on DX12. DSR is DX11 and DSVR is mono-first;
the stereo design transfers but the implementation is a render project,
not a port. Last in the adaptation order.

## Verification

Gate: 23/23 green (`weapon|swing|shield|combat|gesture|pad_math|
skill_flick|config|tracking|vr_heading|arm_pose_math|camera_math|mod_menu`),
`dsvr_core` builds clean. `test_input`/`test_calibration` link errors are
pre-existing (other tabs' symbols). Live capture path:
[operator run-book](../../OPERATOR_CAPTURE_RUNBOOK.md). `test_input` still does not
link (pre-existing missing `CameraNativeCutscene` stub, other tab's lane);
the new wiring adds one more unresolved symbol there (`WeaponMassGet`) and
is covered instead by `test_weapon_weight`.
