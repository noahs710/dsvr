#pragma once

namespace dsvr {

// Quest controller chords for the keyboard F-key functions (Quest has no
// F-keys in the headset). Pure edge detector: each chord fires once when it
// becomes fully pressed, never repeats while held, and re-arms on release.
//   Theater (F8)  : Menu held + Y
//   Marker  (F9)  : Menu held + X
//   Reload  (F7)  : Menu held + B
//   Recenter(F6)  : both thumbstick clicks together (no Menu needed)
// HeldChordDetector covers the same jobs on hardware that cannot report a
// Menu click to the app; see the secondary-button chord in input.cpp.
enum ComboEvent : unsigned {
  kComboNone = 0,
  kComboTheater = 1u << 0,
  kComboMarker = 1u << 1,
  kComboReload = 1u << 2,
  kComboRecenter = 1u << 3,
};

struct ComboButtons {
  bool menu = false;     // left Menu
  bool x = false;        // left primary (X)
  bool y = false;        // left secondary (Y)
  bool b = false;        // right secondary (B)
  bool lstick = false;   // left thumbstick click
  bool rstick = false;   // right thumbstick click
};

class ComboDetector {
 public:
  unsigned Update(ComboButtons in) {
    const bool theater = in.menu && in.y;
    const bool marker = in.menu && in.x;
    const bool reload = in.menu && in.b;
    const bool recenter = in.lstick && in.rstick;
    unsigned events = kComboNone;
    if (theater && !theater_) events |= kComboTheater;
    if (marker && !marker_) events |= kComboMarker;
    if (reload && !reload_) events |= kComboReload;
    if (recenter && !recenter_) events |= kComboRecenter;
    theater_ = theater; marker_ = marker; reload_ = reload;
    recenter_ = recenter;
    return events;
  }
  void Reset() { theater_ = marker_ = reload_ = recenter_ = false; }

 private:
  bool theater_ = false, marker_ = false, reload_ = false, recenter_ = false;
};

// Sustained-chord detector, also usable when the runtime reserves Menu for
// system UI. Hold both buttons for `hold_ns` to fire once; re-arms on release.
// Pure time logic, driven by the caller's frame clock.
class HoldChordDetector {
 public:
  explicit HoldChordDetector(long long hold_ns) : hold_ns_(hold_ns) {}

  // True exactly once per hold, on the sample where the chord has been down
  // for at least hold_ns_. A clock that goes backwards restarts the hold.
  bool Update(bool down, long long now_ns) {
    if (!down) {
      started_ = false;
      fired_ = false;
      return false;
    }
    if (!started_ || now_ns < start_ns_) {
      started_ = true;
      start_ns_ = now_ns;
      return false;
    }
    if (!fired_ && now_ns - start_ns_ >= hold_ns_) {
      fired_ = true;
      return true;
    }
    return false;
  }
  void Reset() { started_ = false; fired_ = false; }

 private:
  long long hold_ns_ = 0;
  long long start_ns_ = 0;
  bool started_ = false;
  bool fired_ = false;
};

struct ButtonPairResult {
  bool first = false, second = false, triggered = false;
};

// Reserve a short lead-in for the partner button without losing ordinary taps.
// Once a lone button has reached gameplay, this press cannot become a chord.
// A consumed chord stays reserved until BOTH buttons release, including after
// firing; Reset discards all pending presses on input/tracking invalidation.
class ButtonPairChord {
 public:
  ButtonPairChord(long long hold_ns, long long lead_ns)
      : hold_(hold_ns), lead_ns_(lead_ns) {}

  void Reset() {
    hold_.Reset();
    active_ = reserved_ = delivered_ = fired_ = false;
    pending_[0] = pending_[1] = false;
    started_ns_ = last_ns_ = 0;
  }

  ButtonPairResult Update(bool first, bool second, long long now_ns) {
    if (active_ && now_ns < last_ns_) Reset();
    const bool any = first || second;
    if (!active_) {
      if (!any) return {};
      active_ = true;
      started_ns_ = now_ns;
    }
    last_ns_ = now_ns;
    if (!delivered_) {
      pending_[0] |= first;
      pending_[1] |= second;
      reserved_ |= first && second;
    }
    ButtonPairResult out;
    if (reserved_) {
      const bool held = hold_.Update(first && second, now_ns);
      out.triggered = held && !fired_;
      fired_ |= held;
    } else if (delivered_) {
      out.first = first;
      out.second = second;
    } else if (!any || now_ns - started_ns_ >= lead_ns_) {
      out.first = pending_[0];
      out.second = pending_[1];
      delivered_ = true;
      pending_[0] = pending_[1] = false;
    }
    if (!any) Reset();
    return out;
  }

 private:
  HoldChordDetector hold_;
  long long lead_ns_ = 0, started_ns_ = 0, last_ns_ = 0;
  bool active_ = false, reserved_ = false, delivered_ = false, fired_ = false;
  bool pending_[2]{};
};

}  // namespace dsvr
