# Upstream drafts — staged for owner, 2026-10-27 (lane pr468-align)

Everything needed to fire the estkme-group actions lives here; agents must
not post these (AGENTS.md manual-only rule). Run from the owner's
interactive shell, in this order:

1. Fix PR #468's stale body (diff no longer matches description):
   `gh pr edit 468 -R estkme-group/lpac --title "fix(driver/apdu/at): close leaked logical channel when an at_csim session aborts" --body-file upstream-drafts/pr468-body.md`

2. Post the segfault issue draft (text at the bottom of Amperstrand/lpac#1),
   then create the zero-init PR referencing it (branch already on the fork
   at cc17113):
   `gh pr create -R estkme-group/lpac -H Amperstrand:fix/applet-cleanup-locals-zero-init -b main -t "fix(applet): zero-initialize _cleanup_ locals — post-error teardown SIGSEGV" -F upstream-drafts/pr-applet-zeroinit-body.md`

3. Optional (nudge if septs stays silent ~a week on #468):
   `gh pr comment 468 -R estkme-group/lpac --body-file upstream-drafts/pr468-review-reply.md`

File headers inside each draft carry the same commands. Related durable
state: PR #468 head = 84945d8 (fix/at-csim-close-on-init-failure), dogfood
pin = pin/nr7101-known-good @ 979c6d5 (13/13 mock matrix), hardware
validation protocol = Amperstrand/lpac#8, leak tracker = Amperstrand/lpac#5,
segfault research = Amperstrand/lpac#1.
