// Memory-utils test: guarded read/write, chain following, byte matching, and
// pattern-scan edge cases against our own module. No game needed.
//
// Uses CHECK (always executes) instead of assert so Release builds with NDEBUG
// still run every call.
#include <windows.h>

#include <cstdio>

#include "../src/common/memory.h"

#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                              \
    }                                                        \
  } while (0)

int main() {
  // GuardedRead on good memory.
  int value = 0x12345678;
  int out = 0;
  CHECK(dsvr::GuardedRead((std::uintptr_t)&value, out));
  CHECK(out == 0x12345678);

  // GuardedRead on null must fail, not crash.
  CHECK(!dsvr::GuardedRead(0, out));

  // GuardedWrite round-trip on our own stack.
  int slot = 0;
  int forty_two = 42;
  CHECK(dsvr::GuardedWriteBytes((std::uintptr_t)&slot, &forty_two,
                                sizeof(forty_two)));
  CHECK(slot == 42);

  // FollowChain over a local link.
  struct Node {
    void* next;
    int pad;
    int payload;
  };
  Node tail{nullptr, 0, 777};
  Node head{(void*)&tail, 0, 0};
  std::uintptr_t step[] = {0};
  auto tail_addr = dsvr::FollowChain((std::uintptr_t)&head, step, 1);
  CHECK(tail_addr && *tail_addr == (std::uintptr_t)&tail);
  int payload = 0;
  CHECK(dsvr::GuardedRead(*tail_addr + offsetof(Node, payload), payload));
  CHECK(payload == 777);
  // Bad chain fails cleanly.
  Node broken{nullptr, 0, 0};
  CHECK(!dsvr::FollowChain((std::uintptr_t)&broken, step, 1));

  // BytesMatch on known bytes.
  unsigned char blob[] = {0xDE, 0xAD, 0xBE, 0xEF};
  unsigned char same[] = {0xDE, 0xAD, 0xBE, 0xEF};
  unsigned char diff[] = {0xDE, 0xAD, 0xBE, 0xEE};
  CHECK(dsvr::BytesMatch((std::uintptr_t)blob, same, sizeof(same)));
  CHECK(!dsvr::BytesMatch((std::uintptr_t)blob, diff, sizeof(diff)));
  CHECK(!dsvr::BytesMatch(0, same, sizeof(same)));

  // ModuleBase/ModuleSize of our own test binary.
  std::uintptr_t self = dsvr::ModuleBase(nullptr);
  CHECK(self != 0);
  CHECK(dsvr::ModuleSize(self) > 0x1000);

  // PatternScan with invalid patterns returns empty (no crash).
  CHECK(dsvr::PatternScan(self, "ZZ ZZ").empty());
  CHECK(dsvr::PatternScan(self, "").empty());

  // CurrentExePath is non-empty.
  CHECK(!dsvr::CurrentExePath().empty());

  printf("test_memory: PASS\n");
  return 0;
}
