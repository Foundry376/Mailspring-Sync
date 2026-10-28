# ICU time zone data (tzdata 2026e)

The four files ICU reads time zones from, in the version-independent "44/le" format, copied
unchanged from [unicode-org/icu-data](https://github.com/unicode-org/icu-data) at
`tzdata/icunew/2026e/44/le/`.

mailsync ships them in an `icu-tzdata` directory beside its binary and points ICU at them at
launch (`SystemTimeZone::UseZoneFilesIn`), so every platform resolves a TZID against the same,
current tzdata. Without them each platform falls back to whatever data its ICU carries: the static
ICU 70 linked on Linux holds 2021a3 (Mexico City still on DST) unless the distro ships
`/usr/share/zoneinfo-icu`, as Ubuntu's tzdata package does; vcpkg's ICU holds its release's;
macOS's libicucore the OS's.

## Updating

When IANA publishes a tzdata release and unicode-org/icu-data adds `tzdata/icunew/<version>`:

```bash
for f in zoneinfo64 windowsZones timezoneTypes metaZones; do
  curl -sfL -o Vendor/icu-tzdata/$f.res \
    https://raw.githubusercontent.com/unicode-org/icu-data/main/tzdata/icunew/<version>/44/le/$f.res
done
```

then update the version in this file's title. `--mode install-check` reports the version a build
reads (`tz_check`), and CI prints it on every platform.
