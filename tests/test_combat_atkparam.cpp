#include "../src/core/combat_atkparam.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

// The native damage vocabulary is only useful if it round-trips: an attack
// id we name must decode back to the same row, the flag byte must survive
// both directions, and a damage scale must land in the correction domain the
// row actually stores. Every check below is a claim about the pinned research
// cache (docs/SOULSMODDING_REFERENCE.md), not about our preference.
int main() {
  int failures = 0;
  auto report = [&](int line, const char* what) {
    ++failures;
    std::printf("FAIL %d: %s\n", line, what);
  };
  // Variadic so designated-initializer braces with commas stay one argument.
#define CHECK(...)                     \
  do {                                 \
    if (!(__VA_ARGS__)) report(__LINE__, #__VA_ARGS__); \
  } while (0)
  auto near = [](float a, float b) { return std::fabs(a - b) < 0.001f; };
  using namespace dsvr;

  // --- attack identity: the schema is category*10000 + hand + action -------
  CHECK(AttackIdentity{.category = 0, .hand = kAttackHandRight,
                       .action = AttackAction::QuickA}
            .Encode() == 3000);
  CHECK(AttackIdentity{.category = 15, .hand = kAttackHandBoth,
                       .action = AttackAction::StrongA}
            .Encode() == 154300);
  CHECK(AttackIdentity{.category = 15, .hand = kAttackHandLeft,
                       .action = AttackAction::StrongA, .zero_durability = true}
            .Encode() == 155305);

  // Every listed action, every hand, with and without the durability
  // modifier, must decode back to exactly what was encoded.
  const AttackAction actions[] = {
      AttackAction::QuickA,        AttackAction::QuickB,
      AttackAction::QuickARepeat,  AttackAction::StatsNotMet,
      AttackAction::QuickAWhiff,   AttackAction::QuickBWhiff,
      AttackAction::QuickARepeatWhiff, AttackAction::Kick,
      AttackAction::DeflectedA,    AttackAction::DeflectedB,
      AttackAction::RiposteA,      AttackAction::RiposteB,
      AttackAction::RiposteC,      AttackAction::StrongA,
      AttackAction::StrongAfterQuick, AttackAction::StrongB,
      AttackAction::BackstabA,     AttackAction::BackstabB,
      AttackAction::BackstabC,     AttackAction::BackstabD,
      AttackAction::Running,       AttackAction::Jumping,
      AttackAction::JumpingAlt,    AttackAction::PlungeStart,
      AttackAction::PlungeContinue, AttackAction::PlungeEnd,
      AttackAction::QuickAfterEvade, AttackAction::QuickAfterEvadeWhiff,
      AttackAction::HornetRingRiposte};
  const int hands[] = {kAttackHandRight, kAttackHandBoth, kAttackHandLeft};
  for (AttackAction a : actions) {
    for (int h : hands) {
      for (bool worn : {false, true}) {
        const AttackIdentity in{.category = 7, .hand = h, .action = a,
                                .zero_durability = worn};
        const AttackIdentity out = AttackIdentity::Decode(in.Encode());
        CHECK(out.category == 7 && out.hand == h && out.action == a &&
              out.zero_durability == worn && out.hand_known &&
              out.action_known);
      }
    }
  }

  // The +5 modifier is only recoverable because no two listed actions differ
  // by exactly 5. If that ever stops being true the decode is ambiguous and
  // the "prefer the unmodified row" rule is a guess, so assert the property.
  for (AttackAction a : actions) {
    const int v = static_cast<int>(a);
    CHECK(!IsListedAttackAction(v + kAttackZeroDurability));
    CHECK(!IsListedAttackAction(v - kAttackZeroDurability));
  }
  // The digit heuristic this replaced: QuickB is 1 so its worn form is 6, and
  // QuickARepeatWhiff is 42 so its worn form is 47 - neither ends in 5.
  {
    const AttackIdentity qb = AttackIdentity::Decode(
        AttackIdentity{.category = 0, .hand = kAttackHandRight,
                       .action = AttackAction::QuickB, .zero_durability = true}
            .Encode());
    CHECK(qb.action == AttackAction::QuickB && qb.zero_durability &&
          qb.action_known && qb.hand_known);
    const AttackIdentity wr = AttackIdentity::Decode(
        AttackIdentity{.category = 0, .hand = kAttackHandRight,
                       .action = AttackAction::QuickARepeatWhiff,
                       .zero_durability = true}
            .Encode());
    CHECK(wr.action == AttackAction::QuickARepeatWhiff && wr.zero_durability);
    // Neither a listed row nor a listed row plus the modifier: flagged.
    CHECK(!AttackIdentity::Decode(3999).action_known);
  }

  // A structurally parseable but unlisted hand is flagged, never guessed.
  {
    const AttackIdentity out = AttackIdentity::Decode(71000);
    CHECK(!out.hand_known && out.category == 7);
    const AttackIdentity neg = AttackIdentity::Decode(-1);
    CHECK(!neg.hand_known && !neg.action_known);
  }

  // Whiff/deflect/kick rows share the id space but are not tracked swings.
  CHECK(ActionCanCarryBlade(AttackAction::QuickA));
  CHECK(ActionCanCarryBlade(AttackAction::StrongAfterQuick));
  CHECK(!ActionCanCarryBlade(AttackAction::QuickAWhiff));
  CHECK(!ActionCanCarryBlade(AttackAction::DeflectedB));
  CHECK(!ActionCanCarryBlade(AttackAction::Kick));
  CHECK(!ActionCanCarryBlade(AttackAction::StatsNotMet));
  CHECK(!ActionCanCarryBlade(AttackAction::RiposteA));

  // --- AtkParam row layout: the offsets must stay inside one row -----------
  CHECK(kAtkParamRowBytes == 0x80);
  // 0x73 (mapHitType) sits between the damage level and the attack
  // attribute; the neighbouring 0x74 byte stays unnamed until checked.
  CHECK(atk_param::kDmgLevel == 0x72);
  CHECK(atk_param::kMapHitType == 0x73);
  CHECK(atk_param::kAtkAttribute == 0x75);
  CHECK(static_cast<std::uint8_t>(NativeMapHitType::Normal) == 0);
  CHECK(static_cast<std::uint8_t>(NativeMapHitType::Precision) == 1);
  CHECK(static_cast<std::uint8_t>(NativeMapHitType::NotOnMap) == 2);
  CHECK(static_cast<std::uint8_t>(NativeMapHitType::MapNotAsset) == 3);
  for (int i = 0; i < kAtkParamHitSlots; ++i) {
    CHECK(atk_param::kHitRadius[i] + 4 <= kAtkParamRowBytes);
    CHECK(atk_param::kHitDummyTip[i] + 2 <= kAtkParamRowBytes);
    CHECK(atk_param::kHitDummyTail[i] + 2 <= kAtkParamRowBytes);
    CHECK(atk_param::kHitType[i] <= kAtkParamRowBytes);
    CHECK(atk_param::kHitPriority[i] <= kAtkParamRowBytes);
    // Four slots are parallel arrays: constant stride between them.
    CHECK(atk_param::kHitDummyTail[i] - atk_param::kHitDummyTip[i] == 8);
    CHECK(atk_param::kHitType[i] == atk_param::kHitType[0] + i);
  }
  // Damage fields are u16 pairs; a one-byte slip would silently rescale.
  CHECK(atk_param::kAtkPhysCorrection + 2 == atk_param::kAtkMagCorrection);
  CHECK(atk_param::kAtkStam + 2 == atk_param::kGuardAtkRate);
  CHECK(atk_param::kGuardAtkRate + 2 == atk_param::kGuardBreakRate);

  // --- flag byte -----------------------------------------------------------
  for (unsigned b = 0; b < 256; ++b) {
    const auto flags = NativeAttackFlags::FromByte(static_cast<std::uint8_t>(b));
    CHECK(flags.ToByte() == static_cast<std::uint8_t>(b));
  }
  {
    const auto f = NativeAttackFlags::FromByte(0x81);  // disable_guard + i-frames
    CHECK(f.disable_guard && f.disable_no_damage && !f.repeat_hit_sfx);
  }

  // Gesture label -> native attribute enum (Slash 1 / Thrust 3).
  CHECK(AttributeFromGesture(GestureKind::Slash) == NativeAtkAttribute::Slash);
  CHECK(AttributeFromGesture(GestureKind::Thrust) == NativeAtkAttribute::Thrust);
  CHECK(static_cast<std::uint8_t>(NativeAtkAttribute::Slash) == 1);
  CHECK(static_cast<std::uint8_t>(NativeAtkAttribute::Thrust) == 3);
  CHECK(static_cast<std::uint8_t>(NativeHitType::MapCollision) == 3);
  CHECK(static_cast<std::uint8_t>(NativeHitSource::Body) == 1);

  // --- segment-segment distance -------------------------------------------
  // Crossing segments meet.
  // Shallow crossings must meet too: alternating projections do not converge
  // in three passes here, and used to veto thin capsule contacts.
  CHECK(near(SegmentSegmentDistance({0, 0, 0}, {1, 0, 0},
                                    {0, -0.1f, 0}, {1, 0.1f, 0}), 0.0f));
  // Endpoint/order reversal cannot change the contact; lifting one capsule
  // gives a known interior minimum even at a shallow angle.
  for (bool reverse : {false, true}) {
    const Vec3 a{0,0,0}, b{1,0,0}, c{0,-0.1f,0.04f}, d{1,0.1f,0.04f};
    CHECK(near(SegmentSegmentDistance(reverse ? b:a, reverse ? a:b,c,d),0.04f));
    CHECK(near(SegmentSegmentDistance(d,c,reverse ? b:a,reverse ? a:b),0.04f));
  }
  CHECK(near(SegmentSegmentDistance({-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}),
             0.0f));
  // Parallel, offset by exactly 0.5 in y, overlapping in x.
  CHECK(near(SegmentSegmentDistance({0, 0, 0}, {1, 0, 0}, {0, 0.5f, 0},
                                    {1, 0.5f, 0}),
             0.5f));
  // Skew: closest pair is interior on both.
  CHECK(near(SegmentSegmentDistance({0, 1, 0}, {0, 1, 2}, {-1, 0, 1},
                                    {1, 0, 1}),
             1.0f));
  // Endpoint clamp: the second segment sits past the blade tip.
  CHECK(near(SegmentSegmentDistance({0, 0, 0}, {0, 0, -1}, {0, 0.75f, -2},
                                    {0, 0.75f, -3}),
             std::sqrt(0.75f * 0.75f + 1.0f)));
  // Degenerate slots collapse to point/segment distance.
  CHECK(near(SegmentSegmentDistance({0, 0, 0}, {0, 0, 0}, {0, 3, 0}, {0, 3, 0}),
             3.0f));
  CHECK(near(SegmentSegmentDistance({0, 0, 0}, {0, 0, 0}, {0, 3, -1}, {0, 3, 1}),
             3.0f));
  // Non-finite input is refused with a negative sentinel, never a fake zero.
  CHECK(SegmentSegmentDistance({NAN, 0, 0}, {1, 0, 0}, {0, 0, 0}, {0, 1, 0}) <
        0.0f);
  // Determinism: identical inputs, identical output.
  {
    const float d1 =
        SegmentSegmentDistance({0, 0, 0}, {0, 1, 0}, {1, 0, 1}, {2, 0, 1});
    const float d2 =
        SegmentSegmentDistance({0, 0, 0}, {0, 1, 0}, {1, 0, 1}, {2, 0, 1});
    CHECK(d1 == d2);
  }

  // --- blade vs native hit slot -------------------------------------------
  const Vec3 grip{0, 1.4f, 0}, tip{0, 1.4f, -0.9f};
  {
    NativeHitSlot sphere{.id = 1, .tip = {0, 1.4f, -0.5f}, .radius = 0.2f};
    CHECK(sphere.valid());
    CHECK(BladeOverlapsHitSlot(sphere, grip, tip, 0.03f));
    sphere.tip = {0.5f, 1.4f, -0.5f};
    CHECK(!BladeOverlapsHitSlot(sphere, grip, tip, 0.03f));
    // Inclusive boundary: a graze at exactly radius + blade radius counts.
    sphere.tip = {0.23f, 1.4f, -0.5f};
    CHECK(BladeOverlapsHitSlot(sphere, grip, tip, 0.03f));
    sphere.radius = 0.0f;
    CHECK(!sphere.valid() && !BladeOverlapsHitSlot(sphere, grip, tip, 0.03f));
  }
  {
    // A capsule slot along the blade line: a through-hit a sphere model of
    // the enemy would have missed.
    NativeHitSlot cap{.id = 2,
                      .tip = {0, 1.4f, -0.1f},
                      .tail = {0, 1.4f, -0.8f},
                      .radius = 0.05f,
                      .capsule = true};
    CHECK(cap.valid());
    CHECK(BladeOverlapsHitSlot(cap, grip, tip, 0.03f));
    cap.tip = {0.4f, 1.4f, -0.1f};
    cap.tail = {0.4f, 1.4f, -0.8f};
    CHECK(!BladeOverlapsHitSlot(cap, grip, tip, 0.03f));
    cap.tip = {0.08f, 1.4f, -0.1f};
    cap.tail = {0.08f, 1.4f, -0.8f};
    CHECK(BladeOverlapsHitSlot(cap, grip, tip, 0.03f));  // 0.05 + 0.03
    // A capsule with an unresolved tail is not a usable slot.
    NativeHitSlot broken{.id = 3,
                         .tip = {0, 1.4f, -0.5f},
                         .radius = 0.1f,
                         .capsule = true};
    broken.tail = {NAN, 0, 0};
    CHECK(!broken.valid() && !BladeOverlapsHitSlot(broken, grip, tip, 0.03f));
  }

  // --- damage scale in the native correction domain ------------------------
  {
    std::uint16_t pct = 0;
    CHECK(NativeCorrectionFromScale(1.0f, pct) && pct == 100);
    CHECK(NativeCorrectionFromScale(0.6f, pct) && pct == 60);
    CHECK(NativeCorrectionFromScale(1.8f, pct) && pct == 180);
    CHECK(NativeCorrectionFromScale(kNativeCorrectionMaxScale, pct) &&
          pct == 65535);
    // Refused, not clamped to zero: 0 percent would edit the row, and a
    // missed hit is expressed by never submitting a contact at all.
    CHECK(!NativeCorrectionFromScale(0.0f, pct));
    CHECK(!NativeCorrectionFromScale(-1.0f, pct));
    CHECK(!NativeCorrectionFromScale(NAN, pct));
    CHECK(!NativeCorrectionFromScale(INFINITY, pct));
    CHECK(!NativeCorrectionFromScale(kNativeCorrectionMaxScale + 1.0f, pct));
    // Round-trip stays inside one percent of the requested scale.
    for (float s : {0.6f, 1.0f, 1.15f, 1.8f, 3.7f}) {
      CHECK(NativeCorrectionFromScale(s, pct) &&
            std::fabs(ScaleFromNativeCorrection(pct) - s) <= 0.01f);
    }
  }

  // The continuous scale the runtime produces must be encodable, and a
  // sub-threshold scale (0) must never be.
  {
    const CombatParams p;
    const float scale = DamageScale(3.0f, GestureKind::Slash, 1.5f, 1.0f, p);
    std::uint16_t pct = 0;
    CHECK(scale > 0.0f && NativeCorrectionFromScale(scale, pct) && pct > 100);
    const float none = DamageScale(1.0f, GestureKind::Slash, 1.5f, 1.0f, p);
    CHECK(none == 0.0f && !NativeCorrectionFromScale(none, pct));
  }

  // --- tracked hit -> native contact candidate ------------------------------
  {
    std::uint16_t pct = 999;
    CHECK(!NativeCorrectionFromScale(0.001f, pct) && pct == 0);
  }
  {
    const CombatParams p;
    const float scale = DamageScaleContinuous(3.0f, GestureKind::Slash, 1.5f, 1.0f, p);
    BladeHit hit;
    hit.target_id = 42;
    hit.damage_scale = scale;
    hit.tip_speed_mps = 3.0f;
    hit.kind = GestureKind::Slash;
    const AttackIdentity atk{.category = 15,
                             .hand = kAttackHandRight,
                             .action = AttackAction::StrongA};
    const NativeAttackSelection selection{.behavior_id=1000300, .attack_param_id=6000,
                                           .table_selector=0, .attack_type=0};
    const std::array one{NativeHitSlot{.id = 1, .tip = {0, 1.4f, -0.5f}, .radius = 0.2f}};
    QualifiedContact q;
    CHECK(QualifyContact(hit, atk, selection, one, grip, tip, q));
    CHECK(q.slot_resolved && q.slot.id == 1);
    CHECK(q.target_id == 42 && q.animation_id == atk.Encode() &&
          q.attack_id == 6000 && q.behavior_id == 1000300 && q.table_selector == 0);
    CHECK(q.attribute == NativeAtkAttribute::Slash);
    CHECK(q.tip_speed_mps == 3.0f && q.blade_action_allowed);
    std::uint16_t pct = 0;
    CHECK(NativeCorrectionFromScale(scale, pct) &&
          q.correction_percent == pct && q.correction_encoded);
    // Animation/behavior/attack row IDs deliberately differ. A parry or
    // unresolved table selection must never turn into a damaging blade row.
    for (NativeAttackSelection bad : {
        NativeAttackSelection{},
        NativeAttackSelection{1000300,6000,2,0},
        NativeAttackSelection{1000300,6000,0,64},
        NativeAttackSelection{1000300,-1,0,0}}) {
      QualifiedContact refused=q;
      CHECK(!QualifyContact(hit,atk,bad,one,grip,tip,refused));
      CHECK(refused.target_id==-1 && !refused.correction_encoded);
    }
    for (float speed : {0.0f,-1.0f,26.0f,NAN,INFINITY}) {
      BladeHit bad=hit;bad.tip_speed_mps=speed;
      CHECK(!QualifyContact(bad,atk,selection,one,grip,tip,q));
    }
    BladeHit unclassified=hit;unclassified.kind=GestureKind::None;
    CHECK(!QualifyContact(unclassified,atk,selection,one,grip,tip,q));
    AttackIdentity malformed=atk;malformed.category=std::numeric_limits<int>::max();
    CHECK(malformed.Encode()==-1);
    CHECK(!QualifyContact(hit,malformed,selection,one,grip,tip,q));
    CHECK(!QualifyContact(hit,atk,selection,{}, {NAN,0,0},tip,q,true));

    // A whiff row is not a swing, so it can never carry a blade to a target.
    const AttackIdentity whiff{.category = 15,
                               .hand = kAttackHandRight,
                               .action = AttackAction::QuickAWhiff};
    CHECK(!QualifyContact(hit, whiff, selection, one, grip, tip, q));
    // An unverified identity is not attributable to any row.
    AttackIdentity unknown = atk;
    unknown.hand_known = false;
    CHECK(!QualifyContact(hit, unknown, selection, one, grip, tip, q));
    // No encoding, no submit: a missed hit is expressed by never submitting.
    BladeHit soft = hit;
    soft.damage_scale = 0.0f;
    CHECK(!QualifyContact(soft, atk, selection, one, grip, tip, q));
    BladeHit huge = hit;
    huge.damage_scale = kNativeCorrectionMaxScale + 1.0f;
    CHECK(!QualifyContact(huge, atk, selection, one, grip, tip, q));
    // The row's own geometry vetoes a sphere-only touch.
    const std::array far{NativeHitSlot{.id = 2, .tip = {0.9f, 1.4f, -0.5f}, .radius = 0.2f}};
    CHECK(!QualifyContact(hit, atk, selection, far, grip, tip, q));
    CHECK(q.target_id == -1 && q.attack_id == -1 && !q.correction_encoded);
    // Unresolved slots are an open native gate, not a free pass: the caller has
    // to opt in, and the record then says the geometry is unproven.
    const std::array<NativeHitSlot, 0> none{};
    QualifiedContact fb;
    CHECK(!QualifyContact(hit, atk, selection, none, grip, tip, fb));
    CHECK(QualifyContact(hit, atk, selection, none, grip, tip, fb, true));
    CHECK(!fb.slot_resolved && fb.correction_encoded && fb.blade_action_allowed);
    // Priority wins over list order, and the same inputs give the same slot.
    const std::array pair{
        NativeHitSlot{.id = 7, .tip = {0, 1.4f, -0.5f}, .radius = 0.2f, .priority = 3},
        NativeHitSlot{.id = 9, .tip = {0.05f, 1.4f, -0.5f}, .radius = 0.2f, .priority = 20}};
    QualifiedContact pr, pr2;
    CHECK(QualifyContact(hit, atk, selection, pair, grip, tip, pr) && pr.slot.id == 9);
    CHECK(QualifyContact(hit, atk, selection, pair, grip, tip, pr2) && pr2.slot.id == pr.slot.id);
    // A thrust swing is labelled in the game's own attribute enum.
    BladeHit thrust = hit;
    thrust.kind = GestureKind::Thrust;
    QualifiedContact th;
    CHECK(QualifyContact(thrust, atk, selection, one, grip, tip, th) &&
          th.attribute == NativeAtkAttribute::Thrust);
  }

  std::printf("combat_atkparam: %s (%d failures)\n",
              failures == 0 ? "PASS" : "FAIL", failures);
  return failures;
}
