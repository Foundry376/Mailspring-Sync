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

## Notes

- Always test builds after applying patches
- Keep the upstream clone available during review
- Document exclusions for future reference
- Consider upstream activity level - dormant repos may not need frequent updates
