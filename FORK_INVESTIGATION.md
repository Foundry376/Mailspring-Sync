# Mailspring-Sync Fork Investigation Report

**Date:** January 2, 2026
**Forks Analyzed:** 38 total forks examined

## Summary

After investigating all 38 forks of Mailspring-Sync, I found that **most forks are synchronized with or behind the main repository**. The main repo has been actively maintained with recent PRs (#51-55) fixing memory leaks, null pointer checks, integer underflows, and SQLite transaction handling.

## Forks with Commits Ahead (Potentially Useful)

### 1. BrandonGillis/Mailspring-Sync ⭐ (Worth Investigating)
**2 commits ahead** (August 2022)

| Commit | Description | Files Changed |
|--------|-------------|---------------|
| `958f49d` | Reduce libetpan mailstream buffer_size from 8192 to 4096 bytes | `mailstream_cfstream.c`, `mailstream_socket.c`, `mailstream_ssl.c` |
| `7c3596d` | Reduce `MAX_FULL_HEADERS_REQUEST_SIZE` from 25000 to 1024 | `MailSync/SyncWorker.cpp` |

**Assessment:** These are performance/stability tuning changes. The dramatic reduction in `MAX_FULL_HEADERS_REQUEST_SIZE` (96% decrease) suggests the contributor may have been addressing specific memory or performance issues. **Worth investigating** if users report memory issues or sync problems with large mailboxes.

---

### 2. glenn2223/Mailspring-Sync (Reverted)
**2 commits ahead** (October 2022) - One commit + revert

| Commit | Description | Status |
|--------|-------------|--------|
| `786a0fd` | SMTP test fix: Replace hardcoded `email@invalid.com` with actual user email in `MCSMTPSession.cpp` | Reverted by author |

**Assessment:** The fix addressed Community issue #1625 (account setup SMTP errors). The author reverted it due to limited C++ knowledge. The underlying issue (using a hardcoded invalid email for SMTP testing) may still be worth addressing properly.

---

### 3. rvmn/Mailspring-Sync (ARM64 Support)
**4 commits ahead** (December 2021)

| Commit | Description |
|--------|-------------|
| `7a8f0ec` | Add ARM64 (aarch64) Linux build support in CMakeLists.txt |
| Various | Ubuntu/Debian package installation updates to build.sh |

**Assessment:** Adds **ARM64 Linux architecture support** by updating library paths and ICU dependencies. Could be valuable for Raspberry Pi, Apple Silicon (Linux VMs), or ARM servers.

---

### 4. mfschumann/Mailspring-Sync (Build Modernization)
**9 commits ahead** (July 2022)

| Key Changes | Description |
|-------------|-------------|
| Compiler upgrade | GCC 5 → GCC 10 |
| ICU library cleanup | Remove `icule` and `icutest` |
| Build simplification | Skip OpenSSL/libcurl compilation |

**Assessment:** Build system modernization with newer compiler. Many of these changes appear experimental (several are labeled "Hack:"). The GCC upgrade idea has likely been superseded by the GitHub Actions updates in the main repo.

---

### 5. kardysm/Mailspring-Sync (Build Updates)
**6 commits ahead** (March 2021)

| Changes | Description |
|---------|-------------|
| curl update | 7.70.0 → 7.75.0 |
| Build automation | Added `-y` flag for non-interactive apt operations |
| libidn2 fix | Reinstall libidn2-dev after curl compilation |

**Assessment:** Minor build script improvements. The curl version is now outdated (current is 8.x). The libidn2 reinstall fix could be relevant if users hit that specific issue.

---

### 6. fanrenng/Mailspring-Sync (Docker Build)
**3 commits ahead** (June 2022)

| Commit | Description |
|--------|-------------|
| `65b0c55` | Add Dockerfile for Ubuntu 16.04 build environment |

**Assessment:** PR #4 was closed by maintainer bengotow with note: "we've moved the build flow to GitHub Actions and also updated a lot of the dependencies which should make it easier to build locally." **No longer needed.**

---

### 7. refind-email/Mailspring-Sync (Build Fixes)
**3 commits ahead** (August 2021)

| Commit | Description |
|--------|-------------|
| `1f358cd` | Comment out `apt-get build-dep curl`, use `ln -sf` for symlinks |

**Assessment:** Minor build script workarounds. Using `-sf` for symlink creation is a reasonable fix but may already be addressed in current build.

---

## Open Pull Requests

### PR #7: Office 365 GCC Environment Support (Draft)
**Author:** thomaskcr
**Status:** Draft (since January 2023)

Adds support for US Government Community Cloud (GCC) Office 365 environments.

**Assessment:** Limited practical value—as noted in PR comments, most GCC High/DoD tenants disable IMAP/SMTP for security. Would require broader OAuth2 provider configuration changes.

---

## Recommendations

### Worth Considering
1. **ARM64 Linux support** (rvmn) - If there's demand for ARM builds
2. **Buffer/header size tuning** (BrandonGillis) - Investigate if there are related performance issues
3. **SMTP test email fix** (glenn2223) - The underlying issue may still exist

### Already Addressed
- Docker builds → GitHub Actions CI/CD handles this
- VS2022 upgrade → Already merged (PR #10)
- GCC compiler updates → Handled in GitHub Actions workflow

### Not Recommended
- GCC High Office 365 support - Too niche, protocol often disabled
- Most build script changes - Outdated or superseded

---

## Methodology

Analyzed all 38 forks using GitHub's compare feature (`/compare/master...fork:master`). Cross-referenced with closed PRs to identify already-merged contributions.
