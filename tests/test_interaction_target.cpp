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

}  // namespace

int main() {
  FullyEligibleFires();
  EachConditionRefuses();
  NumbersFailClosed();
  Deterministic();
  UnknownClassRefuses();
  LeverDefaultRangeRefuses();
  DeterministicPerClass();
  if (g_fail) {
    std::printf("%d interaction_target test(s) failed\n", g_fail);
    return 1;
  }
  std::printf("interaction_target tests passed\n");
  return 0;
}
