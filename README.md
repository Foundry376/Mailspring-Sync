## Known Issues / TODOs

- On Win32, build.cmd and the VS2015 project build libetpan from source. This is because I couldn't find a binary of libetpan that included iconv and properly handled email subject lines with foreign characters. Really, we should build it on all platforms for consistency? Definitely going the monorepo route and just committing my entire working copy.

- On Win32, only mailsync.exe is uploaded as an artifact, not the .pdb files or the .dlls that are needed to make it work. This should probably be fixed.

## Debugging on Win32

Select the project in the sidebar and view Properties => Debugging.

Put this into the command arguments field:


--identity "{\"id\":\"2137538a-6cb1-4c1f-b593-deb6103cf88b\",\"firstName\":\"Ben\",\"lastName\":\"Gotow\",\"emailAddress\":\"bengotow@gmail.com\",\"object\":\"identity\",\"createdAt\":\"2017-09-27T19:41:39.000Z\",\"stripePlan\":\"Basic\",\"stripePlanEffective\":\"Basic\",\"stripeCustomerId\":\"cus_BTirmAoLQVSnSO\",\"stripePeriodEnd\":\"2017-10-27T19:41:39.000Z\",\"featureUsage\":{\"snooze\":{\"quota\":15,\"period\":\"weekly\",\"usedInPeriod\":0,\"featureLimitName\":\"basic-limit\"},\"send-later\":{\"quota\":10,\"period\":\"weekly\",\"usedInPeriod\":2,\"featureLimitName\":\"basic-limit\"},\"send-reminders\":{\"quota\":10,\"period\":\"weekly\",\"usedInPeriod\":2,\"featureLimitName\":\"basic-limit\"}},\"token\":\"b7670d34-d4d4-4391-9757-a8d37f31f839\"}" --account "{\"id\":\"6bde55d600e226ae009c0886dbaae918586f9aeffacad25c9245b22e9858a460\",\"metadata\":[],\"name\":\"Ben Gotow\",\"provider\":\"imap\",\"emailAddress\":\"bengotow@gmail.com\",\"settings\":{\"imap_allow_insecure_ssl\":false,\"imap_host\":\"imap.gmail.com\",\"imap_port\":993,\"imap_security\":\"SSL / TLS\",\"imap_username\":\"bengotow@gmail.com\",\"smtp_allow_insecure_ssl\":false,\"smtp_host\":\"smtp.gmail.com\",\"smtp_port\":465,\"smtp_security\":\"SSL / TLS\",\"smtp_username\":\"bengotow@gmail.com\",\"imap_password\":\"kvaiewqwhdzcgmbh\",\"smtp_password\":\"kvaiewqwhdzcgmbh\"},\"label\":\"bengotow@gmail.com\",\"aliases\":[],\"syncState\":\"ok\",\"syncError\":null,\"__cls\":\"Account\"}" --mode sync

Put this into the environment field:

CONFIG_DIR_PATH=C:\Users\IEUser\AppData\Roaming\Mailspring
IDENTITY_SERVER=https://id.getmailspring.com

The application does not seem to like being debugged in `Debug` mode. You need to use the `Release` configuration. This seems to be because there's a "Debug Heap" that is enabled in Debug configurations, and memory allocated by libetpan and de-allocated in mailcore2 isn't reference tracked properly.
