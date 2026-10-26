# at_csim logical-channel leak — A/B evidence (COMPLETE)

Fix: branch `fix/at-csim-close-on-init-failure` (commit `c004218`)
A/B demo pair: branch `ab/at-csim-leak-demo` (commit `8269852` = A-side parent `083d080` + the fix cherry-picked)

## Builds (OpenWrt 25.12.6 SDK, ramips/mt7621, mipsel_24kc, musl)

| Side | lpac ref | tarball sha256 | differs by |
|---|---|---|---|
| A (no fix) | `083d080` (upstream main + #466 hex option) | `3017c2b88812b6ff53dbb17b7ded3d37010fd8e3a9ad0261bf426b3cd1fe31f7` | — |
| B (fixed) | `8269852` (A + fix) | `bc69a4c8fbd8bdbc97ac5160c9d4579e7277e958608faf624b32a854b90139e6` | exactly the fix commit |

Both sides run with `LPAC_APDU=at_csim LPAC_APDU_AT_HEX=UPPER`.

## Deterministic host A/B — COMPLETE (decisive) — `host-ab/`

Method: a scripted AT device (`mock-modem.py`, pty-based) replays the
RG502Q-EA failure signature byte-for-byte: `AT` → OK, `AT+CSIM=?` → OK,
MANAGE CHANNEL open → channel 01 + SW 9000, ISD-R SELECT by AID → SW 6A82.
The mock logs every APDU the driver sends. Same host builds of the same
two refs (`run-ab.sh`); `timeout`-bounded; wire logs in `host-ab/mock-{A,B}.log`.

Results (2026-10-26 19:33Z):

| | A — `083d080` (no fix) | B — `8269852` (fix) |
|---|---|---|
| lpac-visible output | `{"code":-1,"message":"euicc_init"}` | `{"code":-1,"message":"euicc_init"}` (identical) |
| MANAGE CHANNEL close after failed SELECT | **never sent (0 closes)** | **sent immediately: `AT+CSIM=10,"0070800100"` → SW 9000** |
| channel state at process exit | **channel 01 still allocated** (`[open=[1]]` on the mock's last line) | **pool empty** (`[open=[]]`) |

The captured SELECT (`01A4040010A0000005591010FFFFFFFF8900000100`) confirms
the driver addressed the just-allocated channel (CLA 01) — the exact
failure path the fix targets. User-visible behavior is unchanged by the
fix; only the cleanup differs.

## In-the-wild corroboration — real hardware (redacted logs)

- `ab-run2-router-log-redacted.log`: at session start (live router, after
  prior card sessions with an UNPATCHED lpac), MANAGE CHANNEL open →
  `+CSIM: 6,"006A81"` (pool exhausted); cleanup closes of channels
  01/02/03 each → `9000` — the channels were genuinely still open, leaked
  by aborted sessions.
- `ab-run3/ab-run4-router-log-redacted.log`: re-exhaustion observed again
  after further unpatched-lpac card work by another bench lane; run 4
  achieved a verified-clean pool at Phase 0 (open → `019000`) before its
  phases ran.

## On-router controlled A/B — attempted, confounded (documented)

The strict on-device alternation (harnesses `ab-harness-v2/v3.sh`, plus a
v4 on-router) could not discriminate A vs B on this card state: mid-run,
the baseband's own USIM session re-initializes (URC burst
`+CPIN: READY / +QUSIM: 1 / +QIND: …`) and itself holds / re-opens
channels 01–03, so raw channel probing reflects baseband occupancy, not
lpac's cleanup (visible in run 4: closes succeeded twice in Phase 0 with
no lpac running at all). The deterministic host harness above therefore
carries the controlled comparison; the on-router logs remain as
corroboration that the leak manifests on real hardware.

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
