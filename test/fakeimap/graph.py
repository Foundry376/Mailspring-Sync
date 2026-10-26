"""
A small Microsoft identity platform + Microsoft Graph server for exercising the engine's
Graph send path: the v2.0 token endpoint (refresh_token grant), creating a draft from MIME,
attachments (simple and upload sessions), send, reading a message and Sent Items, and
deleting a draft. Sent messages are kept in memory for assertions and their Sent Items copy
is appended to the IMAP store, as Exchange files it.

Behaviour follows the Graph v1.0 reference: requests over 4 MB are refused with 413
(https://learn.microsoft.com/en-us/graph/api/user-sendmail - "Request size limit"), upload
session chunks are multiples of 320 KiB and refuse an Authorization header
(https://learn.microsoft.com/en-us/graph/api/attachment-createuploadsession), and an
immutable id is kept when the draft moves to Sent Items
(https://learn.microsoft.com/en-us/graph/outlook-immutable-id). A message is not readable
between /send and the moment its Sent Items copy exists (the real service shows it in
Outbox; the engine only cares whether it has reached Sent Items).

Calendars are listed from /me/calendars, paged by `@odata.nextLink`
(https://learn.microsoft.com/en-us/graph/api/user-list-calendars). A calendar's events are listed
in UTC, single instances and series masters only (calendar-list-events); one event is read in the
zone `Prefer: outlook.timezone` names, with a text body only when asked for one, and a series'
cancelledOccurrences / exceptionOccurrences only when selected and expanded (event-get). $batch
runs up to 20 requests and answers them out of order (json-batching). Calendar and mail scopes
are consented and tokened separately: a token for one is refused by the other's routes with
403, as Graph answers a token missing the permission a request needs.

The server also stands in for the Mailspring identity server's DAV host lookup, which sends a
Microsoft account's CardDAV discovery to Outlook on the web: its /owa/ answers 401, as observed
on an Outlook.com account.
"""
import base64
import email
import email.policy
import json
from email.message import Message
import re
import threading
import time
from email.message import EmailMessage
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from datetime import datetime
from typing import Optional
from urllib.parse import parse_qs, unquote, urlparse
from zoneinfo import ZoneInfo

MAX_REQUEST_BYTES = 4 * 1024 * 1024
UPLOAD_CHUNK_MULTIPLE = 320 * 1024
GRAPH_SCOPE = "https://graph.microsoft.com/"
CALENDARS_SCOPE = GRAPH_SCOPE + "Calendars."
MAIL_SCOPE = "https://outlook.office.com/"
SENT_ITEMS_ID = "AQMkAGSentItems="
# The Windows zone names Graph writes, for the zones the scenarios use.
WINDOWS_ZONES = {"UTC": "UTC", "Pacific Standard Time": "America/Los_Angeles", "Tokyo Standard Time": "Asia/Tokyo",
                 "New Zealand Standard Time": "Pacific/Auckland", "W. Europe Standard Time": "Europe/Berlin"}


def _in_zone(value: dict, zone: str) -> dict:
    """A Graph dateTimeTimeZone moved into `zone`."""
    if value.get("timeZone") == zone:
        return dict(value)
    local = datetime.fromisoformat(value["dateTime"][:19]).replace(tzinfo=ZoneInfo(WINDOWS_ZONES[value["timeZone"]]))
    moved = local.astimezone(ZoneInfo(WINDOWS_ZONES[zone]))
    return {"dateTime": moved.strftime("%Y-%m-%dT%H:%M:%S.0000000"), "timeZone": zone}


def _selected(resource: dict, select: str) -> dict:
    fields = set(select.split(",")) if select else None
    return {k: v for k, v in resource.items() if fields is None or k in fields or k == "id"}
DRAFTS_ID = "AQMkAGDrafts="


def _with_from(mime: bytes, name: str, address: str) -> bytes:
    msg = email.message_from_bytes(mime, policy=email.policy.default)
    del msg["From"]
    msg["From"] = f"{name} <{address}>" if name else address
    return msg.as_bytes(policy=email.policy.SMTP)


class GraphMessage:
    def __init__(self, graph_id: str, mime: bytes):
        self.id = graph_id
        self.mime = mime
        self.attachments: list = []   # dicts: name, contentType, isInline, contentId, data
        self.folder = DRAFTS_ID
        self.sent_at: Optional[float] = None
        self.filed = False

    @property
    def parsed(self):
        return email.message_from_bytes(self.mime, policy=email.policy.default)

    def header(self, name: str) -> Optional[str]:
        value = self.parsed[name]
        return str(value) if value is not None else None

    def from_address(self) -> dict:
        addr = self.parsed["From"].addresses[0]
        return {"name": addr.display_name, "address": addr.addr_spec}

    def recipients(self) -> list:
        msg = self.parsed
        out = []
        for field in ("To", "Cc", "Bcc"):
            for addr in msg.get_all(field, []) or []:
                out += [a.addr_spec for a in addr.addresses]
        return out

    def attachment_names(self) -> list:
        names = [part.get_filename() for part in self.parsed.walk() if part.get_filename()]
        return sorted(names + [a["name"] for a in self.attachments])

    def sent_copy(self) -> bytes:
        """The MIME Exchange files in Sent Items: the draft's MIME plus the attachments that
        were added to the draft separately."""
        if not self.attachments:
            return self.mime
        msg = email.message_from_bytes(self.mime, policy=email.policy.default)
        if not isinstance(msg, EmailMessage):
            msg = EmailMessage(policy=email.policy.default)
        if not msg.is_multipart():
            msg.make_mixed()
        for a in self.attachments:
            maintype, _, subtype = (a["contentType"] or "application/octet-stream").partition("/")
            msg.add_attachment(a["data"], maintype=maintype, subtype=subtype or "octet-stream",
                               filename=a["name"], disposition="inline" if a["isInline"] else "attachment",
                               cid=f"<{a['contentId']}>" if a.get("contentId") else None)
        return msg.as_bytes(policy=email.policy.SMTP)


class _Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    _captured: Optional[list] = None   # set while a $batch runs one of its requests

    def log_message(self, fmt, *args):
        pass

    def _reply(self, status: int, body=None, headers: Optional[dict] = None):
        if self._captured is not None:
            self._captured.append((status, body, headers))
            self.server.owner.log(self.command, self.path, status)
            return
        payload = b"" if body is None else json.dumps(body).encode()
        self.send_response(status)
        if body is not None:
            self.send_header("Content-Type", "application/json")
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)
        self.server.owner.log(self.command, self.path, status)

    def _error(self, status: int, code: str, message: str):
        self._reply(status, {"error": {"code": code, "message": message}})

    def _body(self) -> bytes:
        length = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(length) if length else b""

    def do_POST(self):
        self._dispatch()

    def do_GET(self):
        self._dispatch()

    def do_PUT(self):
        self._dispatch()

    def do_DELETE(self):
        self._dispatch()

    def do_PATCH(self):
        self._dispatch()

    def do_PROPFIND(self):
        self._dispatch()

    def _preferences(self) -> dict:
        """Every Prefer header's preferences, whether sent one per header or comma-separated."""
        out = {}
        for header in self.headers.get_all("Prefer") or []:
            for m in re.finditer(r'([\w.-]+)\s*=\s*(?:"([^"]*)"|([^,\s]*))', header):
                out[m.group(1)] = m.group(2) if m.group(2) is not None else m.group(3)
        return out

    def _dispatch(self):
        self._route(self._body())

    def _route(self, body: bytes):
        srv: "FakeGraphServer" = self.server.owner
        path = urlparse(self.path).path
        with srv.lock:
            srv.requests.append({"method": self.command, "path": path, "bytes": len(body),
                                 "headers": {k.lower(): v for k, v in self.headers.items()}})
        if path == "/token":
            return self._token(body)
        if path.startswith("/upload/"):
            return self._upload(path[len("/upload/"):], body)
        if path == "/v1.0/$batch" and self.command == "POST":
            return self._batch(body)
        if path == "/api/resolve-dav-hosts" and self.command == "POST":
            return self._reply(200, {"carddavHost": f"127.0.0.1:{srv.port}"})
        if path == "/.well-known/carddav":
            return self._reply(301, headers={"Location": f"http://127.0.0.1:{srv.port}/owa/"})
        if path == "/owa/":
            return self._reply(401)
        if not path.startswith("/v1.0/me/"):
            return self._error(404, "ResourceNotFound", f"no route for {path}")
        if len(body) > MAX_REQUEST_BYTES:
            return self._error(413, "RequestEntityTooLarge", "The request is larger than 4 MB.")
        route = path[len("/v1.0/me/"):]
        token = (self.headers.get("Authorization") or "")[len("Bearer "):]
        calendar_route = route.split("/")[0] in ("calendars", "events")
        if token not in (srv.calendar_tokens if calendar_route else srv.graph_tokens):
            if token in srv.calendar_tokens or token in srv.graph_tokens:
                return self._error(403, "ErrorAccessDenied", "Access is denied. Check credentials and try again.")
            return self._error(401, "InvalidAuthenticationToken", "Access token is empty or invalid.")
        if self._preferences().get("IdType") != "ImmutableId":
            return self._error(400, "HarnessError", "every request must ask for immutable ids")
        failure = srv.failure_for(unquote(route), self.command)
        if failure:
            headers = {"Retry-After": str(failure["retry_after"])} if "retry_after" in failure else None
            return self._reply(failure.get("status", 500), {"error": {"code": failure.get("code", "ErrorInternalServerError"),
                               "message": failure.get("message", "Injected failure")}}, headers)

        if route == "calendars" and self.command == "GET":
            return self._calendars()
        m = re.match(r"^calendars/([^/]+)/events$", route)
        if m and self.command == "GET":
            return self._calendar_events(unquote(m.group(1)))
        m = re.match(r"^events/([^/]+)$", route)
        if m and self.command == "GET":
            return self._event(unquote(m.group(1)))
        if route == "mailFolders/sentitems" and self.command == "GET":
            return self._reply(200, {"id": SENT_ITEMS_ID})
        if route == "messages" and self.command == "POST":
            return self._create(body)
        m = re.match(r"^messages/([^/]+)(/.*)?$", route)
        if not m:
            return self._error(404, "ResourceNotFound", f"no route for {route}")
        graph_id, rest = unquote(m.group(1)), m.group(2) or ""
        msg = srv.messages.get(graph_id)
        if msg is None or (msg.sent_at is not None and not msg.filed):
            return self._error(404, "ErrorItemNotFound", "The specified object was not found in the store.")
        if rest == "" and self.command == "GET":
            return self._reply(200, {"id": msg.id, "parentFolderId": msg.folder,
                                     "internetMessageId": msg.header("Message-ID")})
        if rest == "" and self.command == "PATCH":
            sender = json.loads(body).get("from", {}).get("emailAddress", {})
            msg.mime = _with_from(msg.mime, sender.get("name", ""), sender["address"])
            return self._reply(200, {"id": msg.id, "from": {"emailAddress": msg.from_address()}})
        if rest == "" and self.command == "DELETE":
            with srv.lock:
                del srv.messages[graph_id]
                srv.deleted.append(msg)
            return self._reply(204)
        if msg.sent_at is not None:
            return self._error(400, "ErrorInvalidOperation", "The message has already been sent.")
        if rest == "/attachments" and self.command == "POST":
            a = json.loads(body)
            msg.attachments.append({"name": a["name"], "contentType": a.get("contentType"),
                                    "isInline": a.get("isInline", False), "contentId": a.get("contentId"),
                                    "data": base64.b64decode(a["contentBytes"])})
            return self._reply(201, {"id": f"att{len(msg.attachments)}"})
        if rest == "/attachments/createUploadSession" and self.command == "POST":
            item = json.loads(body)["AttachmentItem"]
            with srv.lock:
                sid = str(len(srv.uploads) + 1)
                srv.uploads[sid] = {"message": msg, "item": item, "data": b""}
            return self._reply(201, {"uploadUrl": f"http://127.0.0.1:{srv.port}/upload/{sid}",
                                     "expirationDateTime": "2099-01-01T00:00:00Z"})
        if rest == "/send" and self.command == "POST":
            return self._send(msg)
        return self._error(404, "ResourceNotFound", f"no route for {route}")

    def _token(self, body: bytes):
        srv: "FakeGraphServer" = self.server.owner
        form = {k: v[0] for k, v in parse_qs(body.decode()).items()}
        scope = form.get("scope", "")
        with srv.lock:
            srv.token_scopes.append(scope)
        if form.get("grant_type") != "refresh_token" or form.get("refresh_token") != srv.refresh_token:
            return self._reply(400, {"error": "invalid_grant", "error_description": "AADSTS70000: The refresh token is invalid."})
        if CALENDARS_SCOPE in scope:
            if not srv.calendars_consented:
                return self._reply(400, {"error": "invalid_grant", "error_description":
                                         "AADSTS65001: The user or administrator has not consented to use the application."})
            token = f"calendar-token-{len(srv.token_scopes)}"
            srv.calendar_tokens.add(token)
            return self._reply(200, {"token_type": "Bearer", "scope": scope, "expires_in": 3599,
                                     "access_token": token, "refresh_token": srv.refresh_token})
        if GRAPH_SCOPE in scope and srv.drop_graph_token:
            self.close_connection = True   # no response at all: curl reports CURLE_GOT_NOTHING
            self.connection.shutdown(2)
            return
        if GRAPH_SCOPE in scope:
            with srv.lock:
                lagging = srv.consent_refusals > 0
                srv.consent_refusals -= 1 if lagging else 0
            if not srv.graph_consented or lagging:
                return self._reply(400, {"error": "invalid_grant", "error_description":
                                         "AADSTS65001: The user or administrator has not consented to use the application."})
            token = f"graph-token-{len(srv.token_scopes)}"
            srv.graph_tokens.add(token)
        else:
            token = f"mail-token-{len(srv.token_scopes)}"
            srv.mail_tokens.add(token)
        return self._reply(200, {"token_type": "Bearer", "scope": scope, "expires_in": 3599,
                                 "access_token": token, "refresh_token": srv.refresh_token})

    def _calendars(self):
        srv: "FakeGraphServer" = self.server.owner
        skip = int(parse_qs(urlparse(self.path).query).get("$skip", ["0"])[0])
        with srv.lock:
            calendars = list(srv.calendars)
        size = srv.calendars_page_size or max(len(calendars), 1)
        body = {"value": calendars[skip:skip + size]}
        if skip + size < len(calendars):
            body["@odata.nextLink"] = f"{srv.url}/me/calendars?$skip={skip + size}"
        return self._reply(200, body)

    def _batch(self, body: bytes):
        """JSON batching: each request runs as if sent alone with the batch's Authorization, and
        the responses come back out of order (https://learn.microsoft.com/en-us/graph/json-batching)."""
        requests = json.loads(body or b"{}").get("requests", [])
        if not 1 <= len(requests) <= 20:
            return self._error(400, "BadRequest", "A batch holds 1 to 20 requests.")
        outer = (self.path, self.command, self.headers)
        responses = []
        try:
            for request in requests:
                headers = Message()
                headers["Authorization"] = outer[2].get("Authorization") or ""
                for name, value in (request.get("headers") or {}).items():
                    headers[name] = value
                self.path, self.command, self.headers = "/v1.0" + request["url"], request["method"], headers
                self._captured = []
                self._route(json.dumps(request["body"]).encode() if "body" in request else b"")
                status, payload, reply_headers = self._captured[0]
                responses.append({"id": request["id"], "status": status, "headers": reply_headers or {}, "body": payload})
        finally:
            self.path, self.command, self.headers = outer
            self._captured = None
        return self._reply(200, {"responses": list(reversed(responses))})

    def _calendar_events(self, calendar_id: str):
        srv: "FakeGraphServer" = self.server.owner
        query = parse_qs(urlparse(self.path).query)
        select = query.get("$select", [""])[0]
        skip = int(query.get("$skip", ["0"])[0])
        with srv.lock:
            events = [dict(e) for e in srv.events.get(calendar_id, [])]
        size = srv.events_page_size or max(len(events), 1)
        page = []
        for event in events[skip:skip + size]:
            event = {k: v for k, v in event.items() if k not in ("cancelledOccurrences", "exceptionOccurrences")}
            event["start"], event["end"] = _in_zone(event["start"], "UTC"), _in_zone(event["end"], "UTC")
            page.append(_selected(event, select))
        body = {"value": page}
        if skip + size < len(events):
            body["@odata.nextLink"] = f"{srv.url}/me/calendars/{calendar_id}/events?$select={select}&$skip={skip + size}"
        return self._reply(200, body)

    def _event(self, event_id: str):
        srv: "FakeGraphServer" = self.server.owner
        query = parse_qs(urlparse(self.path).query)
        select = query.get("$select", [""])[0]
        expand = query.get("$expand", [""])[0].split(",")
        prefer = self._preferences()
        if prefer.get("outlook.body-content-type") != "text":
            return self._error(400, "HarnessError", "event details must ask for a text body")
        zone = prefer.get("outlook.timezone", "UTC")
        if zone not in WINDOWS_ZONES:
            return self._error(400, "TimeZoneNotSupportedException", f"A valid TimeZone value must be specified: {zone}")
        with srv.lock:
            event = next((dict(e) for events in srv.events.values() for e in events if e["id"] == event_id), None)
        if event is None:
            return self._error(404, "ErrorItemNotFound", "The specified object was not found in the store.")

        def moved(e: dict) -> dict:
            return {**e, "start": _in_zone(e["start"], zone), "end": _in_zone(e["end"], zone)}
        event = moved(event)
        if "exceptionOccurrences" in event:
            event["exceptionOccurrences"] = [moved(x) for x in event["exceptionOccurrences"]]
            if "exceptionOccurrences" not in expand:
                del event["exceptionOccurrences"]
        return self._reply(200, _selected(event, select))

    def _create(self, body: bytes):
        srv: "FakeGraphServer" = self.server.owner
        if self.headers.get("Content-Type") != "text/plain":
            return self._error(400, "HarnessError", "only MIME drafts are modelled")
        try:
            mime = base64.b64decode(body, validate=True)
        except Exception:
            return self._error(400, "ErrorMimeContentInvalidBase64String", "Invalid base64 string for MIME content.")
        with srv.lock:
            # Real ids are base64 and contain '/' and '+', which the engine must escape in URLs.
            graph_id = f"AAMkAG/{len(srv.messages) + len(srv.deleted) + len(srv.sent) + 1}+Draft="
            msg = GraphMessage(graph_id, mime)
            sender = msg.from_address()
            if srv.mailbox and sender["address"].lower() != srv.mailbox.lower():
                msg.mime = _with_from(msg.mime, sender["name"], srv.mailbox)
            srv.messages[graph_id] = msg
        return self._reply(201, {"id": graph_id, "isDraft": True, "parentFolderId": DRAFTS_ID,
                                 "from": {"emailAddress": msg.from_address()}})

    def _send(self, msg: GraphMessage):
        srv: "FakeGraphServer" = self.server.owner
        allowed = {a.lower() for a in [srv.mailbox] + list(srv.aliases) if a}
        if allowed and msg.from_address()["address"].lower() not in allowed:
            return self._error(403, "ErrorSendAsDenied", "The user account which was used to submit this request "
                               "does not have the right to send mail on behalf of the specified sending account.")
        msg.sent_at = time.time()
        with srv.lock:
            srv.sent.append(msg)

        def file_copy():
            msg.folder = SENT_ITEMS_ID
            msg.filed = True
            if srv.sent_copy:
                store, mailbox = srv.sent_copy
                store.append(mailbox, msg.sent_copy(), ["\\Seen"])
            if srv.deliver_to:
                store, mailbox, address = srv.deliver_to
                if any(r.lower() == address.lower() for r in msg.recipients()):
                    store.append(mailbox, msg.sent_copy(), [])
        if srv.sent_copy_delay:
            threading.Timer(srv.sent_copy_delay, file_copy).start()
        else:
            file_copy()
        return self._reply(202)

    def _upload(self, sid: str, body: bytes):
        srv: "FakeGraphServer" = self.server.owner
        session = srv.uploads.get(sid)
        if session is None:
            return self._error(404, "ResourceNotFound", "upload session not found")
        if self.headers.get("Authorization"):
            return self._error(401, "InvalidAuthenticationToken", "Upload URLs must not carry an Authorization header.")
        m = re.match(r"bytes (\d+)-(\d+)/(\d+)", self.headers.get("Content-Range") or "")
        if not m:
            return self._error(400, "InvalidRange", "Content-Range is required.")
        start, end, total = (int(x) for x in m.groups())
        if start != len(session["data"]) or end - start + 1 != len(body) or total != session["item"]["size"]:
            return self._error(416, "InvalidRange", "The Content-Range does not match the upload.")
        if end + 1 < total and len(body) % UPLOAD_CHUNK_MULTIPLE:
            return self._error(400, "InvalidRange", "Chunks other than the last must be multiples of 320 KiB.")
        session["data"] += body
        if len(session["data"]) < total:
            return self._reply(200, {"nextExpectedRanges": [f"{len(session['data'])}-"]})
        item = session["item"]
        session["message"].attachments.append({"name": item["name"], "contentType": item.get("contentType"),
                                                "isInline": item.get("isInline", False),
                                                "contentId": item.get("contentId"), "data": session["data"]})
        return self._reply(201)


class FakeGraphServer:
    def __init__(self, host="127.0.0.1", port=0, refresh_token="refresh-token", graph_consented=True,
                 sent_copy=None, sent_copy_delay: float = 0, failures: Optional[list] = None, deliver_to=None,
                 consent_refusals: int = 0, mailbox: Optional[str] = None, aliases=(),
                 drop_graph_token: bool = False, calendars: Optional[list] = None,
                 calendars_consented: bool = True, calendars_page_size: int = 0,
                 events: Optional[dict] = None, events_page_size: int = 0):
        """sent_copy: optional (Store, mailbox) every sent message's copy is appended to, as
        Exchange files it in Sent Items. sent_copy_delay: seconds between /send and that copy.
        graph_consented: whether the refresh token may be exchanged for Graph mail scopes (an
        account authorized before Mail.Send was requested may not).
        drop_graph_token: close the connection on Graph mail token requests without answering,
        as when the machine goes offline mid-send.
        consent_refusals: refuse the first N Graph mail token requests with AADSTS65001 anyway,
        as Azure AD does for a few seconds while a consent the user just gave replicates.
        calendars: the Graph calendar resources /me/calendars lists, `calendars_page_size` to a
        page (0: all on one). calendars_consented: whether the refresh token may be exchanged
        for the calendar scopes, which Mailspring has asked for since 2020.
        events: {calendar id: [Graph event resources, each in its own zone]}, listed
        `events_page_size` to a page (0: all on one).
        mailbox / aliases: the mailbox's primary address and the aliases it may send as. A
        MIME draft whose From is any other address gets the primary address instead, a PATCH
        of `from` sets it, and /send refuses an address outside these with ErrorSendAsDenied
        (outlook.com, 2026-10-22).
        failures: [{route: regex on the unescaped path after /v1.0/me/, method, status, code, message,
        times, retry_after}] - answer matching requests with that error, `times` times (default
        always), with a Retry-After header when `retry_after` is given.
        deliver_to: optional (Store, mailbox, address) - sent mail addressed to `address` is
        appended to that mailbox, as Exchange delivers mail the user sends to themself."""
        self.host, self.port = host, port
        self.refresh_token = refresh_token
        self.graph_consented = graph_consented
        self.consent_refusals = consent_refusals
        self.drop_graph_token = drop_graph_token
        self.mailbox = mailbox
        self.aliases = list(aliases)
        self.sent_copy = sent_copy
        self.sent_copy_delay = sent_copy_delay
        self.deliver_to = deliver_to
        self.failures = [dict(f) for f in (failures or [])]
        self.calendars = [dict(c) for c in (calendars or [])]
        self.calendars_consented = calendars_consented
        self.calendars_page_size = calendars_page_size
        self.events = {cid: [dict(e) for e in evs] for cid, evs in (events or {}).items()}
        self.events_page_size = events_page_size
        self.mail_tokens: set = set()
        self.graph_tokens: set = set()   # Graph mail scopes
        self.calendar_tokens: set = set()
        self.token_scopes: list = []
        self.messages: dict = {}   # graph id -> GraphMessage, drafts and filed copies
        self.sent: list = []
        self.deleted: list = []
        self.uploads: dict = {}
        self.requests: list = []
        self.transcript: list = []
        self.lock = threading.Lock()
        self._server = None

    def log(self, method, path, status):
        self.transcript.append((time.time(), method, path, status))

    def set_calendars(self, calendars: list):
        with self.lock:
            self.calendars = [dict(c) for c in calendars]

    def set_events(self, calendar_id: str, events: list):
        with self.lock:
            self.events[calendar_id] = [dict(e) for e in events]

    def set_failures(self, failures: list):
        with self.lock:
            self.failures = [dict(f) for f in failures]

    def failure_for(self, route: str, method: str) -> Optional[dict]:
        with self.lock:
            for f in self.failures:
                if f.get("method", method) != method or not re.search(f["route"], route):
                    continue
                if "times" in f:
                    if f["times"] <= 0:
                        continue
                    f["times"] -= 1
                return f
        return None

    @property
    def url(self) -> str:
        return f"http://127.0.0.1:{self.port}/v1.0"

    @property
    def token_url(self) -> str:
        return f"http://127.0.0.1:{self.port}/token"

    @property
    def identity_url(self) -> str:
        return f"http://127.0.0.1:{self.port}"

    def drafts(self) -> list:
        return [m for m in self.messages.values() if m.sent_at is None]

    def start(self):
        self._server = ThreadingHTTPServer((self.host, self.port), _Handler)
        self._server.daemon_threads = True
        self._server.owner = self
        self.port = self._server.server_address[1]
        threading.Thread(target=self._server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True).start()
        return self

    def stop(self):
        if self._server:
            self._server.shutdown()
            self._server.server_close()
            self._server = None
