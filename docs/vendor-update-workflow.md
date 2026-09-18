# Vendor Library Update Workflow

This document describes the process for cherry-picking upstream bug fixes and improvements into vendored dependencies (mailcore2, libetpan) while preserving local modifications.

## Overview

The vendored libraries in `Vendor/` contain local modifications specific to Mailspring. When updating these libraries, we cherry-pick only relevant upstream commits rather than rebasing entirely, to avoid conflicts with local changes.

## Load-Bearing Local Modifications

Most local modifications announce themselves if they are lost: a build breaks, or a feature
visibly stops working. These two do not, so check them explicitly after any update to the
library that carries them.

### `Vendor/mailcore2` - `String::UTF8Characters()` substitutes U+FFFD (MCString.cpp)

`ConvertUTF16toUTF8()` with `lenientConversion` passes an unpaired UTF-16 surrogate through
as three-byte CESU-8 rather than substituting it, and those bytes are not valid UTF-8. ICU's
UTF-7 and IMAP-mailbox-name converters both decode a lone surrogate without reporting an
error, so a MIME part labelled `charset=utf-7` is enough for a mail server to put one in a
mailcore string. `UTF8Characters()` is where mailcore hands text to the rest of Mailspring,
and roughly forty call sites treat what it returns as UTF-8 - including everything that ends
up in a `MailModel`, and therefore in `json::dump()`.

The fix is local to `String::UTF8Characters()`, which is the only caller of
`ConvertUTF16toUTF8()` in the whole library; `ConvertUTF.c` itself is untouched upstream
reference code.

### `Vendor/nlohmann` - `dump()` defaults to `error_handler_t::replace` (json.hpp)

Upstream defaults to `strict`, which throws `json::type_error.316` on ill-formed UTF-8.
Mailsync serializes text that arrived from a mail server, in places where a throw is fatal
rather than recoverable: the delta stream is dumped on a detached thread, models are dumped
while being written to SQLite from workers whose only catch-all calls `abort()`, and the
crash reporter dumps the exception it is reporting - there the throw escapes its own catch
handler and reaches `std::terminate`.

Two defaults carry this, `basic_json::dump()` and the `serializer` constructor (the second is
what `operator<<` uses). Only the defaults are changed; passing `strict` explicitly still
throws. This is not fixed upstream and is unlikely to be - it is a documented public API
default, so a version bump will silently restore the throw.

### Checking them

`runInstallCheck()` in `MailSync/main.cpp` asserts both, and CI runs
`mailsync --mode install-check` on every platform we ship. If either is lost, `utf8_check`
reports which one and the test job for every distro fails. After updating either library,
run:

```bash
CONFIG_DIR_PATH=/tmp IDENTITY_SERVER=https://id.getmailspring.com ./mailsync --mode install-check
```

and confirm `"utf8_check": {"success": true}`.

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

## Notes

- Always test builds after applying patches
- Keep the upstream clone available during review
- Document exclusions for future reference
- Consider upstream activity level - dormant repos may not need frequent updates
