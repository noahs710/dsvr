#include "weapon_grip.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <mutex>
#include <optional>
#include <sstream>
#include <windows.h>

namespace dsvr {
namespace {
constexpr std::size_t kMaxProfiles = 256;
std::mutex g_mutex;
std::filesystem::path g_path;
std::vector<WeaponGripProfile> g_profiles;
std::optional<WeaponGripProfile> g_preview;

bool PlacementValid(const WeaponGripPlacement& p) {
  return FiniteVec(p.position_m) && FiniteVec(p.rotation_deg) &&
      std::fabs(p.position_m.x) <= 1 && std::fabs(p.position_m.y) <= 1 &&
      std::fabs(p.position_m.z) <= 1 && std::fabs(p.rotation_deg.x) <= 180 &&
      std::fabs(p.rotation_deg.y) <= 180 && std::fabs(p.rotation_deg.z) <= 180;
}
auto Find(std::vector<WeaponGripProfile>& profiles, int id, bool left) {
  return std::find_if(profiles.begin(), profiles.end(), [=](const auto& p) {
    return p.weapon_id == id && p.dominant_left == left;
  });
}
bool WriteProfiles(const std::vector<WeaponGripProfile>& profiles) {
  if (g_path.empty()) return false;
  auto temporary = g_path;
  temporary += L".tmp." + std::to_wstring(GetCurrentProcessId());
  {
    std::ofstream file(temporary, std::ios::trunc);
    if (!file) return false;
    file.imbue(std::locale::classic());
    file << "DSVR_GRIPS 1\n" << std::setprecision(9);
    for (const auto& p : profiles) {
      file << p.weapon_id << ' ' << p.dominant_left << ' ' << p.support_enabled;
      for (const auto* placement : {&p.primary, &p.support})
        file << ' ' << placement->position_m.x << ' ' << placement->position_m.y
             << ' ' << placement->position_m.z << ' ' << placement->rotation_deg.x
             << ' ' << placement->rotation_deg.y << ' ' << placement->rotation_deg.z;
      file << '\n';
    }
    file.flush();
    if (!file) { file.close(); std::error_code e; std::filesystem::remove(temporary, e); return false; }
  }
  if (MoveFileExW(temporary.c_str(), g_path.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
  std::error_code error;
  std::filesystem::remove(temporary, error);
  return false;
}
Vec3 RotateEuler(Vec3 p, Vec3 degrees) {
  constexpr float radians = 0.017453292519943295f;
  const float x = degrees.x * radians, y = degrees.y * radians, z = degrees.z * radians;
  Vec3 a{p.x, p.y * std::cos(x) - p.z * std::sin(x), p.y * std::sin(x) + p.z * std::cos(x)};
  Vec3 b{a.x * std::cos(y) + a.z * std::sin(y), a.y, -a.x * std::sin(y) + a.z * std::cos(y)};
  return {b.x * std::cos(z) - b.y * std::sin(z), b.x * std::sin(z) + b.y * std::cos(z), b.z};
}
}  // namespace

bool WeaponGripValid(const WeaponGripProfile& p) {
  return p.weapon_id >= 0 && PlacementValid(p.primary) && PlacementValid(p.support);
}
bool WeaponGripsInitialize(const std::wstring& path) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (path.empty()) return false;
  const std::filesystem::path requested(path);
  std::error_code error;
  const bool exists = std::filesystem::exists(requested, error);
  if (error) return false;
  std::vector<WeaponGripProfile> profiles;
  if (exists) {
    std::ifstream file(requested);
    file.imbue(std::locale::classic());
    std::string header;
    if (!std::getline(file, header) || header != "DSVR_GRIPS 1") return false;
    std::string line;
    while (std::getline(file, line)) {
      if (line.empty()) continue;
      WeaponGripProfile p;
      int left = 0, support = 0;
      std::istringstream record(line);
      record.imbue(std::locale::classic());
      if (!(record >> p.weapon_id >> left >> support)) return false;
      for (auto* placement : {&p.primary, &p.support})
        if (!(record >> placement->position_m.x >> placement->position_m.y
              >> placement->position_m.z >> placement->rotation_deg.x
              >> placement->rotation_deg.y >> placement->rotation_deg.z)) return false;
      record >> std::ws;
      if (!record.eof() || (left != 0 && left != 1) || (support != 0 && support != 1)) return false;
      p.dominant_left = left != 0;
      p.support_enabled = support != 0;
      if (!WeaponGripValid(p) || profiles.size() >= kMaxProfiles ||
          Find(profiles, p.weapon_id, p.dominant_left) != profiles.end()) return false;
      profiles.push_back(p);
    }
    if (!file.eof()) return false;
  }
  g_path = requested;
  g_profiles = std::move(profiles);
  g_preview.reset();
  return true;
}
bool WeaponGripGet(int id, bool left, WeaponGripProfile& out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  out = {};
  out.weapon_id = id;
  out.dominant_left = left;
  if (id < 0) return false;
  if (g_preview && g_preview->weapon_id == id && g_preview->dominant_left == left) {
    out = *g_preview;
    return true;
  }
  const auto found = Find(g_profiles, id, left);
  if (found == g_profiles.end()) return false;
  out = *found;
  return true;
}
std::vector<WeaponGripProfile> WeaponGripProfiles() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_profiles;
}
bool WeaponGripSave(const WeaponGripProfile& profile) {
  if (!WeaponGripValid(profile)) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  auto updated = g_profiles;
  const auto found = Find(updated, profile.weapon_id, profile.dominant_left);
  if (found == updated.end()) {
    if (updated.size() >= kMaxProfiles) return false;
    updated.push_back(profile);
  } else *found = profile;
  if (!WriteProfiles(updated)) return false;
  g_profiles = std::move(updated);
  if (g_preview && g_preview->weapon_id == profile.weapon_id &&
      g_preview->dominant_left == profile.dominant_left) g_preview.reset();
  return true;
}
bool WeaponGripReset(int id, bool left) {
  if (id < 0) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  auto updated = g_profiles;
  const auto found = Find(updated, id, left);
  if (found == updated.end()) {
    if (g_preview && g_preview->weapon_id == id && g_preview->dominant_left == left) g_preview.reset();
    return true;
  }
  updated.erase(found);
  if (!WriteProfiles(updated)) return false;
  g_profiles = std::move(updated);
  if (g_preview && g_preview->weapon_id == id && g_preview->dominant_left == left) g_preview.reset();
  return true;
}
bool WeaponGripPreview(const WeaponGripProfile& profile) {
  if (!WeaponGripValid(profile)) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_preview = profile;
  return true;
}
void WeaponGripClearPreview() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_preview.reset();
}
bool WeaponGripApplyPrimary(const WeaponGripPlacement& placement,
                            const ArmPoseMatrix& reference_hand,
                            const ArmPoseMatrix& reference_weapon,
                            float units, ArmPoseMatrix& desired_hand, Vec3& grip_target) {
  if (!PlacementValid(placement) || !std::isfinite(units) || units <= 0 ||
      !FiniteVec(grip_target) || !arm_pose_detail::ValidMatrix(reference_hand) ||
      !arm_pose_detail::ValidMatrix(reference_weapon) ||
      !arm_pose_detail::ValidMatrix(desired_hand)) return false;
  // W = desired hand * inverse(reference hand) * reference weapon.
  Vec3 native_hand[3], native_weapon[3], desired[3], weapon[3];
  for (int i = 0; i < 3; ++i) {
    native_hand[i] = VNorm(arm_pose_detail::Column(reference_hand, i));
    native_weapon[i] = VNorm(arm_pose_detail::Column(reference_weapon, i));
    desired[i] = VNorm(arm_pose_detail::Column(desired_hand, i));
  }
  for (int i = 0; i < 3; ++i) {
    weapon[i] = {};
    for (int j = 0; j < 3; ++j)
      weapon[i] = VAdd(weapon[i], VScale(desired[j], VDot(native_hand[j], native_weapon[i])));
  }
  const Vec3 axes[3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  Vec3 rotation[3], corrected_weapon[3];
  for (int i = 0; i < 3; ++i) rotation[i] = RotateEuler(axes[i], placement.rotation_deg);
  for (int i = 0; i < 3; ++i) {
    // Inverse of the local grip rotation is its transpose.
    corrected_weapon[i] = {};
    for (int j = 0; j < 3; ++j) {
      const float values[3]{rotation[j].x, rotation[j].y, rotation[j].z};
      corrected_weapon[i] = VAdd(corrected_weapon[i], VScale(weapon[j], values[i]));
    }
  }
  ArmPoseMatrix result = desired_hand;
  for (int i = 0; i < 3; ++i) {
    Vec3 column{};
    for (int j = 0; j < 3; ++j)
      column = VAdd(column, VScale(corrected_weapon[j], VDot(native_weapon[j], native_hand[i])));
    const float scale = VLen(arm_pose_detail::Column(desired_hand, i));
    result.m[i] = column.x * scale; result.m[4 + i] = column.y * scale; result.m[8 + i] = column.z * scale;
  }
  const float translation[3]{placement.position_m.x, placement.position_m.y, placement.position_m.z};
  Vec3 shifted = grip_target;
  for (int i = 0; i < 3; ++i)
    shifted = VAdd(shifted, VScale(corrected_weapon[i], -translation[i] * units));
  if (!FiniteVec(shifted) || !arm_pose_detail::ValidMatrix(result)) return false;
  desired_hand = result;
  grip_target = shifted;
  return true;
}
bool WeaponGripApplySupportRotation(Vec3 degrees, const ArmPoseMatrix& weapon,
                                    ArmPoseMatrix& desired) {
  if (!PlacementValid({{},degrees}) || !arm_pose_detail::ValidMatrix(weapon) ||
      !arm_pose_detail::ValidMatrix(desired)) return false;
  Vec3 axes[3];
  for(int i=0;i<3;++i) axes[i]=VNorm(arm_pose_detail::Column(weapon,i));
  ArmPoseMatrix result=desired;
  for(int i=0;i<3;++i) {
    const auto column=arm_pose_detail::Column(desired,i);
    const auto local=RotateEuler({VDot(axes[0],column),VDot(axes[1],column),VDot(axes[2],column)},degrees);
    const auto rotated=VAdd(VScale(axes[0],local.x),VAdd(VScale(axes[1],local.y),VScale(axes[2],local.z)));
    result.m[i]=rotated.x;result.m[4+i]=rotated.y;result.m[8+i]=rotated.z;
  }
  if(!arm_pose_detail::ValidMatrix(result))return false;
  desired=result;return true;
}
bool WeaponGripSolveTwoHand(const WeaponGripProfile& profile,
                            const ArmPoseMatrix& reference_hand,
                            const ArmPoseMatrix& reference_weapon,
                            float units, Vec3 primary_anchor, Vec3 offhand_target,
                            bool offhand_grip, std::uint64_t epoch,
                            WeaponTwoHandState& state, ArmPoseMatrix& desired,
                            Vec3& weapon_target, Vec3& support_target) {
  support_target = {};
  if (!WeaponGripValid(profile) || !profile.support_enabled || !epoch ||
      !FiniteVec(primary_anchor) || !FiniteVec(offhand_target) ||
      !FiniteVec(weapon_target) || !std::isfinite(units) || units <= 0 ||
      !arm_pose_detail::ValidMatrix(reference_hand) ||
      !arm_pose_detail::ValidMatrix(reference_weapon) ||
      !arm_pose_detail::ValidMatrix(desired)) { state.Reset(); return false; }
  if (state.binding_epoch && (state.binding_epoch != epoch || state.weapon_id != profile.weapon_id ||
      state.dominant_left != profile.dominant_left)) {
    state.active = false;
    state.must_release = offhand_grip;
  }
  state.binding_epoch = epoch;
  state.weapon_id = profile.weapon_id;
  state.dominant_left = profile.dominant_left;
  if (!offhand_grip) { state.active = false; state.must_release = false; return false; }
  if (state.must_release) return false;
  Vec3 weapon[3];
  for (int i = 0; i < 3; ++i) {
    weapon[i] = {};
    for (int j = 0; j < 3; ++j)
      weapon[i] = VAdd(weapon[i], VScale(VNorm(arm_pose_detail::Column(desired, j)),
          VDot(VNorm(arm_pose_detail::Column(reference_hand, j)),
               VNorm(arm_pose_detail::Column(reference_weapon, i)))));
  }
  const auto local = VScale(profile.support.position_m, units);
  Vec3 handle = VAdd(weapon_target, VAdd(VScale(weapon[0], local.x),
      VAdd(VScale(weapon[1], local.y), VScale(weapon[2], local.z))));
  if (!state.active && VLen(arm_pose_detail::Sub(handle, offhand_target)) > 0.14f * units)
    return false;
  const Vec3 from = arm_pose_detail::Sub(handle, primary_anchor);
  const Vec3 to = arm_pose_detail::Sub(offhand_target, primary_anchor);
  const float span = VLen(from), separation = VLen(to);
  if (span < 0.025f * units || separation < 0.025f * units ||
      separation > std::max(0.6f * units, span * 4)) {
    state.active = false;
    state.must_release = true;
    return false;
  }
  const auto rotation = arm_pose_detail::FromTo(from, to);
  ArmPoseMatrix result = desired;
  for (int i = 0; i < 3; ++i) {
    const auto column = rotation.Apply(arm_pose_detail::Column(desired, i));
    result.m[i] = column.x; result.m[4 + i] = column.y; result.m[8 + i] = column.z;
  }
  const Vec3 origin = VAdd(primary_anchor, rotation.Apply(arm_pose_detail::Sub(weapon_target, primary_anchor)));
  const Vec3 support = VAdd(primary_anchor, rotation.Apply(from));
  if (!FiniteVec(origin) || !FiniteVec(support) || !arm_pose_detail::ValidMatrix(result)) {
    state.Reset(); return false;
  }
  desired = result;
  weapon_target = origin;
  support_target = support;
  state.active = true;
  return true;
}
}  // namespace dsvr
