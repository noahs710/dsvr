#include <windows.h>
#include <MinHook.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "../src/common/config.h"
#include "../src/core/body_visibility.h"

namespace {
constexpr std::uintptr_t base = 0x140000000;
constexpr std::uint32_t id = 0x10203040, dirty_bit = 1u << 18;
using Records = std::array<unsigned char, 13 * 16>;
std::array<unsigned char, 0x900> player{};
std::array<unsigned char, 0x250> primary{}, alternate{}, replacement{};
std::array<unsigned char, 0x50> flver{};
std::array<unsigned char, 0x50> facegen{};
constexpr std::uintptr_t generated_face_resource = 0x12345000;
Records records{}, copied{};
std::uintptr_t world = 0x1000;
dsvr::Config config;
bool theater = false, running = true, signature_ok = true, fail_enable = false;
bool fail_records = false, replace_on_copy = false;
unsigned original_queues = 0, original_updates = 0, writes = 0, disables = 0;
unsigned part_reads = 0;
int queued_count = 0;
const void* queued_source = nullptr;
float last_delta = 0;
void* last_queue = nullptr;
std::uint32_t last_id = 0;
using Queue = void(__fastcall*)(void*, std::uint32_t, const void*, int);
using Update = void(__fastcall*)(void*, float);
Queue queue_callback = nullptr;
Update update_callback = nullptr;

template<class T, class Array>
void Put(Array& target, std::size_t offset, const T& value) {
  std::memcpy(target.data() + offset, &value, sizeof(value));
}
template<class T>
T Get(const void* target, std::size_t offset) {
  T result{};
  std::memcpy(&result, static_cast<const unsigned char*>(target) + offset, sizeof(result));
  return result;
}
std::uintptr_t Address(const auto& target) { return reinterpret_cast<std::uintptr_t>(target.data()); }
void __fastcall OriginalQueue(void* queue, std::uint32_t owner, const void* source, int count) {
  ++original_queues;
  last_queue = queue;
  last_id = owner;
  queued_count = count;
  queued_source = source;
  if (count == 13 && source) std::memcpy(copied.data(), source, copied.size());
}
void __fastcall OriginalUpdate(void* model, float delta) {
  ++original_updates;
  last_delta = delta;
  if (reinterpret_cast<std::uintptr_t>(model) != Get<std::uintptr_t>(player.data(), 0x850)) return;
  const auto dirty = Get<std::uint32_t>(model, 0x9c);
  const std::uint32_t zero = 0;
  std::memcpy(static_cast<unsigned char*>(model) + 0x9c, &zero, sizeof(zero));
  if ((dirty & dirty_bit) != 0)
    queue_callback(reinterpret_cast<void*>(0x2000), Get<std::uint32_t>(model, 0x10),
        reinterpret_cast<void*>(Get<std::uintptr_t>(model, 0x80)), Get<int>(model, 0x78));
}
void InitializeAlternate(auto& target) {
  Put(target, 0, base + 0x12eace8);
  Put(target, 0x10, id);
  Put(target, 0x18, Address(flver));
  Put(target, 0x78, 13);
  Put(target, 0x80, Address(records));
  Put(target, 0x1c0, generated_face_resource);
}
void Tick() { update_callback(reinterpret_cast<void*>(Get<std::uintptr_t>(player.data(), 0x850)), 0.0078125f); }
// The same check for an arbitrary set of hidden part slots, so a torso/leg
// request can be told apart from a head request record by record.
bool SlotsMasked(const Records& expected, std::initializer_list<unsigned> slots) {
  for (unsigned i = 0; i < copied.size(); ++i) {
    bool hidden_type = false;
    for (const unsigned slot : slots) if (i == slot * 16 + 9) hidden_type = true;
    if (copied[i] != (hidden_type ? expected[i] & 0xf0 : expected[i])) return false;
  }
  return true;
}
bool MaskedCorrectly(const Records& expected, bool generated_face = true) {
  for (unsigned i = 0; i < copied.size(); ++i) {
    const bool hidden_type = (generated_face && i == 9) || i == 1 * 16 + 9 || i == 2 * 16 + 9 || i == 6 * 16 + 9;
    if (copied[i] != (hidden_type ? expected[i] & 0xf0 : expected[i])) return false;
  }
  return true;
}
}

namespace dsvr {
Config DiagnosticsCurrentConfig() { return config; }
bool XrMenuPresentationActive() { return theater; }
bool XrSessionRunning() { return running; }
std::uintptr_t ModuleBase(const wchar_t*) { return base; }
bool BytesMatch(std::uintptr_t at, const void* bytes, std::size_t size) {
  constexpr unsigned char queue[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
  constexpr unsigned char update[]{0x4c,0x8b,0xdc,0x53,0x56,0x48,0x81,0xec,0x88,0,0,0,0x0f,0x29,0x7c,0x24,0x60};
  return signature_ok && ((at == base + 0x219230 && size == sizeof(queue) && !std::memcmp(bytes, queue, size)) ||
      (at == base + 0x213900 && size == sizeof(update) && !std::memcmp(bytes, update, size)));
}
bool GuardedReadBytes(std::uintptr_t at, void* output, std::size_t size) {
  if (at == base + 0x1c77e50 && size == sizeof(world)) { std::memcpy(output, &world, size); return true; }
  const auto player_address = Address(player);
  if (at == world + 0x68 && size == sizeof(player_address)) { std::memcpy(output, &player_address, size); return true; }
  if (at == Address(records) && size == records.size()) {
    ++part_reads;
    if (fail_records) return false;
    std::memcpy(output, records.data(), size);
    if (replace_on_copy) {
      Put(player, 0x850, Address(replacement));
      replace_on_copy = false;
    }
    return true;
  }
  for (const auto range : {std::pair{Address(player), player.size()},
      std::pair{Address(primary), primary.size()}, std::pair{Address(alternate), alternate.size()},
      std::pair{Address(replacement), replacement.size()}, std::pair{Address(flver), flver.size()},
      std::pair{Address(facegen), facegen.size()}})
    if (at >= range.first && at - range.first <= range.second && size <= range.second - (at - range.first)) {
      std::memcpy(output, reinterpret_cast<void*>(at), size); return true;
    }
  return false;
}
bool GuardedWriteBytes(std::uintptr_t at, const void* data, std::size_t size) {
  if ((at == Address(alternate) + 0x9c || at == Address(replacement) + 0x9c) && size == 4) {
    ++writes;
    std::memcpy(reinterpret_cast<void*>(at), data, size);
    return true;
  }
  return false;
}
}

MH_STATUS WINAPI MH_Initialize() { return MH_OK; }
MH_STATUS WINAPI MH_CreateHook(LPVOID target, LPVOID callback, LPVOID* original) {
  if (reinterpret_cast<std::uintptr_t>(target) == base + 0x219230) {
    queue_callback = reinterpret_cast<Queue>(callback); *original = reinterpret_cast<void*>(OriginalQueue);
  } else {
    update_callback = reinterpret_cast<Update>(callback); *original = reinterpret_cast<void*>(OriginalUpdate);
  }
  return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID target) {
  return fail_enable && reinterpret_cast<std::uintptr_t>(target) == base + 0x213900 ? MH_ERROR_MEMORY_PROTECT : MH_OK;
}
MH_STATUS WINAPI MH_DisableHook(LPVOID) { ++disables; return MH_OK; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { return MH_OK; }

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char** argv) {
  Put(player, 0, base + 0x13251f0);
  Put(player, 0x60, Address(primary));
  Put(player, 0x850, Address(alternate));
  Put(player, 0x7d8, Address(facegen));
  Put(facegen, 0, base + 0x13df6d0); // NS_FRPG::FgModel
  Put(facegen, 0x28, generated_face_resource);
  Put(primary, 0, base + 0x12eaf30);
  Put(primary, 0x18, Address(flver));
  Put(flver, 0, base + 0x12eb760);
  Put(flver, 0x10, short{69}); Put(flver, 0x12, short{80});
  InitializeAlternate(alternate); InitializeAlternate(replacement);
  for (unsigned i = 0; i < records.size(); ++i) records[i] = static_cast<unsigned char>(i * 7 + 3);
  for (unsigned slot = 0; slot < 13; ++slot) records[slot * 16 + 9] = static_cast<unsigned char>(0xf0 | (slot % 2 + 1));
  Put(records, 0, generated_face_resource);
  const auto untouched = records;
  config.vr_enabled = config.headlook_enabled = config.hide_head = true;
  // The fixture below checks head geometry only; the shipped default now also
  // hides torso and legs, which would mask records this test must leave alone.
  config.hide_torso_and_legs = false;
  config.arm_ik_enabled = config.xr_input_enabled = false;
  signature_ok = false; CHECK(!dsvr::BodyVisibilityHookInstall(true)); signature_ok = true;
  CHECK(!dsvr::BodyVisibilityHookInstall(false));
  if (argc > 1 && !std::strcmp(argv[1], "enable")) {
    fail_enable = true;
    CHECK(!dsvr::BodyVisibilityHookInstall(true)); CHECK(disables == 2);
    queue_callback(reinterpret_cast<void*>(0x2000), id, records.data(), 13);
    CHECK(copied == untouched && !dsvr::BodyVisibilityGetStatus().installed);
    std::puts("PASS head geometry partial hook rollback"); return 0;
  }
  CHECK(dsvr::BodyVisibilityHookInstall(true));
  Tick();
  CHECK(original_updates == 1 && original_queues == 1 && writes == 1);
  CHECK(last_delta == 0.0078125f && last_id == id && last_queue == reinterpret_cast<void*>(0x2000));
  CHECK(MaskedCorrectly(untouched) && records == untouched && queued_source != records.data());
  const auto visits = original_queues;
  Tick(); CHECK(original_queues == visits && writes == 1); // no redundant queue work

  // Slot zero is hidden only for the proven local generated-face resource.
  // Unknown model classes/resources retain their base geometry and equipment.
  Put(facegen, 0, std::uintptr_t{1});
  queue_callback(nullptr, id, records.data(), 13); CHECK(MaskedCorrectly(untouched, false));
  Put(facegen, 0, base + 0x13df6d0);
  Put(alternate, 0x1c0, generated_face_resource + 8);
  queue_callback(nullptr, id, records.data(), 13); CHECK(MaskedCorrectly(untouched, false));
  Put(alternate, 0x1c0, generated_face_resource);
  Put(records, 0, generated_face_resource + 8);
  queue_callback(nullptr, id, records.data(), 13); CHECK(MaskedCorrectly(records, false));
  records=untouched;
  queue_callback(nullptr, id, records.data(), 13); CHECK(MaskedCorrectly(untouched));

  // Policy transitions restore unfiltered geometry on the next native update.
  // No menu-canvas gate: the filter follows the rows even with the menu open,
  // so unticking restores geometry immediately instead of on menu close.
  theater = true; Tick(); CHECK(MaskedCorrectly(untouched));
  theater = false; Tick(); CHECK(MaskedCorrectly(untouched));
  running = false; Tick(); CHECK(copied == untouched); running = true; Tick(); CHECK(MaskedCorrectly(untouched));
  config.observer_only = true; Tick(); CHECK(copied == untouched); config.observer_only = false; Tick();
  config.vr_enabled = false; Tick(); CHECK(copied == untouched); config.vr_enabled = true; Tick();
  config.headlook_enabled = false; Tick(); CHECK(copied == untouched); config.headlook_enabled = true; Tick();
  config.hide_head = false; Tick(); CHECK(copied == untouched); config.hide_head = true; Tick();
  CHECK(MaskedCorrectly(untouched) && records == untouched);

  // Torso and legs are their own request: part slots 3 (body) and 5 (legs) lose
  // their type nibble, slot 4 (arms) and every other record keep theirs, and the
  // live records are never written - so unticking restores the geometry on the
  // next native update instead of needing a reload.
  config.hide_head = false; config.hide_torso_and_legs = true; Tick();
  CHECK(SlotsMasked(untouched, {3, 5}) && records == untouched);
  // A mod-menu tick asks for the fresh parts command directly rather than
  // waiting for the frame updater to notice the mask changed.
  const auto refresh_writes = writes;
  CHECK(dsvr::BodyVisibilityRequestRefresh() && writes == refresh_writes + 1);
  config.hide_torso_and_legs = false; Tick();
  CHECK(copied == untouched && records == untouched);
  // Both requests at once: the head set and the torso/leg set combine, and the
  // arms stay drawn. Unticking only the torso restores it while the head set
  // keeps filtering.
  config.hide_head = config.hide_torso_and_legs = true; Tick();
  CHECK(SlotsMasked(untouched, {0, 1, 2, 3, 5, 6}) && records == untouched);
  config.hide_torso_and_legs = false; Tick();
  CHECK(MaskedCorrectly(untouched) && records == untouched);
  // Re-ticking the torso while the head set stays hidden, then back to the
  // head-only state the rest of the run assumes.
  config.hide_torso_and_legs = true; Tick();
  CHECK(SlotsMasked(untouched, {0, 1, 2, 3, 5, 6}) && records == untouched);
  config.hide_torso_and_legs = false; Tick();
  CHECK(MaskedCorrectly(untouched) && records == untouched);

  // A nonlocal model cannot be dirtied by the visibility updater.
  const auto before_writes = writes;
  update_callback(primary.data(), 0.25f); CHECK(writes == before_writes && last_delta == 0.25f);
  queue_callback(reinterpret_cast<void*>(0x3000), id + 1, records.data(), 13);
  CHECK(copied == untouched && last_id == id + 1);
  Records foreign = untouched;
  queue_callback(reinterpret_cast<void*>(0x3000), id, foreign.data(), 13);
  CHECK(copied == untouched && queued_source == foreign.data());
  queue_callback(reinterpret_cast<void*>(0x3000), id, records.data(), 12);
  CHECK(queued_count == 12 && queued_source == records.data());

  // Corrupt identities, source read failure and a scene swap refuse filtering.
  for (auto field : {std::pair{Address(player), base + 0x13251f0},
      std::pair{Address(primary), base + 0x12eaf30}, std::pair{Address(alternate), base + 0x12eace8},
      std::pair{Address(flver), base + 0x12eb760}}) {
    const std::uintptr_t wrong = 1;
    std::memcpy(reinterpret_cast<void*>(field.first), &wrong, sizeof(wrong));
    queue_callback(nullptr, id, records.data(), 13); CHECK(copied == untouched);
    std::memcpy(reinterpret_cast<void*>(field.first), &field.second, sizeof(field.second));
  }
  Put(flver, 0x12, short{79}); queue_callback(nullptr, id, records.data(), 13); CHECK(copied == untouched); Put(flver, 0x12, short{80});
  Put(alternate, 0x18, Address(flver) + 8); queue_callback(nullptr, id, records.data(), 13); CHECK(copied == untouched); Put(alternate, 0x18, Address(flver));
  Put(alternate, 0x78, 12); queue_callback(nullptr, id, records.data(), 13); CHECK(copied == untouched); Put(alternate, 0x78, 13);
  fail_records = true; queue_callback(nullptr, id, records.data(), 13); CHECK(copied == untouched); fail_records = false;
  replace_on_copy = true; queue_callback(nullptr, id, records.data(), 13); CHECK(copied == untouched);
  Tick(); CHECK(MaskedCorrectly(untouched)); // new actor model rebinds without stale ownership
  CHECK(records == untouched && part_reads > 0);
  const auto status = dsvr::BodyVisibilityGetStatus();
  CHECK(status.installed && status.filtered >= 7 && status.refused >= 11 && status.refreshes >= 7);
  dsvr::BodyVisibilityHookUninstall();
  CHECK(!dsvr::BodyVisibilityGetStatus().installed && (Get<std::uint32_t>(replacement.data(), 0x9c) & dirty_bit));
  Tick(); CHECK(copied == untouched); // late callbacks retain valid original trampolines
  CHECK(records == untouched && disables == 2);
  std::puts("PASS actual native head part queue/update detours: geometry exclusion, policy restoration, identity guards and late callbacks");
}
