#include "../src/core/combat_contact.h"

#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

static dsvr::BladeKinematics SwingBlade() {
  dsvr::BladeKinematics blade;
  blade.Feed({0, 0, 0}, {0.5f, 0, 0}, 0.016f);
  blade.Feed({0.02f, 0, 0}, {0.58f, 0, 0}, 0.016f);
  return blade;
}

int main() {
  using namespace dsvr;
  // The middle of a translating blade crosses this target. Neither endpoint
  // path nor either frame's static blade touches it.
  {
    BladeKinematics blade;
    blade.valid = true;
    blade.prev_grip = {0, 0, 0}; blade.prev_tip = {0, 0, -1};
    blade.grip = {0.4f, 0, 0}; blade.tip = {0.4f, 0, -1};
    blade.tip_speed = 4;
    BladeContactTarget targets[] = {{17, {0.2f, 0, -0.5f}, 0.005f}};
    RehitGate gate;
    ContactResult result;
    CHECK(ClassifyWeaponContact(blade, GestureKind::Slash, targets, 0.8f,
                                1, 280, 1000, false, gate, result));
    CHECK(result.hit.target_id == 17 && result.hit.haptic.amplitude == 0);
    CHECK(!ClassifyWeaponContact(blade, GestureKind::Slash, targets, 0.8f,
                                 1, 280, 1000, false, gate, result));
  }
  // Weapon contact: overlap at speed classifies as a hit with a scale;
  // the same swing far from the target is a miss (no event, no damage).
  {
    auto blade = SwingBlade();
    RehitGate gate;
    BladeContactTarget targets[] = {{7, {0.55f, 0, 0}, 0.1f}};
    ContactResult hit;
    CHECK(ClassifyWeaponContact(blade, GestureKind::Slash, targets, 0.8f, 1.0f,
                                280, 1000, true, gate, hit));
    CHECK(hit.event == ContactEvent::WeaponHit && hit.hit.target_id == 7);
    CHECK(hit.hit.damage_scale > 0.0f);
    BladeContactTarget far[] = {{7, {2.0f, 0, 0}, 0.1f}};
    ContactResult miss;
    CHECK(!ClassifyWeaponContact(blade, GestureKind::Slash, far, 0.8f, 1.0f,
                                 280, 1000, true, gate, miss));
    CHECK(miss.event == ContactEvent::None);
    // Rehit window: the same target cannot reclassify until it passes,
    // and a despawned target (empty list) produces nothing.
    ContactResult again;
    CHECK(!ClassifyWeaponContact(blade, GestureKind::Slash, targets, 0.8f, 1.0f,
                                 280, 1100, true, gate, again));
    CHECK(ClassifyWeaponContact(blade, GestureKind::Slash, targets, 0.8f, 1.0f,
                                280, 1300, true, gate, again));
    ContactResult gone;
    CHECK(!ClassifyWeaponContact(blade, GestureKind::Slash, {}, 0.8f, 1.0f,
                                 280, 2000, true, gate, gone));
  }
  // Defence: contact inside the parry window is a parry; the same contact
  // later is a block; no raise or no overlap is nothing.
  {
    ContactParams p;
    CHECK(ClassifyDefence(true, 1000, 1100, true, p) == ContactEvent::Parry);
    CHECK(ClassifyDefence(true, 1000, 1500, true, p) == ContactEvent::ShieldBlock);
    CHECK(ClassifyDefence(false, 1000, 1100, true, p) == ContactEvent::None);
    CHECK(ClassifyDefence(true, 1000, 1100, false, p) == ContactEvent::None);
    CHECK(ClassifyDefence(true, 1200, 1100, true, p) == ContactEvent::None);
    CHECK(ClassifyDefence(true, -1, 1100, true, p) == ContactEvent::None);
  }
  // Support grip: attaches only while held near the handle.
  {
    SupportGrip grip;
    CHECK(!grip.Update(true, 0.30f));
    CHECK(grip.Update(true, 0.10f) && grip.attached);
    CHECK(grip.Update(true, 0.12f));           // stays while held near
    CHECK(!grip.Update(false, 0.10f));         // release detaches at once
    CHECK(!grip.attached);
  }
  // Slice 1 segment envelope: attach at any point of the committed blade
  // segment, not just the handle. Hilt = full two-hand (fraction ~0),
  // mid-blade = halfsword (fraction ~0.5). Hysteresis: boundary hover
  // between attach and release distances must not chatter.
  {
    SupportGrip grip;
    const Vec3 bg{0, 1.4f, 0}, bt{0, 1.4f, -0.9f};
    ContactParams p;
    // Mid-blade grab attaches where the old handle proxy refused.
    CHECK(grip.Update(true, {0.05f, 1.4f, -0.45f}, bg, bt, p));
    CHECK(grip.attached);
    CHECK(grip.grab_fraction > 0.4f && grip.grab_fraction < 0.6f);
    // Hilt grab reports fraction ~0.
    SupportGrip hilt;
    CHECK(hilt.Update(true, {0.05f, 1.4f, -0.02f}, bg, bt, p));
    CHECK(hilt.attached && hilt.grab_fraction < 0.1f);
    // World/native-unit segments scale metre envelopes before comparison.
    SupportGrip native_far;
    CHECK(!native_far.Update(true, {5, 1.4f, -45}, {0, 1.4f, 0},
                             {0, 1.4f, -90}, p, 1.0f));
    SupportGrip native_scaled;
    CHECK(native_scaled.Update(true, {5, 1.4f, -45}, {0, 1.4f, 0},
                               {0, 1.4f, -90}, p, 100.0f));
    CHECK(!native_scaled.Update(true, {25, 1.4f, -45}, {0, 1.4f, 0},
                                {0, 1.4f, -90}, p, 100.0f));
    // Far from the segment never attaches.
    SupportGrip far;
    CHECK(!far.Update(true, {0.5f, 1.4f, -0.45f}, bg, bt, p));
    // Release on squeeze release, and on leaving the release envelope.
    CHECK(!grip.Update(false, {0.05f, 1.4f, -0.45f}, bg, bt, p));
    CHECK(!grip.attached);
    CHECK(grip.Update(true, {0.05f, 1.4f, -0.45f}, bg, bt, p));
    CHECK(grip.Update(true, {0.17f, 1.4f, -0.45f}, bg, bt, p));  // held
    CHECK(!grip.Update(true, {0.25f, 1.4f, -0.45f}, bg, bt, p));  // left
    // Non-finite input fails closed.
    CHECK(!grip.Update(true, {NAN, 1.4f, -0.45f}, bg, bt, p));
      // Mesh, not hitbox: a hand floating beside the mid-blade (inside the
    // hilt tube but off the mesh) refuses, while mesh contact attaches.
    // At fraction ~0.5 the allowed radius is ~0.105.
    SupportGrip float_grip;
    CHECK(!float_grip.Update(true, {0.12f, 1.4f, -0.45f}, bg, bt, p));
    SupportGrip mesh_grip;
    CHECK(mesh_grip.Update(true, {0.05f, 1.4f, -0.45f}, bg, bt, p));
    // Hilt keeps the generous envelope (hand wraps the grip).
    SupportGrip hilt_wide;
    CHECK(hilt_wide.Update(true, {0.13f, 1.4f, -0.02f}, bg, bt, p));
  }
  // Roomscale: walking the blade into a target is locomotion, not an
  // attack. No event fires and the rehit gate stays unconsumed, so a
  // genuine swing immediately after still hits the same target.
  {
    BladeKinematics blade;
    RehitGate gate;
    const float dt = 1.0f / 90.0f;
    Vec3 head{0, 1.6f, 0}, grip{0, 1.4f, 0.2f}, tip{0, 1.4f, -0.7f};
    blade.FeedRelative(grip, tip, head, dt, 1);
    BladeContactTarget targets[] = {{7, {0, 1.4f, -0.4f}, 0.35f}};
    ContactResult walked;
    for (int frame = 0; frame < 45; ++frame) {
      head.z -= 1.4f * dt; grip.z -= 1.4f * dt; tip.z -= 1.4f * dt;
      blade.FeedRelative(grip, tip, head, dt, 1);
      CHECK(!ClassifyWeaponContact(blade, GestureKind::Slash, targets, 0.8f,
                                   1.0f, 280, 1000 + frame, true, gate,
                                   walked));
    }
    CHECK(walked.event == ContactEvent::None && blade.tip_speed < 0.05f);
    // The same blade, now swung relative to the head, hits at once.
    ContactResult swung;
    bool hit = false;
    for (int frame = 0; frame < 45 && !hit; ++frame) {
      grip.x += 3.0f * dt; tip.x += 3.0f * dt;
      blade.FeedRelative(grip, tip, head, dt, 1);
      hit = ClassifyWeaponContact(blade, blade.FrameKind({0, 0, -1}),
                                  targets, 0.8f, 1.0f, 280, 2000 + frame,
                                  true, gate, swung);
    }
    CHECK(hit && swung.event == ContactEvent::WeaponHit &&
          swung.hit.target_id == 7 && swung.hit.damage_scale > 0.0f);
  }
  std::puts("PASS combat contact classification");
  return 0;
}
