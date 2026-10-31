#pragma once

#include <cmath>
#include <cstdint>

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

// Native selection binding (H1 shape, no physics).
//
// The ObjAct selection loop (140314E30) picks the first eligible row and the
// activation path (140313470) marks it; the nearest-row sweep (140314150)
// resolves each candidate's dummy position through 14030E8C0. Until a live
// producer binds the selected row handle, object handle, object generation
// and dummy position from an actual callback, the selection is unbound and
// every gesture refuses. Row/object/generation/dummy are the identity the
// native loop itself consumes; no prompt text, LOS term or range follows
// from this struct alone.
struct InteractionSelection {
  // Selected ObjAct row handle (row +0x1C), nonzero when bound.
  std::uint32_t row_handle = 0u;
  // Selected object handle (row +0x1C sibling identity), nonzero when bound.
  std::uint32_t object_handle = 0u;
  // Object generation: the callback-local identity certification (flags & 4
  // in the interaction observer). Zero means the handle may be reused.
  std::uint64_t generation = 0u;
  // Dummy position resolved for the selected row (14030E8C0 output), metres
  // in the frame the producer publishes. Valid only when dummy_valid holds.
  bool dummy_valid = false;
  float dummy_pos[3] = {0.0f, 0.0f, 0.0f};
};

// A selection counts only when every identity field positively holds.
inline bool SelectionBound(const InteractionSelection& s) {
  if (s.row_handle == 0u || s.object_handle == 0u) return false;
  if (s.generation == 0u || !s.dummy_valid) return false;
  for (float v : s.dummy_pos) {
    if (!std::isfinite(v)) return false;
  }
  return true;
}

// Selection freshness: a bound selection is only live for a bounded window
// after its publish time. Native rows recycle (handles 111/110/109 across
// progression) and the observer certifies identity per callback, so a
// selection that outlives its window must refuse rather than tap a reused
// row. Zero/negative publish times and clock warps refuse fail-closed.
inline constexpr long long kInteractionSelectionFreshMs = 500LL;

class InteractionSelectionStore {
 public:
  void Publish(const InteractionSelection& s, long long now_ms) {
    if (now_ms <= 0 || !SelectionBound(s)) {
      Invalidate();
      return;
    }
    selection_ = s;
    published_ms_ = now_ms;
    valid_ = true;
  }
  void Invalidate() {
    valid_ = false;
    published_ms_ = 0;
    selection_ = InteractionSelection{};
  }
  bool Bound(long long now_ms) const {
    if (!valid_ || published_ms_ <= 0 || now_ms < published_ms_) return false;
    if (now_ms - published_ms_ >= kInteractionSelectionFreshMs) return false;
    return SelectionBound(selection_);
  }
  bool Latest(InteractionSelection& out) const {
    if (!valid_) return false;
    out = selection_;
    return true;
  }

 private:
  InteractionSelection selection_{};
  long long published_ms_ = 0;
  bool valid_ = false;
};

// The one class-table lookup: copy the record, overwrite its reach with the
// class row, and refuse (false) when the class is Unknown or has no row, so no
// caller can reach the AND-gate with a guessed reach.
inline bool ClassReachApplied(const InteractionTargetEligibility& e,
                              InteractionClass cls,
                              InteractionTargetEligibility& out) {
  if (cls == InteractionClass::Unknown) return false;
  for (const auto& row : kInteractionClassRanges) {
    if (row.cls == cls) {
      out = e;
      out.max_reach_m = row.max_reach_m;
      return true;
    }
  }
  return false;
}

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
  InteractionTargetEligibility with_class;
  if (!ClassReachApplied(e, cls, with_class)) return false;
  return SelectTargetEligible(with_class);
}

// Selection-bound eligibility: the gesture-level AND-gate plus a bound
// native selection. An eligible gesture with no selected row still refuses;
// a bound selection with no eligible gesture still refuses.
inline bool SelectTargetEligibleWithSelection(const InteractionTargetEligibility& e,
                                              const InteractionSelection& s) {
  if (!SelectionBound(s)) return false;
  return SelectTargetEligible(e);
}

// Class-aware, selection-bound eligibility: Unknown classes always refuse;
// known classes use their table max before the shared AND-gate.
inline bool SelectTargetEligibleForClassWithSelection(
    const InteractionTargetEligibility& e, InteractionClass cls,
    const InteractionSelection& s) {
  if (!SelectionBound(s)) return false;
  InteractionTargetEligibility with_class;
  if (!ClassReachApplied(e, cls, with_class)) return false;
  return SelectTargetEligible(with_class);
}

// Store-bound eligibility: the gesture-level AND-gate plus a live (fresh,
// non-stale) stored selection. A stale or invalidated store refuses even
// when the gesture is eligible.
inline bool SelectTargetEligibleWithStoredSelection(
    const InteractionTargetEligibility& e,
    const InteractionSelectionStore& store, long long now_ms) {
  if (!store.Bound(now_ms)) return false;
  return SelectTargetEligible(e);
}

}  // namespace dsvr
