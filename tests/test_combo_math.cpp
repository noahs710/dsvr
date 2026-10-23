#include "../src/core/combo_math.h"

#include <cstdio>
#include <source_location>

int main() {
  int failures = 0;
  auto check = [&](bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) { ++failures; std::printf("failed at line %u\n", at.line()); }
  };
  using namespace dsvr;
  ComboButtons none{}, menuY{}, menuX{}, menuB{}, sticks{};

  menuY.menu = true; menuY.y = true;
  menuX.menu = true; menuX.x = true;
  menuB.menu = true; menuB.b = true;
  sticks.lstick = true; sticks.rstick = true;

  ComboDetector d;
  check(d.Update(none) == kComboNone);
  check(d.Update(menuY) == kComboTheater);   // rising edge fires
  check(d.Update(menuY) == kComboNone);      // held: no repeat
  check(d.Update(none) == kComboNone);       // release re-arms
  check(d.Update(menuY) == kComboTheater);   // press again fires again
  d.Update(none);
  // Partial chords never fire.
  ComboButtons onlyMenu{}; onlyMenu.menu = true;
  ComboButtons onlyY{}; onlyY.y = true;
  ComboButtons oneStick{}; oneStick.lstick = true;
  check(d.Update(onlyMenu) == kComboNone);
  check(d.Update(onlyY) == kComboNone);
  check(d.Update(oneStick) == kComboNone);
  // Each function maps to its own event; recenter needs no Menu.
  check(d.Update(menuX) == kComboMarker); d.Update(none);
  check(d.Update(menuB) == kComboReload); d.Update(none);
  check(d.Update(sticks) == kComboRecenter);
  check(d.Update(sticks) == kComboNone);
  d.Update(none);
  // Adding the second button later still counts as one rising chord.
  check(d.Update(onlyMenu) == kComboNone);
  check(d.Update(menuY) == kComboTheater);
  // Reset clears held state without firing.
  d.Reset();
  check(d.Update(menuY) == kComboTheater);

  // Sustained chord (Quest Y+B -> theater toggle): fires once after the hold
  // window, never twice, and re-arms on release.
  {
    const long long k500ms = 500000000LL;
    HoldChordDetector h{k500ms};
    check(!h.Update(false, 0));
    check(!h.Update(true, 0));                 // start of hold
    check(!h.Update(true, 300000000LL));       // 0.3s: not yet
    check(h.Update(true, 500000000LL));        // 0.5s: fires exactly once
    check(!h.Update(true, 900000000LL));       // held: no repeat
    check(!h.Update(true, 5000000000LL));
    check(!h.Update(false, 5100000000LL));     // release re-arms
    check(!h.Update(true, 5200000000LL));
    check(h.Update(true, 5700000000LL));       // second hold fires
    h.Reset();
    check(!h.Update(true, 5800000000LL));      // reset does not fire
    check(h.Update(true, 6300000000LL));
    // Clock going backwards restarts the hold instead of firing early.
    h.Reset();
    check(!h.Update(true, 10000000000LL));
    check(!h.Update(true, 9000000000LL));      // backwards: restart
    check(!h.Update(true, 9300000000LL));
    check(h.Update(true, 9500000000LL));
  }

  if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
  std::printf("combo_math: all checks passed\n");
  return 0;
}
