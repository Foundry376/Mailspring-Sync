"""
Server personalities: the capability set, greeting, folder layout and named quirks of a
real IMAP server family.

Every personality carries a `source` and every quirk a citation. The baseline is
`dovecot`, whose behaviour test/conformance/ checks against a real Dovecot container; the
others differ from it only in the listed, sourced ways. Fields marked NEEDS-RECORDING are
best-effort reconstructions from bug reports rather than captured transcripts and should
be replaced with output from test/tools/record_personality.py before a scenario relies on
their exact text.
"""
from dataclasses import dataclass, field

# Dovecot 2.3 advertises a short list before login and the full list after (LOGIN's
# tagged OK carries the post-login set). mailcore reads both.
DOVECOT_PREAUTH = "IMAP4rev1 SASL-IR LOGIN-REFERRALS ID ENABLE IDLE LITERAL+ AUTH=PLAIN AUTH=LOGIN"
DOVECOT_POSTAUTH = (
    "IMAP4rev1 SASL-IR LOGIN-REFERRALS ID ENABLE IDLE SORT SORT=DISPLAY THREAD=REFERENCES "
    "THREAD=REFS THREAD=ORDEREDSUBJECT MULTIAPPEND URL-PARTIAL CATENATE UNSELECT CHILDREN "
    "NAMESPACE UIDPLUS LIST-EXTENDED I18NLEVEL=1 CONDSTORE QRESYNC ESEARCH ESORT SEARCHRES "
    "WITHIN CONTEXT=SEARCH LIST-STATUS BINARY MOVE SNIPPET=FUZZY PREVIEW=FUZZY PREVIEW "
    "STATUS=SIZE SAVEDATE LITERAL+ NOTIFY SPECIAL-USE"
)

STANDARD_MAILBOXES = [
    ("INBOX", []),
    ("Drafts", ["\\Drafts"]),
    ("Sent", ["\\Sent"]),
    ("Junk", ["\\Junk"]),
    ("Trash", ["\\Trash"]),
    ("Archive", ["\\Archive"]),
]


@dataclass
class Personality:
    name: str
    description: str
    source: str
    preauth_capabilities: str
    postauth_capabilities: str
    greeting: str = "Dovecot ready."
    login_ok_text: str = "Logged in"
    delimiter: str = "/"
    namespace: str = '(("" "/")) NIL NIL'
    id_response: str = '("name" "Dovecot")'
    mailboxes: list = field(default_factory=lambda: list(STANDARD_MAILBOXES))
    # LIST includes SPECIAL-USE attributes without RETURN (SPECIAL-USE); Dovecot does.
    list_special_use: bool = True
    quirks: dict = field(default_factory=dict)   # quirk name -> citation
    gmail: bool = False

    def has(self, quirk: str) -> bool:
        return quirk in self.quirks

    def capability_set(self, authenticated: bool) -> set:
        caps = self.postauth_capabilities if authenticated else self.preauth_capabilities
        return set(caps.split())

    def without(self, *caps: str) -> "Personality":
        """A copy with capabilities removed, e.g. without("CONDSTORE", "QRESYNC") for the
        control run that forces the deep-scan branch."""
        drop = set(caps)
        p = Personality(**self.__dict__)
        p.preauth_capabilities = " ".join(c for c in self.preauth_capabilities.split() if c not in drop)
        p.postauth_capabilities = " ".join(c for c in self.postauth_capabilities.split() if c not in drop)
        p.name = f"{self.name}-without-{'-'.join(sorted(drop)).lower()}"
        return p


PERSONALITIES = {}


def _register(p: Personality) -> Personality:
    PERSONALITIES[p.name] = p
    return p


def personality(name: str) -> Personality:
    if name in PERSONALITIES:
        return PERSONALITIES[name]
    # "dovecot-without-condstore-qresync" style names
    base, _, rest = name.partition("-without-")
    if rest and base in PERSONALITIES:
        return PERSONALITIES[base].without(*[c.upper() for c in rest.split("-")])
    raise KeyError(f"unknown personality {name!r}; known: {sorted(PERSONALITIES)}")


dovecot = _register(Personality(
    name="dovecot",
    description="Dovecot 2.3 with CONDSTORE+QRESYNC, SPECIAL-USE folders. The baseline; "
                "also representative of Fastmail-style hosts built on Dovecot (mailcow, Migadu, Dreamhost).",
    source="Verified against dovecot/dovecot:2.3.21 by test/conformance/",
    preauth_capabilities=DOVECOT_PREAUTH,
    postauth_capabilities=DOVECOT_POSTAUTH,
))

plain = _register(Personality(
    name="plain",
    description="IMAP4rev1 + IDLE + UIDPLUS + MOVE + SPECIAL-USE, no CONDSTORE/QRESYNC. Forces "
                "SyncWorker's deep/shallow-scan branch. Equivalent to Dovecot with imap_capability "
                "overridden, which is how the #140 control run was made.",
    source="Mailspring-Sync PR #140 (control run)",
    preauth_capabilities=DOVECOT_PREAUTH,
    postauth_capabilities=dovecot.without("CONDSTORE", "QRESYNC").postauth_capabilities,
))

proton_bridge = _register(Personality(
    name="proton-bridge",
    description="ProtonMail Bridge: no CONDSTORE/QRESYNC, an \\All 'All Mail' mailbox holding a "
                "second copy of every message, Folders/ and Labels/ containers. NEEDS-RECORDING for "
                "the exact capability string; the folder layout is from user reports.",
    source="Mailspring-Sync PR #137 (Dovecot configured like Bridge); Mailspring-Sync ab5db28; "
           "community.getmailspring.com ProtonMail threads",
    preauth_capabilities="IMAP4rev1 LITERAL+ ID IDLE UNSELECT AUTH=PLAIN",
    postauth_capabilities="IMAP4rev1 LITERAL+ ID IDLE UNSELECT UIDPLUS MOVE CHILDREN NAMESPACE",
    greeting="Proton Mail Bridge ready.",
    id_response='("name" "Proton Mail Bridge")',
    mailboxes=[
        ("INBOX", []),
        ("All Mail", ["\\All"]),
        ("Archive", ["\\Archive"]),
        ("Sent", ["\\Sent"]),
        ("Drafts", ["\\Drafts"]),
        ("Spam", ["\\Junk"]),
        ("Trash", ["\\Trash"]),
        ("Folders", ["\\Noselect"]),
        ("Folders/Receipts", []),
        ("Labels", ["\\Noselect"]),
        ("Labels/Work", []),
    ],
    quirks={
        "all-mail-duplicates": "Every message in a folder is also in All Mail (PR #137)",
        "labels-duplicate": "A labelled message is in its folder and under Labels/<name> (forum reports)",
    },
))

gateway_duplicate_list = _register(Personality(
    name="gateway-duplicate-list",
    description="An IMAP gateway (DavMail, Proton Bridge, Zoho) that lists the same mailbox twice "
                "and exposes both 'INBOX' and 'Inbox'.",
    source="Mailspring-Sync PR #139",
    preauth_capabilities=DOVECOT_PREAUTH,
    postauth_capabilities=DOVECOT_POSTAUTH,
    quirks={
        "duplicate-list-lines": "LIST repeats the Sent mailbox (PR #139: DavMail, Proton Bridge, Zoho)",
        "inbox-case-variant": "LIST returns both INBOX and Inbox; mailcore folds them to one path (PR #139)",
    },
))

netease = _register(Personality(
    name="netease",
    description="NetEase (163.com / 126.com / yeah.net): SELECT is refused with 'Unsafe Login' until "
                "the client sends ID; STATUS omits UIDNEXT. Engine gates on the hostname, so point "
                "imap_host at imap.163.com (see harness.servers.fake host aliasing).",
    source="Mailspring-Sync PR #121; community.getmailspring.com/t/562. Response text as widely "
           "reported by users: 'NO SELECT Unsafe Login. Please contact kefu@188.com for help'. "
           "NEEDS-RECORDING for the capability string.",
    preauth_capabilities="IMAP4rev1 XLIST SPECIAL-USE ID LITERAL+ STARTTLS XAPPLEPUSHSERVICE UIDPLUS X-CM-EXT-1",
    postauth_capabilities="IMAP4rev1 XLIST SPECIAL-USE ID LITERAL+ STARTTLS XAPPLEPUSHSERVICE UIDPLUS X-CM-EXT-1",
    greeting="Coremail System IMap Server Ready(163com[...])",
    id_response='("name" "Coremail")',
    quirks={
        "id-required-before-select": "SELECT before ID -> NO SELECT Unsafe Login (PR #121)",
        "status-omits-uidnext": "STATUS reply carries no UIDNEXT item (PR #121)",
    },
))

courier = _register(Personality(
    name="courier",
    description="Courier IMAP: a message APPENDed on one connection is not visible to a SELECT on "
                "another connection until that connection re-selects or NOOPs.",
    source="TaskProcessor.cpp: 'Courier (and maybe other IMAP servers) won't show us new messages "
           "we've created' (SendDraftTask sent-folder lookup). NEEDS-RECORDING for exact behaviour.",
    preauth_capabilities="IMAP4rev1 UIDPLUS CHILDREN NAMESPACE THREAD=ORDEREDSUBJECT THREAD=REFERENCES SORT QUOTA IDLE AUTH=PLAIN",
    postauth_capabilities="IMAP4rev1 UIDPLUS CHILDREN NAMESPACE THREAD=ORDEREDSUBJECT THREAD=REFERENCES SORT QUOTA IDLE",
    greeting="Courier-IMAP ready. Copyright 1998-2018 Double Precision, Inc.",
    delimiter=".",
    namespace='(("INBOX." ".")) NIL NIL',
    mailboxes=[("INBOX", []), ("INBOX.Drafts", ["\\Drafts"]), ("INBOX.Sent", ["\\Sent"]),
               ("INBOX.Trash", ["\\Trash"]), ("INBOX.Junk", ["\\Junk"])],
    quirks={"append-invisible-until-reselect": "TaskProcessor.cpp SendDraftTask comment"},
))

outlook = _register(Personality(
    name="outlook",
    description="Outlook.com / Office 365 IMAP: a BODY[n] fetch of a part can come back empty when "
                "the message has a text/calendar part. Engine gates the workaround on the hostname "
                "(mailcore MCIMAPSession mOutlookServer), so use imap_host outlook.office365.com.",
    source="MailCore/mailcore2#1621, cherry-picked in Mailspring-Sync 0dcc926 (PR #29). "
           "NEEDS-RECORDING for the capability string and the exact empty-part response.",
    preauth_capabilities="IMAP4rev1 AUTH=PLAIN AUTH=XOAUTH2 SASL-IR UIDPLUS ID UNSELECT CHILDREN IDLE NAMESPACE LITERAL+",
    postauth_capabilities="IMAP4rev1 AUTH=PLAIN AUTH=XOAUTH2 SASL-IR UIDPLUS MOVE ID UNSELECT CHILDREN IDLE NAMESPACE LITERAL+",
    greeting="The Microsoft Exchange IMAP4 service is ready.",
    id_response='("name" "Microsoft Exchange")',
    mailboxes=[("INBOX", []), ("Drafts", ["\\Drafts"]), ("Sent Items", ["\\Sent"]),
               ("Deleted Items", ["\\Trash"]), ("Junk Email", ["\\Junk"]), ("Archive", ["\\Archive"])],
    quirks={
        "empty-part-with-calendar": "BODY[n] returns NIL for a part of a message containing text/calendar (mailcore2#1621)",
        "sent-items-duplicate-copies": "Exchange auto-saves a sent message, racing the client's own APPEND, "
                                       "leaving 2-4 byte-identical copies at adjacent UIDs "
                                       "(observed 2026-09-19, docs/message-placements-plan.md)",
    },
))

icloud = _register(Personality(
    name="icloud",
    description="iCloud: advertises CONDSTORE+QRESYNC; the engine disables QRESYNC by hostname "
                "(imap.mail.me.com). The malformed-VANISHED behaviour itself is NOT modelled: no "
                "transcript exists. Use this personality to exercise the hostname gate only.",
    source="Mailspring-Sync PR #91 / 65f24b4; developer.apple.com/forums/thread/694251. NEEDS-RECORDING.",
    preauth_capabilities="IMAP4rev1 XAPPLEPUSHSERVICE SASL-IR ID ENABLE IDLE AUTH=PLAIN AUTH=ATOKEN AUTH=XOAUTH2",
    postauth_capabilities="IMAP4rev1 XAPPLEPUSHSERVICE SASL-IR ID ENABLE IDLE UIDPLUS NAMESPACE CHILDREN "
                          "CONDSTORE QRESYNC MOVE SPECIAL-USE",
    greeting="[ ... ] iCloud IMAP ready",
    mailboxes=[("INBOX", []), ("Drafts", ["\\Drafts"]), ("Sent Messages", ["\\Sent"]),
               ("Deleted Messages", ["\\Trash"]), ("Junk", ["\\Junk"]), ("Archive", ["\\Archive"])],
    quirks={"same-message-in-multiple-folders": "iCloud exposes the same message in several folders (MailProcessor.cpp folder priority)"},
))

gmail = _register(Personality(
    name="gmail",
    description="Gmail IMAP (X-GM-EXT-1): messages live in [Gmail]/All Mail with X-GM-LABELS; "
                "INBOX and other label folders are views onto it. CONDSTORE but no QRESYNC. "
                "Capability strings and LIST layout are from real Gmail sessions, but should be "
                "confirmed with test/tools/record_personality.py.",
    source="Real Gmail IMAP sessions; MailUtils.cpp roleForFolderViaPath [gmail]/ handling; "
           "SyncWorker.cpp X-GM-LABELS handling",
    preauth_capabilities="IMAP4rev1 UNSELECT IDLE NAMESPACE QUOTA ID XLIST CHILDREN X-GM-EXT-1 XYZZY "
                         "SASL-IR AUTH=XOAUTH2 AUTH=PLAIN AUTH=PLAIN-CLIENTTOKEN AUTH=OAUTHBEARER AUTH=XOAUTH",
    postauth_capabilities="IMAP4rev1 UNSELECT IDLE NAMESPACE QUOTA ID XLIST CHILDREN X-GM-EXT-1 UIDPLUS "
                          "COMPRESS=DEFLATE ENABLE MOVE CONDSTORE ESEARCH UTF8=ACCEPT LIST-EXTENDED "
                          "LIST-STATUS LITERAL- SPECIAL-USE APPENDLIMIT=35651584",
    greeting="Gimap ready for requests from 127.0.0.1 h1mb2",
    login_ok_text="test@gmail.com authenticated (Success)",
    id_response='("name" "GImap" "vendor" "Google, Inc." "support-url" "http://support.google.com/mail" "remote-host" "127.0.0.1")',
    mailboxes=[
        ("INBOX", []),
        ("[Gmail]", ["\\Noselect"]),
        ("[Gmail]/All Mail", ["\\All"]),
        ("[Gmail]/Drafts", ["\\Drafts"]),
        ("[Gmail]/Important", ["\\Important"]),
        ("[Gmail]/Sent Mail", ["\\Sent"]),
        ("[Gmail]/Spam", ["\\Junk"]),
        ("[Gmail]/Starred", ["\\Flagged"]),
        ("[Gmail]/Trash", ["\\Trash"]),
    ],
    gmail=True,
    quirks={"label-views": "INBOX, Sent Mail, Starred, Important and user labels are views of All Mail selected by X-GM-LABELS"},
))

yahoo = _register(Personality(
    name="yahoo",
    description="Yahoo Mail (imap.mail.yahoo.com): UIDPLUS and MOVE but no CONDSTORE, so the "
                "deep-scan branch; a multi-message UID MOVE answers with a COPYUID that does not "
                "pair the UIDs it assigned. COMPRESS=DEFLATE is left out because the fake does "
                "not implement it; the folder layout keeps the harness's standard names.",
    source="Capabilities and greeting from a verbose mailsync log of a Yahoo account, 2026-09-24",
    preauth_capabilities="IMAP4rev1 SASL-IR AUTH=PLAIN AUTH=XOAUTH2 AUTH=OAUTHBEARER ID MOVE NAMESPACE "
                         "XYMHIGHESTMODSEQ UIDPLUS LITERAL+ CHILDREN UNSELECT X-MSG-EXT OBJECTID IDLE ENABLE "
                         "UIDONLY X-UIDONLY LIST-EXTENDED LIST-STATUS SPECIAL-USE PARTIAL APPENDLIMIT=41697280",
    postauth_capabilities="IMAP4rev1 ID MOVE NAMESPACE XYMHIGHESTMODSEQ UIDPLUS LITERAL+ CHILDREN UNSELECT "
                          "X-MSG-EXT OBJECTID IDLE ENABLE UIDONLY X-UIDONLY LIST-EXTENDED LIST-STATUS "
                          "SPECIAL-USE MESSAGELIMIT=1000 PARTIAL APPENDLIMIT=41697280",
    greeting="Welcome! IMAP Server up and ready to accept your request",
    quirks={
        "copyuid-permuted": "COPYUID lists ascending source and destination ranges while the UIDs "
                            "assigned are a permutation of the destination range; 2 of 18 pairs "
                            "correct across 4 multi-message moves, single-message moves correct "
                            "(live Yahoo account, observed 2026-09-24; RFC 4315 §3). "
                            "Modelled as a rotation, which also applies to UID COPY (unverified on Yahoo).",
    },
))

cyrus_vanished = _register(Personality(
    name="cyrus-vanished",
    description="The Dovecot baseline with one Cyrus behaviour: VANISHED (EARLIER) in reply to "
                "UID FETCH ... (CHANGEDSINCE n VANISHED) only covers the UID set, with `*` resolved "
                "to the highest UID still in the mailbox, so an expunge above it is never reported. "
                "Every other Cyrus difference is left to the real cyrus: server kind.",
    source="cyrus-imapd #6071 (fixed on master in 2957d50, in no release as of 2026-09); "
           "reproduced against a local Cyrus 3.6 on 2026-09-24 (UID FETCH 1:* (CHANGEDSINCE n VANISHED) "
           "reported nothing; 1:4294967295 reported VANISHED (EARLIER) for the removed top UIDs)",
    preauth_capabilities=DOVECOT_PREAUTH,
    postauth_capabilities=DOVECOT_POSTAUTH,
    quirks={
        "vanished-clipped-to-star": "VANISHED (EARLIER) is limited to the UID set with `*` = highest "
                                    "UID present (cyrus-imapd #6071; RFC 7162 §3.2.6)",
    },
))
