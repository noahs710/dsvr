#include "../src/core/combat_math.h"
#include "../src/core/arm_pose_math.h"

#include <cmath>
#include <cstdio>

int main() {
  int failures = 0;
  auto check = [&](bool ok) { if (!ok) ++failures; };
  auto near = [](float a, float b) { return std::fabs(a - b) < 0.001f; };
  using namespace dsvr;

  // Below threshold (and NaN) never damages, whatever the gain.
  check(DamageScale(1.0f, GestureKind::Slash, 1.5f, 1.0f) == 0.0f);
  check(DamageScale(1.49f, GestureKind::Slash, 1.5f, 5.0f) == 0.0f);
  check(DamageScale(NAN, GestureKind::Slash, 1.5f, 1.0f) == 0.0f);
  check(DamageScale(3.0f, GestureKind::Slash, 1.5f, NAN) == 0.0f);
  // At the threshold the minimum multiplier applies; at full speed the max.
  check(near(DamageScale(1.5f, GestureKind::Slash, 1.5f, 1.0f), 0.6f));
  check(near(DamageScale(4.0f, GestureKind::Slash, 1.5f, 1.0f), 1.8f));
  check(near(DamageScale(9.0f, GestureKind::Slash, 1.5f, 1.0f), 1.8f));
  // Monotonic between, gain multiplies, thrust earns its bonus.
  {
    float a = DamageScale(2.0f, GestureKind::Slash, 1.5f, 1.0f);
    float b = DamageScale(3.0f, GestureKind::Slash, 1.5f, 1.0f);
    check(a > 0.6f && b > a && b < 1.8f);
    check(near(DamageScale(3.0f, GestureKind::Slash, 1.5f, 2.0f), b * 2.0f));
    float th = DamageScale(3.0f, GestureKind::Thrust, 1.0f, 1.0f);
    float sl = DamageScale(3.0f, GestureKind::Slash, 1.0f, 1.0f);
    check(th > sl);
  }

  // Segment distance: interior, endpoint clamp, degenerate segment.
  check(near(SegmentPointDistance({0, 0, 0}, {0, 0, -1}, {0.5f, 0, -0.5f}),
             0.5f));
  check(near(SegmentPointDistance({0, 0, 0}, {0, 0, -1}, {0, 0, 0.5f}), 0.5f));
  check(near(SegmentPointDistance({1, 2, 3}, {1, 2, 3}, {1, 2, 4}), 1.0f));

  // Physical combat velocity must not depend on render cadence.
  check(near(DisplacementSpeed({0,0,0},{0,0,0.1f},1.0f/90.0f),9.0f));
  check(near(DisplacementSpeed({0,0,0},{0,0,0.2f},1.0f/45.0f),9.0f));
  check(DisplacementSpeed({0,0,0},{0,0,0.1f},0.0f)==0.0f);

  // Blade capsule vs sphere: through-hit, clear miss, graze at boundary.
  const Vec3 grip{0, 1.4f, 0}, tip{0, 1.4f, -0.9f};
  check(BladeOverlapsSphere(grip, tip, {0, 1.4f, -0.5f}, 0.3f));
  check(!BladeOverlapsSphere(grip, tip, {0, 2.5f, -0.5f}, 0.3f));
  check(BladeOverlapsSphere(grip, tip, {0.33f, 1.4f, -0.5f}, 0.3f));
  check(!BladeOverlapsSphere(grip, tip, {NAN, 1.4f, -0.5f}, 0.3f));
  check(!BladeOverlapsSphere(grip, tip, {0, 1.4f, -0.5f}, -1.0f));
  check(!BladeOverlapsSphere({NAN, 1.4f, 0}, tip, {0, 1.4f, -0.5f}, 0.3f));
  check(!BladeOverlapsSphere(grip, {0, NAN, -0.9f}, {0, 1.4f, -0.5f}, 0.3f));
  check(!BladeOverlapsSphere(grip, tip, {0, 1.4f, NAN}, 0.3f));

  // Haptics: disabled or sub-threshold is silent; thrust is the short pulse.
  check(HitHaptic(3.0f, GestureKind::Slash, 1.5f, false).amplitude == 0.0f);
  check(HitHaptic(1.0f, GestureKind::Slash, 1.5f, true).duration_ms == 0);
  {
    HapticPulse s = HitHaptic(3.0f, GestureKind::Slash, 1.5f, true);
    HapticPulse t = HitHaptic(3.0f, GestureKind::Thrust, 1.0f, true);
    check(s.duration_ms == 80 && s.amplitude > 0.35f && s.amplitude <= 1.0f);
    check(t.duration_ms == 45);
    HapticPulse hard = HitHaptic(4.0f, GestureKind::Slash, 1.5f, true);
    check(near(hard.amplitude, 1.0f));
  }

  // Fluid combat: BladeKinematics tracks continuous tip speed, no latch.
  {
    BladeKinematics b;
    const float dt = 1.0f / 90.0f;
    Vec3 g{0, 1.4f, 0}, tp{0, 1.4f, -0.9f};
    b.Feed(g, tp, dt);
    check(b.valid && b.tip_speed == 0.0f);
    for (int i = 0; i < 45; ++i) {
      g.x += 3.0f * dt;
      tp.x += 3.0f * dt;
      b.Feed(g, tp, dt);
    }
    check(b.tip_speed > 2.0f);
    check(b.FrameKind({0, 0, -1}) == GestureKind::Slash);
    // Resting contact: same pose fed repeatedly decays speed to ~0.
    for (int i = 0; i < 60; ++i) b.Feed(g, tp, dt);
    check(b.tip_speed < 0.2f);
    // Teleport resets without a phantom spike.
    Vec3 tele{5.0f, 1.4f, 0}, tele_tip{5.0f, 1.4f, -0.9f};
    b.Feed(tele, tele_tip, dt);
    check(b.tip_speed == 0.0f);
    b.Reset();
    check(!b.valid && b.tip_speed == 0.0f);
  }

  // Continuous damage: 0 below contact speed, ramp above, no latch.
  check(DamageScaleContinuous(0.5f, GestureKind::Slash, 0.8f, 1.0f) == 0.0f);
  check(DamageScaleContinuous(0.8f, GestureKind::Slash, 0.8f, 1.0f) > 0.0f);
  {
    float a = DamageScaleContinuous(2.0f, GestureKind::Slash, 0.8f, 1.0f);
    float c = DamageScaleContinuous(3.0f, GestureKind::Slash, 0.8f, 1.0f);
    check(c > a);
    check(DamageScaleContinuous(2.0f, GestureKind::Thrust, 0.8f, 1.0f) > a);
  }

  // Swept overlap catches tunnelling: static capsule misses, sweep hits.
  {
    Vec3 sg{0, 1.4f, 0}, st{0, 1.4f, -0.9f}, prev{0, 1.4f, 0.9f};
    Vec3 enemy{0, 1.4f, 0.45f};
    check(!BladeOverlapsSphere(sg, st, enemy, 0.1f));
    check(BladeSweptOverlapsSphere(sg, st, prev, enemy, 0.1f));
    check(!BladeSweptOverlapsSphere(sg, st, prev, {0, 3.0f, 0.45f}, 0.1f));
    check(!BladeSweptOverlapsSphere(sg, st, {NAN, 0, 0}, enemy, 0.1f));
  }

  // Rotational sweep: a fast wrist rotation carries the tip along an ARC, and
  // the straight prev_tip->tip chord cuts inside that arc. A small target on
  // the arc's outer side sits inside the volume the blade actually occupied
  // yet outside the previous capsule, the current capsule and the chord.
  {
    CombatParams p;
    p.blade_radius_m = 0.03f;
    const Vec3 g{0, 1.4f, 0};
    const Vec3 prev_tip{0.9f, 1.4f, 0.0f};   // blade pointed +X last frame
    const Vec3 tip{0.0f, 1.4f, -0.9f};       // pointed -Z this frame: 90 deg
    const Vec3 on_arc{0.6364f, 1.4f, -0.6364f};  // mid-arc at full reach
    check(!BladeOverlapsSphere(g, tip, on_arc, 0.05f, p));
    check(!BladeOverlapsSphere(g, prev_tip, on_arc, 0.05f, p));
    check(!BladeSweptOverlapsSphere(g, tip, prev_tip, on_arc, 0.05f, p));
    check(BladeSweptOverlapsSphereArc(g, prev_tip, g, tip, on_arc, 0.05f, p));
    // Nothing outside the swept band becomes a hit, and the sweep stays
    // bounded: a target half a metre beyond the arc stays a miss.
    check(!BladeSweptOverlapsSphereArc(g, prev_tip, g, tip,
                                       {1.0f, 1.4f, -1.0f}, 0.05f, p));
    check(!BladeSweptOverlapsSphereArc(g, prev_tip, g, tip,
                                       {0.6364f, 2.4f, -0.6364f}, 0.05f, p));
    // Pure translation has no rotation plane to invent: the arc path must
    // agree with the chord, never invent coverage and never go NaN.
    check(BladeSweptOverlapsSphereArc({0, 1.4f, 0.9f}, {0, 1.4f, 0.0f}, g, tip,
                                      {0, 1.4f, -0.45f}, 0.1f, p));
    check(!BladeSweptOverlapsSphereArc({0, 1.4f, 0.9f}, {0, 1.4f, 0.0f}, g, tip,
                                       {0, 1.4f, 0.45f}, 0.1f, p));
    check(!BladeSweptOverlapsSphereArc(g, prev_tip, g, tip, on_arc, -1.0f, p));
    check(!BladeSweptOverlapsSphereArc({NAN, 0, 0}, prev_tip, g, tip, on_arc,
                                       0.05f, p));
    CombatParams bad = p;
    bad.blade_radius_m = NAN;
    check(!BladeSweptOverlapsSphereArc(g, prev_tip, g, tip, on_arc, 0.05f, bad));
    // A 180-degree flip has no unique axis: fall back to the chord alone
    // rather than choosing an arbitrary rotation plane.
    check(!BladeSweptOverlapsSphereArc(g, {0, 1.4f, -0.9f}, g,
                                       {0, 1.4f, 0.9f}, {0.9f, 1.4f, 0}, 0.05f,
                                       p));
    // End-to-end: the runtime decision reports the arc-only contact.
    RehitGate gate;
    BladeKinematics blade;
    blade.valid = true;
    blade.grip = g;
    blade.tip = tip;
    blade.prev_grip = g;
    blade.prev_tip = prev_tip;
    blade.tip_speed = 3.0f;
    BladeContactTarget targets[] = {{7, on_arc, 0.05f}};
    BladeHit hit{};
    check(EvaluateBladeContact(blade, GestureKind::Slash, targets, 0.8f, 1.0f,
                               280, 1000, true, gate, hit));
    check(hit.target_id == 7 && hit.damage_scale > 0.0f);
  }

  // Re-hit gate: same enemy blocked inside the window, others pass.
  {
    RehitGate gate;
    check(gate.TryAcquire(7, 1000, 280));
    check(!gate.TryAcquire(7, 1100, 280));
    check(gate.TryAcquire(7, 1300, 280));
    check(gate.TryAcquire(9, 1310, 280));
    check(!gate.TryAcquire(-1, 1400, 280));
  }

  // Contact evaluation: the runtime decision that turns tracked blade
  // motion into at most one attributed hit per target per window.
  {
    RehitGate gate;
    BladeKinematics blade;
    blade.valid = true;
    blade.grip = {0, 1.4f, 0};
    blade.tip = {0, 1.4f, -0.9f};
    blade.prev_tip = {0, 1.4f, 0.9f};
    blade.tip_speed = 3.0f;
    BladeContactTarget targets[] = {{7, {0, 1.4f, -0.5f}, 0.3f},
                                    {9, {0, 1.4f, -0.45f}, 0.3f},
                                    {-1, {0, 1.4f, -0.5f}, 0.3f}};
    BladeHit hit{};
    // Swept tip passes through target 7: fast contact hits with haptics.
    check(EvaluateBladeContact(blade, GestureKind::Slash, targets, 0.8f, 1.0f,
                               280, 1000, true, gate, hit));
    check(hit.target_id == 7 && hit.damage_scale > 0.0f &&
          hit.haptic.amplitude > 0.0f && hit.haptic.duration_ms == 80);
    // Same target inside the window cannot be re-hit; target 9 still can.
    check(EvaluateBladeContact(blade, GestureKind::Slash, targets, 0.8f, 1.0f,
                               280, 1100, true, gate, hit) &&
          hit.target_id == 9);
    check(!EvaluateBladeContact(blade, GestureKind::Slash, targets, 0.8f, 1.0f,
                                280, 1150, true, gate, hit));
    // Resting contact and lost tracking never damage.
    blade.tip_speed = 0.2f;
    check(!EvaluateBladeContact(blade, GestureKind::Slash, targets, 0.8f, 1.0f,
                                280, 5000, true, gate, hit));
    blade.tip_speed = 3.0f;
    blade.valid = false;
    check(!EvaluateBladeContact(blade, GestureKind::Slash, targets, 0.8f, 1.0f,
                                280, 5100, true, gate, hit));
  }

  // Roomscale: physical walking translates head and hands together. That
  // shared translation is locomotion, not a swing: relative tip speed
  // stays at rest while absolute positions (overlap/sweep) still move.
  {
    BladeKinematics walk;
    const float dt = 1.0f / 90.0f;
    Vec3 head{0, 1.6f, 0}, g{0.3f, 1.2f, -0.4f}, tp{0.3f, 1.2f, -1.3f};
    walk.FeedRelative(g, tp, head, dt, 1);
    check(walk.valid && walk.tip_speed == 0.0f);
    for (int i = 0; i < 45; ++i) {
      head.z -= 1.4f * dt; g.z -= 1.4f * dt; tp.z -= 1.4f * dt;
      walk.FeedRelative(g, tp, head, dt, 1);
    }
    check(walk.tip_speed < 0.05f && walk.grip_speed < 0.05f);
    check(DamageScaleContinuous(walk.tip_speed, GestureKind::Slash, 0.8f,
                                1.0f) == 0.0f);
    check(near(walk.tip.z, tp.z) && near(walk.grip.z, g.z));
    // A real swing on top of the walk keeps only its own speed.
    for (int i = 0; i < 45; ++i) {
      head.z -= 1.4f * dt; g.z -= 1.4f * dt; tp.z -= 1.4f * dt;
      g.x += 2.0f * dt; tp.x += 2.0f * dt;
      walk.FeedRelative(g, tp, head, dt, 1);
    }
    check(walk.tip_speed > 1.8f && walk.tip_speed < 2.2f);
    // The swing is across the blade here, not along it: slash, not thrust.
    check(walk.FrameKind({0, 0, -1}) == GestureKind::Slash);
    // Space epoch change (recenter/new reference space) reprimes without
    // a phantom spike: positions valid, speed exactly zero, no hit.
    walk.FeedRelative(g, tp, head, dt, 2);
    check(walk.valid && walk.tip_speed == 0.0f);
    walk.FeedRelative(g, tp, head, dt, 2);
    check(walk.valid && walk.tip_speed == 0.0f);
    // A head teleport in one frame invalidates instead of spiking.
    Vec3 far_head{5.0f, 1.6f, 0};
    walk.FeedRelative(g, tp, far_head, dt, 2);
    check(!walk.valid && walk.tip_speed == 0.0f);
    // Non-finite head fails closed.
    walk.FeedRelative(g, tp, head, dt, 2);
    walk.FeedRelative(g, tp, {NAN, 0, 0}, dt, 2);
    check(!walk.valid && walk.tip_speed == 0.0f);
  }

  // A rejected hand teleport must not subtract head velocity from zero
  // and manufacture a hit. Recovery starts from the new sample at rest.
  {
    BladeKinematics b;
    b.FeedRelative({0,0,0}, {0,0,-.9f}, {0,1.6f,0}, .01f, 1);
    b.FeedRelative({2,0,0}, {2,0,-.9f}, {.1f,1.6f,0}, .01f, 1);
    check(b.tip_speed == 0 && b.grip_speed == 0);
    b.FeedRelative({2.1f,0,0}, {2.1f,0,-.9f}, {.2f,1.6f,0}, .01f, 1);
    check(b.tip_speed < .001f && b.grip_speed < .001f);
    // Mixing the two feed modes must not reuse a stale head reference.
    b.Feed({2.1f,0,0}, {2.1f,0,-.9f}, .01f);
    b.FeedRelative({2.1f,0,0}, {2.1f,0,-.9f}, {.3f,1.6f,0}, .01f, 1);
    check(b.tip_speed == 0 && b.grip_speed == 0);
  }

  // Committed-pose blade: grip is the committed weapon origin in world
  // space; direction is the tracked blade direction carried by the root
  // basis only (native blade axis/length remain untraced assumptions).
  {
    ArmPoseMatrix identity{{1,0,0,0, 0,1,0,0, 0,0,1,0}};
    ArmPoseMatrix weapon{{1,0,0,0.1f, 0,1,0,1.2f, 0,0,1,-0.3f}};
    Vec3 grip{9,9,9}, tip{9,9,9};
    check(BladeFromCommittedPose(identity, weapon, {0,0,-1}, 0.9f, grip, tip));
    check(near(grip.x, 0.1f) && near(grip.y, 1.2f) && near(grip.z, -0.3f));
    check(near(tip.z, -1.2f) && std::fabs(VLen(VAdd(tip, VScale(grip, -1.0f))) - 0.9f) < 1e-4f);
    // Yawed + translated root rotates the direction, never the length.
    ArmPoseMatrix yawed{{0,0,1,10, 0,1,0,20, -1,0,0,30}};
    check(BladeFromCommittedPose(yawed, weapon, {0,0,-1}, 0.9f, grip, tip));
    check(near(grip.x, 9.7f) && near(grip.y, 21.2f) && near(grip.z, 29.9f));
    check(near(tip.x, 8.8f) && near(tip.z, 29.9f));
    // Refusals leave outputs untouched.
    grip = {9,9,9}; tip = {8,8,8};
    ArmPoseMatrix sheared{{1,1,0,0, 0,1,0,0, 0,0,1,0}};
    check(!BladeFromCommittedPose(sheared, weapon, {0,0,-1}, 0.9f, grip, tip));
    check(!BladeFromCommittedPose(identity, weapon, {0,0,0}, 0.9f, grip, tip));
    check(!BladeFromCommittedPose(identity, weapon, {0,0,-1}, 0.0f, grip, tip));
    check(!BladeFromCommittedPose(identity, weapon, {NAN,0,0}, 0.9f, grip, tip));
    check(grip.x == 9.0f && tip.x == 8.0f);
  }

  std::printf("combat math: %d failures\n", failures);
  return failures ? 1 : 0;
}
