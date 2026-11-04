#include "../src/core/menu_state.h"

#include <cstdio>

int main() {
  int failures = 0;
  auto check = [&](bool ok) { if (!ok) ++failures; };
  using namespace dsvr;

  // Stable state is accepted only after the full debounce window.
  {
    MenuDebouncer d(150);
    check(d.Feed(MenuState::Gameplay, 0).state == MenuState::Unknown);
    check(d.Feed(MenuState::Gameplay, 149).state == MenuState::Unknown);
    auto s = d.Feed(MenuState::Gameplay, 150);
    check(s.state == MenuState::Gameplay);
    check(s.confidence == 1.0f);
    check(s.generation == 1);
  }

  // Brief close/open flicker during navigation must not change state.
  {
    MenuDebouncer d(150);
    d.Feed(MenuState::ReadingMenu, 0);
    d.Feed(MenuState::ReadingMenu, 200);  // accepted
    d.Feed(MenuState::Gameplay, 300);     // menu "closes" briefly
    auto s = d.Feed(MenuState::ReadingMenu, 350);  // reopens in 50 ms
    check(s.state == MenuState::ReadingMenu);
    check(s.generation == 1);
  }

  // A real transition increments generation exactly once.
  {
    MenuDebouncer d(150);
    d.Feed(MenuState::Gameplay, 0);
    d.Feed(MenuState::Gameplay, 200);
    d.Feed(MenuState::ReadingMenu, 300);
    auto s = d.Feed(MenuState::ReadingMenu, 500);
    check(s.state == MenuState::ReadingMenu);
    check(s.generation == 2);
    auto s2 = d.Feed(MenuState::ReadingMenu, 900);
    check(s2.generation == 2);
  }

  // Canvas policy: menus yes, gameplay/unknown never.
  check(MenuStateWantsCanvas(MenuState::FrontEnd));
  check(MenuStateWantsCanvas(MenuState::ReadingMenu));
  check(MenuStateWantsCanvas(MenuState::OtherMenu));
  check(!MenuStateWantsCanvas(MenuState::Gameplay));
  check(!MenuStateWantsCanvas(MenuState::Unknown));

  // Panel policy: the calibration screen needs the pointer/stick route but must
  // never replace the game image with a head-anchored canvas. Conflating the
  // two put the calibration panel in the user's face during loading.
  check(MenuStateWantsPanel(MenuState::CalibrationRoom));
  check(!MenuStateWantsCanvas(MenuState::CalibrationRoom));
  check(MenuStateWantsPanel(MenuState::ReadingMenu));
  check(MenuStateWantsPanel(MenuState::FrontEnd));
  check(MenuStateWantsPanel(MenuState::OtherMenu));
  check(!MenuStateWantsPanel(MenuState::Gameplay));
  check(!MenuStateWantsPanel(MenuState::Unknown));

  // Calibration is the startup room and hands straight back to the current
  // game surface as soon as its capture ends; no stale calibration frame can
  // leak into the title theater or the loading view.
  {
    MenuDebouncer d(150);
    check(d.Feed(MenuState::CalibrationRoom, 0).state == MenuState::CalibrationRoom);
    check(d.Feed(MenuState::FrontEnd, 1).state == MenuState::FrontEnd);
    check(d.Feed(MenuState::CalibrationRoom, 2).state == MenuState::CalibrationRoom);
    check(d.Feed(MenuState::Loading, 3).state == MenuState::Loading);
    check(d.Feed(MenuState::Gameplay, 4).state == MenuState::Loading);
    check(d.Feed(MenuState::Gameplay, 154).state == MenuState::Gameplay);
  }
  {
    MenuDebouncer d(150);
    check(d.Feed(MenuState::Loading, 0).state == MenuState::Loading);
    check(d.Feed(MenuState::Gameplay, 1).state == MenuState::Loading);
    // Once the camera is ready, loading must leave quickly enough that the
    // 16:9 transition surface cannot linger through several seconds of play.
    check(d.Feed(MenuState::Gameplay, 34).state == MenuState::Gameplay);
  }
  {
    MenuDebouncer d(150);
    check(d.Feed(MenuState::Gameplay, 0).state == MenuState::Unknown);
    check(d.Feed(MenuState::FrontEnd, 1).state == MenuState::FrontEnd);
    check(d.Feed(MenuState::Loading, 2).state == MenuState::Loading);
  }

  // Confidence ramp: 0 at first differing feed, mid-window in (0,1).
  {
    MenuDebouncer d(150);
    auto s0 = d.Feed(MenuState::Gameplay, 0);
    check(s0.state == MenuState::Unknown);
    check(s0.confidence == 0.0f);
    auto s1 = d.Feed(MenuState::Gameplay, 75);
    check(s1.state == MenuState::Unknown);
    check(s1.confidence > 0.0f && s1.confidence < 1.0f);
    auto s2 = d.Feed(MenuState::Gameplay, 150);
    check(s2.state == MenuState::Gameplay);
    check(s2.confidence == 1.0f);
  }

  // Zero debounce accepts on the first feed at the same timestamp.
  {
    MenuDebouncer d(0);
    auto s = d.Feed(MenuState::ReadingMenu, 100);
    check(s.state == MenuState::ReadingMenu);
    check(s.confidence == 1.0f);
    check(s.generation == 1);
    auto s2 = d.Feed(MenuState::Gameplay, 100);
    check(s2.state == MenuState::Gameplay);
    check(s2.generation == 2);
  }

  // Reset() returns to Unknown/generation 0.
  {
    MenuDebouncer d(150);
    d.Feed(MenuState::Gameplay, 0);
    d.Feed(MenuState::Gameplay, 200);
    check(d.Current().generation == 1);
    d.Reset();
    check(d.Current().state == MenuState::Unknown);
    check(d.Current().generation == 0);
    check(d.Current().confidence == 0.0f);
  }

  std::printf("menu state: %d failures\n", failures);
  return failures ? 1 : 0;
}
