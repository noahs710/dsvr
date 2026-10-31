#pragma once

namespace dsvr {

// Pure, unit-tested menu-state debounce (P6). Detection (native probe,
// title heuristic, manual F8) stays outside; this module only decides
// when a raw reading is stable enough to change presentation (docs/ENGINEERING.md).
// Short close/open gaps during navigation must not flash immersive mode.
//
// Feed raw readings with a monotonic millisecond clock. The published
// snapshot changes only after the same raw state persists for the full
// debounce window; generation increments on every accepted change so
// consumers can detect transitions without polling timestamps.

enum class MenuState {
  Unknown,
  Gameplay,
  FrontEnd,     // title / main menu
  ReadingMenu,  // inventory, equipment, status, pause
  OtherMenu,    // shops, bonfire, dialogue, confirmations
  // Our own calibration screen: a panel drawn into the game image. It needs
  // the pointer/stick route but must never take the head-anchored canvas.
  CalibrationPanel,
};

struct MenuSnapshot {
  MenuState state = MenuState::Unknown;
  float confidence = 0.0f;  // 1.0 = fully debounced; <1 while settling
  unsigned long long generation = 0;
};

class MenuDebouncer {
 public:
  explicit MenuDebouncer(long long debounce_ms = 150)
      : debounce_ms_(debounce_ms > 0 ? debounce_ms : 0) {}

  MenuSnapshot Feed(MenuState raw, long long now_ms) {
    if (raw != candidate_) {
      candidate_ = raw;
      candidate_since_ = now_ms;
    }
    long long held = now_ms - candidate_since_;
    if (candidate_ != snapshot_.state && held >= debounce_ms_) {
      snapshot_.state = candidate_;
      snapshot_.generation += 1;
    }
    // Confidence reflects the candidate the snapshot is converging to:
    // full once accepted, proportional while it is still settling.
    snapshot_.confidence =
        candidate_ == snapshot_.state
            ? 1.0f
            : debounce_ms_ ? (float)held / (float)debounce_ms_ : 1.0f;
    if (snapshot_.confidence > 1.0f) snapshot_.confidence = 1.0f;
    if (snapshot_.confidence < 0.0f) snapshot_.confidence = 0.0f;
    return snapshot_;
  }

  MenuSnapshot Current() const { return snapshot_; }

  void Reset() {
    snapshot_ = {};
    candidate_ = MenuState::Unknown;
    candidate_since_ = 0;
  }

 private:
  long long debounce_ms_;
  MenuSnapshot snapshot_;
  MenuState candidate_ = MenuState::Unknown;
  long long candidate_since_ = 0;
};

// Presentation policy: only these states take the spatial canvas (a
// head-anchored quad that replaces the game image). Gameplay and Unknown never
// do (Unknown = detector absent/unverified, which per spec stays disabled and
// logs its limitation).
inline bool MenuStateWantsSanctuary(MenuState s) {
  return s == MenuState::FrontEnd || s == MenuState::ReadingMenu ||
         s == MenuState::OtherMenu;
}

// Input policy: these states need the pointer/stick route aimed at a panel.
// Wider than the canvas set: the calibration panel lives inside the flat
// theater image, so it navigates without ever becoming a canvas. Conflating
// the two questions is what put the calibration screen in the user's face and
// hid the loading image behind it (live report 31 Oct 2026).
inline bool MenuStateWantsPanel(MenuState s) {
  return MenuStateWantsSanctuary(s) || s == MenuState::CalibrationPanel;
}

}  // namespace dsvr
