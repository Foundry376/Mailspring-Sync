"""
A scriptable, stateful IMAP server for driving mailsync in integration tests.

Realism rule: the default behaviour of this server is meant to match Dovecot 2.3, and
test/conformance/ checks that it does for the commands mailsync issues. Any deviation
from that baseline is an explicit, named quirk on a Personality, with a citation to the
real server, bug report or RFC that justifies it. A behaviour that exists only in this
fake is a bug in the fake, not something the engine must support.
"""
from .store import Store, GmailStore, LabelView, Mailbox, Message, StoreEvent
from .personalities import Personality, PERSONALITIES, personality
from .server import FakeImapServer
