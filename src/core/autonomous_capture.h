#pragma once
#include <string>
#include "../common/config.h"

namespace dsvr {
// Called only by the diagnostics writer. Stop requests precede VrTracePump;
// Pump advances capture/session policy after trace state has been updated.
void AutonomousCaptureRequestStop();
void TraceCapturePump(const Config& config, const std::wstring& config_path,
                      unsigned long long now_ms);

void TraceCaptureFinish(const Config& config, const std::wstring& config_path);
}
