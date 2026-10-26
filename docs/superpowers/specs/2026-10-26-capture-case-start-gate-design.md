# Capture case-start gate

## Context

The protected observer now starts one 600,000 ms combat trace after the local
player appears. The runner stabilizes the game process before `RunCasePlan`, but
does not currently wait for that trace. Consequently, a case label can begin
before the trace header, leaving its interval uncovered. Existing coverage
reporting rejects that interval after the fact, but the operator may spend a
bounded session on a case that cannot provide evidence.

The current source and binaries have a fresh offline candidate at
`build/verification/combat-observer-lifecycle-20261026/candidate.json`; its
read-only preflight passed. This design does not qualify gameplay. The user
selected the header-gate approach and approved the bounds on 26 October 2026.

## Design

1. **Wait for the actual capture.** After one exact game generation settles and
   before any case timer or prompt starts, poll for a new trace file not listed
   in `traces_before`. Accept only a parseable schema-2 header whose PID matches
   the currently tracked game generation, `capture_domain` is `combat`,
   `combat_hook` is true, and `duration_ms` is the expected 600,000 ms. Continue
   to use the process path and start-time identity checks; a PID match alone is
   not ownership.
2. **Bound the wait and fail closed.** Wait for at most 300 seconds, and never
   past the transaction deadline. An explicit finish request or deadline also
   ends the wait. If no matching header appears, do not start any case window.
   Report the missing capture start and let the existing completion path request
   closure if a trace appeared during the race, wait for a footer, require
   normal game exit, and refuse restoration beneath a live process. Do not kill
   the game or synthesize evidence.
3. **Keep labels inside the one-shot recording.** Reject a declared case plan
   whose durations sum to more than 540 seconds. Apply the same validation in
   read-only `Check` and execution modes. The 60-second reserve within the
   600-second trace accommodates case-file writes and capture-close margin; it
   does not relax per-case full-coverage checks. A missing, malformed, lossy,
   or footerless trace remains incomplete evidence.
4. **Preserve existing boundaries.** The gate only coordinates the observer
   runner and trace header. It adds no native calls, gameplay writes, input
   injection, damage/defense logic, or interaction behavior. Detached `Launch`,
   synchronous `Run`, `Finish`, footer handling, receipt journaling, and
   restore refusal keep their existing contracts.

## Offline verification

Extend the existing runner `SelfTest` with synthetic files and process fixtures:

- A valid new header for the tracked PID/domain/hook/duration releases the case
  plan only after the header is visible.
- An old trace, wrong PID/generation, malformed header, wrong domain, disabled
  hook, or unexpected duration never releases the case plan.
- Timeout, finish request, and transaction-deadline paths start no cases and
  preserve safe completion behavior.
- A plan totaling 540 seconds passes; 541 seconds fails in both `Check` and
  `Run` validation.
- The tests do not launch DSR or Steam and do not touch real saves or install
  files.

Then run the focused runner transaction test and the protected full Debug and
Release build/CTest commands. A green offline result only establishes that case
timing is gated on the expected observer header; it is not native gameplay
acceptance or authorization for a live capture.
