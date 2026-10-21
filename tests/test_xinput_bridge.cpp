// XInput bridge install seam with recording MinHook stubs. Run 9 failed once
// with "hook creation failed" while many other launches succeeded, so install
// must survive a transient create failure and a stale already-created entry.
#include <windows.h>
#include <Xinput.h>
#include <MinHook.h>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "../src/common/logging.h"
#include "../src/core/input.h"
#include "../src/core/lab_runner.h"
#include "../src/core/xinput_bridge.h"
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
namespace {
int creates=0, removes=0, enables=0;
MH_STATUS script[4]={};
int script_len=0, script_pos=0;
using Poll = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
Poll callback = nullptr;
int disables = 0;
MH_STATUS enable_status = MH_OK;
bool motion = false, pause_poll = false, entered = false, resume = false;
std::mutex poll_mutex;
std::condition_variable poll_cv;
DWORD WINAPI FakeOriginal(DWORD, XINPUT_STATE* state) {
  if (pause_poll) {
    std::unique_lock<std::mutex> lock(poll_mutex);
    entered = true;
    poll_cv.notify_all();
    poll_cv.wait(lock, [] { return resume; });
  }
  *state = {};
  state->Gamepad.wButtons = XINPUT_GAMEPAD_X;
  return ERROR_SUCCESS;
}
void Script(MH_STATUS a, MH_STATUS b) {
  script[0]=a; script[1]=b; script_len=2; script_pos=0;
  creates=removes=enables=0;
}
}
MH_STATUS WINAPI MH_Initialize() { return MH_OK; }
MH_STATUS WINAPI MH_CreateHook(LPVOID, LPVOID detour, LPVOID* original) {
  ++creates;
  if (script_pos < script_len) {
    const MH_STATUS status = script[script_pos++];
    if (status != MH_OK) return status;
  }
  callback = reinterpret_cast<Poll>(detour);
  *original = reinterpret_cast<void*>(&FakeOriginal);
  return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID) { ++enables; return enable_status; }
MH_STATUS WINAPI MH_DisableHook(LPVOID) { ++disables; return MH_OK; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { ++removes; return MH_OK; }
const char* WINAPI MH_StatusToString(MH_STATUS) { return "stub"; }
namespace dsvr {
void LogWrite(LogLevel, const char*, int, const char*, ...) {}
bool InputLatestPad(VirtualPadState& state) {
  state = {};
  if (motion) state.buttons = XINPUT_GAMEPAD_A;
  return motion;
}
bool LabPollPad(const VirtualPadState&, const VirtualPadState&, VirtualPadState&) { return false; }
}
int main(int argc, char** argv) {
  using namespace dsvr;
  if (argc > 1 && std::strcmp(argv[1], "foreign") == 0) {
    Script(MH_ERROR_ALREADY_CREATED, MH_OK);
    CHECK(!XInputBridgeInstall(true));
    CHECK(creates == 1 && removes == 0 && enables == 0);
    XInputBridgeUninstall();
    CHECK(removes == 0);
  } else if (argc > 1 && std::strcmp(argv[1], "allocation") == 0) {
    script[0] = script[1] = script[2] = MH_ERROR_MEMORY_ALLOC;
    script_len = 3; script_pos = 0;
    CHECK(!XInputBridgeInstall(true));
    CHECK(creates == 3 && removes == 0);
    CHECK(!XInputBridgeInstall(false));
  } else if (argc > 1 && std::strcmp(argv[1], "enable") == 0) {
    Script(MH_OK, MH_OK);
    enable_status = MH_ERROR_MEMORY_PROTECT;
    CHECK(!XInputBridgeInstall(true));
    CHECK(creates == 1 && removes == 0 && callback);
    motion = true;
    XINPUT_STATE state{};
    CHECK(callback(0, &state) == ERROR_SUCCESS);
    CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_X);
    enable_status = MH_OK;
    CHECK(XInputBridgeInstall(true));
    CHECK(creates == 1);
    CHECK(callback(0, &state) == ERROR_SUCCESS);
    CHECK(state.Gamepad.wButtons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X));
    XInputBridgeUninstall();
    CHECK(removes == 0);
  } else {
    Script(MH_ERROR_MEMORY_ALLOC, MH_OK);
    CHECK(XInputBridgeInstall(true));
    CHECK(creates == 2 && enables == 1 && callback);
    CHECK(XInputBridgeInstall(true) && creates == 2 && enables == 1);
    motion = true;
    XINPUT_STATE state{};
    CHECK(callback(0, &state) == ERROR_SUCCESS);
    CHECK(state.Gamepad.wButtons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X));
    pause_poll = true;
    std::thread poll([&] { callback(0, &state); });
    {
      std::unique_lock<std::mutex> lock(poll_mutex);
      poll_cv.wait(lock, [] { return entered; });
    }
    XInputBridgeUninstall();
    { std::lock_guard<std::mutex> lock(poll_mutex); resume = true; }
    poll_cv.notify_all();
    poll.join();
    pause_poll = false;
    CHECK(removes == 0 && disables == 1);
    CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_X);
    CHECK(callback(0, &state) == ERROR_SUCCESS);
    CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_X);
    CHECK(XInputBridgeInstall(true));
    CHECK(creates == 2 && enables == 2);
    CHECK(callback(0, &state) == ERROR_SUCCESS);
    CHECK(state.Gamepad.wButtons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X));
    XInputBridgeUninstall();
    CHECK(removes == 0);
  }
  std::puts("PASS XInput ownership/lifetime (stubbed MinHook, not live game)");
  return 0;
}
