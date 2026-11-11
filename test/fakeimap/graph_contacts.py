"""
The contacts half of the fake Graph server: contact folders, contacts, and per-folder delta
queries (https://learn.microsoft.com/en-us/graph/api/contact-delta).

Behaviour follows what an Office 365 tenant and an Outlook.com account did on 2026-11-11:

- The default folder is `/me/contactFolders/contacts` (its well-known name); `/me/contactFolders`
  lists only the folders inside it. Contacts are POSTed to `/me/contacts`, which files them in the
  default folder. There is no move action for contacts; only other clients move them.
- A delta round lists every contact in the folder, or with a delta token the contacts changed in or
  removed from the folder since, each once, as it is now. A removed one is
  `{"id", "@removed": {"reason": "deleted"}}`. Pages hold `Prefer: odata.maxpagesize` contacts
  (10 without it), and a delta link the scenario expired answers 410 SyncStateNotFound
  (https://learn.microsoft.com/en-us/graph/delta-query-overview#synchronization-reset).
- Read-only fields in a write are ignored. More than three emailAddresses, or two businessPhones
  or homePhones, is refused with 400 ErrorInvalidProperty, and an address set to null with a 500 (ArgumentNullException); {} clears one.
- A create without displayName gets one composed from title, given, middle, surname and
  generation ("Dr. Ada K Lovelace Jr."). A PATCH that changes a name field without displayName
  composes it again, as the contact reference warns.
- A birthday is stored at 11:59 UTC of its date.
"""
import threading
from typing import Optional

DEFAULT_FOLDER_ID = "AAMkAG/Contacts="
ROOT_FOLDER_ID = "AAMkAG/Root="
READ_ONLY = {"id", "changeKey", "parentFolderId", "createdDateTime", "lastModifiedDateTime"}
NAME_FIELDS = ("title", "givenName", "middleName", "surname", "generation")
ADDRESS_FIELDS = ("homeAddress", "businessAddress", "otherAddress")
MAX_EMAILS = 3
MAX_PHONES = 2   # each of businessPhones and homePhones


def composed_name(contact: dict) -> str:
    return " ".join(contact.get(f) for f in NAME_FIELDS if contact.get(f))


class GraphContacts:
    def __init__(self, folders: Optional[list] = None, contacts: Optional[list] = None, page_size: int = 0):
        """folders: [{id, displayName}] inside the default folder. contacts: Graph contact
        resources, each in `parentFolderId` (default: the default folder). page_size: the most a
        delta page holds, whatever the engine prefers (0: no cap)."""
        self.lock = threading.RLock()
        self.folders = {DEFAULT_FOLDER_ID: {"id": DEFAULT_FOLDER_ID, "displayName": "Contacts", "parentFolderId": ROOT_FOLDER_ID}}
        self.contacts: dict = {}
        self.seq = 0
        self.log: list = []          # (seq, folder id, contact id): a change the folder's delta reports
        self.tokens: dict = {}       # delta token -> (folder id, seq)
        self.rounds: dict = {}       # skip token -> (folder id, remaining items, seq at the round's start)
        self.expired_before = 0      # delta tokens at or before this seq answer 410
        self.writes: list = []       # {method, id, fields}: each POST, PATCH and DELETE the engine sent
        self.created = 0
        self.page_size = page_size
        for folder in folders or []:
            self.folders[folder["id"]] = {"parentFolderId": DEFAULT_FOLDER_ID, **folder}
        for contact in contacts or []:
            self.put(contact)

    def _touch(self, folder_id: str, contact_id: str):
        self.seq += 1
        self.log.append((self.seq, folder_id, contact_id))

    def put(self, fields: dict, compose_name: bool = True) -> dict:
        """Creates or changes the contact `fields["id"]` names, in `fields["parentFolderId"]` when given."""
        with self.lock:
            contact_id = fields.get("id")
            if not contact_id:
                self.created += 1
                contact_id = f"AAkALg/{self.created}+Contact="
            old = self.contacts.get(contact_id)
            folder = fields.get("parentFolderId") or (old["parentFolderId"] if old else DEFAULT_FOLDER_ID)
            contact = dict(old) if old else {"emailAddresses": [], "businessPhones": [], "homePhones": [],
                                             "categories": [], "children": [], "imAddresses": []}
            for name, value in fields.items():
                if name not in READ_ONLY and not name.startswith("@"):
                    contact[name] = value
            if "birthday" in fields and fields["birthday"]:
                contact["birthday"] = fields["birthday"][:10] + "T11:59:00Z"
            names_changed = any(f in fields for f in NAME_FIELDS)
            if compose_name and "displayName" not in fields and (old is None or names_changed):
                contact["displayName"] = composed_name(contact)
            contact["id"] = contact_id
            contact["parentFolderId"] = folder
            if old and old["parentFolderId"] != folder:
                self._touch(old["parentFolderId"], contact_id)
            self._touch(folder, contact_id)
            contact["changeKey"] = f"ck-contact-{self.seq}"
            self.contacts[contact_id] = contact
            return dict(contact)

    def delete(self, contact_id: str) -> bool:
        with self.lock:
            contact = self.contacts.pop(contact_id, None)
            if contact is None:
                return False
            self._touch(contact["parentFolderId"], contact_id)
            return True

    def set_folders(self, folders: list):
        """The folders inside the default one; a folder dropped takes its contacts with it."""
        with self.lock:
            wanted = {f["id"]: {"parentFolderId": DEFAULT_FOLDER_ID, **f} for f in folders}
            for folder_id in [f for f in self.folders if f != DEFAULT_FOLDER_ID and f not in wanted]:
                for contact_id in [c for c, v in self.contacts.items() if v["parentFolderId"] == folder_id]:
                    self.delete(contact_id)
                del self.folders[folder_id]
            self.folders.update(wanted)

    def expire_delta_links(self):
        with self.lock:
            self.expired_before = self.seq

    def validate(self, fields: dict) -> Optional[tuple]:
        """(status, code, message) for a write Graph refuses, else None."""
        for name in ("businessPhones", "homePhones"):
            if len(fields.get(name) or []) > MAX_PHONES:
                n = len(fields[name])
                return 400, "ErrorInvalidProperty", f"The multi value property {name} has {n} entries, that exceeds the max allowed value of {MAX_PHONES}."
        if len(fields.get("emailAddresses") or []) > MAX_EMAILS:
            n = len(fields["emailAddresses"])
            return 400, "ErrorInvalidProperty", f"The multi value property emailAddresses has {n} entries, that exceeds the max allowed value of {MAX_EMAILS}."
        for name in ADDRESS_FIELDS:
            if name in fields and fields[name] is None:
                return 500, "ErrorInternalServerError", "An internal server error occurred. The operation failed."
        return None

    def begin_round(self, folder_id: str, token: Optional[str]):
        """The items of a delta round from `token` (None: the whole folder), and the seq it reaches;
        None when the token has expired."""
        with self.lock:
            since = 0
            if token is not None:
                if token not in self.tokens or self.tokens[token][1] <= self.expired_before:
                    return None
                since = self.tokens[token][1]
            if token is None:
                ids = sorted(c for c, v in self.contacts.items() if v["parentFolderId"] == folder_id)
            else:
                ids = []
                for seq, folder, contact_id in self.log:
                    if seq > since and folder == folder_id and contact_id not in ids:
                        ids.append(contact_id)
            items = []
            for contact_id in ids:
                contact = self.contacts.get(contact_id)
                if contact is not None and contact["parentFolderId"] == folder_id:
                    items.append(dict(contact))
                else:
                    items.append({"@odata.type": "#microsoft.graph.contact", "id": contact_id, "@removed": {"reason": "deleted"}})
            return items, self.seq

    def end_round(self, folder_id: str, seq: int) -> str:
        with self.lock:
            token = f"dt{len(self.tokens) + 1}"
            self.tokens[token] = (folder_id, seq)
            return token

    def save_round(self, folder_id: str, items: list, seq: int) -> str:
        with self.lock:
            skip = f"st{len(self.rounds) + 1}"
            self.rounds[skip] = (folder_id, items, seq)
            return skip
