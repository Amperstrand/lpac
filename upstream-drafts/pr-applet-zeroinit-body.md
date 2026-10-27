<!-- STAGED FOR OWNER — create PR on estkme-group/lpac (upstream).
     Per AGENTS.md 'External-service writes are MANUAL-ONLY': agents stage,
     owner posts from their interactive shell.
     SEQUENCE: post the issue draft from Amperstrand/lpac#1 FIRST (bottom of
     that tracker), then this PR referencing it.
     Exact command (after pushing branch fix/applet-cleanup-locals-zero-init,
     already on the fork at cc17113):
       gh pr create -R estkme-group/lpac -H Amperstrand:fix/applet-cleanup-locals-zero-init -b main -t "fix(applet): zero-initialize _cleanup_ locals — post-error teardown SIGSEGV" -F /tmp/opencode/upstream-drafts/pr-applet-zeroinit-body.md -->

## Problem

The lpac CLI prints the correct ES10 error JSON and then segfaults (exit 139)
whenever an ES10 exchange fails mid-session — reachable on any APDU backend
whenever the card answers an error SW to a STORE DATA / ES10 exchange:

```
$ lpac chip info
{"type":"lpa","payload":{"code":-1,"message":"es10c_get_eid","data":""}}
Segmentation fault (core dumped)          # exit 139
```

## Root cause

Several `_cleanup_`-attributed locals across `src/applet/` are declared
without an initializer. When the applet returns early on an ES10 error —
before the variable is ever assigned — the cleanup attribute still fires at
scope exit and runs the `_free` function on uninitialized stack contents
(`free(garbage)` → SIGSEGV). 11 sites across 7 files:

- `chip/info.c`: `addresses`, `ratList`, `euiccinfo2`
- `notification/dump.c`, `list.c`, `process.c`, `remove.c`: the
  `es10b_notification_metadata_list`/`es10b_pending_notification` locals
- `profile/list.c`: `profiles`

## Fix

Zero-initialize every `_cleanup_`-attributed local at declaration
(`= NULL` for pointers, `= {0}` for structs) — the same treatment merged
upstream in #467 for a subset of sites; this covers the remaining ones.

## Reproducer (x86, no hardware)

A scripted AT device (pty) that answers MANAGE CHANNEL open (SW 9000),
ISD-R SELECT by AID (FCI + SW 9000), then `6D00` to the STORE DATA exchange:
`lpac chip info` then prints the `es10c_get_eid` error and crashes pre-fix;
with this change it exits cleanly with the same error JSON (rc 255, no
signal). Verified pre-fix (SIGSEGV, exit 139) / post-fix (clean, exit 255)
on builds of current `main` with only this change applied.

## Scope

This fixes the CLI-side post-error teardown crash only. It does not touch
the at_csim response parser (a malformed `+CSIM:` answer crashes inside the
driver before any applet teardown — separate issue), and #419's exact case
may additionally need driver-side initialization of `lastChannelId`
(qmi_qrtr) — the same uninit family.
