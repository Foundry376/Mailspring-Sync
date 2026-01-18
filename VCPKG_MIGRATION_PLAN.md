# vcpkg Migration Plan: Partial Migration (Option A)

## Overview

Migrate pre-built binary dependencies to vcpkg while keeping source-based vendored libraries as-is. Development happens on Mac, builds run in GitHub Actions on Windows.

## Libraries to Migrate to vcpkg

| Library | Current Location | vcpkg Package | Notes |
|---------|------------------|---------------|-------|
| OpenSSL 3.x | Vendor/mailcore2/Externals | `openssl` | Dynamic linking |
| libcurl | Windows/Externals | `curl` | Currently static, switch to dynamic |
| libxml2 | Windows/Externals, Vendor/mailcore2/Externals | `libxml2` | Dynamic linking |
| zlib | Windows/Externals, Vendor/mailcore2/Externals | `zlib` | Dynamic linking |
| ICU | Vendor/mailcore2/Externals | `icu` | Dynamic linking |
| libiconv | Windows/Externals, libetpan/third-party | `libiconv` | Dynamic linking |
| libtidy | Vendor/mailcore2/Externals | `tidy-html5` | Dynamic linking |
| Cyrus SASL | Vendor/mailcore2/Externals | `cyrus-sasl` | Dynamic linking |
| ctemplate | Vendor/mailcore2/Externals | `ctemplate` | Dynamic linking |
| pthreads | Vendor/mailcore2/Externals | `pthreads` | Dynamic linking |

## Libraries to Keep Vendored (Source in Repo)

- SQLiteCpp/sqlite3 (Vendor/SQLiteCpp)
- nlohmann-json (Vendor/nlohmann)
- spdlog (Vendor/spdlog)
- libetpan (Vendor/libetpan) - custom fork
- mailcore2 (Vendor/mailcore2) - custom fork
- icalendarlib (Vendor/icalendarlib)
- StanfordCPPLib (Vendor/StanfordCPPLib)

---

## Implementation Steps

### Phase 1: Create vcpkg Configuration Files

#### Step 1.1: Create vcpkg.json manifest

Create `vcpkg.json` in the repository root:

```json
{
  "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json",
  "name": "mailspring-sync",
  "version": "1.0.0",
  "dependencies": [
    "openssl",
    "curl",
    "libxml2",
    "zlib",
    "icu",
    "libiconv",
    "tidy-html5",
    "ctemplate",
    "pthreads"
  ],
  "overrides": [],
  "builtin-baseline": "c82f74667287d3dc386bce81e44964370c91a289"
}
```

Note: cyrus-sasl may need special handling - verify availability in vcpkg.

#### Step 1.2: Create vcpkg triplet configuration

For 32-bit Windows with dynamic linking, use triplet: `x86-windows`

If custom triplet needed, create `triplets/x86-windows-mailspring.cmake`:
```cmake
set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
```

---

### Phase 2: Update GitHub Actions Workflow

#### Step 2.1: Modify build-windows.yml

```yaml
name: Build Windows

on:
  push:
    branches: [main, master]
  pull_request:
    branches: [main, master]
  create:
    tags:
      - '*'

env:
  VCPKG_DEFAULT_TRIPLET: x86-windows

jobs:
  build-windows:
    runs-on: windows-2022

    steps:
      - name: Checkout code
        uses: actions/checkout@v4

      - name: Setup MSBuild
        uses: microsoft/setup-msbuild@v2

      # NEW: Setup vcpkg
      - name: Setup vcpkg
        uses: lukka/run-vcpkg@v11
        with:
          vcpkgGitCommitId: 'c82f74667287d3dc386bce81e44964370c91a289'
          runVcpkgInstall: true

      - name: Set commit hash
        shell: pwsh
        run: |
          $commit = $env:GITHUB_SHA.Substring(0,8)
          echo "COMMIT=$commit" >> $env:GITHUB_ENV

      - name: Create app directory
        shell: pwsh
        run: mkdir ..\app

      # Keep this for now - may be able to remove later
      - name: Install VC2010 as we need msvcr100.dll
        shell: cmd
        run: choco install -y vcredist2010

      # MODIFIED: Build with MSBuild, pointing to vcpkg toolchain
      - name: Build with MSBuild
        shell: cmd
        run: |
          cd Windows
          msbuild.exe mailsync.sln /property:Configuration=Release;Platform=Win32 /p:VcpkgEnableManifest=true

      # MODIFIED: Copy build artifacts including vcpkg DLLs
      - name: Copy build artifacts and dependencies
        shell: pwsh
        run: |
          IF (!(Test-Path "..\app\dist")) { mkdir ..\app\dist }

          # Copy build output
          Copy-Item ".\Windows\Release\*" -Destination "..\app\dist" -Recurse

          # Copy vcpkg DLLs
          $vcpkgInstalled = "$env:VCPKG_ROOT\installed\x86-windows\bin"
          Copy-Item "$vcpkgInstalled\*.dll" -Destination "..\app\dist" -Force

          # Copy MSVC runtime DLLs
          Copy-Item "C:\Windows\SysWOW64\msvcr100.dll" "..\app\dist\" -Force
          Copy-Item "C:\Windows\SysWOW64\msvcr120.dll" "..\app\dist\" -Force
          Copy-Item "C:\Windows\SysWOW64\msvcp100.dll" "..\app\dist\" -Force
          Copy-Item "C:\Windows\SysWOW64\msvcp120.dll" "..\app\dist\" -Force
          Copy-Item "C:\Windows\SysWOW64\msvcp140.dll" "..\app\dist\" -Force
          Copy-Item "C:\Windows\SysWOW64\vcruntime140.dll" "..\app\dist\" -Force

          # Copy UCRT DLLs
          Copy-Item "C:\Program Files (x86)\Windows Kits\10\Redist\ucrt\DLLs\x86\*" "..\app\dist\" -Force

      # ... rest of workflow unchanged ...
```

---

### Phase 3: Update Visual Studio Project Files

#### Step 3.1: Update mailsync.vcxproj

Changes needed:

1. **AdditionalIncludeDirectories** - Add vcpkg include path:
   ```xml
   $(VcpkgRoot)\installed\$(VcpkgTriplet)\include;...existing paths...
   ```

2. **AdditionalLibraryDirectories** - Add vcpkg lib path:
   ```xml
   $(VcpkgRoot)\installed\$(VcpkgTriplet)\lib;...existing paths...
   ```

3. **AdditionalDependencies** - Update library names:
   - Remove: `libcurl_a.lib` (static)
   - Add: `libcurl.lib` (dynamic import lib)

4. **PreprocessorDefinitions** - Remove `CURL_STATICLIB` (switching to dynamic)

5. **PostBuildEvent** - Remove manual DLL copy commands for vcpkg-managed libraries

#### Step 3.2: Update mailcore2.vcxproj

Similar changes:

1. **AdditionalIncludeDirectories** - Add vcpkg paths
2. **AdditionalLibraryDirectories** - Add vcpkg paths
3. **AdditionalDependencies** - Update to vcpkg library names:
   - `libssl.lib` → keep (vcpkg uses same name)
   - `libcrypto.lib` → keep (vcpkg uses same name)
   - Update ICU library names if different
   - Update other library names as needed

#### Step 3.3: Update libetpan.vcxproj

Check if libetpan references any of the migrated libraries and update accordingly.

---

### Phase 4: Remove Old Pre-built Binaries

After successful build verification, remove these directories/files:

```
# From Windows/Externals/
Windows/Externals/lib/libcurl_a.lib
Windows/Externals/lib/libxml2.*
Windows/Externals/lib/zlib.*
Windows/Externals/lib/iconv_a.lib
Windows/Externals/include/curl/
Windows/Externals/include/libxml/
Windows/Externals/include/iconv.h

# From Vendor/mailcore2/Externals/
Vendor/mailcore2/Externals/bin/*.dll (vcpkg-managed ones)
Vendor/mailcore2/Externals/lib/*.lib (vcpkg-managed ones)
Vendor/mailcore2/Externals/include/openssl/
Vendor/mailcore2/Externals/include/libxml/
Vendor/mailcore2/Externals/include/unicode/ (ICU)
Vendor/mailcore2/Externals/include/sasl/
Vendor/mailcore2/Externals/include/tidy*
Vendor/mailcore2/Externals/dll/

# From Vendor/libetpan/third-party/
Vendor/libetpan/third-party/bin/
Vendor/libetpan/third-party/lib/
Vendor/libetpan/third-party/include/ (vcpkg-managed headers only)
```

---

### Phase 5: Testing Strategy

#### Step 5.1: Local Testing (on Windows VM or remote)

1. Clone repo on Windows machine
2. Install vcpkg: `git clone https://github.com/microsoft/vcpkg && .\vcpkg\bootstrap-vcpkg.bat`
3. Set `VCPKG_ROOT` environment variable
4. Run: `vcpkg install --triplet x86-windows`
5. Open solution in Visual Studio, build, test

#### Step 5.2: CI Testing

1. Push changes to a feature branch
2. Verify GitHub Actions build succeeds
3. Download artifact, test on Windows
4. Run install-check to verify SSL/TLS connectivity

---

## Detailed File Changes Summary

### New Files to Create

| File | Purpose |
|------|---------|
| `vcpkg.json` | vcpkg manifest listing dependencies |
| `vcpkg-configuration.json` | (optional) vcpkg registry configuration |

### Files to Modify

| File | Changes |
|------|---------|
| `.github/workflows/build-windows.yml` | Add vcpkg setup, modify build commands |
| `Windows/mailsync.vcxproj` | Update include/lib paths, dependencies |
| `Vendor/mailcore2/build-windows/mailcore2/mailcore2/mailcore2.vcxproj` | Update include/lib paths, dependencies |
| `Vendor/libetpan/build-windows/libetpan/libetpan.vcxproj` | Update if needed |

### Files/Directories to Delete (after verification)

- `Windows/Externals/lib/` (most files)
- `Windows/Externals/include/curl/`
- `Vendor/mailcore2/Externals/bin/` (vcpkg-managed DLLs)
- `Vendor/mailcore2/Externals/lib/` (vcpkg-managed libs)
- `Vendor/mailcore2/Externals/include/` (vcpkg-managed headers)
- `Vendor/mailcore2/Externals/dll/`
- `Vendor/libetpan/third-party/` (most contents)

---

## Risk Mitigation

1. **Keep a backup branch** - Create `pre-vcpkg-migration` branch before starting

2. **Incremental approach** - Consider migrating one library at a time:
   - Start with OpenSSL (already partially done)
   - Then curl + zlib
   - Then libxml2 + ICU
   - Finally the rest

3. **Version pinning** - Use vcpkg baseline to lock versions and avoid surprise updates

4. **Fallback plan** - Keep old Externals directories until fully verified (just remove from build paths)

---

## Estimated Effort

| Task | Time Estimate |
|------|---------------|
| Create vcpkg.json and test locally | 2-4 hours |
| Update GitHub Actions workflow | 1-2 hours |
| Update mailsync.vcxproj | 2-3 hours |
| Update mailcore2.vcxproj | 2-3 hours |
| Update libetpan.vcxproj (if needed) | 1-2 hours |
| Testing and debugging | 4-8 hours |
| Cleanup old binaries | 1 hour |
| **Total** | **13-23 hours (2-3 days)** |

---

## Verified vcpkg Availability

| Package | Available | Platform Support | Notes |
|---------|-----------|------------------|-------|
| cyrus-sasl | ✅ Yes | linux, osx, windows (non-UWP) | Added in vcpkg 2024.08.23 |
| ctemplate | ✅ Yes | windows (non-ARM) | Version v2020-09-14#5 |
| openssl | ✅ Yes | All platforms | OpenSSL 3.x |
| curl | ✅ Yes | All platforms | |
| libxml2 | ✅ Yes | All platforms | |
| zlib | ✅ Yes | All platforms | |
| icu | ✅ Yes | All platforms | |
| libiconv | ✅ Yes | All platforms | |
| tidy-html5 | ✅ Yes | All platforms | |
| pthreads | ✅ Yes | Windows | pthreads-windows package |

## Additional Finding: libetpan Also Needs OpenSSL Updates

**IMPORTANT**: The `libetpan.vcxproj` still references old OpenSSL 1.1 library names:
- Debug: `libsslMDd.lib`, `libcryptoMDd.lib`
- Release: `libsslMD.lib`, `libcryptoMD.lib`

These need to be updated to `libssl.lib` and `libcrypto.lib` for OpenSSL 3.x compatibility.

## Open Questions

1. **ICU version**: Current is ICU 54 (very old). vcpkg has newer versions - may need code changes if API differences exist.

2. **Mixed architectures**: If x64 support is needed in future, plan for dual-triplet builds.

3. **SASL plugins**: Current setup includes separate plugin libs (plugin_anonymous.lib, plugin_digestmd5.lib, etc.). Verify vcpkg's cyrus-sasl includes these or if they need separate handling.
