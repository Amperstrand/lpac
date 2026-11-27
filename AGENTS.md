# AGENTS.md — Amperstrand/lpac

## What this repo is

This is a **temporary fork** of [lpac](https://github.com/estkme-group/lpac)
maintained for **experimentation and hardware dogfooding** only. It exists so
we can:

- Test changes on real hardware (NR7101, GL-E750, MF286D class devices)
- Dogfood fixes before proposing them upstream
- Carry patches that are useful to us but not yet upstream-ready
- Build custom artifacts (AT-only static ELF, driver .so bundles) for
  /overlay-constrained OpenWrt targets

## Operating rules (same as other Amperstrand projects)

1. **All work stays in this repository.** We never push, PR, file issues,
   comment, or star anything in the upstream repo (estkme-group/lpac) or any
   other external repository. Upstreaming is a **human decision** made by the
   owner after review — agents may draft the upstream proposal as a local
   document but never submit it.

2. **The fork is disposable.** When our patches land upstream, this fork
   goes away. Do not build permanent infrastructure on top of it.

3. **Patches that are proven on hardware** get a clear commit message with
   the evidence (what was tested, on what hardware, what the result was).
   Patches that are NOT hardware-proven are labeled as such.

4. **The build scripts live in [Amperstrand/lpac-openwrt](https://github.com/Amperstrand/lpac-openwrt)**
   (separate repo) — that repo handles the SDK setup, cross-compilation,
   and artifact verification for the various targets we support.

## Current patches carried on this fork (vs upstream)

| Patch | Status | Purpose |
|---|---|---|
| TLS verification ON by default | hardware-proven | `LPAC_HTTP_SSL_VERIFY=0` opts out; `LPAC_HTTP_CURL_CAINFO` for trust store |
| `LPAC_APDU_AT_TIMEOUT_MS` override | logic-tested, hardware-pending | Raises per-APDU deadline for BPP installs on tired cards |
| `LPAC_DEBUG_ES10B=1` hex-dump | deployed (Felix proved on hardware) | Dumps raw card response before DER parsing — diagnoses "unknown,unknown" failures |
| Default MSS 120→250 (PR #11) | deployed + mechanism-proven on bench NR7101 | SGP.22 §2.5.5: initial SBPP segment (typically 183 B) must fit one APDU; 120 split it → invalidSignature → "unknown,unknown" (estkme-group#474). 250 not 255: Windows PC/SC uint8 total-length truncation (estkme-group#473). Upstream candidate when owner decides |
| `--help` before euicc_init | deployed on bench unit | `lpac chip --help` no longer touches the card |
| AT-only static ELF build | hardware-proven | Single binary, libc-only NEEDED — for /overlay-constrained targets |

## Open defects (spec-anchored, not yet fixed)

| Issue | Class | Spec anchor |
|---|---|---|
| #12 | mid-stream transport error discards accumulated response (early PIR from failed BPP install lost) | SGP.22 v2.2 §2.5.6 + estkme-group#474 fix-2 |
| (conwrt #866) | unknown,unknown conflates unmapped-error with parse-failure | SGP.22 v2.2 §2.5.6 ErrorResult surfacing |

## External etiquette (hard rule)

**Never interact with repositories outside the Amperstrand organization.**
This includes:
- No issues, PRs, comments, or reviews on estkme-group/lpac
- No starring or watching external repos
- No discussion posts on external forums referencing this fork
- Research and referencing is fine; contributing is not

If a fix is good enough for upstream, the OWNER decides when and how to
propose it. Agents prepare the evidence, not the submission.
