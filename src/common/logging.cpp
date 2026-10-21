#include "logging.h"
#include "version.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <deque>

namespace dsvr {
namespace {

std::mutex g_mutex;
HANDLE g_file = INVALID_HANDLE_VALUE;
bool g_init = false;
std::deque<LogEntry> g_recent;
std::uint64_t g_sequence = 0;

const char* LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
  }
  return "?";
}

void WriteRaw(const char* text) {
  if (g_file == INVALID_HANDLE_VALUE || !text) return;
  DWORD written = 0;
  // WriteFile exposes each line to readers even after a process crash. A disk
  // barrier for routine gameplay logs stalls the render thread unnecessarily.
  WriteFile(g_file, text, (DWORD)strlen(text), &written, nullptr);
}

}  // namespace

bool LogInit(const wchar_t* log_path) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_init) return g_file != INVALID_HANDLE_VALUE;
  g_init = true;
  // Win32 handle (not CRT FILE*): loader DLL and core DLL each link their own
  // copy of this static lib, and CRT fopen locks the file exclusively -- the
  // core's log lines were silently lost in the first load test
  // (2026-10-12). FILE_APPEND_DATA + full sharing lets both DLLs append and
  // lets users/support read the log while the game is still running.
  // NOTE: never use ccs=UTF-8 open modes here (fast-fail 0xC0000409 under
  // VS 2026 static CRT). Log content is ASCII.
  g_file = CreateFileW(log_path, FILE_APPEND_DATA,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (g_file == INVALID_HANDLE_VALUE) return false;
  SYSTEMTIME st{};
  GetLocalTime(&st);
  char banner[256]{};
  snprintf(banner, sizeof(banner),
           "--- DSVR %s log opened %04u-%02u-%02u %02u:%02u:%02u ---\n",
           kModVersion, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
           st.wSecond);
  WriteRaw(banner);
    FlushFileBuffers(g_file);
  return true;
}

void LogShutdown() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_file != INVALID_HANDLE_VALUE) {
    WriteRaw("--- DSVR log closed ---\n");
        FlushFileBuffers(g_file);
    CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
  }
}

void LogWrite(LogLevel level, const char* file, int line, const char* fmt,
              ...) {
  // Format into a stack buffer first (no lock held during vsnprintf of user
  // args beyond our mutex; messages are short by design).
  char message[1024]{};
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(message, sizeof(message), fmt, ap);
  va_end(ap);

  std::lock_guard<std::mutex> lock(g_mutex);
  SYSTEMTIME st{};
  GetLocalTime(&st);
  const char* base = file;
  if (file) {
    const char* slash = strrchr(file, '\\');
    const char* fslash = strrchr(file, '/');
    const char* cut = slash > fslash ? slash : fslash;
    if (cut) base = cut + 1;
  }
  char out[1200]{};
  snprintf(out, sizeof(out), "[%02u:%02u:%02u.%03u][%s][%s:%d] %s\n",
           st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
           LevelName(level), base ? base : "?", line, message);
  if (g_recent.size() == 512) g_recent.pop_front();
  g_recent.push_back({++g_sequence, level, out});
  WriteRaw(out);
    if (level == LogLevel::Error && g_file != INVALID_HANDLE_VALUE) FlushFileBuffers(g_file);
}

std::vector<LogEntry> LogRecent(std::uint64_t after) {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<LogEntry> entries;
  for (const auto& entry : g_recent)
    if (entry.sequence > after) entries.push_back(entry);
  return entries;
}

void LogClearRecent() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_recent.clear();
}

std::wstring LogPathNextToModule(void* module_handle, const wchar_t* filename) {
  wchar_t dir[32768]{};
  HMODULE mod = static_cast<HMODULE>(module_handle);
  DWORD n = GetModuleFileNameW(mod, dir, 32767);
  std::wstring path;
  if (n == 0 || n >= 32767) {
    path = L".\\";
  } else {
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) {
      slash[1] = L'\0';
      path = dir;
    } else {
      path = L".\\";
    }
  }
  path += filename;
  return path;
}

}  // namespace dsvr
