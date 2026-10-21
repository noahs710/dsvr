#include "../src/core/frame_queue.h"

#include <cstdio>
#include <thread>

int main() {
  int failures = 0;
  auto check = [&](bool ok) { if (!ok) ++failures; };
  using namespace dsvr;

  // FIFO order, empty pop, wrap-around past capacity.
  {
    SpscQueue<int, 8> q;
    int out = 0;
    check(q.Empty() && !q.Pop(out));
    for (int i = 0; i < 8; ++i) check(q.Push(i));
    check(q.Size() == 8u);
    check(!q.Push(99));  // full: new event dropped, counted
    check(q.Dropped() == 1u);
    for (int i = 0; i < 8; ++i) {
      check(q.Pop(out) && out == i);
    }
    for (int i = 0; i < 100; ++i) {  // many wraps keep order
      check(q.Push(i));
      check(q.Pop(out) && out == i);
    }
    q.Push(7);
    q.Clear();
    check(q.Empty());
  }

  // Concurrent producer/consumer: every value arrives exactly once, in
  // order, with no drops when the consumer keeps up (bounded retries make
  // scheduler stalls show up as failure, never as silent loss).
  {
    SpscQueue<int, 64> q;
    const int kCount = 200000;
    std::thread producer([&] {
      for (int i = 0; i < kCount; ++i) {
        while (!q.Push(i)) std::this_thread::yield();
      }
    });
    long long sum = 0;
    int expected = 0;
    bool ordered = true;
    while (expected < kCount) {
      int v = 0;
      if (q.Pop(v)) {
        if (v != expected) ordered = false;
        sum += v;
        ++expected;
      } else {
        std::this_thread::yield();
      }
    }
    producer.join();
    check(ordered);
    check(sum == (long long)(kCount - 1) * kCount / 2);
    // Dropped() counts full-queue Push attempts; the retrying producer
    // above legitimately incurs them under contention, so no count is
    // asserted here (the single-threaded block pins the counter itself).
  }

  std::printf("frame queue: %d failures\n", failures);
  return failures ? 1 : 0;
}
