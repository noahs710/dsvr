#pragma once

namespace dsvr {

// Quest controller chords for the keyboard F-key functions (Quest has no
// F-keys in the headset). Pure edge detector: each chord fires once when it
// becomes fully pressed, never repeats while held, and re-arms on release.
//   Theater (F8)  : Menu held + Y
//   Marker  (F9)  : Menu held + X
//   Reload  (F7)  : Menu held + B
//   Recenter(F6)  : both thumbstick clicks together (no Menu needed)
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

}  // namespace dsvr
