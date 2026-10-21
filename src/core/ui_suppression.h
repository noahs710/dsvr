#pragma once

namespace dsvr {

enum class UiPresentation { Immersive, Theater };

// Hide/show policy for native UI (T2). The native binding is not qualified
// yet (see build/verification/ui-qualification-20261016.md), so production
// always passes binding_qualified=false and the UI stays visible. Theater
// reveals UI in every state; the DSVR menu is a separate surface and stays
// reachable whether or not native UI is hidden.
struct UiSuppression {
  bool hidden = false;   // native UI currently hidden by this policy
  bool refused = false;  // last request could not be honoured

  bool Update(bool hide_requested, UiPresentation presentation,
              bool binding_qualified) {
    refused = hide_requested && !binding_qualified;
    hidden = hide_requested && binding_qualified &&
             presentation == UiPresentation::Immersive;
    return hidden;
  }
  bool MenuReachable() const { return true; }
};

}  // namespace dsvr
