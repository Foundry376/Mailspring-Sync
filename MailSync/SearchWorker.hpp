//
//  SearchWorker.hpp
//  MailSync
//
//  Copyright © 2026 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef SearchWorker_hpp
#define SearchWorker_hpp

#include <stdio.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <MailCore/MailCore.h>

#include "Account.hpp"
#include "Folder.hpp"
#include "MailProcessor.hpp"
#include "MailStore.hpp"
#include "json.hpp"
#include "spdlog/spdlog.h"

using namespace nlohmann;
using namespace std;
using namespace mailcore;

struct SearchMatch {
    string threadId;
    double date;
    string messageId;
    string folderId;
    uint32_t uid;
};

struct SearchResults {
    vector<SearchMatch> matches;
};

// How far a search has got with downloading its candidates' bodies. `candidates` is the
// number of matched messages kept after the cap, `fetched` those whose body is stored,
// already or now, `skipped` those over the size limit or that the server would not give.
struct FetchProgress {
    size_t candidates = 0;
    size_t fetched = 0;
    size_t skipped = 0;
};

// Runs "search-remote" requests against the IMAP server on a connection of its own, so a
// search never waits behind a sync pass or interrupts IDLE. Requests are served one at a
// time and a newer request supersedes the one in flight: the user has moved on from it.
class SearchWorker {
    shared_ptr<Account> account;
    MailStore * store;
    MailProcessor * processor;
    IMAPSession session;
    shared_ptr<spdlog::logger> logger;

    mutex requestMtx;
    condition_variable requestCv;
    json pending;
    bool hasPending = false;
    atomic<uint64_t> generation { 0 };
    bool connected = false;
    time_t loginRejectedAt = 0;
    string activeSearchId;

public:
    SearchWorker(shared_ptr<Account> account);
    ~SearchWorker();

    // Called on the main thread. A request whose searchId is missing or not a string is
    // ignored, never answered.
    void enqueue(json request);
    void cancel(string searchId);
    static string searchIdOf(const json & packet);

    // Never returns: serves requests on the calling thread.
    void run();

    // Throws std::invalid_argument for anything outside the documented grammar, so a
    // malformed request fails alone instead of sending the server a different query.
    static IMAPSearchExpression * expressionForCriteria(const json & criteria);

private:
    bool superseded(uint64_t gen);
    void perform(const json & request, uint64_t gen);
    void connect();
    void disconnect();
    vector<shared_ptr<Folder>> foldersToSearch(const json & request, bool gmail);
    void collectLocalMessages(string folderId, IndexSet * uids, SearchResults & results);
    vector<SearchMatch> candidatesToFetch(SearchResults & results, size_t fetchLimit, FetchProgress & progress);
    void fetchCandidates(string searchId, uint64_t gen, vector<SearchMatch> candidates, SearchResults & results,
                         size_t limit, json & errors, FetchProgress & progress);
    bool storeFetchedBody(const SearchMatch & match, Data * data);
    bool fetchFromAnotherCopy(const SearchMatch & match);
    json resultJSON(string searchId, SearchResults & results, size_t limit, const json & errors,
                    string phase, const FetchProgress & progress, bool done);
    void emitFailure(string searchId, uint64_t gen, string error);
    void emit(const json & result);
};

#endif /* SearchWorker_hpp */
