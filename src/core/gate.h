#pragma once

namespace dsvr {

// Returns true when the running exe is the exact pinned build.
// Logs every check (pass or fail) so a refusal is diagnosable.
bool GateCheckPinnedBuild();

}  // namespace dsvr
