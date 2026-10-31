# Combat capture: detached execution and explicit trace closure

Direction selected by the user on 26 October 2026; implementation is pending
review of this spec. This is a focused improvement to the protected observer
workflow, not a new combat acceptance gate or permission to run another game
session.

## Observed failure and scope

- `tools/run_combat_capture.ps1 -Mode Run` blocks for the bounded session and
  can outlive an OpenCode foreground tool-call timeout.
- `RestoreTransaction` currently calls `Stop-Process` on tracked game
  generations before copying their traces. Forced termination cannot let the
  diagnostics writer append its normal footer.
- The core starts `DiagnosticsStart`, but has no production caller of
  `DiagnosticsStop`. The writer does append and flush a footer when a capture
  reaches its duration or receives `VrTraceRequestStop`; abrupt process exit
  before either event can leave a footerless trace.
- A footerless trace is incomplete evidence. Never synthesize or append a
  footer after the process has exited.

## Design

1. **Detached runner.** Add a `Launch` mode that starts the existing `Run`
   transaction as an independent PowerShell 7 process. Pass arguments without
   shell-string interpolation. For receipt `R`, capture stdout/stderr at
   `R.stdout.log` and `R.stderr.log`, and atomically publish `R.launch.json`
   with the child PID and log paths. Refuse collisions; do not replace prior
   receipts or logs. Keep synchronous `Run` available for terminal use. A
   separate `Finish` mode creates `R.finish` (the existing finish marker); it
   must not stop the game process.
2. **Explicit trace-stop signal.** The game-side diagnostics thread polls the
   process-specific path `DSVR-trace-stop-<pid>.request` in the game directory.
   The runner creates it only for a still-live, tracked game PID, after the
   existing session wait ends because of `Finish` or the session deadline. The
   diagnostics thread consumes the request and calls
   `VrTraceRequestStop`; the existing writer drains admitted records, writes
   the footer, flushes, closes, and publishes its terminal state. The signal
   must not run from DllMain or a native/gameplay hook.
3. **Graceful close and restoration.** After requesting closure, the runner
   polls the trace for a parseable footer and reports its `interrupted`,
   `io_error`, and drop status without upgrading a lossy trace to usable
   evidence. It tells the operator to close the game normally. Remove all
   forced `Stop-Process` cleanup from the transaction path. If any game
   generation remains live at the bounded close wait, mark recovery blocked,
   retain the receipt/staging, and require a later `-Mode Restore` after normal
   process exit. Restore refuses to touch roots or installed files while any
   game is live. If the game exits before consuming the stop signal, record the
   trace as unclosed and preserve that limitation.
4. **Evidence correction.** Update `MODLOG.md` and
   `docs/NATIVE_MELEE_CONTRACT.md` with the shipped-byte trace of both selector
   outcomes. The equality arm returns through the original `14031D91F` call
   continuation (`14031D926 -> 1421497F6 -> 14031D939`); the non-equality arm
   reaches `142FFAE49` and transfers through `[[RCX]+0x480]`. Their final
   destinations are not proven identical. Mark the old “both outcomes invoke
   +0x480” and `1403589A1` descriptions as superseded; do not assign runtime
   semantics to the indirect slot without runtime evidence.

## Failure handling and safety

- Detached launch failure, missing logs, malformed receipt, stale stop marker,
  wrong PID, or a second live game must fail closed before restoration.
- The stop marker is scoped to the process ID in the trace header and is
  consumed once. Cleanup may remove only markers created for this transaction,
  and only after the associated process has exited.
- A timeout is not successful completion. If footer closure or normal process
  exit does not happen within the declared bounds, preserve the raw trace,
  receipt, backups, and disposable roots; never kill an unowned process or
  restore files beneath a live game.
- The stop signal closes observation only; it performs no gameplay writes and
  does not assert combat outcomes or capture completeness.

## Offline verification

- Unit-test signal consumption, capture-generation isolation, queue drain, and
  footer contents through the existing `vr_trace` capture tests.
- Extend the runner `SelfTest` for detached argument/log handling, finish
  signaling, footer-wait outcomes, graceful-close refusal, and idempotent
  restoration; fixtures must not launch DSR, Steam, or modify real saves.
- Build before tests; run focused Debug and Release tests, then the protected
  full Debug/Release suites. Check for a clean diff boundary and preserve any
  unrelated workspace changes.
- This spec and its offline tests do not qualify the next native capture. The
  user/operator remains responsible for gameplay and normal game closure; a
  later live run must still pass the current protected preflight.
