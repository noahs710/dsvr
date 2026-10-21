#pragma once

#include "lab_scenario.h"

namespace dsvr {

// Process-local diagnostic commands, not a socket, arbitrary input or memory API.
// A normal build is disabled. Trusted offline/save/scene adapters must provide
// evidence before an opt-in build can arm; commands cannot set readiness flags.
void LabInitialize(bool pinned_executable);
bool LabRequestStart();
void LabRequestAbort();
bool LabWantsObservation();
void LabObserveGame(LabObservation observation);
bool LabPollPad(const VirtualPadState& physical, const VirtualPadState& motion,
                VirtualPadState& out);
// Diagnostic writer thread only: automatic capture start/stop and status reports.
void LabPump();
LabResult LabStatus();

}  // namespace dsvr
