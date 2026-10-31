#include <cstdio>
#include <string>

#include "../src/common/logging.h"
#include "../src/core/frame_rate.h"
#include "../src/loader/fps_unlock_settings.h"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace dsvr {
void LogWrite(LogLevel, const char*, int, const char*, ...) {}
}

int main() {
  CHECK(!dsvr::FrameRateHookInstall(true));
  dsvr::FrameRateHookUninstall();

  using dsvr::fps_unlock::ChooseMaxFps;
  using dsvr::fps_unlock::SetFpsUnlockSettings;
  CHECK(ChooseMaxFps(true, false, 120, 165) == 120);
  CHECK(ChooseMaxFps(true, false, 72, 165) == 72);
  CHECK(ChooseMaxFps(true, false, 0, 165) == 0);
  CHECK(ChooseMaxFps(false, false, 120, 165) == 165);
  CHECK(ChooseMaxFps(false, true, 0, 165) == 165);
  CHECK(ChooseMaxFps(false, false, 0, 0) == 0);

  std::string ini = "# keep comment\r\n[Framerate]\r\nFPSUnlock = false # enable\r\nMaxFPS = 120 # cap\r\nVariableFrameTime = false\r\n\r\n[Physics]\r\nFixSlide = false\r\n";
  CHECK(SetFpsUnlockSettings(ini, 144));
  CHECK(ini.find("FPSUnlock = true # enable") != std::string::npos);
  CHECK(ini.find("MaxFPS = 144 # cap") != std::string::npos);
  CHECK(ini.find("VariableFrameTime = true") != std::string::npos);
  CHECK(ini.find("[Physics]\r\nFixSlide = false") != std::string::npos);
  const auto once = ini;
  CHECK(SetFpsUnlockSettings(ini, 144) && ini == once);
  CHECK(!SetFpsUnlockSettings(ini, 1001) && ini == once);

  std::string sparse = "[Framerate]\n# preserve\n\n[Physics]\nFixSlide=false\n";
  CHECK(SetFpsUnlockSettings(sparse, 120));
  CHECK(sparse.find("FPSUnlock = true") != std::string::npos);
  CHECK(sparse.find("MaxFPS = 120") != std::string::npos);
  CHECK(sparse.find("VariableFrameTime = true") != std::string::npos);
  CHECK(sparse.find("[Physics]\nFixSlide=false") != std::string::npos);

  std::string unknown = "[Framerate]\nFPSUnlock = true\nMaxFPS = 120\nVariableFrameTime = true\n[Physics]\nFixSlide=false\n";
  CHECK(SetFpsUnlockSettings(unknown, 0));
  CHECK(unknown.find("FPSUnlock = false") != std::string::npos);
  CHECK(unknown.find("MaxFPS = 120") != std::string::npos);
  CHECK(unknown.find("VariableFrameTime = true") != std::string::npos);
  CHECK(unknown.find("FixSlide=false") != std::string::npos);
  const auto disabled_once = unknown;
  CHECK(SetFpsUnlockSettings(unknown, 0) && unknown == disabled_once);
  std::string sparse_unknown = "[Physics]\nFixSlide=false\n";
  CHECK(SetFpsUnlockSettings(sparse_unknown, 0));
  CHECK(sparse_unknown.find("FPSUnlock = false") != std::string::npos);
  CHECK(sparse_unknown.find("MaxFPS = ") == std::string::npos);

  std::puts("PASS headset/monitor-owned FPS Unlock handoff; unknown refresh refuses preset cap");
  return 0;
}
