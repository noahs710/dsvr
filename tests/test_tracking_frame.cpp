// Coherent publication seam: no game, headset, or OpenXR runtime required.
#include <cstdio>
#include <limits>
#include <atomic>
#include <thread>

#include "../src/core/tracking_frame.h"

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                 \
    if (!(cond)) {                                                     \
      ++g_failures;                                                    \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
    }                                                                  \
  } while (0)

using namespace dsvr;

int main() {
  TrackingFrames frames;
  TrackingFrame in{}, out{};
  in.orientation_valid = in.position_valid = in.focused = true;
  in.captured_ms = 100;
  in.predicted_time_ns = 900000000;
  in.display_period_ns = 11111111;
  in.head_pos[0] = 2.0f;
  in.hand[0].valid = in.arm[0].valid = true;
  in.hand[0].pos[0] = in.arm[0].pos[0] = 1.0f;
  in.hand[1].valid = in.arm[1].valid = true;
  in.hand[1].pos[0] = in.arm[1].pos[0] = 3.0f;
  CHECK(frames.Publish(in) == 1);
  CHECK(frames.Read(110, 100, out));
  CHECK(out.sequence == 1 && out.space_epoch == 1);
  CHECK(out.head_pos[0] == 2.0f);
  CHECK(out.hand[0].pos[0] == 1.0f && out.arm[1].pos[0] == 3.0f);
  CHECK(out.predicted_time_ns == 900000000);
  CHECK(out.display_period_ns == 11111111);
  // Mutating the caller's packet cannot mutate the published snapshot.
  in.head_pos[0] = 99.0f;
  CHECK(frames.Read(110, 100, out) && out.head_pos[0] == 2.0f);
  // Losing focus/session must release cached targets, even at the same age.
  frames.Invalidate();
  CHECK(!frames.Read(110, 100, out));
  CHECK(out.sequence == 0 && !out.arm[0].valid && !out.hand[1].valid);
  CHECK(frames.Publish(in) == 2);
  CHECK(frames.Read(110, 100, out) && out.space_epoch == 1);
  frames.Invalidate(true);  // a new session/reference-space calibration
  CHECK(!frames.Read(110, 100, out));
  CHECK(frames.Publish(in) == 3);
  CHECK(frames.Read(110, 100, out) && out.space_epoch == 2);
  // Runtime validity flags cannot make a nonfinite coordinate safe.
  in.head_pos[0] = std::numeric_limits<float>::quiet_NaN();
  frames.Publish(in);
  CHECK(!frames.Read(110, 100, out));
  CHECK(!out.position_valid && !out.arm[0].valid);
  in.head_pos[0] = 2.0f;
  in.arm[0].pos[0] = std::numeric_limits<float>::infinity();
  frames.Publish(in);
  CHECK(frames.Read(110, 100, out));
  CHECK(!out.arm[0].valid && out.arm[1].valid);
  // Freshness is local monotonic time, never subtraction of XrTime.
  CHECK(frames.Read(200, 100, out));
  CHECK(!frames.Read(201, 100, out) && out.sequence == 0);
  CHECK(!frames.Read(99, 100, out));
  in.position_valid = false;
  frames.Publish(in);
  CHECK(!frames.Read(110, 100, out));
  in.position_valid = true;
  in.focused = false;
  frames.Publish(in);
  CHECK(!frames.Read(110, 100, out));
  in.focused = true;
  in.arm[0].pos[0] = in.head_pos[0];
  in.arm[1].pos[0] = -in.head_pos[0];
  frames.Publish(in);
  // Consumers may skip packets but must never combine head/hand packets.
  std::atomic<bool> done{false};
  std::atomic<bool> coherent{true};
  std::atomic<int> reads{0};
  std::thread reader([&] {
    TrackingFrame sample{};
    do {
      if (frames.Read(110, 100, sample)) {
        ++reads;
        if (sample.arm[0].pos[0] != sample.head_pos[0] ||
            sample.arm[1].pos[0] != -sample.head_pos[0])
          coherent.store(false);
      }
    } while (!done.load());
  });
  for (int i = 1; i <= 10000; ++i) {
    in.head_pos[0] = in.arm[0].pos[0] = (float)i;
    in.arm[1].pos[0] = -(float)i;
    frames.Publish(in);
  }
  done.store(true);
  reader.join();
  CHECK(coherent.load());
  CHECK(reads.load() > 0);
  if (!g_failures) std::printf("test_tracking_frame: all checks passed\n");
  return g_failures ? 1 : 0;
}
