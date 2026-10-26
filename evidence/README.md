# at_csim logical-channel leak — A/B evidence (work in progress)

Fix: branch `fix/at-csim-close-on-init-failure` (commit `c004218`)
A/B demo pair: branch `ab/at-csim-leak-demo` (commit `8269852` = A-side parent `083d080` + the fix cherry-picked)

## Builds (OpenWrt 25.12.6 SDK, ramips/mt7621, mipsel_24kc, musl)

| Side | lpac ref | tarball sha256 | differs by |
|---|---|---|---|
| A (no fix) | `083d080` (upstream main + #466 hex option) | `3017c2b88812b6ff53dbb17b7ded3d37010fd8e3a9ad0261bf426b3cd1fe31f7` | — |
| B (fixed) | `8269852` (A + fix) | `bc69a4c8fbd8bdbc97ac5160c9d4579e7277e958608faf624b32a854b90139e6` | exactly the fix commit |

Both run with `LPAC_APDU=at_csim LPAC_APDU_AT_HEX=UPPER` on a Quectel
RG502Q-EA (removable ECP3-class eUICC, enabled operational profile — a card
state on which `euicc_init` over `AT+CSIM` fails deterministically).

## Result so far — A-side leak CONFIRMED in the wild (2026-10-26)

From `ab-run2-router-log-redacted.log` (live router, stamped UTC):

- `AT+CSIM=10,"0070000001"` (MANAGE CHANNEL open) at session start →
  `+CSIM: 6,"006A81"` — **the card's logical-channel pool was already
  exhausted** after earlier card sessions run with an UNPATCHED lpac
  (each failing `euicc_init` had leaked its opened channel).
- Cleanup: `AT+CSIM=10,"007080XX00"` (MANAGE CHANNEL close) for channels
  01/02/03 → each answered `+CSIM: 4,"9000"` — **those channels were
  genuinely still open** (leaked), confirming exhaustion was caused by
  leaked opens, not card state.
- Six further `lpac chip info` runs with build A failed `euicc_init`
  identically (`{"code":-1,"message":"euicc_init"}`).

## Controlled A/B (pending)

The strict alternation (A runs → probe shows exhaustion by run 4; cleanup;
B runs ×6 → probe stays `9000` every run + stderr shows
`Cleaning up leaked APDU channel N`) is scripted in
`ab-harness-v3.sh` and staged on the test router. It could not complete
tonight: the shared bench modem spent the window in a documented
boot-time-silent state (post-profile-flip wedge, USB re-attach), and
another lane's testing held the modem lock intermittently. The harness,
both tarballs, and the procedure are staged persistently on-router
(`/root/ab/`); the run will be appended here as
`ab-run3-router-log.log` in the next quiet window.

## Code-level summary (why the leak happens)

`driver/apdu/at_etsi_csim.c` (upstream `a43e46a`):
`apdu_interface_logic_channel_open()` performs MANAGE CHANNEL open, then
SELECT; on any SELECT failure it returns −1 **without closing the channel
it just opened**. `apdu_interface_disconnect()` only closes the TTY. When
`euicc_init()` aborts (euicc.c calls `disconnect` on open failure), the
card channel survives the process. The mbim driver already defends against
exactly this (`last_channel_id` cleanup in disconnect, message
"Cleaning up leaked APDU channel"); this fix mirrors that pattern in
at_csim and additionally closes the channel on every in-open failure path.
