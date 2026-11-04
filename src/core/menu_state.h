#pragma once

namespace dsvr {

// Pure, unit-tested menu-state debounce (P6). Detection (native probe,
// title heuristic, manual F8) stays outside; this module only decides
// when a raw reading is stable enough to change presentation (docs/ENGINEERING.md).
// Presentation surfaces open immediately; returning to gameplay is debounced
// so short detection gaps cannot flash immersive mode.
//
// Feed raw readings with a monotonic millisecond clock. The published
// snapshot returns to gameplay only after the same raw state persists for the
// full debounce window; generation increments on every accepted change so
// consumers can detect transitions without polling timestamps.

enum class MenuState {
  Unknown,
  Gameplay,
  FrontEnd,     // title / main menu
  Loading,      // game image remains in the 16:9 theater during transitions
  ReadingMenu,  // inventory, equipment, status, pause
  OtherMenu,    // shops, bonfire, dialogue, confirmations
  // Our own calibration room is rendered as an immersive projection scene.
  CalibrationRoom,
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
    // The loading canvas is only needed until a gameplay camera is valid.
    // Confirm that transition for three 90 Hz frames instead of applying the
    // general menu debounce, which can leave a ready game behind its panel.
    if (snapshot_.state == MenuState::Loading && raw == MenuState::Gameplay) {
      if (candidate_ != raw) {
        candidate_ = raw;
        candidate_since_ = now_ms;
      }
      const long long held = now_ms - candidate_since_;
      const long long loading_debounce =
          debounce_ms_ < kLoadingExitDebounceMs
              ? debounce_ms_ : kLoadingExitDebounceMs;
      if (held >= loading_debounce) {
        snapshot_.state = raw;
        ++snapshot_.generation;
        snapshot_.confidence = 1.0f;
      } else {
        snapshot_.confidence = static_cast<float>(held) /
                               static_cast<float>(loading_debounce);
      }
      return snapshot_;
    }
    // A title, calibration, menu or loading surface takes ownership on its
    // first reading. Calibration's exit also skips a stale room frame; only
    // gameplay uses the full debounce window before replacing a surface.
    const bool immediate =
        (raw != MenuState::Unknown && raw != MenuState::Gameplay) ||
        (snapshot_.state == MenuState::CalibrationRoom &&
         raw != MenuState::CalibrationRoom);
    if (immediate) {
      candidate_ = raw;
      candidate_since_ = now_ms;
      if (snapshot_.state != raw) {
        snapshot_.state = raw;
        ++snapshot_.generation;
      }
      snapshot_.confidence = 1.0f;
      return snapshot_;
    }
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
  static constexpr long long kLoadingExitDebounceMs = 33;
  long long debounce_ms_;
  MenuSnapshot snapshot_;
  MenuState candidate_ = MenuState::Unknown;
  long long candidate_since_ = 0;
};

// Presentation policy: only these states take the 16:9 world theater panel.
inline bool MenuStateWantsCanvas(MenuState s) {
  return s == MenuState::FrontEnd || s == MenuState::Loading ||
         s == MenuState::ReadingMenu || s == MenuState::OtherMenu;
}

// Input policy includes the world-space calibration dialogue even though it
// has its own immersive projection renderer.
inline bool MenuStateWantsPanel(MenuState s) {
  return MenuStateWantsCanvas(s) || s == MenuState::CalibrationRoom;
}

}  // namespace dsvr
