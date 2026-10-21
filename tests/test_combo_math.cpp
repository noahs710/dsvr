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

  if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
  std::printf("combo_math: all checks passed\n");
  return 0;
}
