## Mailspring-Sync

This repository contains the source code for Mailspring's sync engine, a native
C++11 codebase that targets Mac, Windows, and Linux. It leverages the MailCore2
IMAP/SMTP framework and uses sqlite3 to store mail data in a JSON format.

This codebase is GPLv3 licensed. You are welcome to copy, modify, and
distribute this code for private or public use, but all derivative works must
be open-sourced with a GPLv3 license. If you make improvements to mailsync that
would benefit the upstream project, pull requests are greatly appreciated. If
you have questions about licensing Mailsync or whether your use is acceptable,
please email me at `ben@foundry376.com`.

The names of the Mailspring or Mailspring Mailsync project nor the names of its
contributors may be used to endorse or promote products derived from this
software without specific prior written permission.

## Contributing

Mailspring-Sync is entirely open-source. High-quality pull requests and
contributions are welcome! When you're getting started, you may want to join our
[Discourse](https://community.getmailspring.com/) so you can ask questions and
learn from other people doing development.

[![Contributor Covenant](https://img.shields.io/badge/Contributor%20Covenant-v2.0%20adopted-ff69b4.svg)](CODE_OF_CONDUCT.md)

### Core Concepts

Each instance of Mailsync runs email, contact, and calendar sync for a single
account. Mailspring runs one mailsync process for each connected email account,
and automatically starts/stops/restarts them as necessary. This design means
that auth failures, connection errors, etc. can simply terminate the process.

Mailsync can be run on a standard command line, which makes it easy to test
separate from Mailspring. It requires an account and Mailspring ID, and these
can be provided either as arguments or as newline-separated data on `stdin`. If
you're developing in Xcode or in Visual Studio, you can configure the debugger
to pass these arguments automatically so testing is easy.

When Mailsync modifies mail data, it emits all modified objects to stdout as
newline-separated JSON objects. This restricts the kinds of queries that can be
used to modify the database but is critical for providing data to the
Mailspring UI, which is built on reactive data patterns and uses observable
queries. These object changes are broadcast within the Electron app and cause
observable queries backing various views to update and display new data.

Mailsync accepts "tasks" via `stdin` as newline-separated JSON objects. Tasks
are added to a queue table in sqlite (so task completions appear as object
modifications on `stdout`), and are divided into two parts: an immediate
"local" operation and a "remote" operation that requires network access and may
be retried. This design is necessary for Mailspring, because it's reactive
design means no UI changes occur until the task executes "local" changes and
modifies the database.

### Sync Approach

Mailspring uses a fairly basic syncing algorithm, which runs on two threads
with two open connections to the mail server. Within each thread, work is
performed synchronously.

- **Background Worker**: Periodically iterates over folders and (depending on
the supported IMAP features) uses `CONDSTORE / XYZRESYNC` to check for mail or
performs either a "local" or "deep" sync of part of the folder's UID space.

- **Foreground Worker**: Idles on the primary folder and wakes to syncs
changes. Also wakes to perform other tasks, like fetching message bodies the
user clicks.

### Sync Design Considerations

- **Table Design**: Mailspring's approach to SQLite is similar to CoreData's: A
`data` column stores a full JSON representation of the model, and additional
columns contain copies of the individual fields so the table can be queried.
This "fat" approach means fields can be added easily (to the JSON) and
migrations are only necessary when adding new queryable columns. The only known
downside is that the increased row size caused by the duplication of data into
columns and the non-fixed row size makes SCAN queries slower. In retrospect we
may have chosen a different approach.

- **Message Contents**: Mailspring only fetches the contents of the last three
months worth of email (configurable in the SyncWorker). For older emails, it
fetches only the headers necessary to display subject lines and build stable
IDs.

- **Stable IDs**: Mailspring hashes message headers to create a stable ID for
each message. In rare cases, two messages in an account can have the same ID
and Mailspring will only show one.

- **Metadata**: Mailspring allows you to attach arbitrary metadata to threads
and messages, and syncs this data to id.getmailspring.com so it can be shared
between computers and modified server-side (for read receipts, etc). Metadata
objects are stored directly on the corresponding models and modifications to
metadata are broadcast to the Mailspring application as modifications to their
parent objects. A separate thread in mailsync uses `libcurl` to listen to a
streaming endpoint for metadata events.

- **Gmail**: Gmail's IMAP interface makes extensive use of "Virtual Folders"
for labels. Mailspring ignores all of these, and only syncs Spam, All Mail and
Trash. It then uses the X-GM-LABELS extension to add labels.

- **Error Handling**: Errors happen and sync is designed to be as stateless as
possible so that unexpected interruptions of the sync process are fine. When
you quit Mailspring, it just force-kills all the mailsync processes. Since all
modifications are done within sqlite transactions, this works just fine.

### Tips

- **SQLite**: SQLite is a database but many common optimization strategies
(doing bulk inserts, etc.) don't apply because access latency is zero and
parsing query strings is expensive. When you create a statement, think of it as
building a dynamic function. Build them infrequently and re-use the functions.

## Known Issues / TODOs

- If you add a file to the project, you need to manually add it to the build
configurations for Mac, Windows and Linux. I typically do this by grepping for
an existing file and adding the new one beside it. Word on the street is that
CMake can auto-generate the Xcode and Visual Studio project files, which would
be a way to solve this, but I can only imagine it'd be difficult to get all the
various config flags set correctly for libMailCore.

- This project builds `libetpan` from source in the `Vendor/libetpan`. This is
because I couldn't find a binary of libetpan that included iconv and properly
handled email subject lines with foreign characters. The copy in the repo
contains local modifications. - This project builds `libcurl`:

  - On Win32, we use a libcurl binary at `/Windows/Externals/lib/libcurl_a.lib`.
  - On Linux, we build libcurl from scratch in `./build.sh` and use it,
    because the installed version is ancient.
  - On Mac, we use the libcurl present on the system.

- On Linux, we build OpenSSL from scratch in build.sh and statically link
against it because the installed version varies on different Linux distros, and
very old distros (looking at you Ubuntu 14) have such out-of-date OpenSSL
libraries they do not support SSLv3 and TLS1.2.

  - This is horribly annoying and means Mailspring needs to search all over the
  place for the user's certificate chain. Turns out that OpenSSL is re-compiled
  for each Linux distro with different compile-time constants, and nobody
  stores the certificate chain in the same place.

## Debugging on Win32

Select the project in the sidebar and view `Properties => Debugging`.

Put this into the command arguments field:

```
--identity "{\"id\":\"2137538a-6cb1-4c1f-b593-deb6103cf88b\",\"firstName\":\"Test\",\"lastName\":\"User\",\"emailAddress\":\"testuser@getmailspring.com\",\"object\":\"identity\",\"createdAt\":\"2017-09-27T19:41:39.000Z\",\"stripePlan\":\"Basic\",\"stripePlanEffective\":\"Basic\",\"stripeCustomerId\":\"XXXXX\",\"stripePeriodEnd\":\"2017-10-27T19:41:39.000Z\",\"featureUsage\":{\"snooze\":{\"quota\":15,\"period\":\"weekly\",\"usedInPeriod\":0,\"featureLimitName\":\"basic-limit\"},\"send-later\":{\"quota\":10,\"period\":\"weekly\",\"usedInPeriod\":2,\"featureLimitName\":\"basic-limit\"},\"send-reminders\":{\"quota\":10,\"period\":\"weekly\",\"usedInPeriod\":2,\"featureLimitName\":\"basic-limit\"}},\"token\":\"b7670d34-d4d4-4391-9757-a8d37f31f839\"}" --account "<your account object with .settings populated>" --mode sync
```

Put this into the environment field on TWO LINES and WITHOUT QUOTES:

```
CONFIG_DIR_PATH=C:\Users\IEUser\AppData\Roaming\Mailspring
IDENTITY_SERVER=https://id.getmailspring.com
```

The application does not seem to like being debugged in `Debug` mode. You need
to use the `Release` configuration. This seems to be because there's a "Debug
Heap" that is enabled in Debug configurations, and memory allocated by libetpan
and de-allocated in mailcore2 isn't reference tracked properly.
