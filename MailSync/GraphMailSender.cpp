//
//  GraphMailSender.cpp
//  MailSync
//
//  Copyright © 2026 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "GraphMailSender.hpp"

#include <chrono>
#include <thread>
#include <fstream>
#include <vector>

#include "Account.hpp"
#include "File.hpp"
#include "MailUtils.hpp"
#include "Message.hpp"
#include "NetworkRequestUtils.hpp"
#include "SyncException.hpp"
#include "XOAuth2TokenManager.hpp"
#include "constants.h"
#include "json.hpp"
#include "spdlog/spdlog.h"

#if defined(_MSC_VER)
#include <codecvt>
#include <locale>
#endif

using namespace std;
using namespace nlohmann;

namespace {

const char * GRAPH_BASE = "https://graph.microsoft.com/v1.0";

// Graph's /me/sendMail accepts inline attachments via fileAttachment, which is
// reliable up to a request body of ~4 MB. Above that, individual attachments
// must be uploaded via createUploadSession. We pick the strategy based on the
// raw byte total and add a margin for base64 (~33%) + JSON envelope overhead.
const size_t GRAPH_INLINE_TOTAL_LIMIT = 3 * 1024 * 1024;
const size_t GRAPH_PER_ATTACHMENT_INLINE_LIMIT = 3 * 1024 * 1024;
const size_t GRAPH_UPLOAD_CHUNK_SIZE = 5 * 1024 * 1024; // must be a multiple of 320 KiB

// Turn a contact JSON ({name, email}) into a Graph Recipient.
json toGraphRecipient(const json & contact) {
    json addr = json::object();
    if (contact.count("email") && contact["email"].is_string()) {
        addr["address"] = contact["email"].get<string>();
    }
    if (contact.count("name") && contact["name"].is_string()) {
        addr["name"] = contact["name"].get<string>();
    }
    return json{{"emailAddress", addr}};
}

json toGraphRecipientArray(json & list) {
    json out = json::array();
    if (!list.is_array()) return out;
    for (json & p : list) {
        out.push_back(toGraphRecipient(p));
    }
    return out;
}

string attachmentPath(const File & file) {
    string root = MailUtils::getEnvUTF8("CONFIG_DIR_PATH") + FS_PATH_SEP + "files";
    File mutableCopy = file;
    return MailUtils::pathForFile(root, &mutableCopy, false);
}

// Read entire file into memory. mailcore's Data handles the wide-path quirks
// on Windows in TaskProcessor (line 1518-1523), but here we need the raw bytes
// for both inline base64 and chunked PUT, so we read directly.
vector<uint8_t> readFileBytes(const string & path) {
#if defined(_MSC_VER)
    wstring_convert<codecvt_utf8<wchar_t>, wchar_t> convert;
    ifstream in(convert.from_bytes(path), ios::binary);
#else
    ifstream in(path, ios::binary);
#endif
    if (!in) {
        throw SyncException("send-failed", "Could not read attachment from local disk: " + path, false);
    }
    in.seekg(0, ios::end);
    streamsize size = in.tellg();
    in.seekg(0, ios::beg);
    vector<uint8_t> buf(static_cast<size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char *>(buf.data()), size)) {
        throw SyncException("send-failed", "Failed to read attachment bytes: " + path, false);
    }
    return buf;
}

size_t totalAttachmentBytes(json files) {
    size_t total = 0;
    if (!files.is_array()) return 0;
    for (json & fileJSON : files) {
        File f{fileJSON};
        try {
            string path = attachmentPath(f);
#if defined(_MSC_VER)
            wstring_convert<codecvt_utf8<wchar_t>, wchar_t> convert;
            ifstream in(convert.from_bytes(path), ios::binary | ios::ate);
#else
            ifstream in(path, ios::binary | ios::ate);
#endif
            if (in) {
                total += static_cast<size_t>(in.tellg());
            }
        } catch (...) {
            // ignore; will surface during actual read
        }
    }
    return total;
}

// Build a Graph `Message` resource from the inflated draft.
//
// `recipientOverride` (optional) replaces to/cc/bcc with a single `to`. Used
// for multisend, where every per-recipient call goes to one address with a
// customized body.
//
// `includeFileAttachments` controls whether attachments are inlined. When
// using the /me/messages create-draft + upload-session flow, we leave this
// false and add attachments via separate calls afterwards.
json buildGraphMessage(Message & draft,
                       const string & body,
                       bool plaintext,
                       const string * recipientOverride,
                       bool includeFileAttachments) {
    json msg = json::object();

    msg["subject"] = draft.subject();
    msg["body"] = {
        {"contentType", plaintext ? "Text" : "HTML"},
        {"content", body},
    };

    if (recipientOverride != nullptr) {
        json one = json::object();
        one["emailAddress"] = json{{"address", *recipientOverride}};
        msg["toRecipients"] = json::array({one});
        msg["ccRecipients"] = json::array();
        msg["bccRecipients"] = json::array();
    } else {
        msg["toRecipients"] = toGraphRecipientArray(draft.to());
        msg["ccRecipients"] = toGraphRecipientArray(draft.cc());
        msg["bccRecipients"] = toGraphRecipientArray(draft.bcc());
    }

    if (draft.replyTo().is_array() && draft.replyTo().size() > 0) {
        msg["replyTo"] = toGraphRecipientArray(draft.replyTo());
    }

    if (draft.from().is_array() && draft.from().size() > 0) {
        msg["from"] = toGraphRecipient(draft.from().at(0));
    }

    // Match the SMTP path's behavior at TaskProcessor.cpp:1472-1484 — preserve
    // our own Message-ID and reply/forward references.
    json headers = json::array();
    if (draft.headerMessageId() != "") {
        // Graph also accepts `internetMessageId` as a top-level property and
        // round-trips it without modification, which is what we want.
        msg["internetMessageId"] = "<" + draft.headerMessageId() + ">";
    }
    if (draft.replyToHeaderMessageId() != "") {
        string ref = "<" + draft.replyToHeaderMessageId() + ">";
        headers.push_back({{"name", "In-Reply-To"}, {"value", ref}});
        headers.push_back({{"name", "References"}, {"value", ref}});
    } else if (draft.forwardedHeaderMessageId() != "") {
        string ref = "<" + draft.forwardedHeaderMessageId() + ">";
        headers.push_back({{"name", "References"}, {"value", ref}});
    }
    if (!headers.empty()) {
        msg["internetMessageHeaders"] = headers;
    }

    if (includeFileAttachments) {
        json attachments = json::array();
        json files = draft.files();
        if (files.is_array()) {
            for (json & fileJSON : files) {
                File file{fileJSON};
                string path = attachmentPath(file);
                vector<uint8_t> bytes = readFileBytes(path);
                string b64 = MailUtils::toBase64(reinterpret_cast<const char *>(bytes.data()), bytes.size());

                json a = {
                    {"@odata.type", "#microsoft.graph.fileAttachment"},
                    {"name", file.filename()},
                    {"contentType", file.contentType() != "" ? file.contentType() : "application/octet-stream"},
                    {"contentBytes", b64},
                };
                if (file.contentId().is_string()) {
                    a["isInline"] = true;
                    a["contentId"] = file.contentId().get<string>();
                }
                attachments.push_back(a);
            }
        }
        if (!attachments.empty()) {
            msg["attachments"] = attachments;
        }
    }

    return msg;
}

string urlEscape(const string & in) {
    CURL * h = curl_easy_init();
    char * esc = curl_easy_escape(h, in.c_str(), 0);
    string out{esc};
    curl_free(esc);
    curl_easy_cleanup(h);
    return out;
}

// Look up the Graph message id for a draft we already APPEND'd to IMAP. We
// match on the RFC 2822 Message-ID we stamped on the draft. Replication from
// IMAP -> Exchange isn't always instant, so retry briefly. Returns "" if not
// found after retries.
string findGraphMessageIdByMessageId(const string & headerMessageId, const string & authorization) {
    auto logger = spdlog::get("logger");
    string filter = "internetMessageId eq '<" + headerMessageId + ">'";
    string url = string(GRAPH_BASE) + "/me/messages?$filter=" + urlEscape(filter) + "&$select=id&$top=1";

    int delays[] = {0, 1, 2}; // mirrors the sent-folder retry pattern in TaskProcessor.cpp:1607-1618
    for (int i = 0; i < 3; i++) {
        if (delays[i] > 0) {
            logger->info("-- Graph: draft not yet visible, sleeping {}s for replication", delays[i]);
            this_thread::sleep_for(chrono::seconds(delays[i]));
        }
        json resp = PerformJSONRequest(CreateJSONRequest(url, "GET", authorization, nullptr));
        if (resp.count("value") && resp["value"].is_array() && resp["value"].size() > 0) {
            const json & m = resp["value"][0];
            if (m.count("id") && m["id"].is_string()) {
                return m["id"].get<string>();
            }
        }
    }
    return "";
}

// Strategy A: tell Graph to send the existing draft (already APPEND'd via IMAP).
// Returns true on success, false if the lookup didn't find the draft (caller
// should fall through to Strategy B).
bool tryStrategyA(Message & draft, const string & authorization) {
    auto logger = spdlog::get("logger");
    if (draft.remoteUID() == 0 || draft.headerMessageId() == "") {
        return false;
    }
    string graphId = findGraphMessageIdByMessageId(draft.headerMessageId(), authorization);
    if (graphId == "") {
        logger->info("-- Graph Strategy A: no matching draft visible via Graph; falling back");
        return false;
    }
    logger->info("-- Graph Strategy A: found existing draft {}, calling /send", graphId);
    string url = string(GRAPH_BASE) + "/me/messages/" + graphId + "/send";
    // POST /send returns 202 Accepted with empty body. PerformRequest accepts
    // any 2xx; CleanupCurlRequest is invoked inside.
    PerformRequest(CreateJSONRequest(url, "POST", authorization, nullptr));
    return true;
}

// Upload a single attachment via createUploadSession + chunked PUT.
void uploadLargeAttachment(const string & messageId,
                           File & file,
                           const string & authorization) {
    auto logger = spdlog::get("logger");

    string path = attachmentPath(file);
    vector<uint8_t> bytes = readFileBytes(path);

    json sessionReq = {
        {"AttachmentItem", {
            {"attachmentType", "file"},
            {"name", file.filename()},
            {"size", bytes.size()},
            {"contentType", file.contentType() != "" ? file.contentType() : "application/octet-stream"},
        }},
    };
    if (file.contentId().is_string()) {
        sessionReq["AttachmentItem"]["isInline"] = true;
        sessionReq["AttachmentItem"]["contentId"] = file.contentId().get<string>();
    }

    string sessionUrl = string(GRAPH_BASE) + "/me/messages/" + messageId + "/attachments/createUploadSession";
    string sessionBody = sessionReq.dump();
    json sessionResp = PerformJSONRequest(CreateJSONRequest(sessionUrl, "POST", authorization, sessionBody.c_str()));

    if (!sessionResp.count("uploadUrl") || !sessionResp["uploadUrl"].is_string()) {
        throw SyncException("send-failed", "Graph upload session creation returned no uploadUrl", true);
    }
    string uploadUrl = sessionResp["uploadUrl"].get<string>();

    // Chunked PUT. The upload session URL is pre-authorized; we don't send the
    // bearer token here.
    size_t total = bytes.size();
    size_t offset = 0;
    while (offset < total) {
        size_t end = min(offset + GRAPH_UPLOAD_CHUNK_SIZE, total) - 1;
        size_t len = end - offset + 1;

        CURL * h = curl_easy_init();
        curl_easy_setopt(h, CURLOPT_URL, uploadUrl.c_str());
        curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, bytes.data() + offset);
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)len);

        struct curl_slist * headers = nullptr;
        string contentRange = "Content-Range: bytes " + to_string(offset) + "-" + to_string(end) + "/" + to_string(total);
        headers = curl_slist_append(headers, contentRange.c_str());
        headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);

        string respBody;
        curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, _onAppendToString);
        curl_easy_setopt(h, CURLOPT_WRITEDATA, (void *)&respBody);

        CURLcode res = curl_easy_perform(h);
        long http_code = 0;
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(h);

        if (res != CURLE_OK || http_code < 200 || http_code > 299) {
            throw SyncException("send-failed",
                                "Graph upload PUT failed (http=" + to_string(http_code) + "): " + respBody,
                                http_code >= 500);
        }
        logger->info("-- Graph: uploaded chunk {}-{}/{}", offset, end, total);
        offset = end + 1;
    }
}

void runStrategyB(shared_ptr<Account> account,
                  Message & draft,
                  const string & body,
                  bool plaintext,
                  bool saveToSentItems,
                  const string * recipientOverride,
                  const string & authorization) {
    auto logger = spdlog::get("logger");

    json files = draft.files();
    size_t totalBytes = files.is_array() ? totalAttachmentBytes(files) : 0;
    bool hasLargeAttachment = false;
    if (files.is_array()) {
        for (json & fj : files) {
            File f{fj};
            string p = attachmentPath(f);
#if defined(_MSC_VER)
            wstring_convert<codecvt_utf8<wchar_t>, wchar_t> convert;
            ifstream in(convert.from_bytes(p), ios::binary | ios::ate);
#else
            ifstream in(p, ios::binary | ios::ate);
#endif
            if (in && (size_t)in.tellg() > GRAPH_PER_ATTACHMENT_INLINE_LIMIT) {
                hasLargeAttachment = true;
                break;
            }
        }
    }

    bool useUploadSession = hasLargeAttachment || totalBytes > GRAPH_INLINE_TOTAL_LIMIT;

    if (!useUploadSession) {
        logger->info("-- Graph Strategy B (inline): POST /me/sendMail (attachments={} bytes)", totalBytes);
        json msg = buildGraphMessage(draft, body, plaintext, recipientOverride, /*includeFileAttachments*/ true);
        json envelope = {
            {"message", msg},
            {"saveToSentItems", saveToSentItems},
        };
        string url = string(GRAPH_BASE) + "/me/sendMail";
        string bodyStr = envelope.dump();
        PerformRequest(CreateJSONRequest(url, "POST", authorization, bodyStr.c_str()));
        return;
    }

    logger->info("-- Graph Strategy B (upload session): create draft + upload {} bytes + send", totalBytes);

    // 1. Create draft message (no attachments yet).
    json draftMsg = buildGraphMessage(draft, body, plaintext, recipientOverride, /*includeFileAttachments*/ false);
    string createUrl = string(GRAPH_BASE) + "/me/messages";
    string createBody = draftMsg.dump();
    json createResp = PerformJSONRequest(CreateJSONRequest(createUrl, "POST", authorization, createBody.c_str()));
    if (!createResp.count("id") || !createResp["id"].is_string()) {
        throw SyncException("send-failed", "Graph create draft returned no id: " + createResp.dump(), true);
    }
    string graphId = createResp["id"].get<string>();

    // 2. For each attachment: small ones via /attachments POST, large ones via
    //    upload session.
    if (files.is_array()) {
        for (json & fj : files) {
            File file{fj};
            string p = attachmentPath(file);
            vector<uint8_t> bytes = readFileBytes(p);
            if (bytes.size() > GRAPH_PER_ATTACHMENT_INLINE_LIMIT) {
                uploadLargeAttachment(graphId, file, authorization);
            } else {
                string b64 = MailUtils::toBase64(reinterpret_cast<const char *>(bytes.data()), bytes.size());
                json a = {
                    {"@odata.type", "#microsoft.graph.fileAttachment"},
                    {"name", file.filename()},
                    {"contentType", file.contentType() != "" ? file.contentType() : "application/octet-stream"},
                    {"contentBytes", b64},
                };
                if (file.contentId().is_string()) {
                    a["isInline"] = true;
                    a["contentId"] = file.contentId().get<string>();
                }
                string attachUrl = string(GRAPH_BASE) + "/me/messages/" + graphId + "/attachments";
                string attachBody = a.dump();
                PerformRequest(CreateJSONRequest(attachUrl, "POST", authorization, attachBody.c_str()));
            }
        }
    }

    // 3. Send. Note: /send with saveToSentItems=false isn't directly supported
    //    via this path; for multisend we instead delete the auto-saved sent
    //    copy after the fact. The IMAP cleanup in performRemoteSendDraft
    //    already handles this scenario.
    string sendUrl = string(GRAPH_BASE) + "/me/messages/" + graphId + "/send";
    PerformRequest(CreateJSONRequest(sendUrl, "POST", authorization, nullptr));
}

} // namespace

void sendViaGraph(shared_ptr<Account> account,
                  Message & draft,
                  const string & body,
                  bool plaintext,
                  bool saveToSentItems,
                  const string * recipientOverride) {
    auto logger = spdlog::get("logger");

    XOAuth2Parts parts;
    try {
        parts = SharedXOAuth2TokenManager()->partsForAccount(account, XOAuth2ScopeKind::GRAPH_MAIL_SEND);
    } catch (SyncException & e) {
        // Surface a clearer message: this is almost always because the user
        // consented before Mail.Send was added to the scope set.
        throw SyncException("send-failed",
                            "Sending via Microsoft Graph requires re-authentication. Please remove and re-add this account in Mailspring's preferences. (" + string(e.what()) + ")",
                            false);
    }
    string authorization = "Bearer " + parts.accessToken;

    // Strategy A: send the existing IMAP-uploaded draft directly (no
    // re-upload). Skipped for multisend since each recipient needs a
    // different body.
    if (recipientOverride == nullptr) {
        if (tryStrategyA(draft, authorization)) {
            return;
        }
    }

    runStrategyB(account, draft, body, plaintext, saveToSentItems, recipientOverride, authorization);
}
