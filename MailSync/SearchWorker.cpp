//
//  SearchWorker.cpp
//  MailSync
//
//  Copyright © 2026 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "SearchWorker.hpp"
#include "DeltaStream.hpp"
#include "MailUtils.hpp"
#include "SyncException.hpp"
#include "XOAuth2TokenManager.hpp"
#include "constants.h"

#include <algorithm>
#include <chrono>

#define SEARCH_DEFAULT_LIMIT 1000
#define SEARCH_IDLE_DISCONNECT_SEC 300
#define SEARCH_LOGIN_BACKOFF_SEC 30
#define SEARCH_UID_BATCH 500

static int secondsSetting(const char * env, int fallback) {
    int value = atoi(MailUtils::getEnvUTF8(env).c_str());
    return value > 0 ? value : fallback;
}

static int idleDisconnectSeconds() {
    static const int seconds = secondsSetting("SEARCH_IDLE_DISCONNECT", SEARCH_IDLE_DISCONNECT_SEC);
    return seconds;
}

// Yahoo rate-limits logins (`NO [LIMIT] LOGIN Rate limit hit`, live 2026-10-25), and each
// search would otherwise try again at once. 30 s is the sync workers' own minimum wait after
// a rejected login (LOGIN_REJECTED_MIN_WAIT).
static int loginBackoffSeconds() {
    static const int seconds = secondsSetting("SEARCH_LOGIN_BACKOFF", SEARCH_LOGIN_BACKOFF_SEC);
    return seconds;
}

static bool isControl(unsigned char c) {
    return c < 0x20 || c == 0x7F;
}

static String * criteriaString(const json & criteria, size_t index) {
    if (criteria.size() <= index || !criteria[index].is_string() || criteria[index].get<string>() == "") {
        throw std::invalid_argument("search criteria " + criteria.dump() + " needs a non-empty string");
    }
    string term = criteria[index].get<string>();
    // LITERAL- servers accept non-synchronizing literals of at most 4096 bytes (RFC 7888).
    if (term.size() > 1024) {
        throw std::invalid_argument("search term is longer than 1024 bytes");
    }
    // libetpan sends an ASCII term as a quoted string and escapes only `"` and `\`
    // (mailimap_quoted_char_send), so a CR LF inside one would end the SEARCH line and run
    // the rest as a second command. RFC 3501 keeps CR and LF out of quoted strings.
    if (any_of(term.begin(), term.end(), [](char c) { return isControl((unsigned char)c); })) {
        throw std::invalid_argument("search term holds a control character");
    }
    return AS_MCSTR(term);
}

// A header name is always sent as a quoted string (mailimap_header_fld_name_send), so it
// is held to RFC 5322's field-name: printable ASCII other than ':'.
static String * headerFieldName(const json & criteria, size_t index) {
    if (criteria.size() <= index || !criteria[index].is_string()) {
        throw std::invalid_argument("search criteria " + criteria.dump() + " needs a header name");
    }
    string name = criteria[index].get<string>();
    bool fieldName = !name.empty() && all_of(name.begin(), name.end(), [](unsigned char c) {
        return c >= 33 && c <= 126 && c != ':';
    });
    if (!fieldName) {
        throw std::invalid_argument("search header name is not an RFC 5322 field name");
    }
    return criteriaString(criteria, index);
}

static time_t criteriaDate(const json & criteria) {
    String * str = criteriaString(criteria, 1);
    int y = 0, m = 0, d = 0;
    // A field sscanf could not read stays 0, which the range checks reject.
    (void)sscanf(str->UTF8Characters(), "%4d-%2d-%2d", &y, &m, &d);
    if (m < 1 || m > 12 || d < 1 || d > 31) {
        throw std::invalid_argument("search date " + criteria.dump() + " is not YYYY-MM-DD");
    }
    // mailcore formats the IMAP date with localtime_r, so local noon names the same day
    // in every zone.
    struct tm tm = {};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = 12;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

SearchWorker::SearchWorker(shared_ptr<Account> account) :
    account(account),
    store(nullptr),
    session(IMAPSession()),
    logger(spdlog::get("logger"))
{
}

SearchWorker::~SearchWorker() {
    delete store;
}

string SearchWorker::searchIdOf(const json & packet) {
    // A non-string searchId reads as missing: json::value would throw, and the listen
    // thread's catch aborts the process over one malformed request.
    if (!packet.contains("searchId") || !packet["searchId"].is_string()) {
        return "";
    }
    return packet["searchId"].get<string>();
}

void SearchWorker::enqueue(json request) {
    if (searchIdOf(request) == "") {
        logger->warn("Search request without a searchId ignored");
        return;
    }
    std::unique_lock<std::mutex> lck(requestMtx);
    generation++;
    pending = request;
    hasPending = true;
    requestCv.notify_one();
}

void SearchWorker::cancel(string searchId) {
    std::unique_lock<std::mutex> lck(requestMtx);
    if (hasPending && searchIdOf(pending) == searchId) {
        hasPending = false;
    }
    if (activeSearchId == searchId) {
        generation++;
    }
}

bool SearchWorker::superseded(uint64_t gen) {
    return generation.load() != gen;
}

void SearchWorker::run() {
    // MailStore is bound to the thread that creates it.
    store = new MailStore();

    while (true) {
        json request;
        uint64_t gen;
        {
            std::unique_lock<std::mutex> lck(requestMtx);
            bool woke = requestCv.wait_for(lck, chrono::seconds(idleDisconnectSeconds()), [&] { return hasPending; });
            if (!woke) {
                lck.unlock();
                if (connected) {
                    // Connections count against per-account limits (Gmail: 15 email
                    // clients at a time), so an idle search connection is given back.
                    logger->info("Search connection idle, disconnecting");
                    disconnect();
                }
                continue;
            }
            request = pending;
            hasPending = false;
            gen = generation.load();
            activeSearchId = searchIdOf(request);
        }

        string searchId = activeSearchId;

        AutoreleasePool pool;
        try {
            perform(request, gen);
        } catch (SyncException & ex) {
            logger->error("Search {} failed: {}", searchId, ex.toJSON().dump());
            emitFailure(searchId, gen, ex.key);
        } catch (std::exception & ex) {
            logger->error("Search {} failed: {}", searchId, ex.what());
            emitFailure(searchId, gen, ex.what());
        }
    }
}

void SearchWorker::connect() {
    time_t sinceRejected = time(0) - loginRejectedAt;
    if (sinceRejected < loginBackoffSeconds()) {
        logger->warn("Search skipped: login rejected {}s ago, retrying after {}s", sinceRejected, loginBackoffSeconds());
        throw SyncException(ErrorCode::ErrorAuthentication, "search login backoff");
    }

    // For XOAuth2 accounts this may refresh the access token over HTTP.
    MailUtils::configureSessionForAccount(session, account);

    ErrorCode err = ErrorCode::ErrorNone;
    session.connectIfNeeded(&err);
    if (err != ErrorCode::ErrorNone) {
        throw SyncException(err, "search connectIfNeeded");
    }
    session.loginIfNeeded(&err);
    if (err != ErrorCode::ErrorNone) {
        if (err == ErrorCode::ErrorAuthentication) {
            loginRejectedAt = time(0);
            logger->warn("Search login rejected, not retrying for {}s", loginBackoffSeconds());
            // The sync workers fetch a fresh token after a rejection too (#163).
            if (account->refreshToken() != "") {
                SharedXOAuth2TokenManager()->invalidateTokenForAccount(account);
            }
        }
        disconnect();
        throw SyncException(err, "search loginIfNeeded");
    }
    connected = true;
}

void SearchWorker::disconnect() {
    session.disconnect();
    connected = false;
}

void SearchWorker::perform(const json & request, uint64_t gen) {
    string searchId = request["searchId"].get<string>();
    size_t limit = SEARCH_DEFAULT_LIMIT;
    if (request.count("limit")) {
        if (!request["limit"].is_number_unsigned() || request["limit"].get<size_t>() == 0) {
            throw std::invalid_argument("search limit must be a positive integer");
        }
        limit = request["limit"].get<size_t>();
    }
    string gmailRaw = request.value("gmailRaw", "");

    auto connectStarted = chrono::steady_clock::now();
    connect();
    logger->info("Search {}: connected in {}ms", searchId, chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - connectStarted).count());
    bool gmail = session.storedCapabilities()->containsIndex(IMAPCapabilityGmail);
    // Fastmail's Cyrus reads its Xapian index only under FUZZY (imapd.conf search_fuzzy_always)
    // and scans every message otherwise. Gated on the provider because Cyrus advertises
    // SEARCH=FUZZY with no index configured, and FUZZY then matches nothing (Cyrus 3.6, 2026-10-22).
    bool fuzzy = account->provider() == "fastmail" && session.supportsFuzzySearch();

    IMAPSearchExpression * expression = nullptr;
    if (gmail && gmailRaw != "") {
        expression = IMAPSearchExpression::searchGmailRaw(criteriaString({"gmailRaw", gmailRaw}, 1));
    } else {
        expression = expressionForCriteria(request.at("criteria"));
        // libetpan's SEARCH cannot send a synchronizing literal, the only standard way to send
        // a non-ASCII term to a server without LITERAL+, LITERAL- or XAPPLELITERAL (RFC 3501 4.3).
        string terms = request["criteria"].dump();
        bool ascii = all_of(terms.begin(), terms.end(), [](char c) { return (unsigned char)c < 0x80; });
        if (!ascii && !session.acceptsNonSynchronizingLiterals()) {
            throw std::invalid_argument("non-ASCII search terms need a server that accepts non-synchronizing literals");
        }
        if (fuzzy) {
            expression = IMAPSearchExpression::searchFuzzy(expression);
        }
    }

    vector<shared_ptr<Folder>> folders = foldersToSearch(request, gmail);
    // The query itself stays out of the log: logs travel with crash reports.
    logger->info("Search {}: {} folder(s), {}", searchId, folders.size(), (gmail && gmailRaw != "") ? "X-GM-RAW" : fuzzy ? "FUZZY criteria" : "criteria");

    SearchResults results;
    json errors = json::array();

    // A failed SEARCH is retried once on a fresh connection, whatever the error: a BAD in
    // mid-command can leave the stream out of step with the server (Yahoo then answered every
    // later command BAD, live 2026-10-25), and a connection dropped while idle fails the same
    // way. Only one reconnect per search, and a failure after it ends the search: the query is
    // the same in every folder, and retrying each one cost Office 365, which answers non-ASCII
    // terms NO [BADCHARSET], about 33 logins for one search of 16 folders (live, 2026-10-25).
    bool reconnected = false;
    for (auto & folder : folders) {
        if (superseded(gen)) {
            logger->info("Search {} superseded", searchId);
            return;
        }
        // Through connect() rather than mailcore's implicit login, so the login backoff and
        // token refresh apply and the idle disconnect knows the connection is open.
        if (!connected) {
            connect();
        }

        auto started = chrono::steady_clock::now();
        ErrorCode err = ErrorCode::ErrorNone;
        IndexSet * uids = session.search(AS_MCSTR(folder->path()), expression, &err);
        if (err != ErrorCode::ErrorNone && !reconnected) {
            logger->info("Search {} in {} failed: {}, retrying on a fresh connection", searchId, folder->path(), ErrorCodeToTypeMap[err]);
            reconnected = true;
            disconnect();
            connect();
            err = ErrorCode::ErrorNone;
            uids = session.search(AS_MCSTR(folder->path()), expression, &err);
        }
        if (err != ErrorCode::ErrorNone) {
            disconnect();
            if (err == ErrorCode::ErrorConnection) {
                throw SyncException(err, "search " + folder->path());
            }
            logger->warn("Search {} in {} failed: {}, not searching further folders", searchId, folder->path(), ErrorCodeToTypeMap[err]);
            errors.push_back({{"folderId", folder->id()}, {"error", ErrorCodeToTypeMap[err]}});
            break;
        }

        auto searched = chrono::steady_clock::now();
        collectLocalMessages(folder->id(), uids, results);
        logger->info("Search {} in {}: {} match(es), server {}ms, local {}ms", searchId, folder->path(), uids->count(),
            chrono::duration_cast<chrono::milliseconds>(searched - started).count(),
            chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - searched).count());
        // A cancel or newer request that arrived during this folder's SEARCH ends the
        // search here, so a cancelled search never reports done.
        if (superseded(gen)) {
            logger->info("Search {} superseded", searchId);
            return;
        }
        emit(resultJSON(searchId, results, limit, errors, false));
    }

    // A search the client cancelled while a folder's SEARCH failed has been left.
    if (superseded(gen)) {
        logger->info("Search {} superseded", searchId);
        return;
    }
    emit(resultJSON(searchId, results, limit, errors, true));
}

json SearchWorker::resultJSON(string searchId, SearchResults & results, size_t limit, const json & errors, bool done) {
    // The newest `limit` threads by message date, the order the thread list shows them in,
    // whichever folder each match came from.
    stable_sort(results.matches.begin(), results.matches.end(), [](const SearchMatch & a, const SearchMatch & b) {
        return a.date > b.date;
    });
    vector<string> threadIds;
    set<string> keptThreads;
    for (auto & match : results.matches) {
        if (threadIds.size() < limit && keptThreads.insert(match.threadId).second) {
            threadIds.push_back(match.threadId);
        }
    }
    return {{"id", searchId}, {"accountId", account->id()}, {"done", done}, {"threadIds", threadIds}, {"errors", errors}};
}

vector<shared_ptr<Folder>> SearchWorker::foldersToSearch(const json & request, bool gmail) {
    auto all = store->findAll<Folder>(Query().equal("accountId", account->id()));

    if (request.count("folderIds")) {
        set<string> wanted;
        for (auto & id : request["folderIds"]) {
            wanted.insert(id.get<string>());
        }
        vector<shared_ptr<Folder>> result;
        for (auto & folder : all) {
            if (wanted.count(folder->id())) {
                result.push_back(folder);
            }
        }
        return result;
    }

    // Every folder but Spam and Trash, which Gmail's search also leaves out unless asked
    // for in:anywhere, and but a duplicate \All view, whose messages are found in their own
    // folders. On Gmail that leaves All Mail alone: labels are views of it.
    vector<shared_ptr<Folder>> result;
    for (auto & folder : all) {
        if (folder->role() != "spam" && folder->role() != "trash" && !MailUtils::isDuplicateAllMail(account.get(), folder.get(), gmail)) {
            result.push_back(folder);
        }
    }
    return result;
}

void SearchWorker::collectLocalMessages(string folderId, IndexSet * uids, SearchResults & results) {
    vector<uint32_t> all;
    for (int ii = 0; ii < uids->rangesCount(); ii++) {
        uint64_t left = RangeLeftBound(uids->allRanges()[ii]);
        uint64_t right = RangeRightBound(uids->allRanges()[ii]);
        for (uint64_t uid = left; uid <= right; uid++) {
            all.push_back((uint32_t)uid);
        }
    }

    for (size_t start = 0; start < all.size(); start += SEARCH_UID_BATCH) {
        size_t end = min(start + SEARCH_UID_BATCH, all.size());
        string placeholders = "";
        for (size_t i = start; i < end; i++) {
            placeholders += (i == start) ? "?" : ",?";
        }
        SQLite::Statement query(store->db(),
            "SELECT Message.threadId, Message.date FROM MessageFolder "
            "INNER JOIN Message ON Message.id = MessageFolder.messageId "
            "WHERE MessageFolder.accountId = ? AND MessageFolder.folderId = ? AND MessageFolder.remoteUID IN (" + placeholders + ")");
        query.bind(1, account->id());
        query.bind(2, folderId);
        for (size_t i = start; i < end; i++) {
            query.bind((int)(3 + i - start), (long long)all[i]);
        }
        while (query.executeStep()) {
            results.matches.push_back({query.getColumn(0).getString(), query.getColumn(1).getDouble()});
        }
    }
}

void SearchWorker::emitFailure(string searchId, uint64_t gen, string error) {
    // A search cancelled or superseded before it failed is one the client has left.
    if (superseded(gen)) {
        return;
    }
    emit({{"id", searchId}, {"accountId", account->id()}, {"done", true}, {"error", error}});
}

void SearchWorker::emit(const json & result) {
    SharedDeltaStream()->sendSearchResult(result);
}

IMAPSearchExpression * SearchWorker::expressionForCriteria(const json & criteria) {
    if (!criteria.is_array() || criteria.empty() || !criteria[0].is_string()) {
        throw std::invalid_argument("search criteria " + criteria.dump() + " is not [op, ...]");
    }
    string op = criteria[0].get<string>();

    if (op == "and" || op == "or") {
        if (criteria.size() < 2) {
            throw std::invalid_argument("search " + op + " needs at least one operand");
        }
        IMAPSearchExpression * result = expressionForCriteria(criteria[1]);
        for (size_t i = 2; i < criteria.size(); i++) {
            IMAPSearchExpression * next = expressionForCriteria(criteria[i]);
            result = (op == "and") ? IMAPSearchExpression::searchAnd(result, next) : IMAPSearchExpression::searchOr(result, next);
        }
        return result;
    }
    if (op == "not") {
        if (criteria.size() != 2) {
            throw std::invalid_argument("search not takes one operand");
        }
        return IMAPSearchExpression::searchNot(expressionForCriteria(criteria[1]));
    }
    if (op == "all") return IMAPSearchExpression::searchAll();
    if (op == "from") return IMAPSearchExpression::searchFrom(criteriaString(criteria, 1));
    if (op == "to") return IMAPSearchExpression::searchTo(criteriaString(criteria, 1));
    if (op == "cc") return IMAPSearchExpression::searchCc(criteriaString(criteria, 1));
    if (op == "bcc") return IMAPSearchExpression::searchBcc(criteriaString(criteria, 1));
    if (op == "subject") return IMAPSearchExpression::searchSubject(criteriaString(criteria, 1));
    if (op == "text") return IMAPSearchExpression::searchContent(criteriaString(criteria, 1));
    if (op == "body") return IMAPSearchExpression::searchBody(criteriaString(criteria, 1));
    if (op == "header") return IMAPSearchExpression::searchHeader(headerFieldName(criteria, 1), criteriaString(criteria, 2));
    if (op == "unread") return IMAPSearchExpression::searchUnread();
    if (op == "read") return IMAPSearchExpression::searchRead();
    if (op == "flagged") return IMAPSearchExpression::searchFlagged();
    if (op == "unflagged") return IMAPSearchExpression::searchUnflagged();
    // SINCE and BEFORE compare the server's internal (received) date, the date the client's
    // local search and thread list sort by; SENTSINCE would read the sender's Date header.
    if (op == "since") return IMAPSearchExpression::searchSinceReceivedDate(criteriaDate(criteria));
    if (op == "before") return IMAPSearchExpression::searchBeforeReceivedDate(criteriaDate(criteria));
    if (op == "larger" || op == "smaller") {
        if (criteria.size() != 2 || !criteria[1].is_number_unsigned()) {
            throw std::invalid_argument("search " + op + " takes a byte count");
        }
        uint32_t size = criteria[1].get<uint32_t>();
        return (op == "larger") ? IMAPSearchExpression::searchSizeLarger(size) : IMAPSearchExpression::searchSizeSmaller(size);
    }
    throw std::invalid_argument("unknown search criteria " + op);
}
