#include <windows.h>
#include <cstdio>
#include <fstream>
#include <iterator>
namespace {
unsigned flushes = 0;
BOOL WINAPI CountFlush(HANDLE file) { ++flushes; return ::FlushFileBuffers(file); }
}
#define FlushFileBuffers CountFlush
#include "../src/common/logging.cpp"
#undef FlushFileBuffers
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main() {
  using namespace dsvr;
  wchar_t dir[MAX_PATH]{}, path[MAX_PATH]{};
  CHECK(GetTempPathW(MAX_PATH, dir));
  CHECK(GetTempFileNameW(dir, L"dsv", 0, path));
  CHECK(LogInit(path));
  const unsigned initial_flushes = flushes;
  LogClearRecent();
  const auto start = GetTickCount64();
  for(int i=0;i<600;++i) LogWrite(LogLevel::Info,"fixture",1,"line %d",i);
  const auto elapsed = GetTickCount64() - start;
  CHECK(flushes == initial_flushes);
  { std::ifstream file(path, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    CHECK(text.find("line 599") != std::string::npos);
  }
  const auto recent=LogRecent();CHECK(recent.size()==512);
  CHECK(recent.front().text.find("line 88")!=std::string::npos);
  CHECK(LogRecent(recent.back().sequence).empty());
  LogWrite(LogLevel::Error,"fixture",2,"failure 100%%");
  CHECK(flushes == initial_flushes + 1);
  const auto fresh=LogRecent(recent.back().sequence);CHECK(fresh.size()==1);
  CHECK(fresh[0].level==LogLevel::Error && fresh[0].text.find("failure 100%")!=std::string::npos);
  const auto sequence=fresh[0].sequence;LogClearRecent();CHECK(LogRecent().empty());
  LogWrite(LogLevel::Warn,"fixture",3,"after clear");CHECK(LogRecent()[0].sequence>sequence);
  CHECK(flushes == initial_flushes + 1);
  LogShutdown();CHECK(flushes == initial_flushes + 2);
  CHECK(DeleteFileW(path));
  std::printf("logging: PASS, 600 info lines in %llu ms without disk barriers, errors/shutdown durable\n", elapsed);
  return 0;
}
