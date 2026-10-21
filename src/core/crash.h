#pragma once

namespace dsvr {

// Installs an unhandled-exception filter that writes a minidump next to the
// game exe (DSVR-crash-<pid>-<time>.dmp) and flushes the log. Never throws.
void CrashHandlerInstall();

}  // namespace dsvr
