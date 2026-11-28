#pragma once

/* SGP.22 v2.2 §3.2.4 (line 7259) + §2.4.5.2 (2821-2824): the LPA SHALL not
 * display Provisioning Profiles to the End User, and they SHALL not be
 * selectable nor deletable. Found by spec-lab D2-F1. The guard resolves the
 * target identifier's profileClass before any mutation; --force overrides
 * (operator tooling, not an end-user LUI). */

int profile_class_check(const char *id, int allow_force, const char **class_out);
/* returns: 0 = not provisioning (or force passed), -1 = lookup failed,
 * 1 = provisioning and not forced (the caller must refuse) */
