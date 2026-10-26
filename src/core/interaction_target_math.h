#pragma once

#include <cmath>

namespace dsvr {

// Interaction-target eligibility gate (H2 shape, no physics).
//
// Today a rested forward thrust (ReachIntentTrigger) taps the native
// interact button with no target knowledge: no prompt flag is bound, so the
// gesture alone decides. The runbook requires per-class eligibility —
// reach/aim, distance, line of sight, verified prompt, ownership and
// lifecycle — before routine progression can be called controller-usable.
// This header supplies the AND-gate shape the targeting producer will fill:
// a qualified thrust fires only when an eligible target is actually
// selected, and every unknown refuses fail-closed.
//
// No physics, no native reads/writes, no menu decisions. When the native
// prompt/LOS owner exists, it produces InteractionTargetEligibility; until
// then the default-constructed value refuses everything.

struct InteractionTargetEligibility {
  // Set by the native targeting producer once H1 identifies the per-class
  // prompt/activation owner. All false/unknown until then.
  bool prompt_visible = false;  // native prompt actually offered
  bool line_of_sight_clear = false;  // no wall/occlusion between hand and target
  bool target_valid = false;  // lifecycle: load/death/reuse/invalidation checked
  float dist_to_target_m = -1.0f;  // hand/object-to-interactable distance, metres
  float max_reach_m = 0.0f;  // this interaction class's allowed range
};

// Per-class range table (H1 shape, no physics, no native reads).
//
// The gate above is class-agnostic: whichever producer fills the struct owns
// the max. This table records, per interaction class, the allowed range plus
// where the number came from. Every entry starts at 0.0 (refuse) with an
// "unverified" provenance until H1 cites a research-cache or native source;
// a 0.0 max refuses everything through the existing finite/positive checks,
// so adding the table changes no live decision today.
enum class InteractionClass : std::uint8_t {
  Unknown = 0,
  Lever = 1,  // first bounded manipulation class per H3; owner still open
};

struct InteractionClassRange {
  InteractionClass cls = InteractionClass::Unknown;
  float max_reach_m = 0.0f;  // 0.0 = unverified: refuses until an H1 value is cited
  const char* provenance = "unverified: refuses until H1 research-cache value cited";
};

inline constexpr InteractionClassRange kInteractionClassRanges[] = {
// Installed pinned GameParam supplies per-row validDist observations:
// lever rows differ (90, 110 and 60 raw; PARAMDEF labels centimetres).
// This class-wide gate still refuses: selected native row, distance origin,
// prompt/LOS and object lifetime are unbound. Do not substitute a default
// or one class-wide guess. See docs/NATIVE_INTERACTION_CONTRACT.md.
    {InteractionClass::Lever, 0.0f,
     "unverified: refuses until H1 research-cache value cited"},
};

// Fail-closed eligibility: every condition must positively hold.
inline bool SelectTargetEligible(const InteractionTargetEligibility& e) {
  if (!e.prompt_visible || !e.line_of_sight_clear || !e.target_valid)
    return false;
  if (!std::isfinite(e.dist_to_target_m) || !std::isfinite(e.max_reach_m))
    return false;
  if (!(e.max_reach_m > 0.0f)) return false;
  if (!(e.dist_to_target_m >= 0.0f)) return false;
  return e.dist_to_target_m <= e.max_reach_m;
}

// Class-aware eligibility: Unknown classes always refuse; known classes use
// their table max before the shared AND-gate. Non-finite/negative table
// values refuse through SelectTargetEligible's own checks.
inline bool SelectTargetEligibleForClass(const InteractionTargetEligibility& e,
                                         InteractionClass cls) {
  if (cls == InteractionClass::Unknown) return false;
  for (const auto& row : kInteractionClassRanges) {
    if (row.cls == cls) {
      InteractionTargetEligibility with_class = e;
      with_class.max_reach_m = row.max_reach_m;
      return SelectTargetEligible(with_class);
    }
  }
  return false;
}

}  // namespace dsvr
