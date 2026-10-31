#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

#include "camera_math.h"
#include "combat_math.h"
#include "gesture_math.h"

namespace dsvr {

// The native damage contract, transcribed from the pinned research cache
// (docs/SOULSMODDING_REFERENCE.md -> build/research/soulsmodding-20261024,
// pages ds1-refmat:attack-animation-schema and ds1-refmat:param:atkparam).
// This header owns no game memory: it is the vocabulary the tracked-blade
// decision uses so a qualified contact can be expressed in the game's own
// terms (attack identity + hit slot + correction domain) instead of a
// invented HP number. Nothing here dispatches damage; the native owner does.

// --- Attack animation identity ---------------------------------------------
// id = wepmotionCategory * 10000 + hand + action, plus 5 when the weapon is
// at zero durability. Hand is 3000 (right), 4000 (both), 5000 (left).
constexpr int kAttackCategoryScale = 10000;
constexpr int kAttackHandRight = 3000;
constexpr int kAttackHandBoth = 4000;
constexpr int kAttackHandLeft = 5000;
constexpr int kAttackZeroDurability = 5;
// Every listed action is below this, so the hand field is exactly the part of
// the id at or above it (3000 / 4000 / 5000) and the two never overlap.
constexpr int kAttackActionFieldWidth = 1000;


enum class AttackAction : int {
  QuickA = 0,
  QuickB = 1,
  QuickARepeat = 2,
  StatsNotMet = 30,
  QuickAWhiff = 40,
  QuickBWhiff = 41,
  QuickARepeatWhiff = 42,
  Kick = 100,
  DeflectedA = 150,
  DeflectedB = 200,
  RiposteA = 201,
  RiposteB = 202,
  RiposteC = 203,
  StrongA = 300,
  StrongAfterQuick = 301,
  StrongB = 310,
  BackstabA = 400,
  BackstabB = 401,
  BackstabC = 402,
  BackstabD = 403,
  Running = 500,
  Jumping = 600,
  JumpingAlt = 640,
  PlungeStart = 800,
  PlungeContinue = 801,
  PlungeEnd = 810,
  QuickAfterEvade = 900,
  QuickAfterEvadeWhiff = 940,
  HornetRingRiposte = 980,
};

// True for the actions that can carry a player blade to a target. Whiff,
// deflect, kick and stats-not-meet rows exist in the same id space but are
// not tracked swings, so a contact candidate must never claim them.
inline bool ActionCanCarryBlade(AttackAction a) {
  switch (a) {
    case AttackAction::QuickA:
    case AttackAction::QuickB:
    case AttackAction::QuickARepeat:
    case AttackAction::StrongA:
    case AttackAction::StrongAfterQuick:
    case AttackAction::StrongB:
    case AttackAction::Running:
    case AttackAction::Jumping:
    case AttackAction::JumpingAlt:
    case AttackAction::QuickAfterEvade:
      return true;
    default:
      return false;
  }
}

// The action values the pinned schema actually lists. The zero-durability
// modifier is a flat +5 on the whole id, so recovering it means asking which
// interpretation lands on a real row: a digit heuristic is wrong because
// QuickB is 1 (worn form 6) and QuickARepeatWhiff is 42 (worn form 47).
inline bool IsListedAttackAction(int value) {
switch (static_cast<AttackAction>(value)) {
  case AttackAction::QuickA:
  case AttackAction::QuickB:
  case AttackAction::QuickARepeat:
  case AttackAction::StatsNotMet:
  case AttackAction::QuickAWhiff:
  case AttackAction::QuickBWhiff:
  case AttackAction::QuickARepeatWhiff:
  case AttackAction::Kick:
  case AttackAction::DeflectedA:
  case AttackAction::DeflectedB:
  case AttackAction::RiposteA:
  case AttackAction::RiposteB:
  case AttackAction::RiposteC:
  case AttackAction::StrongA:
  case AttackAction::StrongAfterQuick:
  case AttackAction::StrongB:
  case AttackAction::BackstabA:
  case AttackAction::BackstabB:
  case AttackAction::BackstabC:
  case AttackAction::BackstabD:
  case AttackAction::Running:
  case AttackAction::Jumping:
  case AttackAction::JumpingAlt:
  case AttackAction::PlungeStart:
  case AttackAction::PlungeContinue:
  case AttackAction::PlungeEnd:
  case AttackAction::QuickAfterEvade:
  case AttackAction::QuickAfterEvadeWhiff:
  case AttackAction::HornetRingRiposte:
    return true;
  default:
    return false;
}
}

// One attack ANIMATION's identity. This is not a BehaviorParam or AtkParam
// row ID: the native builder selects those separately (1403C93B0 ->
// 140528A30 -> BehaviorParam.refId -> 140528910). `known` flags mark a
// decode that parsed structurally but landed on a value the schema does not
// list; the caller must treat those as unverified, never as a free pass.
struct AttackIdentity {
  int category = 0;  // wepmotionCategory of the weapon row
  int hand = kAttackHandRight;
  AttackAction action = AttackAction::QuickA;
  bool zero_durability = false;
  bool hand_known = true;
  bool action_known = true;

  int Encode() const {
    if (category < 0 || !hand_known || !action_known ||
        (hand != kAttackHandRight && hand != kAttackHandBoth && hand != kAttackHandLeft) ||
        !IsListedAttackAction(static_cast<int>(action))) return -1;
    const auto id = std::int64_t(category) * kAttackCategoryScale + hand +
        static_cast<int>(action) + (zero_durability ? kAttackZeroDurability : 0);
    return id <= std::numeric_limits<int>::max() ? static_cast<int>(id) : -1;
  }

  static AttackIdentity Decode(int id) {
    AttackIdentity out;
    if (id < 0) {
      out.hand_known = out.action_known = false;
      return out;
    }
    out.category = id / kAttackCategoryScale;
    const int rest = id % kAttackCategoryScale;
    // Hand is the thousands digit: every listed action is < 1000, so the hand
    // and action fields never overlap. Dividing by 100 instead would let an
    // action like StrongA (300) masquerade as part of the hand field.
    const int hand = (rest / kAttackActionFieldWidth) * kAttackActionFieldWidth;
    out.hand_known =
        hand == kAttackHandRight || hand == kAttackHandBoth ||
        hand == kAttackHandLeft;
    out.hand = out.hand_known ? hand : 0;
    const int action = rest - hand;
    // Prefer the unmodified reading when both interpretations are real rows.
    // No two listed actions differ by exactly 5, so that tie cannot occur; the
    // test asserts it rather than trusting the enumeration by eye.
    if (IsListedAttackAction(action)) {
      out.action = static_cast<AttackAction>(action);
    } else if (IsListedAttackAction(action - kAttackZeroDurability)) {
      out.zero_durability = true;
      out.action = static_cast<AttackAction>(action - kAttackZeroDurability);
    } else {
      out.action_known = false;
      out.action = AttackAction::QuickA;
    }
    return out;
  }
};

// --- AtkParam row layout ---------------------------------------------------
// One row is 0x80 bytes and carries four hit slots. The offsets are recorded
// so a future native reader validates its struct against one table rather
// than folklore, and so a transcription slip fails at compile time.
constexpr std::uint32_t kAtkParamRowBytes = 0x80;
constexpr int kAtkParamHitSlots = 4;

namespace atk_param {
constexpr std::uint32_t kHitRadius[kAtkParamHitSlots] = {0x00, 0x04, 0x08, 0x0c};
constexpr std::uint32_t kHitDummyTip[kAtkParamHitSlots] = {0x2c, 0x2e, 0x30, 0x32};
constexpr std::uint32_t kHitDummyTail[kAtkParamHitSlots] = {0x34, 0x36, 0x38, 0x3a};
constexpr std::uint32_t kHitType[kAtkParamHitSlots] = {0x6a, 0x6b, 0x6c, 0x6d};
constexpr std::uint32_t kHitPriority[kAtkParamHitSlots] = {0x6e, 0x6f, 0x70, 0x71};
constexpr std::uint32_t kSpEffectId[5] = {0x18, 0x1c, 0x20, 0x24, 0x28};
constexpr std::uint32_t kKnockbackDist = 0x10;  // f32, m/s
constexpr std::uint32_t kHitStopTime = 0x14;    // f32, seconds
constexpr std::uint32_t kBlowCorrection = 0x3c;
constexpr std::uint32_t kAtkPhysCorrection = 0x3e;
constexpr std::uint32_t kAtkMagCorrection = 0x40;
constexpr std::uint32_t kAtkStamCorrection = 0x46;
constexpr std::uint32_t kGuardAtkRateCorrection = 0x48;
constexpr std::uint32_t kGuardBreakCorrection = 0x4a;
constexpr std::uint32_t kAtkSuperArmorCorrection = 0x4e;
constexpr std::uint32_t kAtkPhys = 0x50;
constexpr std::uint32_t kAtkStam = 0x58;
constexpr std::uint32_t kGuardAtkRate = 0x5a;
constexpr std::uint32_t kGuardBreakRate = 0x5c;
constexpr std::uint32_t kAtkSuperArmor = 0x5e;
constexpr std::uint32_t kDmgLevel = 0x72;
// 0x73 selects a map/world hit context for the attack (cache transcription:
// ds1-refmat mapHitType with ATK_PARAM_MAP_HIT 0 Normal / 1 Precision /
// 2 Not on the map / 3 Map, not an asset; not DSR-verified, and not a claim
// about 1403C72D0's 23/24/25 message selection). 0x74 (guardCutCancelRate)
// is deliberately left unnamed until its own transcription is checked.
constexpr std::uint32_t kMapHitType = 0x73;
constexpr std::uint32_t kAtkAttribute = 0x75;
constexpr std::uint32_t kHitSourceType = 0x7c;
constexpr std::uint32_t kFlags = 0x7e;  // bitfield byte, 0x7f is padding
}  // namespace atk_param

static_assert(atk_param::kDmgLevel + 1 == atk_param::kMapHitType &&
                  atk_param::kMapHitType < atk_param::kAtkAttribute,
              "0x73 sits between kDmgLevel and kAtkAttribute");
static_assert(atk_param::kFlags + 2 == kAtkParamRowBytes,
              "AtkParam row layout transcribed wrong");
static_assert(atk_param::kHitRadius[0] == 0x00 && atk_param::kHitRadius[3] == 0x0c,
              "AtkParam hit radii are four consecutive f32 slots");
static_assert(atk_param::kHitDummyTail[0] == atk_param::kHitDummyTip[0] + 8,
              "AtkParam tip/tail dummy ids are parallel s16 arrays");

enum class NativeHitType : std::uint8_t {
  Normal = 0,      // tip
  Middle = 1,
  Root = 2,
  MapCollision = 3,
};

// AtkParam byte 0x73: which map/world context the attack hit selects.
// Transcribed from the research cache (ds1-refmat mapHitType,
// ATK_PARAM_MAP_HIT); not DSR-verified and not bound to any native call.
enum class NativeMapHitType : std::uint8_t {
  Normal = 0,
  Precision = 1,
  NotOnMap = 2,
  MapNotAsset = 3,
};

enum class NativeAtkAttribute : std::uint8_t {
  None = 0,
  Slash = 1,
  Strike = 2,
  Thrust = 3,
  Push = 4,
};

enum class NativeHitSource : std::uint8_t {
  Weapon = 0,
  Body = 1,
  PartBase = 2,  // 2..33 are Part 0..31
};

enum class NativeDamageLevel : std::uint8_t {
  Default = 0,
  NoStagger = 1,
  Addition = 3,
  HitStop = 4,
  SmallStagger = 5,
  LargeStagger = 7,
  BlownAway = 10,
};

// The 0x7e flag byte, decoded. Kept as plain bools so the contact decision
// can read intent without re-deriving bit positions.
struct NativeAttackFlags {
  bool disable_guard = false;
  bool disable_stamina_attack = false;
  bool disable_hit_sp_effect = false;
  bool ignore_miss_swing_notify_ai = false;
  bool repeat_hit_sfx = false;
  bool is_arrow_attack = false;
  bool is_ghost_attack = false;
  bool disable_no_damage = false;

  static NativeAttackFlags FromByte(std::uint8_t b) {
    NativeAttackFlags f;
    f.disable_guard = (b & 0x01u) != 0u;
    f.disable_stamina_attack = (b & 0x02u) != 0u;
    f.disable_hit_sp_effect = (b & 0x04u) != 0u;
    f.ignore_miss_swing_notify_ai = (b & 0x08u) != 0u;
    f.repeat_hit_sfx = (b & 0x10u) != 0u;
    f.is_arrow_attack = (b & 0x20u) != 0u;
    f.is_ghost_attack = (b & 0x40u) != 0u;
    f.disable_no_damage = (b & 0x80u) != 0u;
    return f;
  }

  std::uint8_t ToByte() const {
    return static_cast<std::uint8_t>(
        (disable_guard ? 0x01u : 0u) | (disable_stamina_attack ? 0x02u : 0u) |
        (disable_hit_sp_effect ? 0x04u : 0u) |
        (ignore_miss_swing_notify_ai ? 0x08u : 0u) |
        (repeat_hit_sfx ? 0x10u : 0u) | (is_arrow_attack ? 0x20u : 0u) |
        (is_ghost_attack ? 0x40u : 0u) | (disable_no_damage ? 0x80u : 0u));
  }
};

// Our gesture label expressed in the game's attribute enum, so a tracked
// swing and a native row agree without a second translation at the hit site.
inline NativeAtkAttribute AttributeFromGesture(GestureKind kind) {
  return kind == GestureKind::Thrust ? NativeAtkAttribute::Thrust
                                     : NativeAtkAttribute::Slash;
}

// One hit slot in world space. The native row stores dummy-polygon ids; the
// caller that owns the model resolves them to endpoints (that resolution is
// the open native gate, not this header). `capsule == false` means the row
// describes a sphere at `tip`.
struct NativeHitSlot {
  int id = -1;
  Vec3 tip{0, 0, 0};
  Vec3 tail{0, 0, 0};
  float radius = 0.0f;
  int tip_dummy_id = -1;
  int tail_dummy_id = -1;
  NativeHitType hit_type = NativeHitType::Normal;
  NativeHitSource source = NativeHitSource::Weapon;
  std::uint8_t priority = 0;
  bool capsule = false;

  bool valid() const {
    if (id < 0 || !FiniteVec(tip) || !(radius > 0.0f) || !std::isfinite(radius))
      return false;
    return !capsule || FiniteVec(tail);
  }
};

// Shortest distance between two segments, or -1.0f for non-finite input.
// The minimum is either an endpoint-to-segment pair or the two interior line
// projections. Fixed-count alternating projections miss shallow crossings.
// Double intermediates and the cross-product determinant avoid cancellation
// for nearly parallel blades and overflow for finite float coordinates.
inline float SegmentSegmentDistance(Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
  if (!FiniteVec(a) || !FiniteVec(b) || !FiniteVec(c) || !FiniteVec(d))
    return -1.0f;
  using D3 = std::array<double, 3>;
  const auto delta = [](Vec3 x, Vec3 y) -> D3 {
    return {double(x.x)-y.x, double(x.y)-y.y, double(x.z)-y.z};
  };
  const auto dot = [](D3 x, D3 y) {
    return x[0]*y[0] + x[1]*y[1] + x[2]*y[2];
  };
  const auto cross = [](D3 x, D3 y) -> D3 {
    return {x[1]*y[2]-x[2]*y[1], x[2]*y[0]-x[0]*y[2],
            x[0]*y[1]-x[1]*y[0]};
  };
  const auto point_distance2 = [&](Vec3 point, Vec3 start, Vec3 end) {
    const D3 line=delta(end,start), offset=delta(point,start);
    const double length2=dot(line,line);
    const double t=length2>0 ? std::clamp(dot(offset,line)/length2,0.0,1.0) : 0;
    const D3 gap{offset[0]-t*line[0],offset[1]-t*line[1],offset[2]-t*line[2]};
    return dot(gap,gap);
  };
  double best=std::min({point_distance2(a,c,d),point_distance2(b,c,d),
                        point_distance2(c,a,b),point_distance2(d,a,b)});
  const D3 ab=delta(b,a), cd=delta(d,c), ac=delta(c,a), normal=cross(ab,cd);
  const double determinant=dot(normal,normal);
  if (determinant>0) {
    const double s=dot(cross(ac,cd),normal)/determinant;
    const double t=dot(cross(ac,ab),normal)/determinant;
    if (s>=0 && s<=1 && t>=0 && t<=1) {
      const D3 gap{s*ab[0]-ac[0]-t*cd[0],s*ab[1]-ac[1]-t*cd[1],
                   s*ab[2]-ac[2]-t*cd[2]};
      best=std::min(best,dot(gap,gap));
    }
  }
  const double distance=std::sqrt(best);
  return distance<=std::numeric_limits<float>::max() ? float(distance) : -1.0f;
}

// Tracked blade capsule (grip -> tip) against one native hit slot. This is
// the same primitive the row describes, so a qualified contact can name the
// slot it touched instead of approximating the enemy as a sphere.
inline bool BladeOverlapsHitSlot(const NativeHitSlot& slot, Vec3 grip, Vec3 tip,
                                 float blade_radius) {
  if (!slot.valid() || !FiniteVec(grip) || !FiniteVec(tip) ||
      !std::isfinite(blade_radius) || blade_radius < 0.0f)
    return false;
  if (!slot.capsule)
    return SegmentPointDistance(grip, tip, slot.tip) <= slot.radius + blade_radius;
  const float d = SegmentSegmentDistance(grip, tip, slot.tip, slot.tail);
  return d >= 0.0f && d <= slot.radius + blade_radius;
}

// Use the same whole-blade continuous query as target classification, with
// the selected sphere/capsule distance. A slot is attack geometry, not proof
// of a target hurt volume. Non-finite history permits only current overlap.
inline bool BladeSweptOverlapsHitSlot(const NativeHitSlot& slot, Vec3 prev_grip,
                                      Vec3 prev_tip, Vec3 grip, Vec3 tip,
                                      float blade_radius) {
  if (BladeOverlapsHitSlot(slot, grip, tip, blade_radius)) return true;
  if (!FiniteVec(prev_grip) || !FiniteVec(prev_tip)) return false;
  if (!slot.valid() || !std::isfinite(blade_radius) || blade_radius < 0.0f)
    return false;
  return SweepBladeContact(prev_grip, prev_tip, grip, tip,
      slot.radius + blade_radius, [&](Vec3 a, Vec3 b) {
        return slot.capsule ? SegmentSegmentDistance(a, b, slot.tip, slot.tail)
                            : SegmentPointDistance(a, b, slot.tip);
      }) == BladeSweepResult::Hit;
}

// --- Damage scale in the native correction domain --------------------------
// AtkParam stores damage as a base u16 value times a u16 percent correction
// (100 = 1.00x). Our swing-energy scale is a multiplier of exactly that
// shape, so it is expressed in the native domain instead of as invented HP:
// the native owner keeps the base value and every defense/cost rule.
// Fail closed: a non-finite or non-positive scale has no native encoding,
// and encoding it as 0 would be a parameter edit, not a missed hit.
constexpr std::uint16_t kNativePercentUnity = 100;
constexpr float kNativeCorrectionMaxScale =
    65535.0f / static_cast<float>(kNativePercentUnity);

inline bool NativeCorrectionFromScale(float scale, std::uint16_t& percent) {
  percent = 0;
  if (!std::isfinite(scale) || scale <= 0.0f) return false;
  if (scale > kNativeCorrectionMaxScale) return false;
  const float scaled = scale * static_cast<float>(kNativePercentUnity);
  if (!std::isfinite(scaled)) return false;
  const float rounded = std::floor(scaled + 0.5f);
  if (rounded < 1.0f || rounded > 65535.0f) return false;
  percent = static_cast<std::uint16_t>(rounded);
  return true;
}

// Round-trip check used by tests and by any native binding that wants to
// prove the value it submitted is the value the decision asked for.
inline float ScaleFromNativeCorrection(std::uint16_t percent) {
  return static_cast<float>(percent) / static_cast<float>(kNativePercentUnity);
}

// Selected native rows, read from one owned attack context, never synthesized
// from AttackIdentity::Encode(). The table selector's 0/1 PC/NPC meaning still
// requires runtime corroboration; preserve it verbatim. 1405290C0 stores these
// at +38/+3C/+40 and attack type at +34. Type 64 is a parry context, not damage.
struct NativeAttackSelection {
  int behavior_id = -1;
  int attack_param_id = -1;
  int table_selector = -1;
  int attack_type = -1;

  bool valid() const {
    return behavior_id >= 0 && attack_param_id >= 0 &&
        (table_selector == 0 || table_selector == 1) &&
        (attack_type == 0 || attack_type == 2);
  }
};

// A qualified contact: what the tracked blade measured, stated in native
// terms. The native owner decides whether it damages; this only records the
// evidence needed to submit it honestly.
struct QualifiedContact {
  int target_id = -1;
  int animation_id = -1;
  int attack_id = -1;            // Selected AtkParam row, not an animation ID
  int behavior_id = -1;
  int table_selector = -1;
  NativeAtkAttribute attribute = NativeAtkAttribute::None;
  NativeHitSlot slot{};          // the slot touched, if one was resolved
  bool slot_resolved = false;    // false => sphere fallback, geometry unproven
  std::uint16_t correction_percent = kNativePercentUnity;
  bool correction_encoded = false;
  float tip_speed_mps = 0.0f;
  bool blade_action_allowed = false;
};

// Turn one tracked hit into a candidate the native owner could act on. Every
// field is evidence we actually hold: the identity must be verified (an
// unverified hand or action cannot be attributed to a row), the action must
// be one that can carry a blade (a whiff, deflect or kick row is not a
// swing), the scale must have a native encoding (0 or an unrepresentable
// multiplier is refused, never submitted as a row edit), and the selected
// attack slots must contain the slot the blade touched. These slots describe
// the attacking weapon, not the victim's hurt volumes: this shape check cannot
// qualify a target collision, world occlusion, or a native submit by itself.
// `allow_sphere_fallback`
// exists because resolving dummy-polygon ids to endpoints is still an open
// native gate: passing true is a deliberate caller policy, never a default,
// and the record then says the geometry is unproven.
inline bool QualifyContact(const BladeHit& hit, const AttackIdentity& attack,
                           const NativeAttackSelection& selection,
                           std::span<const NativeHitSlot> slots, Vec3 grip,
                           Vec3 tip, QualifiedContact& out,
                           bool allow_sphere_fallback = false,
                           const CombatParams& p = {},
                           Vec3 prev_grip = {NAN, NAN, NAN},
                           Vec3 prev_tip = {NAN, NAN, NAN}) {
  out = {};
  // Refuse malformed motion even if a caller supplied a plausible multiplier.
  // Encode also refuses invalid/overflowing animation identities.
  if (hit.target_id < 0 || !selection.valid() || !attack.hand_known ||
      !attack.action_known || !ActionCanCarryBlade(attack.action) ||
      attack.Encode() < 0 ||
      (hit.kind != GestureKind::Slash && hit.kind != GestureKind::Thrust) ||
      !std::isfinite(hit.tip_speed_mps) || hit.tip_speed_mps <= 0 ||
      hit.tip_speed_mps > 25 || !FiniteVec(grip) || !FiniteVec(tip) ||
      !std::isfinite(p.blade_radius_m) || p.blade_radius_m < 0)
    return false;
  QualifiedContact candidate;
  if (!NativeCorrectionFromScale(hit.damage_scale, candidate.correction_percent))
    return false;
  candidate.correction_encoded = true;
  candidate.target_id = hit.target_id;
  candidate.animation_id = attack.Encode();
  candidate.attack_id = selection.attack_param_id;
  candidate.behavior_id = selection.behavior_id;
  candidate.table_selector = selection.table_selector;
  candidate.attribute = AttributeFromGesture(hit.kind);
  candidate.tip_speed_mps = hit.tip_speed_mps;
  candidate.blade_action_allowed = true;
  const bool have_sweep = FiniteVec(prev_grip) && FiniteVec(prev_tip);
  int best = -1;
  for (std::size_t i = 0; i < slots.size(); ++i) {
    const bool overlaps = have_sweep
        ? BladeSweptOverlapsHitSlot(slots[i], prev_grip, prev_tip, grip, tip,
                                    p.blade_radius_m)
        : BladeOverlapsHitSlot(slots[i], grip, tip, p.blade_radius_m);
    if (!overlaps) continue;
    // The row's own priority beats list order; equal priority keeps the
    // earlier slot so the choice stays deterministic across runs.
    if (best < 0 || slots[i].priority > slots[best].priority)
      best = static_cast<int>(i);
  }
  if (best >= 0) {
    candidate.slot = slots[static_cast<std::size_t>(best)];
    candidate.slot_resolved = true;
    out = candidate;
    return true;
  }
  // The selected attack's shape vetoes the candidate. Target hurt geometry
  // and obstruction still belong to the native collision owner.
  if (!slots.empty()) return false;
  if (!allow_sphere_fallback) return false;
  out = candidate;
  return true;
}

}  // namespace dsvr
