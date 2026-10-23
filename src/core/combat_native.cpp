#include "combat_native.h"

#include <mutex>
#include "../common/memory.h"

namespace dsvr {
namespace {
std::mutex g_mutex;
CombatNativeStatus g_status;

// Qwen: this adapter reads equipment; it deliberately does not dispatch damage.
// 140322910 is a health setter, not melee. Its callers include zero/max-HP and
// replicated-state paths. Even the split HP+delta paths still lack a qualified
// delta producer/outcome contract. See docs/NATIVE_MELEE_CONTRACT.md; do not turn
// a plausible EDX value into a new native-call signature.

// Facts recovered from the pinned EXE and offline native corpus, not inferred
// from IDs: PlayerIns vtable[0x3a0/8] -> 35b880 -> 20aff0 -> 31b750.
// 35b880 delegates PlayerIns+840. 20aff0 maps -2 (left) through slot+0xc,
// -1 (right) through slot+0x10. 31b750 bounds index<20 and reads +24+index*4.
// Corpus batches 000372:36, 000259:45, 000336:48; input caller 396860
// looks up weapon parameters from these returned IDs via 532b20.
constexpr unsigned char kPlayerAccessor[]{0x48,0x8b,0x89,0x40,0x08,0,0,0xe9,0x64,0xf7,0xea,0xff};
constexpr unsigned char kSlotAccessor[]{0x83,0xfa,0xfe,0x75,0x0a,0x8b,0x51,0x0c,0x03,0xd2,0xe9,0x51,0x07,0x11,0};
constexpr unsigned char kIdAccessor[]{0x83,0xfa,0x13,0x77,0x08,0x48,0x63,0xc2,0x8b,0x44,0x81,0x24,0xc3,0x83,0xc8,0xff,0xc3};

bool LocalPlayer(std::uintptr_t base, std::uintptr_t& player) {
  std::uintptr_t world = 0;
  return GuardedRead(base + 0x1c77e50, world) && world &&
      GuardedRead(world + 0x68, player) && player;
}
}  // namespace

bool CombatNativeReadEquipment(std::uintptr_t expected_player, CombatEquipment& out) {
  out = {};
  const auto refuse = [&]() { CombatNativeInvalidateEquipment(); return false; };
  const auto base = ModuleBase(nullptr);
  std::uintptr_t player = 0, vtable = 0, accessor = 0, assembly = 0, stance = 0;
  if (!expected_player || !base || !LocalPlayer(base, player) || player != expected_player ||
      !GuardedRead(player, vtable) || vtable != base + 0x13251f0 ||
      !GuardedRead(vtable + 0x3a0, accessor) || accessor != base + 0x35b880 ||
      !BytesMatch(accessor, kPlayerAccessor, sizeof(kPlayerAccessor)) ||
      !BytesMatch(base + 0x20aff0, kSlotAccessor, sizeof(kSlotAccessor)) ||
      !BytesMatch(base + 0x31b750, kIdAccessor, sizeof(kIdAccessor)) ||
      !GuardedRead(player + 0x840, assembly) || !assembly ||
      !GuardedRead(player + 0x198, stance) || !stance) return refuse();
  CombatEquipment result{};
  result.player = player;
  if (!GuardedRead(stance, result.native_stance) || result.native_stance < 0 ||
      result.native_stance > 3) return refuse();
  int slots[2]{};
  for (int side = 0; side < 2; ++side) {
    if (!GuardedRead(assembly + (side ? 0x10 : 0xc), slots[side]) ||
        slots[side] < 0 || slots[side] >= 10 ||
        !GuardedRead(assembly + 0x24 + static_cast<std::uintptr_t>(slots[side] * 2 + side) * 4,
                     result.weapon_id[side]) || result.weapon_id[side] < -1) return refuse();
  }
  // Detect scene/equipment replacement during the guarded snapshot. A stale
  // weapon profile must never be applied to the next actor or weapon slot.
  std::uintptr_t current_player=0,current_assembly=0,current_stance=0,current_vtable=0;
  int current_stance_value=-1;
  if (!LocalPlayer(base, current_player) || current_player != player ||
      !GuardedRead(player,current_vtable) || current_vtable!=vtable ||
      !GuardedRead(player+0x198,current_stance) || current_stance!=stance ||
      !GuardedRead(current_stance,current_stance_value) ||
      current_stance_value!=result.native_stance ||
      !GuardedRead(player + 0x840, current_assembly) || current_assembly != assembly) return refuse();
  for (int side = 0; side < 2; ++side) {
    int slot = -1, id = -1;
    if (!GuardedRead(assembly + (side ? 0x10 : 0xc), slot) || slot != slots[side] ||
        !GuardedRead(assembly + 0x24 + static_cast<std::uintptr_t>(slot * 2 + side) * 4, id) ||
        id != result.weapon_id[side]) return refuse();
  }
  out = result;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status.equipment = result;
  g_status.equipment_bound = true;
  return true;
}
void CombatNativeInvalidateEquipment() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status.equipment_bound = false;
  g_status.equipment = {};
}
CombatNativeStatus CombatNativeGetStatus() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_status;
}
const char* CombatNativeLimitation() {
  return "Physical damage, shield contact blocking and parrying require a qualified native contact/damage binding. No HP edits or canned attacks are substituted.";
}
}  // namespace dsvr
