#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace dsvr {

// Thread-safe file logger. Every line is flushed so crash logs are complete.
// Call Init once (loader or core, whichever runs first wins); Shutdown flushes.
enum class LogLevel { Info, Warn, Error };

bool LogInit(const wchar_t* log_path);
void LogShutdown();
void LogWrite(LogLevel level, const char* file, int line, const char* fmt, ...);
struct LogEntry {
  std::uint64_t sequence = 0;
  LogLevel level = LogLevel::Info;
  std::string text;
};
// Bounded in-memory copy for GUI readers; never tails the disk log.
std::vector<LogEntry> LogRecent(std::uint64_t after = 0);
void LogClearRecent();

// Convenience macros capture file/line.
#define DSVR_LOG(...) ::dsvr::LogWrite(::dsvr::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define DSVR_WARN(...) ::dsvr::LogWrite(::dsvr::LogLevel::Warn, __FILE__, __LINE__, __VA_ARGS__)
#define DSVR_ERROR(...) ::dsvr::LogWrite(::dsvr::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)

// Build a log path next to a module (game folder when loaded in-game).
std::wstring LogPathNextToModule(void* module_handle, const wchar_t* filename);

}  // namespace dsvr
