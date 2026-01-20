## MailCore 2 on Windows ##

MailCore 2 is built as part of the Mailspring-Sync project. External dependencies are managed via [vcpkg](https://vcpkg.io/) and are automatically installed during the GitHub Actions build.

### Dependencies ###

The following dependencies are installed via vcpkg (defined in `vcpkg.json` at the mailsync project root):

- openssl
- curl
- libxml2
- zlib
- icu
- libiconv
- tidy-html5
- ctemplate
- pthreads
- cyrus-sasl

### GitHub Actions Build ###

Windows builds run automatically via GitHub Actions. The workflow:

1. Sets up vcpkg using `lukka/run-vcpkg`
2. Installs dependencies from `vcpkg.json` with triplet `x86-windows`
3. Builds using MSBuild with vcpkg integration enabled

See `.github/workflows/build-windows.yml` for details.

### Local Development ###

To build locally on Windows:

1. Install vcpkg:
   ```cmd
   git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   set VCPKG_ROOT=C:\vcpkg
   ```

2. Install dependencies (from mailsync project root):
   ```cmd
   vcpkg install --triplet x86-windows
   ```

3. Open `Windows/mailsync.sln` in Visual Studio and build.

Public headers are located in `mailcore2/build-windows/include`.
