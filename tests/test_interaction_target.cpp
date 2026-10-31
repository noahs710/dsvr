// Interaction-target eligibility gate: a rested thrust fires only when a
// native target is actually eligible (prompt visible, line of sight clear,
// lifecycle valid, inside class range). No native reads; the producer that
// fills the eligibility struct (H1 prompt/LOS owner) does not exist yet, so
// the default refuses everything fail-closed.
#include "../src/core/interaction_target_math.h"

#include <cmath>
#include <cstdio>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
  if (!ok) { ++g_fail; std::printf("FAIL: %s\n", what); }
}

using dsvr::InteractionTargetEligibility;
using dsvr::SelectTargetEligible;

InteractionTargetEligibility Eligible() {
  InteractionTargetEligibility e;
  e.prompt_visible = true;
  e.line_of_sight_clear = true;
  e.target_valid = true;
  e.dist_to_target_m = 1.2f;
  e.max_reach_m = 2.0f;
  return e;
}

void FullyEligibleFires() {
  check(SelectTargetEligible(Eligible()),
        "prompt + LOS + valid lifecycle + in-range target is eligible");
}

void EachConditionRefuses() {
  {
    auto e = Eligible();
    e.prompt_visible = false;
    check(!SelectTargetEligible(e), "a hidden prompt never interacts");
  }
  {
    auto e = Eligible();
    e.line_of_sight_clear = false;
    check(!SelectTargetEligible(e), "an occluded target never interacts");
  }
  {
    auto e = Eligible();
    e.target_valid = false;
    check(!SelectTargetEligible(e), "a reused/dead/loading target never interacts");
  }
  {
    auto e = Eligible();
    e.dist_to_target_m = 2.5f;
    check(!SelectTargetEligible(e), "a target past class range never interacts");
  }
  {
    auto e = Eligible();
    e.dist_to_target_m = 2.0f;
    check(SelectTargetEligible(e), "a target exactly at class range stays eligible");
  }
}

void NumbersFailClosed() {
  {
    InteractionTargetEligibility e;  // all-default: nothing known
    check(!SelectTargetEligible(e), "an unknown target never interacts");
  }
  {
    auto e = Eligible();
    e.dist_to_target_m = std::nanf("");
    check(!SelectTargetEligible(e), "a non-finite distance never interacts");
  }
  {
    auto e = Eligible();
    e.dist_to_target_m = -0.5f;
    check(!SelectTargetEligible(e), "a negative distance never interacts");
  }
  {
    auto e = Eligible();
    e.max_reach_m = 0.0f;
    check(!SelectTargetEligible(e), "a zero class range never interacts");
  }
}

void Deterministic() {
  const auto a = Eligible();
  const auto b = Eligible();
  check(SelectTargetEligible(a) == SelectTargetEligible(b),
        "the same eligibility always gives the same answer");
}

void UnknownClassRefuses() {
  check(!SelectTargetEligibleForClass(Eligible(), dsvr::InteractionClass::Unknown),
        "an unknown interaction class never interacts");
}

void LeverDefaultRangeRefuses() {
  // H1 has not cited a lever range yet: the table max is 0.0 and the shared
  // gate refuses. When research cites a value, this test pins the new number.
  check(!SelectTargetEligibleForClass(Eligible(), dsvr::InteractionClass::Lever),
        "lever with unverified range never interacts");
  for (const auto& row : dsvr::kInteractionClassRanges) {
    check(row.provenance != nullptr && row.provenance[0] != '\0',
          "every class range carries its provenance");
    if (row.cls == dsvr::InteractionClass::Lever)
      check(row.max_reach_m == 0.0f, "lever range stays 0.0 until H1 cites it");
  }
}

void DeterministicPerClass() {
  const auto a = Eligible();
  check(SelectTargetEligibleForClass(a, dsvr::InteractionClass::Lever) ==
            SelectTargetEligibleForClass(a, dsvr::InteractionClass::Lever),
        "the same per-class eligibility always gives the same answer");
}

dsvr::InteractionSelection BoundSelection() {
  dsvr::InteractionSelection s;
  s.row_handle = 111u;
  s.object_handle = 0x21d4c9beu;
  s.generation = 1u;
  s.dummy_valid = true;
  s.dummy_pos[0] = 0.5f;
  s.dummy_pos[1] = 0.0f;
  s.dummy_pos[2] = 1.0f;
  return s;
}

void UnboundSelectionRefuses() {
  check(!dsvr::SelectionBound(dsvr::InteractionSelection()),
        "an unbound selection never counts as selected");
  check(!dsvr::SelectTargetEligibleWithSelection(
            Eligible(), dsvr::InteractionSelection()),
        "an eligible gesture with no selected row never interacts");
}

void SelectionRequiresIdentity() {
  {
    auto s = BoundSelection();
    s.row_handle = 0u;
    check(!dsvr::SelectionBound(s),
          "a selection with no row handle never counts as selected");
  }
  {
    auto s = BoundSelection();
    s.object_handle = 0u;
    check(!dsvr::SelectionBound(s),
          "a selection with no object handle never counts as selected");
  }
  {
    auto s = BoundSelection();
    s.generation = 0u;
    check(!dsvr::SelectionBound(s),
          "a selection with no object generation never counts as selected");
  }
  {
    auto s = BoundSelection();
    s.dummy_valid = false;
    check(!dsvr::SelectionBound(s),
          "a selection with no dummy position never counts as selected");
  }
  {
    auto s = BoundSelection();
    s.dummy_pos[0] = std::nanf("");
    check(!dsvr::SelectionBound(s),
          "a selection with a non-finite dummy never counts as selected");
  }
}

void BoundSelectionPassesWhenEligible() {
  check(dsvr::SelectTargetEligibleWithSelection(Eligible(), BoundSelection()),
        "prompt + LOS + valid lifecycle + bound selection + in-range target is eligible");
}

void BoundSelectionStillNeedsEligibility() {
  auto e = Eligible();
  e.prompt_visible = false;
  check(!dsvr::SelectTargetEligibleWithSelection(e, BoundSelection()),
        "a bound selection with no prompt never interacts");
  check(!dsvr::SelectTargetEligibleWithSelection(
            Eligible(), dsvr::InteractionSelection()),
        "eligibility with no selection never interacts");
}

void UnknownClassStillRefusesWithSelection() {
  check(!dsvr::SelectTargetEligibleForClassWithSelection(
            Eligible(), dsvr::InteractionClass::Unknown, BoundSelection()),
        "an unknown interaction class never interacts even when selected");
}

void StaleSelectionRefuses() {
  dsvr::InteractionSelectionStore store;
  auto s = BoundSelection();
  store.Publish(s, 1000LL);
  check(store.Bound(1000LL + dsvr::kInteractionSelectionFreshMs - 1),
        "a fresh selection stays bound inside its window");
  check(!store.Bound(1000LL + dsvr::kInteractionSelectionFreshMs),
        "a selection at exactly its age bound is stale and refuses");
  check(!dsvr::SelectTargetEligibleWithStoredSelection(
            Eligible(), store, 1000LL + dsvr::kInteractionSelectionFreshMs),
        "an eligible gesture with a stale selection never interacts");
}

void RepublishRearms() {
  dsvr::InteractionSelectionStore store;
  store.Publish(BoundSelection(), 1000LL);
  check(!store.Bound(1000LL + dsvr::kInteractionSelectionFreshMs),
        "a selection past its age bound refuses before republish");
  auto s = BoundSelection();
  s.row_handle = 110u;
  store.Publish(s, 1000LL + dsvr::kInteractionSelectionFreshMs);
  check(store.Bound(1000LL + dsvr::kInteractionSelectionFreshMs),
        "a republished selection re-arms the gate");
  dsvr::InteractionSelection out{};
  check(store.Latest(out) && out.row_handle == 110u,
        "the store returns the latest published selection");
}

void InvalidateRefuses() {
  dsvr::InteractionSelectionStore store;
  store.Publish(BoundSelection(), 1000LL);
  check(store.Bound(1200LL), "a published selection binds inside its window");
  store.Invalidate();
  check(!store.Bound(1200LL),
        "an invalidated selection refuses even inside its window");
  check(!dsvr::SelectTargetEligibleWithStoredSelection(Eligible(), store, 1200LL),
        "an eligible gesture with an invalidated selection never interacts");
}

void ClockWarpRefuses() {
  dsvr::InteractionSelectionStore store;
  store.Publish(BoundSelection(), 2000LL);
  check(!store.Bound(1000LL),
        "a clock step behind the publish time refuses fail-closed");
}

}  // namespace

int main() {
  FullyEligibleFires();
  EachConditionRefuses();
  NumbersFailClosed();
  Deterministic();
  UnknownClassRefuses();
  LeverDefaultRangeRefuses();
  DeterministicPerClass();
  UnboundSelectionRefuses();
  SelectionRequiresIdentity();
  BoundSelectionPassesWhenEligible();
  BoundSelectionStillNeedsEligibility();
  UnknownClassStillRefusesWithSelection();
  StaleSelectionRefuses();
  RepublishRearms();
  InvalidateRefuses();
  ClockWarpRefuses();
  if (g_fail) {
    std::printf("%d interaction_target test(s) failed\n", g_fail);
    return 1;
  }
  std::printf("interaction_target tests passed\n");
  return 0;
}
