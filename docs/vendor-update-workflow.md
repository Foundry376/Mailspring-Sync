# Vendor Library Update Workflow

This document describes the process for cherry-picking upstream bug fixes and improvements into vendored dependencies (mailcore2, libetpan) while preserving local modifications.

## Overview

The vendored libraries in `Vendor/` contain local modifications specific to Mailspring. When updating these libraries, we cherry-pick only relevant upstream commits rather than rebasing entirely, to avoid conflicts with local changes.

## Process

### 1. Identify Local Changes

First, identify all commits that have modified the vendor library:

```bash
git log --oneline -- Vendor/<library>/
```

Review each commit to understand what local modifications exist and why they were made.

### 2. Identify Base Version

Check the library's version files (e.g., `podspec.json`, `VERSION`, `README.md`) to determine the base upstream version:

```bash
grep -r "version" Vendor/<library>/*.json
```

### 3. Analyze Upstream Commits

Clone the upstream repository and list commits since the base version:

```bash
git clone --depth=100 https://github.com/<org>/<repo>.git /tmp/<repo>-upstream
cd /tmp/<repo>-upstream
git log --oneline <base-tag>..master
```

### 4. Categorize Commits

Review each upstream commit and categorize:

**Include:**
- Security fixes
- Bug fixes in core functionality
- New features that benefit the project
- Updates to vendored sub-dependencies

**Exclude:**
- Platform-specific changes (Swift/iOS/Android) if not used
- Build system changes that would conflict with local config
- Documentation-only changes
- Changes to wrapper code (Obj-C, Java) if only C++ core is used

### 5. Generate and Apply Patches

For each commit to cherry-pick:

```bash
# Generate patch
cd /tmp/<repo>-upstream
git format-patch -1 <commit-sha> -o /tmp/patches/

# Check if patch applies cleanly
cd /path/to/project
git apply --check --directory=Vendor/<library> -p1 /tmp/patches/<patch-file>

# Apply patch
git apply --directory=Vendor/<library> -p1 /tmp/patches/<patch-file>
```

### 6. Commit and Document

Create a commit with detailed documentation of what was cherry-picked:

```bash
git add Vendor/<library>/
git commit -m "Cherry-pick upstream <library> bug fixes and improvements

Cherry-picked commits:
1. <sha> - <description>
2. <sha> - <description>
...

Excluded: <reason for exclusions>
"
```

## Example: mailcore2 Update (Dec 2025)

### Base Version
- v0.6.4 (August 2020)

### Local Modifications (15 commits)
- Plaintext rendering changes (`MCHTMLRenderer.cpp`)
- SMTP HELO/EHLO fixes
- Certificate error logging improvements
- SMTP test email fix
- Build configuration updates

### Cherry-picked from Upstream
1. `fad23d73` - SSL Certificate checking in IMAP StartTLS (Security)
2. `cccebc79` - IMAPMessagesRequestKindFullHeaders fix (Bug)
3. `29f9488a` - IMAPMessagesRequestKindAllHeaders flag (Feature)

### Excluded
- Swift Package Manager changes (not used)
- Android build updates (not used)
- Objective-C NSCoding fix (C++ core only)
- Xcode 13+ build fixes (would conflict with local config)
- Documentation updates

## Review: Oct 2026

Covered upstream through libetpan `13c00bd` (2026-10-23) and mailcore2 `e512ab51`
(2026-09-27). libetpan was revived in May 2026 (1.10, 1.10.1) and its PR queue
emptied; most of its ~240 commits are CI, packaging, or new ActiveSync/JMAP/Gmail/
S/MIME/PGP layers we don't build. Start the next review from those SHAs.

### Taken (libetpan)
- IMAP parser: `68de795` (EOF spin in literals), `266d59f`, `13c00bd` + `ddf1d98`,
  `1ea9481`, `7bb634c` (literal cap raised to 1 GiB)
- IMAP behaviour: `1f52a62` (IDLE continuation, #393), `229bdac` (home.pl bare `\` flag)
- IMF: `72e4825`, `2669d21`, `e5db7e7`, `28bcbc4`, `b64741f`, `f08af1a`, `f54f5de`
- MIME: `3f14913`, `e6ca7d9` (depth limit raised to 64), `022ecfd`, `0e40abc`,
  `b5d4b2c`, `28e2dc8`
- Streams: `9637b6c`, the WAIT_TIMEOUT checks from `0e45e28`, `acfe80a` (ported)

### Taken (mailcore2)
- `htmlEncodedString` static buffer (from closed PR #2023; not fixed upstream)

### Deliberately not taken
- `be0520a`, `23fd6ae`, `c77bc94` (zero-byte writes): sockets are non-blocking, so 0
  means "would block". `be0520a` caused upstream #468; `c77bc94` still has that bug.
- `9c1c2b4`, `5273665`, `bcaf9c7`, `4020850` (MMAPString allocator): global change
  that needed two regression fixes upstream, for small leaks.
- `7a90eea` (skip malformed FETCH): skips to the first CRLF, which lands inside a
  literal, and leaves the skipped UID to be deleted by the orphan sweep.
- mailcore2 `03a19472` (IDLE teardown race): needs a cross-thread `disconnect()`,
  which the engine never does.
- `9d92e31` + `8efd727` (RFC 2231 decoding): deferred; MCAttachment would run the
  already-decoded UTF-8 filename through charset detection again.
- `4baa384` (SMTP LOGIN/PLAIN without SASL): compiled only without `USE_SASL`; worth
  wiring into MCSMTPSession separately to drop the Cyrus plugin dependency.
- PR #427 (FD_SETSIZE check, reverted in 3457ac5): Windows SOCKETs are handle
  values, usually above FD_SETSIZE (64), so the check rejected valid sockets.

## Notes

- Always test builds after applying patches
- Keep the upstream clone available during review
- Document exclusions for future reference
- Consider upstream activity level - dormant repos may not need frequent updates
