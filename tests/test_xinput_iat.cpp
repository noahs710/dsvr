#include <windows.h>
#include <Xinput.h>
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "../src/common/memory.h"
#include "../src/common/logging.h"
#include "../src/core/input.h"
#include "../src/core/lab_runner.h"

namespace {
HMODULE image = nullptr;
const auto provider = reinterpret_cast<HMODULE>(1);
int creates = 0;
bool motion_available = false;
bool physical_connected = true;
dsvr::VirtualPadState motion_state{dsvr::kPadA};
bool capabilities_available = false;
bool capabilities_hook_created = false;
using CapsPoll = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
CapsPoll capabilities_callback = nullptr;
std::atomic<bool> pause_original{false};
std::mutex pause_mutex;
std::condition_variable pause_cv;
bool entered = false, resume = false;
DWORD WINAPI Physical(DWORD user, XINPUT_STATE* state) {
  if (pause_original.load()) {
    std::unique_lock<std::mutex> lock(pause_mutex);
    entered = true;
    pause_cv.notify_all();
    pause_cv.wait(lock, [] { return resume; });
  }
  if (user != 0 || !physical_connected) return ERROR_DEVICE_NOT_CONNECTED;
  *state = {};
  state->Gamepad.wButtons = XINPUT_GAMEPAD_X;
  state->Gamepad.sThumbLY = 123;
  return ERROR_SUCCESS;
}
DWORD WINAPI Foreign(DWORD, XINPUT_STATE*) { return ERROR_DEVICE_NOT_CONNECTED; }
DWORD WINAPI PhysicalCaps(DWORD, DWORD, XINPUT_CAPABILITIES*) {
  return ERROR_DEVICE_NOT_CONNECTED;
}
HMODULE WINAPI BridgeModuleW(LPCWSTR name) { return name ? provider : image; }
HMODULE WINAPI BridgeModuleA(LPCSTR name) {
  return _stricmp(name, "xinput1_3.dll") == 0 ? provider : nullptr;
}
HMODULE WINAPI BridgeLoad(LPCWSTR) { return provider; }
FARPROC WINAPI BridgeProc(HMODULE module, LPCSTR name) {
  if (module != provider) return nullptr;
  auto key = reinterpret_cast<std::uintptr_t>(name);
  if (key < 65536) return key == 2 ? reinterpret_cast<FARPROC>(&Physical) :
                                    reinterpret_cast<FARPROC>(&Foreign);
  if (std::strcmp(name, "XInputGetState") == 0)
    return reinterpret_cast<FARPROC>(&Physical);
  return capabilities_available && std::strcmp(name, "XInputGetCapabilities") == 0
      ? reinterpret_cast<FARPROC>(&PhysicalCaps) : nullptr;
}
constexpr DWORD kSlot = 0x704;  // Four-mod-eight, like the pinned executable.
int protect_calls = 0, protect_fail_at = 0, protect_fail_from = 0;
bool race_owner = false;
BOOL WINAPI BridgeProtect(LPVOID address, SIZE_T size, DWORD mode, PDWORD old) {
  ++protect_calls;
  if (protect_calls == protect_fail_at ||
      (protect_fail_from && protect_calls >= protect_fail_from)) return FALSE;
  const BOOL result = VirtualProtect(address, size, mode, old);
  if (result && race_owner && mode == PAGE_READWRITE) {
    void* foreign = reinterpret_cast<void*>(&Foreign);
    std::memcpy(address, &foreign, sizeof(foreign));
    race_owner = false;
  }
  return result;
}

bool pin_refuse = false;
BOOL WINAPI BridgePin(DWORD flags, LPCWSTR address, HMODULE* module) {
  return !pin_refuse && GetModuleHandleExW(flags, address, module);
}

void StoreSlot(void* value) {
  std::memcpy(reinterpret_cast<unsigned char*>(image) + kSlot, &value, sizeof(value));
}
void* LoadSlot() {
  void* value = nullptr;
  std::memcpy(&value, reinterpret_cast<unsigned char*>(image) + kSlot, sizeof(value));
  return value;
}
void Fixture(bool ordinal = true) {
  DWORD old = 0;
  VirtualProtect(image, 0x4000, PAGE_READWRITE, &old);
  auto* bytes = reinterpret_cast<unsigned char*>(image);
  std::memset(bytes, 0, 0x4000);
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes);
  dos->e_magic = IMAGE_DOS_SIGNATURE;
  dos->e_lfanew = 0x100;
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(bytes + 0x100);
  nt->Signature = IMAGE_NT_SIGNATURE;
  nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
  nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
  nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
  nt->OptionalHeader.SizeOfImage = 0x4000;
  nt->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
  nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = {0x400, 60};
  auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(bytes + 0x400);
  imports[0].Name = 0x500;
  imports[0].OriginalFirstThunk = 0x600;
  imports[0].FirstThunk = kSlot;
  std::strcpy(reinterpret_cast<char*>(bytes + 0x500), "XINPUT1_3.dll");
  auto* lookup = reinterpret_cast<IMAGE_THUNK_DATA64*>(bytes + 0x600);
  lookup[0].u1.Ordinal = ordinal ? IMAGE_ORDINAL_FLAG64 | 2 : 0x800;
  std::strcpy(reinterpret_cast<char*>(bytes + 0x802), "XInputGetState");
  StoreSlot(reinterpret_cast<void*>(&Physical));
  VirtualProtect(image, 0x1000, PAGE_READONLY, &old);
  creates = 0;
}
DWORD Protection() {
  MEMORY_BASIC_INFORMATION info{};
  VirtualQuery(image, &info, sizeof(info));
  return info.Protect;
}
}

#define GetModuleHandleW BridgeModuleW
#define GetModuleHandleA BridgeModuleA
#define LoadLibraryW BridgeLoad
#define GetProcAddress BridgeProc
#define VirtualProtect BridgeProtect
#define GetModuleHandleExW BridgePin
#include "../src/core/xinput_bridge.cpp"
#undef GetModuleHandleW
#undef GetModuleHandleA
#undef LoadLibraryW
#undef GetProcAddress
#undef VirtualProtect
#undef GetModuleHandleExW

MH_STATUS WINAPI MH_Initialize() { return MH_OK; }
MH_STATUS WINAPI MH_CreateHook(LPVOID target, LPVOID detour, LPVOID* original) {
  ++creates;
  if (target == reinterpret_cast<void*>(&PhysicalCaps)) {
    capabilities_hook_created = true;
    capabilities_callback = reinterpret_cast<CapsPoll>(detour);
    *original = reinterpret_cast<void*>(&PhysicalCaps);
    return MH_OK;
  }
  return MH_ERROR_MEMORY_ALLOC;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID target) {
  return capabilities_hook_created && target == reinterpret_cast<void*>(&PhysicalCaps)
      ? MH_OK : MH_ERROR_NOT_CREATED;
}
MH_STATUS WINAPI MH_DisableHook(LPVOID) { return MH_OK; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { return MH_OK; }
const char* WINAPI MH_StatusToString(MH_STATUS) { return "forced allocation failure"; }
namespace dsvr {
void LogWrite(LogLevel, const char*, int, const char*, ...) {}
bool InputLatestPad(VirtualPadState& state) {
  state = motion_available ? motion_state : VirtualPadState{};
  return motion_available;
}
bool LabPollPad(const VirtualPadState&, const VirtualPadState&, VirtualPadState&) {
  return false;
}
}
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

int QuestOnlyPad() {
  using namespace dsvr;
  Fixture();
  physical_connected = false;
  motion_available = true;
  CHECK(XInputBridgeInstall(true));
  auto detour = reinterpret_cast<DWORD(WINAPI*)(DWORD, XINPUT_STATE*)>(LoadSlot());
  VrPadInput quest{};
  quest.left.stick_y = 1;
  quest.right.primary = true;
  motion_state = MapVrPad(quest, true);
  XINPUT_STATE state;
  std::memset(&state, 0xff, sizeof(state));
  CHECK(detour(0, &state) == ERROR_SUCCESS);
  CHECK(state.Gamepad.wButtons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_DPAD_UP));
  CHECK(state.Gamepad.sThumbLX == 0 && state.Gamepad.sThumbLY == 0);
  CHECK(state.Gamepad.sThumbRX == 0 && state.Gamepad.sThumbRY == 0);
  CHECK(state.Gamepad.bLeftTrigger == 0 && state.Gamepad.bRightTrigger == 0);
  const auto pressed_packet = state.dwPacketNumber;
  CHECK(detour(0, &state) == ERROR_SUCCESS && state.dwPacketNumber == pressed_packet);
  motion_state = {};
  CHECK(detour(0, &state) == ERROR_SUCCESS && state.Gamepad.wButtons == 0);
  CHECK(state.dwPacketNumber != pressed_packet);
  motion_available = false;
  CHECK(detour(0, &state) == ERROR_DEVICE_NOT_CONNECTED);
  XInputBridgeUninstall();
  physical_connected = true;
  motion_available = true;
  motion_state = {kPadA};
  return 0;
}

int FailureCases() {
  using namespace dsvr;
  Fixture();
  protect_calls = 0;
  protect_fail_at = 1;
  CHECK(!XInputBridgeInstall(true));
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Physical));
  CHECK(Protection() == PAGE_READONLY);
  protect_fail_at = 0;
  Fixture();
  protect_calls = 0;
  protect_fail_at = 2;
  CHECK(!XInputBridgeInstall(true));
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Physical));
  CHECK(Protection() == PAGE_READONLY);
  protect_fail_at = 0;
  Fixture();
  protect_calls = 0;
  protect_fail_from = 2;
  CHECK(!XInputBridgeInstall(true));
  auto stale = reinterpret_cast<DWORD(WINAPI*)(DWORD, XINPUT_STATE*)>(LoadSlot());
  CHECK(stale != &Physical);
  XINPUT_STATE state{};
  CHECK(stale(0, &state) == ERROR_SUCCESS);
  CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_X);
  CHECK(!XInputBridgeInstall(true));
  protect_fail_from = 0;
  XInputBridgeUninstall();
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Physical));
  CHECK(Protection() == PAGE_READONLY);
  Fixture();
  protect_calls = 0;
  race_owner = true;
  CHECK(!XInputBridgeInstall(true));
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Foreign));
  CHECK(Protection() == PAGE_READONLY);
  return 0;
}

int AdditionalRefusals() {
  using namespace dsvr;
  Fixture();
  pin_refuse = true;
  CHECK(!XInputBridgeInstall(true));
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Physical));
  pin_refuse = false;
  CHECK(XInputBridgeInstall(true));
  XInputBridgeUninstall();
  Fixture();
  DWORD old = 0;
  CHECK(VirtualProtect(image, 0x1000, PAGE_READWRITE, &old));
  auto* bytes = reinterpret_cast<unsigned char*>(image);
  auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(bytes + 0x400);
  imports[0].FirstThunk = 0x73c;
  void* original = reinterpret_cast<void*>(&Physical);
  std::memcpy(bytes + 0x73c, &original, sizeof(original));
  CHECK(VirtualProtect(image, 0x1000, old, &old));
  CHECK(!XInputBridgeInstall(true));
  void* current = nullptr;
  std::memcpy(&current, bytes + 0x73c, sizeof(current));
  CHECK(current == original && Protection() == PAGE_READONLY);
  Fixture();
  CHECK(XInputBridgeInstall(true));
  XInputBridgeUninstall();
  return 0;
}

int main() {
  using namespace dsvr;
  image = reinterpret_cast<HMODULE>(VirtualAlloc(nullptr, 0x4000,
      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  CHECK(image);
  Fixture();
  CHECK(!XInputBridgeInstall(false));
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Physical));
  CHECK(XInputBridgeInstall(true));
  CHECK(creates == 3);
  auto detour = reinterpret_cast<DWORD(WINAPI*)(DWORD, XINPUT_STATE*)>(LoadSlot());
  CHECK(detour != &Physical);
  CHECK(Protection() == PAGE_READONLY);
  CHECK(XInputBridgeInstall(true) && creates == 3);
  XINPUT_STATE state{};
  CHECK(detour(0, nullptr) == ERROR_BAD_ARGUMENTS);
  CHECK(detour(1, &state) == ERROR_DEVICE_NOT_CONNECTED);

  CHECK(detour(0, &state) == ERROR_SUCCESS);
  CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_X);
  motion_available = true;
  CHECK(detour(0, &state) == ERROR_SUCCESS);
  CHECK(state.Gamepad.wButtons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X));
  CHECK(state.Gamepad.sThumbLY == 0);
  XInputBridgeUninstall();
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Physical));
  CHECK(Protection() == PAGE_READONLY);
  CHECK(detour(0, &state) == ERROR_SUCCESS);
  CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_X);

  Fixture(false);
  CHECK(XInputBridgeInstall(true));
  detour = reinterpret_cast<decltype(detour)>(LoadSlot());
  pause_original = true;
  std::thread poll([&] { detour(0, &state); });
  {
    std::unique_lock<std::mutex> lock(pause_mutex);
    pause_cv.wait(lock, [] { return entered; });
  }
  XInputBridgeUninstall();
  { std::lock_guard<std::mutex> lock(pause_mutex); resume = true; }
  pause_cv.notify_all();
  poll.join();
  pause_original = false;
  CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_X);
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Physical));

  Fixture();
  CHECK(XInputBridgeInstall(true));
  DWORD old = 0;
  VirtualProtect(image, 0x1000, PAGE_READWRITE, &old);
  StoreSlot(reinterpret_cast<void*>(&Foreign));
  VirtualProtect(image, 0x1000, old, &old);
  XInputBridgeUninstall();
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Foreign));
  CHECK(Protection() == PAGE_READONLY);
  CHECK(!XInputBridgeInstall(true));
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Foreign));

  Fixture();
  VirtualProtect(image, 0x1000, PAGE_READWRITE, &old);
  auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
      reinterpret_cast<unsigned char*>(image) + 0x400);
  imports[0].OriginalFirstThunk = 0;
  CHECK(!XInputBridgeInstall(true));
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Physical));
  Fixture();
  VirtualProtect(image, 0x1000, PAGE_READWRITE, &old);
  imports[0].FirstThunk = 0x3ffc;
  CHECK(!XInputBridgeInstall(true));
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Physical));
  Fixture();
  VirtualProtect(image, 0x1000, PAGE_READWRITE, &old);
  imports[1] = imports[0];
  CHECK(!XInputBridgeInstall(true));
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Physical));
  CHECK(FailureCases() == 0); CHECK(AdditionalRefusals() == 0);
  CHECK(QuestOnlyPad() == 0);
  // Exports can choose different bridge routes. Reinstall must restore the
  // state IAT patch and re-enable the retained capabilities trampoline.
  Fixture();
  capabilities_available = true;
  CHECK(XInputBridgeInstall(true));
  CHECK(capabilities_callback && capabilities_hook_created);
  XINPUT_CAPABILITIES caps{};
  CHECK(capabilities_callback(0, 0, &caps) == ERROR_SUCCESS);
  XInputBridgeUninstall();
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Physical));
  CHECK(capabilities_callback(0, 0, &caps) == ERROR_DEVICE_NOT_CONNECTED);
  CHECK(XInputBridgeInstall(true));
  detour = reinterpret_cast<decltype(detour)>(LoadSlot());
  CHECK(detour != &Physical && detour(0, &state) == ERROR_SUCCESS);
  CHECK(state.Gamepad.wButtons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X));
  CHECK(capabilities_callback(0, 0, &caps) == ERROR_SUCCESS);
  XInputBridgeUninstall();
  CHECK(LoadSlot() == reinterpret_cast<void*>(&Physical));
  VirtualFree(image, 0, MEM_RELEASE);
  std::puts("PASS XInput game-local IAT fallback (synthetic PE, not live game)");
  return 0;
}
