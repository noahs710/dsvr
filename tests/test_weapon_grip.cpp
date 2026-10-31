#include "../src/core/weapon_grip.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
int failures = 0;
void Check(bool value, const char* message) {
  if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }
dsvr::ArmPoseMatrix Identity() { return {{1,0,0,0,0,1,0,0,0,0,1,0}}; }
}
int main() {
  using namespace dsvr;
  const auto directory = std::filesystem::temp_directory_path() /
      (L"dsvr-grip-test-" + std::to_wstring(GetCurrentProcessId()));
  std::filesystem::create_directories(directory);
  const auto path = directory / L"grips.txt";
  std::filesystem::remove(path);
  Check(WeaponGripsInitialize(path.wstring()), "a new profile store starts empty");
  WeaponGripProfile right;
  right.weapon_id = 100000;
  right.primary.position_m = {0.01f, -0.03f, 0.08f};
  right.primary.rotation_deg = {0, 15, -20};
  Check(WeaponGripSave(right), "save a weapon-local right-handed grip");
  auto left = right;
  left.dominant_left = true;
  left.primary.position_m.x = -0.02f;
  Check(WeaponGripSave(left), "left-handed placement is a separate profile");
  Check(WeaponGripsInitialize(path.wstring()), "profiles reload from disk");
  WeaponGripProfile read;
  Check(WeaponGripGet(100000, false, read) && Near(read.primary.position_m.x, 0.01f) &&
        Near(read.primary.rotation_deg.z, -20), "death/load-independent right grip survives reload");
  Check(WeaponGripGet(100000, true, read) && Near(read.primary.position_m.x, -0.02f),
        "handedness does not mix saved placements");
  auto preview = right;
  preview.primary.position_m.y = 0.2f;
  Check(WeaponGripPreview(preview) && WeaponGripGet(100000, false, read) &&
        Near(read.primary.position_m.y, 0.2f), "preview immediately overrides the matching live profile");
  WeaponGripClearPreview();
  Check(WeaponGripGet(100000, false, read) && Near(read.primary.position_m.y, -0.03f),
        "closing preview restores the saved placement without disk changes");
  auto invalid = right;
  invalid.primary.position_m.z = std::numeric_limits<float>::quiet_NaN();
  Check(!WeaponGripSave(invalid) && !WeaponGripPreview(invalid), "nonfinite placements never reach disk or pose preview");
  {
    std::ofstream file(path, std::ios::trunc);
    file << "DSVR_GRIPS 1\n100000 0 1 broken record\n";
  }
  Check(!WeaponGripsInitialize(path.wstring()) && WeaponGripGet(100000, false, read) &&
        Near(read.primary.position_m.z, 0.08f), "a corrupt reload preserves all previous live profiles");
  Check(WeaponGripReset(100000, true) && !WeaponGripGet(100000, true, read),
        "reset removes only the selected weapon and handedness");
  Check(WeaponGripsInitialize(path.wstring()) && WeaponGripGet(100000, false, read),
        "reset is persisted without losing the other hand");

  const auto identity = Identity();
  auto desired = identity;
  Vec3 target{1,2,3};
  WeaponGripPlacement offset{{0,0,0.1f},{0,0,90}};
  Check(WeaponGripApplyPrimary(offset, identity, identity, 1, desired, target),
        "a saved placement can drive the native pose seam");
  Check(Near(target.x, 1) && Near(target.y, 2) && Near(target.z, 2.9f) &&
        Near(desired.m[0], 0) && Near(desired.m[4], -1) && Near(desired.m[1], 1),
        "grip translation and rotation move the weapon opposite the chosen hand placement");

  WeaponGripProfile two;
  two.weapon_id = 200000;
  two.support.position_m = {0,0,0.2f};
  WeaponTwoHandState state;
  desired = identity;
  Vec3 weapon{}, support{};
  Check(!WeaponGripSolveTwoHand(two, identity, identity, 1, {}, {0,0,0.2f}, false,
                               1, state, desired, weapon, support),
        "crossing the handle without holding grip does not attach");
  Check(!WeaponGripSolveTwoHand(two, identity, identity, 1, {}, {1,0,0.2f}, true,
                               1, state, desired, weapon, support),
        "holding grip away from the saved handle does not attach");
  Check(WeaponGripSolveTwoHand(two, identity, identity, 1, {}, {0,0,0.2f}, true,
                              1, state, desired, weapon, support),
        "holding grip near the handle attaches the support hand");
  desired = identity; weapon = {};
  Check(WeaponGripSolveTwoHand(two, identity, identity, 1, {}, {0.3f,0,0}, true,
                              1, state, desired, weapon, support) &&
        Near(support.x, 0.2f) && Near(support.y, 0) && Near(support.z, 0) &&
        Near(desired.m[2], 1),
        "attached offhand aims the weapon while preserving fixed handle separation and reach");
  desired = identity; weapon = {};
  Check(!WeaponGripSolveTwoHand(two, identity, identity, 1, {}, {0,0,0.2f}, true,
                               2, state, desired, weapon, support) && state.must_release,
        "a new actor/reference epoch cannot inherit a held support grip");
  Check(!WeaponGripSolveTwoHand(two, identity, identity, 1, {}, {0,0,0.2f}, false,
                               2, state, desired, weapon, support) && !state.must_release,
        "releasing the support grip rearms attachment");
  // Slice 3 mass table: per-weapon response, whole-file refusal on malformed.
  const auto mass_path = directory / L"mass.txt";
  {
    std::ofstream mass(mass_path, std::ios::trunc);
    mass << "DSVR_MASS 1\n100000 3.5 1.2 0.6\n200000 0.8 0.2 1.0\n";
  }
  Check(WeaponMassLoad(mass_path.wstring()), "mass table loads");
  WeaponMassEntry entry;
  Check(WeaponMassGet(100000, entry) && Near(entry.mass_kg, 3.5f) &&
        Near(entry.inertia, 1.2f) && Near(entry.response, 0.6f),
        "heavy weapon response lags");
  Check(WeaponMassGet(200000, entry) && Near(entry.response, 1.0f),
        "dagger response is transparent");
  Check(!WeaponMassGet(999999, entry), "unknown weapon is not a mass");
  {
    std::ofstream mass(mass_path, std::ios::trunc);
    mass << "DSVR_MASS 1\n100000 3.5 1.2 99.0\n";
  }
  Check(!WeaponMassLoad(mass_path.wstring()) && WeaponMassGet(100000, entry) &&
        Near(entry.response, 0.6f),
        "corrupt reload preserves the previous live table");
  std::filesystem::remove(mass_path);
  std::filesystem::remove(path);
  std::filesystem::remove(directory);
  if (failures) return 1;
  std::puts("weapon grip tests passed");
  return 0;
}
