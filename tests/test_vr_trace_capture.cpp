// Exercises the production writer, never installs hooks or launches the game.
#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include "../src/core/vr_trace.h"

#define CHECK(condition) do { if (!(condition)) { \
  std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; \
} } while (0)

std::string Read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int wmain(int argc, wchar_t* argv[]) {
  if (argc == 3 && std::wcscmp(argv[1], L"--step-fixture") == 0) {
    CHECK(dsvr::VrTraceBegin(argv[2]));
    // Exercise the production encoding; these are synthetic, not native tasks.
    dsvr::VrTraceRecordStep({3, 42, 0, 1, 3, .125f, 0});
    dsvr::VrTraceRecordStep({2, 42, INT32_MAX, INT32_MIN, 3, .25f, 0});
    dsvr::VrTraceRecordStep({1, 42, -1, 0, 1, .5f, 0});
    dsvr::VrTraceRequestStop();
    dsvr::VrTracePump();
    CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::Complete);
    return 0;
  }
  if (argc == 3 && std::wcscmp(argv[1], L"--readiness-fixture") == 0) {
    CHECK(dsvr::VrTraceBegin(argv[2]));
    // Exact 64-bit boundaries, not floats pretending to preserve a full mask.
    dsvr::VrTraceRecordInputBlockers(0xabcd800000000001ull, 42);
    dsvr::VrTraceRecordInputBlockers(0, 42);
    dsvr::VrTraceRecordInputBlockers(0xffffffffffffffffull, 0);
    dsvr::VrTraceRequestStop();
    dsvr::VrTracePump();
    CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::Complete);
    return 0;
  }
  if (argc == 3 && std::wcscmp(argv[1], L"--fixture") == 0) {
    CHECK(dsvr::VrTraceBegin(argv[2]));
    dsvr::VrTraceEvent event{};
    event.event = "test_observation";
    dsvr::VrTraceRecord(event);
    dsvr::VrTraceRequestStop();
    dsvr::VrTracePump();
    CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::Complete);
    return 0;
  }
  wchar_t local[MAX_PATH]{};
  CHECK(GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH) > 0);
  const auto directory = std::filesystem::path(local) / L"Temp" / L"opencode";
  CHECK(std::filesystem::is_directory(directory));
  const auto name = L"dsvr-trace-test-" + std::to_wstring(GetCurrentProcessId()) +
                    L"-" + std::to_wstring(GetTickCount64());
  const auto path = directory / (name + L".jsonl");
  CHECK(!std::filesystem::exists(path));
  CHECK(dsvr::VrTraceBegin(path.c_str()));
  const auto capture_id = dsvr::VrTraceCaptureId();
  CHECK(capture_id != 0);
  CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::Recording);
  CHECK(!dsvr::VrTraceBegin(path.c_str()));  // cannot truncate an active capture
  dsvr::VrTraceEvent event{};
  event.event = "test_observation";
  dsvr::VrTraceRecord(event);
  dsvr::VrTracePump();
  CHECK(std::filesystem::file_size(path) > 0);
  CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::Recording);
  const auto stop_request=directory/(name+L"-stop.request");
  CHECK(!dsvr::VrTraceConsumeStopRequest(stop_request.c_str()));
  {
    std::ofstream request(stop_request,std::ios::binary);
    CHECK(request.good());
    request << "stop-v1";
  }
  bool consumed=false;
  std::thread stopper([&] {
    consumed=dsvr::VrTraceConsumeStopRequest(stop_request.c_str());
  });
  stopper.join();
  CHECK(consumed);
  CHECK(!std::filesystem::exists(stop_request));
  CHECK(!dsvr::VrTraceConsumeStopRequest(stop_request.c_str()));
  CHECK(!dsvr::VrTraceActive());
  CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::Closing);
  dsvr::VrTraceRecord(event);  // must not be accepted after stop
  dsvr::VrTracePump();
  CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::Complete);
  const auto completed = Read(path);
  CHECK(completed.find("test_observation") != std::string::npos);
  CHECK(completed.find("\"schema\":2") != std::string::npos);
  CHECK(completed.find("\"events\":1") != std::string::npos);
  CHECK(completed.find("\"interrupted\":false") != std::string::npos);
  CHECK(completed.find("\"io_error\":false") != std::string::npos);
  CHECK(!dsvr::VrTraceBegin(path.c_str()));  // exclusive create, no overwrite
  CHECK(dsvr::VrTraceCaptureId() == capture_id);  // failed begins cannot own a run
  CHECK(Read(path) == completed);
  CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::IoError);

  const auto interrupted = directory / (name + L"-interrupted.jsonl");
  CHECK(dsvr::VrTraceBegin(interrupted.c_str()));
  CHECK(dsvr::VrTraceCaptureId() > capture_id);
  // A native callback begun in the previous capture cannot join its replacement.
  dsvr::VrTraceRecordStep({3, 42, 0, 1, 3, .125f, 0, capture_id});
  dsvr::VrTraceRecord(event);
  dsvr::VrTraceFinish(true);
  CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::Interrupted);
  CHECK(Read(interrupted).find("\"interrupted\":true") != std::string::npos);
  CHECK(Read(interrupted).find("native_step_dispatch") == std::string::npos);

  const auto dropped = directory / (name + L"-dropped.jsonl");
  CHECK(dsvr::VrTraceBegin(dropped.c_str()));
  for (unsigned i = 0; i < 4097; ++i) dsvr::VrTraceRecord(event);
  dsvr::VrTraceRequestStop();
  dsvr::VrTracePump();
  CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::Dropped);
  CHECK(Read(dropped).find("\"dropped\":1") != std::string::npos);

  const auto deadline = directory / (name + L"-deadline.jsonl");
  CHECK(dsvr::VrTraceBegin(deadline.c_str(), 1));
  const auto deadline_started = GetTickCount64();
  // Sleep(5) can return within one GetTickCount64 tick. Observe automatic
  // deadline closure with a bounded wait, without requesting stop ourselves.
  do {
    dsvr::VrTracePump();
    if (dsvr::VrTraceState() != dsvr::TraceCaptureState::Recording &&
        dsvr::VrTraceState() != dsvr::TraceCaptureState::Closing) break;
    Sleep(1);
  } while (GetTickCount64() - deadline_started < 1000);
  CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::Complete);
  CHECK(Read(deadline).find("\"events\":0") != std::string::npos);

  const auto invalid = directory / (name + L"-absent") / L"capture.jsonl";
  CHECK(!dsvr::VrTraceBegin(invalid.c_str()));
  CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::IoError);
  CHECK(!dsvr::VrTraceActive());
  CHECK(!dsvr::VrTraceBegin(invalid.c_str(), 0));
  // Remove only this test's individually named files, never a directory tree.
  CHECK(std::filesystem::remove(path));
  CHECK(std::filesystem::remove(interrupted));
  CHECK(std::filesystem::remove(dropped));
  CHECK(std::filesystem::remove(deadline));
  std::puts("test_vr_trace_capture: PASS");
  return 0;
}
