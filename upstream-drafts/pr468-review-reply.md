<!-- STAGED FOR OWNER — reply to septs' review comment on estkme-group/lpac PR #468.
     Per AGENTS.md 'External-service writes are MANUAL-ONLY': agents stage,
     owner posts from their interactive shell.
     Exact command:
       gh pr comment 468 -R estkme-group/lpac --body-file /tmp/opencode/upstream-drafts/pr468-review-reply.md
     (or reply inline on the review thread with this text)
     Status: branch already reworked + pushed as 84945d8; this reply closes the loop. -->

Reworked as requested — the new field is gone and the driver now reuses
`userdata->channels` exactly like `at_etsi`:

- `logic_channel_open()` registers the channel with `at_channel_set()` right
  after the MANAGE CHANNEL open succeeds (identifier = channel number as a
  string, mirroring how `at_etsi` stores the `+CCHO` response), and the
  failure paths after the open close the channel and clear the slot.
- `logic_channel_close()` sends the close APDU and clears the slot.
- `disconnect()` walks the registry and closes every still-registered
  channel, so a session that aborts with channels open can't leak them —
  including more than one open channel, which the previous single-variable
  version couldn't track.

The diff is now `driver/apdu/at_etsi_csim.c` only (+29 lines); the
`at_cmd.h`/`at_cmd_unix.c`/`at_cmd_win32.c` changes from the earlier push
are dropped. A/B-tested against the previous head with a mock-modem
edge-case matrix (12 scenarios, wire-level identical) plus a multi-channel
abort probe: two channels open, session aborts, disconnect closes both
(card-side pool empty afterwards).
