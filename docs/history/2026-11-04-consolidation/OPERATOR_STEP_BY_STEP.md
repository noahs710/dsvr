# Operator step-by-step: feel-checks for the installed fixes

Date: 2026-11-02. The new logic (aim selection, bow steering, wheel,
valley rearm, inertia lag) has NO live consumer yet — it is tested
offline only and there is nothing to feel for it in-game. This session
covers what IS installed. Captures for lanes B/C stay blocked on the
candidate re-qualification (another tab's flow, dirty tree).

## 0. Close the game, say "closed"

The agent then (no game needed): Release rebuild, full Release suite,
repackage, reinstall with the game closed, Doctor, preflight. It reports
READY with the new version label, or stops at the first red.

## 1. Launch, load the arena, say "live"

Steam offline preference on, firewall block in place, launch yourself
through Steam, fixed window, usual arena and ready character.

## 2. Feel-check A: respawn neutral (the arm-calibration fix)

Die 3 times (or: quit to menu + reload, and once remove the headset for
10 seconds and put it back on). After each return, look at both hands
before moving:
- PASS: hands hold their orientation, no snap, no "palm up / shield flat".
- FAIL: note which death and which hand, and what the wrist did.

Three lines back, e.g: `death1 pass / death2 left snapped palm-up / reload pass`.

## 3. Feel-check B: Quality auto-smooth preset (optional)

Only if you have a few minutes: DSVR-Setup → suggest-for-this-PC on a
Quality-tier machine should now also set MSAA 4x (previously FXAA). This
one is already proven by unit test; skip it if time is short.

## 4. Report back, close the game

Paste the three lines. Do NOT run the capture script this session: the
apparatus is pinned to the pre-fix qualified build and will (correctly)
refuse the current tree. Say "closed" when the game is down.
