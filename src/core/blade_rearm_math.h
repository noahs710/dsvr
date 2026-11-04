#pragma once

// Spent-contact lifecycle: valley rearm (lane B bench follow-up).
//
// Design adapted from ERVR's weapon_contact.h (GPL-3.0, (c) Ilya Mezerowsky):
// SpentContactEnd (valley/timeout) and RearmReady. Reimplemented for DSVR
// types; no ERVR code is copied. The bench's time-based rehit gate lets a
// lingering blade re-fire after the window; motion-correct behavior ends a
// spent contact at the tip's next speed valley (or a timeout for long
// weapons whose tips seldom slow) and requires the tip to speed up again
// from the lowest speed it reached before a new contact opens — the
// follow-through of the swing that just hit only slows down, so it can
// never open a second hit.
//
// Pure frame-driven logic: the caller feeds tip speed each frame. The
// consumer seam is EvaluateBladeContact's rehit gate (contact stays shut
// while spent or un-rearmed); NOT wired there yet — the bench's current
// time-gate behavior is unchanged until a headset pass approves the swap.

#include <cmath>

namespace dsvr {

struct BladeRearm {
  bool spent = false;
  bool rearming = false;  // spend ended, waiting for the speedup
  float peak = 0.0f;      // tip speed when the contact was spent
  float minimum = 0.0f;   // lowest tip speed since (rearm floor)
  float off_speed = 0.0f;
  double spent_at = 0.0;

  void Reset() { *this = BladeRearm{}; }

  void ArmSpent(float tip_peak, float off, double now_s) {
    spent = true;
    rearming = false;
    peak = tip_peak;
    minimum = tip_peak;
    off_speed = off;
    spent_at = now_s;
  }

  // Feed one frame. Returns true while a new contact may open.
  // valley_frac: valley = max(valley_frac * peak, off_speed).
  // spent_seconds: timeout for tips that never slow (long weapons).
  bool AllowContact(float tip, double now_s, float valley_frac,
                    double spent_seconds) {
    if (!std::isfinite(tip) || tip < 0.0f) return !spent && !rearming;
    if (spent) {
      if (tip < minimum) minimum = tip;
      const float valley =
          valley_frac * peak > off_speed ? valley_frac * peak : off_speed;
      if (tip < valley || now_s - spent_at >= spent_seconds) {
        spent = false;  // valley or timeout end: needs re-acceleration
        rearming = true;
      } else {
        return false;
      }
    }
    if (rearming) {
      if (Rearmed(tip)) {
        rearming = false;
        minimum = 0.0f;
        return true;
      }
      return false;
    }
    return true;
  }

  // The tip must speed up again from the lowest speed it reached since the
  // spend: rearm_min <= 0 disables the condition.
  bool Rearmed(float tip) const {
    return minimum <= 0.0f || tip >= minimum * 1.5f + 0.5f;
  }
};

}  // namespace dsvr
