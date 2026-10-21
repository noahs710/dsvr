// Offline micro-benchmark for GuardedReadBytes cost profiles.
// cl /nologo /std:c++20 /O2 /EHsc /I src tools\profile_guarded_read.cpp
//   src\common\memory.cpp /Fe:build\profile_guarded_read.exe /link bcrypt.lib
#include "../src/common/memory.h"

#include <chrono>
#include <cstdio>
#include <vector>

using namespace dsvr;
namespace {
volatile std::uint64_t sink = 0;
double Run(const char* name, std::uintptr_t addr, void* buf, std::size_t size,
           int iterations, bool scoped) {
  const auto begin = std::chrono::steady_clock::now();
  std::uint64_t total = 0;
  if (scoped) {
    GuardedReadScope scope;
    for (int i = 0; i < iterations; ++i)
      if (GuardedReadBytes(addr, buf, size)) total += ((unsigned char*)buf)[0];
  } else {
    for (int i = 0; i < iterations; ++i)
      if (GuardedReadBytes(addr, buf, size)) total += ((unsigned char*)buf)[0];
  }
  const auto end = std::chrono::steady_clock::now();
  sink += total;
  const double us =
      std::chrono::duration<double, std::micro>(end - begin).count() / iterations;
  std::printf("%-40s %8.3f us/read\n", name, us);
  return us;
}
}  // namespace

int main() {
  constexpr int kIterations = 20000;
  std::vector<unsigned char> local(4096, 7);
  unsigned char buf[3840]{};
  const auto local_addr = reinterpret_cast<std::uintptr_t>(local.data());
  Run("same 8B, unscoped", local_addr, buf, 8, kIterations, false);
  Run("same 8B, scoped", local_addr, buf, 8, kIterations, true);
  Run("same 3840B, scoped", local_addr, buf, 3840, kIterations, true);
  // Many distinct pages inside one scoped transaction (scope holds 16).
  std::vector<std::uintptr_t> pages;
  for (int i = 0; i < 64; ++i)
    pages.push_back(local_addr + (i % 16) * 64);
  const auto begin = std::chrono::steady_clock::now();
  std::uint64_t total = 0;
  {
    GuardedReadScope scope;
    for (int i = 0; i < kIterations; ++i)
      if (GuardedReadBytes(pages[i % pages.size()], buf, 8))
        total += buf[0];
  }
  const auto end = std::chrono::steady_clock::now();
  sink += total;
  std::printf("%-40s %8.3f us/read\n", "64 addrs same region, scoped",
              std::chrono::duration<double, std::micro>(end - begin).count() /
                  kIterations);
  std::printf("sink=%llu\n", (unsigned long long)sink);
  return 0;
}
