#include "../src/common/blur_state.h"

#include <cstdio>
#include <string>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  using namespace dsvr;
  const std::string ini =
      "[DisplaySetting]\r\nWindowMode = 1\r\n[DisplaySettingFilter]\r\n"
      "Blur = 1\r\nAntialiasing = 3\r\nDOF = 0\r\n";
  // The live preference is found in its own section only.
  const auto found = FindBlurPreference(ini);
  CHECK(found.found && found.value == "1");
  CHECK(!FindBlurPreference("[Other]\r\nBlur = 1\r\n").found);
  CHECK(!FindBlurPreference("[DisplaySettingFilter]\r\nDOF = 0\r\n").found);
  // Forcing off rewrites only the Blur value; every other byte survives.
  std::string forced;
  CHECK(ForceBlurOff(ini, forced) == BlurForceResult::Forced);
  CHECK(forced ==
        "[DisplaySetting]\r\nWindowMode = 1\r\n[DisplaySettingFilter]\r\n"
        "Blur = 0\r\nAntialiasing = 3\r\nDOF = 0\r\n");
  // Forcing an already-off file changes nothing and says so.
  std::string again;
  CHECK(ForceBlurOff(forced, again) == BlurForceResult::AlreadyOff);
  CHECK(again == forced);
  // A missing preference is a refusal, never an invented key.
  std::string untouched;
  CHECK(ForceBlurOff("[DisplaySettingFilter]\r\nDOF = 0\r\n", untouched) ==
        BlurForceResult::NotFound);
  // Install decisions: the forced value is never recorded as the original.
  CHECK(DecideBlurInstall(true, "1", false) == BlurInstallAction::RecordAndForce);
  CHECK(DecideBlurInstall(true, "1", true) == BlurInstallAction::ForceOnly);
  CHECK(DecideBlurInstall(true, "0", false) == BlurInstallAction::AlreadyOff);
  CHECK(DecideBlurInstall(true, "0", true) == BlurInstallAction::ForceOnly);
  CHECK(DecideBlurInstall(false, "", false) == BlurInstallAction::Refuse);
  // Restore writes back the recorded original and nothing else.
  std::string restored;
  CHECK(RestoreBlurPreference(forced, "1", restored) == BlurRestoreResult::Restored);
  CHECK(restored == ini);
  std::string same;
  CHECK(RestoreBlurPreference(ini, "1", same) == BlurRestoreResult::AlreadyMatches);
  CHECK(same == ini);
  std::string missing;
  CHECK(RestoreBlurPreference("[DisplaySettingFilter]\r\nDOF = 0\r\n", "1", missing) ==
        BlurRestoreResult::NotFound);
  std::puts("PASS blur enforcement state machine");
  return 0;
}
