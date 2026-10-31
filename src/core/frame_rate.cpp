#include "frame_rate.h"

#include "../common/logging.h"

namespace dsvr {

bool FrameRateHookInstall(bool) {
  DSVR_LOG("FrameRate: DSVR native-rate hook disabled; high-rate frame timing is delegated to the chained FPS Unlock dependency");
  return false;
}

void FrameRateHookUninstall() {}

}  // namespace dsvr
