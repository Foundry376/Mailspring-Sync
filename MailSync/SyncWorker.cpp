
//  SyncWorker.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//
#include <algorithm>
#include <map>
#include <set>

#include "SyncWorker.hpp"
#include "MailUtils.hpp"
#include "MailStoreTransaction.hpp"
#include "Folder.hpp"
#include "Label.hpp"
#include "File.hpp"
#include "Task.hpp"
#include "TaskProcessor.hpp"
#include "Account.hpp"
#include "constants.h"
#include "ProgressCollectors.hpp"
#include "SyncException.hpp"


#define CACHE_CLEANUP_INTERVAL      60 * 60
#define SHALLOW_SCAN_INTERVAL       60 * 2
#define DEEP_SCAN_INTERVAL          60 * 10
// How often we verify a CONDSTORE+QRESYNC folder against the server in full. These servers tell us
// about every change, so this is only a safety net for messages we never ingested in the first
// place (which no modseq will ever cover) and can be very infrequent.
#define CONDSTORE_GAP_SCAN_INTERVAL 60 * 60 * 24
// The longest the orphan sweep waits for a folder that has not been fully scanned since a
// message was orphaned. A folder whose STATUS fails on every pass (a shared mailbox without
// the `r` right, RFC 4314 4) or whose scan truncates on every pass would otherwise keep every
// orphan forever. A day outlasts a server outage or a backlog draining one
// MAX_FULL_HEADERS_REQUEST_SIZE batch per pass, and orphans are invisible to the client, so
// waiting costs only disk. Past the wait an orphan is swept even if such a folder holds its
// last copy, which comes back without its plugin metadata if the folder ever becomes readable:
// the accepted cost, since the user cannot see an unreadable mailbox, and dropping the limit
// would stall the sweep for good. A folder whose initial walk towards UID 1 moved down in the
// pass is exempt: the walk will reach every copy, and a first sync of a folder with hundreds of
// thousands of messages can take days. The ORPHAN_SWEEP_MAX_WAIT environment variable (seconds)
// overrides it so the test harness can reach it.
#define ORPHAN_SWEEP_MAX_WAIT       60 * 60 * 24

#define MAX_FULL_HEADERS_REQUEST_SIZE  1024
// How many messages a folder can hold and still be swept in a single UID range during initial
// sync. This is a separate question from MAX_FULL_HEADERS_REQUEST_SIZE (how many full headers we
// will ask for at once): a folder with a high UIDNEXT and few messages should be grabbed in one
// range rather than walked a chunk at a time through mostly-empty UID space. Tying the two
// together makes a 2,000 message folder with UIDNEXT 200,000 issue ~200 empty round trips.
#define MAX_SINGLE_RANGE_SWEEP_SIZE    5000
#define MODSEQ_TRUNCATION_THRESHOLD 4000
#define MODSEQ_TRUNCATION_UID_COUNT 12000

using namespace mailcore;
using namespace std;

static time_t orphanSweepMaxWait() {
    static const time_t wait = [] {
        long long seconds = atoll(MailUtils::getEnvUTF8("ORPHAN_SWEEP_MAX_WAIT").c_str());
        return seconds > 0 ? (time_t)seconds : (time_t)(ORPHAN_SWEEP_MAX_WAIT);
    }();
    return wait;
}


SyncWorker::SyncWorker(shared_ptr<Account> account) :
    store(new MailStore()),
    account(account),
    logger(spdlog::get("logger")),
    processor(new MailProcessor(account, store)),
    session(IMAPSession())
{
    store->setStreamDelay(500);
}

void SyncWorker::configure()
{
    // For accounts connecting with XOAuth2, this function may
    // make HTTP requests so it's important this function is called
    // within the thread retry handlers.
    MailUtils::configureSessionForAccount(session, account);
}

void SyncWorker::idleInterrupt()
{
    // called on main / random threads to interrupt idle
    // do not call functions from here to avoid accidentally running on two threads!
    std::unique_lock<std::mutex> lck(idleMtx);
    idleShouldReloop = true;
    session.interruptIdle();
    idleCv.notify_one();
}

void SyncWorker::idleQueueBodiesToSync(vector<string> & ids) {
    // called on main thread
    std::unique_lock<std::mutex> lck(idleMtx);
    for (string & id : ids) {
        idleFetchBodyIDs.push_back(id);
    }
}

void SyncWorker::idleCycleIteration()
{
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;

    // Run body requests from the client. The inbox is the folder this worker keeps
    // selected, so a copy there is fetched without a SELECT.
    shared_ptr<Folder> preferredFolder = nullptr;
    while (true) {
        string id;
        {
            std::unique_lock<std::mutex> lck(idleMtx);
            if (idleFetchBodyIDs.empty()) {
                break;
            }
            id = idleFetchBodyIDs.back();
            idleFetchBodyIDs.pop_back();
        }
        if (preferredFolder == nullptr) {
            preferredFolder = store->find<Folder>(Query().equal("accountId", account->id()).equal("role", "inbox"));
        }
        Query byId = Query().equal("id", id);
        auto msg = store->find<Message>(byId);
        if (msg.get() != nullptr) {
            logger->info("Fetching body for message ID {}", msg->id());

            // Check if session is connected before attempting fetch
            if (session.isDisconnected()) {
                logger->warn("IMAP session not connected, connecting before body fetch");
                ErrorCode connectErr = ErrorCode::ErrorNone;
                session.connectIfNeeded(&connectErr);
                if (connectErr != ErrorCode::ErrorNone) {
                    logger->error("Failed to connect for body fetch: {}", ErrorCodeToTypeMap[connectErr]);
                    continue;
                }
                session.loginIfNeeded(&connectErr);
                if (connectErr != ErrorCode::ErrorNone) {
                    logger->error("Failed to login for body fetch: {}", ErrorCodeToTypeMap[connectErr]);
                    continue;
                }
            }

            syncMessageBody(msg.get(), preferredFolder.get());
        }
    }

    if (idleShouldReloop) {
        idleShouldReloop = false;
        return;
    }
    
    // Connect and login to the IMAP server
    
    ErrorCode err = ErrorCode::ErrorNone;
    session.connectIfNeeded(&err);
    if (err != ErrorCode::ErrorNone) {
        string advice = MailUtils::tlsFailureAdvice(err, session.lastTLSErrorDescription(), session.isObsoleteTLSAllowed());
        if (advice != "") {
            logger->error("{}", advice);
            throw SyncException(err, "connectIfNeeded: " + advice);
        }
        throw SyncException(err, "connectIfNeeded");
    }
    
    if (idleShouldReloop) {
        idleShouldReloop = false;
        return;
    }

    session.loginIfNeeded(&err);
    if (err != ErrorCode::ErrorNone) {
        throw SyncException(err, "loginIfNeeded");
    }
    processor->setIsGmail(session.storedCapabilities()->containsIndex(IMAPCapabilityGmail));

    if (idleShouldReloop) {
        idleShouldReloop = false;
        return;
    }

    // Run tasks ready for performRemote. This is set up in an odd way
    // because we want tasks created when a task runs to also be run
    // immediately. (eg: A SendDraftTask queueing a SyncbackMetadataTask)
    //
    vector<shared_ptr<Task>> tasks;
    TaskProcessor processor { account, store, &session };

    // Ensure our pile of completed tasks doesn't grow unbounded
    processor.cleanupOldTasksAtRuntime();

    // Find tasks ready for "remote" that we haven't processed yet in this pass
    int rowid = -1;
    SQLite::Statement statement(store->db(), "SELECT rowid, data FROM Task WHERE accountId = ? AND status = \"remote\" AND rowid > ?");

    do {
        tasks = {};
        statement.bind(1, account->id());
        statement.bind(2, rowid);
        while (statement.executeStep()) {
            tasks.push_back(make_shared<Task>(statement));
            rowid = max(rowid, statement.getColumn("rowid").getInt());
        }
        statement.reset();
        for (auto & task : tasks) {
            processor.performRemote(task.get());
        }
    } while (tasks.size() > 0);

    if (idleShouldReloop) {
        idleShouldReloop = false;
        return;
    }

    // Identify the preferred idle folder (inbox / all)
    
    Query q = Query().equal("accountId", account->id()).equal("role", "inbox");
    auto inbox = store->find<Folder>(q);
    if (inbox.get() == nullptr) {
        Query q = Query().equal("accountId", account->id()).equal("role", "all");
        inbox = store->find<Folder>(q);
        if (inbox.get() == nullptr) {
            throw SyncException("no-inbox", "There is no inbox or all folder to IDLE on.", false);
        }
    }
    // Note: must be copy-assignment, not brace-init - `json x { obj }` builds a single-element
    // ARRAY, which makes every saveFolderStatus key comparison below miss and causes this worker
    // to write its whole stale snapshot back over the row, including syncedMinUID.
    json inboxInitialStatus = inbox->localStatus();
    
    if (idleShouldReloop) {
        idleShouldReloop = false;
        return;
    }
    
    // Check for mail in the preferred idle folder (inbox / all)
    // Must verify both key presence AND that value is a number (not null) to prevent
    // JSON type_error exception when calling .get<uint32_t>() below
    bool hasStartedSyncingFolder = inbox->localStatus().count(LS_SYNCED_MIN_UID) > 0 &&
                                   inbox->localStatus()[LS_SYNCED_MIN_UID].is_number();

    if (hasStartedSyncingFolder) {
        String path = AS_MCSTR(inbox->path());

        if (idleExitedWithError) {
            idleExitedWithError = false;
            // The dead connection's VANISHED are lost and the reconnect above leaves nothing
            // selected; re-select so the CHANGEDSINCE fetch re-reports them as VANISHED (EARLIER).
            session.selectIfNeeded(&path, &err);
            if (err != ErrorCode::ErrorNone) {
                throw SyncException(err, "selectIfNeeded after IDLE exited with an error");
            }
        }

        // Expunges the server told us about on this connection - during the IDLE we just
        // exited, but also alongside the body fetches and task commands above. It only
        // tells us once, so these are lost if we don't apply them before the FETCH below.
        deleteVanishedUIDs(*inbox, session.takeVanishedMessages(&path), "this connection");

        IMAPFolderStatus remoteStatus = session.folderStatus(&path, &err);

        // Note: If we have CONDSTORE but don't have QRESYNC, this if/else may result
        // in us not seeing "vanished" messages until the next shallow sync iteration.
        // Right now I think that's fine.
        if (session.storedCapabilities()->containsIndex(IMAPCapabilityCondstore)) {
            syncFolderChangesViaCondstore(*inbox, remoteStatus, false);
        } else {
            uint32_t uidnext = remoteStatus.uidNext();
            uint32_t syncedMinUID = inbox->localStatus()[LS_SYNCED_MIN_UID].get<uint32_t>();
            uint32_t bottomUID = store->fetchMessageUIDAtDepth(*inbox, 100, uidnext);
            if (bottomUID < syncedMinUID) { bottomUID = syncedMinUID; }
            // Guard against underflow if uidnext <= bottomUID (server inconsistency)
            bool truncated = false;
            if (uidnext > bottomUID) {
                truncated = syncFolderUIDRange(*inbox, RangeMake(bottomUID, uidnext - bottomUID), false).truncated;
            }
            inbox->localStatus()[LS_LAST_SHALLOW] = time(0);
            // uidnext is what the background pass reads to find new mail; advancing it past a
            // truncated fetch would hide the remainder from that path until the next deep scan.
            if (!truncated) {
                inbox->localStatus()[LS_UIDNEXT] = uidnext;
            }
        }

        syncMessageBodies(*inbox, remoteStatus);
        
        store->saveFolderStatus(inbox.get(), inboxInitialStatus);
    }

    // Idle on the folder
    
    if (idleShouldReloop) {
        idleShouldReloop = false;
        return;
    }
    if (session.setupIdle()) {
        logger->info("Idling on folder {}", inbox->path());
        String path = AS_MCSTR(inbox->path());
        session.idle(&path, 0, &err);
        session.unsetupIdle();
        logger->info("Idle exited with code {}", err);
        idleExitedWithError = (err != ErrorCode::ErrorNone);
        
        // Ben Note: We don't throw these errors because Yandex (maybe others) abruptly and
        // randomly close IDLE connections - and that's ok! The point is to idle "for a while"
        // and then reconnect and idle again. If the reconnect fails on the next iteration,
        // /that/ error will propagate up and trigger the `retryable=true` flow.
    } else {
        logger->info("Connection does not support idling. Locking until more to do...");
        std::unique_lock<std::mutex> lck(idleMtx);
        idleCv.wait(lck);
    }
}

// Background Behaviors

void SyncWorker::markAllFoldersBusy() {
    logger->info("Marking all folders as `busy`");
    {
        MailStoreTransaction transaction(store, "markAllFoldersBusy");
        auto allLocalFolders = store->findAll<Folder>(Query().equal("accountId", account->id()));
        for (auto f : allLocalFolders) {
            f->localStatus()[LS_BUSY] = true;
            store->save(f.get());
        }
        transaction.commit();
    }
}


void SyncWorker::markFolderStatusSynced(json & localStatus, IMAPFolderStatus & remoteStatus) {
    localStatus[LS_LAST_SHALLOW] = time(0);
    localStatus[LS_LAST_DEEP] = time(0);
    localStatus[LS_BODIES_WANTED] = 0;
    localStatus[LS_SYNCED_MIN_UID] = 1;
    localStatus[LS_UIDNEXT] = remoteStatus.uidNext();
    localStatus[LS_MESSAGE_COUNT] = remoteStatus.messageCount();
    localStatus[LS_UNSEEN_COUNT] = remoteStatus.unseenCount();
    localStatus[LS_RECENT_COUNT] = remoteStatus.recentCount();
    // Note: markAllFoldersBusy sets `busy` on every folder at launch and on wake-workers; a
    // folder that never reaches the end-of-pass bookkeeping has to clear it here.
    localStatus[LS_BUSY] = false;
}

bool SyncWorker::syncNow()
{
    AutoreleasePool pool;
    bool syncAgainImmediately = false;

    // A message orphaned before this instant has had every folder that this pass covers in
    // full scanned since, so a copy that moved into one of them has been recorded. Recorded
    // before any scan so the foreground worker's orphans get the same grace, and it is the
    // pass start rather than a folder's scan start so one clock serves every folder.
    time_t passStartedAt = time(0);

    // A folder the pass does not cover in full (STATUS failed, still in initial sync, a
    // truncated fetch) may hold a copy the sweep would otherwise treat as gone, so the sweep
    // only takes orphans older than that folder's last full coverage - but never waits more
    // than orphanSweepMaxWait() for it, unless its initial walk moved down this pass. A walk
    // through a very large folder can take days, and it will reach every copy eventually.
    time_t waitLimit = passStartedAt - orphanSweepMaxWait();
    time_t sweepBefore = passStartedAt;
    auto recordCoverage = [&](Folder & folder, bool covered, bool walkProgressed = false) {
        if (covered) {
            folderCoveredAt[folder.id()] = passStartedAt;
            foldersPastOrphanWait.erase(folder.id());
            return;
        }
        time_t coveredAt = folderCoveredAt.count(folder.id()) ? folderCoveredAt[folder.id()] : 0;
        if (walkProgressed) {
            sweepBefore = min(sweepBefore, coveredAt);
            return;
        }
        if (coveredAt < waitLimit && foldersPastOrphanWait.insert(folder.id()).second) {
            if (coveredAt) {
                logger->warn("Orphans older than {}s are swept without waiting for {}, not fully scanned for {:.1f}h.",
                             orphanSweepMaxWait(), folder.path(), (passStartedAt - coveredAt) / 3600.0);
            } else {
                logger->warn("Orphans older than {}s are swept without waiting for {}, not fully scanned since launch.",
                             orphanSweepMaxWait(), folder.path());
            }
        }
        sweepBefore = min(sweepBefore, max(coveredAt, waitLimit));
    };

    vector<shared_ptr<Folder>> folders = syncFoldersAndLabels();
    bool hasCondstore = session.storedCapabilities()->containsIndex(IMAPCapabilityCondstore);
    bool hasQResync = session.storedCapabilities()->containsIndex(IMAPCapabilityQResync);
    bool isGmail = session.storedCapabilities()->containsIndex(IMAPCapabilityGmail);
    processor->setIsGmail(isGmail);

    // iCloud's QRESYNC implementation has known issues: it returns malformed VANISHED
    // responses and doesn't send the ENABLED untagged response per RFC. This causes
    // messages to be incorrectly detected as deleted. Disable QRESYNC for iCloud.
    // See: https://developer.apple.com/forums/thread/694251
    if (account->isICloud() && hasQResync) {
        logger->info("Disabling QRESYNC for iCloud account due to known server compatibility issues");
        hasQResync = false;
    }

    // The STATUS below for the folder this connection left selected on the last pass would
    // report that pass's MESSAGES, UIDNEXT and HIGHESTMODSEQ, and nothing in a quiet pass
    // refreshes the view, so mail arriving in that folder would go unnoticed until some other
    // folder was selected here. A QRESYNC session needs this as much as a plain one: VANISHED
    // reports expunges, not appends.
    if (session.currentFolder() != NULL) {
        noopSelectedFolder();
    }

    // Identify folders to sync. On Gmail, labels are mapped to IMAP folders and
    // we only want to sync all, spam, and trash.
    
    array<string, 7> roleOrder{"inbox", "sent", "drafts", "all", "archive", "trash", "spam"};
    sort(folders.begin(), folders.end(), [&roleOrder](const shared_ptr<Folder> lhs, const shared_ptr<Folder> rhs) {
        ptrdiff_t lhsRank = find(roleOrder.begin(), roleOrder.end(), lhs->role()) - roleOrder.begin();
        ptrdiff_t rhsRank = find(roleOrder.begin(), roleOrder.end(), rhs->role()) - roleOrder.begin();
        return lhsRank < rhsRank;
    });

    for (auto & folder : folders) {
        String path = AS_MCSTR(folder->path());
        ErrorCode err = ErrorCode::ErrorNone;
        IMAPFolderStatus * remoteStatusPtr = session.folderStatus(&path, &err);
        if (err != ErrorNone) {
            logger->warn("SyncNow: unable to get folder status for {} ({}), skipping...", folder->path(), ErrorCodeToTypeMap[err]);
            // markAllFoldersBusy set `busy`, and nothing below will run to clear it.
            json initialLocalStatus = folder->localStatus();
            folder->localStatus()[LS_BUSY] = false;
            store->saveFolderStatus(folder.get(), initialLocalStatus);
            recordCoverage(*folder, false);
            continue;
        }

        json & localStatus = folder->localStatus();
        json initialLocalStatus = localStatus; // note: json not json&
        IMAPFolderStatus & remoteStatus = *remoteStatusPtr;
        bool firstChunk = false;
        bool deepScanIncomplete = false;
        bool covered = true;

        // NetEase omits UIDNEXT from STATUS, so new mail cannot be detected from it. The
        // message / unseen / recent counts are the only signal, and a change in any of them
        // deep-scans this folder.
        bool countsChanged = false;
        if (account->isNetEase() && remoteStatus.uidNext() == 0) {
            auto changed = [&localStatus](const char * key, uint32_t value) {
                return !localStatus.count(key) || !localStatus[key].is_number() ||
                       localStatus[key].get<uint32_t>() != value;
            };
            countsChanged = changed(LS_MESSAGE_COUNT, remoteStatus.messageCount()) ||
                            changed(LS_UNSEEN_COUNT, remoteStatus.unseenCount()) ||
                            changed(LS_RECENT_COUNT, remoteStatus.recentCount());
        }
        
        // Step 1: Check folder UIDValidity
        if (localStatus.empty() || localStatus[LS_UIDVALIDITY].is_null()) {
            // We're about to fetch the top N UIDs in the folder and start working backwards in time.
            // When we eventually finish and start using CONDSTORE, this will be the highestmodseq
            // from the /oldest/ synced block of UIDs, ensuring we see changes.
            localStatus[LS_HIGHESTMODSEQ] = remoteStatus.highestModSeqValue();
            localStatus[LS_UIDVALIDITY] = remoteStatus.uidValidity();
            localStatus[LS_UIDVALIDITY_RESET_COUNT] = 0;
            localStatus[LS_UIDNEXT] = remoteStatus.uidNext();
            localStatus[LS_SYNCED_MIN_UID] = remoteStatus.uidNext();
            localStatus[LS_LAST_SHALLOW] = 0;
            localStatus[LS_LAST_DEEP] = 0;
            firstChunk = true;
        }
        
        // Step 1.5: Should we skip this folder?
        //
        // An \All mailbox (role "all") is a *duplicate view* of messages that also live
        // in Inbox / Sent / Archive - ProtonMail Bridge's "All Mail" is the common case.
        // Syncing the extra copy as a placement would double header traffic and rows, file
        // every thread under "Archive" (the [archive, all] role group), and a delete from
        // All Mail on Bridge is a delete everywhere. Skip the folder; it is still created so
        // the client can archive into it. (#137)
        //
        // Gmail is the one provider where All Mail is the *primary* message store rather
        // than a duplicate view: there we sync only all/spam/trash and derive the rest
        // from X-GM-LABELS, so it must keep syncing.
        //
        // Note: this used to be keyed on `containerFolder() != "" && path == "All Mail"`,
        // which only held for ProtonMail accounts that the client had tagged with a
        // container folder. Accounts that reach the engine without one (any @proton.me
        // or custom-domain Bridge account, since the client's provider table only lists
        // protonmail.com / protonmail.ch / pm.me) fell through and hit the bug.
        // The legacy `containerFolder` clause is kept so that a Bridge build which does
        // not advertise SPECIAL-USE (leaving "All Mail" without a role) keeps the exact
        // behaviour it has today.
        bool isDuplicateAllMail = (!isGmail && folder->role() == "all") ||
                                  (account->containerFolder() != "" && folder->path() == "All Mail");

        if (isDuplicateAllMail) {
            logger->info("SyncNow: skipped duplicate \\All folder {}", folder->path());
            markFolderStatusSynced(localStatus, remoteStatus);
            store->saveFolderStatus(folder.get(), initialLocalStatus);
            recordCoverage(*folder, true);
            continue;
        }

        if (localStatus[LS_UIDVALIDITY].get<uint32_t>() != remoteStatus.uidValidity()) {
            // The UIDs the server previously reported for this folder mean nothing now. Every
            // placement in the folder is set to UID 0 - still visible, no deltas -
            // and a full scan re-fetches the headers, hashes them to the same message ids and
            // assigns the new UIDs through the ordinary upsert. Only once the scan has covered
            // the whole folder can a row still at UID 0 be called gone; a truncated rebuild
            // leaves its tail at UID 0 for the initial-sync walk to finish, and the walk
            // deletes the leftovers when it reaches UID 1.
            //
            // syncedMinUID is reset to 1 (or to the rebuild's remainder). If the initial scan of
            // the folder had not finished, this can create a large number of messages at once.
            logger->warn("UIDInvalidity! Resetting placement UIDs in {}, rebuilding index. This may take a moment...", folder->path());
            store->resetPlacementUIDs(*folder);
            auto rebuilt = syncFolderUIDRange(*folder, RangeMake(1, UINT64_MAX), false);
            if (!rebuilt.truncated) {
                processor->deleteUnassignedPlacements(*folder);
            }

            if (localStatus.count(LS_UIDVALIDITY_RESET_COUNT) == 0) {
                localStatus[LS_UIDVALIDITY_RESET_COUNT] = 1;
            }
            localStatus[LS_UIDVALIDITY_RESET_COUNT] = localStatus[LS_UIDVALIDITY_RESET_COUNT].get<uint32_t>() + 1;
            localStatus[LS_HIGHESTMODSEQ] = remoteStatus.highestModSeqValue();
            localStatus[LS_UIDVALIDITY] = remoteStatus.uidValidity();
            localStatus[LS_UIDNEXT] = remoteStatus.uidNext();
            // If the rebuild couldn't re-map every message in one pass, leave syncedMinUID above the
            // remainder so the initial-sync loop walks back down and picks them up.
            localStatus[LS_SYNCED_MIN_UID] = rebuilt.truncated ? rebuilt.syncedMinUID : 1;
            localStatus[LS_LAST_SHALLOW] = time(0);
            localStatus[LS_LAST_DEEP] = time(0);
            localStatus[LS_MESSAGE_COUNT] = remoteStatus.messageCount();
            localStatus[LS_UNSEEN_COUNT] = remoteStatus.unseenCount();
            localStatus[LS_RECENT_COUNT] = remoteStatus.recentCount();
            // This branch skips the bookkeeping at the end of the loop body, which is where `busy`
            // (set for every folder at the start of the pass) is normally cleared. If the rebuild
            // left a remainder we have to ask for another iteration here or the remaining messages
            // drain at one chunk per sleep interval while the folder sits unlinked and half empty.
            localStatus[LS_BUSY] = rebuilt.truncated;
            if (rebuilt.truncated) {
                syncAgainImmediately = true;
            }

            store->saveFolderStatus(folder.get(), initialLocalStatus);
            recordCoverage(*folder, !rebuilt.truncated);
            continue;
        }
        
        // Step 2: Initial sync. Until we reach UID 1, we grab chunks of messages
        uint32_t syncedMinUID = localStatus[LS_SYNCED_MIN_UID].get<uint32_t>();
        // Note: the chunk is sized to MAX_FULL_HEADERS_REQUEST_SIZE so it resolves in a single
        // full-headers request. A larger chunk is no longer incorrect - syncFolderUIDRange reports
        // how far down it got and we only record that much - but it costs an extra attributes-only
        // fetch across the whole chunk for every MAX_FULL_HEADERS_REQUEST_SIZE messages ingested.
        uint32_t chunkSize = firstChunk ? 750 : MAX_FULL_HEADERS_REQUEST_SIZE;

        uint32_t walkStartUID = syncedMinUID;
        if (syncedMinUID > 1) {
            // The UID value space is sparse, meaning there can be huge gaps where there are no
            // messages. If the folder indicates UIDNext is 100000 but there are only 100 messages,
            // go ahead and fetch them all in one chunk. Otherwise, scan the UID space in chunks,
            // ensuring we never bite off more than we can chew.
            uint32_t chunkMinUID = syncedMinUID > chunkSize ? syncedMinUID - chunkSize : 1;
            if (remoteStatus.messageCount() < MAX_SINGLE_RANGE_SWEEP_SIZE) {
                // Note: this can hand syncFolderUIDRange a range holding more messages than one
                // full-headers request covers. That is safe now - it reports back how far down it
                // actually got and we only record that much below.
                chunkMinUID = 1;
            }
            auto chunk = syncFolderUIDRange(*folder, RangeMake(chunkMinUID, syncedMinUID - chunkMinUID), true);

            // Only record the part of the chunk we actually ingested. If the folder held more
            // messages in this UID range than we were willing to fetch at once, the remainder is
            // picked up on the next iteration - advancing past them would drop them permanently,
            // because CONDSTORE/QRESYNC only reports messages changed since our highestmodseq.
            if (chunk.truncated && chunk.syncedMinUID > chunkMinUID && chunk.syncedMinUID < syncedMinUID) {
                chunkMinUID = chunk.syncedMinUID;
            }
            localStatus[LS_SYNCED_MIN_UID] = chunkMinUID;
            syncedMinUID = chunkMinUID;

            if (chunkMinUID <= 1) {
                // The walk just covered the whole folder, so record it as a completed deep pass.
                // Without this a brand new account immediately follows initial sync with a
                // redundant full-folder scan, because lastDeep is still 0 on the CONDSTORE branch.
                localStatus[LS_LAST_DEEP] = time(0);
                // Every UID has now been fetched once, so a placement still at UID 0 (the tail
                // of a truncated UIDVALIDITY rebuild) is a copy the server no longer has.
                processor->deleteUnassignedPlacements(*folder);
            }
        }

        // A folder still walking down towards UID 1 has copies this pass has not recorded,
        // so the sweep cannot tell a copy that moved into it from one the server dropped.
        // Same reasoning as the truncation guards below, for the other way a pass can end
        // without having seen a folder's whole range.
        covered = (syncedMinUID <= 1);

        // Step 3: A) Retrieve new messages  B) update existing messages  C) delete missing messages
        // CONDSTORE, when available, does A + B.
        // XYZRESYNC, when available, does C
        if (hasCondstore && hasQResync) {
            // Hooray! We never need to fetch the entire range to sync. Just look at
            // highestmodseq / uidnext and sync if we need to.
            syncFolderChangesViaCondstore(*folder, remoteStatus, true);

            // ...with one caveat: CONDSTORE only reports messages whose modseq changed since ours,
            // so a message we failed to ingest during the initial sync is never mentioned again and
            // stays missing forever. Once the initial sync has reached UID 1, run a cheap
            // attributes-only pass over the whole folder occasionally to notice and repair any gaps.
            time_t lastDeep = localStatus.count(LS_LAST_DEEP) && localStatus[LS_LAST_DEEP].is_number()
                                ? localStatus[LS_LAST_DEEP].get<time_t>() : 0;
            if ((syncedMinUID <= 1) && (iterationsSinceLaunch > 0) &&
                (time(0) - lastDeep > CONDSTORE_GAP_SCAN_INTERVAL)) {
                // Note: a failure here must not abort the whole folder loop. This scan is a
                // consistency check on top of an otherwise complete CONDSTORE sync, and letting it
                // throw would skip every folder after this one and retry forever.
                try {
                    auto deep = syncFolderUIDRange(*folder, RangeMake(1, UINT64_MAX), false);
                    covered = covered && !deep.truncated;
                    // If there were more gaps than one pass can fill, leave lastDeep alone so we
                    // come back immediately rather than in another day - but only while the
                    // backlog is actually shrinking.
                    if (shouldRetryTruncatedScan(*folder, deep)) {
                        deepScanIncomplete = true;
                    } else {
                        localStatus[LS_LAST_DEEP] = time(0);
                    }
                } catch (SyncException & ex) {
                    logger->warn("- {}: gap scan failed ({}), will retry after the normal interval.",
                                 folder->path(), ex.toJSON().dump());
                    localStatus[LS_LAST_DEEP] = time(0);
                    covered = false;
                }
            }
        } else {
            uint32_t remoteUidnext = remoteStatus.uidNext();
            uint32_t localUidnext = localStatus[LS_UIDNEXT].get<uint32_t>();
            bool newMessages = remoteUidnext > localUidnext;
            bool timeForDeepScan = countsChanged ||
                                   ((iterationsSinceLaunch > 0) &&
                                    (time(0) - localStatus[LS_LAST_DEEP].get<time_t>() > DEEP_SCAN_INTERVAL));
            bool timeForShallowScan = !timeForDeepScan && (time(0) - localStatus[LS_LAST_SHALLOW].get<time_t>() > SHALLOW_SCAN_INTERVAL);

            // Okay. If there are new messages in the folder (UIDnext has increased), do a heavy fetch of
            // those /AND/ get the bodies. This ensures people see both very quickly, which is important.
            //
            // This could potentially grab zillions of messages, in which case syncFolderUIDRange will
            // bail out and the next "deep" scan will pick up the ones we skipped.
            //
            if (newMessages) {
                vector<SyncedMessage> synced{};
                auto fetched = syncFolderUIDRange(*folder, RangeMake(localUidnext, remoteUidnext - localUidnext), true, &synced);
                // A truncated fetch leaves uidnext where it is so the next pass comes back for
                // the remainder; otherwise the same range would be re-fetched on every pass
                // until the next shallow scan, and on a server without QRESYNC that repeat FETCH
                // is what resurrects a copy the foreground connection has just moved out.
                if (!fetched.truncated) {
                    localStatus[LS_UIDNEXT] = remoteUidnext;
                } else {
                    covered = false;
                }
                
                if ((folder->role() == "inbox") || (folder->role() == "all")) {
                    // download the newest (highest UID) bodies first
                    std::sort(synced.begin(), synced.end(), [](const SyncedMessage & a, const SyncedMessage & b) {
                        return a.uid > b.uid;
                    });
                    int count = 0;
                    for (auto & entry : synced) {
                        if (!entry.message->isInInbox(store)) {
                            continue; // skip "all mail" that is not in inbox
                        }
                        syncMessageBody(entry.message.get(), folder.get());
                        if (count++ > 30) { break; }
                    }
                }
            }
            
            if (timeForShallowScan) {
                // note: we use local uidnext here, because we just fetched everything between
                // localUIDNext and remoteUIDNext so fetching that section again would just slow us down.
                uint32_t bottomUID = store->fetchMessageUIDAtDepth(*folder, 399, localUidnext);
                if (bottomUID < syncedMinUID) {
                    bottomUID = syncedMinUID;
                }
                // Guard against underflow if remoteUidnext <= bottomUID (server inconsistency)
                if (remoteUidnext > bottomUID) {
                    auto shallow = syncFolderUIDRange(*folder, RangeMake(bottomUID, remoteUidnext - bottomUID), false);
                    covered = covered && !shallow.truncated;
                }
                localStatus[LS_LAST_SHALLOW] = time(0);
                localStatus[LS_UIDNEXT] = remoteUidnext;
            }
            
            if (timeForDeepScan) {
                auto deep = syncFolderUIDRange(*folder, RangeMake(syncedMinUID, UINT64_MAX), false);
                covered = covered && !deep.truncated;
                if (syncedMinUID == 0) {
                    syncedMinUID = 1;
                    localStatus[LS_SYNCED_MIN_UID] = 1;
                }
                localStatus[LS_LAST_SHALLOW] = time(0);
                localStatus[LS_UIDNEXT] = remoteUidnext;
                // Only mark the deep scan done if it got through the backlog. Otherwise keep
                // scanning on subsequent iterations instead of waiting out DEEP_SCAN_INTERVAL
                // between every MAX_FULL_HEADERS_REQUEST_SIZE messages.
                if (shouldRetryTruncatedScan(*folder, deep)) {
                    deepScanIncomplete = true;
                } else {
                    localStatus[LS_LAST_DEEP] = time(0);
                }
            }
        }

        localStatus[LS_MESSAGE_COUNT] = remoteStatus.messageCount();
        localStatus[LS_UNSEEN_COUNT] = remoteStatus.unseenCount();
        localStatus[LS_RECENT_COUNT] = remoteStatus.recentCount();
        
        bool moreToDo = false;

        // Retrieve some message bodies. We do this concurrently with the full header
        // scan so the user sees snippets on some messages quickly.
        if (syncMessageBodies(*folder, remoteStatus)) {
            moreToDo = true;
        }
        if (syncedMinUID > 1) {
            moreToDo = true;
        }
        if (deepScanIncomplete) {
            moreToDo = true;
        }
        
        // Update cache metrics and cleanup bodies we don't want anymore.
        // these queries are expensive so we do this infrequently and increment
        // blindly as we download bodies.
        time_t lastCleanup = localStatus.count(LS_LAST_CLEANUP) ? localStatus[LS_LAST_CLEANUP].get<time_t>() : 0;
        // Note: <= 1, not == 1. syncedMinUID is seeded from UIDNEXT, which some servers report as
        // 0, and only the non-CONDSTORE deep scan normalises 0 to 1 - so on a CONDSTORE folder it
        // can sit at 0 forever, and == 1 would mean the body cache is never trimmed and
        // LS_BODIES_WANTED (the UI's progress denominator) is never written.
        if (syncedMinUID <= 1 && (time(0) - lastCleanup > CACHE_CLEANUP_INTERVAL)) {
            cleanMessageCache(*folder);
            localStatus[LS_LAST_CLEANUP] = time(0);
        }

        // Save a general flag that indicates whether we're still doing stuff
        // like syncing message bodies. Set to true below.
        localStatus[LS_BUSY] = moreToDo;
        syncAgainImmediately = syncAgainImmediately || moreToDo;

        // Save the folder - note that helper methods above mutated localStatus.
        // Avoid the save if we can, because this creates a lot of noise in the client.
        store->saveFolderStatus(folder.get(), initialLocalStatus);
        recordCoverage(*folder, covered, syncedMinUID < walkStartUID);
    }
    
    // If a copy of an orphan reappeared in a scanned folder, the upsert cleared its record;
    // remove the rest that are old enough. Without the per-folder bound, a bulk move of more
    // messages than one fetch carries would have its remainder removed here and re-created,
    // without metadata, once the destination catches up.
    processor->sweepExpiredOrphans(sweepBefore, passStartedAt);
    
    logger->info("Sync loop complete.");
    iterationsSinceLaunch += 1;

    return syncAgainImmediately;
}

void SyncWorker::ensureRootMailspringFolder(vector<string> containerFolderComponents, Array * remoteFolders)
{
    auto components = Array::array();
    for (string containerFolderComponent : containerFolderComponents) {
      components->addObject(AS_MCSTR(containerFolderComponent));
    }
    
    String * desiredPath = session.defaultNamespace()->pathForComponents(components);
    
    bool exists = false;
    for (int ii = ((int)remoteFolders->count()) - 1; ii >= 0; ii--) {
        IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
        if (remote->path()->isEqual(desiredPath)) {
            exists = true;
        }
    }
    
    if (!exists) {
        ErrorCode err = ErrorCode::ErrorNone;
        session.createFolder(desiredPath, &err);
        if (err) {
            logger->error("Could not create Mailspring container folder: {}. {}", desiredPath->UTF8Characters(), ErrorCodeToTypeMap[err]);
        } else {
            logger->error("Created Mailspring container folder: {}.", desiredPath->UTF8Characters());
        }
    }
}

// Some servers, IMAP gateways especially (DavMail, Proton Bridge, Zoho), list a mailbox twice:
// a repeated LIST line, or two names MailCore collapses when it normalizes "Inbox" to "INBOX".
// Both hash to one folder ID, and the second insert aborts mailsync with "UNIQUE constraint
// failed: Folder.id" before any folder is saved - on every launch. Keeps the first copy, merging
// the others' flags so role detection doesn't depend on the order the server used. Runs after
// unselectable folders are dropped, so the merge can't reintroduce NoSelect.
void SyncWorker::removeDuplicateFolders(Array * remoteFolders)
{
    map<string, IMAPFolder *> remotesById {};

    for (int ii = 0; ii < (int)remoteFolders->count(); ii++) {
        IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
        string remoteId = MailUtils::idForFolder(account->id(), string(remote->path()->UTF8Characters()));
        auto existing = remotesById.find(remoteId);
        if (existing == remotesById.end()) {
            remotesById[remoteId] = remote;
            continue;
        }
        logger->warn("-X the server listed {} more than once - ignoring the duplicate.", remote->path()->UTF8Characters());
        existing->second->setFlags((IMAPFolderFlag)(existing->second->flags() | remote->flags()));
        remoteFolders->removeObjectAtIndex(ii);
        ii -= 1;
    }
}

vector<shared_ptr<Folder>> SyncWorker::syncFoldersAndLabels()
{
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;

    string containerFolderPath = account->containerFolder();
    vector<string> containerFolderComponents;

    if (containerFolderPath == "" || containerFolderPath == MAILSPRING_FOLDER_PREFIX_V2) {
      logger->info("Syncing folder list...");
      containerFolderComponents.push_back(MAILSPRING_FOLDER_PREFIX_V2);
    } else {
      logger->info("Syncing folder list on custom container folder {} ...", containerFolderPath);

      std::stringstream data(containerFolderPath);
      std::string folder;
      while(std::getline(data, folder, '/'))
      {
        containerFolderComponents.push_back(folder);
      }
    }

    ErrorCode err = ErrorCode::ErrorNone;
    Array * remoteFolders = session.fetchAllFolders(&err);
    if (err) {
        throw SyncException(err, "syncFoldersAndLabels - fetchAllFolders");
    }

    string mainPrefix = MailUtils::namespacePrefixOrBlank(&session);
    bool ensuredRoot = false;
    
    // create required Mailspring folders if they don't exist
    // TODO: Consolidate this into role association code below, and make it
    // use the same business logic as creating / updating folders from tasks.
    // Accounts with create_helper_folders=false (e.g. O365 shared mailboxes, where
    // any folder we create is visible to every member of the mailbox) are skipped;
    // features depending on these folders (snooze) are unavailable there.
    vector<string> mailspringFolders{};
    if (account->createHelperFolders()) {
        mailspringFolders.push_back("Snoozed");
    }

    for (string mailspringFolder : mailspringFolders) {
        string mailspringRole = mailspringFolder;
        transform(mailspringRole.begin(), mailspringRole.end(), mailspringRole.begin(), ::tolower);

        bool exists = false;
        for (int ii = ((int)remoteFolders->count()) - 1; ii >= 0; ii--) {
            IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
            string remoteRole = MailUtils::roleForFolder(containerFolderPath, mainPrefix, remote);
            if (remoteRole == mailspringRole) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            if (!ensuredRoot) {
                ensureRootMailspringFolder(containerFolderComponents, remoteFolders);
                ensuredRoot = true;
            }
            
            auto components = Array::array();
            for (string containerFolderComponent : containerFolderComponents) {
              components->addObject(AS_MCSTR(containerFolderComponent));
            }
            components->addObject(AS_MCSTR(mailspringFolder));
            String * desiredPath = session.defaultNamespace()->pathForComponents(components);
            session.createFolder(desiredPath, &err);
            if (err) {
                logger->error("Could not create required Mailspring folder: {}. {}", desiredPath->UTF8Characters(), ErrorCodeToTypeMap[err]);
                continue;
            }
            logger->error("Created required Mailspring folder: {}.", desiredPath->UTF8Characters());
            IMAPFolder * fake = new IMAPFolder();
            fake->autorelease();
            fake->setPath(desiredPath);
            fake->setDelimiter(session.defaultNamespace()->mainDelimiter());
            remoteFolders->addObject(fake);
        }
    }
    
    // sync with the local store
    vector<shared_ptr<Folder>> foldersToSync{};

    {
        // Phase 1: Read local state and compute diff OUTSIDE the transaction
        // to minimize write-lock hold time on the shared SQLite database.
        Query q = Query().equal("accountId", account->id());
        bool isGmail = session.storedCapabilities()->containsIndex(IMAPCapabilityGmail);
        auto unusedLocalFolders = store->findAllMap<Folder>(q, "id");
        auto unusedLocalLabels = store->findAllMap<Label>(q, "id");
        map<string, shared_ptr<Folder>> allFoundCategories {};
        set<string> labelIds {}; // track which IDs are labels vs folders

        // New folders/labels to INSERT (version == 0, no stale data risk)
        vector<shared_ptr<Folder>> toCreate {};
        // Role assignments for existing folders (reload fresh inside transaction)
        struct RoleUpdate { string id; string role; };
        vector<RoleUpdate> roleUpdates {};

        // Eliminate unselectable folders
        for (int ii = ((int)remoteFolders->count()) - 1; ii >= 0; ii--) {
            IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
            if (remote->flags() & IMAPFolderFlagNoSelect) {
                remoteFolders->removeObjectAtIndex(ii);
                continue;
            }
        }

        // Eliminate folders the server listed more than once
        removeDuplicateFolders(remoteFolders);

        // Find / create local folders and labels to match the remote ones
        // Note: We don't assign roles, just create the objects here.
        for (unsigned int ii = 0; ii < remoteFolders->count(); ii++) {
            IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
            string remoteId = MailUtils::idForFolder(account->id(), string(remote->path()->UTF8Characters()));
            string remotePath = remote->path()->UTF8Characters();

            bool isLabel = false;
            if (isGmail) {
                IMAPFolderFlag remoteFlags = remote->flags();
                isLabel = !(remoteFlags & IMAPFolderFlagAll) && !(remoteFlags & IMAPFolderFlagSpam) && !(remoteFlags & IMAPFolderFlagTrash);
            }

            shared_ptr<Folder> local;

            if (isLabel) {
                labelIds.insert(remoteId);
                // Treat as a label
                if (unusedLocalLabels.count(remoteId) > 0) {
                    local = unusedLocalLabels[remoteId];
                    unusedLocalLabels.erase(remoteId);
                } else {
                    local = make_shared<Label>(remoteId, account->id(), 0);
                    local->setPath(remotePath);
                    toCreate.push_back(local);
                }

            } else {
                // Treat as a folder
                if (unusedLocalFolders.count(remoteId) > 0) {
                    local = unusedLocalFolders[remoteId];
                    unusedLocalFolders.erase(remoteId);
                } else {
                    local = make_shared<Folder>(remoteId, account->id(), 0);
                    local->setPath(remotePath);
                    toCreate.push_back(local);
                }
                foldersToSync.push_back(local);
            }

            allFoundCategories[remoteId] = local;
        }

        for (auto role : MailUtils::roles()) {
            bool found = false;

            // If the role is already assigned, skip
            for (auto it : allFoundCategories) {
                if (it.second->role() == role) {
                    found = true;
                    break;
                }
            }
            if (found) {
                continue;
            }

            // find a folder that matches the flags
            for (unsigned int ii = 0; ii < remoteFolders->count(); ii++) {
                IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
                string cr = MailUtils::roleForFolderViaFlags(mainPrefix, remote);
                if (cr != role) {
                    continue;
                }
                string remoteId = MailUtils::idForFolder(account->id(), string(remote->path()->UTF8Characters()));
                if (!allFoundCategories.count(remoteId)) {
                    logger->warn("-X found folder for role, couldn't find local object for {}", role);
                    continue;
                }
                auto & cat = allFoundCategories[remoteId];
                cat->setRole(role);
                // If this is a newly created item, role is already set on the in-memory object.
                // For existing items, record the update to reload fresh inside the transaction.
                if (cat->version() > 0) {
                    roleUpdates.push_back({remoteId, role});
                }
                found = true;
                break;
            }

            if (found) {
                continue;
            }

            // find a folder that matches the name
            for (unsigned int ii = 0; ii < remoteFolders->count(); ii++) {
                IMAPFolder * remote = (IMAPFolder *)remoteFolders->objectAtIndex(ii);
                string cr = MailUtils::roleForFolderViaPath(containerFolderPath, mainPrefix, remote);
                if (cr != role) {
                    continue;
                }
                string remoteId = MailUtils::idForFolder(account->id(), string(remote->path()->UTF8Characters()));
                if (!allFoundCategories.count(remoteId)) {
                    logger->warn("-X found folder for role, couldn't find local object for {}", role);
                    continue;
                }
                auto & cat = allFoundCategories[remoteId];
                cat->setRole(role);
                if (cat->version() > 0) {
                    roleUpdates.push_back({remoteId, role});
                }
                found = true;
                break;
            }
        }

        // Detach the copies in folders the server no longer has before their rows go, in
        // bounded transactions, so a quit before the removal below leaves every message's
        // snapshot matching its rows.
        for (auto const & item : unusedLocalFolders) {
            processor->detachMessagesFromFolder(item.first);
        }
        for (auto const & item : unusedLocalLabels) {
            processor->detachMessagesFromFolder(item.first);
        }

        // Phase 2: SHORT transaction with only writes.
        // Existing items are reloaded fresh to avoid overwriting concurrent changes.
        MailStoreTransaction transaction{store, "syncFoldersAndLabels"};

        for (auto & item : toCreate) {
            // Never let a folder that already exists take the process down. De-duplication
            // should make this unreachable, but the row is present either way and the next
            // pass picks it up - the insert-then-recover shape MailProcessor uses.
            try {
                store->save(item.get());
            } catch (SQLite::Exception & ex) {
                if (ex.getErrorCode() != 19) { // constraint failed
                    throw;
                }
                logger->error("-X could not create {} {}: {}. Continuing.", item->tableName(), item->path(), ex.what());
            }
        }
        for (auto & ru : roleUpdates) {
            shared_ptr<Folder> fresh;
            if (labelIds.count(ru.id)) {
                fresh = store->find<Label>(Query().equal("id", ru.id));
            } else {
                fresh = store->find<Folder>(Query().equal("id", ru.id));
            }
            if (fresh) {
                fresh->setRole(ru.role);
                store->save(fresh.get());
            }
        }
        // A copy the foreground worker recorded in a removed folder after the detach above
        // is deleted with the folder (deletePlacementsForFolder records the orphans) and its
        // message caught up once this commits.
        vector<string> placedSinceDetach {};
        auto removeCategory = [&](Folder * category) {
            // remove() only needs the id and tableName for DELETE — safe to use stale objects
            store->remove(category);
            for (auto & id : store->deletePlacementsForFolder(category->id())) {
                placedSinceDetach.push_back(id);
            }
        };
        for (auto const & item : unusedLocalFolders) {
            removeCategory(item.second.get());
        }
        for (auto const & item : unusedLocalLabels) {
            removeCategory(item.second.get());
        }
        transaction.commit();

        processor->refreshMessages(placedSinceDetach, UnplacedMessages::Remove, "syncFoldersAndLabels");
    }

    return foldersToSync;
}

// A truncated full-folder scan normally means "we ingested a batch, come straight back for the
// rest". But some messages can never be ingested - MailProcessor::updateMessage returns without
// applying anything while the local row is newer than the scan, which lasts as long as the
// user's task on it is in flight - so they are reported as needing full headers on every pass. Without a
// progress check that turns into an unthrottled loop (the background worker runs syncNow in a
// `while (moreToSync)` with no sleep) that also starves other folders. Only come straight back if
// the backlog actually shrank; otherwise treat the scan as done so we wait out the normal interval.
bool SyncWorker::shouldRetryTruncatedScan(Folder & folder, UIDRangeSyncResult const & scan)
{
    if (!scan.truncated) {
        // The scan covered the whole folder, so this episode is over. Forget the count: a future
        // gap has to be judged on its own, and a leftover value would make a new, larger backlog
        // look like it wasn't draining and cost it an interval before the first retry.
        lastTruncatedScanNeeded.erase(folder.id());
        return false;
    }

    auto it = lastTruncatedScanNeeded.find(folder.id());
    bool draining = (it == lastTruncatedScanNeeded.end()) || (scan.needed < it->second);
    // Note: keep the count on the back-off path - it is what bounds the retry loop.
    lastTruncatedScanNeeded[folder.id()] = scan.needed;
    if (!draining) {
        logger->warn("- {}: {} messages still need full headers after a full pass and the count is "
                     "not falling. Backing off instead of rescanning immediately.",
                     folder.path(), scan.needed);
    }
    return draining;
}

SyncWorker::UIDRangeSyncResult SyncWorker::syncFolderUIDRange(Folder & folder, Range range, bool heavyInitialRequest, vector<SyncedMessage> * syncedMessages)
{
    std::string remotePath = folder.path();
    
    // Safety check: "0" is not a valid start and causes the server to return only the last item
    if (range.location == 0) {
        range.location = 1;
    }
    // Safety check: force an attributes-only sync of the range if the requested UID range is so
    // large the query might never complete if we ask for it all. We might still need to fetch all the
    // bodies, but we'll cap the number we fetch.
    if (range.length > MAX_FULL_HEADERS_REQUEST_SIZE) {
        heavyInitialRequest = false;
    }

    // Unless we truncate below, the caller can consider the whole requested range synced.
    UIDRangeSyncResult result;
    result.syncedMinUID = (uint32_t)range.location;

    // Note: an open-ended range is requested as `location:*`, and location + length would wrap.
    string rangeDesc = (range.length == UINT64_MAX)
        ? std::to_string(range.location) + " - *"
        : std::to_string(range.location) + " - " + std::to_string(range.location + range.length);
    logger->info("syncFolderUIDRange for {}, UIDs: {}, Heavy: {}", remotePath, rangeDesc, heavyInitialRequest);

    AutoreleasePool pool;
    IndexSet * set = IndexSet::indexSetWithRange(range);
    IMAPProgress cb;
    ErrorCode err(ErrorCode::ErrorNone);
    String path(AS_MCSTR(remotePath));
    vector<uint32_t> heavyNeededUIDs {};

    // A FETCH on the folder this connection already has selected is answered from its stale
    // view, with the EXPUNGE lines following the data, so a copy the foreground connection
    // just moved out would be re-added as a live placement. Without QRESYNC no VANISHED
    // arrives to correct that; with it the trailing VANISHED is harvested and applied.
    // A folder that is not the selected one is brought up to date by the SELECT the fetch issues.
    if (!session.isQResyncEnabled()) {
        String * selected = session.currentFolder();
        if (selected != NULL && selected->caseInsensitiveCompare(&path) == 0) {
            noopSelectedFolder();
        }
    }
    
    // Step 1: Fetch the local attributes (unread, starred, etc.) of the live placements in
    // the range. Note: we do this first because the remote fetch may take a long time, and
    // if the data that comes back is already stale, we want to calculate changes (deletes,
    // especially) based on old <> old, not new <> old, since new, freshly downloaded messages
    // will always be missing in the stale server set and will be marked for deletion.
    // Re-downloading is better.
    map<uint32_t, MessageAttributes> local(store->fetchMessagesAttributesInRange(range, folder));

    // Step 2: Fetch the remote attributes (unread, starred, etc.) for the same UID range
    time_t syncDataTimestamp = time(0);
    auto kind = MailUtils::messagesRequestKindFor(session.storedCapabilities(), heavyInitialRequest);
    Array * remote = session.fetchMessagesByUID(&path, kind, set, &cb, &err);
    if (err) {
        throw SyncException(err, "syncFolderUIDRange - fetchMessagesByUID");
    }

    // RFC 9738 §3: a server advertising MESSAGELIMIT (Yahoo) may answer a FETCH over more
    // messages than the limit for only the highest-UID ones, flagged by [MESSAGELIMIT] on the
    // tagged OK. Copies below the lowest UID returned were not looked at, so they are not gone.
    uint32_t scannedMinUID = 0;
    if (session.lastResponseHitMessageLimit() && remote->count() > 0) {
        scannedMinUID = UINT32_MAX;
        for (unsigned int ii = 0; ii < remote->count(); ii++) {
            scannedMinUID = std::min(scannedMinUID, ((IMAPMessage *)remote->objectAtIndex(ii))->uid());
        }
        result.truncated = true;
        result.syncedMinUID = std::max(result.syncedMinUID, scannedMinUID);
        logger->warn("- {}: server applied MESSAGELIMIT, only UIDs {} and up were scanned", remotePath, scannedMinUID);
    }

    clock_t lastSleepClock = clock();

    logger->info("- {}: remote={}, local={}, folderId={}", remotePath, remote->count(), local.size(), folder.id());

    for (int ii = ((int)remote->count()) - 1; ii >= 0; ii--) {
        // Never sit in a hard loop inserting things into the database for more than 250ms.
        // This ensures we don't starve another thread waiting for a database connection
        if (((clock() - lastSleepClock) * 4) / CLOCKS_PER_SEC > 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            lastSleepClock = clock();
        }
        
        IMAPMessage * remoteMsg = (IMAPMessage *)(remote->objectAtIndex(ii));
        uint32_t remoteUID = remoteMsg->uid();

        // Step 3: Collect messages that are different or not in our local UID set.
        bool inFolder = (local.count(remoteUID) > 0);
        bool same = inFolder && MessageAttributesMatch(local[remoteUID], MessageAttributesForMessage(remoteMsg));

        // With headers in hand, a row that names another message is wrong however well its
        // flags match (a mis-paired COPYUID, see _resolveNewUIDs). Ingesting the UID gives it
        // to the message it holds; the ingest of the displaced message's own UID relinks it.
        if (same && heavyInitialRequest) {
            string remoteId = MailUtils::idForMessage(folder.accountId(), remotePath, remoteMsg);
            if (remoteId != local[remoteUID].messageId) {
                logger->warn("- {} UID {} holds message {}, recorded as {}", remotePath, remoteUID, remoteId, local[remoteUID].messageId);
                same = false;
            }
        }

        if (!inFolder || !same) {
            // Step 4: Attempt to insert the new message. If we get unique exceptions,
            // look for the existing message and do an update instead. This happens whenever
            // a message has moved between folders or it's attributes have changed.
            
            // Note: We could prefetch all changedOrMissingIDs and then decide to update/insert,
            // but we can only query for 500 at a time, it /feels/ nasty, and we /could/ always
            // hit the exception anyway since another thread could be IDLEing and retrieving
            // the messages alongside us.
            if (heavyInitialRequest) {
                auto local = processor->insertFallbackToUpdateMessage(remoteMsg, folder, syncDataTimestamp);
                if (syncedMessages != nullptr) {
                    syncedMessages->push_back({local, remoteUID});
                }
            } else {
                // Note: we collect every UID that needs full headers and decide which ones to
                // request below. Truncating here would depend on the order the server returned
                // messages in, which is not guaranteed to be sorted by UID.
                heavyNeededUIDs.push_back(remoteUID);
            }
        }
        
        local.erase(remoteUID);
    }
    
    if (!heavyInitialRequest && heavyNeededUIDs.size() > 0) {
        // Note: heavyNeededUIDs could be enormous if the user added a zillion items to a folder, if it's
        // been years since the app was launched, or if a sync bug caused us to delete messages we
        // shouldn't have. (eg the issue with uidnext becoming zero suddenly)
        //
        // We don't re-fetch them all in one request because it could be an impossibly large amount of data.
        // Instead we sync the newest MAX_FULL_HEADERS_REQUEST_SIZE and report the range as only synced
        // down to the lowest UID we took, so the caller comes back for the rest.
        //
        size_t heavyNeededIdeal = heavyNeededUIDs.size();
        result.needed = heavyNeededIdeal;
        std::sort(heavyNeededUIDs.begin(), heavyNeededUIDs.end());
        // Note: a request over MESSAGELIMIT would be answered only in part, see above.
        size_t heavyRequestSize = MAX_FULL_HEADERS_REQUEST_SIZE;
        uint32_t messageLimit = session.messageLimit();
        if (messageLimit > 0 && messageLimit < heavyRequestSize) {
            heavyRequestSize = messageLimit;
        }
        if (heavyNeededUIDs.size() > heavyRequestSize) {
            // Keep the highest (newest) UIDs, which keeps the un-synced remainder a contiguous
            // block at the bottom of the range that `result.syncedMinUID` can describe.
            heavyNeededUIDs.erase(heavyNeededUIDs.begin(), heavyNeededUIDs.end() - heavyRequestSize);
            result.truncated = true;
            result.syncedMinUID = heavyNeededUIDs.front();
        }

        logger->info("- Fetching full headers for {} (of {} needed)", heavyNeededUIDs.size(), heavyNeededIdeal);

        IndexSet * heavyNeeded = IndexSet::indexSet();
        for (uint32_t uid : heavyNeededUIDs) {
            heavyNeeded->addIndex(uid);
        }

        syncDataTimestamp = time(0);
        auto kind = MailUtils::messagesRequestKindFor(session.storedCapabilities(), true);
        remote = session.fetchMessagesByUID(&path, kind, heavyNeeded, &cb, &err);
        if (err != ErrorNone) {
            throw SyncException(err, "syncFolderUIDRange - fetchMessagesByUID (heavy)");
        }
        for (int ii = ((int)remote->count()) - 1; ii >= 0; ii--) {
            IMAPMessage * remoteMsg = (IMAPMessage *)(remote->objectAtIndex(ii));
            auto local = processor->insertFallbackToUpdateMessage(remoteMsg, folder, syncDataTimestamp);
            if (syncedMessages != nullptr) {
                syncedMessages->push_back({local, remoteMsg->uid()});
            }
            remote->removeLastObject();
        }
    }

    // Step 5: Delete. The UIDs left in the local map are copies we had in the range which
    // the server no longer reports. The end-of-pass sweep removes messages left with no copy
    // that do not turn up in another folder first.
    if (local.size() > 0) {
        vector<uint32_t> deletedUIDs {};
        for (auto const &ent : local) {
            if (ent.first >= scannedMinUID) {
                deletedUIDs.push_back(ent.first);
            }
        }
        for (vector<uint32_t> chunk : MailUtils::chunksOfVector(deletedUIDs, 200)) {
            processor->deleteVanishedPlacements(folder, chunk);
        }
    }

    return result;
}

/* Dovecot answers STATUS and FETCH for the mailbox a connection has SELECTed from that
   connection's own view of it, and only a NOOP, IDLE or another SELECT delivers the untagged
   EXISTS / EXPUNGE / VANISHED that bring the view up to date; the FETCH data even precedes the
   EXPUNGE lines in the same response (RFC 3501 §6.1.2; test/README.md "Things learned from
   Dovecot", conformance `probe_stale_fetch`). Callers send this before trusting a STATUS or
   FETCH of the selected folder. */
void SyncWorker::noopSelectedFolder()
{
    ErrorCode err = ErrorNone;
    session.noop(&err);
    if (err != ErrorNone) {
        throw SyncException(err, "noop");
    }
}

void SyncWorker::syncFolderChangesViaCondstore(Folder & folder, IMAPFolderStatus & remoteStatus, bool mustSyncAll)
{
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;

    uint32_t uidnext = folder.localStatus()[LS_UIDNEXT].get<uint32_t>();
    uint64_t modseq = folder.localStatus()[LS_HIGHESTMODSEQ].get<uint64_t>();
    uint64_t remoteModseq = remoteStatus.highestModSeqValue();
    uint32_t remoteUIDNext = remoteStatus.uidNext();
    time_t syncDataTimestamp = time(0);
    
    logger->info("syncFolderChangesViaCondstore - {}: modseq {} to {}, uidnext {} to {}",
                 folder.path(), modseq, remoteModseq, uidnext, remoteUIDNext);

    String path(AS_MCSTR(folder.path()));

    // Must happen before the early return below, and before LS_HIGHESTMODSEQ moves.
    deleteVanishedUIDs(folder, session.takeVanishedMessages(&path), "this connection");

    if (modseq == remoteModseq && uidnext == remoteUIDNext) {
        return;
    }

    // A folder that has never held a message has nothing to report, and `1:0` would go on
    // the wire as `1:*`.
    if (remoteUIDNext == 1) {
        folder.localStatus()[LS_UIDNEXT] = remoteUIDNext;
        folder.localStatus()[LS_HIGHESTMODSEQ] = remoteModseq;
        return;
    }

    // The set must end at an explicit UID, not `*`: RFC 7162 §3.2.6 limits VANISHED to UIDs
    // in the set, and `*` is the highest UID still in the mailbox, so Cyrus never reports an
    // expunge above it (cyrus-imapd #6071, unfixed in any release as of 2026-09). UIDNEXT-1
    // is the bound RFC 7162 §3.2.5.1 uses for SELECT QRESYNC without known UIDs. A STATUS
    // without UIDNEXT (NetEase) reports 0 and keeps `*`.
    // Note: a UID appended after the STATUS is outside the set; new mail is found via UIDNEXT.
    uint64_t topUID = remoteUIDNext > 1 ? remoteUIDNext - 1 : UINT64_MAX;

    // if the difference between our stored modseq and highestModseq is very large,
    // we can create a request that takes forever to complete and /blocks/ the foreground
    // worker from performing mailbox actions, which is really bad. To bound the request,
    // we ask for changes within the last 12,000 UIDs only. Our intermittent "deep" scan
    // will recover the rest of the changes so it's safe not to ingest them here.
    uint32_t bottomUID = 1;
    bool limited = false;
    if (!mustSyncAll && remoteModseq - modseq > MODSEQ_TRUNCATION_THRESHOLD) {
        bottomUID = remoteUIDNext > MODSEQ_TRUNCATION_UID_COUNT ? remoteUIDNext - MODSEQ_TRUNCATION_UID_COUNT : 1;
        limited = true;
        logger->warn("syncFolderChangesViaCondstore - request limited to {}:{}, remaining changes will be detected via deep scan", bottomUID, topUID);
    }
    IndexSet * uids = IndexSet::indexSetWithRange(RangeMake(bottomUID, topUID == UINT64_MAX ? UINT64_MAX : topUID - bottomUID));

    IMAPProgress cb;
    ErrorCode err = ErrorCode::ErrorNone;

    auto kind = MailUtils::messagesRequestKindFor(session.storedCapabilities(), true);
    IMAPSyncResult * result = session.syncMessagesByUID(&path, kind, uids, modseq, &cb, &err);
    if (err != ErrorCode::ErrorNone) {
        throw SyncException(err, "syncFolderChangesViaCondstore - syncMessagesByUID");
    }

    // for modified messages, fetch local copy and apply changes
    Array * modifiedOrAdded = result->modifiedOrAddedMessages();
    IndexSet * vanished = result->vanishedMessages();
    
    logger->info("syncFolderChangesViaCondstore - Changes since HMODSEQ {}: {} changed, {} vanished",
                 modseq, modifiedOrAdded->count(), (vanished != nullptr) ? vanished->count() : 0);

    for (unsigned int ii = 0; ii < modifiedOrAdded->count(); ii ++) {
        IMAPMessage * msg = (IMAPMessage *)modifiedOrAdded->objectAtIndex(ii);
        processor->insertFallbackToUpdateMessage(msg, folder, syncDataTimestamp);
    }
    
    // for deleted messages, collect UIDs and delete their placements. Note: vanishedMessages
    // is only populated when QRESYNC is available.
    deleteVanishedUIDs(folder, vanished, "FETCH CHANGEDSINCE");

    // libetpan only hands the first VANISHED line of the response to IMAPSyncResult, and
    // an unrelated expunge can arrive untagged while this FETCH is in flight.
    deleteVanishedUIDs(folder, session.takeVanishedMessages(&path), "this connection");

    folder.localStatus()[LS_UIDNEXT] = remoteUIDNext;
    // A limited request has not applied the changes below its bottom UID. Recording the
    // server's modseq here would make the background pass, which asks for everything, see
    // nothing left to fetch, and those changes would wait for the daily gap scan.
    if (!limited) {
        folder.localStatus()[LS_HIGHESTMODSEQ] = remoteModseq;
    }
}

void SyncWorker::deleteVanishedUIDs(Folder & folder, IndexSet * vanished, const char * source) {
    // Test rangesCount, not count(): count() sums `length + 1` per range, so an open-ended
    // range like 12:* (length UINT64_MAX) wraps it to zero.
    if (vanished == NULL || vanished->rangesCount() == 0) {
        return;
    }
    logger->info("Deleting {} VANISHED UID range(s) in {} reported by {}",
                 vanished->rangesCount(), folder.path(), source);

    // IMPORTANT: vanished may include an infinite range, like 12:*, so we can't convert
    // it to a fixed array.
    vector<Query> queries = MailUtils::queriesForUIDRangesInIndexSet(folder.id(), vanished);
    for (Query & query : queries) {
        processor->deleteVanishedPlacements(folder, query);
    }
}

void SyncWorker::cleanMessageCache(Folder & folder) {
    logger->info("Cleaning local cache and updating stats");
    
    // delete bodies we no longer want. Note: you can't do INNER JOINs within a DELETE
    // note: we only delete messages fetchedd more than 14 days ago to avoid deleting
    // old messages you're actively viewing / could still want
    SQLite::Statement purge(store->db(),
        "DELETE FROM MessageBody WHERE MessageBody.fetchedAt < datetime('now', '-14 days') AND MessageBody.id IN ("
        "SELECT Message.id FROM MessageFolder INNER JOIN Message ON Message.id = MessageFolder.messageId "
        "WHERE MessageFolder.accountId = ? AND MessageFolder.folderId = ? AND MessageFolder.remoteUID > 0 "
        "AND Message.draft = 0 AND Message.date < ?)");
    purge.bind(1, folder.accountId());
    purge.bind(2, folder.id());
    purge.bind(3, (double)(time(0) - maxAgeForBodySync(folder)));
    int purged = purge.exec();
    logger->info("-- {} message bodies deleted from local cache.", purged);
    // TODO BG: Remove them from the search index and remove attachments

    // update messages body stats
    folder.localStatus()[LS_BODIES_PRESENT] = countBodiesDownloaded(folder);
    folder.localStatus()[LS_BODIES_WANTED] = countBodiesNeeded(folder);
}

// Message Body Sync

time_t SyncWorker::maxAgeForBodySync(Folder & folder) {
    return 24 * 60 * 60 * 30 * 3; // three months TODO pref!
}

bool SyncWorker::shouldCacheBodiesInFolder(Folder & folder) {
    // who needs this stuff? probably nobody.
    if ((folder.role() == "spam") || (folder.role() == "trash")) {
        return false;
    }
    return true;
}

long long SyncWorker::countBodiesDownloaded(Folder & folder) {
    SQLite::Statement count(store->db(),
        "SELECT COUNT(DISTINCT MessageFolder.messageId) FROM MessageFolder INNER JOIN MessageBody ON MessageBody.id = MessageFolder.messageId "
        "WHERE MessageFolder.accountId = ? AND MessageFolder.folderId = ? AND MessageFolder.remoteUID > 0 "
        "AND MessageBody.value IS NOT NULL");
    count.bind(1, folder.accountId());
    count.bind(2, folder.id());
    count.executeStep();
    return count.getColumn(0).getInt64();
}

// "Has a server copy in this folder", as a correlated subquery on Message. The body queries
// are driven from the Message date indexes rather than from the folder's placements: on a
// 180k-message folder the placement walk costs three B-tree lookups per copy (0.8-1.3 s),
// while the date-bounded walk over the account's recent messages is 30-70 ms.
static const string HAS_SERVER_COPY_IN_FOLDER =
    " AND EXISTS (SELECT 1 FROM MessageFolder WHERE MessageFolder.messageId = Message.id AND MessageFolder.folderId = ? "
    "AND MessageFolder.remoteUID > 0)";

// Messages wanted in the body cache are the recent ones plus drafts of any age. They are
// always addressed as two statements - recent from MessageListDateIndex, older drafts from
// the partial MessageListDraftIndex - because with the two conditions OR'd in one WHERE the
// planner drops the date bound and walks the whole account, and given a choice of index
// for the draft statement it picks the date index and walks every older message too, so
// the draft statement names its index.
static const string RECENT_BODY_CANDIDATES =
    "FROM Message WHERE Message.accountId = ? AND Message.date > ?";
static const string OLDER_DRAFT_BODY_CANDIDATES =
    "FROM Message INDEXED BY MessageListDraftIndex WHERE Message.accountId = ? AND Message.draft = 1 AND Message.date <= ?";

long long SyncWorker::countBodiesNeeded(Folder & folder) {
    if (!shouldCacheBodiesInFolder(folder)) {
        return 0;
    }
    long long total = 0;
    for (const string * candidates : {&RECENT_BODY_CANDIDATES, &OLDER_DRAFT_BODY_CANDIDATES}) {
        SQLite::Statement count(store->db(), "SELECT COUNT(*) " + *candidates + HAS_SERVER_COPY_IN_FOLDER);
        count.bind(1, folder.accountId());
        count.bind(2, (double)(time(0) - maxAgeForBodySync(folder)));
        count.bind(3, folder.id());
        count.executeStep();
        total += count.getColumn(0).getInt64();
    }
    return total;
}

/*
 Syncs the top N missing message bodies. Returns true if it did work, false if it did nothing.
 */
bool SyncWorker::syncMessageBodies(Folder & folder, IMAPFolderStatus & remoteStatus) {
    if (!shouldCacheBodiesInFolder(folder)) {
        return false;
    }

    vector<string> ids{};
    vector<shared_ptr<Message>> results{};

    // Newest first, stopping at the limit: each index is walked from now downwards and
    // each message checked for a copy here and a missing body.
    const size_t limit = 30;
    const string tail = HAS_SERVER_COPY_IN_FOLDER +
        " AND NOT EXISTS (SELECT 1 FROM MessageBody WHERE MessageBody.id = Message.id)"
        " ORDER BY Message.date DESC LIMIT ?";
    double cutoff = (double)(time(0) - maxAgeForBodySync(folder)); // three months TODO pref!
    SQLite::Statement recent(store->db(), "SELECT Message.id " + RECENT_BODY_CANDIDATES + tail);
    SQLite::Statement drafts(store->db(), "SELECT Message.id " + OLDER_DRAFT_BODY_CANDIDATES + tail);
    for (SQLite::Statement * missing : {&recent, &drafts}) {
        if (ids.size() >= limit) {
            break;
        }
        missing->bind(1, folder.accountId());
        missing->bind(2, cutoff);
        missing->bind(3, folder.id());
        missing->bind(4, (int)(limit - ids.size()));
        while (missing->executeStep()) {
            ids.push_back(missing->getColumn(0).getString());
        }
    }
    
    SQLite::Statement stillMissing(store->db(), "SELECT Message.* FROM Message LEFT JOIN MessageBody ON MessageBody.id = Message.id WHERE Message.id IN (" + MailUtils::qmarks(ids.size()) + ") AND MessageBody.id IS NULL");
    SQLite::Statement insertPlaceholder(store->db(), "INSERT OR IGNORE INTO MessageBody (id, value) VALUES (?, ?)");

    {
        MailStoreTransaction transaction { store, "syncMessageBodies" };

        // Re-check, inside a transaction, that the messages found above still have no body.
        // Inserting an empty body reserves them for this worker so the other one cannot
        // fetch the same message at the same time.
        int ii = 1;
        for (auto id : ids) {
            stillMissing.bind(ii++, id);
        }
        while (stillMissing.executeStep()) {
            results.push_back(make_shared<Message>(stillMissing));
        }
        if (results.size() < ids.size()) {
            logger->info("Body for {} messages already being fetched.", ids.size() - results.size());
        }

        for (auto result : results) {
            // write a blank entry into the MessageBody table so we'll only try to fetch each
            // message once. Otherwise a persistent ErrorFetch or crash for a single message
            // can cause the account to stay "syncing" forever.
            insertPlaceholder.bind(1, result->id());
            insertPlaceholder.bind(2);
            insertPlaceholder.exec();
            insertPlaceholder.reset();
        }

        transaction.commit();
    }

    json & ls = folder.localStatus();
    if (!ls.count(LS_BODIES_PRESENT) || !ls[LS_BODIES_PRESENT].is_number()) {
        ls[LS_BODIES_PRESENT] = 0;
    }
    
    for (auto result : results) {
        // increment local sync state - it's fine if this sometimes fails to save,
        // we recompute the value via COUNT(*) during cleanup
        ls[LS_BODIES_PRESENT] = ls[LS_BODIES_PRESENT].get<long long>() + 1;

        // attempt to fetch the message body from the copy in this folder
        syncMessageBody(result.get(), &folder);
    }
    
    return results.size() > 0;
}

/*
 Fetches the body from the first copy the server will return, in the order
 MailStore::fetchableCopiesOfMessage ranks them. A copy the server refuses to FETCH
 (typically expunged since our last scan) is skipped in favour of the next one; any other
 error is thrown to the caller.
 */
void SyncWorker::syncMessageBody(Message * message, Folder * preferredFolder) {
    // allocated mailcore objects freed when `pool` is removed from the stack
    AutoreleasePool pool;

    auto candidates = store->fetchableCopiesOfMessage(*message, preferredFolder);
    if (candidates.empty()) {
        logger->info("No copy of message \"{}\" ({}) is on the server to fetch a body from.", message->subject(), message->id());
        return;
    }

    for (auto & candidate : candidates) {
        IMAPProgress cb;
        ErrorCode err = ErrorCode::ErrorNone;
        string folderPath = candidate.folder->path();
        String path(AS_MCSTR(folderPath));

        Data * data = session.fetchMessageByUID(&path, candidate.uid, &cb, &err);
        if (err != ErrorNone) {
            logger->error("Unable to fetch body for message \"{}\" ({} UID {}). Error {}",
                          message->subject(), folderPath, candidate.uid, ErrorCodeToTypeMap[err]);

            if (err == ErrorFetch) {
                // The copy we know about may already be gone - the sync worker may not have
                // caught up with the server yet, and drafts in particular come and go.
                continue;
            }

            throw SyncException(err, "syncMessageBody - fetchMessageByUID");
        }
        if (data == nullptr) {
            logger->error("fetchMessageByUID returned null data for message \"{}\" ({} UID {})",
                          message->subject(), folderPath, candidate.uid);
            continue;
        }
        MessageParser * messageParser = MessageParser::messageParserWithData(data);
        if (messageParser == nullptr) {
            logger->error("MessageParser::messageParserWithData returned null for message \"{}\" ({} UID {})",
                          message->subject(), folderPath, candidate.uid);
            return;
        }
        processor->retrievedMessageBody(message, messageParser);
        return;
    }
}
