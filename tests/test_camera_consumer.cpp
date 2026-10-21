// Real byte-gated trace detour and writer; fake native dispatcher/memory.
// Tests natural-call preservation and observations, not actual GPU consumption.
#include <windows.h>
#include <MinHook.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "../src/common/logging.h"
#include "../src/common/memory.h"
#include "../src/core/vr_trace.h"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); \
  return 1; } } while (0)

namespace {
constexpr std::uintptr_t base = 0x140000000;
using Consume = unsigned(__fastcall*)(void*, unsigned, std::uintptr_t*);
using Camera = void(__fastcall*)(void*);
using Upload = void(__fastcall*)(void*, void*, const void*, int);
Consume detour = nullptr;
Camera projection_detour = nullptr;
Upload upload_detour = nullptr;
alignas(16) unsigned char upload_owner[0x40]{}, upload_context[0x40]{};
alignas(16) float upload_data[48]{};
unsigned uploads = 0;
int expected_vectors = 0;
alignas(16) unsigned char entity[0x100]{}, producer[0x60]{}, payload[0x80]{};
unsigned char expected_entity[sizeof(entity)]{};
std::uintptr_t cursor = 0;
unsigned originals = 0, camera_originals = 0, violations = 0, reads = 0;
bool mismatch = false, bad_signature = false, unreadable = false, replace_capture = false;
bool bad_upload_signature = false;
std::wstring replacement;
void __fastcall WaitOriginal(void*, unsigned) {}
void __fastcall RenderOriginal(void*, int) {}
void __fastcall CameraOriginal(void*) { ++camera_originals; }
unsigned __fastcall ConsumeOriginal(void* self, unsigned command, std::uintptr_t* at) {
  ++originals;
  if (self != entity || at != &cursor) ++violations;
  if (command == 0x3e) {
    const auto aligned = (*at + 15) & ~std::uintptr_t(15);
    std::memcpy(entity + 0x20, reinterpret_cast<void*>(aligned), 64);
    *at = aligned + 64;
  } else if (command == 0x3b) {
    *at = ((*at + 3) & ~std::uintptr_t(3)) + 16;
    float fixture[16]{};
    for (unsigned i = 0; i < 16; ++i) fixture[i] = 80.0f + i;
    std::memcpy(entity + 0xa0, fixture, sizeof(fixture));
  }
  std::memcpy(expected_entity, entity, sizeof(entity));
  if (replace_capture) {
    dsvr::VrTraceRequestStop();
    dsvr::VrTracePump();
    if (!dsvr::VrTraceBegin(replacement.c_str())) ++violations;
  }
  return command == 0x3e ? 1 : 0;  // test preservation, not native projection math
}
void __fastcall UploadOriginal(void* owner, void* context, const void* source, int vectors) {
  ++uploads;
  if (owner != upload_owner || context != upload_context || source != upload_data ||
      vectors != expected_vectors) ++violations;
  if (replace_capture) {
    dsvr::VrTraceRequestStop();
    dsvr::VrTracePump();
    if (!dsvr::VrTraceBegin(replacement.c_str())) ++violations;
  }
}
std::string Read(const std::wstring& path) {
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}

MH_STATUS WINAPI MH_Initialize() { return MH_OK; }
MH_STATUS WINAPI MH_CreateHook(LPVOID target, LPVOID callback, LPVOID* original) {
  const auto rva = reinterpret_cast<std::uintptr_t>(target) - base;
  if (rva == 0x7b870) {
    detour = reinterpret_cast<Consume>(callback);
    *original = reinterpret_cast<void*>(&ConsumeOriginal);
  } else if (rva == 0x2472d0) {
    projection_detour = reinterpret_cast<Camera>(callback);
    *original = reinterpret_cast<void*>(&CameraOriginal);
  } else if (rva == 0xcd0a10) {
    upload_detour = reinterpret_cast<Upload>(callback);
    *original = reinterpret_cast<void*>(&UploadOriginal);
  } else if (rva == 0x245690) *original = reinterpret_cast<void*>(&CameraOriginal);
  else if (rva == 0xce3450) *original = reinterpret_cast<void*>(&WaitOriginal);
  else if (rva == 0x11be710) *original = reinterpret_cast<void*>(&RenderOriginal);
  else { ++violations; return MH_ERROR_UNSUPPORTED_FUNCTION; }
  return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID) { return MH_OK; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { return MH_OK; }

namespace dsvr {
void LogWrite(LogLevel, const char*, int, const char*, ...) {}
std::wstring LogPathNextToModule(void*, const wchar_t*) { return L"unused"; }
std::uintptr_t ModuleBase(const wchar_t*) { return base; }
bool BytesMatch(std::uintptr_t address, const void*, std::size_t) {
  return (!bad_signature || address != base + 0x7b870) &&
         (!bad_upload_signature || address != base + 0xcd0a10);
}
bool GuardedReadBytes(std::uintptr_t address, void* out, std::size_t size) {
  ++reads;
  if (address == base + 0x1609070 + 0x28 && size == sizeof(std::uintptr_t)) {
    const std::uintptr_t slot = mismatch ? 0 : base + 0x7b870;
    std::memcpy(out, &slot, size); return true;
  }
  if (unreadable) return false;  // observation failure must not skip the native call
  const struct { const void* start; std::size_t size; } regions[]{
    {entity, sizeof(entity)}, {producer, sizeof(producer)},
    {payload, sizeof(payload)}, {&cursor, sizeof(cursor)},
    {upload_owner, sizeof(upload_owner)}, {upload_context, sizeof(upload_context)},
    {upload_data, sizeof(upload_data)}};
  for (const auto& region : regions) {
    const auto start = reinterpret_cast<std::uintptr_t>(region.start);
    if (address >= start && address - start <= region.size &&
        size <= region.size - (address - start)) {
      std::memcpy(out, reinterpret_cast<void*>(address), size); return true;
    }
  }
  ++violations; return false;
}
}

int main(int argc, char** argv) {
  using namespace dsvr;
  mismatch = argc > 1 && std::string(argv[1]) == "refused";
  bad_signature = argc > 1 && std::string(argv[1]) == "signature";
  bad_upload_signature = argc > 1 && std::string(argv[1]) == "upload-signature";
  CHECK(VrTraceInstall(true) == !(mismatch || bad_signature || bad_upload_signature));
  if (bad_upload_signature) { CHECK(!upload_detour && violations == 0); return 0; }
  if (mismatch || bad_signature) { CHECK(!detour && violations == 0); return 0; }
  CHECK(detour && projection_detour);
  // The pinned FrpgCameraEntity override inherits the same +0x28 dispatcher.
  // Installation validates the base slot, not a per-instance exact-type filter.
  const bool derived = argc > 1 && std::string(argv[1]) == "derived";
  const std::uintptr_t table = base + (derived ? 0x12f2908 : 0x1609070);
  const unsigned handle = 0x80001234;
  std::memcpy(entity, &table, sizeof(table));
  std::memcpy(entity + 0x10, &handle, sizeof(handle));
  std::memcpy(producer + 8, &handle, sizeof(handle));
  float matrix[16]{};
  for (unsigned i = 0; i < 16; ++i) matrix[i] = static_cast<float>(i);
  std::memcpy(payload + 16, matrix, sizeof(matrix));
  wchar_t temp[MAX_PATH]{};
  CHECK(GetTempPathW(MAX_PATH, temp));
  const std::wstring path = std::wstring(temp) + L"DSVR-consumer-" +
      std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
  replacement = path + L"-replacement.jsonl";
  if (argc > 1 && std::string(argv[1]) == "upload") {
    CHECK(upload_detour);
    for (unsigned i = 0; i < 48; ++i) upload_data[i] = static_cast<float>(i);
    const std::uintptr_t buffer = 0x123456789abcdef0, context = 0xabcdef0123456789;
    std::memcpy(upload_owner + 0x28, &buffer, sizeof(buffer));
    std::memcpy(upload_context + 0x20, &context, sizeof(context));
    CHECK(VrTraceBegin(path.c_str()));
    expected_vectors = 12;
    upload_detour(upload_owner, upload_context, upload_data, expected_vectors);
    const auto full_reads = reads;
    expected_vectors = 0;
    upload_detour(upload_owner, upload_context, upload_data, expected_vectors);
    CHECK(reads == full_reads + 2);  // identity only, no incomplete matrix reads
    expected_vectors = 3;
    upload_detour(upload_owner, upload_context, upload_data, expected_vectors);
    CHECK(reads == full_reads + 4);
    expected_vectors = 0x7fffffff;
    upload_detour(upload_owner, upload_context, upload_data, expected_vectors);
    CHECK(reads == full_reads + 9);  // metadata + exactly three bounded blocks
    unreadable = true;
    expected_vectors = 4;
    upload_detour(upload_owner, upload_context, upload_data, expected_vectors);
    unreadable = false;
    VrTraceRequestStop();
    VrTracePump();
    const auto text = Read(path);
    CHECK(text.find("\"events\":17") != std::string::npos);
    CHECK(text.find("\"event\":\"constant_upload_block2\"") != std::string::npos);
    CHECK(text.find("\"values\":[32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47]") != std::string::npos);
    CHECK(text.find("\"dropped\":0") != std::string::npos);
    CHECK(uploads == 5 && violations == 0);
    for (unsigned i = 0; i < 48; ++i) CHECK(upload_data[i] == static_cast<float>(i));
    CHECK(VrTraceBegin((path + L"-old.jsonl").c_str()));
    replace_capture = true;
    expected_vectors = 12;
    upload_detour(upload_owner, upload_context, upload_data, expected_vectors);
    VrTraceRequestStop();
    VrTracePump();
    CHECK(uploads == 6 && violations == 0);
    CHECK(Read(replacement).find("\"events\":0") != std::string::npos);
    CHECK(Read(path + L"-old.jsonl").find("\"events\":4") != std::string::npos);
    const auto inactive_reads = reads;
    replace_capture = false;
    upload_detour(upload_owner, upload_context, upload_data, expected_vectors);
    CHECK(uploads == 7 && reads == inactive_reads);
    CHECK(DeleteFileW(path.c_str()));
    CHECK(DeleteFileW((path + L"-old.jsonl").c_str()));
    CHECK(DeleteFileW(replacement.c_str()));
    std::puts("test_constant_upload: PASS (CPU observation, no GPU execution)");
    return 0;
  }
  CHECK(VrTraceBegin(path.c_str()));
  cursor = reinterpret_cast<std::uintptr_t>(payload + 1);
  CHECK(detour(entity, 0x3e, &cursor) == 1 && originals == 1);
  CHECK(cursor == reinterpret_cast<std::uintptr_t>(payload + 80));
  CHECK(std::memcmp(entity, expected_entity, sizeof(entity)) == 0);
  cursor = reinterpret_cast<std::uintptr_t>(payload + 17);
  CHECK(detour(entity, 0x3b, &cursor) == 0 && originals == 2);
  CHECK(cursor == reinterpret_cast<std::uintptr_t>(payload + 36));
  CHECK(std::memcmp(entity, expected_entity, sizeof(entity)) == 0);
  const auto old_reads = reads;
  CHECK(detour(entity, 0x3a, &cursor) == 0 && originals == 3 && reads == old_reads);
  projection_detour(producer);
  CHECK(camera_originals == 1);
  VrTraceRequestStop();
  VrTracePump();
  CHECK(VrTraceState() == TraceCaptureState::Complete);
  const auto text = Read(path);
  CHECK(text.find("\"events\":11") != std::string::npos);
  CHECK(text.find("\"event\":\"camera_payload_3e\"") < text.find("\"event\":\"camera_consume_3e\""));
  CHECK(text.find("\"flags\":15,\"native_rate\":0") != std::string::npos);
  CHECK(text.find("\"values\":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]") != std::string::npos);
  CHECK(text.find("\"values\":[80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95]") != std::string::npos);
  CHECK(text.find("\"event\":\"camera_projection_command\"") != std::string::npos);
  CHECK(text.find("\"dropped\":0") != std::string::npos);
  // Consume metadata includes four exact 16-bit limbs of the observed vtable.
  CHECK(text.find(derived ? ",10504,16687,1,0,1,0]" :
                            ",36976,16736,1,0,1,0]") != std::string::npos);
  const auto idle_reads = reads;
  CHECK(detour(entity, 0x3e, &cursor) == 1 && originals == 4 && reads == idle_reads);
  const auto failed_path = path + L"-unreadable.jsonl";
  CHECK(VrTraceBegin(failed_path.c_str()));
  unreadable = true;
  cursor = reinterpret_cast<std::uintptr_t>(payload + 1);
  CHECK(detour(entity, 0x3e, &cursor) == 1 && originals == 5);
  CHECK(std::memcmp(entity, expected_entity, sizeof(entity)) == 0);
  VrTraceRequestStop();
  VrTracePump();
  const auto failed_text = Read(failed_path);
  CHECK(VrTraceState() == TraceCaptureState::Complete);
  CHECK(failed_text.find("\"events\":5") != std::string::npos);
  CHECK(failed_text.find("\"flags\":1") == std::string::npos);
  unreadable = false;
  CHECK(VrTraceBegin((path + L"-old.jsonl").c_str()));
  cursor = reinterpret_cast<std::uintptr_t>(payload + 1);
  replace_capture = true;
  CHECK(detour(entity, 0x3e, &cursor) == 1 && originals == 6);
  VrTraceRequestStop();
  VrTracePump();
  CHECK(VrTraceState() == TraceCaptureState::Complete);
  CHECK(Read(replacement).find("\"events\":0") != std::string::npos);
  CHECK(Read(path + L"-old.jsonl").find("\"events\":1") != std::string::npos);
  CHECK(violations == 0);
  CHECK(DeleteFileW(path.c_str()));
  CHECK(DeleteFileW((path + L"-old.jsonl").c_str()));
  CHECK(DeleteFileW(replacement.c_str()));
  CHECK(DeleteFileW(failed_path.c_str()));
  std::puts("test_camera_consumer: PASS (fixture only, GPU/main view unverified)");
  return 0;
}
