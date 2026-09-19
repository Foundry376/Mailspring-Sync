# Running mailsync locally (for automated agents and CI containers)

Mailsync is normally launched by the Mailspring client, which supplies an
environment the engine quietly depends on. When you launch it yourself from a
shell — especially inside a container — several of those assumptions break, and
the failures are silent or look like engine crashes. This page lists the ones
that will cost you a test run.

## TL;DR

```bash
# 1. Build (see BUILDING.md). In a container, configure libetpan with --disable-db.
# 2. Create the schema once.
CONFIG_DIR_PATH=/tmp/msync IDENTITY_SERVER=https://id.getmailspring.com \
  /abs/path/to/Mailspring-Sync/mailsync --mode migrate \
  --account "$ACCOUNT_JSON" --identity null

# 3. Run a sync. Note --orphan, and the absolute path.
CONFIG_DIR_PATH=/tmp/msync IDENTITY_SERVER=https://id.getmailspring.com \
  /abs/path/to/Mailspring-Sync/mailsync --mode sync --orphan \
  --account "$ACCOUNT_JSON" --identity null
```

Stop the process with `SIGTERM` when the test is done.

## Always pass `--orphan` (unless you are feeding stdin)

This is the big one. It is the cause of the "mailsync segfaults repeatedly and
pegs a CPU" reports from agent runs.

Without `--orphan`, `main()` calls `runListenOnMainThread()`, which treats stdin
as a liveness link to a parent Mailspring process. If stdin is closed or at EOF
— which is what `< /dev/null`, a closed pipe, or most process runners give you —
the loop:

1. Busy-spins for 30 seconds. Each iteration does `cin.clear()` / `cin.sync()` /
   `getline()` on an EOF stream and sleeps 1 ms, which measures at roughly 40%
   of one core. This is the "pegged thread".
2. Then calls `std::exit(141)`.

`std::exit()` does not stop the four worker threads (`background`,
`calContacts`, `metadata`, `metadataExpiration`). It runs static destructors
underneath them — including `DeltaStream.cpp`'s global `_globalStream`, spdlog's
logger registry, and StanfordCPPLib's `programNameForStackTrace`. The workers
keep calling `SharedDeltaStream()->emit(...)` on every model save, so they
dereference objects that have just been destroyed.

Observed on a container run with no IMAP server reachable: ~40% CPU for 30 s,
then a ~40 s hang, then **SIGSEGV (exit 139)** rather than the intended exit
141, with the crash handler printing a corrupted program name
(`addr2line: 'I\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbdU': No such file`) because it
is reading the static it just destroyed.

Both workarounds are verified to hold CPU at ~0% and shut down cleanly on
SIGTERM (exit 143):

* **`--orphan`** — `main()` joins the background thread instead of watching
  stdin. Use this whenever you do not need to send tasks.
* **Hold stdin open** — e.g. `mkfifo` and keep a writer attached, or run under a
  coprocess. Use this when the test needs to send `queue-task`, `wake-workers`,
  `need-bodies` or similar on stdin. Do not let the writer close until you are
  finished; closing it starts the 30-second countdown above.

Note that `--orphan` also changes logging: `logToFile` is
`mode == "sync" && !options[ORPHAN]`, so with `--orphan` the log goes to stdout
in the abbreviated format instead of
`$CONFIG_DIR_PATH/mailsync-<accountid>.log`.

This does not affect desktop installs. Mailspring keeps the child's stdin open
for the whole lifetime of the process and terminates it with a signal, so the
orphan path is not reached.

## Invoke the binary by a path containing "mailspring"

Running `./mailsync` exits with code **2 and prints nothing at all**. In release
builds `main()` lowercases `argv[0]` and requires it to contain `mailspring`, to
discourage rebranded forks. `./mailsync` does not match; the absolute path
`/home/you/Mailspring-Sync/mailsync` does.

Use an absolute path, or invoke it from a directory whose name contains
`mailspring`.

## Reading stack traces

Mailsync symbolizes its own crashes by shelling out to `addr2line` against
`argv[0]`, so traces look like this:

```
*** Stack trace (line numbers are approximate):
*** ??:?  SyncWorker::syncFoldersAndLabels()
*** ??:?  runBackgroundSyncWorker()
*** ??:?  main()
```

Build with `cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo .` to get line numbers
instead of `??:?`. Note that the frame-to-file attribution is approximate — the
resolver looks each address up twice (raw and load-base-relative) and keeps
whichever answer is longer, so individual frames are sometimes credited to an
unrelated compilation unit. Trust the function names over the file names.

If you are on a build from before this was fixed, every frame will instead read
like `addr2line: '/home/user/mailspring-sync/mailsync': No such file()`. That is
not a second crash — `main()` used to lowercase `exectuablePath` in place for the
fork check and then hand that same string to
`exceptions::setProgramNameForStackTrace()`, so on a case-sensitive filesystem
the binary could not be found. Any path with an uppercase letter was affected,
including this repository's own checkout. Copy the binary to an all-lowercase
path containing `mailspring` to work around it.

## Run `--mode migrate` before `--mode sync`

Against a fresh `CONFIG_DIR_PATH`, `--mode sync` starts its workers against an
empty database and every thread immediately throws:

```
*** An exception occurred during program execution:
*** no such table: _State
*** no such table: Task
*** no such table: Folder
```

and the process aborts. The client runs `--mode migrate` first; so must you.

## `--mode` is required

Omitting it prints the usage message and exits 1. On builds from before this was
fixed it instead aborted, because `main()` constructed `string mode(options[MODE].arg)`
without checking for the absent case:

```
*** A C++ exception occurred during program execution:
*** basic_string: construction from null is not valid
```

`CArg::Required` only rejects `--mode` supplied with no value; an entirely
absent option leaves `.arg` null and passes the parse.

## A silent crash with no output at all

If mailsync dies with exit code 139 and prints *nothing* — no `*** Mailspring
Sync` banner — the crash handler could not run. Two known causes:

* **Stack overflow.** `SHOULD_USE_SIGNAL_STACK` is commented out in
  `Vendor/StanfordCPPLib/exceptions.cpp`, so there is no `sigaltstack` and the
  SIGSEGV handler cannot execute on the exhausted stack.
* **`std::terminate()` raised with no active exception** (for example a
  `std::thread` destroyed while still joinable).
  `exceptions::logCurrentExceptionWithStackTrace()` opens with a bare `throw;`,
  which with no active exception calls `std::terminate()` again, re-entering the
  same handler. Measured at over 12,000 re-entries before the stack is exhausted
  and the process dies silently.

In both cases reach for `gdb`/`coredumpctl` rather than the engine's own
output — mailsync will not tell you anything.

## Container build notes

* `./configure` for libetpan auto-detects Berkeley DB. If `libdb-dev` headers
  are present in the image, libetpan compiles `mail_cache_db.c` against them but
  mailsync does not link `-ldb`, and the final link fails with
  `undefined reference to 'db_create'`. Configure libetpan with `--disable-db`.
* Ubuntu 24.04 build dependencies beyond those in `BUILDING.md`: `autoconf`,
  `automake`, `libtool` for libetpan's `autogen.sh`.
* `Vendor/mailcore2` only needs the `MailCore` target — `make MailCore`, not
  `make`. The mailcore2 tests cannot link without mailsync.
