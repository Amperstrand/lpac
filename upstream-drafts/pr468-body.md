<!-- STAGED FOR OWNER — replace PR #468 body (estkme-group/lpac).
     Reason: the current body still describes the OLD implementation
     (last_logic_channel field, mbim mirror) that septs' review asked to
     remove; body and diff now contradict each other.
     Exact command (owner interactive shell):
       gh pr edit 468 -R estkme-group/lpac --title "fix(driver/apdu/at): close leaked logical channel when an at_csim session aborts" --body-file /tmp/opencode/upstream-drafts/pr468-body.md -->

## Problem

On the `at_csim` backend, `apdu_interface_logic_channel_open()` sends MANAGE
CHANNEL open and then the SELECT by AID. If the SELECT fails (AT-level error,
or a status word other than 9000/61xx), the function returns −1 **without
closing the channel it just opened**. `apdu_interface_disconnect()` only
closes the serial device. The card-side channel stays allocated.

Any condition that makes the ES10 session abort after the open — a modem that
rejects part of the exchange, a wedged AT parser, an early exit — therefore
leaks one logical channel per invocation. After a handful of aborted runs the
eUICC answers every open with `6A81` (no logical channel available) and all
further eSIM operations fail until a full modem/card session recycle. On
unattended embedded hosts (the at_csim use case) this wedges eSIM management
invisibly; we also initially mis-diagnosed the `6A81`s as a broken modem.

## Fix

Reuses the existing `userdata->channels` registry the same way the `at_etsi`
backend already does — no new driver state:

1. `logic_channel_open()` registers the channel with `at_channel_set()`
   immediately after MANAGE CHANNEL open succeeds (identifier = the channel
   number as a string, mirroring how `at_etsi` stores the `+CCHO` response).
   A channel number outside `1..AT_MAX_LOGICAL_CHANNELS` fails the open.
2. Every failure path after the open (SELECT transmit error, bad status word,
   OOM) closes the just-opened channel and thereby clears its slot. The
   cleanup close is best-effort: a refused close (bare `ERROR` or an error
   SW — observed on this modem class) is ignored and never propagates a new
   failure.
3. `logic_channel_close()` sends the close APDU only for a registered
   channel and clears the slot, like `at_etsi`'s close.
4. `disconnect()` walks the registry and closes **every** still-registered
   channel before closing the device — covering abort paths in libeuicc
   (e.g. `euicc_init()` failure calls `disconnect()`, not the channel close),
   including sessions that aborted with more than one channel open.

The diff is `driver/apdu/at_etsi_csim.c` only (+29 lines).

## Evidence

- **In the wild (Quectel RG502Q-EA, removable ECP3-class eUICC, enabled
  operational profile)**: after several failed `euicc_init` sessions with an
  unpatched build, MANAGE CHANNEL open returns `6A81` (pool exhausted) and
  closing channels 01–03 each returns `9000` — i.e. those channels were
  still open, leaked by the aborted sessions.
  Log (ICCID-redacted) + A/B harnesses + build hashes:
  https://github.com/Amperstrand/lpac/tree/ab/at-csim-channel-leak-evidence
- **Deterministic A/B (complete)**: identical builds except this fix
  (`083d080` = main + #466 hex option, sha256 `3017c2b8…` vs
  `8269852` = same + this fix, sha256 `bc69a4c8…`), driven against a
  scripted AT device replaying the modem's failure signature (MANAGE
  CHANNEL open → channel 01 + SW 9000; ISD-R SELECT by AID → SW 6A82),
  logging every APDU the driver sends. Result: without the fix the driver
  **never sends a close** — the channel stays allocated at process exit;
  with the fix the driver sends MANAGE CHANNEL close for that channel
  immediately after the failed SELECT (SW 9000, pool empty).
  lpac-visible behavior is identical on both sides (`euicc_init` error) —
  the fix changes only the cleanup. Harness + wire logs (`mock-modem.py`,
  `run-ab.sh`, `host-ab/*`) in the evidence branch above. The registry
  rework in the current head produces the same wire behaviour (re-verified
  against the previous head with a 12-scenario mock matrix, wire-identical
  on all scenarios), and additionally closes every open channel on
  disconnect.
