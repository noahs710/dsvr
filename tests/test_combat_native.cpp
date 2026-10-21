// Native accessor fixture reconstructed from the pinned EXE's complete simple
// accessor chain. This verifies fail-closed binding; it is not live hit evidence.
#include "../src/core/combat_native.h"
#include "../src/common/memory.h"
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace {
constexpr std::uintptr_t base = 0x10000000, world = 0x20000000,
    player = 0x30000000, assembly = 0x40000000, stance = 0x50000000;
std::unordered_map<std::uintptr_t, unsigned char> bytes;
int failures = 0;
unsigned mutation = 0;
template<class T> void Put(std::uintptr_t address, T value) {
  const auto* p = reinterpret_cast<const unsigned char*>(&value);
  for (std::size_t i = 0; i < sizeof(value); ++i) bytes[address+i] = p[i];
}
void PutBytes(std::uintptr_t address, const unsigned char* data, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) bytes[address+i] = data[i];
}
void Check(bool value, const char* message) {
  if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
void Fixture() {
  bytes.clear();mutation=0;
  Put(base+0x1c77e50,world); Put(world+0x68,player);
  Put(player,base+0x13251f0); Put(base+0x1325590,base+0x35b880);
  Put(player+0x840,assembly); Put(player+0x198,stance); Put(stance,1);
  Put(assembly+0xc,1); Put(assembly+0x10,0);
  Put(assembly+0x2c,1400000); Put(assembly+0x28,100000);
  const unsigned char a[]{0x48,0x8b,0x89,0x40,0x08,0,0,0xe9,0x64,0xf7,0xea,0xff};
  const unsigned char b[]{0x83,0xfa,0xfe,0x75,0x0a,0x8b,0x51,0x0c,0x03,0xd2,0xe9,0x51,0x07,0x11,0};
  const unsigned char c[]{0x83,0xfa,0x13,0x77,0x08,0x48,0x63,0xc2,0x8b,0x44,0x81,0x24,0xc3,0x83,0xc8,0xff,0xc3};
  PutBytes(base+0x35b880,a,sizeof(a)); PutBytes(base+0x20aff0,b,sizeof(b)); PutBytes(base+0x31b750,c,sizeof(c));
}
}
namespace dsvr {
std::uintptr_t ModuleBase(const wchar_t*) { return base; }
bool GuardedReadBytes(std::uintptr_t address, void* out, std::size_t size) {
  auto* p = static_cast<unsigned char*>(out);
  for (std::size_t i = 0; i < size; ++i) {
    const auto found = bytes.find(address+i);
    if (found == bytes.end()) return false;
    p[i] = found->second;
  }
  if (address==assembly+0x28 && mutation) {
    const auto change=mutation;mutation=0;
    if (change==1) Put(stance,2);
    if (change==2) Put(player+0x198,stance+8);
    if (change==3) Put(player,base+0x13251f8);
  }
  return true;
}
bool BytesMatch(std::uintptr_t address, const void* expected, std::size_t size) {
  const auto* p = static_cast<const unsigned char*>(expected);
  for (std::size_t i = 0; i < size; ++i) {
    const auto found = bytes.find(address+i);
    if (found == bytes.end() || found->second != p[i]) return false;
  }
  return true;
}
}
int main() {
  using namespace dsvr;
  Fixture();
  CombatEquipment equipment;
  Check(CombatNativeReadEquipment(player,equipment) && equipment.weapon_id[0] == 1400000 &&
        equipment.weapon_id[1] == 100000, "native selected left/right slots resolve independent weapon IDs");
  const auto status = CombatNativeGetStatus();
  Check(status.equipment_bound && !status.physical_damage_bound && !status.shield_contact_bound &&
        !status.two_hand_rules_bound, "equipment qualification never claims unbound native combat outcomes");
  Check(!CombatNativeReadEquipment(player+8,equipment) && !CombatNativeGetStatus().equipment_bound,
        "a foreign or replaced player cannot reuse saved grip identity");
  Fixture(); Put(assembly+0xc,10);
  Check(!CombatNativeReadEquipment(player,equipment), "out-of-range native slot fails closed");
  Fixture(); bytes[base+0x31b750] = 0x90;
  Check(!CombatNativeReadEquipment(player,equipment), "changed accessor instructions disable the binding");
  Fixture(); Put(player,base+0x13251f8);
  Check(!CombatNativeReadEquipment(player,equipment), "foreign actor type cannot bind equipment");
  Fixture(); Put(assembly+0x28,-1);
  Check(CombatNativeReadEquipment(player,equipment) && equipment.weapon_id[1] == -1,
        "an empty native equipment slot is represented explicitly");
  for (unsigned change=1;change<=3;++change) {
    Fixture();mutation=change;
    Check(!CombatNativeReadEquipment(player,equipment) &&
          !CombatNativeGetStatus().equipment_bound && equipment.player==0,
          "stance value, stance owner and actor type are revalidated before publication");
  }
  CombatNativeInvalidateEquipment();
  Check(!CombatNativeGetStatus().equipment_bound, "lifecycle invalidation clears published equipped identity");
  if (failures) return 1;
  std::puts("combat native accessor tests passed");
  return 0;
}
