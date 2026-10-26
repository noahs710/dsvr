# DSVR current handoff

### Test-loop efficiency pass, 26 October 2026

Full suite now ~3 s per config (was ~27 s): `/MP` added to root MSVC flags
(TU-level compile parallelism; no PCH/Gm/import conflicts), `ctest --parallel`
(bare flag = all 16 cores; every temp-output test verified PID-unique —
`test_config.cpp` fixed from shared `DSVR-test-config.toml` to PID-suffixed),
package.py AV-lock retry sleep 2 s -> 0.25 s (saves ~10 s in the `package`
lock-contention case), `auto_verify.ps1` uses bare `--parallel` for both
build and ctest. Verified: Release + Debug builds exit 0, both suites 100/100
in ~3.0 s. Commands updated in AGENTS.md / TESTING.md / execution-guide
section 6 / auto_verify.ps1. One transient note: the first Release rebuild
after the test_config edit exited 1 on a stale locked artifact; clean rebuild
exit 0, no code issue. No game launch/install/headset; offline only.

Current checkpoint (26 Oct): twenty-one offline slices landed and verified —
AtkParam 0x73 named (kMapHitType/NativeMapHitType, cache transcription only),
launcher theme test registered (setup_theme), untargeted reach refused by
default (interaction eligibility gate + per-class range table with Lever at
0.0/unverified), calibration floor note profile-aware (first launch says
setup height until capture succeeds; hold-reset rule named) with shipped-WARP
render coverage in both modes, restart-pending footer indicator
(applied-vs-attached binding comparison via frozen attach-time signature;
WARP asserts present after Apply, absent for live-only changes), launcher
install-gate hardening (Install disabled on failed Verify in UpdateBrief +
CTL_INSTALL refuse; DoInstall:522 untouched; fail/pass Verify test), and
read-only B/D static writeups (14032F548 verdict + first-half probe;
D-probe second window with RDI/R14 producers and renderer+10 store pinned,
plus re-read addendum with ctor identity and outer caller seed, plus third
window with Begin-payload borrow-appearing verdict and derived slot set,
plus fourth window with queue/teardown shape and bracket scout), E locomotion
owner table (docs-only, six UNKNOWN owners), E2 XInput poller probe (6 call sites, 2 per-frame candidates, mapper still open), F2 per-variant coverage matrix
(docs-only, one CAPTURED row), and H1 lever-range cache search (no citable
value: validDist 150 default conflicts cm-vs-metres, lever rows name-only;
0.0 retained with search paths documented), B type-64 static probe
(negative-only: 64 excluded from durability chain, zero ==0x40 consumers),
and launcher chain-conflict messages carrying the full colliding path
(message text only; test asserts stub mirror — production text reviewed in
diff, suites green both configs), E2 follow-up (consumers unpinned,
slots decoded as self-RVA pairs not vftables, candidate #3 unproven; sibling
walk: trigger/event siblings only, no float reader, VA decode corrected; byte-pattern retry: axis getters pinned (idx5 reads by index, idx7 mirror setters, idx6 button bits), scaling consumer behind zero-xref computed dispatch), and
package mid-refresh atomicity (comment corrected + mid-loop failure test,
old zip/verification pinned), and B swing-producer xref probe (no swing
caller in either xref set; builder has zero code xrefs; ObjDam wrapper
excluded), and B record-stride decode (1417991F0/224 are PE unwind metadata,
not dispatch records; exactly 3 DATA refs; no code owner; correction supersedes
the earlier table gloss).
Full Debug 101/101, Release 101/101, runner SelfTest both engines PASS, doctor PASS, dev
package refreshed (DSVR.dll 3a136a39…, built 2026-10-26T01:25:42Z). Stale
hardcoded hash prefixes removed from the runbook; verify_preflight compares
live full hashes against the verification JSON. B-probe 14032F548 verdict:
instruction candidate only (byte-pinned CALL [RAX+0x640] in .text1 split gap,
zero xrefs, receiver unrecoverable; not a damage classification — see
contract). +0x640 slot comparison closed 26 Oct: EnemyIns slot holds the
negative-HP path, PlayerIns slot a self-stub, neither links to 548.
C/D/E/F native gates unchanged and open; no game launch, install,
or headset run. Inherited controller/weight work preserved.
[Authority](README.md#authority-and-maintenance). Resume
solely from the [execution guide](docs/superpowers/plans/2026-10-21-native-conversion-execution.md);
this handoff records evidence/state, not a competing task order.

### B slot-inhabitant comparison closed (second half, first part), 26 Oct 2026

Read-only GhidraMCP: EnemyIns vftable +0x640 slot holds 0x14034BF70 (negative-HP min path), PlayerIns same slot holds the 140339600 self-stub; each DATA-xrefed from its own table only, zero xrefs to 14032F548. Closes "is +0x640 a damage slot" (class-divergent: negative-HP path vs no-op) without typing 548's receiver. Remaining open half is live hit/miss correlation under TESTING; never invoke 548. Contract holds bytes/arithmetic. No build/test/launch/commit.

### Read-only 548-verdict verification pass, 26 Oct 2026

Read-only GhidraMCP: EnemyIns vftable +0x640 slot holds 0x14034BF70 (negative-HP min path), PlayerIns same slot holds the 140339600 self-stub; each DATA-xrefed from its own table only, zero xrefs to 14032F548. Closes "is +0x640 a damage slot" (class-divergent: negative-HP path vs no-op) without typing 548's receiver. Remaining open half is live hit/miss correlation under TESTING; never invoke 548. Contract holds bytes/arithmetic. No build/test/launch/commit.

Re-checked the dirty-tree 14032F548 verdict subsection against the live GUI (PID 17320, `DSRFull`, `/DarkSoulsRemastered.exe`): re-ran the tasked first half, `get_assembly_context` 12 each side at 142756FC5 / 1421B6015 / 14032F471 — all three match the recorded outcomes (CMOVNZ return-selector toward 14032F584/594; CMOVZ immediate 14032F54E; JMP-island before 14032F480). No contract edit needed: verdict, bytes/xrefs/split-gap/.text1/forged-JMP/forward-JMP+TEST-ESI/+0x638/+0x640-inhabitant evidence, limitations and exact next probe are all present, plus the +0x73 linkage with the 1403C72D0 read-verification caveat. Cache citations spot-checked (raw wiki line 55 mapHitType 0x73, ATK_PARAM_MAP_HIT 0..3; Paramdex DS1R Defs AtkParam.xml u8 mapHitType). Read-only; no repair/save/launch/commit.

### Launcher install-gate hardening (Option A), 26 Oct 2026

RED: rebuilt test_setup_theme, `ctest -C Debug -R setup_theme` exit 1 (FAIL line 117, Install button enabled after failed Verify). GREEN: `g_install` HWND stored at create plus reset in RunGui; UpdateBrief gates Install on path resolved plus verified exe match plus verified-ok; CTL_INSTALL refuses without verified build (DoInstall:522 untouched defense-in-depth). Test stubs VerifyExe fail-once plus DoInstall counter; fail-Verify disables Install and blocks DoInstall, pass-Verify re-enables and installs. Full `cmake --build`/ctest Debug plus Release 101/101. Files: src/setup/gui.cpp, tests/test_setup_theme.cpp. No game launch/install/headset; offline only. No commit.

## Current task and candidate

### Physical attack/block/parry continuation, 24 October 2026

User priority: finish as much physical attack, blocking and parry as possible,
using live Ghidra and the Soulsmodding DS1R reference. Baseline HEAD
`cf864aa6e9d37dd595d109125f3c248dfab9595a` plus valuable mixed unfinished
work. No reset, blanket staging or delegation. Source graph tools are not
exposed in this session; existing stdio GhidraMCP bridge reaches the primary
live `DSRFull` GUI. No headless wrapper, DB repair or Ghidra script was used.

Task-owned corrections in the existing contact/AtkParam headers and tests:
three-pass capsule distance missed shallow crossings; tiny positive scales
could round to zero correction; invalid/future raise timestamps manufactured
defense classifications; and the candidate stored an animation ID as though
it were a selected attack row. The red command was freshly rebuilt
`ctest --test-dir build -C Debug -R 'combat_atkparam|combat_contact'
--output-on-failure`, exit 1 (`contact-red.log`). Corrected all four, added
motion/identity/refusal checks, and left refused candidates empty. The candidate
now carries separate animation, behavior and selected AtkParam IDs and a raw
table selector; type-64 parry contexts cannot become damaging blade contacts.
Source attack slots are not victim hurt geometry or obstruction proof.

Static advance: native builder -> event -> contact selected-field/flag/handle
copies recovered and independently byte-checked. PlayerIns selector is 1;
EnemyIns is 0. Type-64 payload +30 comes through a weapon +F2 byte, not the
separate +C2 parry lifetime field. Primary revision stays
`1792646701740:124420c4:3`, unsaved changes false. Static receipt/reproducer:
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\physical-combat-20261024\verify_context.py`.
4,626 bytes PASS; report SHA-256
`8151c7b52092d469d1b07948ad15cca5901e848eb7a349665ff1a66b955decda`.
Details and remaining native consumer probes: `docs/NATIVE_MELEE_CONTRACT.md`.

`native_combat_context` joins each LAB batch as its fifth atomic record.
Tuple, payload flags and event handle have independent read bits; IDs use
lossless limbs. Header `combat_context_schema=1` requires complete matching
contexts; legacy captures remain readable. Duplicate/orphan/mismatched-return
records refuse. Queue pressure drops all five together. The actual detour ->
writer -> analyzer fixture verifies pre-dispatch values despite native mutation.
Worst callback permission-query budget is now 18; real hook cost is measured
separately and is not 90 Hz qualification.

Offline verification: Debug/Release builds exit 0; full fresh suites **94/94**
each, `ctest -N` retained. `pwsh -File tools/auto_verify.ps1` exits 0, doctor
PASS, development ZIP refreshed. Separate `build/combat-lab` Release core
rebuilt (tests remain owned by `build`). Logs:
`build/verification/physical-combat-20261024/` (`final-debug-ctest.log`,
`auto-verify.log`, `final-lab-build.log`, `context-final-tests.log`).
Package regular DLL SHA-256:
`34a5e75e537d926a81e7f509694da41cb3bf94e64b8e4e45e1f03c9bd542b3da`;
LAB DLL `75d2ac2702eb2a7d7f1639da7241957a9f8a2f47eac01fde28bbb9e7b6ced570`;
ZIP `87060c06ac64cb6355e93fe907c1abb1d58d391bfdaf697caf5302688a62161f`.
ZIP/regular DLL/verification JSON hashes agree. Fresh source/binary receipt
`build/verification/physical-combat-20261024/candidate-final.json`, SHA-256
`4b3f0ab6f8febb9ba470add45ad48bd050de31baf4b4f4f900b5250d627d8b02`:
all 187 source entries and four binaries independently rechecked.

Native transaction: first Check lacked required -Receipt and exited 1; corrected
Check stopped at Steam Offline preference. The user selected **Ready for the
protected capture**, switched Offline, and the exact-candidate Check passed.
Protected runner launched with retained test-progress seed vault
`C:\Users\Gabrielle Monlea\.dsvr-backups\combat-observer-20261024-150514`,
not the shieldless tutorial seed. Original roots/install bytes are snapshotted
and held by the new receipt; current vault ends `20261024-210607`. Absolute
cutoff **21:36:07 Eastern**, including setup. Operator alone controls gameplay;
XR, physical input injection and native gameplay mutation remain disabled.

First completed capture: operator confirms one ordinary sword hit. External
two-read equipment snapshot confirms left1401000/right500000, raw stance1.
`DSVR-trace-11228-125204171-1.jsonl` has 160 complete five-record samples,
zero drops/refusals, no detail ceiling, 1,449 excluded non-player contacts.
Both integrity checks PASS. Hit seq26: stable PlayerIns -> EnemyIns, target
HP75 ->22, source594 unchanged; behavior102900000 / AtkParam2900000 /
table1 / geometry selector-1 / weapon500000 / event handle131074, raw payload
flags0/0. All 160 motion records have unit direction; speeds stay native units
per second because this observer-only run has no applied XR world scale.
This qualifies the observed fields on the natural path, not physical submission
or the full damage ABI. Remaining cases and restoration are recorded below
when the protected transaction ends. Do not infer Restored from the older runs.

### Motion offsets confirmed in the builder's own decompile; native and measured speed put in one unit, 25 October 2026

Trace work only: still no game-memory writes and no damage claim. `ContactDetour`
emits a fourth record per detailed contact batch, `native_combat_motion`, carrying
the caller-built record's own fields - a normalized direction at `+0xF0` and a
`(current-previous)*60` velocity at `+0x150`. Those offsets are no longer the
neighbour-of-a-proven-offset guess they were: the pinned program's own decompile of
the builder `FUN_1403c7520` (`1403c7520`-`1403c7871`, revision
`1792646701740:124420c4:3`, unsaved changes false) initializes the record at
`RSP+0x20`, stores the source handle at `+0x130` and the target object at `+0x138`
(the two this hook already proves at runtime), writes the normalized direction -
**w forced to zero**, components zeroed or infinite when the native source delta was
degenerate - at `+0xF0`, and writes `(current-previous)*60.0` at `+0x150`. Two
consequences. A non-unit direction is now a question, not a refutation, so the
unit-length bit stays reported rather than assumed. And that velocity is **native
world units per second under the game's own fixed 60 Hz assumption**, not metres per
second, while our tip speed is metres per second. The analyzer now divides the
native magnitude by the world scale actually applied (`combat_bench_provisional`
values[13]), keeps the raw count as `native_speed_units_per_s`, and when no scale is
present leaves the comparison empty and counts it in
`native_speed_without_world_scale` instead of assuming a scale. Static layout
evidence is still not a runtime observation. `docs/NATIVE_MELEE_CONTRACT.md` records
the same finding and warns that `+0xF0`/`+0xF4` are also key fields of
`1403C7B10`'s **event** object - same offsets, different structure, never merge them.
Nothing is trusted: the reads happen **before** the
native call (the swing state that produced this contact), each read sets its own
bit, the direction's length is reported in `values[8]` with flag 8 rather than
assumed unit, and a caller that passes a smaller record just fails the guarded
read. The batch always emits the record, so its presence - not a
`combat_detail_schema` bump - marks the detail shape; the schema stays 1.

Hook cost is now guarded at 15 permission queries for the worst single callback
(was 14): no `GuardedReadScope` is open across the detour, so every guarded read
is its own permission query, and motion adds two. `test_combat_trace` prints the
count and asserts the budget. The fixture leaves both fields zeroed, so it proves
the read-ok and finite bits set, the unit-length bit stays clear, and the record
stays bound to the same callback through `native_rate`.

Analyzer: motion pairs with its contact by `(thread, seq, object)`. A motion
record bound to a different native return, a second motion record for one
callback, or an orphan motion record invalidates the capture instead of being
ignored. Per case: `native_motion_samples`, `native_unit_direction_samples`,
`native_speed_units_per_s_values` and `native_speed_mps_values`; per sample:
`native_motion`. New `case_attribution` joins each tracked blade swing to the nearest
native callback inside a 150 ms wall-clock window and reports
`bench_minus_native_speed_mps` - the first place our measured tip speed and the
native record's own speed for the same swing appear in the same unit. That window is
a correlation radius, never a causal claim. `python tests\test_vr_trace.py` covers
the pairing rules, the bit gating, the disagreement cases, and the unit conversion:
a fixture native speed of 4.5 units/s with an applied world scale of 0.9 must read
as 5.0 m/s and diff -2.0 against a 3.0 m/s tip speed, and the same native speed with
world scale zero must stay a raw unit count with an empty comparison.

Verification (all exit 0): Debug and Release builds of `build` plus the
`build\combat-lab` Release core (the DLL `tools/run_combat_capture.ps1` deploys; that
cache configures no tests, so the `build` dir still owns them), then
`ctest --test-dir build -C Debug --output-on-failure` **94/94** and
`ctest --test-dir build -C Release --output-on-failure` **94/94** (`ctest -N`
lists 94 registered names), plus the combat-only subset
`-R "combat_native_trace_filtered|combat_capture_transaction|diagnostics_combat|vr_trace_capture|vr_trace"`
**6/6**. Logs and `ctest -N` output are kept in
`build/verification/native-combat-only-20261025/`. The LAB DLL was confirmed to
carry all four record kinds (`native_combat_contact`/`_identity`/`_motion`/`_diagnostics`
strings present) and the non-LAB core to carry none of the motion kind; object mtimes
were checked against `combat_math.h`/`vr_trace.h` first, and every translation unit
that includes `combat_math.h` was rebuilt. Non-LAB core deployed to the game
directory and `build\src\setup\Release\DSVR-Setup.exe --doctor` exits 0 with no
stale-object AV.

Next operator experiment (the desktop run is the operator's). The receipt is already
pinned: `build/verification/native-combat-only-20261025/combat-only-candidate-233145.json`
(SHA-256 `5fcc509a8b7ba7146c62119fb9b91b58fcf82ad08d829b42e716b8e4c3456c2c`), and a
hash-by-hash replay of it against the current tree matches all 187 source entries and
all 4 binaries. `-Mode Check` with that receipt reaches the operator-only gate and
stops there: **`Steam Offline preference is not set.`** (`run_combat_capture.ps1:388`,
which reads `WantsOfflineMode` for account 1843241039 in `config\loginusers.vdf`).
Everything before that gate passes - game closed, active account correct, Steam
running - and doctor confirms the firewall block and exact EXE hash. Setting Steam
Offline drops the operator's own session, so it stays a human action. Then:
`tools/run_combat_capture.ps1 -Mode Run -CandidateReceipt <that receipt> -Cases
hit:20,miss:15,block:12,parry:10` in a **new** transaction directory. No new analyzer
flag. Read `case_attribution`: if `native_speed_units_per_s` arrives on real swings
and the unit-length bit sets, the `+0xF0`/`+0x150` layout is qualified at runtime and
`bench_minus_native_speed_mps` becomes a real calibration signal for `CombatParams`.
If the bit never sets, check the degenerate-delta case above before calling the
layout wrong; if speed is zero or absent while a contact clearly happened, the motion
read goes back to static recovery - do not reinterpret the numbers to fit the model.

Still gated, unchanged: plan gates C1-C7 stay open. Nothing here reads or writes
game memory, selects a live AtkParam row, resolves dummy-polygon ids, or submits a
contact. `QualifyContact` in `src/core/combat_atkparam.h` still has no production
caller; it is deliberately unwired while the B contract is open. The next B probe
remains `14032F548` (`CALL [RAX+0x640]`) per `docs/NATIVE_MELEE_CONTRACT.md`.

### Soulsmodding research closed; native-shaped damage candidate built offline; contact ceiling made honest, 25 October 2026

Research finish (no new harvest): `python -B build/research/soulsmodding-20261024/verify_and_index.py`
exit 0, and `verification.json` now reads 199/199 wiki pages hash-matched (0 mismatch,
0 absent, 0 unlisted), 304 upstream files on disk / 0 absent across seven commit-pinned
repos, 6 workbooks / 27 tabs / 13360 rows. The one known hole is unchanged and already
recorded in `docs/SOULSMODDING_REFERENCE.md`: Smithbox's recursive tree exceeded the
collection size bound, so only its README is cached. Treat that as a documented gap, not
a harvest bug.

Physical-damage work this pass is **vocabulary and refusal logic, not a binding**. The
pinned AtkParam/attack-schema pages are now transcribed into
`src/core/combat_atkparam.h`: attack id
(`wepmotionCategory*10000 + hand 3000/4000/5000 + action`, `+5` at zero durability) with
`Encode`/`Decode` round-trip; the 0x80-byte row layout as `static_assert`-checked
offsets; hit slots as sphere-or-capsule with priority plus
`BladeOverlapsHitSlot`/`SegmentSegmentDistance` (deterministic, degenerate- and
NaN-safe); and damage expressed in the native domain (base u16 x u16 percent,
`NativeCorrectionFromScale` refuses 0 / non-finite / >655.35 instead of clamping,
because a 0-percent submit is a parameter edit rather than a missed hit).
`QualifyContact` is the new gate from a tracked `BladeHit` to a submittable candidate:
it refuses an unverified hand/action, any whiff/deflect/kick row, an unencodable scale,
and any touch the target row's own slot geometry vetoes; sphere-only fallback requires
an explicit caller opt-in and is then recorded as unproven geometry. `BladeHit` gained
`tip_speed_mps` so a candidate carries the measurement, not only the multiplier derived
from it. Registered in `src/core/CMakeLists.txt` and as `test_combat_atkparam`.

Trace repair: `g_contact_samples` was incremented **before** the 256-sample ceiling
check, so the footer's `combat_detail` count exceeded the declared ceiling and the
"every capture hit the 256-contact ceiling" reading above was partly an artifact of our
own counter. Refused samples no longer count. `test_combat_trace` now asserts the
actor-permission query budget (10) as well as the footer count.

Verification (all exit 0): Debug and Release builds, then
`ctest --test-dir build -C Debug --output-on-failure` **94/94** and
`ctest --test-dir build -C Release --output-on-failure` **94/94** (`ctest -N` lists 94
registered names; do not reuse an older total). `combat_atkparam` covers every encode
round-trip, every refusal path and slot-priority determinism.

Still gated, unchanged: plan gates C1-C7 stay unchecked because the C lane entry
requires the B contract, and B is open. Nothing here reads or writes game memory,
selects a live AtkParam row, resolves dummy-polygon ids to endpoints, or submits a
contact. The next B probe is still `14032F548` (`CALL [RAX+0x640]`) per
`docs/NATIVE_MELEE_CONTRACT.md`.

Operator experiment (the desktop run is the operator's): pin a fresh combat-only
candidate receipt with `build/verification/*/pin_combat_only.py`, then
`tools/run_combat_capture.ps1 -Mode Run -CandidateReceipt <receipt> -Cases
hit:20,miss:15,block:12,parry:10` in a **new** transaction directory. The analyzer
needs no new flag. Read `case_observations` per label and now trust
`contact_limit_reached`: with the counter fixed, hitting 256 means the game really
produced 256+ natural contact calls, so raise the ceiling deliberately rather than
assuming our bookkeeping ate the samples.

### Real-save comparisons finished; trace coverage needs repair before more trials

All four desktop transactions (PID20680,7332,26804,21832) are **Restored**.
The three real-save receipts were independently rechecked against all original
install/save hashes by
`build/verification/combat-session-30m-20261024/summarize_sessions.py` (exit0).
Original personal saves were never used as the running test outputs; copies
and test progress remain in the receipted vaults. Steam stayed disconnected.
The runner repeatedly hit a transient dinput8.dll sharing violation after
shutdown; existing guarded Mode Restore succeeded each time. Do not hide the
first exit1 or claim its cause/owner is proven.

The user-approved30-minute session (PID21832, hard cutoff15:35:14 local) was
finished deliberately after collecting cases, not allowed to time out. User's
latest correction to the **last** case: missed parry window, took one hit,
then successfully parried and riposted. Keep this as `failed_then_parry_riposte`,
not an isolated parry. Earlier positive report also included riposte; the
requested parry-only attempt failed. Native defense binding remains OPEN.

Critical coverage findings, not green tests: every completed real-character
capture hit the256-contact detail ceiling. Five completed PID21832 captures
are valid/zero-drop, but two are outside declared windows; there are **zero**
contact samples in the declared `clean_block` window. The last capture
`DSVR-trace-21832-104309390-6.jsonl` has59 triplets but **no closing footer**:
our stop happened while that late-started capture was still active. It is
INVALID, not successful final-parry evidence. Preserve it unchanged. Original
sidecars and hash-linked label corrections retain all timestamps; reports and
raw SHA256 identities are in each run's comparison-summary.json.

Useful independent native read: equipment IDs left/right1401000/500000 and
raw stance3(two-handed operator report) ->1(one-handed return) with the SAME
world/player/assembly/stance pointers and player handle in
`stance-two-hand-equipment.json` and `stance-one-hand-return-equipment.json`.
The earlier stance-before snapshot belongs to a different player instance:
do not call that first1->3 pair a same-generation transition. This corroborates
the current character's native equipment/stance read, not physical two-hand rules.

Bounded external raw-word sampler retained98 samples/0 refusals. Despite its
`raw-stats-block.json` filename, its trigger was capture4 in the **failed_parry**
window, NOT the requested clean block. Raw+3F8 varied67..91 while+3FC stayed91;
do not "fix" the existing+3F8 read to+3F4 based on speculation. Meanings/phase
still require native consumer evidence. No game calls, writes or input injection
were performed by the external readers (VM_READ|QUERY_LIMITED_INFORMATION only).

Next exact implementation/research unit before any additional human trial:
repair capture synchronization (confirm F9 capture opened BEFORE the action;
require a closing footer BEFORE signalling transaction.finish) and investigate
the global detail budget consumed by unrelated/unqualified contacts. Retain
explicit exclusion/ceiling/refusal counts; do not merely raise caps, erase
refusals or bless an HP setter. Measure observer cost separately. Then repeat
one labelled native defense case on a fresh pinned candidate. Native physical
combat and VR/IK qualification remain OPEN; VR hardware is still unavailable.
The Souls Modding reference is documented in MODDING_PLAN, with DS1/DSR and
data-schema-versus-runtime-ABI caveats. No commit/push.

### Operator-approved 30-minute retry: timing correction

The first parry retry also consumed its 15-minute window during human setup.
The remaining-time guard correctly refused to start a new case; no parry result
exists. Its transaction has been restored through the existing guarded Restore.
After the repeated interruption, user explicitly approved a bounded **30-minute**
session. Do not relaunch another 15-minute loop or pretend a completed test.

Smallest correction: retain default15, allow explicit WindowMinutes30, keep
finite max30 and all existing save/install/Offline/actor protections. RED:
`pwsh -NoProfile -File tools/run_combat_capture.ps1 -Mode SelfTest -WindowMinutes 30`
exit1, parameter validation rejected values above15 before any fixture/native
operation. Both registered PowerShell transaction tests now exercise30. No new
native ABI, game code or observation-domain change; re-pin sources after tests.
Check remaining time before instructions, announce the absolute hard stop, and
collect cases before analyzing. GREEN: rebuilt Debug/Release93/93, both engines'
SelfTest30 PASS, request31 refused before execution. Compiled-source and all four
binary hashes equal the prior native-observed candidate; only runner/test args
changed. Fresh source receipt:
`build/verification/combat-session-30m-20261024/candidate.json`, SHA256
`1755b27b791eb12300650e3f56bb6548390c1a7542ad272d842d9feee55c291d`.
No distribution refresh for this runner-only bound correction.

Next fresh30-minute run receipt:
`build/verification/native-combat-real-parry-20261024-30m/transaction.json`.
Use the preserved real-character seed. The prior three native transactions are
Restored; keep the DLL-sharing failure/retry receipts. This is still only a
desktop original-controls comparison, not native physical-combat acceptance.

### Real-character desktop defense/two-hand follow-up and parry retry

User explicitly requested the real save because it has a shield, and confirmed
the character belongs to the currently signed-in Steam account. Use a protected
copy, not the original: both restored personal save roots were copied and
SHA-256 checked into
`C:\Users\Gabrielle Monlea\.dsvr-backups\real-character-seed-20261024-141757\`
(`seed-manifest.json`, 5 Documents files / 0 AppData files). Originals unchanged.
The existing runner accepts this through `-SeedVault`; no save conversion,
cross-account move, online transition or source/binary modification is needed.
Preflight Check PASS against the same hash-pinned candidate.

Objective: original-controls shield block, parry attempt and two-handed hit
against an **active** attacker, with operator outcomes and measured case windows.
VR/IK stay off; these are native comparison cases, not enabled physical combat.
Receipt: `build/verification/native-combat-real-20261024-141757/transaction.json`.
Protected 15-minute run and exact-PID restore boundaries unchanged. Status:
first run finished; guarded Restore retry and independent hashes PASS.

First real-character run PID7332 reached its 15-minute deadline during the
two-hand setup question. The operator reported the game closed mid-test; this
was an agent coordination failure, not a two-hand result. Never count it.
Operator had reported chip block, then clarified a second capture was taking
hits and killing the undead; do not label both captures as pure blocking.
Both captures reached the detail ceiling; parry was not attempted (no enemy).
Equipment read-only snapshots (no game calls/writes) corroborated left/right
IDs1408000/500000 and raw stance1 around block; later left ID changed to1401000.
These are matching external reads, not atomic contact/update attribution.

User requested retry of parry/others. Fresh single-purpose protected run:
`build/verification/native-combat-real-parry-20261024-retry1/transaction.json`,
same preserved real-character seed and pinned observer binary. Parry first;
collect first, analyze after stopping. Check remaining deadline before issuing
each instruction; do not begin another case near cutoff or silently turn an
interrupted setup into a successful result. No deadline extension or source/
binary change. Both previous transactions are Restored; the original runner
again needed guarded retry after a transient dinput8.dll sharing violation.

User's Souls Modding wiki link was checked and expanded in MODDING_PLAN's
existing permanent references: useful AtkParam dummy-key/guard semantics and
attack/one-/two-hand animation vocabulary; many DSR index links target DS1.
It is a community data clue, never proof of a DSR native pointer layout/ABI.

### Desktop-only B1 comparison, 24 October 2026 (operator available; VR unavailable)

User confirmed game launch is available, VR is not. Current protected preflight
is PASS: pinned sources/core/loader, matching disposable Steam account, Offline/
disconnected client, exact-game outbound block and both original/seed save roots.
No C++ or runner changes since `ik-combat-20261024-134111/candidate-final.json`.
Task: compare natural original-controls contacts against separately recorded
hit/miss/block/parry/dodge outcomes; no physical-damage calls or VR/IK writes.
Expected evidence: combat-domain complete triplets, zero dropped records, native
Step coverage, per-case QPC windows plus operator loadout/outcome, measured cost.
Existing ~10 ms observer overhead is not fixed and no performance PASS is implied.

Run receipt: `build/verification/native-combat-desktop-20261024-135521/transaction.json`.
Use the existing observer-only reversible transaction, 15-minute hard deadline,
operator controls all input, exact tracked-PID stop/restoration in runner finally.
Launch without automatic `-Cases`: process creation is not gameplay readiness,
so loading/menu time must not be labelled hit/miss. Stamp manual case windows
only after operator readiness; F9 starts each bounded 20-second capture. On exit,
require receipt state Restored and original-file/save-root hash verification.
Result: PID 20680, owner thread 10628. Three combat-domain captures are `valid`,
zero drops/IO errors/contention and complete atomic triplets. Hit: 146 samples,
2,348 Step records; miss: 196 / 2,336; roll: 256 / 2,320. Hit seq 24 is stable
PlayerIns -> EnemyIns, AL=1, target HP **44 -> 31**, source HP 659 unchanged,
matching the operator's single sword hit. Clean miss has no observed HP changes.
Exact weapon/PARAM identity remains unknown: operator described a sword only.

Corrections matter: operator has **no shield**, and the enemy is a **passive
tutorial Hollow** that never attacked. The third capture is `roll_no_attack`,
not dodge/invulnerability proof; it also reached the 256-contact detail limit.
The original requested-case sidecar and a hash-linked label correction are
retained; timestamps were not changed. No block/parry case ran. B1 remains
partial and B/C remain OPEN, not qualified by two clean cases.

Observer read/assembly medians: 3.25015 / 3.02725 / 3.08175 ms; p99:
11.4865 / 9.2873 / 10.0099 ms. No code changed, no overhead fix/performance
acceptance is inferred. Stats bracket the contact, not the entire swing/cost.
Reports, raw hashes and cases: `comparison-summary.json`, each capture's
`-report.json`, `combat-cases.json`, `case-label-correction.json`, `save-route.json`
in the run directory. The actual disposable Documents save was written after
launch and differed from its seed while held personal roots hash-checked intact.

Restoration: runner's first exit was 1 at RestoreFiles/Copy-Item, a sharing
violation on dinput8.dll after tracked-PID shutdown. Both personal roots had
already returned. No unknown process was killed and no conflict guard bypassed.
`pwsh -NoProfile -File tools/run_combat_capture.ps1 -Mode Restore -Receipt
build/verification/native-combat-desktop-20261024-135521/transaction.json` exit 0.
Receipt is **Restored**; independent SHA-256 recheck of all three install files
and both original save inventories PASS (`restoration-independent.json`). Game
closed; Steam left open/disconnected on the matching account, metadata not
overwritten. Owner of the transient file lock is unproven; retain the failure,
do not describe the original runner exit as success.

Independent static follow-up pinned both endpoint-helper callsites, including
the previously missing fifth/seventh stack arguments and count forwarding.
240 new primary bytes + 226 previously pinned helper bytes PASS independent PE/
Capstone checks. External evidence and recovered argument tuple are in
NATIVE_MELEE_CONTRACT. No source/config/binary change, rebuild, commit or push
for this native-observation/documentation unit; the prior 93/93 suites belong
to the hash-pinned candidate used here. VR/IK stayed disabled throughout.

Next exact probe: follow endpoint resource producers `140229630` / `140225A30`
and the event constructors that populate `+0xF0/+0xF4/+0xF8/+0x198`; recover keys,
index bounds, actor/equipment generation and legitimate attack-context ownership
before proposing a physical-contact seam. B1 defense needs an active attacker
and an actually equipped shield, not another passive roll. VR-only IK/armor/
headset cases wait for hardware. Do not relaunch without a new bounded question.

### IK/combat continuation: close post-copy publication invalidation

Task: F2/C2, verify the existing source-before-copy transaction still owns its
tracking/calibration, rig and equipment when native code returns. Baseline
`cf864aa`; inherited dirty work is preserved. Fresh Debug and Release builds and
all 93 registered tests passed before this probe. Source graph tools are absent
from this session's catalog, so discovery used direct source reads instead.

Hypothesis: pre-write guards do not cover the native interval between the inner
source hook and outer publication. A fixture mutation in that interval should
refuse publication even after the changed state is restored. Alternatives: the
consumer already invalidates it, or the pre-write guards cover the interval;
the actual two-hook test discriminates these without a game launch.
Rollback boundary: task-owned additions in arm_ik.cpp/test_arm_pose_native.cpp
and existing documentation only; no new native calls/writes or altered solver
thresholds. Native tests remain blocked: current Steam ActiveUser is 0 and
`run_combat_capture.ps1 -Mode Check` exits 1, `Active Steam account changed`,
before staging. No receipt/install/save mutation or game launch occurred.

Result: reproduced RED with
`cmake --build build --config Debug --target test_arm_pose_native --parallel 4`
(exit 0), then `ctest --test-dir build -C Debug -R '^arm_pose_native$'
--output-on-failure` (exit 1, `post-copy mutation 1 published a stale receipt`).
The cause was final-copy verification accepting coherent matrix bytes without
rechecking the source transaction's external ownership after native execution.
`VerifyNaturalCopy` now repeats tracking/calibration/mode, rig selection/resource/
topology and equipment/stance validity at that boundary. It refuses publication,
not an unsafe late rollback of native-owned final-body/root bytes.

The 22-case actual two-hook regression restores changed external state before
querying hands: an invalid receipt must stay unavailable until a fresh solve.
The focused rebuild/test then passed, as did full rebuilt Debug and Release
93/93 suites and five relevant Debug tests x50 (250 passes). Solver thresholds,
source write owner, mapping/proportion behavior and natural call count remain
unchanged. Updated the header's stale alias-only/two-bone description.

Independent B2/C1 static unit: `1403C7520`'s stack-local contact record and AL-only
acceptance/bookkeeping, plus native endpoint geometry via `1403C7B10 -> PlayerIns
vtable+0x448 -> 14035BBE0 -> PlayerIns+0x850 assembly / 140212670`. Four primary MCP ranges,
1,084 bytes, match pinned PE bytes; external evidence and next exact stack-
argument/resource probe are in NATIVE_MELEE_CONTRACT. This is not blade-bounds
or damage/defense qualification. Existing run-2 observer overhead remains
median 9.63725 ms, p99 16.2664 ms; no cost improvement is claimed.

Final offline commands (all exit 0): Debug build with `--parallel 4`, full Debug
CTest, separate LAB Release `dsvr_core` build with `--parallel 4`, then
`pwsh -NoProfile -File tools/auto_verify.ps1` (fresh Release build, 93/93 CTest,
doctor PASS and development package refresh). Logs/results and final candidate
hash/source manifest: `build/verification/ik-combat-20261024-134111/`,
`candidate-final.json`. Regular package stays LAB OFF; the observer runner uses
the separately pinned LAB core. The receipt helper handles PowerShell's UTF-16
redirected logs; its first UTF-8-only read refused before writing a receipt.

Gate: source regression PASS; static geometry edge PASS; F2 native/headset and
B/C OPEN/BLOCKED on the live account/case prerequisites. No install, launch,
desktop input, save/Steam metadata changes, database mutation, commit or push.
Next: after the operator restores the established disposable Steam account in
Offline mode, use `candidate-final.json` with the existing protected runner and
separately labelled combat cases in a new transaction directory. Re-pin if any
manifest source or binary changes; do not reuse historical candidates. For IK,
use the runbook's verified-copy/armor cases
on this source candidate; the natural copy counter is not headset proof.

### D1b receiver edge closed: the Begin handler is a pump side effect, 25 October 2026

```text
Task ID / objective: D1b - pin the receiver/virtual edge that reaches the
  dispatcher worker-creating handler 1411BEF10. Read-only, no game launch.
Baseline commit + dirty files: cf864aa; 35 tracked paths modified, 3 deleted
  (`git status --short`), including this session's vr_trace/combat-capture
  work. Nothing was staged or committed.
Prerequisites and evidence: live Ghidra GUI holding DSRFull through the
  GhidraMCP default-group rail; pinned EXE bytes for independent decoding;
  build/verification/d1-owner-20261024/ from 24 October.
Hypothesis / exact missing fact: the missing fact was which virtual slot and
  which caller invoke 1411BEF10. The 24 October note assumed the route ran
  through vftable slot +0x50 (1411BE560) and its HGBeginThreadMessage.
Files and symbols touched: docs/NATIVE_STEREO_CONTRACT.md (worker-slot row plus
  a new closed-edge section), this handoff, and the execution guide's D1b
  bullet, first-next-probe block and lane-D ledger row. No source, test,
  config or package change.
Experiment / expected discriminating observation: if the +0x50 route were real,
  an edge from that message path into 1411BEF10 would exist. Instead enumerate
  the pump's own xrefs and decode its switch bodies.
Result: static, and the hypothesis is falsified. 1411BEF10 is dispatcher vftable
  slot +0x10; its only caller is the message pump 1411BF150 (slot +0x00), body
  [1411BF150,~1411BF2F8), which waits on 140CC5600, reads the type with
  140CC5410 and `JMP RDX` over an eight-entry image-relative table at RVA
  0x11BF2FC. The route in is the body at 1411BF1CC: gate dispatcher+0xbb == 0,
  a user32 cursor import call, `MOV R9/R8/RDX` from message +0x50/+0x48/+0x40,
  then `CALL [RAX+0x10]` at 1411BF1F3. Four further bodies `CMP
  [dispatcher],R15` against 1416098B0 and skip when the object *is* the base
  dispatcher, so a derived dispatcher vftable exists and is still unnamed.
  Worker creation and both failure paths are byte-pinned (contract table).
Commands, exit codes, candidate hashes, evidence paths: get_xrefs_to
  0x1416098B0 (11 DATA refs), get_xrefs_to 0x1411BF150 (2 refs only: the vftable
  slot and its unwind record 141E36D48), get_assembly_context 0x1411bf175,
  read_memory 0x1411BF150 len 352, read_memory 0x1411BEF10 len 128 and
  0x1411BEF90 len 480, Capstone x86-64 decode of those buffers - all exit 0.
  Fresh regression at this tree state:
  `ctest --test-dir build -C Debug --output-on-failure` exit 0, "100% tests
  passed out of 93". No rebuild was required (documentation only).
Gate: static evidence only. D1b stays open, D2 stays UNVERIFIED.
Remaining uncertainty: the type-index-to-body mapping is unpinned because
  read_memory cannot reach ram:0x14011BF2FC or 0x14011BF2F0 in this program
  view; the pump's thread owner is unknown; the derived dispatcher vftable is
  unknown; the poster that supplies the dispatcher's +0x20/+0x28 refcounted
  objects is unnamed; not every counter writer is proven.
Next exact action / alternative if it fails: decide the 141A18B64 reading by
  asking which section contains it. If it is `.CRT`, a statically constructed
  dispatcher exists outside the renderer and its pump owner must be found
  before any per-view claim. Otherwise enumerate the renderer `+0x68`
  consumers from `1411B498B` outward to the `CALL [dispatcher]` site. If no
  thread start names the dispatcher, take the guide's fallback: trace the
  pump's message producers backward to the nearest earlier per-view producer
  and repeat D1 there. Do not replay `0x1411BE710`, patch no-return flags, or
  repair the primary database.
  Follow-up probes run in the same pass (all exit 0): `get_xrefs_to 0x1411BF380`
  -> exactly 3 refs (renderer `UNCONDITIONAL_CALL` `1411B498B`, unwind record
  `141E36D60`, DATA slot `141A18B64`); `read_memory 0x1416029E0 len 64` -> the
  string block is `HGSystemThread`, `HG_Worker`, `HGCommandDispatcher::Exec`;
  `get_assembly_context 0x1411BEF36` -> `LEA RCX,[0x1416029e0]` then
  `CALL 140E44D20`, so the thread name is pinned; `get_xrefs_to 0x141602A00`
  -> zero refs, so `HGCommandDispatcher::Exec` is a naming hint for the pump,
  not an edge. The DATA slot at `141A18B64` holds RVA `0x11BF380` with
  `0xFFFFFFFF` before it and RVA `0x11BF3A0` (inside the constructor, not a
  function entry) after it — a CRT static-init descriptor for a second
  module-scope dispatcher is the leading reading, a registration table the
  alternative, undecided.
```

Task: close the one named next probe in `NATIVE_STEREO_CONTRACT.md` — "pin that
initialization message's receiver/virtual edge" for `1411BEF10` — without
touching the primary database or launching the game.

Probe (read-only MCP against the live `DSRFull` GUI, then independent Capstone
decoding of the returned bytes; no `GHIDRA_MCP_ALLOW_SCRIPTS`, no repair):

```text
get_xrefs_to   0x1416098B0            -> 11 DATA refs, all constructor sites + 1411BF175
get_xrefs_to   0x1411BF150            -> 2 DATA refs only: the vftable slot, unwind record 141E36D48
get_assembly_context 0x1411bf175      -> pump prologue, 12 instructions each side
read_memory    0x1411BF150 len 352    -> full switch bodies
read_memory    0x1411BEF10/BEF90      -> the no-return fallthrough window
capstone x86-64 over those bytes      -> addresses pinned, RET at 1411BF146
```

Result: `1411BEF10` is dispatcher vftable slot `+0x10`, and its only caller is
the dispatcher's own message pump at `1411BF150` (vftable slot `+0x00`), which
waits on `140CC5600`, reads the type with `140CC5410` and `JMP RDX` over an
eight-entry image-relative table at RVA `0x11BF2FC`. The route into the handler
is `1411BF1CC`: gate `dispatcher+0xbb == 0`, a user32 cursor import call, then
`MOV R9/R8/RDX` from message `+0x50/+0x48/+0x40` and `CALL [RAX+0x10]` at
`1411BF1F3`. Four other bodies `CMP [dispatcher],R15` against `1416098B0` and
skip when the object *is* the base dispatcher, so a derived dispatcher class
exists and is still unnamed. Worker creation is now byte-pinned with its error
paths (two iterations over `dispatcher+0x50`; `0xC8` queue, `140CC5B80` init with
capacity `0x4000` and 12-byte entries, `140CC5EE0(queue,4)`, `0x48` worker,
`1411C2CE0`, stores at `+0x40/+0x48` and `+0x50/+0x58`); a failed queue skips the
whole iteration and a failed worker frees the queue through the owner's virtual
`+0x68` — neither returns an error, so partial initialization is observable.

Two findings that outrank the original question: the handler takes the
dispatcher's `+0x20/+0x28` refcounted objects from the *message payload*
(`1411BF00D`/`1411BF017`), so D2a cannot claim retention without naming the
poster; and the pump also mutates Win32 cursor state and dispatches
window-shaped derived handlers, which is the concrete reason D1c keeps the
"trace backward to the nearest earlier per-view producer" fallback rather than a
replay hook.

Honest limit: `read_memory` cannot reach `ram:14011BF2FC` or `14011BF2F0` in this
program view, so the type-index-to-body mapping is **not** pinned — the contract
table lists bodies in listing order, not type order. Do not promote that mapping
to a claim without a defined-block read or the GUI's switch annotation.

No C++/test/config/package change and no build was required for this unit; the
last full verification remains the 24 October 93/93 Debug and Release receipts.

Next probe: the pump is entered only as `CALL [dispatcher]`, so its thread owner
is not one xref away. Enumerate consumers of renderer `+0x68` (xrefs to
constructor `1411BF380`) and the `HGSystemThread` name reference at `1411BEF36`
to find the `CALL [dispatcher]` site, then classify the derived dispatcher
vftable required by the last four bodies. D1 stays unresolved, D2 UNVERIFIED.

### G5 comfort composition and D1 ownership, 24 October 2026

The existing `comfort_vignette`/`vignette_strength` controls had no compositor
consumer. They now drive the real mono eye blit: artificial stick demand
(sampled before physical-follow mixing), emitted snap/smooth turn deltas,
60/250 ms attack/release and a bounded concurrent head-turn boost. A feathered
angular mask stays centered on each eye's optical axis without changing the
camera/projection. Physical-only motion remains clear. Off, theater, tracking
invalidation, stale timing and failed submission clear the envelope. The new
pixel constant buffer and native D3D11.1 binding range are restored afterward.
Native stereo composition and operator comfort remain **UNVERIFIED**; G5 is
not accepted by offline rendering alone.

Verification (all final commands exit 0):

```powershell
cmake --build build --config Debug --parallel 4
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
& .\build\src\setup\Release\DSVR-Setup.exe --doctor
python -B tools/package.py --build build --out dist --version 0.16.0-render-host-preview --latest --reshade-addon --ctest-log build/verification/comfort-vignette-20261024/release-ctest.log --doctor pass
```

Both configurations: **93/93 PASS**, including actual WARP pixel readback in
both eyes, asymmetric optical centers, a feathered clear center/dim periphery,
Off, no physical-follow activation, cadence/duplicate/epoch/stale-time cases,
head-turn boost and native constant-buffer range restoration. Readback occurs
only in the test. Receipts: `build/verification/comfort-vignette-20261024/`.
An intermediate WARP fixture crash was traced to its truncated Context vtable
and a null buffer-output pointer in the new range check; both were corrected.
The full suite also exposed Windows PowerShell 5.1 loading incompatible
inherited pwsh module paths. Its CTest entry now unsets `PSModulePath`, allowing
5.1 to discover its own built-ins; the transaction runner itself is unchanged.
Development package refreshed at `dist/DSVR-latest-x64.zip`; Release core SHA256
`c275982e88c4407f66774d3673d0dc4b539d41f4166b8aba2c61906e9db4aac7`.

The D1 probe recovered the inline factory's renderer argument, renderer
`+0x68 -> HGCommandDispatcher`, its two worker slots `+0x40/+0x48`, and the
counter decrements hidden after context cleanup. `140CC4010` is an atomic
`XADD -1` returning the prior count. Dispatcher virtual `+0x50` targets
`1411BE560`, an initialization-message wrapper, not `1411BEF10` directly.
Read-only MCP responses, complete PE unwind decoding and 34 pinned static
assertions are in `build/verification/d1-owner-20261024/`:

```powershell
python -B build/verification/d1-owner-20261024/verify_static.py
```

Next D1 probe: follow `1411BE560`/`1411BF150` initialization message/virtual
dispatch, then each worker's error/cancellation paths; camera, retained storage
and GPU readiness remain open before D2. Use the existing dynamic-vignette
runbook case for the prepared comfort candidate when collecting headset
evidence. No game launch, installation, input injection, native writes or
restoration transaction occurred; doctor observed the game closed. The mixed
unfinished checkout remains unstaged, with no reset or commit.

### B1 natural combat capture, 24 October 2026 (active)

**First run restored; comparison failed trace coverage.** Protected transaction
`build/verification/native-combat-20261024/transaction.json` is `Restored`;
original install files and both personal save roots were hash-verified after
operator exit (PID 38116). No input automation or XR was used. Operator reported
one ordinary hit connected; weapon/shield/enemy names were not supplied. Raw
capture `DSVR-trace-38116-52995984-1.jsonl` is retained beside the receipt:
354,950,419 bytes, SHA-256
`78fd62e3563e0dc06236ad115d2e83a48d3f96ca40b21fce1411440bfebf8522`.
Footer: 1,224,269 events, **795,660 dropped**, queue peak 4096. Read/assembly
median **9.26235 ms**, p99 **14.575 ms**. These are native measurements, not
the earlier stub profile. `hit1-native-summary.json` retains one stable
PlayerIns -> EnemyIns sample (seq 32), return 1, target HP 44 -> 31. This is
consistent with the reported hit, not complete attribution/defense qualification.
Selected observations explicitly retain the original loss footer and are not a
reconstructed full capture.

**One-cause experiment: remove unrelated tracing.** Added startup-only
`combat_trace_only`, LAB-gated `VrTraceInstallCombat()`, immutable capture-domain
selection/header and combat/Step event admission. This mode installs no camera,
render, constant-upload or input-blocker probes. A 100,000-upload regression
failed before filtering and passes afterward. Native actor read guards and
memory implementation remain unchanged: do not claim the ~9 ms read cost fixed
until measured. Regular Release and separate LAB core rebuilt successfully;
Release suite **91/91 PASS** (`release-combat-domain.log`); fresh Debug build
and suite **91/91 PASS** (`debug-combat-domain.log`), all exit 0. Receipts are
under `build/verification/native-combat-20261024/`.

Next: pin current sources/binaries in a new combat-only candidate receipt, use
`tools/run_combat_capture.ps1 -CandidateReceipt <new receipt>` with a **new**
transaction directory, and capture separately labelled hit/miss/defense cases.
Require combat-domain header, zero drops, complete triplets, native Step coverage
and measured observer cost before accepting comparisons. Do not reuse the old
candidate manifest (its source hashes must now fail) or the restored transaction.
Static next probe follows confirmed type-1 dispatch through `1428C7F11`, which
restores the outer frame and transfers record/target/source into `14031D5D0`;
preserve actual register flow rather than split-function signatures. B/C OPEN.
(Superseded 25 October 2026: that tail is a forged-return epilogue and `14031D5D0`
is a thunk-pool entry, not a handler - see the 25 October static-probe record
below and `docs/NATIVE_MELEE_CONTRACT.md`.)

### Combat-domain captures are analyzable: analyzer fix, 25 October 2026

Run-2's capture was always rejected by the analyzer, not by the capture.
`tools/vr_trace.py::validate` demanded XR submissions unconditionally, so a
capture whose header declares `capture_domain: "combat"` - which deliberately
installs no camera, render or input probes and therefore submits nothing - could
never be `valid`. The acceptance tool could not certify the only evidence the
combat transaction is allowed to produce.

One cause, one change: the submission requirement is now domain-aware.
`validate` reads the declared domain from the header; only `combat` is excused
the empty submission stream, and a new `submission_coverage` check records why
(`NOT_APPLICABLE`). Legacy headers (`run-1`, no `capture_domain`) keep the old
strict behaviour, and a combat capture that *does* emit a submission without two
verified consumed views still fails.

- `python -B tests\test_vr_trace.py`: exit 0. Added four assertions: a
  combat-domain capture with tracking only is `valid`, `complete` stays false,
  `submission_coverage` is `NOT_APPLICABLE` and `gpu_stereo` stays
  `UNVERIFIED`; a `full`-domain capture with the same records is still invalid;
  a combat capture submitting without consumed views is still invalid.
- Re-analysis of the retained run-2 capture
  `build/verification/native-combat-only-20261024-run2/DSVR-trace-29540-54040046-1.jsonl`
  (report written beside it as `report-domain-aware.json`): **`valid: true`,
  zero errors**, `capture_domain: combat`, `tracking_correspondence` PASS,
  footer `dropped 0 / queue_peak 21 / contended 0`, 166 paired contact/identity
  samples with 166 diagnostic samples.
- Same analyzer on the run-1 capture and `hit1-selected-observations.jsonl`:
  still `valid: false` (`capture dropped events`, 795,660 drops, invalid
  completion acknowledgement, legacy domain). The fix excuses nothing real.

Next probe unchanged: run-2 already satisfies the "combat-domain header, zero
drops, complete triplets" requirement from the B1 note above, so the remaining
B/C gaps are case labelling and attribution, not capture quality. Re-run
`tools/run_combat_capture.ps1` with separately labelled hit/miss/block/parry
cases against a fresh candidate receipt, then read
`native_combat_observations.samples` per case. `contact_limit_reached` at 256
natural calls is the coverage ceiling to watch.

### Case windows are measured, not declared: runner/analyzer round-trip, 25 October 2026

Task objective: make the B1 comparison cases *observable*. Until now
`tools/run_combat_capture.ps1` printed "perform hit now" and the analyzer had no
idea when the operator acted, so `native_combat_observations.samples` was one
undifferentiated pile and B1 could not be attributed even if the capture ran.
Prerequisite evidence: the retained run-2 capture already satisfies header /
zero-drop / complete-triplet quality, so the gap is labelling, not capture.
Expected observation: one JSON plan written by the runner during the case loop,
consumed by the analyzer, with per-case sample counts. Rollback boundary: the
runner, the analyzer and their tests only - no native code, no new install
transaction.

Draft first, then depth. The rough draft was a `combat-cases.json` sidecar with
label + seconds. Testing it exposed the real defect: the boundaries were
*planned*, so a case the operator skipped still got a window. The detail pass
stamps each window from the clock while the operator is actually performing it.

- `tools/run_combat_capture.ps1`: `-Cases hit:20,miss:15,block:12,parry:10`
  drives the loop; `SaveCases` writes `combat-cases.json` (schema 1, `qpc_hz`,
  per-case `start_qpc`/`end_qpc`/`state`) atomically when the case loop starts -
  after the observer is staged and the game is up - and again as each window
  opens and closes, so a crash mid-case still leaves the completed windows. Windows are monotonic and non-overlapping, and the
  SelfTest refuses a plan whose boundaries are not real QPC readings.
- Portability trap, reproduced in isolation before the edit:
  `[Diagnostics.Stopwatch]::Timestamp` is .NET Core only, so under
  `powershell.exe` (5.1) every boundary was silently `null` - the same class of
  fault as the 24 October `GetRelativePath` finding. Fixed with a `Qpc` helper
  that resolves `Stopwatch.GetTimestamp()` when the engine has it and falls back
  to `Timestamp` otherwise. SelfTest now PASSes under both `powershell.exe` and
  `pwsh`.
- `tools/vr_trace.py`: `read_capture` picks up `combat-cases.json` beside the
  capture, rejects a plan whose `qpc_hz` disagrees with the capture header, and
  binds each paired `native_combat_contact` sample into the window that actually
  contains it. Output gains `combat_case_plan` (path, window count, labels,
  errors) and `native_combat_observations.case_observations`, which per label
  reports `window_qpc`, `samples`, `samples_with_hp_change`,
  `samples_with_hp_loss`, `stable_samples`, `target_handles` and the distinct
  HP/stamina delta values seen in that window. A malformed plan marks the report
  invalid; an absent plan
  stays valid and reports `{}`, so old captures analyze unchanged.
- `tests/test_vr_trace.py`: per-case bucketing, hyphenated labels, the
  absent-plan-stays-valid case, the `qpc_hz` mismatch refusal, and the
  diagnostic-flags/`native_rate` cross-checks.
- The round trip is now a registered regression test, not a manual check: the
  runner's `-Mode SelfTest` synthesizes a combat-domain capture with one complete
  contact/identity/diagnostics triplet inside each written window and one outside
  both, runs `tools/vr_trace.py` on it, and requires `valid`, combat integrity
  `PASS`, two windows, three paired samples, exactly one sample per label, and
  per-label HP-loss evidence matching the declared windows. CTest #8
  (`combat_capture_transaction`, pwsh) and #9 (`..._ps51`, powershell.exe) both
  cover it, so the producer/consumer clock agreement is guarded on both engines.

Depth pass on the analyzer: each paired sample now carries `read_reasons` (the
four actor-read reason codes from its own diagnostics record, `null` when no
diagnostics record agreed with the contact), and each case reports
`diagnostic_samples` and `read_reason_counts`. Without that attribution a window
in which every actor read was refused is indistinguishable from a window with no
samples at all, which is exactly the confusion a confident label invites. The
round-trip test asserts the per-case counts too.

Second portability trap found by running that test, and it was worse than the
first because it was nondeterministic: `($a+$b)/2` on two `[long]` QPC readings
promotes to a **Double** whenever the sum is odd, so `ConvertTo-Json` wrote
`"qpc":100000000001.5` and the analyzer reported malformed records plus an invalid
completion acknowledgement. The parity of a live clock reading decides it, so the
same script passed under `powershell.exe` and failed under `pwsh` on the next
run - it looked engine-specific and was not. Reproduced in isolation before the
fix (`pwsh -NoProfile -Command "$a=[long]100000000001;$b=[long]100000000002;(($a+$b)/2)"`
→ `Double 100000000001.5`), fixed with `-shr 1`, and four consecutive SelfTest
runs (two per engine) then passed.

Verification (all exit 0):

```powershell
python -B tests/test_vr_trace.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_combat_capture.ps1 -Mode SelfTest
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\run_combat_capture.ps1 -Mode SelfTest
cmake --build build --config Debug --parallel 4
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

Debug **93/93 PASS**, Release **93/93 PASS**. Cross-tool round-trip against the
plan the runner itself wrote (real 10 MHz QPC values) plus a synthetic
combat-domain capture: `capture_integrity` PASS,
`combat_observation_integrity` PASS, `valid: true`, 2 paired samples,
per-case `{hit: 1, miss-block: 1}`. An intermediate FAIL was my fixture's own
footer event count, not the analyzer.

Live path status: `-Mode Check` against the re-pinned receipt now stops at
`Active Steam account changed` (registry `ActiveUser=0`, nobody signed in), so the
labelled capture stays blocked on the operator signing the disposable account
(`1843241039`) into Steam in Offline mode. The guard fired correctly and staged
nothing; the preflight receipt path was not created.

Static probe completed on the named lead, and it refuted the lead. The 24 October
note "transfers record/target/source into `14031D5D0`" reads a **forged-return
epilogue** as a call. Pinned bytes at `1428C7F11`-`1428C7F3C` show a register
shuffle into R8/RDX/RCX, an outer-frame restore, then `PUSH RBP` /
`LEA RBP,[0x14031D5D0]` / `XCHG [RSP],RBP` / `RET` - the thunk address is pushed
as a *return address*, so no `CALL` exists and Ghidra shows the thunk with zero
callers. Walking it anyway gives six wrapper links
(`14031D5D0` -> `1400baf52` -> `142eca4c2` -> `142e381b2` -> `1427d78ad` cookie
write -> `142ebfbab` `LOCK`/`UNLOCK` -> `14280bc9b`), and the last is a
return-address selector: `MOV RBX,[RSP+0x10]`, `MOV RAX,0x14031D863`,
`CMOVNC RBX,RAX`, `MOV [RSP+0x10],RBX`, `RET`, keyed on the carry flag from that
atomic op. Its alternate target `14031D863` is `XOR AL,AL; JMP 140045BE3`, and
`140045BE3` is `MOV RCX,[RSP+0x40]; XOR RCX,RSP; JMP …` - a stack-cookie
verification stub, not gameplay. The identical `LOCK`/`UNLOCK` + return-selector
shape at `FUN_142e2b96f` (called from `14031D7A8`) and the `CALL` to the 411-byte
`FUN_140386F00` at `14031D85E` confirm the page `0x14031D5xx`-`0x14031D8xx` is a
shared compiler-generated thunk/return pool. Ghidra's "no instruction at
`14031D863`" is an undisassembled block, not a destroyed function; the database
was not repaired and the outer dispatcher was not replayed. Consequence: recover
the type-1 continuation from the register state at the shuffle site itself, never
by stepping through the forged `RET`. Full byte-level record in
`docs/NATIVE_MELEE_CONTRACT.md`.

Next probe unchanged and now better armed: pin a fresh candidate receipt, then
`tools/run_combat_capture.ps1 -Mode Run -CandidateReceipt <receipt> -Cases
hit:20,miss:15,block:12,parry:10` in a **new** transaction directory with the
operator performing each case. The analyzer needs no extra flag - it reads the
sidecar automatically - so read `case_observations` per label and watch
`contact_limit_reached` at 256 natural calls. B1 stays unchecked: no live
labelled capture exists yet, and a plan is not an observation.

### The 25 October physical-combat run recorded two of five cases, 25 October 2026

Task objective: the labelled run happened (`build/verification/physical-combat-20261025`,
game PID 38456, four operator-opened 20 s captures). Decide what it actually
establishes. Rollback boundary: the analyzer, the runner and their tests - no
native code, no new install transaction.

What the retained captures say, per window, on the capture's own QPC clock:

- cap1, cap2, cap3 overlap only `load`, and only ~20 s of its 300 s window.
- cap4 overlaps only `block`, ~20 s of its 45 s window.
- `hit` and `miss` overlap **no** capture at all: cap3 ends before `hit` opens and
  cap4 starts after `miss` closes. Those two declared cases were never recorded.
- The `parry` window measured 7.16 s of its declared 45 s - the transaction
  deadline ended it, and the plan still called it `declared`.
- cap3 reached the 256-call detail ceiling, so its 256 samples is a floor.
- All four are zero-drop (`dropped: 0`, `contended: 0`, `queue_peak` 23-62/4096)
  and `capture_integrity` PASS.

Consequence: this run adds no B1 evidence for hit or miss. The 24 October run-2
pair remains the only labelled hit/miss native evidence on this checkout.

The defect that hid this was ours, not the operator's: the analyzer collapsed
"not fully covered" and "never recorded here" into one `uncovered_labels` list,
so a window with zero overlap read like a window whose count is a floor.

- `tools/vr_trace.py`: `partly_covered_labels` (overlap but not end to end) is now
  separate from `unobserved_labels` (zero overlap), with `coverage_status`
  `PASS`/`PARTIAL`/`NONE`/`UNVERIFIED` (`UNVERIFIED` when the capture never wrote a
  footer, because then coverage is unknown rather than zero). Each window carries
  `capture_overlap_ms`, `capture_covers_window`, `declared_seconds`,
  `measured_seconds`, `plan_state` and `truncated`, and the report gains
  `truncated_labels`. The coverage sentences in
  `native_combat_observations.coverage_limitations` now name which gap applies.
  An unclosed capture is a third case: no footer means no span, so its
  `coverage_status` is `UNVERIFIED` and no label belongs in any accusation list.
  Those lists were built with truthiness (`if not row["capture_overlap_ms"]`), which
  put every window of an unclosed capture into "never recorded" - the same false
  negative one layer down. They now compare against `0` and `False`, so `None`
  (unknown) stays out of all three.
- `tools/run_combat_capture.ps1`: one `FinishRequested` stop marker replaces the
  same `Test-Path ($Receipt+'.finish')` spelled inline three times. A window ended
  by the stop request or the deadline is stamped `state='truncated'` and warned
  about, instead of `declared` - the QPC boundaries are identical either way, so
  the state was the only thing separating "the transaction ended first" from "the
  operator had the full time". `TraceInventory` reads each new capture's header and
  footer and reports a capture with no closing footer; `RestoreTransaction` records
  those as `unclosed_traces` in the receipt rather than copying them silently.
  `ReportCaseCoverage` prints `COVERAGE <label>: n capture(s) overlap, m cover it
  completely` while the run is still open, because after restoration the chance to
  re-attempt an uncovered case is gone.
- `tests/test_vr_trace.py`: a fixture window that straddles the capture edge plus
  one entirely outside it must report two different gaps, and the never-recorded
  one must not appear in the partly-covered sentence. The first attempt at that
  fixture was itself rejected with `case plan windows overlap` - the guard working,
  not a test bug. A capture written without its footer must report `UNVERIFIED` with
  all three label lists empty and `capture_overlap_ms`/`capture_covers_window` `None`
  for every window.
- `tools/run_combat_capture.ps1 -Mode SelfTest`: now also requires the round-trip
  capture to report coverage `PASS` with no unobserved label, requires
  `ReportCaseCoverage` itself to print `COVERAGE <label>: 1 capture(s) overlap, 1
  cover it completely` against the fixture capture, and requires a stop-requested
  window to come back `truncated` and to have ended well before its declared 300 s.
  It then writes a header-only capture next to the closed one and requires
  `TraceInventory` to call exactly one of the two unclosed, and requires
  `ReportCaseCoverage` to still count one overlapping capture - an unclosed capture
  has a header timestamp, so nothing else stops it from reading as coverage.
  That last assertion found a real bug in the new code: `TraceInventory` ended with
  `return ,$spans`, which hands the array back as a single object, so every caller's
  `.Count` read 1 no matter how many captures existed. The comma is gone; the
  operator-facing counts are now the number of files on disk.

Verification (all exit 0):

```powershell
python -B tests/test_vr_trace.py
ctest --test-dir build -C Debug -R "vr_trace|combat_capture_transaction" --output-on-failure   # 4/4, pwsh + powershell.exe
cmake --build build --config Debug --target test_combat_trace --parallel 4
build\tests\Debug\test_combat_trace.exe
python build\verification\physical-combat-20261025\coverage_probe.py
```

The full Debug/Release `ctest` suite was deliberately not re-run: this checkout
carries other tabs' in-flight edits (`src/core/input.cpp`, `src/common/config.*`,
`src/core/mod_menu.cpp`, `src/core/vr_heading.h`, `src/core/camera_math.h`,
`src/core/arm_pose_math.h`, `tests/test_input.cpp`, `tests/test_camera_writes.cpp`).
Do not quote a suite total for a tree that is not yours alone.

Re-running the probe over the four 25 October captures after the list-semantics fix
changed nothing: all four wrote footers, so their `PARTIAL` status, per-window
`overlap_ms` and the `hit`/`miss` zero-overlap finding are identical to the numbers
above. The unclosed-capture path only affects captures that never closed, and this run
had none.

Unexplained, and left open on purpose: `observer_read` is the diagnostics span at
`src/core/vr_trace.cpp:415`, `(start-observe_start)+(VrTraceClock()-native_end)` -
our own work with the native dispatch subtracted out. Its median, in cap1..cap4
order, is 14.3 ms / 30.0 ms / 3.7 ms / 5.6 ms while `native_call` median is
1-3 us, so the same code costs 8x differently between captures. With
`dropped: 0` and `contended: 0` the writer queue was not the bottleneck. The
fixture cannot settle this: `test_combat_trace`'s actor reads target the test
process's own resident heap, and it prints no observer timing. Next probe for
that one: a capture with the four actor reads refused (force the permission
budget to zero) against a normal capture in the same scene - if `observer_read`
collapses, the cost is the guarded reads touching paged game memory on the game
thread; if it does not, the span is measuring descheduling and the number should
be labelled as wall-clock exposure, not work.

Next probe for B1, unchanged and better armed: one labelled capture per case. F9
holds 20 s, so declare each case window at <= 20 s or open a capture inside each
window; the runner now prints per-label coverage before the transaction closes, so
an uncovered case is visible while the game is still up. Still blocked on the
operator signing the disposable account (`1843241039`) into Steam in Offline mode -
`-Mode Check` stops at `Active Steam account changed` and stages nothing.

### Ghidra MCP is now a first-class tool route, 24 October 2026

The Empryo `mcpServers` entry named `GhidraMCP` pointed at
`http://127.0.0.1:8089` with `transport: http`. That listener is the Ghidra
plugin's private HTTP bridge, not an MCP endpoint: `POST /mcp`, `/sse` and `/`
all answer 404, so the server never connected and no `mcp__GhidraMCP__*` tool
ever existed. That absence, not any Ghidra fault, is what forced the
`combat-evidence/melee-20261024/mcp_read.py` shim and the refused headless
`tools/ghidra/query.ps1` path (the GUI holds the `DSRFull` lock).

Fixed in `%LOCALAPPDATA%\Empryo\config.json`: the entry now launches the
project's own stdio bridge
`C:\Tools\DarkSoulsRemaster_De\ghidra-mcp\.venv\Scripts\bridge-mcp-ghidra.exe
--default-groups function,listing,program,xref,comment,symbol,utility,datatype,analysis`
with `cwd` at the ghidra-mcp checkout and `timeout: 1800000` so a long
decompile or `run_script_inline` is not cut off below the bridge's own
1815 s ceiling. The group list is the real fix for the lazy problem: default
lazy advertises 91 tools and leaves `xref` unloaded, so `get_xrefs_to` and
`get_assembly_context` return `not_loaded` and invite a hand-rolled HTTP call;
`--no-lazy` advertises 239 endpoints, roughly 16k tokens of tool schema billed
on every request. These nine groups advertise 148 and cover every tool this
repo names. `load_tool_group` is not a substitute here: it loads server-side
(it reported `new_tools: 8` for `xref`) but Empryo never re-lists, so the tool
is still `unknown tool` in the rail - a workaround, not a route. The bridge
auto-connects over TCP to the live GUI instance
(UDS is unavailable on CPython for Windows) and reports
`DSRFull` / `DarkSoulsRemastered.exe`, 261,311 functions.

Probe: `tools/ghidra/mcp_probe.py` (run it with the ghidra-mcp venv
interpreter, which is the only Python holding the `mcp` module). Bare run does
the handshake and prints instance plus metadata; `mcp_probe.py <tool> <json>`
calls one tool. Verified through the MCP rail itself, not the shim:
`get_functions` on `1411CD8A0` returns size 1283, body end `1411CDDE`, three
callers including `ProcessDantelionRenderFrame`; `get_xrefs_to` on the same
address returns the two in-function calls plus the `141E37720` and `141A1A35C`
data references. Use these tools directly for static probes. The workarounds are
gone: `combat-evidence/melee-20261024/mcp_read.py`, `tools/ghidra/query.ps1`,
`tools/ghidra/repair.ps1` and the duplicate `tools/DsvrProbe.java` were deleted
on 24 October 2026 and must not be reintroduced while the GUI holds the project.
`run_script_inline` refuses until `GHIDRA_MCP_ALLOW_SCRIPTS=1`, which stays
unset: the read-only tools already cover bounded listing, xref and decompile
probes, and `get_assembly_context(xref_sources="0x1411BFED3,0x1411BFEDA",
context_instructions=12)` returned the inline `HGDrawPlanEntity` vptr store that
`DumpInstructions.java` used to produce.

### Harness alignment: effort ladder, Ghidra heap, prompt-cache budget, 25 October 2026

Empryo model config (`%LOCALAPPDATA%\Empryo\config.json`, provider `local`,
model `iq3_s`): the effort ladder is now `low, medium, xhigh`. `max` was removed
from both `models[].efforts` and `reasoning.efforts` at the user's instruction,
and the previously missing `medium`/`xhigh` entries are present. The bridge
entry no longer carries `--no-lazy` (see the section above).

`maxOutputTokens` 32768 -> 16384: October request logs show completion p95 1,235
and max 13,013 tokens, so the old reserve parked a quarter of the window on
output that never arrives. Compaction trigger 0.6 -> 0.72 and reset 0.4 -> 0.55;
today's input p50 was 64k and p95 87k, so ordinary sessions no longer compact
mid-flight and each compaction stops resetting the prefix.

The context ceiling stays 131072, and the reason is VRAM, not preference. The
Strata launch profile `iq3_s-128k` runs `--max-context 131072 --kv int8 --spec 4
--vram-reserve-mib 1536`; its engine log reports "only 0 MiB free once the slots
are written (reserve 1536 MiB); shrinking the expert cache", an expert cache
already cut to 6.24 GiB, "conversation cache off: not enough free RAM to park
conversations" and 1414 MiB free with everything loaded. A larger window needs
memory the 16 GiB card does not have, so the honest levers were the output
reserve and the compaction thresholds. October did see 55 requests above 131072
(max 178,600 input) - that is the truncation and re-planning source, now bounded
by compaction instead of by the host.

Prompt cache: the host emits `prompt_tokens_details.cached_tokens` only, never
`cache_write_tokens`, so Empryo's cache-write figure stays 0 by construction and
only the read figure is meaningful (measured hit rate 92.5%). The break log
under `%LOCALAPPDATA%\Empryo\logs\cache-breaks` recorded 21 breaks today, most
from instruction-file and config edits rebuilding the prefix. Lesson: batch
`AGENTS.md` / `config.json` edits instead of spreading them across a session.

Ghidra heap: the `-Xms8G -Xmx32G` override is gone from
`C:\Tools\DarkSoulsRemaster_De\ghidra-12.1.4\support\launch.properties` and a
`findstr` over that file now matches nothing, so the launcher default applies.
Next probe: compare per-request `cacheRead` before and after the tool-group
trim; if advertised schemas still dominate the breaks, drop `datatype` and
`analysis` from the default group list and load them per task.

User now explicitly permits game launch, will control the game, and offers HMD
on request. Start with desktop original-controls combat; no input automation or
headset qualification of an absent binding. Baseline `cf864aa`, inherited 21
modified files and two untracked user files preserved. Objective: observe whether
the natural `1403C4020` contact route correlates with hit/miss/defense outcomes,
then follow the evidenced native producer/consumer rather than the effect-like
HP lead. Expected evidence: LAB header/contact samples, independent operator
case/outcome, and hash-verified install/save restoration within a 15-minute cap.
Recovered `build/desktop-observer-session.ps1` (the previous search used the old
`run_pose_capture.ps1` name). Audit found its DeployObserver duplicates an
existing `observer_only` setting and deploys the regular DLL. Preserve that
retained script; restore a narrowly scoped, tested observer transaction in tools
using its existing save-separation/conflict-aware rollback pattern. No unknown
native writes/calls. Rollback: the new runner/tests plus receipt-owned test
installation/save roots only; Steam's live metadata is snapshotted, not blindly
overwritten. Current active Steam account matches the established disposable
route, cached Offline preference is set and the latest connection state is
Logged Off after user-initiated logoff; recheck before actual staging/launch.

### Combat-only re-pin and PowerShell engine portability, 24 October 2026 (preflight green; native capture not run)

Pinned the tested combat-only candidate in a new receipt
`build/verification/native-combat-20261024-repin/combat-only-candidate.json`
(SHA-256 `2a3278c4e435c96880d616ba8fdfb0879e19b6459b73cde2c256dcf73bc8d18d`),
generated by `pin_combat_only.py` in that folder. The old manifest under
`build/verification/native-combat-20261024/` is kept untouched as a negative
control, and it does fail: an offline hash probe shows 2 of 183 source entries
now differ (`tests/CMakeLists.txt`, `tools/run_combat_capture.ps1`) while all
four binary hashes still match, so the C++ candidate itself is unchanged. The
live runner rejects the old manifest with `Source changed since qualified
build: tests/CMakeLists.txt`.

Runner defect found by running `-Mode Check` under Windows PowerShell 5.1
rather than `pwsh`: `[IO.Path]::GetRelativePath` (.NET Core 2.0+) and the
three-argument `[IO.File]::Move(source,dest,overwrite)` do not exist there, and
`[IO.File]::Replace(source,dest,$null)` misbinds `$null` as an empty path
("The path is not of a legal form"), reproduced in isolation before any source
edit. Fixed with a `RelPath` helper and a named `.bak` third-party backup, so
one script serves both engines. Locked in as regression tests: ctest registers
`combat_capture_transaction` (pwsh) and `combat_capture_transaction_ps51`
(powershell.exe) as tests #8/#9, both PASS. Registered suite is now **92**
tests; Debug **92/92 PASS** (`debug-combat-domain.log`), Release **92/92 PASS**
(`release-combat-domain.log`), combat-only filtered set 6/6
(`combat-only-prelaunch.log`). With the new receipt, live preflight prints
`CHECK PASS: pinned candidate, offline/disconnected Steam, exact-game block,
disposable seeds and original roots. No changes yet.` under both engines and
creates no receipt (Check exits before any write; absence verified).

Next probe unchanged: deploy the pinned LAB core/loader in a **new**
transaction directory, capture separately labelled hit/miss/defense cases, and
require the combat-domain header, zero drops, complete triplets, native Step
coverage and a measured observer read cost. B1 remains operator-gated.

Static probe run against the live read-only primary database. Headless
`tools/ghidra/query.ps1` refuses because the Ghidra GUI holds `DSRFull`
("Project file ... is already open in another instance"); the lock was left
alone. Working route is
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\melee-20261024\mcp_read.py`
through the running bridge, with run-only interpreter
`C:\Tools\DarkSoulsRemaster_De\ghidra-mcp\.venv\Scripts\python.exe`. Receipts:
`handler-14031d5d0-primary.json`, `handler-14031d5d0-stage2.json`.
`mcp_read.py` needed one fix: pydantic exposes `isError`, not `is_error`, so it
died after its first call. The result contradicts the recorded lead.
`14031D5D0` is `thunk_FUN_1400baf52`, five bytes (`14031d5d0`-`14031d5d4`),
zero recorded callers/callees, reached only by DATA references from
`1428c7f31`/`1428c7f38`, `14177c298`/`14177c2a8` and `141d4a258` - not a call
target. It jumps to `FUN_1400baf52` (14 bytes, `1400baf52`-`1400baf5f`), whose
only caller is that thunk and which calls `FUN_142eca4c2`. `1428C7F11` has
exactly one xref: an unconditional jump from `1403C41CA`, as recorded. So the
type-1 route lands on a thunk plus stub, not the real handler. Next static probe
is `FUN_142eca4c2` plus the `1428C7F11` tail bytes - check pinned bytes and PE
unwind bounds before trusting any ABI or lifetime, and do not repair the primary
database or replay the outer dispatcher. **Done 25 October 2026**: that probe ran
through the MCP rail and closed the route as a compiler-generated thunk/return
pool (forged-return epilogue, `CMOVNC` return-address selector, stack-cookie
stub). See the 25 October static-probe record below; do not walk this chain again.

### Layered combat observability, 24 October 2026 (offline slice complete; combat open)

User approved layered logging and the written focused specification
`docs/superpowers/specs/2026-10-24-combat-observability-design.md` (design-only
commit `cf864aa`). Execution authority remains the existing B/C guide.
Baseline: inherited 13 modified files and two user-owned untracked files;
preserve them. Fresh Debug build exit 0; six focused combat/trace checks PASS.
Task: finish the inherited identity trace, expose read/lifetime refusal reasons
and native-versus-observer timing, retain whole samples under queue pressure,
and emit rate-limited writer-side INFO health summaries. No damage or haptics.
Experiment: regress actual detour/writer/analyzer behavior, including unreadable
actors, replaced handles, overflow, incomplete pairs and capture replacement;
then fresh Debug/Release suites. Rollback: this task's incremental trace/test/doc
edits only, never the inherited embodiment changes. Native B/C remain open until
the natural encounter/producer contract is independently qualified.

**Implemented and checked.** The inherited full-width actor/return identity
records are retained. A contact, its identity and its new read-reason/timing
diagnostics now enqueue atomically (three records or three counted drops).
Refusals distinguish record/address/vtable/type/handle/stats/range failures,
actor replacement and missing before-call admission; unknown deltas stay null.
Natural dispatch still executes exactly once, with no injected calls, damage,
haptics or getter execution. The 256-call/20-second limits remain intact.
`tools/vr_trace.py` rejects ambiguous/contradictory samples, exposes read counts,
queue coverage and native/read p95/p99, and never certifies physical combat.
Removed three synchronous per-frame/contact `CombatBench:` text sites from the
pose hook; one bounded `combat_bench_provisional` packet now records those
classifications without pretending the later native body copy succeeded.
`TraceHealth` writer summaries and `CombatHealth` capability/verbose heartbeats
are limited to five-second intervals, plus trace closure/capability changes.

**Fresh verification.** Commands exit 0 unless stated otherwise:

- `cmake --build build --config Debug --parallel 4`, then
  `ctest --test-dir build -C Debug --output-on-failure --output-log build/verification/combat-observability-20261024/debug-ctest-verified.log`:
  **88/88 PASS**. An intermediate Python regression indentation error in
  `debug-ctest-final.log` was corrected, followed by focused and full green runs.
- `cmake --build build --config Release --parallel 4`, then
  `ctest --test-dir build -C Release --output-on-failure --output-log build/verification/combat-observability-20261024/release-ctest-final.log`:
  **88/88 PASS**.
- `ctest --test-dir build -C Release -R "^(combat_native_trace|vr_trace|vr_trace_capture|arm_pose_native|diagnostics_.*)$" --repeat until-fail:50 --output-on-failure --output-log build/verification/combat-observability-20261024/repeat-50.log`:
  **eight checks x50 PASS**. The new actual-detour regression first failed at
  the missing diagnostic record; the analyzer regression first failed at the
  missing diagnostic-sample field before implementation made them green.
- `build/tests/Release/test_combat_trace.exe --profile build/verification/combat-observability-20261024/synthetic-cost-profile.jsonl`:
  **256 complete triplets**, zero drops, queue peak 768. Reader output is
  `synthetic-cost-report.json` beside it. Its read-cost p99 .0002 ms uses stubbed
  memory/native calls: **not** real GuardedRead cost, whole-hook cost or a
  game/VR performance result. No native/headset performance threshold was changed.
- `pwsh -NoProfile -File tools/auto_verify.ps1`: Release **88/88**, doctor PASS,
  latest development package refreshed. Final documentation is packaged by the
  same verifier once this handoff is written; authoritative package identity is
  the generated `dist/DSVR-latest-verification.json` and the final candidate
  receipt, not an old ZIP hash copied here.

**Candidates, not deployments.** Regular Release DLL SHA-256
`ead3b6863196c06f4829dd054ae3c37e6cc24a249dab79a8d14b8e5c36c410a3`.
A separate offline-source-dependency LAB core was configured in
`build/combat-lab` (`DSVR_ENABLE_LAB=ON`, tests/setup OFF), then built with
`cmake --build build/combat-lab --config Release --target dsvr_core --parallel 4`.
LAB DLL SHA-256
`1de09ca51f2b6392b6a373d54ceef938078997e6a8ebe6a767a9831b7ac6116d`;
`lab-candidate.json` records the cache/hash. The regular build stays LAB **OFF**;
its package deliberately does not contain the native contact hook. Neither
candidate was installed or run. The ordinary header records `combat_hook=0`;
verbose mode alone cannot create native contact evidence.

**Static combat finding.** Five primary ranges / 443 bytes equal the pinned PE.
The `32F480` path loads maximum HP, consumes effect-like weighted percent/fixed
values through `403250/407110`, then places the returned delta in ESI. This
narrows the previously promising `32F548` lead; it is not an ordinary-contact
or accepted damage ABI. Exact facts, bounded coverage and external receipts
are in `docs/NATIVE_MELEE_CONTRACT.md`. Primary DB was not modified/saved.
REA MCP still reports its Windows native security authority unsupported; a
direct helper-script attempt also found Python's `mcp` module absent. No
package installation or security bypass was attempted; the live read-only
Ghidra MCP route supplied the successful observations.

**B/C verdict and next experiment: OPEN / native comparison unavailable.**
Still required: a protected original-controls hit/miss/block/parry/invulnerability
comparison binding actual contact/attack context, actor generations, simulation
phase and independent outcomes. The runbook now specifies the exact diagnostic
checks and limitations. Before that run, reconcile stale installed binaries,
verify current Steam Offline and disposable-save state, and recover/audit the
protected transaction command: the old handoff names
`tools/run_pose_capture.ps1`, but it is not present at that path or the searched
retained build/temp/backup locations. Do not invent an unprotected launch or
request a headset test of an absent damage binding. No game launch, desktop
input, install/save/Steam metadata/runtime-registration write, or push occurred;
the only commit in this turn is the approved design-only `cf864aa`. Mixed source
work remains unstaged, including all inherited embodiment edits and the two
user-owned untracked files. No background command remains active.

### Physical combat priority, 24 October 2026 (active)

User steering: move onto physical combat ASAP; stop further IK/camera work.
Both full configurations have rebuilt and passed 88/88; neck reproduction and
camera-tilt receipts are retained under
`build/verification/neck-combat-continuation/`. No deployment or game run.
Combat task: close one natural-contact observation gap in the existing lab-only
`vr_trace.cpp` hook. It checks actor handles internally but does not serialize
them or the upper 32 native return bits, preventing independent lifetime/result
correlation across callbacks. Use the existing `combat_native_trace` detour/
writer test seam; add one paired identity record, keep natural dispatch exactly
once, capture-generation/refusal/256-call bounds, and no damage/haptics/writes.
Static probe has recovered the `32F548` argument-loading predecessor through
`14032F52D -> 140F86571 -> 1427ED353`: receiver/vtable R14, EDX zero,
R8D EBX, R9D -ESI and stack flag bytes BPL. This is not an accepted attack ABI.
Raw primary bytes and independent pinned-PE decode are outside distributable
sources at `C:\Tools\DarkSoulsRemaster_De\combat-evidence\melee-20261024\`.
Game and Steam processes are currently stopped. Native hit/miss/block and the
actual contact producer remain unverified; no speculative call is authorized
by these static findings. Rollback: this task's trace/test/doc edits only.

### Native neck/IK realism, 23 October 2026 (offline slice complete)

Direct user priority supersedes stereo task order: focus on IK realism and an
immersive camera using the actual native Neck bone. Operator is now available
for requested desktop/Quest 3 launches and headset inputs. Computer control
remains prohibited; no game launch occurs without coordinating a protected run.
Task: correct one embodiment boundary: HMD-authoritative view and a length-
preserving native Neck->Head chain, not Head-only translation toward the eyes.
Baseline HEAD `17bd941`; only this session's MODLOG edit and the two user-owned
untracked files are present. Debug rebuilt successfully; four focused registered
checks (`head_rider`, `arm_ik_naturalness`, `arm_pose_native`, `body_pose`) pass.
Independent fixture provenance rechecked: all 960 floats match retained run-7
sample 0. Its Head uses column 0 approximately up and column 2 approximately
native facing; the existing torso snap assumes negative column 2, while a
separate Head-only shift can change the Neck->Head link length by up to .35.
Seams: existing SolveTorsoPose tests on the independent native fixture and the
actual source-pose adapter in arm_pose_native. Preserve camera tracking, all
native-copy/root/refusal guards, authored naturalness thresholds and exact links.
Rollback: task-owned source/tests/docs only; no root/collision writes, guessed
eye offsets, new native hooks, saved-profile deletion, commits or deployment.
Result: IN PROGRESS; first reproduce neutral native neck twist and no-lean link
stretch, then make the smallest correction and rebuild both configurations.

Continuation objective: finish the inherited neck/IK/camera slice, then recover
one bounded physical-combat contract edge without enabling guessed damage.
Entry HEAD remains `17bd941`; eight inherited modified files (this handoff,
arm_ik/body_pose_math/camera/head_rider and the three associated tests), plus
user-owned `IDEA.md` and `tools/ghidra/FindFieldWrites.java`; preserve all.
Fresh Debug rebuild and five focused checks pass. Reproduce the old native
neck-frame failure in an isolated scratch harness using HEAD's solver and the
current independent-fixture regression, leaving this checkout untouched.
Then exercise the current candidate, both full configurations and camera
tracking isolation. Combat probe: natural indirect dispatch at `14032F548`,
read-only primary instructions and pinned bytes only. No game launch, desktop
control, runtime writes, deployment, commits or analysis-database changes.
Receipts belong under `build/verification/neck-combat-continuation/`.

### Unattended D1b ownership research, 23 October 2026 (paused)

Task ID / objective: recover the native stereo executor's renderer input and
separate completion-counter owners, one bounded static work unit.
Baseline: HEAD `17bd941`; user-owned untracked `IDEA.md` and
`tools/ghidra/FindFieldWrites.java` are preserved. No source diff at entry.
Direct user restriction: **do not launch the game, use computer control, or
request operator input**. This supersedes earlier operator-availability notes.
Prerequisites: pinned shipped EXE, retained D1 executor receipts, primary
read-only Ghidra project; recheck REA MCP availability without importing or
repairing the primary database. Code-graph MCP is not exposed in this session.
Hypothesis / missing fact: the inline `HGDrawPlanEntity` construction at
`0x1411BFED3/DA` identifies the renderer whose `+0x68` owner supplies the
consumer's two polled objects. Follow actual pointer flow, not equal offsets.
Experiment: decode `0x1411BFE80..0x1411BFF4F`, trace the incoming renderer,
then classify each counter's allocation/reset/worker/retirement separately.
Expected discriminating observation: explicit producer-to-wait identity edges,
or a bounded documented gap; a zero-count loop is not GPU-completion evidence.
Rollback boundary: task-owned evidence/docs/tooling only; no C++/config/native
writes, replay, deployment, save/install/runtime-registration changes or commits.
Result / gate: paused by the user's neck/IK priority. The independent verifier
has now run successfully: **124 bounded checks**, **10 primary-memory ranges /
5,323 bytes** equal the pinned shipped PE. Normal HGCommandWorker ownership,
submission and counter retirement were recovered; cancellation/fault paths and
native coverage remain open. D1b PARTIAL, D1 unresolved, D2 and live F2 unverified.
Receipt: `build/verification/d1b-counter-owners-20261023/static-checks.json`;
raw MCP/decode receipts and `verify_static_contract.py` are physically retained
under `C:\Tools\DarkSoulsRemaster_De\combat-evidence\d1b-20261023\`.
No C++/tests/config/package/install/save/database edits or game run occurred in
that static work unit. Detailed stereo-contract consolidation is still pending.

### Active embodiment wrap and native IK transfer run, 23 October 2026

User priority: finish existing unfinished work before expanding scope, and
resolve the IK issue in this run. Operator is available for game/headset tests.
Entry HEAD `52bddc1`; entry changes were the two Ghidra range scripts,
`FindFieldWrites.java` and `IDEA.md`. Current menu/height/aim edits are uncommitted.
No neck feature or replacement solver has been started.

Objective: finish the current settings/input integration, then replace the
known no-op post-copy IK boundary only with an evidenced native transfer seam.
Hypothesis: the natural animation return precedes native body remapping and
proportion processing; solving its selected source before that transfer lets
native code propagate body/armor/attachments without guessing an identity map.
Primary read-only Ghidra query confirms `357A20 -> 320980 -> 207F10` order;
new correct-root/map/proportion capture is required before changing the hook.
Raw query: external `combat-evidence/embodiment-pose-boundary-20261023.txt`.
Rollback boundary: this run's source/test changes and receipted test-owned
install/save files only. No native root/collision writes, replay, primary
analysis repair, or alteration of retained independent fixtures.

Fresh initial Debug baseline: 88/88. Current integration sweep exposed one
seated-camera expectation still using the old implicit manual height; it now
has separate automatic and explicit-override cases. Focused camera checks pass.
Menu Apply/Save was reproduced red with real pointer clicks and fixed with
writer-acknowledged applied/saved state. Aim/grip profile tests were red and now
pass; the actual XR pointer adapter passes the distance/yaw/grid fixture.
Full Debug/Release and protected native/headset receipts are still pending.

Operator confirms Steam Offline and game closed. Preparing the existing
reversible disposable-save transaction; exact receipt and restoration status
must be recorded here before concluding the run.

**Capture transaction receipt and restoration (complete).** The protected
`run_pose_capture.ps1` transaction ran 14:06:50-14:21:50 local, vault
`C:\Users\Gabrielle Monlea\.dsvr-backups\desktop-observer-20261023-140650`,
saved receipt `build/verification/embodiment-wrap/native-capture-receipt.json`
reports state **Restored**: `DSVR.dll` restored to its exact pre-transaction
identity `304A63FCD676E26547BEB5D9D33508798FD78EEC9258E2BE812B6507ED890645`;
`DSVR.toml` hash-verified `53F06F10755C5DC5B2B6271738A47E2FC1B74A767CCA824E7A255401470BC70C`;
`DSVR-Setup.exe` restored; loader stub absent after cleanup; save roots were
never moved (`moved:false`) and the five inventoried save-file hashes verify
against the pre-transaction inventory; Steam-owned `remotecache.vdf` test
metadata retained as client-owned state; Steam client left untouched;
exact-game outbound block and `cachedOffline=True` verified. The transaction
console host was orphaned when harness processes were killed at the start of
this continuation session, but the transaction completed its own restore;
receipt and completion output agree. No game/headset run was launched for the
wrap itself.

**Read-only capture evidence (hashes re-verified this session).** Pinned
executable `a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`;
hardware debug registers restored before detach (`debug_registers_restored:
true`); only the expected attach-breakpoint
exception (`0x80000003`, one event); 16 complete pairs / 32 samples. The full
capture archive now lives in-repo at
`build/verification/embodiment-wrap/native-pose-transfer-baseline.json`
(602,923 bytes), SHA-256
`e3b459e6de9257028a62ab2019c5cfc481a4b83aa5b3160cb4ec1f194c21b866`,
re-hashed matching during this wrap. The observer script is retained beside
it at `build/verification/embodiment-wrap/dsvr-pose-transfer-observe.py`,
SHA-256 `543449a3893b33423f3347c070eca9bb2dd55d61ec1e10d692e89b86392154fd`,
matching the capture's embedded `script_sha256`. The scratch-cache capture
copy under `.dsvr-backups\pose-transfer-20261023-140810\` has since been
purged; the in-repo archive is the retained evidence. Prior-session raw
Ghidra receipts remain under
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\embodiment-pose-*-20261023.txt`.

**Native copy contract recovered (large finding of this run).** Fully written
up in [arm IK evidence](docs/ARM_IK_EVIDENCE.md); independently recomputed
from the retained capture this session: both models are `HGIRootDrawNode`
arm assemblies with roots at assembly `+0x20` (the old observer read `+0x30`,
which is model-root field 4); the 80-row body palette's rows 0..68 are
byte-identical to source rows 0..68 (a 69-entry identity map for this
resource/character, with the proportion flag at body arm assembly `+8 & 4`);
and the two proportion records reproduce all 13,248 `model+4` floats with
maximum float32 error `5.245391818571167e-08`. This is evidence for this one
captured resource only; nothing about other equipment variants or animation
states follows.

**Seam implemented against that contract.** Two detours with pinned prologue
bytes (installation refuses on any byte mismatch, and refuses before hooking
unless the player vtable route resolves exactly to the update function):
`0x140320980` source-pose writer and `0x140357A20` local-player outer pose
update. Inside an open local outer scope the source-pose detour lets the
native source writer run, then solves FABRIK on the fresh source palette
*before* the natural `140207F10` body copy executes in the outer update; a
second native source update invalidates the pending receipt. The outer
detour then lets exactly one natural update complete and
`VerifyNaturalCopy()` (read-only contract verifier in
`src/core/pose_transfer.h`: layout, map counts, proportion scale records;
never a body writer) must accept the corrected pending transfer before
`g_copy_verified` is set, and every public hand/head/combat-bench getter
refuses without it. The `tests/test_arm_pose_native.cpp` fixture models the
hook ordering, counts provisional-getter calls and keeps premature
publications at zero, and refuses commits under five after-read and two
before-copy mutation cases. RED was demonstrated at the source-pose seam
(`pose-transfer-red.log`), then green (`pose-transfer-green.log`); the
adapter RED/GREEN cycle is recorded in `ik-input-red.log`.

**Offline receipts (fresh at wrap time).** Debug build exit 0; Debug CTest
**88/88** (`build/verification/embodiment-wrap/debug-ctest-wrap.log`); the
four arm tests x50 green with zero failures (`arm-4x50-wrap.log` /
`arm-4x50-wrap-fails.txt` = 0); eighteen settings/menu/input/camera/xr/
diagnostics/calibration/tracking tests x50 green with zero failures
(`sweep-18x50-wrap.log`). The seam RED/GREEN logs from the run itself are
`pose-transfer-red.log`, `pose-transfer-green.log` and `ik-input-red.log`.
`pwsh -NoProfile -File tools/auto_verify.ps1` completed earlier in this run
(18:53:15Z, after the last source edit at 14:24:38 local; a full-tree mtime
scan confirms no packaged input changed after that build): Release CTest
**88/88**, doctor PASS, `dist/DSVR-latest-x64.zip` SHA-256
`883f4f428478b7dbb40715748d493d3824eda4b50fc5a5665dd6ea314ad4eb14`
with packaged `DSVR.dll`
`4c026540e676646cfde6da330b2c0478b2d5e3632f6ccb17f1edfdbe54393bdc`,
`dinput8.dll` `e8f1a44455c9fa8600de55afe9c6dcc167f3b7d2406f2421096075eb0cb60169`,
`DSVR-Setup.exe` `996e48643a8b6d50ed7a61997456b3806030d9217c7247c10b1750dffb02b339`
(per `dist/DSVR-latest-verification.json`).

**Wrap verdict.** The seam and its contract are offline-verified only. The
new hooks have never executed against the live game: live publication through
`g_copy_verified`, equipment/animation-state variant maps, armor/attachment
and crouch behavior (F3), headset comfort and the neck feature all remain
open. F2 stays unchecked in the execution guide; the runbook's new
"Verified native copy publication" row is the next live probe. Nothing new
was deployed: the game directory still holds the restored packaged
`304A63FC...` DLL, and this fresh package was not installed (operator was
logged in during preflight; deployment belongs to the next live probe, which
the capture script performs inside its own transaction).

**Commit receipt.** The verified wrap was committed locally as `18792a2`
("feat: solve arm IK before native body copy with copy-verified
publication"), 32 files, explicit paths only: reviewed source/tests, the
read-only `pose_transfer.h`, the two multi-range Ghidra dump scripts, and
MODLOG/ARM_IK_EVIDENCE/VERIFICATION_RUNBOOK/execution guide. No generated
package, ignored capture or raw decompilation was staged. The working tree
now holds only the untracked user entry files `IDEA.md` and
`tools/ghidra/FindFieldWrites.java`. No push, no deployment, no game launch,
no headset run.


### Source wrap committed, 23 October 2026

The verified wrap checkpoint above was committed locally as `763d198`
("feat: complete coherent calibration, embodiment fit and source wrap")
on top of `271bd47`, using explicit paths only: reviewed source/tests,
the provenance tool, AGENTS, evidence/authority docs, the sole execution
guide, and the superseded plans/specs retained verbatim under
`docs/history/`. No generated package, ignored capture or raw
decompilation was staged.

Fresh pre-commit receipts (not just the historical ones):
`cmake --build build --config Debug --parallel 4` exit 0;
`ctest --test-dir build -C Debug --output-on-failure` **88/88**.
`git diff --cached --check` flagged exactly one whitespace defect, a
trailing blank line at EOF inside the retained snapshot
`docs/history/2026-10-21-before-consolidation/docs/VERIFICATION_RUNBOOK.md.txt`.
It was intentionally left uncorrected: `docs/history/README.md` defines
those files as exact-text evidence snapshots, and editing them would
break byte fidelity. The exception is recorded here instead.

Working tree is clean after the commit. No push, no deployment, no game
launch, no headset run. Native gates remain open exactly as the wrap
verdict states: D1b/D1/D2 stereo, F2 source->body transfer, F3
independent mesh fit, B/C physical damage, collision/crouch and headset
qualification. Resume at the execution guide's D1b counter-owner probe:
`& ./tools/ghidra/query.ps1 -Address @('0x1411BFE80','0xD0') -Script
DumpInstructions.java`, then the entity vptr sites `0x1411BFED3/DA` and
owner `+0x68` counters; read-only primary database only.

### Source wrap and commit, 22 October 2026 (offline complete)

User request: finish and commit the existing source work, including its tests,
evidence and documentation consolidation. Baseline HEAD
`271bd47d809ca3e368e3c7e746160e8210ef72c2`; entry status/diff preserved under
`build/verification/source-wrap-20261022/`. Scope is the existing changes and
review-discovered defects, not completion of the remaining native-conversion
gates. No deployment/game launch or native ABI changes are needed for this wrap.

Initial rebuilt Debug and Release suites each passed 88/88. Retained fixture
provenance passed (960 floats byte-identical). Review found that extra Index/WMR
menu suggestion calls replace earlier bindings. The actual input adapter's fake
runtime now models profile replacement; focused Debug `input` is red as expected
(`input-bindings-red.log`). Expected fix: one complete set per profile, with WMR's
documented squeeze-click path. Spec reference:
https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrSuggestInteractionProfileBindings.html
Meta native documentation independently confirms replacement semantics. Keep
live/native/headset acceptance open. Rollback boundary: wrap-owned fixes only;
preserve all prior source work and capture evidence.

Wrap review fixes, each with a red/green adapter regression:

- One complete binding suggestion per Index/WMR profile preserves pose,
  stick/trigger and haptics alongside menu/clicks; WMR uses squeeze-click.
- Theater button-pair reservation delivers quick lone taps, consumes partial
  chord release, requires full release before rearming and resets on input
  invalidation. Menu chords discard any pending pair-button tap.
- XInput reinstalls each export's owned route independently, including mixed
  state-IAT/capabilities-inline ownership and delayed state-hook recovery.
- The camera hook preserves the arm measurement's rig revision when upright
  stature is unavailable, and refuses mixed measurement revisions. Existing
  observer-only refusal checks remain intact.
- Corrected a render-facing diagnostic's angle calculation and stale comments;
  retained all native alias/root, damage and stereo refusal boundaries.

Receipts under `build/verification/source-wrap-20261022/`:

- `cmake --build build --config Debug --parallel 4`: exit 0,
  `debug-final-build.log`.
- `ctest --test-dir build -C Debug --output-on-failure --output-log build/verification/source-wrap-20261022/debug-final-ctest.log`:
  **88/88**, exit 0.
- `pwsh -NoProfile -File tools/auto_verify.ps1`: exit 0, Release build,
  **88/88**, doctor PASS, development package refreshed (`auto-verify.log`,
  `build/latest-ctest.txt`). No formal release minted.
- `python tools/verify_native_pose_fixture.py build/native-pose-run7-snapshot.json`:
  exit 0, **960 floats byte-identical**, retained capture SHA-256
  `52f9170f8812e7bc5dcbfdb87740c14fdea3e8b3f7339e0ed6561adfcc48c747`.
- Review reproductions: `input-bindings-red.log`, `input-chord-red.log`,
  `xinput-mixed-red.log`, `calibration-fallback-red.log`; rebuilt focused green
  receipts record the corresponding fixes. A final comment-only evidence-link
  correction rebuilt/retested `arm_ik_naturalness` in both configurations.
- Documentation QA: **12 Markdown documents, 49 local links/anchors**, no
  missing targets or unbalanced fences (`doc-checks.json`).
- `git -c core.whitespace=cr-at-eol -c core.safecrlf=false diff --check`: exit 0.
  No line-ending normalization changes or unrelated warning cleanup.
- Graph refresh was attempted, but its returned camera snippet retained stale
  pre-edit line ranges; targeted source reads supplied the review evidence.

Package built UTC `2026-10-23T02:56:01Z`; independently compared Release DLL,
ZIP member and verification manifest, all identical (`package-check.json`).
`DSVR.dll` SHA-256:
`304a63fcd676e26547beb5d9d33508798fd78eec9258e2be812b6507ed890645`.
ZIP SHA-256:
`05595ac866114a0cd47a4501a7aadb9ed08b481be67c19bb99439b333de883e2`.

Commit scope: reviewed source/tests, the independent fixture/provenance tool,
AGENTS, current evidence/authority docs and the retained historical snapshots
that accompany superseded-document removals. Explicit path manifest; no blanket
staging, generated packages, ignored captures or raw decompilation included.
No deployment, game launch, protected save transaction or headset run occurred.
Doctor observed the game closed and firewall active; installed-candidate or
restoration equivalence is not inferred. No push/publication requested.

Verdict: **existing source checkpoint verified offline and ready for its local
commit**. Native stereo, distinct-palette body transfer, independent mesh fit,
physical damage/defense, collision/crouch and headset qualification remain open.
Resume native work at the execution guide's D1b owner/counter probe below;
committing this checkpoint does not close any of those native gates.

### D1b executor ownership continuation, 22 October 2026 (static slice complete)

Task: classify the executor wrapper's DATA references and recover one bounded
task/owner edge before considering any native replay. Baseline HEAD remains
`271bd47d809ca3e368e3c7e746160e8210ef72c2`; preserve the mixed dirty tree.
Prerequisites: pinned executable, current stereo contract, read-only primary
Ghidra route. Expected observation: the table slot identifies a constructor or
dispatch owner, or its references demonstrate a different role. Rollback
boundary: this continuation's evidence/doc edits only; no game/database/source
mutation, deployment or launch. D1/D2 remain open until independently evidenced.

Result: `0x141E370D8` is the wrapper's PE unwind entry, **not a task table**.
`0x142BBA7FC/803` are one stack-mediated control-transfer sequence in the
second executable `.text` region; its eventual wrapper invocation/ABI is still
unknown. Native RTTI verifies `HGDrawPlanExecutant` at vtable `0x141609898`
and `HGDrawPlanEntity` at `0x14160EA80`. Entity slot `+0x50` calls
`0x1411C4FA0`, which builds a stack-local executant/scratch, invokes the
consumer and passes its allocation to `0x140CC3D20`. Constructor
`0x1411C5320` records the renderer input in entity `+8`; destructor
`0x1411C5280` invokes the existing entity-container cleanup. See
[stereo evidence](docs/NATIVE_STEREO_CONTRACT.md#executor-reference-classification-22-october-2026-continuation)
for instruction sites, field transport and limitations. No enqueued-task,
completion-counter, result-semantics or GPU-retirement claim follows.

Commands/receipts: primary `query.ps1` calls in the evidence note exited 0,
with `-readOnly -noanalysis`. The separate query for `0x1407E8363` reported
no containing function, despite process exit 0. Bounded Capstone decoding and
independent PE/RTTI assertions passed. Exact local-only receipt paths/hashes:
`build/verification/d1-executor-20261022/static-checks.json`; baseline status,
EXE identity and saved mixed diff: `provenance.json` / `entry.diff` beside it.
Raw decompilation/disassembly stays under the external tool directory.

REA MCP recheck returned `provider_unavailable` / `not_configured` for
Ghidra/Java. CLI 6.7.0 doctor exited 1 overall but reported Ghidra and Windows
native admission healthy; the earlier security-gate diagnosis is historical.
No setup, client configuration change or binary import was attempted.

Candidate was preserved: package verification JSON SHA-256
`930c1b45fc47fdcd235e1a0882f64468a668d774c9104d1a352fb234cd7664e9`,
ZIP `29fd4dfaf0a6c8ec3099001eaa74040a8424e04c233370a1ca19f2dff3bdbb38`.
Its recorded DLL is `e731d3c22c72444ff9992ca9c153dbf18d5a7f149d8b4d968630845bcb814212`;
these are package identities, not fresh source/native qualification. No build
or CTest was needed/run for this static/doc slice. No C++/tests/config/package,
install/save/runtime/input changes, game launch or database repair/save.

Final validation: `python C:\Tools\DarkSoulsRemaster_De\combat-evidence\stereo_executor_verify_20261022.py`
exit 0 (pinned PE, unwind, RTTI/vtable and bounded instruction assertions;
22 raw/tool receipts hashed). Markdown QA passed for all three edited documents:
balanced fences, 13 local links/anchors, whitespace and retained A-K milestones.
Receipt: `build/verification/d1-executor-20261022/doc-checks.json`.
`git -c core.whitespace=cr-at-eol -c core.safecrlf=false diff --check -- MODLOG.md docs/NATIVE_STEREO_CONTRACT.md docs/superpowers/plans/2026-10-21-native-conversion-execution.md`
exit 0. No commits or staging; unfinished source work remains preserved.

Gate: **reference classification PASS (static); D1b PARTIAL; D1 unresolved;
D2 UNVERIFIED**. Both counter-owner records and a natural main-scene trace
remain prerequisites. No replay binding enabled. Next exact probe:
`& ./tools/ghidra/query.ps1 -Address @('0x1411BFE80','0xD0') -Script DumpInstructions.java`.
Inspect the inline entity vptr installation at `0x1411BFED3/DA`, trace its
renderer value into `+8`, then follow owner `+0x68` to the two counter objects.
If the primary listing is absent, use hash-pinned bounded decoding; do not
repair the database or infer lifetime from the wait loop.

### Stereo feasibility takeover, 23 October 2026 (planned next work unit)

Task: D1/D2 native stereo feasibility. User priority now supersedes the older
stereo-deferred handoff below: resolve stereo ownership first, then source/body
pose transfer, then a genuine physical-hit path. The sole work package is in
[execution guide D](docs/superpowers/plans/2026-10-21-native-conversion-execution.md#stereo-feasibility-work-package);
no parallel roadmap or production replay binding was created.

Baseline: `271bd47d809ca3e368e3c7e746160e8210ef72c2`, shared dirty checkout preserved.
Task-owned changes: this handoff, the execution guide and the stereo evidence
note only. No C++/tests/config/package/install changes. Saved Debug/Release
88/88 receipts remain historical; no fresh build/test run is claimed here.

Static probe: REA admission recheck returned `provider_unavailable` with
Windows `unsupported_host` security diagnostics. The existing primary query
fallback then succeeded: `pwsh -NoProfile -File tools/ghidra/query.ps1 -Address
0x1411C2F50 -Script DescribeFunction.java`, exit 0, `-readOnly -noanalysis`.
It reconfirmed the executor wrapper's argument/result transport and reported
DATA references at `0x141E370D8`, `0x142BBA7FC`, `0x142BBA803`; their role
and runtime use remain unknown. Fresh shipped-EXE SHA-256 matches
`a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`.
Raw receipt and hash: [stereo evidence](docs/NATIVE_STEREO_CONTRACT.md#planning-refresh-23-october-2026).

Documentation checks: all three edited files retain balanced fences and the
execution guide retains every A-K milestone; all 12 local links/anchors resolve.
`git diff --check -- MODLOG.md docs/NATIVE_STEREO_CONTRACT.md docs/superpowers/plans/2026-10-21-native-conversion-execution.md`
exit 0. No Markdown lint command is configured; no rebuild or CTest was run
for this documentation-only task.

Result: **static query route PASS; D1 ownership unresolved; D2 UNVERIFIED**.
The plan requires separate counter-owner and resource-lifetime records, one
natural main-scene observation, then a conditional single two-eye transaction.
A wait loop, synthetic audit or different target handles cannot close D2. An
unsafe consumer means moving earlier in command production, not duplicating
simulation or declaring stereo impossible.

Prior proposed probe (superseded by the current continuation):
`pwsh -NoProfile -File tools/ghidra/query.ps1 -Address
0x141E370D8 -Script ListReferences.java`. Classify the table/owner and task
lifecycle, then trace both polled `+0x20` counters before proposing any hook.
Reconcile current package/preflight and TESTING's save/install transaction
before native instrumentation. No game launch/deployment or save/runtime/input
mutation occurred; current restoration/desktop state was not inferred.

### Head-bone body fit, resumed after a crash, 23 October 2026 (offline complete; native fit unverified)

The previous session died mid-turn (machine restart) with an in-flight change on
disk: a Head-bone-anchored body fit intended to answer the live report that
embodiment is poor and the head camera clips into the model even after a good
calibration. The tree did not compile: `camhook.cpp` already read
`cfg.embodiment_fit`, but `Config` never gained that field, and
`tests/test_camera_observer.cpp` had no stub for the new `ArmIkNativeStature`
call (link error LNK2019, which ctest reported as `camera_observer (Not Run)`).
Nothing was rewritten; the pieces already on disk were kept and completed.

What the change does, in one ratio: `CalibrationFit { Manual, Arm, Head }` and
`ResolveCalibrationMapping` can now anchor the physical-metre -> native-world
conversion on the native upright Head bone instead of measured arm reach, so the
camera lands on the avatar's own head instead of hovering off it. `arm_ik.cpp`
freezes `g_native_head_height` and `g_native_shoulder_width` from a fresh native
palette before any mod correction (the same freeze rule as the already-verified
arm reach), and `ArmIkNativeStature` converts them with the already-validated
root scale. Shoulder spread is measured and logged only: no shoulder socket is
inferred from a controller capture, and no guard was relaxed to force a write.
The setting is `embodiment_fit = "head"|"arm"` (default head), parsed,
validated, saved with comments, and exposed in DSVR-Setup ("Body fit") and the
mod menu ("Body fit anchored on"). A requested head anchor with no upright
measurement yet logs one warning and uses the arm fit rather than guessing.

Root cause found in the in-flight code, not papered over:
`CalibrationNativeHeadHeight` compared `std::min` against `std::max` in the
wrong variables, so the one-knee-down/lunge rejection (`upper - lower > .35f`)
was dead code and a lunging palette could have become the avatar's stature.
Corrected to low/high foot semantics and pinned with offline assertions for
airborne, below-floor, knee-down, crouched-pelvis, non-finite and upright cases.

Build hazard that cost the most time here: `DSVR-Setup.exe --doctor` crashed
with 0xC0000005 (exit -1073741819) and printed nothing. `Config` gained a
`std::string`, which changes the struct layout, but
`build\src\setup\dsvr_setup.dir\Release\setup.obj` was still from 21 October --
MSBuild had not recompiled it, so the Setup exe mixed two `Config` layouts.
Incremental builds do not reliably track this header. After a shared-header
struct change, rebuild the whole configuration
(`cmake --build build --config Release --parallel 4 -- /t:Rebuild`) before
trusting any Setup result. Doctor then passed unchanged.

Receipts: `cmake --build build --config Debug --parallel 4` exit 0;
`ctest --test-dir build -C Debug --output-on-failure` **88/88**; `ctest -C Debug
-R "calibration_fit|config|camera_observer|camera_writes|arm_" --repeat
until-fail:50` 8 tests x 50, exit 0; full Release rebuild exit 0;
`pwsh -File tools/auto_verify.ps1` exit 0 with Release **88/88**, doctor PASS and
refreshed `dist\DSVR-latest-x64.zip`. Release `DSVR.dll` SHA-256
`e731d3c22c72444ff9992ca9c153dbf18d5a7f149d8b4d968630845bcb814212`, deployed to
the game directory and re-hashed identical; the previous candidate is kept as
`DSVR.dll.stale-20261022-212742` in that folder. New offline assertions live in
`tests/test_calibration_fit.cpp` (head anchor, trim, seated, arm preference,
out-of-band refusal, stance gate) and `tests/test_config.cpp`
(`embodiment_fit` default, load, invalid-value rejection, round-trip).

Next probe: the new "Body fit anchor" row in `docs/VERIFICATION_RUNBOOK.md`.
Only a headset verdict says whether anchoring on the native Head bone removes the
head-camera clipping and whether the frozen native head/shoulder numbers are
plausible for the pinned build; the CPU fixture proves the switch and the
refusals, nothing about comfort or fit. `tools/verify_preflight.ps1` still needs
reconciling before any protected native run.

### Locomotion basis and invisible player, 23 October 2026 (superseded by the pass above)

Live functional report against the deployed 2156b538 candidate: travel felt
disoriented, the avatar sometimes went the complete wrong way and pushed into
geometry, both got worse the more the right stick turned the view, the player
capsule never rotated with the view, and the player model was invisible for the
whole run.

Root cause of the travel failure, in `src/core/camera.cpp`:
`CameraPublishHeadDirectedLocomotion` handed input the **rendered model's root
facing** as the frame native interprets travel in, and `input.cpp`/
`UpdateRoomscaleInput` pre-rotated the stick into it. Native travel is
camera-relative: the follow camera whose matrix we already own is the frame the
stick is consumed in. The pre-rotation therefore double-counted the turn the
right stick had just applied to that camera, with error exactly
(camera heading - model facing) - zero at bind, growing with every stick turn,
which is why turning made it worse and why large turns walked backwards into
walls. The published basis is now the planar heading of the camera we wrote
(`g_last_planar_heading`); in Head mode the mapping is identity, and Controller
mode rotates hand heading into that same camera heading. `ReadPlayerRenderFacing`
stays a validity gate and a diagnostic, never a movement basis. Renamed
`native_forward` to `movement_basis` through `RoomscaleBasis`,
`HeadDirectedLocomotion` and `VrHeading::CandidateLocomotion` so the next
reader cannot re-introduce the model-facing assumption.

The capsule complaint is the same bug seen from the other side: with the stick
steered in the camera heading, native's own movement turns the capsule toward
where the view asks to go. Rotating the capsule independently of native would
need a player-facing/root write, which `docs/ENGINEERING.md` forbids until the
E2 motor contract is verified ("Direct root translation is not a replacement
motor implementation"), and history already bans reintroducing player-yaw
writes. Instead the `camera_heading` VR trace now records ChrPosData yaw, the
rendered model facing yaw and the steered heading in one event, so the next
headset capture answers the attribution question read-only before any write is
considered.

Invisibility was a settings collision, not a render failure: the live
`DSVR.toml` had `hide_player_model = true` and `hide_second_model = true`, and
`presence.cpp` treated `hide_player_model` as "hide both models". At most one of
the two local-player render models may now be hidden, with a one-shot warning in
`DSVR.log`. Two offline assertions pinned the old behavior and were rewritten to
the new contract rather than relaxed: `tests/test_presence.cpp` (both flags ->
primary hidden, body retained) and `tests/test_camera_writes.cpp` (published
movement basis equals the written camera heading and is explicitly not the model
facing).

Receipts: Debug and Release builds exit 0; `ctest --test-dir build -C Debug
--output-on-failure` 88/88 and `-C Release` 88/88 (88 registered per `ctest -N`).
`pwsh -File tools/auto_verify.ps1` exit 0, doctor PASS, refreshed
`dist\DSVR-latest-x64.zip`. New Release `DSVR.dll` SHA-256
`a54e9d19f45821950c6d4b743b45c2bac51488cd2dc91b2bf3a83bd7e7e3ca58`, deployed to
the game directory and re-hashed identical. The live `DSVR.toml` also had
`hide_player_model` reset to the documented `false` default so the body and
weapon both render; the presence guard now makes that setting unable to hide
everything on its own.

Next probe: the two new `docs/VERIFICATION_RUNBOOK.md` rows - "Turn-relative
travel" (forward at 90/180/270-degree view headings, then while travelling) and
"Local player visibility" (both hide flags true). Start a VR trace capture during
the turn row so the `camera_heading` event answers whether native carries the
model facing toward the camera heading; that is the E2 attribution evidence the
capsule question needs.

### Left-stick orientation completion, 23 October 2026

User priority: finish only the left-stick orientation reference and its settings
surface, then leave the mod ready for one more functional test. No native ABI,
IK, calibration or stereo boundary was touched.

What was actually unfinished: `movement_orientation = head|controller` (default
`head`) and `movement_controller = non_dominant|left|right` already parsed,
persisted and steered, but Controller mode treated a missing reference as a
reason to deaden the stick. In `src/core/input.cpp` the selected hand's heading
is now an optional overlay on a reference that always exists: when that hand is
untracked, aimed vertically with no stable planar heading, or non-finite, the
head reference carried by the same fresh locomotion packet keeps steering. The
rearm latch was narrowed to gate the hand heading only, so a reacquired hand
cannot yank a player already travelling head-oriented. A stale or
epoch-mismatched locomotion packet still neutralizes artificial translation -
that safety property is unchanged and still asserted.

Deliberate policy reversal, recorded rather than hidden: the runbook previously
required "no silent HMD fallback" and four `tests/test_input.cpp` assertions
pinned a dead stick on hand loss. The requested contract is the opposite, so
those assertions and the runbook row were rewritten to the new behavior instead
of being relaxed to stay green. The superseded wording is recoverable from git
history for this file; if the old no-fallback rule is ever reinstated, this
section is the reason it was changed.

Surfaces now carrying the setting: DSVR-Setup gained "Left stick oriented by"
(Head default / Controller hand) and "Hand" (Non-dominant / Left / Right) combo
boxes read and written through DSVR.toml; the mod menu shows the same pair with
the fallback explained; `ConfigSave` writes both keys with inline comments.
Defaults stay Head, so a fresh install is head-oriented smooth locomotion.

Receipts: `cmake --build build --config Debug --parallel 4` and the same for
Release exit 0; `ctest --test-dir build -C Debug --output-on-failure` 88/88 and
`-C Release` 88/88; `ctest --test-dir build -C Release -R "^input$" --repeat
until-fail:50` 50/50 (Debug likewise). `pwsh -File tools/auto_verify.ps1` exit
0, doctor PASS, refreshed `dist\DSVR-latest-x64.zip`. Release `DSVR.dll`
SHA-256 `2156b538d7aaa4f37f8ffcffbd31894b1758a468a2a6ff7c6e10cd9b13870970`,
deployed to the game directory and re-hashed identical; package ZIP
`a5e08b4d9f8c2c92cd86d11581483f6f2e2656d6c186f470c1d563c1eb94f82c`. New
regression `MovementAlwaysOriented()` in `tests/test_input.cpp` covers
head-default with no controller tracked, a never-tracked selected hand, hand
reacquisition after a neutral rearm, and mid-stride hand loss; the fake runtime
gained `right_pose_valid` so the right grip can be invalidated independently.

Next probe, and the one thing the CPU fixture cannot qualify: run the rewritten
"Reference stability/preferences" row in `docs/VERIFICATION_RUNBOOK.md` on the
deployed candidate. Hold forward, drop the steering hand out of view, and confirm
travel continues head-oriented instead of stopping, then re-acquire and confirm
the heading only changes after the stick returns to neutral. The fixture proves
the switch happens; only a headset verdict says whether the hand-to-head switch
is comfortable mid-stride, which is the open question for this feature.

### Focused melee/body-transfer pass, 22 October 2026 (paused by the pass above)

User priority: physical melee and native body transfer / independent visual fit;
stereo work is deferred for this pass. Preserve the mixed dirty checkout and
the previous verified candidate; no native write is authorized by a guessed ABI.

Bounded questions: (B) where do EDI at `142E191AE` and ESI at `14035F910`
originate, and do their callers carry a real melee context? (F2/F3) what is the
source->body index/proportion transfer, and which owner can receive tracked
corrections without changing physics or losing attachment updates? Expected
evidence is instruction-grounded producer/consumer semantics, checked against
retained native palettes where possible. Rollback boundary: this pass's own
source/test or diagnostic edits only; preserve captures and primary Ghidra DB.
Do not enable damage, drop the alias/root guards or relabel world scale as mesh
fitting to get past an unresolved boundary.

### Native-contract continuation, 22 October 2026

Objective: resolve difficult F2/F3 body ownership and D/B integration boundaries,
not add more cosmetic settings. Primary Ghidra GUI bridge is live; REA native
provider was checked once and remains unavailable under its Windows security
gate. No bypass or primary-project edits. Program revision at entry:
`1792646701740:aeb52525:2664`, unsaved changes false.

Completed bounded check: aliasing `player+0x850` assembly palette data to the
selected model palette does not alone prove that both use the same root.
Static `FUN_140357a20 -> FUN_140321f80 -> FUN_14020bec0` supplies the model root
to `FUN_140207f10`, which copies it into assembly `+0x20` if its twelve floats
changed (`FUN_140209820`). Added a read-only coherence guard in the already
approved body transaction: mismatched/non-finite assembly roots must refuse the
palette commit; signed-zero-equivalent roots remain valid. Both root copies
are rechecked at commit. Neither root is written to make the check pass.

Root regression first failed as expected (`root-coherence-red.log`, line 421).
The first implementation then failed the original nonzero-root case (line 376):
it compared assembly translation against the translation-free solver copy.
Corrected the comparison to full `root_before`, not a relaxed assertion.
`root-coherence-green.log` passes; four focused Debug tests passed 50 runs each
(`arm-repeat.log`). Paths are under `build/verification/20261022-native-contracts`.

**Important native blocker found while checking retained evidence:** every one
of the 32 run-7 after-copy samples has **distinct source/body palette pointers**.
Existing `output data mismatch` gate refuses that real pattern; aliased adapter
fixtures do not demonstrate current native retargeting. The capture serializer
also reads assembly `root` at +0x30 instead of +0x20 (`native-pose-observe.py`,
lines 85-91), so saved `asm.root` is incomplete/mislabelled. Do not treat its
raw equality failures as native divergence. Captures/script were not changed
or rerun. Audit receipt: `retained-root-coherence.json`; limitations included.
The model-space numerical fixture remains valid; complete assembly roots and
source->body index/proportion mapping remain research requirements. Native reach
measurement is downstream of the alias gate, so this path can retain fallback
scale rather than automatic fit. Details: [arm evidence](docs/ARM_IK_EVIDENCE.md).

Stereo advance: raw executable bytes + PE unwind bounds recovered the omitted
`1411CD8A0` allocation/cleanup tail through RET `1411CE028`. It allocates contexts,
polls two atomic counters before further item/parser teardown, and returns an
integer value; Ghidra's void/no-return pseudocode omits important lifetime work.
This is **not** a certified view-only replay bracket or GPU completion fence.
Exact sites and the counter-owner probe: [stereo contract](docs/NATIVE_STEREO_CONTRACT.md).

Melee advance: bounded setter-callsite classification eliminated zero/max-HP
paths and identified two split HP+delta predecessors; delta origin/defenses and
ordinary-hit correlation are still unknown. Several analysis CALL xrefs are
actual JMPs. Do not invoke continuation fragments or repeat the timed-out global
MOV/HP scan. Exact addresses: [melee contract](docs/NATIVE_MELEE_CONTRACT.md).

Local-only static instruction receipt (13 bounded ranges):
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\native-contracts-20261022T211921Z.json`,
SHA-256 `b59ab7ce0e9cbbb1e9370e7acdde082814e6d4e96d975631874f6adc051ef324`.
Pinned EXE hash was independently checked before decoding. This is static
evidence, not an executed contract. No primary database mutation/save.

Native test readiness: `tools/verify_preflight.ps1 -OutDir
build/verification/20261022-native-contracts` exited 1: installed loader/core are
stale and desktop active. Receipt `preflight-20261022-170939.txt`. No deployment,
focus, game launch or new native execution evidence from this continuation.

### Current continuation candidate and final receipts

- `cmake --build build --config Debug --parallel 4`: exit 0.
- `ctest --test-dir build -C Debug --output-on-failure --output-log build/verification/20261022-native-contracts/debug-ctest.log`: **88/88**, exit 0.
- `ctest --test-dir build -C Debug -R "arm_ik_math|arm_pose_math|arm_ik_naturalness|arm_pose_native" --repeat until-fail:50 --output-on-failure --output-log build/verification/20261022-native-contracts/arm-repeat.log`: **50 runs each**, exit 0, including the final distinct-body refusal regression.
- Release rebuilt/tested by `pwsh -File tools/auto_verify.ps1`: exit 0,
  **88/88**, doctor PASS. Final Release receipt `build/latest-ctest.txt`;
  refreshed `dist/DSVR-latest-x64.zip`, built UTC `2026-10-22T21:26:21Z`.
- Final `DSVR.dll` SHA-256:
  `7d4bdea96bf70db54694f384e388c50a867304e69109efaac1f67b7e85a488c2`.
  ZIP SHA-256: `6aff8337f27d9586abc38eb11683ae531ff931f0af295222577a5cb1fddab81e`.
  Independently hashed ZIP's DLL, Release DLL and verification JSON; all agree.
- Fixture provenance command still passes (960 model floats byte-identical).
  `git -c core.whitespace=cr-at-eol -c core.safecrlf=false diff --check`: exit 0.
  Final Ghidra revision unchanged, unsaved changes false.
- Updated AGENTS, focused evidence notes, execution ledger and comments at the
  alias/root/stereo/combat boundaries for Qwen. No blanket staging, commits,
  pushes, install changes or native/headset runs. Prior mixed user work remains.

Verdict: **offline safety regressions PASS; F2 transfer, F3 independent mesh
fit, B/C physical damage and D native stereo remain unresolved**. The retained
native palette mismatch is an explicit blocker, not something hidden by the
green suite or the refreshed development package.

### Prior coherent-calibration task, 22 October 2026

Objective: finish F1 solver regressions and implement approved automatic coherent
calibration (focused [design](docs/superpowers/specs/2026-10-22-coherent-calibration-design.md)).
Baseline: ded831f plus the pre-existing dirty work; design-only commit 271bd47.
At entry, Debug and Release built. Debug baseline: 85/87 tests; Release: 86/87.
`arm_ik_math` failed three opposite-bend cap assertions; Debug `arm_pose_native`
also intermittently failed unchanged-pose equality at line 604. Both are fixed.
Rollback boundary: task-owned IK/calibration/camera/input changes only; preserve
all prior dirty work. Native render-scale, collision and stereo bindings will
not be guessed. Expected observation: exact bend cap, fixed-point held poses,
one shared metre conversion without hand-only compensation, unchanged bases.
Codebase graph tools are unavailable in this session; direct source/caller
inspection is the fallback. writing-plans skill is unavailable; execution stays
under the existing guide's F1/F3 task order.

Native conversion remains fixed. This turn changed source and ran offline
verification; it did not install, launch DSR or run a headset session. No
restoration or live performance claim follows from these tests.

The checkout still mixes earlier work with this task. Preserve it. The only
commit initially made by this turn is design-only 271bd47; do not stage the
entire dirty tree or assume the documentation consolidation is newly generated
code evidence. Fixture numerical provenance is rechecked in ARM_IK_EVIDENCE.

### Prior coherent-calibration result and verification

- Fixed antipodal FABRIK cap bypass and the zero-bend conditioning cusp;
  retained exact link lengths, authored naturalness thresholds and independent
  hand orientation. Pole smoothing is bit-stable for steady inputs and cannot
  advance on duplicate callbacks. Clavicle refusal preserves prior bend state.
- Added `calibration_fit.h`: automatic reach-based whole-world/native-avatar
  apparent sizing plus manual trim. The applied camera snapshot supplies native
  height and scale to arms/grips/roomscale. Removed shoulder-local hand gain.
  Native measurements are frozen per rig and replaced on resource/topology
  rebind; a stale model revision refuses the fitted native write.
- T/A anatomy estimates use full controller geometry, not horizontal hand
  spread or the lowered hand plane. Shoulder location remains an explicit
  anthropometric estimate. Old profiles survive; explicit recapture is advised.
- Added boundary regressions for mapping, model replacement, basis preservation,
  camera/roomscale snapshots, calibration changes, and held/opposite poses.
- `cmake --build build --config Debug --parallel 4`: exit 0.
- `ctest --test-dir build -C Debug --output-on-failure --output-log build/2026-10-22-calibration-debug-ctest.log`: **88/88**, exit 0.
- `cmake --build build --config Release --parallel 4`: exit 0.
- `ctest --test-dir build -C Release --output-on-failure --output-log build/2026-10-22-calibration-release-ctest.log`: **88/88**, exit 0.
- Final focused repetition: `ctest --test-dir build -C Debug -R "arm_ik_math|arm_pose_math|arm_ik_naturalness|arm_pose_native" --repeat until-fail:50 --output-on-failure --output-log build/2026-10-22-fabrik-repeat.log`: 50 runs each, exit 0.
- `python tools/verify_native_pose_fixture.py build/native-pose-run7-snapshot.json`:
  exit 0, 960 floats byte-identical; a deliberately altered temporary fixture
  was independently rejected (negative-control harness exit 0).
- `pwsh -File tools/auto_verify.ps1`: exit 0; final Release **88/88** and doctor
  PASS; refreshed `dist/DSVR-latest-x64.zip` and its verification JSON. Final
  Release suite receipt: `build/latest-ctest.txt`. No install or game launch.
  Prior packaged `DSVR.dll` SHA-256 (superseded by the continuation):
  `edb591a163af10058feafffea2598f657b938164871cb1c85377a31e7c52bdca`.
- Toolchain: Visual Studio 18 2026, MSBuild 18.7.8, Windows SDK 10.0.26100.0,
  OpenXR 1.1.49. Doctor confirmed pinned DSR executable SHA-256
  `a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`,
  game closed and existing firewall block. These checks do not establish a
  current protected-save transaction or qualify the previously installed mod.
- `git -c core.whitespace=cr-at-eol -c core.safecrlf=false diff --check`: exit 0.
  Do not disable CRLF normalization to diagnose whitespace; Windows CRLF then
  appears as thousands of false-positive trailing spaces. No mass rewrite made.
- `AGENTS.md` is the auto-discovered OpenCode/Strata handoff. Comments explain
  the scale, pose-continuity and stereo integration traps where they occur.
  Source and mixed pre-existing changes remain uncommitted; no blanket staging,
  reset, install replacement or history cleanup was performed by this turn.

Verdict: source/offline regressions PASS. Independent native mesh resizing,
full stature/reach proportion fit and rendered/headset acceptance remain OPEN.
Global apparent sizing is not proof of an independently resized skinned body.

## Evidence boundaries

- Existing source covers XR mono, tracked view/input, calibration, native pose adaptation, equipment reads, contact math and menu/config. Head/Controller movement selection has recent implementation; inspect current code rather than redoing an older baseline.
- Production same-simulation native stereo and physical native damage/block/parry remain unaccepted. Diagnostic .9 m blade/proxy contacts are not real equipment/target integration.
- Native motor ownership/resolved-motion attribution, head/world queries, crouch collision/hurt volumes and final body/headset qualification remain open. Arm naturalness changes are not headset acceptance.
- Static melee research has contact and HP-setter leads but no complete runtime-correlated melee ABI. See [contract](docs/NATIVE_MELEE_CONTRACT.md). Read-only Ghidra fallback is documented; historical REA Windows-provider unavailability should be checked, not repeatedly retried unchanged.
- Historical performance and test totals describe different candidates/routes. Re-establish comparable timings and actual suite totals on the current checkout.

## Exact resume action

Do not restart broad reconnaissance. For F2, the `140207F10` source->body
transfer is recovered (69-entry identity map plus proportion records for the
captured resource; see `docs/ARM_IK_EVIDENCE.md`), and the before-copy source
seam is implemented with offline regressions. The next F2 step is the live
probe: the runbook "Verified native copy publication" row, driven by
`tools/run_pose_capture.ps1` with observer re-anchor
`pose-transfer-20261023-140810` watching the publication gate. Do not
hardcode the captured identity map or proportion constants for other
equipment variants or animation states; capture per-variant contracts first.
Assembly roots stay at +0x20. Do not remove the copy-verification gate to
manufacture a live write. Observe the live gate through the `ArmIK: native
body copy verified=... refused=...` log lines and the retained observer
`build/verification/embodiment-wrap/dsvr-pose-transfer-observe.py`, inside
the protected `tools/run_pose_capture.ps1` transaction.

Independent D probe: find the producers/owners of the two counters polled at
`1411CDE10..46` and the natural executant `1411C2F50`; correlate camera/plan and
storage retirement before proposing replay. Independent B probe: trace EDI at
`142E191AE` or ESI at `14035F910`, or correlate natural encounter outcomes under
TESTING. Keep mutation bindings disabled while their contracts are unknown.
Reconcile current package/preflight and desktop activity before native work;
do not ask for headset qualification of absent stereo/crouch/damage bindings.

No deployment/restoration state was inferred during this turn. Inspect active process/transaction receipts before launching; do not assume an old Restored receipt describes present machine state. Useful protected runs are authorized under TESTING; no new lab account is required. Human comfort still needs the operator; publication requires separate authorization.

### Animation ownership and the +0.2 m calibration height trim, 25 October 2026

Operator request, two parts, both offline-verified only.

**Height trim.** Every standing calibration read short in-headset. One named
trim, `kCalibrationHeightOffsetM = .2f` in `src/core/calibration_fit.h`, raises
the applied view height and the head-fit denominator together, so the camera
stays on the native Head bone while the whole avatar and world grow together
relative to the wearer's body. It is applied inside `ResolveCalibrationMapping`
only: the saved `UserCalibration.eye_height_m` still reports what was captured,
older profiles stay usable, and head, hands and roomscale keep consuming the one
applied snapshot. `tests/test_calibration_fit.cpp` and
`tests/test_camera_observer.cpp` expectations moved with it; the head-fit
assertion `eye_height == native_head_height` still holds, which is the proof the
trim does not float the camera above the head shell. Side effect to watch: the
avatar's limbs lengthen relative to the wearer's body, so the IK naturalness
budget absorbs the difference - reach-gate refusals are the symptom.

**Animation ownership.** New startup-tunable `arm_ik_block_animation` (default
true, `[combat]` in DSVR.toml, plus a Settings row). In `RetargetSourcePose` the
IK-owned rows are reset each frame to `g_source_reference`, the first palette of
a rig revision whose owned rows are all well-formed, read before any correction.
The native writer still runs, so animation state, event counters, legs,
equipment and every non-owned row stay native and the natural-copy verification
is unchanged. `animated` keeps the true palette image for the after-read
re-verification, the rollback and `g_pending_copy.source`; the commit now writes
an owned row whenever it differs from what is in memory, not from what the
animation asked for.

Two guards found while wiring it, both worth keeping:

- Substituting before the guards made a zeroed native Head row invisible: the
  torso solve accepted the frozen row and published over a palette the fixture
  expected to be refused (`arm_pose_native` FAIL 783). Only a well-formed
  `animated` row may be replaced.
- Freezing the source inside the grounded-upright stature gate meant the block
  never engaged at all: the memory fixture's palette fails that gate, so
  `g_source_reference_valid` stayed false, and the fixture's exactly-one-write-
  per-published-frame invariant proved the substitution was inert. The gate is
  now reported in the freeze log rather than required.

Receipts: `cmake --build build --config Debug --parallel 4` exit 0;
`ctest --test-dir build -C Debug` **94/94**; Release rebuild then
`ctest --test-dir build -C Release` **94/94**. No commits, deployment, game
launch or headset qualification.

Experiment before believing either change. Animation: set
`arm_ik_block_animation = false`, watch the arms through a canned RB/RT swing -
with the block on they must stay on the controllers for the whole animation, and
the freeze log line must name the rig revision. If that line never appears the
block is inert for that rig, and the owned-row set is the first thing to check.
Height: find a doorway the operator passes standing; at +0.2 m it should need a
slight duck where +0.0 did not. An avatar that still reads short with the trim
on means the fit is not head-anchored (check `embodiment_fit` and the
`CalibrationFit::Arm` fallback), not that the trim is wrong.

### The neck question: keep the live neck solve, delete the head rider, 25 October 2026

Operator asked whether the neck bone system earns its keep. Two things carry the
word "neck" here, and only one of them was live.

**Kept.** `SolveTorsoPose` (`src/core/body_pose_math.h`) bends the authored
spine->chest->neck chain for lean and finishes with a rigid rotation about the
native Neck pivot that carries the whole Neck subtree, so the captured
Neck->Head link length is preserved and the actual skull axes turn. It runs every
frame from `src/core/arm_ik.cpp`, and Neck is in the owned-bone set, so under
animation ownership the neck is moved only by the wearer's own head. Its
regressions are `CheckNativeNeckFrame` (`tests/test_body_pose.cpp`) and the
Neck->Head link-length assertion in `tests/test_arm_pose_native.cpp`. Worth
keeping: it is the only thing that makes a visible neck follow the head at all.

**Removed.** `src/core/head_rider.h` (`HeadRiderOffsetModel`, `HeadEyePoint`)
translated the solved Head bone alone onto an eye point dropped below the
rendered camera. Its own header already said it was unused since 23 October and
that the approach was unfixable in place: the tracked eye is not a measured skull
pivot, so the shift stretched the authored Neck->Head link by up to its .35
clamp, and the eye offsets were guesses rather than captured geometry. Nothing in
`src/` called it; only `tests/test_head_rider.cpp` kept it green. A passing suite
for a superseded approach is how the next session gets talked back into it, so the
header, its test and both CMake entries are gone.

The matching producer went with it: `ArmIkHeadWorldDelta`, its
`HeadDelta`/`g_head_delta` state, the per-commit publish block in
`RetargetSourcePose`, and the `tests/test_camera_writes.cpp` stub plus its
`head_delta_reads` guard. That getter existed to hand the camera the solved
head's world displacement; the superseded history records that doing so detached
the viewpoint from the body in live testing, and the surviving header comment
said "must not feed the camera" while still exporting the symbol. An unused
export whose only documentation is a warning is a footgun, not an API. The
camera-side guard is now structural: the symbol does not exist.

Receipts: Debug and Release rebuild clean; `ctest --test-dir build -C Debug` and
`-C Release` now **93/93** (was 94/94 - `head_rider` was the deleted test, and
`ctest -N` is the source of registered names).
`git -c core.whitespace=cr-at-eol -c core.safecrlf=false diff --check` clean. No
commits, deployment, game launch or headset run.

Experiment before trusting the deletion: stand still and turn your head 90
degrees. The model's neck and skull must turn with the gaze - spine and chest
carry their shares, the Neck-subtree rotation finishes the turn - with no
stretched Neck->Head gap and no pop at the snap cap. If the neck now stays frozen
while the view turns, what is missing is not the rider; it is the Neck-subtree
rotation, and the first thing to check is whether `g_rig.neck` bound at all
(`ValidRig` requires `parents[head]==neck`), then whether the torso solve refused
that frame.

### Gaze-locked torso, torso/leg hiding, and the menu pointer beam, 25 October 2026

Operator request: the physical body, arms and weapon must face where the head
looks, the right stick turns the body the way the left stick already orients
locomotion, small head tremor must not drag the character, the torso and legs can
be hidden while arms and weapon stay, and the spatial menu pointer needs a visible
beam and smoothed motion.

**Gaze lock.** `TorsoTargetYaw` (`src/core/body_pose_math.h:456`) now maps the
head's planar yaw relative to the heading basis through
`kTorsoLookDeadbandRad = 0.035` (~2 degrees) of slack and a soft ramp that reaches
`kTorsoTwistLimitRad = 0.8` exactly at the twist cap, then `DampedAngle`
(`kTorsoDampTauMs = 90 ms`, `kTorsoDampDeadbandRad = 0.003`) approaches it. Below
the deadband a glance does not move the body at all; the ramp is what keeps a
90-degree glance from snapping the spine straight past its authored limit. The
right stick does not fight it: turning still goes through `TurnTracker` ->
`VrHeading::ApplyTurn`, the same latched path the left stick's head-directed
locomotion consumes, so the stick re-points the heading frame and the gaze keeps
the body facing inside that frame. Arms and weapon inherit the result rather than
tracking yaw separately, because `WeaponGripSolveTwoHand` and
`OrientModelHandPose` consume the applied body basis.

**Torso and leg hiding.** `hide_torso_and_legs` (`src/common/config.h:114`, menu
row "Hide torso and legs (arms and weapon stay)") sets native part bits 3 (body)
and 5 (legs) in `FilterMask` (`src/core/body_visibility.cpp:100`). Bit 4 (arms) is
never set and the primary weapon model is not read by this filter, so arms and
weapon keep drawing. `hide_head` stays a separate key: face, head gear and hair
(bits 0, 1, 2, 6) are not entangled with the torso request. The dirty-marking gate
previously tested `applied.filtered`, which conflated "some part was filtered this
frame" with "this part is hidden", so a mask change could go unmarked; it now keys
on `applied.mask`.

**Pointer beam and smoothing.** `ProjectMenuPoint` (`src/core/menu_presentation.h:101`)
projects the aiming hand onto the panel plane in the panel's own u/v, clamped to
the quad; `ProjectMenuPointer` publishes it as `beam_x`/`beam_y`/`beam_valid` and
`DrawPointerBeam` (`src/core/mod_menu.cpp:266`) draws the segment from that tail
to the cursor. The beam has to live in the panel's own texture because the menu is
a compositor quad - there is no world-space line to draw. `MenuPointerSmoother`
damps pointer motion with the same exponential the torso filters use: sub-pixel
tremor under `kMenuPointerEpsilonPx = 1.2` is dropped, a move past
`kMenuPointerSnapPx = 64` lands at once, and a gap past `kMenuPointerGapMs = 200`
re-primes instead of sliding across the panel.

Root cause worth keeping: the first smoother returned early when
`now_ms <= last_ms_`, so two frames inside one millisecond tick never advanced the
filter and the cursor froze mid-sweep. `mod_menu_render` caught it as a missed
Save click, not as a visual bug, which is why the failure looked like a UI
hit-test problem. The clock now advances on every sample and the epsilon/snap
tests run before the elapsed-time guard.

Receipts: Debug and Release rebuilds clean; `ctest --test-dir build -C Debug` and
`-C Release` **93/93**. Release core deployed to
`C:\Program Files (x86)\Steam\steamapps\common\DARK SOULS REMASTERED\DSVR.dll`
(SHA-256 `04cd62d725312ae05ec5bfa4b5cf6e3d1e8007ed46263ed4c51f4ac85e15a154`),
previous DLL backed up to `%USERPROFILE%\.dsvr-backups\pointer-lock-20261026\`.
`build\src\setup\Release\DSVR-Setup.exe --doctor` exits 0, so the shared-header
changes left no stale Release object. `tests/test_config.cpp` now round-trips
`hide_torso_and_legs` and `arm_ik_block_animation` away from their defaults, so a
dropped parse or save line fails the suite instead of silently reverting to the
default. No commits, no game launch, no headset run. An untracked
byte-identical copy of `src/core/arm_ik.cpp` sits at the repository root
(`arm_ik.cpp`, `fc /b` reports no differences); it is outside every CMake target
and was left alone for whoever made it.

Experiment before believing any of it: look 45 degrees off your travel direction
and hold it - the body must rotate toward the gaze and settle inside the twist
cap, not oscillate, and a 1-degree head tremor must move nothing. Then flick the
right stick: the heading, not the torso twist, has to move. With
`hide_torso_and_legs` on, the arms and weapon must stay tracked while the torso
and legs are absent; if the arms go limp, the owned-bone set is dropping arm bones
with the spine rather than the filter excluding only bits 3 and 5. For the menu,
hold your hand still and watch the cursor: it must not crawl, then sweep across
the panel: the beam must stay attached to the cursor and end at the projected
hand, never lag behind it.

### Live part-visibility toggles from the mod menu, 25 October 2026

Operator request: unhiding the IK torso and legs has to be possible on the fly
from the mod menu, not only by re-applying a saved profile.

**What was missing.** The two visibility rows were draft edits: the tick only
reached the live config through the footer Apply/Save buttons, and the restore
depended on `ModelUpdateDetour` happening to notice that the mask changed on a
model it already knew. Hiding looked instant because the queue command that hid
the parts was already in flight; un-hiding had to wait for a different native
update, so it read as one-way.

**Added.** `BodyVisibilityRequestRefresh()` (`src/core/body_visibility.cpp:226`)
asks the native updater for a fresh parts command for the local player right
away. A queue command already pending (the dirty bit set) counts as refreshed,
because the detour applies whatever mask is live when the native updater
re-reads the records. The Settings rows now apply on tick
(`DiagnosticsRequestConfig(g_draft,false)` plus the refresh request), so hide and
unhide both land on the next native update with no reload and no reopening the
menu; Save still persists. The panel prints `filtered / policy refreshes /
refused` so the transition is visible instead of inferred.

**Tightened while there.** The re-verification inside `QueuePartsDetour` tested
`!FilterMask()`, so a toggle between reading the part records and handing the
scratch buffer to the native queue passed a buffer built for the previous mask.
It now requires `FilterMask() == mask` and otherwise lets the native game have
its own unfiltered copy. The block's indentation also made the re-verification
`if` look like the body of the preceding `for`; it never was.

**Unchanged on purpose.** The menu-canvas gate in `FilterMask` stays: while the
spatial canvas is up (title screen, DSVR menu, calibration) the filter is
intentionally off and the whole body draws, which is what the pinned
`theater = true` assertion in `tests/test_body_visibility.cpp` records. The
change therefore appears the moment the menu closes, and the panel text now says
so rather than leaving the operator to guess why nothing changed on screen.

Receipts: Debug and Release rebuilds clean, **93/93** in both. New coverage in
`tests/test_body_visibility.cpp`: part slots 3 (body) and 5 (legs) masked on their
own, slot 4 (arms) and every other record intact, the head set and torso/leg set
combining, untick restoring the geometry, the live records never written, and the
explicit refresh producing a dirty-flag write. Release core deployed to the game
directory, SHA-256
`92f4e0052caa1de2c0e58e8df15f477a2747aae4ff633c38744dd59ee629fad8`; the previous
DLL is at `%USERPROFILE%\.dsvr-backups\pointer-lock-20261026\DSVR-before-unhide.dll`.
`DSVR-Setup.exe --doctor` exits 0 after the shared-header change. No commits, no
game launch, no headset run.

Operator standing direction this session: the latest build always lands in
`dist/`. `pwsh tools/auto_verify.ps1` ran after the green Release suite and
refreshed `dist\DSVR-latest\`, `dist\DSVR-latest-x64.zip`,
`dist\DSVR-latest-verification.json` and `dist\DSVR-latest-ctest.txt`
(`mode=latest-dev-test`, `ctest=93/93`, `doctor=pass`, built
`2026-10-25T05:25:35Z`). `dist\DSVR-latest\DSVR.dll` hashes identical to the DLL
deployed to the game directory, so the test package and the installed mod are the
same bytes.

Experiment: tick "Hide torso and legs", close the menu - torso and legs gone,
arms and weapon still drawn and still tracked. Reopen, untick, close: the same
geometry returns with no reload. If it does not return, read the status line
first: a rising `refused` means the actor binding changed under the toggle, not
that the mask is wrong, and the thing to chase is `ReadBinding` identity, not the
part bits.

### The physical attack and defence bridges: swings and shields drive native buttons, 25 October 2026

Operator request: "Get Physical attacks working." Nothing in this log recorded
that unit, so it is written up here together with the defence half.

**What was actually missing.** With IK arms applied and animation ownership
taken, `MapVrPad` nullifies every canned combat output
(`src/core/pad_math.h:166-177`): weapon squeeze/trigger RB/RT, off-hand squeeze
LB, off-hand trigger LT. That suppression is correct - a canned animation drives
the same bones the IK solver owns - but it left the player unable to attack,
block or parry at all. The arms were solved and the game never heard them. The
gap was not native contact damage, which is still an open gate; it was that a
physical arm produced no input.

**Two bridges, one shape each.** `src/core/swing_attack_math.h` turns a
qualified weapon-hand swing into exactly one press of the native light/strong
attack: rest, then armed, then a swing above the configured band, one press,
then a cooldown; a swing that never comes back to rest never re-fires.
`src/core/shield_guard_math.h` is its mirror for the shield hand: a raised hand
held still for `guard_settle_ms` holds the native shield button for as long as
it is held, and a short fast impulse that returns to rest taps the same button.
DSR already separates a block from a parry by press length alone, so neither
bridge invents a native binding - only press windows. Guard windows, stamina,
chip damage, parry judgement and ripostes stay the game's and stay unqualified.

**The guard is a height band, not a pose.** `src/core/input.cpp:67-72` measures
the shield hand head-relative (`kGuardHandBelowHeadM` 0.85,
`kGuardHandAboveHeadM` 0.5), so the block follows the player wherever they hold
the shield, overhead or at the belt. What makes it a guard rather than a wave is
that it is held still, so the settle window is the entire pose test and no
canonical guard pose is encoded anywhere. Hand velocities are head-relative, so
roomscale walking moves head and hands together and cannot read as a flick.

**Root causes worth keeping.**
- The first attack trigger re-fired repeatedly on one continuous swing: the
  press was released after its window while the swing condition was still true,
  so a single swing became a stream of attacks. One press per swing needs a
  per-swing latch, not a per-press cooldown.
- The guard required the shield hand to be raised on the frame the flick
  *ended*, which silently rejected every downward flick. A downward flick is a
  parry in a direction like any other, and the hand is usually still coming down
  when the impulse finishes. The raise test now happens at flick start
  (`flick_raised_`, `src/core/shield_guard_math.h:104`).
- A parry press may only begin from a released button (`release_ms` after
  `block_off_ms_`). Without that the game reads a continuous hold, and the
  "parry" is a block with extra steps.
- A pending parry whose released button never arrives expires rather than
  queueing: a stale tap landing during a later guard is worse than the parry
  that never happened.
- Near-opposite flick directions and single-frame tracking spikes are handled by
  `flick_settle_ms`/`flick_return_ms` plus a peak band, so a 12 m/s backhand is
  classified as a swing with the shield in hand, not a parry.

**Configuration and menu.** Five keys, default off, parsed/saved/validated at
`src/common/config.cpp:223-231`, `:479-485`, `:528-531`:
`physical_defence`, `guard_settle_ms`, `guard_flick_speed_min`,
`guard_parry_press_ms`, `guard_parry_cooldown_ms`. `GuardParamsFromConfig`
(`src/core/input.cpp:392-405`) keeps the bands ordered no matter how the sliders
are crossed: the flick bar stays above the still band, the ceiling above the
floor, the cooldown at least twice the tap. The Combat panel gained the toggle
plus four sliders and a plain-language description of what the body has to do
(`src/core/mod_menu.cpp:498-506`).

**Receipts.** `cmake --build build --config Debug --parallel 4` then
`ctest --test-dir build -C Debug --output-on-failure`: 95/95. Same pair for
Release: 95/95. No new compiler warnings from the changed files. New registered
test `shield_guard`.
`tests/test_input.cpp:664-800` (`PhysicalDefence`) drives the whole chain
through the published pad rather than the state machine alone: guard held open,
hand at the side cannot guard, one flick is one tap and then the guard returns,
a second flick inside the cooldown cannot tap, a hand that keeps moving is a
swing, motion between the still and flick bands drops the guard without
parrying, roomscale walking neither drops the guard nor flicks, tracking loss
releases and re-engages, the weapon hand cannot tap the shield button, the
left-hand shield behaves identically, and eleven gates (disabled, uncommitted,
log-only, no animation ownership, wrong epoch, sanctuary, menu capture,
calibration capture, camera not gameplay-ready) emit nothing and the guard
resumes by itself once a gate is released. `tests/test_pad_math.cpp` pins that
the guard survives `suppress_weapon_attacks`, never reaches the canned LB block,
and is dead during native menu navigation. `tests/test_config.cpp` round-trips
the five keys and pins their defaults. `pwsh tools/auto_verify.ps1` ran green
afterwards and refreshed `dist\DSVR-latest\`, `dist\DSVR-latest-x64.zip`,
`dist\DSVR-latest-verification.json` (`mode=latest-dev-test`, `ctest=95/95`,
`doctor=pass`, built `2026-10-25T06:42:13Z`). The Release core was deployed to
the game directory and hashes identical to `dist\DSVR-latest\DSVR.dll`
(`c004d57a…02b00e`); the replaced DLL is at
`%USERPROFILE%\.dsvr-backups\physical-defence-20261025\DSVR-before-defence.dll`.
No commits, no game launch, no headset run.

**Still open, and the probe for it.** No native damage is bound and nothing here
is headset-verified. The presses are ordinary XInput-shaped lines at the same
seam as every other motion button; whether DSR's own guard windows accept a
90 ms tap as a parry is a headset question. The log line
`Combat: shield flick at %.2f m/s -> parry tap` is the probe. Experiment: enable
both bridges with committed IK arms and animation ownership, raise the shield
hand and hold it still - the block should engage within ~120 ms and stay while
held; then flick that hand and stop. If the game blocks instead of parrying,
lengthen `guard_parry_press_ms` before touching the state machine, and if the
guard never engages check the hand height against the head before assuming the
settle window is wrong.

### The arm-swing gait detector: swinging the arms asks the game to run, 26 October 2026

Operator request, in the same breath as the combat bridges: sprinting should
also be possible by "swinging the arms in loose alternation in a running
motion repeatedly", while holding the dash as normal keeps working.

**What landed.** `src/core/arm_swing_math.h` is a head-relative two-hand gait
detector, the movement counterpart of `swing_attack_math.h`. A fast phase on
one hand counts as a stride only if it lasted `stride_ms_min`, stayed inside
the speed band, and started while the other hand was quiet; the swap count only
advances when consecutive strides change hands inside `window_ms`. Output is
one `sprint` flag plus a 0..1 `strength` ramped by `ramp_alternations`, so the
caller composes it with whatever locomotion axis it already has.

**Why that shape.** Three movements look like running to a speed threshold and
are not: one hand flapping (no swap ever happens), a two-handed swing or stretch
(both hands move together, which a running gait never does), and a weapon swing
(above `stride_speed_max`, and the attack bridge already owns it). The overlap
test is taken at stride *start* and marks the already-open partner phase too,
because a crossover inside one frame would otherwise cancel a legitimate stride
while a staggered two-handed stretch would otherwise pass. Same-hand repeats
and over-long gaps clear the count rather than carrying stale strides.

**Not wired yet.** Nothing feeds it and nothing consumes it: no `VrPadInput`
field, no config keys, no menu control. That is deliberate -- the composition
point for the forward axis and the dash button has to be chosen together with
the roomscale sampler so one applied snapshot owns both, and that is the next
unit, not this one.

**Receipts.** `cmake --build build --config Debug --parallel 4` then
`ctest --test-dir build -C Debug --output-on-failure` 96/96, and the same for
Release, both 96/96. `ctest -N` now lists `arm_swing` (#60). New test
`tests/test_arm_swing.cpp` covers: alternation engages and ramps to exactly 1.0
and never past it; one-hand flapping, both-hands-together (same-frame *and*
staggered onsets), below-band, above-band, and a fast phase held past
`stride_ms_max` all stay out; the run lingers for `hold_ms` then releases and
clears its count; one stride after a long gap does not resume it; a backwards
clock, a NaN and a negative speed clear the run and it recovers afterwards;
four impossible parameter sets refuse it; and the same hand history always
produces the same per-frame output. No commits, no deployment, no game launch,
no headset run.

**Next probe.** Add `arm_sprint` to `VrPadInput`, feed it from the existing
head-relative hand speeds at the same seam that feeds `swing_buttons`, and
compose it into the forward axis next to `roomscale_x/y` in `pad_math.h`. The
first thing to check in-headset is whether a real running arm motion actually
keeps the partner hand under `quiet_speed_max` at the crossover; if it does not,
lengthen `quiet_speed_max` before touching the state machine.

### Frame pacing is now measured, not assumed, 26 October 2026

Refresh rate is the display's active Hz; frame pacing is the *consistency* of
the interval between successive displayed frames. Matching the runtime's
requested cadence is only half of a smooth result: a title can average exactly
90 Hz and still judder if the intervals alternate 11.1/22.2 ms.

**What landed.** `src/core/frame_pacing.h` is a fixed-storage interval
distributor: 400 x 0.25 ms buckets to 100 ms, nearest-rank p50/p95/p99 read at
the bucket midpoint, mean/min/max, plus `long_frames`/`short_frames` counted
against 1.5x the window median and an `Evenness()` of
`1 - (p95-p50)/p50`. Absurd samples (non-positive, non-finite, over 100 ms: a
paused game, a modal menu, a session gap) are refused and counted, never
averaged in. `PacingMatchesTarget()` compares the measured mean against
`PeriodNsForFps()` from `display_rate.h` rather than inventing a second period
formula. `src/core/frame_rate.cpp` records the wall interval between
successive entries of the presentation-owner detour on the render thread (the
only writer, so no lock) and emits one `Pacing:` summary every five seconds,
tagged `NOT-MATCHING-RUNTIME-CADENCE` when the mean is not the runtime's.
Reset happens on owner change and uninstall, so a new session cannot inherit
the previous session's distribution.

**Why that shape.** K1 asks for p50/p95/p99 and frame drops; `cpu_frame_timer.h`
only carries mean and max, and `display_rate.h` proves the requested *value*
reached the native setter, not that the resulting intervals are even. Bucket
medians make one exact cadence read as that cadence instead of its lower bucket
edge. Classifying long/short at read time against the median, instead of at
Record time, keeps the result independent of arrival order. The detour measures
the game's submission cadence; the XR-reported refresh stays the display's
cadence, and the two are compared rather than conflated.

**Also fixed here.** `PresentOwnerDetour` forwarded four arguments into a
native callee whose full arity is not pinned. `tests/test_frame_rate_native.cpp`
already asserted that the fifth (stack) argument survives; it did not. The
detour and `PresentOwnerFn` now carry and forward the fifth argument.

**Receipts.** `cmake --build build --config Debug --parallel 4` then
`ctest --test-dir build -C Debug --output-on-failure` 98/98, and the same pair
for Release, 98/98. `ctest -N` lists `frame_pacing` (#64); `frame_rate_native`
(#4) and `display_rate` (#63) pass. New `tests/test_frame_pacing.cpp` pins: an
empty window invents nothing; a perfectly even 90 Hz window reports 90 Hz with
`Evenness()==1.0` and no long/short frames; 2:1 judder averages to a clean
60 Hz and *still* fails (`p50 < 15 ms`, `p95 > 20 ms`, 500 long frames,
`Evenness() < 0.2`) - that is the case a mean-only check would wave through; one
held present inside 999 exact ones is counted while p99 stays put; early
presents count as short, not long; five absurd samples are refused and counted
without moving the mean; min/max are real samples and the first sample seeds
min; the log interval drains while the cumulative window keeps running; reset
clears both; out-of-range percentile queries refuse. No commits, no deployment,
no game launch, no headset run.

**Next probe.** In-headset, read the `Pacing:` line at a steady scene. The pass
condition is `target=<runtime cadence>` with no
`NOT-MATCHING-RUNTIME-CADENCE`, `even` at or near 1.0 and `long=0`; a matching
mean with `even` far below 1.0 is judder, not a rate fault, so look at what is
holding presents (GPU time in the `RenderMetrics` line, the native limiter's own
history after a setter call) before touching `display_rate.h`. A
`NOT-MATCHING-RUNTIME-CADENCE` tag with a correct `RateMatch:` line means the
setter did not take effect on the owner - re-check `owner+0x60` and the
`ShouldApplyNativeRate` re-apply path rather than the measurement. `rejected`
growing while `frames` is steady means the game is stalling past 100 ms and the
window is honest about refusing to average it in.

### Menu/input failure and source recovery checkpoint

Latest supplied DSVR.log (06:07:46): both inline XInput hooks fail with MH_ERROR_MEMORY_ALLOC; GetState IAT fallback succeeds and is polled, but capabilities fallback refuses because no unique owned import exists. This supports a front-end controller-enumeration failure, not proof that locomotion math is the only cause. Right-stick look and reported smoothness do not qualify buttons/menu navigation. Next probe: establish the real capabilities provider/import ownership from pinned executable and existing captures; preserve refusal for foreign/ambiguous imports.

An assistant shell edit removed newlines from mod_menu.cpp. Reconstruction restored compilation, but subsequent token reconstruction altered formatting and some user-visible strings. Semantic recovery is NOT certified; audit this file against pre-incident history before deployment. No installed DLL was changed in this recovery turn.

Observed commands: cmake --build build --config Debug --parallel 4 (initial recovery attempts FAIL, final PASS); ctest --test-dir build -C Debug --output-on-failure PASS 98/98. Release build PASS and ctest -C Release PASS 98/98. pwsh -NoProfile -File tools/auto_verify.ps1 PASS, Release 98/98, doctor PASS; dist/DSVR-latest-x64.zip refreshed (1405900 bytes). Logs: build/menu-recovery-debug.log, build/menu-recovery-debug-tests.log, build/menu-recovery-release.log, build/menu-recovery-release-tests.log, build/menu-recovery-auto-verify.log. No game launch or new native evidence; no commit/push. Package is an offline candidate, not qualified release. H remains blocked and all native combat/hitbox acceptance remains open.

### Menu label regression and correction to capabilities diagnosis

Restored five damaged strings in CalibrationPanel, SettingsPanel, CombatPanel, DiagnosticsPanel and DrawMenu, including the hide-head checkbox label (also its ImGui ID). A source probe first failed for the missing original label. The real WARP-render test now captures shipped ImGui text from inside the body panel: reintroducing the corrupted label fails mod_menu_render 0/1; restoring it passes. Early experimental logging outside an active window caused two segfaults; that approach was removed. Buffer logging also proved unsuitable because End finishes and clears the buffer. The retained test uses file logging inside the body panel and clears its own temporary capture to prevent appended stale text from passing. No receipts under build/verification were modified.

Correction to the previous capabilities diagnosis: live Ghidra GUI inventory (DarkSoulsRemastered.exe) exposes XInputGetState and XInputSetState imports, but no XInputGetCapabilities import or defined string. GetState thunk 14082f790 references IAT 142017ed4. Callers 140E1C180 and 140E1C200 enumerate users 0..3 via GetState and add successful users via 140E1E5A0. Consumer calls 140C9F6EF, 140C9FB7A, 140E27FF6 and 140E28986 also use GetState; the first stores success in object +0xC. Thus the capabilities refusal is real but is NOT established as the cause of failed menus. Do not weaken ownership checks or invent a missing capabilities import. Receipt: build/xinput-owner-static-current.json. Next read-only probe: trace the enumerated-user records into button/menu consumers and correlate capture mode with the failed session. No game-memory writes or input injection occurred.

Final commands this turn: cmake --build build --config Debug --parallel 4; ctest --test-dir build -C Debug --output-on-failure; matching Release pair; pwsh -NoProfile -File tools/auto_verify.ps1. All exit 0; fresh full Debug 98/98 and full Release 98/98; verifier Release 98/98 and doctor PASS. Exact output: build/unresolved-debug-build.log, build/unresolved-debug-tests.log, build/unresolved-release-build.log, build/unresolved-release-tests.log, build/unresolved-auto-verify.log. Package refreshed, not deployed. Full source recovery, native controller usability, stereo, physical combat, mesh hitboxes and crouch remain unqualified; no plan checkbox closed.

### VR menu attempts and lifecycle failure, 25 October 2026 07:28–07:40 local

Formatted mod_menu.cpp with clang-format, disabling include sorting and string splitting; 9825 non-comment tokens exactly match the pre-format candidate. Current complete builds and suites passed: Debug 98/98, Release 98/98; tools/auto_verify.ps1 exit 0, Release 98/98, doctor PASS, development ZIP 1407772 bytes. Logs: build/route-debug-build.log, build/route-debug-tests.log, build/route-release-build.log, build/route-release-tests.log, build/route-auto-verify.log. This last candidate adds once-per-second InputRoute and XInputRoute diagnostics only, not a claimed input fix. It is not deployed while the game is active.

The operator accepted VR testing. The preceding verified core was snapshotted and deployed without setup or backup-vault access. Sessions live outside build/verification: build/vr-menu-session-20261025-072840-be2bb72d and build/vr-menu-session-20261025-073548-065d1cdf. Both save roots and Steam app/config metadata were copied and hashed. No backup-vault files or existing verification receipts were changed.

SAFETY FAILURE: the first launcher PID 18592 exited after about five seconds. The initial runner mistakenly treated its exit as game completion and restored roots at 07:28:52, before the real Steam-launched game ran. Thus the later operator feedback is NOT a qualified disposable-save transaction, and subsequent original-save writes cannot be claimed restored. The modified runner held its deadline across bootstrap exit, but the second attempt rejected replacement-process ownership and exited before its automatic finally recovery completed. After the game closed, manual recovery moved both held original roots back and individually verified their SHA256 values; test outputs were retained, and manual-recovery.json records this narrower claim. No newer user/Steam metadata was overwritten.

An unowned game PID 15888 subsequently started at 07:38:36, parent 7992, outside either runner. A later inventory verification refused because that game still existed even after the operator said the window was closed. Do not kill unowned processes or claim present root equality from the earlier hash checks. Do not reuse this experimental runner until process ownership and lifecycle are independently qualified. No additional launches planned.

Operator reports: native menus fail; DSVR panel initially worked but also failed on retry. Native menus already started in theater, and manually focusing the desktop game did not fix controls. Those hypotheses are ruled out as sufficient fixes. Live logs showed controller triggers and completed calibration, GetState IAT polling, and only about 30–33 measured game FPS at requested/runtime 90 Hz. This is not a performance or controller acceptance PASS. Next bounded investigation uses InputRoute publication/capture/rearm flags and XInputRoute merged buttons/axes/foreground on a correctly owned session; no more blind headset retries. A3/H4 remain blocked/failed and no checkbox was closed.

### Post-run reconciliation, 25 October 2026 07:43 local

DSR eventually exited (NO_GAME_PROCESS observed). A fresh full root inventory compared to the second session snapshot reports Documents: 5 files, original_inventory_equal=False; AppData: 5 files, original_inventory_equal=True. Preserve newer Documents state; no automatic overwrite/rollback was attempted. Therefore do not claim that all post-test saves equal pre-test state. Original snapshot and retained test output remain in the session directory; both held-root paths are absent and both original-root paths exist.

With the game confirmed closed, the verified diagnostic package was snapshotted/deployed: build/vr-menu-session-20261025-074315-ac471cd9/deployment-intent.json and deployment-complete.json. This is deployment only, not a new launch or accepted native fix. Do not run the experimental launcher again until its Steam handoff/ownership failure is fixed and tested.

### Quest 3 controls: two reproduced regressions fixed, 25 October 2026

Root cause 1: the capture-release latch required every stick, grip, trigger and button on both controllers to become neutral simultaneously. A grip or menu-navigation stick still held after Home/calibration therefore muted a newly pressed A, walking and turning indefinitely. tests/test_input.cpp exercised the actual InputPollFrame -> InputLatestPad seam: newly pressed A with an old navigation stick held, and new confirm/walking with an old grip held, both failed on the old implementation. ReleaseCapturedHand now masks only channels held during capture, until their own release. It still prevents the calibration-completion trigger, closing-button and held-navigation leaks; unrelated fresh controls are not blocked. Theater/front-end and calibration cases extend this regression matrix.

Root cause 2: MenuGamepadSelect ignored controller_select whenever spatial_menu was true, relying on navigation_select from the rendered tracking snapshot. A valid raw A was discarded when that snapshot was missing. A new WARP test drove ModMenuControllerInput(A) with spatial presentation and an invalid ray: mod_menu_render failed before the fix, then passed. Input now publishes only A as focus confirm, independent from pose; the trigger remains solely on the valid spatial-ray mouse route. FeedInput switches to navigation on raw A. Existing trigger/ray click and no-phantom-mouse tests remain green; the two old raw-trigger-as-confirm assertions were replaced to enforce the intentional separation, not to waive a failed control requirement.

Additional regression coverage exercises the actual IAT detour with native_result=ERROR_DEVICE_NOT_CONNECTED and no capabilities export: Quest A+D-pad reaches user 0, stale original output is cleared, repeated unchanged polls retain the packet, release changes it, and unavailable motion returns disconnected. This is a synthetic bridge seam, not proof of game enumeration or native/headset success. Removed the misleading capabilities warning that presented its missing import as an established front-end failure.

Verification commands: cmake --build build --config Debug --parallel 4; ctest --test-dir build -C Debug --output-on-failure; matching Release build/CTest; pwsh -NoProfile -File tools/auto_verify.ps1. Final results: Debug 98/98, Release 98/98, verifier Release 98/98, doctor PASS, ZIP 1409577 bytes. Focused input|mod_menu_render|menu_presentation|xinput_iat: 4/4. The new test matrix initially had an unqualified camera-stub namespace compile error, corrected before testing; no stale executable was trusted. project(check,file=input.cpp) separately failed to locate src/core/build (harness working-directory error, no tests executed); the root CMake ladder above owns the observed results. Logs: build/quest-controls-debug-build.log, build/quest-controls-debug-tests.log, build/quest-controls-release-build.log, build/quest-controls-release-tests.log, build/quest-controls-auto-verify.log. Optional WARP render emitted build/quest-controls-menu.bmp and passed.

Deployment: verified game closed; snapshotted prior DSVR.dll, copied the packaged DLL, and compared its SHA256 to package metadata: a92b2a7436ab12a9d7a36795796e9ce0707a675eb31c70a24888d2624d8df3c1. Unique deployment receipts: build/quest-controls-deploy-20261025-075834-01c8ceb1/intent.json and complete.json. This operation touched no saves, config, vault or existing verification receipts, and launched nothing. Native/headset retest remains UNVERIFIED; earlier native failures are not erased, and no H gate is closed. Next protected experiment: captured grip/stick held across Home/calibration close, new A/walking, then A with ray lost, retaining InputRoute/XInputRoute evidence. Do not reuse build/vr-menu-check.ps1.

### Physical-combat investigation blocked by native analysis availability

Read the current contract, CombatNative adapter, AtkParam qualification and preserved native contact evidence. Production CombatNative currently reads and validates equipment only; physical_damage_bound/shield_contact_bound remain false. Cached contact dispatch still has a split type-1 continuation and an indirect target dispatch, not a verified end-to-end physical damage ABI. Selected rows and a plausible HP setter do not qualify contact submission, lifetime, thread ownership, target volumes or defenses. The retained physical-tip path also still assumes .9m and is not independent weapon geometry. No placeholder implementation was added.

Current tools expose neither GhidraMCP nor REA MCP. Exact tool discovery failed to find GhidraMCP/check_connection. Following the installed REA guide, npx --yes rea-agents@latest doctor exited1: REA6.8.0 readiness failed, including Node26.3.1 outside its supported range and unavailable configured engines. The installed/pinned CLI npx --yes rea-agents@4.0.1 analyze C:\Tools\DarkSoulsRemaster_De\analysis-full\DSRFull.gpr --provider ghidra --format json exited1 with target_unavailable. A targeted inspect-native-api request for 0x1403C4020 against the pinned executable, first with process-scoped GHIDRA_INSTALL_DIR, returned runtime_missing. Resolving the existing JDK through process-scoped JAVA_HOME=C:\Tools\DarkSoulsRemaster_De\jdk-21 reached the decisive failure: unsupported_host, verified Windows Job Object ownership/private DACL/reparse-safe path admission not implemented. No permission/security bypass, global config edit, DB repair or headless shim was attempted.

Receipts: build/physical-combat-rea-probe.json, build/physical-combat-rea-api.json and build/physical-combat-rea-java-api.json plus corresponding stderr files; .empryo/jobs/job-59.log retains the readiness output. Existing cached reads at C:\Tools\DarkSoulsRemaster_De\combat-evidence\physical-combat-20261024 (contact-dispatch,1425ba7dc,1403c4162) provide no missing execution/lifetime proof and were not rewritten.

B/C remain blocked; no checkbox closed. No C++ changes, no CMake/CTest/package/DSVR doctor run, no game launch, memory write, input injection, save/vault operation or commit. Next prerequisite is supported read-only GhidraMCP access to the already-open pinned DSRFull database; then trace the actual contact/defense consumers and correlate native outcomes before implementing any dispatch. Do not substitute canned attacks or direct HP subtraction.

### Quest 3 D-pad channel bound for real; rotational blade sweep closes its own gap, 25 October 2026

**D-pad root cause.** The Quest profile suggested `/user/hand/left/input/x_axis` and
`y_axis` for a vector2 `dpad` action. Those are touchpad axes; the Oculus Touch
controller profile does not carry them, so the action never became active. The
channel looked bound and silently never answered, in the spatial menu and in game
alike. Meta's controller mapping reference publishes the thumbstick's four discrete
directions (Thumbstick Up/Down/Left/Right, both hands) as the Touch D-pad
equivalent, and Quest 3's Touch Plus controllers have no physical D-pad.
`src/core/input.cpp` now creates four boolean actions `dpad_up`/`dpad_down`/
`dpad_left`/`dpad_right` bound to
`/user/hand/{left,right}/input/thumb_up|thumb_down|thumb_left|thumb_right`, and
`ReadDpad` synthesizes signed axes from them with opposite directions cancelling.
Downstream consumers were already correct once the channel went live: `MapVrPad`
D-pad bits into the game's XInput packet, ImGui `GamepadDpad` keys for the spatial
menu, and the capture-release latch.

**Test seam.** `ProfileBindings` now requires eight direction bindings and zero
phantom paths. `DigitalDpad` drives the real `InputPollFrame` -> `InputLatestPad`
seam: gameplay D-pad reaches the game, a new direction replaces the previous one,
D-pad beats stick-synthesised navigation, the spatial menu navigates, the D-pad
cannot leak when the menu closes, and it works again after the latch releases.

**Blade sweep.** `BladeSweptOverlapsSphere` only covered the straight
`prev_tip -> tip` chord. A fast wrist rotation carries the tip along an arc, so a
small target on the arc's outer side sits inside the volume the blade actually
occupied yet outside both capsules and outside the chord: a fast slash through a
small target could miss. `BladeSweptOverlapsSphereArc` subdivides the rotation
(bounded `kSweepSteps = 4`), tests each sampled capsule plus the tip-to-tip
segment, refuses to invent a rotation plane for near-parallel or near-opposite
pairs, and fails closed on non-finite input. `BladeKinematics` gained `prev_grip`
and the bench in `src/core/arm_ik.cpp` feeds the previous frame's native grip;
`EvaluateBladeContact` now uses the arc test. New `tests/test_combat_math.cpp`
cases: chord misses while the arc hits at the mid-arc target, no coverage beyond
the band, pure translation agrees with the chord, a 180-degree flip falls back to
the chord, NaN and negative-radius refusals, and end-to-end `EvaluateBladeContact`
reports the arc-only contact. This is geometry only: `physical_damage_bound` is
still false and nothing was submitted to the game.

Verification commands, all run this session:
`cmake --build build --config Debug --parallel 4`;
`ctest --test-dir build -C Debug --output-on-failure`;
`ctest --test-dir build -C Debug -R "input|mod_menu|pad_math|menu_presentation|xinput_iat"`;
`cmake --build build --config Release --parallel 4`;
`ctest --test-dir build -C Release --output-on-failure`;
`cmake --build build --config Release --target dsvr_setup -- /t:Rebuild /m:4`
(forced full Rebuild because `BladeKinematics` is a shared-header struct change);
`pwsh -NoProfile -File tools/auto_verify.ps1`.
Results: Debug build clean, **Debug 98/98**; focused input/menu subset **9/9**;
Release build clean, **Release 98/98**; verifier Release **98/98**; doctor PASS
(pinned build supported, App 1.03.1 / build 10943698);
`dist\DSVR-latest-x64.zip` **1414197 bytes** + `DSVR-latest-verification.json`.
Logs: `build/q3-dpad2-debug.log`, `build/q3-dpad2-ctest-debug.log`,
`build/q3-dpad2-focused-debug.log`, `build/q3-dpad2-release.log`,
`build/q3-dpad2-ctest-release.log`, `build/q3-dpad2-setup-rebuild.log`,
`build/q3-dpad2-autoverify.log`. An earlier packaging attempt failed on a locked
`dist\DSVR-latest\DSVR-Setup.exe` held by an open setup window; the process was
not killed and the refresh succeeded after the operator closed it.

No game launch, no memory write, no input injection, no save or vault operation.
Native/headset D-pad behaviour remains **UNVERIFIED**: the profile-path claim
rests on Meta's published mapping reference, not a captured Quest 3 session, and
no H gate is closed. Next headset probe: enumerate the active Oculus profile's
available paths and confirm `thumb_up`/`thumb_down`/`thumb_left`/`thumb_right`
become active, then re-run the menu and XInput D-pad rows. For the blade, the
next probe is a recorded slow-versus-fast wrist rotation past a small static
target: the arc test must report contact on the fast pass where the chord test
did not, and neither may report contact when the blade stays clear.

## 26 October read-only melee subagent: 14032F548 verdict + next-probe first half

Task-owned files: `docs/NATIVE_MELEE_CONTRACT.md` (new 26 Oct verdict
subsection at the end: verdict, evidence, limitations, exact next probe,
probe outcome, +0x73 linkage), this handoff line. No game launch, repair,
save, write, commit, or native call.

Evidence added: `14032F548` recorded as instruction candidate only —
byte-pinned `CALL [RAX+0x640]` in a `.text1` split gap (forged-JMP island),
zero xrefs, RAX/receiver/args unrecovered; adjacent `140322910` has 27 CALL +
3 DATA refs with `548` contributing none; forward JMP `1425C31F0` + TEST ESI
sign branch, parallel `+0x638` slot lead, known `+0x640` inhabitants
EnemyIns `1413234A8->14034BF70` vs PlayerIns `140339600` with no xref link to
`548`.

Probe outcome (read-only GhidraMCP, GUI reachable, `get_assembly_context` 12
each side): `142756FC5` = `TEST AL,AL` in a thunk/return-pool island with a
`CMOVNZ` return-address selector toward `14032F584/594` (not a producer);
`1421B6015` = `MOV R14,RAX` with a `CMOVZ` selector taking `14032F54E`
(`548`+6) as a conditional return immediate (no xref; producer still
unrecovered); `1411B6015` = `MOV RCX,[R10+0x30]` negative control (split gap,
no `548` link); `14032F471` = `JMP 0x142656c78` in garbage/forged island
before `14032F480`, not a trustworthy linear producer. DB unsaved/unmutated.

+0x73 linkage appended as cache transcription only: `kMapHitType` /
`NativeMapHitType` exist in code; contract notes `1403C72D0` 23/24/25 still
needs DSR verification that it reads `0x73`.

Exact next probe (second half still open): compare `+0x640` slot inhabitants
+ natural hit/miss correlation under TESTING; never invoke `548`.

## Historical evidence recovery

The [history index](docs/history/README.md) retains the complete previous MODLOG and other replaced documents, including run IDs, crash observations, capture paths and restoration receipts. They are dated evidence only. Verify the candidate and underlying artifact before reusing any finding. Historical next actions and approval language are superseded.

## D stereo read-only next probe, 26 October 2026

Live-GUI `DSRFull` D-probe handoff (read-only; no game launch/repair/save/replay/write/commit): backward trace from `1411B4950`-class seeds in three bounded windows recovered the RDI/R14 producers at the `1411B46C0` ctor head (`RDI=RCX` at `46DF`, `R14=RDX` at `46DC`; span `[1411B47CE,1411B491A]` still uncovered); renderer+10's sole observed store is `1411B494E` (`140CC5B80` return with `"HighGraphics"` name arg, or `RSI` null-alloc fallback); `140CC5E60` (5B `MOV [RCX+0x18],RDX; RET`, 5 xrefs) and `140CC5EB0` (42B 11-insn body, 27 xrefs) byte-pinned by read; full windows, bytes, and limits in `docs/NATIVE_STEREO_CONTRACT.md` ("26 October D-probe, second window"). Exact next probe: Begin-payload retain/release + derived dispatcher vftable, then close `[1411B47CE,1411B491A]` and type the R14 source via `1411B46C0` callers; fallback is the main-scene bracket via `1411BEA27/1411C5049/1411C2F6C`. Dirty tree preserved; GHIDRA_MCP_ALLOW_SCRIPTS untouched; nothing saved or committed. D1/D2 still open.

### E locomotion docs-only owner table, 26 October 2026

Created `docs/NATIVE_LOCOMOTION_CONTRACT.md`: status UNRESOLVED, no native motor binding (orientation done, motor attribution open). Six owner rows all UNKNOWN with file:line evidence pointers (XInput poller, stick mapper, motor/collision resolver, authoritative root, forced-motion source, head/solid query); three refusals (mixed-input non-attribution, forced-motion no-credit, blocked-residual heuristic bound per ENGINEERING.md:29); next probe is the pinned XInput poll caller first per E2 order. Scope: only `docs/NATIVE_LOCOMOTION_CONTRACT.md` + `MODLOG.md`. No build (docs-only); `ctest -N` count unchanged (101). No game launch/install/headset; no commit.

### D stereo re-read: RDI/R14 ctor-head producers re-confirmed + outer caller seed, 26 October 2026

Read-only GhidraMCP (live GUI DSRFull; no launch/repair/save/replay/write/scripts/commit): re-recovered the three backward windows from `1411B4950`-class seeds (stop at `46DC/46DF` producers), confirmed sole renderer+10 store `1411B494E`, byte/xref re-pinned `140CC5E60` (5B, 5 xrefs) + `140CC5EB0` (42B 11 insns incl `CALL 140CCE4D0` + tail `JMP [RAX+0x20]`, 27 xrefs), added `FUN_1411b46c0` identity + outer caller seed `1411B3FA5` (`0x300` alloc renderer; R14-source = outer RCX, type open); deltas appended to `docs/NATIVE_STEREO_CONTRACT.md` as the 26 Oct D-probe addendum (existing second-window section left intact). Exact next probe: type outer RCX at `1411B3F79` upward, then Begin-payload retain/release + derived vftable; fallback main-scene bracket `1411BEA27/1411C5049/1411C2F6C`. Span `[1411B47CE,1411B491A]` still uncovered. D1/D2 still open.

### Lever interact-range cache search: no citable value, 0.0 retained, 26 October 2026

Searched `build/research/soulsmodding-20261024` for a citable lever interaction range. Found only a cross-class `validDist` field default (`u16 validDist = 150` in `primary/soulsmods__Paramdex/files/{DS1,DS1R}/Defs/ObjActParam.xml`, DisplayName `[cm]` = 1.5 m) versus the raw wiki boilerplate "In metres" (`raw/ds1-refmat__param__objactparam.wiki` line 7; `tables.tsv` row `ds1-refmat:param:objactparam` 7). Lever rows in `primary/soulsmods__Paramdex/files/DS1R/Names/ObjActParam.txt` (1200/1202/3011/3012/4560/6100/6101/7201/7202/16010) and object-list lever names are name-only; the cache holds defs + names, no per-row regulation.bin values. Unit conflict + no per-lever value = not citable: applied no number, kept the Lever row at 0.0/refuse, added a code comment naming the exact cache paths searched. Build/tests: `cmake --build build --config Debug --target test_interaction_target` + `ctest -C Debug -R interaction_target` GREEN; same in Release GREEN. Scope: only `src/core/interaction_target_math.h`, `tests/test_interaction_target.cpp` (unchanged, still pins 0.0), `MODLOG.md`. No game launch/install/headset; no commit.

### D stereo third window: Begin-payload borrow-appearing + derived slot set pinned, 26 October 2026

Read-only GhidraMCP (live GUI DSRFull; no launch/repair/save/replay/write/scripts/commit): poster `1411BE560` maps RDI=dispatcher, R14=ctor-R14, RBP=`&renderer+20`, RSI=`&renderer+28` into msg `+40/+48/+50` (vftable `141609970`, queued via `140CC5E90`, sync fallback `[RAX+0x10]`); pump `1411BF1F3 CALL [RAX+0x10]` passes msg+40/48/50 as RDX/R8/R9; handler `1411BEF10` renames them R14/RDI/RSI with RBP=dispatcher. Verdict: +48/+50 borrow-appearing (plain `MOV RCX,[RSI]` / `[RDI]` into dispatcher +0x20/+0x28 at `1411BF01E..030`, no interposed AddRef; `140CD3040` untyped so hidden retain unexcluded; reverse store `1411BEF66 [RDI],RAX` mutates the renderer field), +40 open (untyped `140CCF680/5A0/5C0` + virtual +0x10/+0x8, none typed retain/release). Derived vftable: overridden set {+0x28,+0x30,+0x40,+0x48} pinned (`2DB/2E3/2EB/2F3` with `CMP RAX,R15` / `JZ retire` skip-on-base; R15=`1416098B0` at `1411BF175`); +0x38 shared (no base check); base 96B decodes to 12 pinned slots incl +0x10 `1411BEF10`, +0x50 `1411BE560`; same compare idiom for all four slots in `1411BE100/1B0/340/3F0`; derived table address/targets OPEN (`get_xrefs_to 1416098B0` = 11 DATA refs, all base sites, no second table loaded). Limits: `1411BEF3A/BF00D/BF34B` no-instruction gaps (BF00D superseded by observed BF029/030), listing slices only, no prototypes/decompile, `ram:14011BF2FC` still unpinned. Exact next probe: queue completion/teardown (`140CC5E90`, `1411BF2AE/140CC5920`, tear-down `1411BF1AE`, `140CC5600/140CC5410`) + main-scene bracket `1411BEA27/1411C5049/1411C2F6C`, fallback bracket alone if blocked. Details in `docs/NATIVE_STEREO_CONTRACT.md` ("third window"). Scope: only `docs/NATIVE_STEREO_CONTRACT.md` + `MODLOG.md`. D1/D2 still open.

### E2 XInput poller static probe: 6 call sites, 2 per-frame candidates, 26 October 2026

Read-only GhidraMCP (live GUI DSRFull, `/DarkSoulsRemastered.exe`; no launch/repair/save/replay/write/scripts/commit): `XInputGetState` thunk `0x14082f790` = `JMP [0x142017ed4]` (import `Ordinal_2`); exactly 6 `UNCONDITIONAL_CALL` callers, all classified. Top per-frame candidates: `FUN_140e27aa0` (site `140e27ff6`, `ECX=[RDI+0x264]`, thumbs state `+8/+0xA` → `/65535.0*2` floats at `+0x26c/+0x270`, zero code callers) and its mirror `FUN_140e28430` (site `140e28986`, `ECX=[RDI+0x204]`, floats at `+0x20c/+0x210`). One-shots: `FUN_140c9f6c0` (connected flag only), `FUN_140e1c180/200` (4-pad bind enumerators), `FUN_140c9faa0` (remap layer with 7849/8689 deadzone, cadence unproven). Stopped at the poller→mapper edge: no velocity mapping in the same windows, motor/resolver unclaimed. Details in `docs/NATIVE_LOCOMOTION_CONTRACT.md` ("E2 static probe"); exact next probe: consumers of `+0x26c/+0x270` / `+0x20c/+0x210` = mapper→motor boundary, plus 8 DATA-ref slot resolution. Scope: only those two files. All six owners still UNKNOWN; no commit.

### D stereo fourth window: queue/teardown shape + bracket scout, 26 October 2026

Read-only GhidraMCP (live GUI DSRFull; no launch/repair/save/replay/write/scripts/commit): `140CC5E90` is a 5-insn enqueue tail-thunk (`ADD RCX,0x78; JMP 140CC55A0`), not a completion point; retire `1411BF2AE` calls `140CC5920` then BPL decides loop (`JNZ 1411BF180` → `140CC5600`) vs exit; `140CC5920` branches on msg+0x10 bit0 (free path OPEN); teardown `1411BF1AE` is pump-owned (+0xbb gate, virtual +0x18, BPL=0, retire); wait `140CC5600` calls `[queue]+0x30`-object virtual +0x10 (identity OPEN); type `140CC5410` is `MOV EAX,[RCX+0x14]; RET` with >7 retiring. Bracket: `FUN_1411c2f50` (49B, 5-arg forwarder, result halves) is the recommended live-observation bracket; `FUN_1411c4fa0` (222B) secondary for alloc lifetime (stack-local, unsupported past return); `ProcessDantelionRenderFrame` (1547B) explicitly not a bracket. Details in `docs/NATIVE_STEREO_CONTRACT.md` ("fourth window"). Scope: only those two files. D1/D2 still open.

### Type-64 (parry-context) static probe: type-2 dispatch + gates, 26 October 2026

Read-only live GhidraMCP against GUI `DSRFull` (PID 17320; token `1792646701740:fa2c6e65:3542`, unsaved true before/after; no save/repair/script/launch/write/commit): `1403C4210` (sole code caller `1403C4020`, +DATA `141d53e64`) routes kind 0 -> `1402F84F0` (notify `1403CA1D0` + effect chain `1403E6450`, return 1, no +34 read), kind 1 -> thunk `1402F8410`/`1429E5D7E` (forged-return selector on `[RDX+0x524]` bit to `140045643`/`1402F8494=JMP 142C58024`; meaning open, never step through), kind 2 -> `1402F84B0` (`target!=source && [record+0x34] < 0x40` then `1402FC360`, else 0 — attack_type 64 fails the gate). Type-3 consumers `140420A20`/`140420B30` share the `< 0x40` gate; `1402FC360`/`1403E6450` bodies have no +34 read. Producer re-read: `1405290C0` stores attack type at payload +34 and fills +30 via `1402DEB20` only when `+34==0x40`; which wrapper argument values pass 64 vs 0/2 is unrecovered (wrappers `140528CE0/140528E90/140528F70` pinned). Zero `== 0x40` consumers found: +34-xref not field-indexable (timeout-limited enumeration, not proof of absence). Details in `docs/NATIVE_MELEE_CONTRACT.md` ("26 October type-64 static probe"). Exact next probe: natural parry correlation under TESTING. Scope: only those two files. No binding proposed.


### Launcher chain-DLL FAILs print full colliding path, 26 October 2026

RED first in tests/test_setup_theme.cpp (stubbed chain-present/chain-move-fail conditions driven through CTL_INSTALL, log asserted for full game-dir path; RED confirmed as FAIL 154) then GREEN: src/setup/setup.cpp DoInstall chain-conflict message logs the full chain_dest path and the move-failure message logs dest_loader to chain_dest. DoInstall:522 and all behavior untouched, message text only. Verify: setup_theme PASS both configs; full ctest 101/101 Debug + 101/101 Release. No game launch/install/headset; no commit.

### Package mid-refresh atomicity documented + tested, 26 Oct 2026

K-audit follow-up (code+test, offline only): `tools/package.py` latest-mode comment corrected — the live `DSVR-latest/` refresh is intentionally non-atomic (per-file for Explorer locks; kill/lock mid-loop may leave a mixed stage) while the zip (replaced last) + verification.json (written after) stay on the old build, so a failed refresh never claims the new build. New `test_latest_mid_refresh_failure_keeps_old_zip_and_verification` locks a live destination mid-refresh, asserts nonzero exit and byte-identical old zip + verification. `test_package.py` 9/9 GREEN (unittest OK), `ctest -R package` PASS. Scope: tools/package.py, tests/test_package.py. No game/install/headset; no commit.

Read-only GhidraMCP (live GUI DSRFull, `/DarkSoulsRemastered.exe`; no launch/repair/save/replay/write/scripts/commit): full decompiles confirm both candidates take the device struct as param_1/RCX (dynamic heap base, so no concrete field address exists to xref). `read_memory` 8B each decoded all 8 DATA-ref slots: per-candidate duplicate RVA pairs plus one VA+1 slot each -- not vftables, vftable claim refused. `FUN_140c9faa0` sole caller is `thunk_FUN_140c9faa0` (`140c9f800`), dispatched from table `14149dc40` (thunk at index 6); table owner unclaimed, candidate 3 cadence stays UNPROVEN. Consumer search (whole-program plus function-scoped `search_instructions`, bulk `get_xrefs_to`) timed out with 0/3 windows completed; no velocity-scaling reader recovered, mapper/motor UNKNOWN. Details in `docs/NATIVE_LOCOMOTION_CONTRACT.md` (E2 follow-up static probe). Exact next probe: retry the field-pattern search or walk sibling tables `1414dde70`/`1414ddd20`; type table `14149dc40`. Scope: only those two files. No commit.


### E2 sibling-table walk: no float-field consumer, VA decode corrected, 26 October 2026

Read-only GhidraMCP (live GUI DSRFull, `/DarkSoulsRemastered.exe`; no launch/repair/save/replay/write/scripts/commit): 2 entry windows + <300 B decompiles each table at index 0 (trigger reader: `140e26780` 181 B / mirror `140e26840` 181 B, neighbouring `+0x3dc/+0x3ec` / `+0x37c/+0x38c` fields) and index 3 (event dispatcher: `140e28dc0` 169 B / mirror `140e28e70` 169 B, rumble-ish/`0x219` branches, no float reads) -- bound hit, no sibling reads `+0x26c/+0x270` / `+0x20c/+0x210`, mapper/motor UNKNOWN. Correction: `1414dde90`/`1414ddd40` are full-VA poller slots (index 4 of each 8-slot table), not VA+1. Details in `docs/NATIVE_LOCOMOTION_CONTRACT.md` (E2 sibling-table walk). Exact next probe: idle-bridge field-access byte-pattern search for the scaling reader (mapper->motor boundary). Scope: only those two files. No commit.

### B swing-time producer xref probe: constructor chain confirmed, no swing caller, 26 October 2026

Read-only live GhidraMCP against GUI DSRFull (token 1792646701740:fa2c6e65:3542, unsaved true before/after; no launch/repair/save/replay/write/scripts/commit): get_xrefs_to 1403C9B50 -> exactly 2 refs (DATA 141D54398, CALL 1404910AD) and 1403C7460 -> exactly 3 DATA refs (141D5417C, 1417991F0, 141799224); zero new xrefs vs priors. 1404910AD = CALL in FUN_140490F40 (ObjDam condition wrapper: resolves two objects via 140371200/140371360/140371bd0, gates nonzero, calls 1403C9B50(manager, param_2, lVar3); no animation/weapon/swing argument). Its own refs: DATA 141D612F8 + JMP-island 1404B0EC7 (split, not swing). DATA holders return no-instruction; RVA-decode confirms address tables (003C9B50/003C7460); bulk xref-back on all four is empty, so no indirect code owner is indexed. Verdict: no swing-plausible producer in either xref set; builder has zero code xrefs. Details in docs/NATIVE_MELEE_CONTRACT.md (B swing-time producer probe). Exact next probe: decode the 1417991F0/224 record stride to name the builder's dispatch owner, else natural-swing correlation under TESTING. Scope: only those two files. No commit.

### E2 byte-pattern retry: axis getters pinned, scaling consumer behind unindexed dispatch, 26 Oct 2026

Read-only GhidraMCP (live GUI DSRFull; no launch/repair/save/replay/write/scripts/commit): masked MOVSS/dword byte-pattern search (patterns from candidates' own RDI stores) completed cheaply where instruction search timed out. Direct paired readers are other-struct copy-out/clamp/smoothing, not this device layout. Family sweep pinned indexed axis getters 140e269a0/140e269c0 (table idx 5, read +0x26c/+0x20c by index) and mirror setters idx 7; all 16 dispatch-table slots have zero code xrefs, so the velocity-scaling caller is behind computed dispatch. Mapper/motor UNKNOWN. Details in docs/NATIVE_LOCOMOTION_CONTRACT.md (E2 byte-pattern retry). Exact next probe: byte-pattern for table VAs as immediates to type the dispatch owner, or outer-RCX device-struct typing. Scope: only those two files. No commit.

### B static probe: 1417991F0/224 are unwind metadata, no dispatch owner, 26 October 2026

Read-only live GhidraMCP against GUI DSRFull (PID 17320; no launch/repair/save/replay/write/scripts/commit): read_memory 64B at 1417991F0/141799224/141D5417C + neighbours decodes a 12-byte RUNTIME_FUNCTION triad (Begin 0x1403C7460, End 0x1403C749D = 61B body, UnwindInfo 0x1417991D8); 1417991F0/224 are mid-UNWIND_INFO offsets in .rdata (header 01/17/06/00, codes, handler RVA 0x00020521 + chained 0x000A6405 = the prior 'small-int fields'), not dispatch records -- the 12-/16B dispatch-record premise is refuted. get_xrefs_to (cap 20) on the three tables + +/-0x10/stride neighbours: only .pdata DATA self-refs, zero code indexed dispatch; 1403C7460 keeps exactly its 3 DATA refs. No dispatch owner found, no indirect invocation recorded, no swing producer claimed. Details in docs/NATIVE_MELEE_CONTRACT.md (B static probe subsection). Exact next probe: walk 14024F6B0 -> 1403C8DD0 -> 1403C6030/1403C6850/1403C7D30 -> 1403C7520 backward to find which first references the 1403C7460 fragment range and name its outer caller; else natural-swing correlation under TESTING. Scope: only those two files. No commit.

### Mod-menu gate consistency: restart footer extended + stay-editable sliders gated, 26 Oct 2026

Prior audit's described footer restart block / ModMenuCaptureTextArmed hook did not exist in this tree (verified by grep; the applied-vs-attached binding comparison lived only in input.cpp's frozen InputLiveBindingSignature). Implemented: DrawMenu footer now prints "Restart needed: <parts> (restart the game to apply)." comparing applied bindings vs InputLiveBindingSignature plus applied reshade_host/render_scale_percent vs a first-draw baseline snapshot in mod_menu.cpp (render fields have no attach-time freeze; baseline frozen on first DrawMenu, honest session-start reference, not a production attach snapshot). Gated with BeginDisabled following the controller-hand pattern: Snap angle on snap_turn, Turn speed on smooth_turn, Vignette strength on comfort_vignette, sparring distance on proving ground, swing speeds/cooldown on physical_attacks+motion_attacks, contact speed/re-hit on haptics, support offset/rotation on Enable support grip; correctly-conditional groups (arm-swing/reach/defence) untouched, no label/ID removed. RED first: new WARP asserts failed at the disabled-slider check (FAIL item_was_disabled) before gating; footer asserts then drove the capture-seam fix (footer hook must start before DrawMenu — armed via pending flag in BodyVisibilityGetStatus + started in CalibrationGetStatus, since a log binds at LogToFile time and LogFinish fires at the panel child's End). Test seams: fixture stubs InputLiveBindingSignature to the Config defaults and mod_menu exposes probe_disabled_item/item_was_disabled (never armed in production). GREEN: ctest -R mod_menu_render PASS Debug+Release; full suites 101/101 Debug + 101/101 Release. Files: src/core/mod_menu.cpp, tests/test_mod_menu.cpp. No game launch/install/headset; no commit.

### Mod-menu everyday/advanced separation: default-open Advanced tuning groups, 26 Oct 2026

RED first: extended tests/test_mod_menu.cpp with a WARP assertion that an "Advanced tuning" grouping exists and default-open shows advanced controls (arm-swing sliders visible when enabled) — rebuilt test_mod_menu, confirmed RED (no grouping yet). GREEN: src/core/mod_menu.cpp adds one labeled always-visible "Advanced tuning" text row inside SettingsPanel (above the arm-IK rows) and one inside CombatPanel (above the per-hand rows; grip editor reads under the same label), plus a comment row cross-referencing them — same panels, same IDs/labels/behavior, grouping only; plain Text row (not SeparatorText/CollapsingHeader) so the label stays greppable in the LogToFile capture, no collapse so default renders show every advanced control, no new capture call above the footer capture point. Combat route asserted structurally via shipped next/prev-panel edges (0 -> 3 -> 0) with panel-ID re-resolve; no Combat-rows-in-capture assertion (one-shot hook has Settings-route-only stubs, shipped footer hook sits below the panel switch). Verify: cmake --build build --config Debug --target test_mod_menu + ctest -C Debug -R mod_menu_render PASS; same Release; full ctest 101/101 Debug + 101/101 Release. Files: src/core/mod_menu.cpp, tests/test_mod_menu.cpp, MODLOG.md (this entry). No game launch/install/headset; no commit.

### 25 October continuation: F2/C2 committed-pose lifetime and B update-owner evidence

Resumed the existing plan's later IK/physical-combat steering. HEAD
`ca989d74ae388f1b41d33b14ecfea2833ed19b71`; all pre-existing dirty work
preserved. Existing CMake cache: Visual Studio 18 2026/x64, LAB OFF,
ReShade addon ON. Graph/source-harness tools are unavailable in this chat,
so targeted direct source reads followed tool discovery. No delegation.

Reproduced a source defect after successful native-copy verification:
`cmake --build build --config Debug --target test_arm_pose_native --parallel 4`,
then `ctest --test-dir build -C Debug -R '^arm_pose_native$' --output-on-failure`
failed with `post-publication reader 0 mutation 1 returned stale poses`.
Losing focus between native callbacks still exposed the old valid hands.
`ArmIkHands` now stamps its committed calibration revision, and both public
readers share a lifetime check for focus/HMD/controller tracking, current
tracking timestamps, camera sequence/epoch, calibration, modes and capture.
Refusal ends the receipt and clears diagnostic motion history; restoring
external state cannot reopen it. A fresh verified natural copy reprimes at
zero speed. No new native call/write or hook was added.

Regression: 19 transitions independently through each reader (38 cases),
empty outputs, no native write/callback during reads, latched refusal,
fresh-copy recovery, calibration stamp and acceptance of a newer valid XR
packet with the same camera-bound native pose. The first shared-guard draft
erased a provisional source publication; existing CheckCombatBench caught
it before acceptance. An unverified-copy early return now preserves the
pending snapshot. Head-only/one-hand and duplicate-callback tests remain
active. This is adapter evidence, not modified-candidate native skinning.

Native B investigation: the installed GhidraMCP bridge still reaches GUI
DSRFull despite Empryo's current MCP config listing only REA. An inline SDK
client listed advertised tools before read-only calls; no shim/headless
wrapper or Ghidra script was created. Update owner `1402500D8` is named;
`1403C8DD0` calls `6030` and `6850`, the latter reaches `7D30` and directly
`7520`. Corrected the older `6030 -> 7520` claim. No indexed entry from those
paths into `7460`, including its unwind fragments. Independent EXE comparison
pins all 338 bytes of `1403C72D0` and `1403C7460`; `1403C733A` reads selected
AtkParam +0x73 and chooses messages 23/24/25. Message meaning, damage,
defenses, lifetime and thread correlation remain unresolved. Raw calls and
primary-byte-verification.json are outside distributable sources at
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\physical-combat-20261025-update-owner\`.
Executable SHA256 remains `a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b`.

Final full commands: `cmake --build build --config Debug --parallel 4`;
`ctest --test-dir build -C Debug --output-on-failure`; matching Release pair.
Both builds exit 0, **Debug 101/101 and Release 101/101**. Logs:
`build/continue-20261025-final-{debug,release}-{build,tests}.log`.
Focused red/green logs use `build/continue-20261025-publication-*`.

The first `pwsh -NoProfile -File tools/auto_verify.ps1` passed its tests and
doctor but packaging exited 1 on locked `dist/DSVR-latest/DSVR-Setup.exe`
(PID 33496); log `build/continue-20261025-auto-verify.log`. The old ZIP and
verification remain the old candidate; the live extracted directory may be
mixed after the refused refresh. Do not call that directory a verified new
package. Final `pwsh -NoProfile -File tools/auto_verify.ps1 -NoLatest` exits
0: Release 101/101, doctor PASS; log
`build/continue-20261025-final-auto-verify.log`.

Separate development package completed after midnight on 26 October:
`python -B tools/package.py --build build --out dist/continue-20261026-pose-lifetime --version 0.16.0-render-host-preview --latest --reshade-addon --ctest-log build/latest-ctest.txt --doctor pass`
exits 0. Output `dist/continue-20261026-pose-lifetime/DSVR-latest-x64.zip`
with its adjacent verification JSON, Release 101/101 and doctor PASS.
Core DLL SHA256:
`4c4e33c91adb82ebd240349b888da32ff360fc68425e7202278f476e6c135ab7`.
The development package is refreshed once more to include this completed
handoff; this is not a formal release, installation or native acceptance.

After the user confirmed Setup was closed, verified no DSVR-Setup process
remained and ran the same gated package command with `--out dist`.
Standard `dist/DSVR-latest-x64.zip`, extracted files and verification JSON
now contain the tested candidate. Log:
`build/continue-20261026-latest-package.log`. The standard package is refreshed
with this note and checked against its binary hashes; the earlier lock no
longer blocks packaging. No game deployment was performed.

Read-only preflight found an already-running, unowned game PID 24412 and
active desktop use. Its installed DLL SHA256 is
`3a136a395a55b7b3bd8678f05d0c47d80af7cd8916de3c7b5e8186211b52a880`,
not this fix. No installation, game launch/stop/input, process-memory read,
save/config/vault mutation, or restoration claim. Preflight log:
`build/continue-20261025-preflight.log`. Native/body/headset acceptance and
B/C stay open. Next experiment: after the active session closes, qualify
the protected transaction, then test lost focus/controller tracking and
recovery on the exact new candidate; observe source-to-body/armor propagation
and separately labelled native selected-context hit/miss/block/parry-only
outcomes. Do not substitute getter tests or the message field for damage proof.

### 26 October resume: release-blocking combat/interaction evidence and interrupted source fixes

User priority: finish physical native attack/block and interactions for the release.
B/C/H are release blockers; no playable release or native acceptance is claimed.
Existing dirty work retained; no commit, install, game launch/input, game-memory
access, backup-vault/save/config mutation or restoration claim in this resume.

The interrupted source candidate now builds and passes full Debug **101/101**
and Release **101/101**. Commands: `cmake --build build --config Debug --parallel 4`
then `ctest --test-dir build -C Debug --output-on-failure`, matching Release
pair; final logs `build/resume2-current-{debug,release}-{build,tests}.log`.
This validates the existing adjustable seated lift, immediate live menu apply /
automatic background save and mono camera-bound source-pose/FOV correction.
Headset smoothness, live body visibility and native combat are NOT established.

Extended the actual WARP/OpenXR submission seam with both-eye center-pose checks
and **13** independently mutated camera-source refusal/recovery cases: missing
sequence, epoch mismatch, old/future capture time, missing/future prediction,
focus/orientation/position invalidity, FOV boundaries, invalid aspect and a
sequence-only diagnostic tag. Each refusal preserves fresh tracking and releases
the XR frame/images; a fresh camera receipt recovers. Focused command:
`ctest --test-dir build -C Debug -R "^xr_mono_recovery$" --output-on-failure`
passes **1/1** (`build/resume2-mono-test.log`). This extends the earlier logged
red/green pose-mismatch regression, not a new in-headset performance result.
The syntax index cannot parse OpenXR calling-convention macros; real MSVC builds
are authoritative. `project check` scoped to tests incorrectly sought tests/build;
that harness error is retained, not reported as a source failure or bypassed test.

Forced `cmake --build build --config Release --target dsvr_setup -- /t:Rebuild /m:4`
before doctor (`build/resume2-setup-rebuild.log`, exit 0). Then
`pwsh -NoProfile -File tools/auto_verify.ps1` exits 0, Release **101/101**,
doctor PASS and refreshed the development ZIP; log `build/resume2-auto-verify.log`.
Final documentation/package refresh also exits 0: Release **101/101**, doctor
PASS, development ZIP 1,436,019 bytes at that refresh. Log
`build/resume2-final-auto-verify.log`. A final docs-only package refresh includes
this completion note; use its adjacent verification JSON for exact ZIP size/hash.

Native B progress: GUI DSRFull PID 17320 remains reachable through advertised
read-only tools of the installed inline MCP SDK client; no reusable shim or
Ghidra script. REA 6.8.0 now reports Windows PE analysis available, but that is
not a binding to the live primary .gpr. Corrected the false "atomic failure / no
handler" interpretation: selector flags come from target +0x524 comparison.
The type-1 accepted path reaches target virtual +0x480 then source +0x460;
PlayerIns/EnemyIns differ. Independently compared **336 bytes** of DB observations
with the pinned EXE. Native kind-2/0x40 state branches recovered but not classified
as damage/block/parry. Actor generation, record lifetime, simulation owner and
independent outcome still unknown. One interior-instruction walk is explicitly
rejected in its receipt. See docs/NATIVE_MELEE_CONTRACT.md. Exact next static
probe: successful PlayerIns +0x480 kind-2 branch from 140095D94, then +0x460
notification/context owner; do not invoke a speculative function to test it.

H1 progress: parsed the installed bounded DCX/BND3 ObjActParam declarations,
not just Paramdex defaults. **49** real rows; selected cached lever/door rows
have raw validDist **60/90/110**, not the schema default 150. Kept class-wide
physical reach disabled: native selected-row/prompt/LOS, distance origin and
object generation are still unbound. Carrier/member/schema/row hashes and raw
observations live outside distributable sources; see
`docs/NATIVE_INTERACTION_CONTRACT.md`. The bridge ObjAct function-name query
returned "No complete response ... timed out"; subsequent string collection
hit the bounded SDK client deadline. No identical query retry, DB repair or
negative claim about missing interaction code.

Observation readiness: old runner Check correctly refused changed config.cpp
(`build/resume2-combat-check.log`). Rebuilt current LAB observer. Its inherited
cache had DSVR_BUILD_TESTS=OFF: initial CTest registered **0 tests**, which is NOT
qualification. Configured the same VS2026/x64 cache with LAB ON, tests ON and
ReShade addon OFF; rebuilt and ran actual registered **99/99** Release tests,
then source-stable recheck **99/99**. Commands and logs:
`cmake -S . -B build/combat-lab -G "Visual Studio 18 2026" -A x64 -DDSVR_ENABLE_LAB=ON -DDSVR_BUILD_TESTS=ON -DDSVR_BUILD_RESHADE_ADDON=OFF`,
`cmake --build build/combat-lab --config Release --parallel 4`,
`ctest --test-dir build/combat-lab -C Release -N`, then full CTest.
Logs `build/resume2-observer-{configure,qualified-build,registered,qualified-tests}.log`
and `build/resume2-observer-recheck-{build,tests}.log`.

Created a NEW immutable offline qualification receipt (did not rewrite old ones):
`build/verification/combat-observer-20261026-resume/candidate.json`, binary hashes,
226 sealed source/build/test/tool files, 209-file pre/post equality check, actual
normal Debug/Release and LAB test-log hashes. Command
`pwsh -NoProfile -File tools/run_combat_capture.ps1 -Mode Check -CandidateReceipt build/verification/combat-observer-20261026-resume/candidate.json -Receipt build/verification/resume2-combat-ready/receipt.json`
exits 0: **CHECK PASS**, offline/disconnected Steam, exact-game block, qualified
candidate/seeds/current roots. It is read-only readiness, not a transaction or
native outcome. Log `build/resume2-combat-ready-check.log`. No Run was started.
Next native evidence is separately labelled ordinary hit/miss/block/parry and
selected-object/prompt activation under TESTING, with independent outcomes and
current ownership/restoration receipts. The no-shield/passive-enemy historical
comparison cannot supply defense acceptance. Plan and A/B/D/F/H cockpit evidence
updated together; no native gate checked complete.

### 26 October native qualification request: response observability fixed; gameplay not qualified

Requested scope is native attacks, defense and interactions on this build. These
are NOT merely missing signatures on a test receipt: combat_native still has no
production physical-damage/shield-contact binding, and physical reach still has
no native prompt/LOS/selected-object producer. Those gates remain BLOCKED.
No native/gameplay PASS, release, game launch, input injection, direct game-memory
write, vault change, receipt replacement or restoration is claimed.

Advanced the pinned PlayerIns +480 kind-2 continuation. It performs a horizontal
direction dot/Cosf comparison (observed degree multiplier and 120-degree constant)
and a later accepted path writes native state 1006/1007, +31C bit 2 and actor+404
1.0f. These are shipped-code observations, not an HP write or a block/parry label.
Pinned 125 DB bytes across five windows against the EXE. External evidence:
`C:\Tools\DarkSoulsRemaster_De\combat-evidence\physical-combat-20261026-qualify\`.
For interactions, UTF-16 ObjActParam at 141372BE8 reaches name-list registration
in 140531BC0; no selected-target owner follows from it. Four exact debug-string
xref/RIP-LEA probes are bounded negatives, not proof that code is absent.
See NATIVE_MELEE_CONTRACT and NATIVE_INTERACTION_CONTRACT.

A concrete observation defect was reproduced at the REAL contact detour/writer:
record+54 and +168/+169 changed during the natural callback, while diagnostics
always serialized zeros. Command
`cmake --build build --config Debug --target test_combat_trace --parallel 4`, then
`ctest --test-dir build -C Debug -R "^combat_native_trace$" --output-on-failure`
failed **0/1** with the missing response-payload assertion. Logs
`build/qualify-response-red-{build.log,log}` (actual test log is
`build/qualify-response-red.log`). The Python consumer independently failed with
`raw native response fields rejected`; log
`build/qualify-response-parser-red.log`.

Source fix: retain exact raw before/after kind and status fields in the EXISTING
five-event atomic diagnostics batch, with independent read bits and payload
version 1. Coalesced pair/handle/status reads retain the authored **18-query cold
permission budget**; no scope crosses the original native callback. Extra-field
read failures preserve the previously valid actor/context fields through bounded
fallbacks. No native function binding or gameplay write was added. The strict
Python analyzer exposes raw native_response only, preserves legacy unknowns,
rejects malformed/missing-read-bit payloads and NEVER turns it into native combat
acceptance. Tests cover all **16** partial-read masks, full unsigned 32-bit values,
failed optional reads, identity/capture transitions and whole-batch drops.
Focused **3/3 PASS**: combat_native_trace, combat_native_trace_filtered and vr_trace
(`build/qualify-response-partial.log`). This is synthetic seam evidence.

Current full verification, observed this turn:
- `cmake --build build --config Debug --parallel 4`, then
  `ctest --test-dir build -C Debug --output-on-failure`: **101/101 PASS**;
  `build/qualify-final-debug-{build,tests}.log`.
- Matching Release pair: **101/101 PASS**;
  `build/qualify-final-release-{build,tests}.log`.
- Current LAB observer Release build + full CTest: **99/99 PASS**;
  `build/qualify-observer-{build,tests}.log`.
- `pwsh -NoProfile -File tools/auto_verify.ps1`: exit 0, Release **101/101**,
  doctor PASS; development ZIP only (`build/qualify-auto-verify.log`).
No shared struct/header changed in this turn (only vr_trace.cpp, vr_trace.py and
their two tests), so no new setup-layout rebuild dependency was introduced.
`project check` built successfully but its unconfigured CTest invocation lacked
-C: **101 NOT RUN**, a harness failure, not 101 executed failing assertions.
The configured runs above are the actual results; no threshold was relaxed.

Fresh NEW immutable records:
`build/verification/native-qualification-20261026-response/candidate.json` seals
current source/binaries/test-log hashes. Adjacent qualification.json explicitly
reports attacks, blocking, parrying and targeted interactions **BLOCKED**, with
zero live observations. Old receipts remain unchanged. Command
`pwsh -NoProfile -File tools/run_combat_capture.ps1 -Mode Check -CandidateReceipt build/verification/native-qualification-20261026-response/candidate.json -Receipt build/verification/native-qualification-20261026-response-run/receipt.json`
exits 0; `build/qualify-native-check.log` says CHECK PASS, no changes yet.

Native Run was not started: runner lines 724-725 create data inside the backup
vault, and SaveReceipt replaces its active verification receipt. Both contradict
the current hard limits. Actual mod/gameplay qualification also needs the native
hooks/callbacks boundary and operator-provided gameplay (no input injection).
Resolve that boundary with a fresh transaction OUTSIDE the vault and append-only
receipts, then obtain labelled ordinary hit/miss/block/parry and prompt/activation
observations before implementing/qualifying the missing production bindings.
The plan ledger and A/B/C/H lane files were updated together; no checkbox closed.

### PowerShell 7 development tooling, 26 October 2026

All active PowerShell scripts are development/operator tools, not the product installer. Added `#requires -Version 7.0` to `tools/auto_verify.ps1`, `tools/verify_preflight.ps1` and `tools/run_combat_capture.ps1`; modernized the capture runner hashing/path handling and preflight list construction. CTest now requires and invokes `pwsh` on Windows and no longer registers a Windows PowerShell 5.1 path. `docs/TESTING.md` records the boundary. Product installer remains independent and portable.

Verification: PowerShell 7.6.5 parser PASS for all three scripts; `pwsh -NoProfile -File tools/run_combat_capture.ps1 -Mode SelfTest -WindowMinutes 30` exit 0. Debug build exit 0; full Debug CTest **99/100**, with unrelated `combat_native_trace_filtered` failing at `unrelated_signatures==0` (test #9). Release build exit 0; full Release CTest **100/100**. `ctest --test-dir build -C Release -N` reports 100 registered tests and only the `pwsh` capture transaction entry.

Next exact probe for the unrelated Debug-only red test: `ctest --test-dir build -C Debug -R '^combat_native_trace_filtered$' --output-on-failure`; if reproducible, inspect the modified fixture around `tests/test_combat_trace.cpp:159`. No installer/game/save transaction was run.

### 26 October operator combat/interaction capture and automatic ten-minute observer

Task: advance B/C/H using protected ordinary-controls comparisons; eliminate F9
capture burden and investigate the operator's contact hitch. HEAD remains
3c26ed95de5dd669925d704e6a7d01d3ad7ddb8c. Existing dirty work preserved, no
staging/commit/reset/delegation. The parallel user-owned PowerShell chat's PS7
changes are retained. Its removal of the PS5.1 CTest explains current 100/98
counts; no gameplay assertion/threshold was relaxed.

Source changes in this slice: vr_trace.cpp, diagnostics.cpp/core.cpp,
run_combat_capture.ps1, test_combat_trace/test_diagnostics_mode,
vr_trace.py/test_vr_trace.py. Cold synthetic permission-query budget tightened
18 -> 10 (red then green). Permissions are reused only inside one pre/post
snapshot; every scope ends before the natural original call. Positive local
PlayerIns self contacts are counted separately and excluded from detailed actor
reads, with original dispatch preserved. Short captures retain the 256 ceiling;
long captures stream details to the bounded 4096-event queue. Footer counters
explicitly account total = non_player + self + refused + detail. Analyzer validates
that equation and bounds without promoting raw observations to native acceptance.

The combat-only diagnostic thread now waits for the verified local player, then
starts exactly one automatic 600000-ms capture per process. F9 is unnecessary
and cannot restart this recorder. Title/bootstrap time does not consume it.
Tests first failed on long duration and absent automatic start; now verify loaded
player admission, one attempt, exact duration and no automatic restart. No XR,
input injection, physical damage binding or direct gameplay write was enabled.

A new LAB-only natural 140313470 interaction observer forwards original arguments
once, preserves all return bits, and captures selected group/row/PARAM/handle/state
before and after. Identity certification is callback-local only. Strict parser,
changed-handle and combat-domain regressions pass; the domain regression was red
until the new event was admitted. See NATIVE_INTERACTION_CONTRACT for pinned
ObjAct owner chain, raw payload and native raw * float32(0.01) * 2 range helper.
External native evidence is C:\Tools\DarkSoulsRemaster_De\combat-evidence\native-combat-20261026-finish.
No decompiled source/game data entered distributable sources or primary DB edits.

Protected transaction repairs: the first staging attempt failed inventory before
launch; restoration succeeded. Replaced provider Move-Item root staging with
atomic Directory.Move plus held inventory/source-absence/empty-root checks; later
staging succeeded. This does not establish the provider failure's exact cause.
The first automatic retry saw transient overlapping Steam bootstrap processes;
strict steady-state ownership refused it. Both exited and explicit Restore
succeeded. Startup now requires one exact PID/start-time/path generation stable
for 1000 ms, within a 90-second/deadline bound. Steady-state multiple/foreign
process refusal remains. SelfTest covers overlapping/empty/changing/stable startup
and pre-existing-generation rejection. Fresh append-only journals remain outside
original backup vaults; old receipts/candidates were not overwritten.

Native run 1: build/verification/native-finish-20261026-live-retry1/receipt.json,
PID30784, RESTORED and independently hash-checked. Three intact captures:
- 270943343-1: mixed wall then enemy hilt hit, not two clean F9 cases. 146 detailed
  contacts, zero loss/cap; seq24 stable EnemyIns HP44->31, weapon212000,
  Atk2301000/table1, behavior102301000, kind2, statuses0/0.
- 271445750-2: operator failed parry/hit, 256 details and 32 refused. Six incoming
  contacts include HP557->546->524->502->491->480->458. No successful parry proof.
- 271480312-3: operator successful block then other contact, 256 details and643
  refused. First incoming target HP403->403/stamina84->76; later HP403->369.
  The entire capture is not a clean block case. Both defense traces hit the old
  detail ceiling. Operator reported 1-2-second hitches. Raw captures, reports and
  operator notes are retained beside the receipt.

Native automatic run: build/verification/native-finish-20261026-continuous-run2/receipt.json,
PID48220, candidate ../native-finish-20261026-continuous/candidate-settled.json
(SHA256 55861ecd97df9cc39d7d79884ced3756a0b0aebe9c1185c511d80265a9ae6fda).
LAB DLL cafbca0862bb54c4cba9113f7498fdc79a8ae86d105a6ebd32c1b771b242bc87.
Command: pwsh -NoProfile -File tools/run_combat_capture.ps1 -Mode Run
-WindowMinutes 15 -CandidateReceipt build/verification/native-finish-20261026-continuous/candidate-settled.json
-Receipt build/verification/native-finish-20261026-continuous-run2/receipt.json
-SeedVault build/native-transactions/combat-observer-20261026-133218-f14e751b069b4f858471133e38547fc1.
Automatic capture ran 14:06:02.798 through14:16:02.842 New York time, before the
14:20:47 deadline. Operator returned normally to title; finish marker at14:16:32;
runner exit0, journal revision19 state RESTORED. Independent check confirms both
original roots (five files each) and three install/config hashes exactly equal.
Evidence: independent-restoration-check.json, native-finish-auto2-live.log.
No game remains running; Steam left Offline. Disposable progress is preserved
in the transaction test roots, not merged into personal saves.

DSVR-trace-48220-272695343-1.jsonl: 19,515,758 bytes, SHA256
ff5856750948eac9e120f081b18bf39e9da251c6f0ebb82336d96a9b77eb7fe6.
78023 events, zero dropped/contended, queue_peak105. 4681 callbacks =270 nonplayer
+3141 self +0 refused +1270 detailed; no detail cutoff. Capture and combat-integrity
PASS; global VR verdict remains incomplete/UNVERIFIED, not a truncated capture.
Operator: two swings missed enemy but HIT WALLS; block; two successful parries
each followed by a separate ordinary non-riposte attack; then kill. Seq442/513
are stable player->enemy type64/Atk4800450/shield1452000 with status[0,0]->[1,1],
no HP/stamina change. Following ordinary Atk2301000/weapon202000 contacts at460/514
reduce enemy HP69->12->0. Incoming contacts319/320/321 keep player HP659 while
stamina changes -24/-8/-8. These are correlated mixed-sequence observations,
not independently timed clean cases or proof that status bytes exclusively mean
parry. NPC contact554 has HP1->0 and statuses[0,0]->[0,1].

Operator also tried two locked doors, killed NPC, picked up its item and opened a
keyed door, reporting expected behavior without noticeable FPS damage for those
actions. One natural ObjAct callback at QPC2731792790240, thread43532, selected
index27, actor handle268713984, object handle553648238, state0->3, return1,
raw validDist60, flags15. This correlates with the sequence; it does not cover
locked-door refusal, pickup ownership or physical reach/LOS for all classes.

Performance remains FAIL/unresolved: operator still felt the hitch, including on
a jump. Detailed read/assembly ELAPSED wall time median2.4618ms,p99 6.1289,max9.6132;
native median.0042ms,p99.0866,max.2283. Smaller query budget did NOT solve it.
Do not call QPC intervals pure CPU work: descheduling remains possible. Most
rejected actor reads are unqualified types (1142), with11 stat-range failures.
Raw Step samples are tiny dispatch calls, not full-frame/simulation timing.
Next discriminating probe is CPU stacks or a labelled guarded-read-disabled
comparison, not another speculative cache across native callbacks.

Current exact-build checks all exit0:
cmake --build build --config Debug --parallel4; configured full CTest100/100
(build/native-finish-auto2-debug-{build,tests}.log); matching Release100/100
(native-finish-auto2-release-*); LAB Release98/98(native-finish-auto2-lab-*).
ctest -N owns these current counts. New 228-entry source manifest equal before/
after those builds. Runner SelfTest PASS(native-finish-settle-selftest.log).
pwsh -NoProfile -File tools/auto_verify.ps1 exit0, Release100/100 and doctorPASS,
refreshes DEVELOPMENT latest ZIP only(native-finish-auto-verify.log).
No formal release, upload or native physical-combat acceptance. B/C/H remain OPEN:
production physical_damage_bound/shield_contact_bound still false, native context
owner/generation/simulation phase and qualified target/geometry/LOS unresolved.
Continue from correlated status setters and verified creation wrappers, with the
saved exact raw traces available; do not repeat manual F9 captures.
