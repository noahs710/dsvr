#include "autonomous_capture.h"
#include "diagnostics.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <string>

#include "../common/logging.h"
#include "combat_native.h"
#include "input.h"
#include "vr_trace.h"

namespace dsvr {
namespace {
struct AutonomousCapture {
  std::string id;
  std::wstring prefix;
  FILE* index = nullptr;
  std::uint64_t epoch = 0;
  std::uint64_t segment = 0;
  long long segment_start_qpc = 0;
  long long previous_segment_end_qpc = 0;
  unsigned long long segment_start_ms = 0;
  unsigned long long absent_since_ms = 0;
  const char* close_reason = nullptr;
  bool initialized = false;
  bool disabled = false;
  bool epoch_open = false;
  bool segment_open = false;
  bool epoch_end_pending = false;
  bool degraded = false;
  bool operator_stopped = false;
  const char* stop_reason = "operator_stop";
  std::wstring disk_directory;
  unsigned long long next_disk_check_ms = 0;
};

constexpr unsigned long long kSessionDiskReserveBytes = 512ull * 1024 * 1024;

unsigned SessionConfigFlags(const Config& config) {
  unsigned flags = 0;
  if (config.vr_enabled) flags |= kSessionConfigVr;
  if (config.xr_input_enabled) flags |= kSessionConfigXrInput;
  if (config.dominant_hand_left) flags |= kSessionConfigDominantLeft;
  if (config.motion_attacks) flags |= kSessionConfigMotionAttacks;
  if (config.physical_attacks) flags |= kSessionConfigPhysicalAttacks;
  if (config.physical_defence) flags |= kSessionConfigPhysicalDefence;
  if (config.arm_ik_enabled) flags |= kSessionConfigArmIk;
  return flags;
}

unsigned SessionCapabilities(const Config& config) {
  unsigned capabilities = VrTraceSessionCapabilities();
  if (config.xr_input_enabled && InputAvailable())
    capabilities |= kSessionCapVirtualPad | kSessionCapTrackedGrip;
  return capabilities;
}

bool SessionDiskAvailable(const std::wstring& directory,
                          unsigned long long& available) {
  ULARGE_INTEGER free_bytes{};
  if (!GetDiskFreeSpaceExW(directory.c_str(), &free_bytes, nullptr, nullptr))
    return false;
  available = free_bytes.QuadPart;
  return true;
}

ULONGLONG SessionUtcFiletime() {
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER value{};
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return value.QuadPart;
}

std::wstring WidenAscii(const std::string& value) {
  return std::wstring(value.begin(), value.end());
}

bool SessionIndexLine(AutonomousCapture& capture, const char* format, ...) {
  if (!capture.index || capture.disabled) return false;
  va_list args;
  va_start(args, format);
  const int written = std::vfprintf(capture.index, format, args);
  va_end(args);
  if (written < 0 || std::fputc('\n', capture.index) == EOF ||
      std::fflush(capture.index) != 0) {
    std::fclose(capture.index);
    capture.index = nullptr;
    capture.disabled = true;
    DSVR_WARN("Diagnostics: autonomous session index write failed; capture disabled");
    if (capture.segment_open) {
      capture.close_reason = "index_write_error";
      VrTraceRequestStop();
    }
    return false;
  }
  return true;
}

bool InitializeAutonomousCapture(AutonomousCapture& capture,
                                 const std::wstring& config_path) {
  // The external writer must be known before the first segment starts; the
  // sidecar lives next to the config so a stale path cannot silently persist.
  wchar_t module[MAX_PATH]{};
  if (GetModuleFileNameW(nullptr, module, MAX_PATH) > 0) {
    std::wstring dir = module;
    const auto slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.resize(slash + 1);
    const std::wstring candidate = dir + L"DSVR-TraceWriter.exe";
    DWORD attributes = GetFileAttributesW(candidate.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
      VrTraceSetExternalWriter(candidate.c_str());
  }
  if (capture.initialized) return capture.index != nullptr && !capture.disabled;
  capture.initialized = true;
  const auto qpc = static_cast<unsigned long long>(VrTraceClock());
  const auto tick = GetTickCount64();
  char id[64]{};
  const int length = std::snprintf(id, sizeof(id), "p%lu-t%llu-q%llx",
      GetCurrentProcessId(), tick, qpc);
  if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(id)) {
    capture.disabled = true;
    return false;
  }
  capture.id.assign(id, static_cast<std::size_t>(length));
  const auto separator = config_path.find_last_of(L"\\/");
  capture.disk_directory = separator == std::wstring::npos ? L"." :
      config_path.substr(0, separator + 1);
  unsigned long long available = 0;
  if (!SessionDiskAvailable(capture.disk_directory, available) ||
      available < kSessionDiskReserveBytes) {
    capture.disabled = true;
    DSVR_WARN("Diagnostics: autonomous capture refused; disk reserve is below 512 MiB or unavailable");
    return false;
  }
  capture.prefix = config_path + L".session-" + WidenAscii(capture.id);
  const std::wstring index_path = capture.prefix + L".jsonl";
  if (_wfopen_s(&capture.index, index_path.c_str(), L"wx") != 0 || !capture.index) {
    capture.index = nullptr;
    capture.disabled = true;
    DSVR_WARN("Diagnostics: autonomous session index could not be created; no capture started");
    return false;
  }
  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  const Config config = DiagnosticsCurrentConfig();
  if (!SessionIndexLine(capture,
      "{\"event\":\"session_start\",\"schema\":1,\"session_id\":\"%s\","
      "\"pid\":%lu,\"qpc_hz\":%lld,\"qpc\":%llu,\"utc_filetime_100ns\":%llu,"
      "\"capabilities\":%u,\"config_flags\":%u,\"disk_reserve_bytes\":%llu}",
      capture.id.c_str(), GetCurrentProcessId(), frequency.QuadPart, qpc,
      SessionUtcFiletime(), SessionCapabilities(config),
      SessionConfigFlags(config), kSessionDiskReserveBytes)) return false;
  DSVR_LOG("Diagnostics: autonomous telemetry index opened for session %s",
           capture.id.c_str());
  return true;
}

const char* TraceStateName(TraceCaptureState state) {
  switch (state) {
    case TraceCaptureState::Idle: return "idle";
    case TraceCaptureState::Recording: return "recording";
    case TraceCaptureState::Closing: return "closing";
    case TraceCaptureState::Complete: return "complete";
    case TraceCaptureState::Interrupted: return "interrupted";
    case TraceCaptureState::Dropped: return "dropped";
    case TraceCaptureState::IoError: return "io_error";
  }
  return "unknown";
}

std::string EventCountsJson(const VrTraceHealth& health) {
  std::string json = "{";
  bool first = true;
  for (const auto& [event, count] : health.event_counts) {
    if (!first) json += ",";
    first = false;
    json += "\"" + event + "\":" + std::to_string(count);
  }
  json += "}";
  return json;
}

bool BeginAutonomousSegment(AutonomousCapture& capture, const Config& config) {
  ++capture.segment;
  const std::wstring filename = L"-epoch-" + std::to_wstring(capture.epoch) +
      L"-segment-" + std::to_wstring(capture.segment) + L".jsonl";
  const std::wstring path = capture.prefix + filename;
  char leaf[96]{};
  const int length = std::snprintf(leaf, sizeof(leaf), "epoch-%llu-segment-%llu.jsonl",
      static_cast<unsigned long long>(capture.epoch),
      static_cast<unsigned long long>(capture.segment));
  if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(leaf)) {
    capture.degraded = true;
    return false;
  }
  const auto start_qpc = VrTraceClock();
  const auto start_utc = SessionUtcFiletime();
  const auto gap_qpc = capture.previous_segment_end_qpc
      ? start_qpc - capture.previous_segment_end_qpc : 0;
  if (!SessionIndexLine(capture,
      "{\"event\":\"segment_start\",\"session_id\":\"%s\","
      "\"epoch\":%llu,\"segment\":%llu,\"qpc\":%lld,\"utc_filetime_100ns\":%llu,\"gap_qpc\":%lld,"
      "\"segment_file\":\"%s\",\"capabilities\":%u,\"config_flags\":%u,"
      "\"state\":\"starting\"}",
      capture.id.c_str(), static_cast<unsigned long long>(capture.epoch),
      static_cast<unsigned long long>(capture.segment), start_qpc, start_utc, gap_qpc, leaf,
      SessionCapabilities(config), SessionConfigFlags(config))) {
    capture.degraded = true;
    return false;
  }
  if (!VrTraceBeginSession(path.c_str(), 600000, capture.id.c_str(), // 10-minute segments
                           capture.epoch, capture.segment,
                           SessionCapabilities(config), SessionConfigFlags(config))) {
    const auto health = VrTraceGetHealth();
    SessionIndexLine(capture,
        "{\"event\":\"segment_failure\",\"session_id\":\"%s\","
        "\"epoch\":%llu,\"segment\":%llu,\"qpc\":%lld,\"state\":\"%s\"}",
        capture.id.c_str(), static_cast<unsigned long long>(capture.epoch),
        static_cast<unsigned long long>(capture.segment), VrTraceClock(),
        TraceStateName(health.state));
    capture.degraded = true;
    DSVR_WARN("Diagnostics: autonomous segment start failed; player epoch degraded");
    return false;
  }
  capture.segment_open = true;
  capture.segment_start_qpc = start_qpc;
  capture.segment_start_ms = GetTickCount64();
  capture.next_disk_check_ms = capture.segment_start_ms + 1000;
  capture.close_reason = nullptr;
  return true;
}

void CloseAutonomousSegment(AutonomousCapture& capture,
                            TraceCaptureState state,
                            bool checkpoint_first = false) {
  if (!capture.segment_open) return;
  // A checkpoint before close is best-effort: the writer ack proves the pipe
  // drained, never that the game produced complete coverage.
  if (checkpoint_first && (state == TraceCaptureState::Recording ||
      state == TraceCaptureState::Closing)) VrTraceCheckpoint();
  const auto health = VrTraceGetHealth();
  const auto end_qpc = VrTraceClock();
  const auto end_utc = SessionUtcFiletime();
  const char* reason = capture.close_reason;
  const auto event_counts = EventCountsJson(health);
  if (!reason) {
    if (state == TraceCaptureState::Complete &&
        GetTickCount64() - capture.segment_start_ms >= 600000)
      reason = "rotation";
    else if (state == TraceCaptureState::Dropped) reason = "dropped_events";
    else if (state == TraceCaptureState::IoError) reason = "writer_io_error";
    else if (state == TraceCaptureState::Interrupted) reason = "interrupted";
    else reason = "trace_closed";
  }
  SessionIndexLine(capture,
      "{\"event\":\"segment_end\",\"session_id\":\"%s\","
      "\"epoch\":%llu,\"segment\":%llu,\"qpc\":%lld,\"utc_filetime_100ns\":%llu,"
      "\"duration_qpc\":%lld,\"reason\":\"%s\",\"state\":\"%s\","
      "\"capture_id\":%llu,\"events\":%llu,\"dropped\":%lu,"
      "\"contended\":%lu,\"queue_peak\":%lu,\"io_error\":%s,\"event_counts\":%s}",
      capture.id.c_str(), static_cast<unsigned long long>(capture.epoch),
      static_cast<unsigned long long>(capture.segment), end_qpc, end_utc,
      end_qpc - capture.segment_start_qpc, reason, TraceStateName(state),
      static_cast<unsigned long long>(health.capture_id),
      static_cast<unsigned long long>(health.events), health.dropped,
      health.contended, health.queue_peak, health.io_error ? "true" : "false",
      event_counts.c_str());
  capture.segment_open = false;
  capture.previous_segment_end_qpc = end_qpc;
  capture.close_reason = nullptr;
  if (state != TraceCaptureState::Complete) capture.degraded = true;
}

void CloseAutonomousEpoch(AutonomousCapture& capture, const char* reason) {
  if (!capture.epoch_open) return;
  const auto end_qpc = VrTraceClock();
  const auto end_utc = SessionUtcFiletime();
  SessionIndexLine(capture,
      "{\"event\":\"epoch_end\",\"session_id\":\"%s\","
      "\"epoch\":%llu,\"qpc\":%lld,\"utc_filetime_100ns\":%llu,\"reason\":\"%s\","
      "\"degraded\":%s}", capture.id.c_str(),
      static_cast<unsigned long long>(capture.epoch), end_qpc, end_utc, reason,
      capture.degraded ? "true" : "false");
  capture.epoch_open = false;
  capture.previous_segment_end_qpc = 0;
  capture.epoch_end_pending = false;
  capture.absent_since_ms = 0;
  capture.degraded = false;
}

void PumpAutonomousCapture(AutonomousCapture& capture, const Config& config,
                           const std::wstring& config_path,
                           unsigned long long now, bool shutdown = false) {
  if (!capture.initialized && !config.autonomous_session_capture) return;
  if (shutdown) {
    if (capture.segment_open) {
        // One close per segment: checkpoint first, then close once with the
        // post-checkpoint state. A second call is a no-op by the segment_open
      // guard, but a single call keeps the index to one segment_end line.
        CloseAutonomousSegment(capture, VrTraceState(), true);
      }
    CloseAutonomousEpoch(capture, "process_shutdown_interrupted");
    if (capture.index) {
      SessionIndexLine(capture,
          "{\"event\":\"session_end\",\"session_id\":\"%s\","
          "\"qpc\":%lld,\"utc_filetime_100ns\":%llu,\"reason\":\"process_shutdown\"}",
          capture.id.c_str(), VrTraceClock(), SessionUtcFiletime());
      std::fclose(capture.index);
      capture.index = nullptr;
    }
    return;
  }
  if (capture.disabled || !config.autonomous_session_capture ||
      !VrTraceSessionHooksReady()) return;

  std::uintptr_t player = 0;
  const bool present = CombatNativeLocalPlayer(player) && player != 0;
  if (capture.epoch_open && capture.segment_open &&
      now >= capture.next_disk_check_ms) {
    capture.next_disk_check_ms = now + 1000;
    unsigned long long available = 0;
    if (!SessionDiskAvailable(capture.disk_directory, available) ||
        available < kSessionDiskReserveBytes) {
      capture.operator_stopped = true;
      capture.stop_reason = "low_disk_reserve";
      capture.close_reason = "low_disk_reserve";
      SessionIndexLine(capture,
          "{\"event\":\"capture_stopped\",\"session_id\":\"%s\",\"epoch\":%llu,\"qpc\":%lld,"
          "\"utc_filetime_100ns\":%llu,\"reason\":\"low_disk_reserve\",\"available_bytes\":%llu,"
          "\"reserve_bytes\":%llu}",
          capture.id.c_str(), static_cast<unsigned long long>(capture.epoch),
          VrTraceClock(), SessionUtcFiletime(), available, kSessionDiskReserveBytes);
      if (VrTraceState() == TraceCaptureState::Recording) VrTraceRequestStop();
    }
  }
  if (present) capture.absent_since_ms = 0;
  else if (capture.epoch_open && !capture.absent_since_ms)
    capture.absent_since_ms = now;

  if (capture.epoch_open && !present && capture.absent_since_ms &&
      now >= capture.absent_since_ms && now - capture.absent_since_ms >= 2000) {
    capture.epoch_end_pending = true;
    if (capture.segment_open && VrTraceState() == TraceCaptureState::Recording) {
      capture.close_reason = "player_unload";
      VrTraceCheckpoint();
      VrTraceRequestStop();
    }
  }

  const auto state = VrTraceState();
  if (capture.segment_open && state != TraceCaptureState::Recording &&
      state != TraceCaptureState::Closing)
    CloseAutonomousSegment(capture, state);

  if (capture.operator_stopped) {
    if (capture.segment_open && VrTraceState() == TraceCaptureState::Recording) {
      capture.close_reason = capture.stop_reason;
      VrTraceCheckpoint();
        VrTraceRequestStop();
    }
    if (capture.epoch_open && !capture.segment_open)
      CloseAutonomousEpoch(capture, capture.stop_reason);
    return;
  }
  if (capture.epoch_end_pending && !capture.segment_open) {
    CloseAutonomousEpoch(capture, capture.degraded ? "player_unload_degraded" :
                                                      "player_unload");
  }
  if (!capture.epoch_open && present) {
    if (!InitializeAutonomousCapture(capture, config_path)) return;
    ++capture.epoch;
    capture.epoch_open = true;
    capture.degraded = false;
    SessionIndexLine(capture,
        "{\"event\":\"epoch_start\",\"session_id\":\"%s\","
        "\"epoch\":%llu,\"qpc\":%lld,\"utc_filetime_100ns\":%llu}",
        capture.id.c_str(), static_cast<unsigned long long>(capture.epoch),
        VrTraceClock(), SessionUtcFiletime());
  }
  if (capture.epoch_open && !capture.disabled && !capture.degraded && !capture.segment_open &&
      !capture.epoch_end_pending)
    BeginAutonomousSegment(capture, config);
}
AutonomousCapture g_autonomous_capture;
}

void AutonomousCaptureRequestStop() {
  g_autonomous_capture.operator_stopped = true;
  g_autonomous_capture.stop_reason = "operator_stop";
  g_autonomous_capture.close_reason = "operator_stop";
  if (g_autonomous_capture.epoch_open) {
    SessionIndexLine(g_autonomous_capture,
        "{\"event\":\"capture_stopped\",\"session_id\":\"%s\","
        "\"epoch\":%llu,\"qpc\":%lld,\"utc_filetime_100ns\":%llu,"
        "\"reason\":\"operator_stop\"}",
        g_autonomous_capture.id.c_str(),
        static_cast<unsigned long long>(g_autonomous_capture.epoch),
        VrTraceClock(), SessionUtcFiletime());
  }
}

void TraceCapturePump(const Config& config, const std::wstring& config_path,
                      unsigned long long now_ms) {
  VrTracePump();
  PumpAutonomousCapture(g_autonomous_capture, config, config_path, now_ms);
}

void TraceCaptureFinish(const Config& config, const std::wstring& config_path) {
  VrTraceFinish(true);
  const auto until = GetTickCount64() + 1000;
  while (VrTraceState() == TraceCaptureState::Closing && GetTickCount64() < until) {
    VrTracePump();
    Sleep(1);
  }
  PumpAutonomousCapture(g_autonomous_capture, config, config_path,
                        GetTickCount64(), true);
}
}
