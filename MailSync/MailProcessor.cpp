//
//  MailProcessor.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/20/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "MailProcessor.hpp"
#include "MailStoreTransaction.hpp"
#include "MailUtils.hpp"
#include "File.hpp"
#include "constants.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>
#include <thread>

#if defined(_MSC_VER)
#include <direct.h>
#include <codecvt>
#include <locale>
#endif

using namespace std;
using nlohmann::json;

class CleanHTMLBodyRendererTemplateCallback : public Object, public HTMLRendererTemplateCallback {
    mailcore::String * templateForMainHeader(MessageHeader * header) {
        return MCSTR("");
    }

    mailcore::String * templateForAttachment(AbstractPart * part) {
        return MCSTR("");
    }

    mailcore::String * templateForAttachmentSeparator() {
        return MCSTR("");
    }
    
    // Normally this calls through to XMLTidy but we have our own sanitizer at the Javascript
    // level and Tidy has led to some bugs due to its very strict parsing:
    // https://github.com/Foundry376/Mailspring/issues/301#issuecomment-342265351
    mailcore::String * cleanHTMLForPart(mailcore::String * html) {
        return html;
    }

    // TODO: Image attachments can be added to the middle of messages
    // by putting them between two HTML parts and we can render them
    // within the body this way. However the attachments don't have cid's,
    // and the client expects to filter attachments based on whether they
    // have cids.
    
//    mailcore::String * templateForImage(AbstractPart * part) {
//        MailUtils::idForFilePart(part)
//        return MCSTR("<img src=\"{{CONTENTID}}\" data-size=\"{{SIZE}}\" data-filename=\"{{FILENAME}}\" />");
//    }
//
//    bool canPreviewPart(AbstractPart * part) {
//        string t = part->mimeType()->UTF8Characters();
//        
//        if ((t == "image/png") || (t == "image/jpeg") || (t == "image/jpg") || (t == "image/gif")) {
//            return true;
//        }
//        return false;
//    }
};

string stringByAppendingOrSkipping(string input, string val) {
    auto valWithSpace = " " + val;
    if (input.find(valWithSpace) != std::string::npos) {
        return input;
    }
    return input + valWithSpace;
}

MailProcessor::MailProcessor(shared_ptr<Account> account, MailStore * store) :
    store(store),
    account(account),
    logger(spdlog::get("logger"))
{

}

// Detected from the X-GM-EXT-1 capability rather than the account's provider, which is
// "imap" for a Gmail account added with generic IMAP settings.
void MailProcessor::setIsGmail(bool isGmail) {
    _isGmail = isGmail;
}

namespace {

// The copy of `messageId` at (folder, UID), if one is recorded.
optional<Placement> placementAt(MailStore * store, const string & messageId, const string & folderId, uint32_t uid) {
    for (auto & p : store->placementsForMessage(messageId)) {
        if (p.folderId == folderId && p.remoteUID == uid) {
            return p;
        }
    }
    return nullopt;
}

MessageAttributes attributesOfPlacement(const Placement & p) {
    MessageAttributes attrs{p.remoteUID, p.unread, p.starred, p.draft, {}};
    for (auto & l : p.labels) {
        attrs.labels.push_back(l.get<string>());
    }
    return attrs;
}

bool placementIsCurrent(const optional<Placement> & p, const MessageAttributes & reported) {
    return p && MessageAttributesMatch(attributesOfPlacement(*p), reported);
}

} // namespace

/*
 Ingests one copy of a message reported by a folder scan. Message ids are a hash of the
 headers, so a message already known from another folder or UID is found by id and the
 copy recorded as a placement on it. The lookup and placement comparison come first so an
 unchanged copy - most of every scan - does not open a transaction; updateMessage makes the
 decision again inside one. The insert can still hit a constraint error (SQLite 19) when
 the other worker ingested the same message a moment ago, or the two race on one
 (folder, UID) of the MessageFolder unique index.
 */
shared_ptr<Message> MailProcessor::insertFallbackToUpdateMessage(IMAPMessage * mMsg, Folder & folder, time_t syncDataTimestamp) {
    string id = MailUtils::idForMessage(folder.accountId(), folder.path(), mMsg);
    auto localMessage = store->find<Message>(Query().equal("id", id));
    if (localMessage != nullptr) {
        auto existing = placementAt(store, id, folder.id(), mMsg->uid());
        if (placementIsCurrent(existing, MessageAttributesForMessage(mMsg))) {
            return localMessage;
        }
        localMessage = updateMessage(id, mMsg, folder, syncDataTimestamp);
        if (localMessage != nullptr) {
            return localMessage;
        }
    }
    try {
        return insertMessage(mMsg, folder, syncDataTimestamp);
    } catch (const SQLite::Exception & ex) {
        if (ex.getErrorCode() != 19) { // constraint failed
            throw;
        }
        localMessage = updateMessage(id, mMsg, folder, syncDataTimestamp);
        if (localMessage.get() == nullptr) {
            throw;
        }
        return localMessage;
    }
}

shared_ptr<Message> MailProcessor::insertMessage(IMAPMessage * mMsg, Folder & folder, time_t syncDataTimestamp) {
    shared_ptr<Message> msg = make_shared<Message>(mMsg, folder, syncDataTimestamp);
    shared_ptr<Thread> thread = nullptr;

    Array * references = mMsg->header()->references();
    if (references == nullptr) {
        references = new Array();
        references->autorelease();
    }

    {
        MailStoreTransaction transaction{store, "insertMessage"};

        // Find the correct thread

        if (mMsg->gmailThreadID()) {
            Query query = Query().equal("gThrId", to_string(mMsg->gmailThreadID()));
            thread = store->find<Thread>(query);
            
        } else if (!mMsg->header()->isMessageIDAutoGenerated()) {
            // find an existing thread using the references. Note - a rouge client could
            // throw a lot of shit in here, limit the number of refs we look at to 50.
            // TODO: It appears we should technically use the first 1 and then last 49.
            int refcount = min(50, (int)references->count());
            SQLite::Statement tQuery(store->db(), "SELECT Thread.* FROM Thread INNER JOIN ThreadReference ON ThreadReference.threadId = Thread.id WHERE ThreadReference.accountId = ? AND ThreadReference.headerMessageId IN (" + MailUtils::qmarks(1 + refcount) + ") LIMIT 1");
            tQuery.bind(1, msg->accountId());
            tQuery.bind(2, msg->headerMessageId());
            for (int i = 0; i < refcount; i ++) {
                String * ref = (String *)references->objectAtIndex(i);
                // Skip null entries that could arise from malformed reference headers
                if (ref == nullptr) {
                    tQuery.bind(3 + i, "");
                    continue;
                }
                tQuery.bind(3 + i, ref->UTF8Characters());
            }
            if (tQuery.executeStep()) {
                thread = make_shared<Thread>(tQuery);
            }
        }
        
        if (thread == nullptr) {
            // TODO: could move to message save hooks
            thread = make_shared<Thread>(msg->id(), account->id(), msg->subject(), mMsg->gmailThreadID());
        }
        
        msg->setThreadId(thread->id());

        // Written before the Message row and rebuilt now rather than on save, because the
        // thread diff below runs before the save; an id collision on the insert rolls the
        // row back with the transaction.
        string displaced = store->upsertPlacement(*msg, folder, mMsg->uid(), MessageAttributesForMessage(mMsg));
        store->refreshMessageFromPlacements(*msg);

        // Apply the new message's attributes to the thread (folder/label refcounts,
        // unread/starred counters, timestamps) BEFORE saving the thread. This avoids
        // Message::afterSave re-loading and re-saving the thread a second time.
        MessageSnapshot empty = MessageEmptySnapshot;
        thread->applyMessageAttributeChanges(empty, msg.get(), store);
        msg->captureSnapshot();
        msg->_skipThreadUpdatesAfterSave = true;

        // Index the thread metadata for search. We only do this once and it'd
        // be costly to make it part of the save hooks.
        appendToThreadSearchContent(thread.get(), msg.get(), nullptr);
        store->save(thread.get());
        store->save(msg.get());
        
        // Make the thread accessible by all of the message references
        upsertThreadReferences(thread->id(), thread->accountId(), msg->headerMessageId(), references);

        saveDisplacedMessage(displaced);

        transaction.commit();
    }

    upsertContacts(msg.get());

    return msg;
}

/*
 Records the copy at (folder, uid) on a message that already exists and returns the message
 as saved, or nullptr if it no longer exists. The message and its placements are read after
 BEGIN IMMEDIATE: both workers can process the same server change at once, and a copy loaded
 before the other worker committed would apply that change's thread delta a second time and
 write its stale JSON over the other worker's. Only the placement at (folder, uid) is compared
 against what the server reported.

 The `syncedAt` guard is message-level: while a task the user queued is in flight, its local
 phase has already written the copies' new flags or marked them for a move, and a scan of
 those copies must not revert that. It only protects copies already recorded. A copy the
 message has nowhere yet is recorded with the server's flags even under the lock: another
 client may have moved the message's only copy, and skipping the new one would leave the
 message an orphan for the sweep. The lock is left in place for the task to release.
 */
shared_ptr<Message> MailProcessor::updateMessage(const string & messageId, IMAPMessage * remote, Folder & folder, time_t syncDataTimestamp)
{
    auto updated = MessageAttributesForMessage(remote);

    // Every return commits: a rollback would also drop the store's cached statements.
    MailStoreTransaction transaction{store, "updateMessage"};

    auto local = store->find<Message>(Query().equal("id", messageId));
    if (local == nullptr) {
        transaction.commit();
        return nullptr;
    }
    auto p = placementAt(store, messageId, folder.id(), updated.uid);
    if (placementIsCurrent(p, updated)) {
        transaction.commit();
        return local;
    }
    bool locked = local->syncedAt() > syncDataTimestamp;
    if (p && locked) {
        logger->warn("Ignoring changes to {}, local data is newer {} < {}", local->subject(), syncDataTimestamp, local->syncedAt());
        transaction.commit();
        return local;
    }
    if (p) {
        auto existing = attributesOfPlacement(*p);
        logger->info("- Updating message {} in {}", local->id(), folder.path());
        if (updated.unread != existing.unread) {
            logger->info("-- Unread ({} to {})", existing.unread, updated.unread);
        }
        if (updated.starred != existing.starred) {
            logger->info("-- Starred ({} to {})", existing.starred, updated.starred);
        }
        if (updated.draft != existing.draft) {
            logger->info("-- Draft ({} to {})", existing.draft, updated.draft);
        }
        if (updated.labels != existing.labels) {
            logger->info("-- XGMLabels ({} to {})", json(existing.labels).dump(), json(updated.labels).dump());
        }
    } else {
        logger->info("- Message {} has a copy in {} (UID {})", local->id(), folder.path(), updated.uid);
    }

    json before = local->toJSON();
    string displaced = store->upsertPlacement(*local, folder, updated.uid, updated);

    if (_isGmail) {
        string role = folder.role();
        if (role == "all" || role == "spam" || role == "trash") {
            store->removePlacementsOutsideFolder(*local, folder.id());
        }
    }

    // Only a change the client can see is worth a persist delta and a thread update; a
    // second copy's flags or a UIDVALIDITY relink may have changed the row alone.
    store->refreshMessageFromPlacements(*local);
    if (local->toJSON() != before) {
        logger->info("-- Folders now {}", local->folders().dump());
        if (!locked) {
            local->setSyncedAt(syncDataTimestamp);
        }
        store->save(local.get());
    }

    saveDisplacedMessage(displaced);

    transaction.commit();
    return local;
}

// A (folder, UID) row taken over from another message leaves that message's snapshot
// listing a copy it no longer has. Losing its last row makes it an orphan like any other
// vanished copy, swept at the end of the pass unless another folder turns it up.
void MailProcessor::saveDisplacedMessage(const string & messageId) {
    if (messageId.empty()) {
        return;
    }
    logger->warn("- Message {} lost a placement to another message at the same UID", messageId);
    refreshMessagesInOpenTransaction({messageId}, UnplacedMessages::KeepAsOrphan);
}

namespace {

// Lowercase + trim ASCII whitespace. Uses unsigned-char lambdas because
// the C ctype functions are UB on negative `char` values, and these headers
// can carry arbitrary bytes from the public Internet.
std::string lowerTrimmed(const std::string & s) {
    auto isWs = [](unsigned char c) { return std::isspace(c) != 0; };
    auto first = std::find_if_not(s.begin(), s.end(), isWs);
    auto last  = std::find_if_not(s.rbegin(), s.rend(), isWs).base();
    if (first >= last) return {};
    std::string out(first, last);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Classify an Importance / X-MSMail-Priority value to one of
// "high" / "low" / "normal", or empty if unrecognized. Token-based to avoid
// substring false positives like "abnormal" matching "normal" or
// "highly recommended" matching "high".
std::string classifyImportanceText(const std::string & raw) {
    std::string v = lowerTrimmed(raw);
    if (v.empty()) return {};
    // Outlook/MAPI extended values that some gateways pass through verbatim.
    if (v.compare(0, 12, "above normal") == 0) return "high";
    if (v.compare(0, 12, "below normal") == 0) return "low";
    // Otherwise compare the first whitespace/comment-delimited token.
    auto cut = v.find_first_of(" \t(,;");
    std::string token = (cut == std::string::npos) ? v : v.substr(0, cut);
    if (token == "high")   return "high";
    if (token == "low")    return "low";
    if (token == "normal") return "normal";
    return {};
}

// X-Priority: numeric scale 1-5 per RFC 2076. Other digits and non-digit
// values are treated as unrecognized so the caller can fall through.
std::string classifyXPriority(const std::string & raw) {
    std::string v = lowerTrimmed(raw);
    if (v.empty() || !std::isdigit(static_cast<unsigned char>(v[0]))) return {};
    switch (v[0]) {
        case '1': case '2': return "high";
        case '3':           return "normal";
        case '4': case '5': return "low";
        default:            return {};
    }
}

} // namespace

void MailProcessor::retrievedMessageBody(Message * message, MessageParser * parser) {
    CleanHTMLBodyRendererTemplateCallback * htmlCallback = new CleanHTMLBodyRendererTemplateCallback();
    const char * bodyRepresentation;
    bool bodyIsPlaintext;
    
    Array * partAttachments = Array::array();
    Array * htmlInlineAttachments = Array::array();

    // Note - exposed this lower level API manually so that we can avoid running this renderer three
    // times to retrieve attachments, relatedAttachments, message HTML separately. The code seems to build
    // and discard things you don't ask for.
    String * html = parser->htmlRenderingAndAttachments(htmlCallback, partAttachments, htmlInlineAttachments);
    if (html == NULL) {
        logger->warn("Failed to render message body for message {}: parser returned null", message->id());
        MC_SAFE_RELEASE(htmlCallback);
        return;
    }
    String * text = html;

    if (html->hasPrefix(MCSTR("PLAINTEXT:"))) {
        text = html->substringFromIndex(10);
        bodyRepresentation = text->UTF8Characters();
        bodyIsPlaintext = true;
    } else {
        String * flattenedHTML = html->flattenHTML();
        if (flattenedHTML != NULL) {
            text = flattenedHTML->stripWhitespace();
        } else {
            // flattenHTML failed, use empty string to avoid crash
            text = MCSTR("");
        }
        bodyRepresentation = html->UTF8Characters();
        bodyIsPlaintext = false;
    }
    MC_SAFE_RELEASE(htmlCallback);

    // build file containers for the attachments and write them to disk
    Array attachments = Array();
    attachments.addObjectsFromArray(partAttachments);
    attachments.addObjectsFromArray(htmlInlineAttachments);
    
    vector<File> files;
    for (int ii = 0; ii < attachments.count(); ii ++) {
        Attachment * a = (Attachment *)attachments.objectAtIndex(ii);
        if (a->contentID() && a->isInlineAttachment() == false) {
            // This is suspicious - the item has a content ID but we don't think it's an attachment?
            // Look in the content of the message for "cid:XXX". If we find it, the MIME was missing
            // the Content-Disposition but the client should render it inline.
            if (html->locationOfString(MCSTR("cid:")->stringByAppendingString(a->contentID())) != -1) {
                a->setInlineAttachment(true);
            }
        }
        
        File f = File(message, a);
        
        bool duplicate = false;
        for (auto & other : files) {
            if (other.partId() == string(a->partID()->UTF8Characters())) {
                duplicate = true;
                logger->info("Attachment is duplicate: {}", f.toJSON().dump());
                break;
            }
        }
        
        // Sometimes the HTML will reference "cid:filename.png@123123garbage" and the file will
        // not have a contentId. The client does not support this, so if cid:filename.png appears
        // in the body we manually make it the contentId
        if (f.contentId().is_null() && strstr(bodyRepresentation, ("cid:" + f.filename()).c_str()) != nullptr) {
            f.setContentId(f.filename());
        }

        if (!duplicate) {
            if (!retrievedFileData(&f, a->data())) {
                logger->info("Could not save file data!");
            }
            files.push_back(f);
        }
    }
    
    // enter transaction
    {
        MailStoreTransaction transaction{store, "retrievedMessageBody"};

        // The caller's object predates the body fetch - up to 30 fetches, on the other
        // worker's schedule - and the message's placements or flags may have changed since.
        // Saving it would write that stale folders snapshot and unread state over the row,
        // and no scan repairs the snapshot: scans compare MessageFolder against the server.
        auto fresh = store->find<Message>(Query().equal("id", message->id()));
        if (fresh == nullptr) {
            logger->info("Message {} was removed while its body was fetched, discarding the body.", message->id());
            return;
        }
        message = fresh.get();
        
        // write body to the MessageBodies table
        SQLite::Statement insert(store->db(), "REPLACE INTO MessageBody (id, value, fetchedAt) VALUES (?, ?, datetime('now'))");
        insert.bind(1, message->id());
        insert.bind(2, bodyRepresentation);
        insert.exec();
        
        // write files to the files table
        
        // try to save the files to the database. We don't care about failures here -
        // it's possible the files are already there if we're re-fetching this message
        // for some reason and we haven't loaded the existing ones.
        for (auto & file : files) {
            try {
                store->save(&file);
            } catch (SQLite::Exception &) {
                logger->warn("Unable to insert file ID {} - it must already exist.", file.id());
            }
        }
        
        // append the body text to the thread's FTS5 search index
        auto thread = store->find<Thread>(Query().equal("id", message->threadId()));
        if (thread.get() != nullptr) {
            appendToThreadSearchContent(thread.get(), nullptr, text);
        }

        // write the message snippet. This also gives us the database trigger!
        message->setSnippet(text->substringToIndex(400)->UTF8Characters());
        message->setPlaintext(bodyIsPlaintext);
        message->setBodyForDispatch(bodyRepresentation);
        message->markBodyStored();
        message->setFiles(files);

        // extract additional headers from the full message that weren't available
        // during initial sync (which only fetches IMAP ENVELOPE)
        MessageHeader * msgHeader = parser->header();
        if (msgHeader != nullptr) {
            String * listUnsub = msgHeader->extraHeaderValueForName(MCSTR("List-Unsubscribe"));
            String * listUnsubPost = msgHeader->extraHeaderValueForName(MCSTR("List-Unsubscribe-Post"));

            if (listUnsub != nullptr) {
                message->_data["hListUnsub"] = listUnsub->UTF8Characters();
            }
            if (listUnsubPost != nullptr) {
                message->_data["hListUnsubPost"] = listUnsubPost->UTF8Characters();
            }

            // Resolve message importance to a canonical "high" / "low" / "normal".
            // Precedence: Importance > X-Priority > X-MSMail-Priority. The Importance
            // header is authoritative when present — if its value is unrecognized we
            // stop the chain rather than fall through to lower-precedence headers
            // that might contradict an explicitly-set Importance.
            string importance;
            if (String * h = msgHeader->extraHeaderValueForName(MCSTR("Importance"))) {
                importance = classifyImportanceText(h->UTF8Characters());
            } else {
                if (String * h = msgHeader->extraHeaderValueForName(MCSTR("X-Priority"))) {
                    importance = classifyXPriority(h->UTF8Characters());
                }
                if (importance.empty()) {
                    if (String * h = msgHeader->extraHeaderValueForName(MCSTR("X-MSMail-Priority"))) {
                        importance = classifyImportanceText(h->UTF8Characters());
                    }
                }
            }
            if (!importance.empty()) {
                message->_data["hImportance"] = importance;
            }
        }

        store->save(message);
        
        transaction.commit();
    }
}


bool MailProcessor::retrievedFileData(File * file, Data * data) {
    string root = MailUtils::getEnvUTF8("CONFIG_DIR_PATH") + FS_PATH_SEP + "files";
    string path = MailUtils::pathForFile(root, file, true);
#ifdef _MSC_VER
    wstring_convert<codecvt_utf8<wchar_t>, wchar_t> convert;
    return (data->writeToFile(AS_WIDE_MCSTR(convert.from_bytes(path))) == ErrorNone);
#else
    return (data->writeToFile(AS_MCSTR(path)) == ErrorNone);
#endif
}

void MailProcessor::deleteVanishedPlacements(Folder & folder, const vector<uint32_t> & uids)
{
    logger->info("Deleting {} UIDs no longer present in {}.", uids.size(), folder.path());
    vector<string> affected;
    {
        MailStoreTransaction transaction{store, "deleteVanishedPlacements"};
        affected = store->deleteVanishedPlacements(folder, uids);
        transaction.commit();
    }
    refreshMessages(affected, UnplacedMessages::KeepAsOrphan, "deleteVanishedPlacements");
}

void MailProcessor::deleteVanishedPlacements(Folder & folder, Query & uidQuery)
{
    logger->info("Deleting UIDs matching {} no longer present in {}.", uidQuery.getSQL(), folder.path());
    vector<string> affected;
    {
        MailStoreTransaction transaction{store, "deleteVanishedPlacements"};
        affected = store->deleteVanishedPlacements(folder, uidQuery);
        transaction.commit();
    }
    refreshMessages(affected, UnplacedMessages::KeepAsOrphan, "deleteVanishedPlacements");
}

void MailProcessor::deleteUnassignedPlacements(Folder & folder)
{
    vector<string> affected;
    {
        MailStoreTransaction transaction{store, "deleteUnassignedPlacements"};
        affected = store->deleteUnassignedPlacements(folder);
        transaction.commit();
    }
    if (affected.size() > 0) {
        logger->info("Deleted {} copies in {} the UIDVALIDITY rebuild did not find.", affected.size(), folder.path());
    }
    refreshMessages(affected, UnplacedMessages::KeepAsOrphan, "deleteUnassignedPlacements");
}

/*
 Catches each message's snapshot up with its rows, in transactions of 100 so a mass change
 does not hold the database for the whole batch. `inTransaction`, when given, runs first in
 each chunk's transaction and returns the ids to refresh: it is where a caller deletes the
 rows or re-checks a condition under the lock, so the change and the snapshots commit
 together. `pause` gives the client time to keep up with a mass deletion.
 */
void MailProcessor::refreshMessages(const vector<string> & messageIds, UnplacedMessages unplaced, const string & transactionName,
                                    const RefreshChunkStep & inTransaction, std::chrono::milliseconds pause)
{
    if (messageIds.empty()) {
        return;
    }
    bool logSubjects = messageIds.size() < 20;
    vector<string> ids = messageIds;
    for (auto chunk : MailUtils::chunksOfVector(ids, 100)) {
        int removed = 0;
        size_t refreshed = 0;
        {
            MailStoreTransaction transaction{store, transactionName};
            vector<string> refresh = inTransaction ? inTransaction(chunk) : chunk;
            refreshed = refresh.size();
            removed = refreshMessagesInOpenTransaction(refresh, unplaced, logSubjects);
            transaction.commit();
        }
        if (unplaced == UnplacedMessages::Remove) {
            logger->info("-- Deleted {} local messages, {} kept copies elsewhere", removed, refreshed - removed);
        }
        if (pause.count() > 0) {
            std::this_thread::sleep_for(pause);
        }
    }
}

/*
 The caller owns the transaction so a row change and the messages it affects commit
 together. A message with no rows is either kept as an orphan (its snapshot empties and
 refreshMessageFromPlacements records it) or, with UnplacedMessages::Remove, goes through
 store->remove so Message::afterRemove balances the thread and deletes the body, metadata
 and orphan record; the check runs inside the transaction so a copy the other worker
 records meanwhile keeps its message. A message whose snapshot did not change is not saved,
 so the client gets no persist for a version bump alone. Returns how many were removed.
 */
int MailProcessor::refreshMessagesInOpenTransaction(const vector<string> & messageIds, UnplacedMessages unplaced, bool logSubjects)
{
    int removed = 0;
    if (messageIds.empty()) {
        return removed;
    }
    vector<string> ids = messageIds;
    auto messages = store->findAll<Message>(Query().equal("id", ids));
    for (auto & msg : messages) {
        json before = msg->toJSON();
        store->refreshMessageFromPlacements(*msg);
        if (unplaced == UnplacedMessages::Remove && msg->folders().empty()) {
            if (logSubjects) {
                logger->info("-- Removing \"{}\" ({}), no remaining copies", msg->subject(), msg->id());
            }
            store->remove(msg.get());
            removed++;
            continue;
        }
        if (msg->toJSON() == before) {
            continue;
        }
        if (logSubjects) {
            logger->info("-- \"{}\" ({}) now in {}", msg->subject(), msg->id(), msg->folders().dump());
        }
        store->save(msg.get());
    }
    return removed;
}

/*
 Detaches every copy in a folder that is gone from the server (deleted, or emptied by
 ExpungeAllInFolder) and catches the messages up chunk by chunk: a message whose only copy
 was there is removed right away, one with copies elsewhere just loses this folder.

 Deleting all of the folder's rows first and rewriting the messages afterwards would leave
 a window - minutes wide when a large Trash is emptied with a pause between chunks - in
 which a quit or a crash strands messages with copies elsewhere whose snapshot still lists
 the folder: nothing would revisit them.
 */
void MailProcessor::detachMessagesFromFolder(string folderId, std::chrono::milliseconds pause)
{
    refreshMessages(store->messageIdsWithPlacementsInFolder(folderId), UnplacedMessages::Remove, "detachMessagesFromFolder",
                    [&](const vector<string> & chunk) {
                        store->deletePlacementsForFolder(folderId, chunk);
                        return chunk;
                    }, pause);
}

/*
 End-of-pass sweep. SyncWorker::syncNow picks `before` so that every folder has been
 scanned in full since a message orphaned before it (except a folder that has gone
 unscanned for longer than ORPHAN_SWEEP_MAX_WAIT), so a copy that moved elsewhere has
 already been recorded and cleared its orphan record
 (MailStore::refreshMessageFromPlacements). What is still listed is removed. `before` is
 0 while a folder still in initial sync has never been fully scanned, and earlier than
 `passStartedAt` when a folder was not covered in full this pass.
 */
void MailProcessor::sweepExpiredOrphans(time_t before, time_t passStartedAt)
{
    if (before == 0) {
        logger->info("Orphan sweep skipped: a folder still in initial sync has not been fully scanned since launch.");
        return;
    }
    if (before < passStartedAt) {
        logger->info("Orphan sweep limited to messages orphaned more than {}s before this pass: a folder was skipped, still in initial sync, or had a fetch truncated.", passStartedAt - before);
    }
    vector<string> candidates = store->orphanMessageIdsBefore(account->id(), before);
    if (candidates.empty()) {
        return;
    }
    logger->info("Sync loop removing {} messages left with no copies.", candidates.size());
    // The foreground worker can revive and re-orphan a candidate while earlier chunks run,
    // restarting its grace period, so its record is re-read under the lock.
    refreshMessages(candidates, UnplacedMessages::Remove, "sweepExpiredOrphans", [&](const vector<string> & chunk) {
        return store->orphanMessageIdsBefore(account->id(), before, chunk);
    });
}

void MailProcessor::appendToThreadSearchContent(Thread * thread, Message * messageToAppendOrNull, String * bodyToAppendOrNull) {
    string to = "";
    string from = "";
    string categories = thread->categoriesSearchString();
    string body = "";
    
    // retrieve the current index if there is one
    if (thread->searchRowId()) {
        SQLite::Statement existing(store->db(), "SELECT to_, from_, body FROM ThreadSearch WHERE rowid = ?");
        existing.bind(1, (double)thread->searchRowId());
        if (existing.executeStep()) {
            to = existing.getColumn("to_").getString();
            from = existing.getColumn("from_").getString();
            body = existing.getColumn("body").getString();
        }
    }
    
    if (messageToAppendOrNull != nullptr) {
        for (auto c : messageToAppendOrNull->to()) {
            if (c.count("email")) { to = stringByAppendingOrSkipping(to, c["email"].get<string>()); }
            if (c.count("name")) { to = stringByAppendingOrSkipping(to, c["name"].get<string>()); }
        }
        for (auto c : messageToAppendOrNull->cc()) {
            if (c.count("email")) { to = stringByAppendingOrSkipping(to, c["email"].get<string>()); }
            if (c.count("name")) { to = stringByAppendingOrSkipping(to, c["name"].get<string>()); }
        }
        for (auto c : messageToAppendOrNull->bcc()) {
            if (c.count("email")) { to = stringByAppendingOrSkipping(to, c["email"].get<string>()); }
            if (c.count("name")) { to = stringByAppendingOrSkipping(to, c["name"].get<string>()); }
        }
        for (auto c : messageToAppendOrNull->from()) {
            if (c.count("email")) { from = stringByAppendingOrSkipping(from, c["email"].get<string>()); }
            if (c.count("name")) { from = stringByAppendingOrSkipping(from, c["name"].get<string>()); }
        }
    }
    
    if (bodyToAppendOrNull != nullptr) {
        body = body + " " + bodyToAppendOrNull->substringToIndex(5000)->UTF8Characters();
    }
    
    if (thread->searchRowId()) {
        SQLite::Statement update(store->db(), "UPDATE ThreadSearch SET to_ = ?, from_ = ?, body = ?, categories = ? WHERE rowid = ?");
        update.bind(1, to);
        update.bind(2, from);
        update.bind(3, body);
        update.bind(4, categories);
        update.bind(5, (double)thread->searchRowId());
        update.exec();
    } else {
        SQLite::Statement insert(store->db(), "INSERT INTO ThreadSearch (subject, to_, from_, body, categories, content_id) VALUES (?, ?, ?, ?, ?, ?)");
        insert.bind(1, thread->subject());
        insert.bind(2, to);
        insert.bind(3, from);
        insert.bind(4, body);
        insert.bind(5, categories);
        insert.bind(6, thread->id());
        insert.exec();
        thread->setSearchRowId(store->db().getLastInsertRowid());
    }
}

void MailProcessor::upsertThreadReferences(string threadId, string accountId, string headerMessageId, Array * references) {
    SQLite::Statement query(store->db(), "INSERT OR IGNORE INTO ThreadReference (threadId, accountId, headerMessageId) VALUES (?,?,?)");
    query.bind(1, threadId);
    query.bind(2, accountId);
    query.bind(3, headerMessageId);
    query.exec();
    query.reset();

    // Index the first reference (thread root) and last N-1 references (most recent).
    // This ensures thread continuity through the root while keeping recent messages connected.
    // If count <= MAX_REFS, all references are indexed.
    const int MAX_REFS = 100;
    int count = (int)references->count();
    int lastNCount = min(MAX_REFS - 1, count - 1);
    int lastNStart = count - lastNCount;

    // Index first reference (thread root)
    if (count > 0) {
        String * firstRef = (String*)references->objectAtIndex(0);
        query.bind(3, firstRef->UTF8Characters());
        query.exec();
        query.reset(); // does not clear bindings 1 and 2! https://sqlite.org/c3ref/reset.html
    }

    // Index last N-1 references (most recent), skipping index 0 to avoid duplicate
    for (int i = max(1, lastNStart); i < count; i++) {
        String * address = (String*)references->objectAtIndex(i);
        // Skip null entries that could arise from malformed reference headers
        if (address == nullptr) {
            continue;
        }
        query.bind(3, address->UTF8Characters());
        query.exec();
        query.reset();
    }
}

void MailProcessor::upsertContacts(Message * message) {
    // As of Mailspring 1.7, we no longer keep around Contacts that you've never
    // sent email to. We actually never really did anything with these.
    if (!message->isSentByUser(store)) {
        return;
    }
    
    map<string, json> byEmail{};
    for (auto & c : message->to()) {
        if (c.count("email")) {
            byEmail[MailUtils::contactKeyForEmail(c["email"].get<string>())] = c;
        }
    }
    for (auto & c : message->cc()) {
        if (c.count("email")) {
            byEmail[MailUtils::contactKeyForEmail(c["email"].get<string>())] = c;
        }
    }
    for (auto & c : message->from()) {
        if (c.count("email")) {
            byEmail[MailUtils::contactKeyForEmail(c["email"].get<string>())] = c;
        }
    }
    
    // contactKeyForEmail returns "" for some emails. Toss out that item
    if (byEmail.count("")) {
        byEmail.erase("");
    }
    
    vector<string> emails{};
    for (auto const& imap: byEmail) {
        emails.push_back(imap.first);
    }
    
    if (emails.size() > 25) {
        // I think it's safe to say mass emails shouldn't create contacts.
        return;
    }
    
    {
        // Index contacts for autocomplete. We do this separately in a transaction that does not
        // emit any deltas, since the client doesn't need to be bothered with contacts changes.
        MailStoreTransaction transaction{store, "insertMessage:contacts"};

        Query query = Query().equal("email", emails).equal("source", CONTACT_SOURCE_MAIL);
        auto results = store->findAll<Contact>(query);
        
        // update refcounts of existing items if this is a sent message
        for (auto & result : results) {
            if (result->refs() < CONTACT_MAX_REFS) {
                result->incrementRefs();
                store->save(result.get());
            }
            byEmail.erase(result->email());
        }
        
        // insert remaining items (contacts not yet in the database)
        for (auto & result : byEmail) {
            string name = result.second.count("name") ? result.second["name"].get<string>() : "";
            string email = result.second.count("email") ? result.second["email"].get<string>() : "";

            // "Mailspring Team" is used in the welcome email sent from the user's own address.
            // Skip creating the contact to avoid saving the wrong display name.
            if (name == "Mailspring Team" && email.find("@getmailspring.com") == string::npos) {
                continue;
            }

            auto c = make_shared<Contact>(result.first, message->accountId(), result.first, 0, CONTACT_SOURCE_MAIL);
            c->setName(name);
            c->incrementRefs();
            store->save(c.get());
        }
        
        store->unsafeEraseTransactionDeltas();
        transaction.commit();
    }
}

