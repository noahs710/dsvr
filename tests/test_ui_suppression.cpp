#include "../src/core/ui_suppression.h"

#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  using namespace dsvr;
  // Unqualified binding: a hide request is refused, UI stays visible.
  UiSuppression s;
  CHECK(!s.Update(true, UiPresentation::Immersive, false));
  CHECK(!s.hidden && s.refused);
  CHECK(s.MenuReachable());
  // Qualified: hide applies only in immersive VR.
  UiSuppression q;
  CHECK(q.Update(true, UiPresentation::Immersive, true));
  CHECK(q.hidden && !q.refused);
  // Theater always reveals the native UI again.
  CHECK(!q.Update(true, UiPresentation::Theater, true));
  CHECK(!q.hidden);
  CHECK(q.MenuReachable());
  // Back to immersive re-hides; turning the request off shows UI.
  CHECK(q.Update(true, UiPresentation::Immersive, true));
  CHECK(!q.Update(false, UiPresentation::Immersive, true));
  CHECK(!q.hidden && !q.refused);
  // Losing the binding mid-hide restores visibility and reports refusal.
  UiSuppression lost;
  CHECK(lost.Update(true, UiPresentation::Immersive, true));
  CHECK(!lost.Update(true, UiPresentation::Immersive, false));
  CHECK(!lost.hidden && lost.refused);
  std::puts("PASS ui suppression state machine");
  return 0;
}
