# Session state — 2026-10-28 close-down (lane: lpac upstreaming chain)

Machine-volatility backup of the 2026-10-27/28 session. Durable copies of the
staged drafts live in this directory; this file records what was posted, what
remains staged, and the upstream snapshot at session close.

## Posted this session (2026-10-27T22:18–22:19Z, all verified live)

- **estkme-group/lpac#469** (draft PR, the estkme#419 fix): body updated with
  "Hardware validation of the fix chain" (nr7101/RG502Q-EA, 10/10 bogus-AID
  sessions, close-on-wire every run, zero 6A81, evidence link
  Amperstrand/lpac#8 issuecomment-6449411573) + "Relationship to #468"
  section. Timeline comment: issuecomment-6455002140.
- **Amperstrand/lpac#6** closeout: issuecomment-6455005063 (incl. the finding
  that `fix/applet-cleanup-locals-zero-init` @ cc17113 tree == #467 head
  3521f66 — `git diff` empty; no duplicate applet PR filed; staged
  pr-applet-zeroinit-body.md obsolete).
- **Amperstrand/lpac#8** link-back: issuecomment-6455007938.
- Verified: the posted #419 cross-ref (issuecomment-6442194080) IS the
  recalibrated strict-epistemics draft (ifd-modemifd @ 7feb932); only the
  gating header was stripped and the closing line replaced with the #469
  pointer. No re-post needed.

## Upstream snapshot at close (checked 2026-10-28)

| PR | state | note |
|---|---|---|
| #466 (at-hex) | OPEN, APPROVED, not merged | still blocks Amperstrand/lpac#4 (stock switch) |
| #467 (applet zero-init) | OPEN, latest review CHANGES_REQUESTED = "approved, but please format" | formatting already applied in head 3521f66; our side done, awaiting re-review/merge |
| #468 (at_csim leak) | OPEN, **draft**, septs APPROVED + pinged @CoelacanthusHex (2026-10-27T09:40Z) | **body still STALE**: describes the dropped `last_logic_channel` implementation, contradicts the reworked diff (verified live: contains `last_logic_channel` and a literal `**Title:**/**Body:**` wrapper) |
| #469 (qmi/mbim inits) | OPEN, draft, no maintainer review yet | body carries the hwval addendum; #419 cross-ref live, no reporter response |

## Still staged for owner (this directory)

- `pr468-body.md` — replacement #468 body matching the reworked
  `userdata->channels` registry implementation. Apply with
  `gh pr edit 468 -R estkme-group/lpac --body-file pr468-body.md`
  (strip nothing — this copy has no wrapper; title already correct upstream).
- `pr468-review-reply.md` — reply closing the loop on septs' review.
  `gh pr comment 468 -R estkme-group/lpac --body-file pr468-review-reply.md`
  (or reply inline on the review thread).
- `pr-applet-zeroinit-body.md` — **OBSOLETE, do not file** (cc17113 ==
  3521f66 == #467 content; a second PR would duplicate an open PR).

## Parked / tabled (owner decisions, no agent action pending)

- #468 un-draft timing (after @CoelacanthusHex second review; septs already
  approved). The old "#468 close vs keep" question is effectively settled:
  keep (approved rework).
- Bench-lane dogfood of the pin (Amperstrand/lpac#5 checklist item; hardware
  side ticked via #8).
- #466 merge → then Amperstrand/lpac#4 (switch router to stock lpac).
- Amperstrand/lpac#7 item 2 (`lpac driver list` segfault — needs
  backtrace/minimal repro before any upstream filing) and item 3
  (LPAC_HTTP=curl env-selection quirk — needs loader env-matching
  characterization).
- `at_etsi_csim.c:97` `aid_hex` dead code — future driver-cleanup PR only.
- Multi-channel disconnect cleanup remains mock-proven only (scenario 13);
  documented, no action.

## Durable-state map

- Fork branches: `fix/driver-lastchannel-init` @ 4922eb3 (= #469 head),
  `fix/applet-cleanup-locals-zero-init` @ cc17113 (== #467 head 3521f66),
  `pin/nr7101-known-good` @ 979c6d5, `fix/at-csim-close-on-init-failure` @
  84945d8 (= #468 head), this `upstream-drafts` branch.
- Lane repo `lpac-openwrt-lane` @ `lane/multi-target-at-backend` 06575cd
  (pushed): hwval artifacts 20261027T173836Z, pin tarball + verification
  record.
- `/tmp/opencode/*` clones and scratch are disposable; labgrid place and
  modem-lane lock released per #8 evidence.
