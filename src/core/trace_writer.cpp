// Separate process: EOF drains the pipe even when the producer is terminated.
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <map>
#include <string>

static bool WriteAll(HANDLE file, const char* data, std::size_t size) {
  while (size) {
    DWORD written = 0;
    if (!WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) || !written)
      return false;
    data += written;
    size -= written;
  }
  return true;
}

int wmain(int argc, wchar_t** argv) {
  if (argc == 3 && !std::wcscmp(argv[1], L"--self-test")) {
    HANDLE output = CreateFileW(argv[2], GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) return 3;
    const char* lines[] = {"{\"event\":\"header\",\"qpc\":1}\n",
        "{\"event\":\"future_extension\",\"qpc\":2}\n",
        "{\"event\":\"footer\",\"qpc\":3}\n"};
    DWORD written = 0;
    for (const char* line : lines) {
      if (!WriteFile(output, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr) ||
          written != std::strlen(line)) { CloseHandle(output); return 4; }
    }
    CloseHandle(output);
    const std::wstring report_path = std::wstring(argv[2]) + L".transport.json";
    HANDLE report = CreateFileW(report_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (report == INVALID_HANDLE_VALUE) return 3;
    const std::string report_text = "{\"schema\":1,\"writer\":\"external-pipe-v1\",\"producer_footer_seen\":true,\"io_error\":false,\"complete_lines\":3,\"trailing_bytes\":0,\"bytes\":" +
        std::to_string(std::strlen(lines[0]) + std::strlen(lines[1]) + std::strlen(lines[2])) +
        ",\"unsubmitted_events\":null,\"event_counts\":{\"footer\":1,\"future_extension\":1,\"header\":1}}\n";
    if (!WriteFile(report, report_text.data(), static_cast<DWORD>(report_text.size()), &written, nullptr) ||
        written != report_text.size()) { CloseHandle(report); return 4; }
    CloseHandle(report);
    return 0;
  }
  if (argc != 4) return 2;
  const auto parse = [](const wchar_t* text) -> HANDLE {
    wchar_t* end = nullptr;
    const auto value = std::wcstoull(text, &end, 10);
    return end && end != text && !*end ? reinterpret_cast<HANDLE>(value) : nullptr;
  };
  HANDLE input = parse(argv[1]), ack = parse(argv[2]);
  if (!input || !ack) return 2;
  HANDLE output = CreateFileW(argv[3], GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE) return 3;
  const std::wstring report_path = std::wstring(argv[3]) + L".transport.json";
  HANDLE report = CreateFileW(report_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (report == INVALID_HANDLE_VALUE) { CloseHandle(output); return 3; }
  if (!SetEvent(ack)) { CloseHandle(report); CloseHandle(output); return 4; }
  bool io_error = false, footer = false, oversized = false;
  unsigned long long lines = 0, trailing = 0, bytes = 0;
  std::map<std::string, unsigned long long> counts;
  std::string line;
  char buffer[16384];
  for (;;) {
    DWORD received = 0;
    if (!ReadFile(input, buffer, sizeof(buffer), &received, nullptr)) {
      if (GetLastError() != ERROR_BROKEN_PIPE) io_error = true;
      break;
    }
    if (!received) break;
    for (DWORD i = 0; i < received; ++i) {
      const char c = buffer[i];
      if (c != '\n') {
        ++trailing;
        if (line.size() < 65536) line += c;
        else oversized = true;
        continue;
      }
      if (!oversized && line == "\x1e") {
        if (!FlushFileBuffers(output) || !SetEvent(ack)) io_error = true;
      } else {
        // The producer's line length is bounded; never allocate unbounded input.
        if (oversized) { io_error = true; break; }
        line += '\n';
        if (!WriteAll(output, line.data(), line.size())) { io_error = true; break; }
        bytes += line.size();
        ++lines;
        const std::string prefix = "{\"event\":";
        if (line.compare(0, prefix.size(), prefix) == 0) {
          const auto end = line.find('"', prefix.size() + 1); // closing quote after {"event":"
          if (end != std::string::npos) {
            const auto name = line.substr(prefix.size() + 1, end - prefix.size() - 1);
            if (!name.empty() && name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos) {
              if (counts.size() < 1024 || counts.count(name)) ++counts[name];
              footer = name == "footer";
            }
          }
        }
      }
      line.clear();
      trailing = 0;
      if (io_error) break;
    }
    if (io_error) break;
  }
  if (!line.empty()) {
    if (!WriteAll(output, line.data(), line.size())) io_error = true;
    bytes += line.size();
  }
  if (!FlushFileBuffers(output)) io_error = true;
  CloseHandle(output);
  // This receipt never substitutes for the producer footer or proves coverage.
  std::string json = "{\"schema\":1,\"writer\":\"external-pipe-v1\",\"producer_footer_seen\":";
  json += footer && !trailing ? "true" : "false";
  json += ",\"io_error\":";
  json += io_error ? "true" : "false";
  json += ",\"complete_lines\":" + std::to_string(lines) +
      ",\"trailing_bytes\":" + std::to_string(trailing) +
      ",\"bytes\":" + std::to_string(bytes) +
      ",\"unsubmitted_events\":null,\"event_counts\":{";
  bool first = true;
  for (const auto& entry : counts) {
    if (!first) json += ',';
    first = false;
    json += "\"" + entry.first + "\":" + std::to_string(entry.second);
  }
  json += "}}\n";
  if (!WriteAll(report, json.data(), json.size()) || !FlushFileBuffers(report)) io_error = true;
  CloseHandle(report);
  CloseHandle(input);
  CloseHandle(ack);
  return io_error ? 5 : 0;
}
