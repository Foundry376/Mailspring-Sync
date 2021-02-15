//
//  Folder.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "TaskProcessor.hpp"
#include "MailProcessor.hpp"
#include "MailStoreTransaction.hpp"
#include "MailUtils.hpp"
#include "Thread.hpp"
#include "Message.hpp"
#include "MailUtils.hpp"
#include "DAVWorker.hpp"
#include "DAVUtils.hpp"
#include "GoogleContactsWorker.hpp"
#include "File.hpp"
#include "ContactGroup.hpp"
#include "constants.h"
#include "ProgressCollectors.hpp"
#include "SyncException.hpp"
#include "NetworkRequestUtils.hpp"

#if defined(_MSC_VER)
#include <direct.h>
#include <codecvt>
#include <locale>
#endif

using namespace std;
using namespace mailcore;
using namespace nlohmann;

// A helper function that can move messages between folders and update the provided
// messages remoteUIDs, even if UIDPLUS and/or MOVE extensions are not present.

void _moveMessagesResilient(IMAPSession * session, String * path, Folder * destFolder, IndexSet * uids, vector<shared_ptr<Message>> messages) {
    ErrorCode err = ErrorCode::ErrorNone;
    HashMap * uidmap = nullptr;
    String * destPath = AS_MCSTR(destFolder->path());
    bool mustApplyAttributes = false;
    
    // First, perform the action - either the MOVE or the COPY, STORE, EXPUNGE
    // if IMAPCapabilityMove is not present.
    if (session->storedCapabilities()->containsIndex(IMAPCapabilityMove)) {
        session->moveMessages(path, uids, destPath, &uidmap, &err);
        if (err != ErrorCode::ErrorNone) {
            throw SyncException(err, "moveMessages");
        }
    } else {
        session->copyMessages(path, uids, destPath, &uidmap, &err);
        if (err != ErrorCode::ErrorNone) {
            throw SyncException(err, "moveMessages(copy)");
        }
        session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindAdd, MessageFlagDeleted, &err);
        session->expunge(path, &err); // this will empty their whole trash...
        if (err != ErrorCode::ErrorNone) {
            throw SyncException(err, "moveMessages(copy cleanup)");
        }
        mustApplyAttributes = true;
    }

    // Only returned if UIDPLUS extension is present and the server tells us
    // which UIDs in the old folder map to which UIDs in the new folder.
    if (uidmap != nullptr) {
        for (auto msg : messages) {
            Value * currentUID = Value::valueWithUnsignedLongValue(msg->remoteUID());
            Value * newUID = (Value *)uidmap->objectForKey(currentUID);
            if (!newUID) {
                throw SyncException("generic", "move did not provide new UID.", false);
            }
            msg->setRemoteFolder(destFolder);
            msg->setRemoteUID(newUID->unsignedIntValue());
        }
    } else {
        // UIDPLUS is not supported, we need to manually find the messages. Thankfully moves
        // should add higher UIDs to the folder so we can grab the last few and get the messages
        auto status = session->folderStatus(destPath, &err);
        IMAPMessagesRequestKind kind = MailUtils::messagesRequestKindFor(session->storedCapabilities(), true);
        
        if (status != nullptr) {
            uint32_t min = status->uidNext() - (uint32_t)messages.size() * 2;
            if (min < 1) min = 1;
            IndexSet * set = IndexSet::indexSetWithRange(RangeMake(min, UINT64_MAX));
            Array * movedMessages = session->fetchMessagesByUID(destPath, kind, set, nullptr, &err);
            for (auto msg : messages) {
                bool found = false;
                for (unsigned int ii = 0; ii < movedMessages->count(); ii ++) {
                    IMAPMessage * movedMessage = (IMAPMessage*)movedMessages->objectAtIndex(ii);
                    string movedId = MailUtils::idForMessage(msg->accountId(), destFolder->path(), movedMessage);
                    if (msg->id() == movedId) {
                        msg->setRemoteFolder(destFolder);
                        msg->setRemoteUID(movedMessage->uid());
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    spdlog::get("logger")->error("-- Could not find new UID for message {}", msg->id());
                }
            }
        }
    }
    
    if (mustApplyAttributes) {
        for (auto msg : messages) {
            if (msg->remoteFolderId() == destFolder->id()) {
                MessageFlag flags = MessageFlagNone;
                if (msg->isStarred())
                    flags = (MessageFlag)(flags | MessageFlagFlagged);
                if (!msg->isUnread())
                    flags = (MessageFlag)(flags | MessageFlagSeen);
                if (msg->isDraft())
                    flags = (MessageFlag)(flags | MessageFlagDraft);
        
                if (flags != MessageFlagNone) {
                    session->storeFlagsByUID(destPath, IndexSet::indexSetWithIndex(msg->remoteUID()), IMAPStoreFlagsRequestKindSet, flags, &err);
                }
            }
        }
    }
}

// A helper function to permanently remove messages by UID from a given folder path. When a trash folder
// and CapabilityMove are present, it moves there and expunges. Otherwises it expunges in place.

void _removeMessagesResilient(IMAPSession * session, MailStore * store, string accountId, String * path, IndexSet * uids) {
    ErrorCode err = ErrorCode::ErrorNone;

    // First, add the "DELETED" flag to the given messages
    session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindAdd, MessageFlagDeleted, &err);
    if (err != ErrorNone) {
        spdlog::get("logger")->info("X- removeMessages could not add deleted flag (error: {})", ErrorCodeToTypeMap[err]);
        return;
    }
    
    // If possible, move the messages to the identified trash folder.
    // Sometimes [on Gmail] this is necessary to properly mark them as deleted.
    String * trashPath = nullptr;
    if (session->storedCapabilities()->containsIndex(IMAPCapabilityMove)) {
        auto trash = store->find<Folder>(Query().equal("accountId", accountId).equal("role", "trash"));
        if (trash != nullptr) trashPath = AS_MCSTR(trash->path());
    }

    if (trashPath != nullptr) {
        HashMap * uidMapping = nullptr;
        session->moveMessages(path, uids, trashPath, &uidMapping, &err);
        if (err != ErrorNone) {
            spdlog::get("logger")->info("X- removeMessages could not move to {} (error: {})", trashPath->UTF8Characters(), ErrorCodeToTypeMap[err]);
        } else {
            // If we were successful moving to the trash, we will now expunge from here, and the UIDs
            // we had before are no longer valid so we'll need to expunge the entire folder.
            uids->removeAllIndexes();
            path = trashPath;
            
            // If we got a UID mapping back, we can make an Expunge UIDs request for the specific deleted UIDs.
            // We also re-flag them as deleted because Gmail removes the Deleted attribute when the items are moved.
            if (uidMapping) {
                Array * uidsInNewFolder = uidMapping->allValues();
                for (unsigned int ii = 0; ii < uidsInNewFolder->count(); ii ++) {
                    Value * val = (Value *)uidsInNewFolder->objectAtIndex(ii);
                    uids->addIndex(val->unsignedLongValue());
                }
                spdlog::get("logger")->info("-- removeMessages re-applying deleted flag after moving to {}", trashPath->UTF8Characters());
                session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindAdd, MessageFlagDeleted, &err);
                if (err != ErrorNone) {
                    spdlog::get("logger")->info("X- removeMessages could not add deleted flag (error: {})", ErrorCodeToTypeMap[err]);
                    err = ErrorNone;
                }
            }
        }
    }
    
    if (uids->count() > 0) {
        spdlog::get("logger")->info("-- removeMessages Expunging (UIDs) from {}", path->UTF8Characters());
        session->expungeUIDs(path, uids, &err);
        if (err != ErrorNone) {
            spdlog::get("logger")->info("-- removeMessages Expunge (UIDs) failed (error: {})", ErrorCodeToTypeMap[err]);
            spdlog::get("logger")->info("-- removeMessages Expunging (Basic) from {}", path->UTF8Characters());
            err = ErrorNone;
            session->expunge(path, &err);
        }
    } else {
        spdlog::get("logger")->info("-- removeMessages Expunging (Basic) from {}", path->UTF8Characters());
        session->expunge(path, &err);
    }

    if (err != ErrorNone) {
        spdlog::get("logger")->info("X- removeMessages Expunge failed (error: {})", ErrorCodeToTypeMap[err]);
    }
}


// Small functions that we pass to the generic ChangeMessages runner

void _applyUnread(Message * msg, json & data) {
    msg->setUnread(data["unread"].get<bool>());
}

void _applyUnreadInIMAPFolder(IMAPSession * session, String * path, IndexSet * uids, vector<shared_ptr<Message>> messages, json & data) {
    ErrorCode err = ErrorCode::ErrorNone;
    if (data["unread"].get<bool>() == false) {
        session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindAdd, MessageFlagSeen, &err);
    } else {
        session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindRemove, MessageFlagSeen, &err);
    }
    if (err != ErrorCode::ErrorNone) {
        throw SyncException(err, "storeFlagsByUID");
    }
}

void _applyStarred(Message * msg, json & data) {
    msg->setStarred(data["starred"].get<bool>());
}

void _applyStarredInIMAPFolder(IMAPSession * session, String * path, IndexSet * uids, vector<shared_ptr<Message>> messages, json & data) {
    ErrorCode err = ErrorCode::ErrorNone;
    if (data["starred"].get<bool>() == true) {
        session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindAdd, MessageFlagFlagged, &err);
    } else {
        session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindRemove, MessageFlagFlagged, &err);
    }
    if (err != ErrorCode::ErrorNone) {
        throw SyncException(err, "storeFlagsByUID");
    }
}

void _applyFolder(Message * msg, json & data) {
    Folder folder{data["folder"]};
    msg->setClientFolder(&folder);
}

void _applyFolderMoveInIMAPFolder(IMAPSession * session, String * path, IndexSet * uids, vector<shared_ptr<Message>> messages, json & data) {
    Folder destFolder{data["folder"]};
    
    _moveMessagesResilient(session, path, &destFolder, uids, messages);
}

string _xgmKeyForLabel(json & label) {
    string role = label["role"].get<string>();
    string path = label["path"].get<string>();
    if (role == "inbox") {
        return "\\Inbox";
    }
    if (role == "important") {
        return "\\Important";
    }
    return path;
}

void _applyLabels(Message * msg, json & data) {
    json & toAdd = data["labelsToAdd"];
    json & toRemove = data["labelsToRemove"];
    json & labels = msg->remoteXGMLabels();
    
    for (auto & item : toAdd) {
        string xgmValue = _xgmKeyForLabel(item);
        bool found = false;
        for (auto & existing : labels) {
            if (existing.get<string>() == xgmValue) {
                found = true;
                break;
            }
        }
        if (found == false) {
            labels.push_back(xgmValue);
        }
    }
    for (auto & item : toRemove) {
        string xgmValue = _xgmKeyForLabel(item);
        for (int i = (int)labels.size() - 1; i >= 0; i --) {
            if (labels.at(i).get<string>() == xgmValue) {
                labels.erase(i);
            }
        }
    }
    msg->setRemoteXGMLabels(labels);
}

void _applyLabelChangeInIMAPFolder(IMAPSession * session, String * path, IndexSet * uids, vector<shared_ptr<Message>> messages, json & data) {
    AutoreleasePool pool;

    ErrorCode err = ErrorCode::ErrorNone;
    Array * toAdd = new mailcore::Array{};
    toAdd->autorelease();
    for (auto & item : data["labelsToAdd"]) {
        toAdd->addObject(AS_MCSTR(_xgmKeyForLabel(item)));
    }

    Array * toRemove = new mailcore::Array{};
    toRemove->autorelease();
    for (auto & item : data["labelsToRemove"]) {
        toRemove->addObject(AS_MCSTR(_xgmKeyForLabel(item)));
    }
    
    if (toAdd->count() > 0) {
        session->storeLabelsByUID(path, uids, IMAPStoreFlagsRequestKindAdd, toAdd, &err);
        if (err != ErrorCode::ErrorNone) {
            throw SyncException(err, "storeLabelsByUID - add");
        }
    }
    if (toRemove->count() > 0) {
        session->storeLabelsByUID(path, uids, IMAPStoreFlagsRequestKindRemove, toRemove, &err);
        if (err != ErrorCode::ErrorNone) {
            throw SyncException(err, "storeLabelsByUID - remove");
        }
    }
}


TaskProcessor::TaskProcessor(shared_ptr<Account> account, MailStore * store, IMAPSession * session) :
    account(account),
    store(store),
    logger(spdlog::get("logger")),
    session(session) {
}

void TaskProcessor::cleanupTasksAfterLaunch() {
    // look for tasks that are in the `local` state. The app most likely crashed while running these
    // tasks, since they're saved immediately before performLocal is run. Delete them to avoid
    // the app crashing again.
    auto stuck = store->findAll<Task>(Query().equal("accountId", account->id()).equal("status", "local"));
    for (auto & t : stuck) {
        store->remove(t.get());
    }

    cleanupOldTasksAtRuntime();
}

void TaskProcessor::cleanupOldTasksAtRuntime() {
    // Keep only the last 100 completed / cancelled tasks
    //
    // Note: We need to load and then delete these rather than using a DELETE query
    // because the app observes the entire Task queue using a QuerySubscription, and
    // if we delete them from under it, it won't release them from the array.
    //
    SQLite::Statement count(store->db(), "SELECT COUNT(id) FROM Task WHERE accountId = ? AND (status = \"complete\" OR status = \"cancelled\")");
    count.bind(1, account->id());
    count.executeStep();
    int countToRemove = count.getColumn(0).getInt() - 100;
    count.reset();

    if (countToRemove > 10) { // slop
        MailStoreTransaction transaction{store, "cleanupOldTasksAtRuntime"};

        SQLite::Statement unneeded(store->db(), "SELECT data FROM Task WHERE accountId = ? AND (status = \"complete\" OR status = \"cancelled\") ORDER BY rowid ASC LIMIT ?");
        unneeded.bind(1, account->id());
        unneeded.bind(2, countToRemove);
        vector<shared_ptr<Task>> unneededTasks{};
        while (unneeded.executeStep()) {
            unneededTasks.push_back(make_shared<Task>(unneeded));
        }
        unneeded.reset();
        for (auto & t : unneededTasks) {
            store->remove(t.get());
        }
        
        transaction.commit();
    }
}

// PerformLocal is run from the main thread as tasks are received from the client

void TaskProcessor::performLocal(Task * task) {
    string cname = task->constructorName();
    
    logger->info("[{}] Running {} performLocal:", task->id(), cname);

    try {
        store->save(task);
    } catch (SQLite::Exception & ex) {
        logger->error("[{}] -- Exception: Task could not be saved to the database. {}", task->id(), ex.what());
        return;
    }

    try {
        if (task->accountId() != account->id()) {
            throw SyncException("generic", "You must provide an account id.", false);
        }

        if (cname == "ChangeUnreadTask") {
            performLocalChangeOnMessages(task, _applyUnread);
            
        } else if (cname == "ChangeStarredTask") {
            performLocalChangeOnMessages(task, _applyStarred);

        } else if (cname == "ChangeFolderTask") {
            performLocalChangeOnMessages(task, _applyFolder);
            
        } else if (cname == "ChangeLabelsTask") {
            performLocalChangeOnMessages(task, _applyLabels);
        
        } else if (cname == "SyncbackDraftTask") {
            performLocalSaveDraft(task);
            
        } else if (cname == "DestroyDraftTask") {
            performLocalDestroyDraft(task);
            
        } else if (cname == "SyncbackCategoryTask") {
            performLocalSyncbackCategory(task);
            
        } else if (cname == "DestroyCategoryTask") {
            // nothing

        } else if (cname == "SendDraftTask") {
            // nothing

        } else if (cname == "SyncbackMetadataTask") {
            performLocalSyncbackMetadata(task);
            
        } else if (cname == "SendFeatureUsageEventTask") {
            // nothing
        
        } else if (cname == "ChangeRoleMappingTask") {
            performLocalChangeRoleMapping(task);
        
        } else if (cname == "ExpungeAllInFolderTask") {
            // nothing

        } else if (cname == "GetMessageRFC2822Task") {
            // nothing

        } else if (cname == "EventRSVPTask") {
            // nothing

        } else if (cname == "DestroyContactTask") {
            performLocalDestroyContact(task);

        } else if (cname == "SyncbackContactTask") {
            performLocalSyncbackContact(task);

        } else if (cname == "ChangeContactGroupMembershipTask") {
            performLocalChangeContactGroupMembership(task);

        } else if (cname == "SyncbackContactGroupTask") {
            performLocalSyncbackContactGroup(task);

        } else if (cname == "DestroyContactGroupTask") {
            performLocalDestroyContactGroup(task);

        } else {
            logger->error("Unsure of how to process this task type {}", cname);
        }

        logger->info("[{}] -- Succeeded. Changing status to `remote`", task->id());
        task->setStatus("remote");

    } catch (SyncException & ex) {
        logger->error("[{}] -- Failed ({}). Changing status to `complete`", task->id(), ex.toJSON().dump());
        logger->flush();
        task->setError(ex.toJSON());
        task->setStatus("complete");
    }
    
    store->save(task);
}

// PerformRemote is run from the foreground worker

void TaskProcessor::performRemote(Task * task) {
    string cname = task->constructorName();

    logger->info("[{}] Running {} performRemote:", task->id(), cname);
    
    try {
        if (task->accountId() != account->id()) {
            throw SyncException("generic", "You must provide an account id.", false);
        }
        if (task->shouldCancel()) {
            task->setStatus("cancelled");
        } else {
            if (cname == "ChangeUnreadTask") {
                performRemoteChangeOnMessages(task, false, _applyUnreadInIMAPFolder);
                
            } else if (cname == "ChangeStarredTask") {
                performRemoteChangeOnMessages(task, false, _applyStarredInIMAPFolder);
                
            } else if (cname == "ChangeFolderTask") {
                performRemoteChangeOnMessages(task, true, _applyFolderMoveInIMAPFolder);

            } else if (cname == "ChangeLabelsTask") {
                performRemoteChangeOnMessages(task, false, _applyLabelChangeInIMAPFolder);
                
            } else if (cname == "SyncbackDraftTask") {
                // right now we don't syncback drafts
                
            } else if (cname == "DestroyDraftTask") {
                performRemoteDestroyDraft(task);

            } else if (cname == "SyncbackCategoryTask") {
                performRemoteSyncbackCategory(task);
                
            } else if (cname == "DestroyCategoryTask") {
                performRemoteDestroyCategory(task);
            
            } else if (cname == "SendDraftTask") {
                performRemoteSendDraft(task);
                
            } else if (cname == "SyncbackMetadataTask") {
                performRemoteSyncbackMetadata(task);

            } else if (cname == "SendFeatureUsageEventTask") {
                performRemoteSendFeatureUsageEvent(task);

            } else if (cname == "ChangeRoleMappingTask") {
                // no-op

            } else if (cname == "ExpungeAllInFolderTask") {
                performRemoteExpungeAllInFolder(task);

            } else if (cname == "GetMessageRFC2822Task") {
                performRemoteGetMessageRFC2822(task);

            } else if (cname == "EventRSVPTask") {
                performRemoteSendRSVP(task);
                
            } else if (cname == "DestroyContactTask") {
                performRemoteDestroyContact(task);

            } else if (cname == "SyncbackContactTask") {
                performRemoteSyncbackContact(task);

            } else if (cname == "ChangeContactGroupMembershipTask") {
                performRemoteChangeContactGroupMembership(task);

            } else if (cname == "SyncbackContactGroupTask") {
                performRemoteSyncbackContactGroup(task);

            } else if (cname == "DestroyContactGroupTask") {
                performRemoteDestroyContactGroup(task);

            } else {
                logger->error("Unsure of how to process this task type {}", cname);
            }

            logger->info("[{}] -- Succeeded. Changing status to `complete`", task->id());
            task->setStatus("complete");
        }
    } catch (SyncException & ex) {
        logger->error("[{}] -- Failed ({}). Changing status to `complete`", task->id(), ex.toJSON().dump());
        logger->flush();
        task->setError(ex.toJSON());
        task->setStatus("complete");
    }
    store->save(task);
}

void TaskProcessor::cancel(string taskId) {
    MailStoreTransaction transaction{store, "cancel"};
    auto task = store->find<Task>(Query().equal("id", taskId).equal("accountId", account->id()));
    if (task != nullptr) {
        task->setShouldCancel();
        store->save(task.get());
    }
    transaction.commit();
}

#pragma mark Privates

Message TaskProcessor::inflateClientDraftJSON(json & draftJSON, shared_ptr<Message> existing = nullptr) {
    // set other JSON attributes the client may not have populated, but we require to be non-null
    
    // Note BG 2019 - I don't know why this is so defensive. I guess these objects JSON is created client-side
    // and we don't want the C++ to need to trust that the JS and the C++ are exactly aligned?
    
    // Followup: This is because the client only serializes the fields it's aware of, so remoteUID, etc.
    // ARE actually missing.
    
    json base;
    if (existing) {
        base = existing->_data;
    } else {
        Query q = Query().equal("accountId", account->id()).equal("role", "drafts");
        auto folder = store->find<Folder>(q);
        if (folder.get() == nullptr) {
            q = Query().equal("accountId", account->id()).equal("role", "all");
            folder = store->find<Folder>(q);
        }
        if (folder == nullptr) {
            throw SyncException("no-drafts-folder", "Mailspring can't find your Drafts folder. To create and send mail, visit Preferences > Folders and choose a Drafts folder.", false);
        }
        base = {
            {"remoteUID", 0},
            {"draft", true},
            {"unread", false},
            {"starred", false},
            {"folder", folder->toJSON()},
            {"remoteFolder", folder->toJSON()},
            {"date", time(0)},
            {"_sa", 0},
            {"_suc", 0},
            {"labels", json::array()},
            {"id", MailUtils::idForDraftHeaderMessageId(draftJSON["aid"], draftJSON["hMsgId"])},
            {"threadId", ""},
            {"gMsgId", ""},
            {"files", json::array()},
            {"from", json::array()},
            {"to", json::array()},
            {"cc", json::array()},
            {"bcc", json::array()},
            {"replyTo", json::array()},
        };
    }

    // Take the base values (either our local copy of the draft with our metadata, or a new stub) and smash in
    // the values provided by the client. This allows us to retain the actual remoteUID, remoteFolder, etc. if
    // the draft is synced remotely.
    
    draftJSON.insert(base.begin(), base.end());

    // Always update the timestamp
    draftJSON["date"] = time(0);
    
    auto msg = Message{draftJSON};
    
    if (msg.accountId() != account->id()) {
        throw SyncException("bad-accountid", "The draft in this task has the wrong account ID.", false);
    }

    return msg;
}

ChangeMailModels TaskProcessor::inflateMessages(json & data) {
    ChangeMailModels models;

    if (data.count("threadIds")) {
        vector<string> threadIds{};
        for (auto & member : data["threadIds"]) {
            threadIds.push_back(member.get<string>());
        }
        models.messages = store->findLargeSet<Message>("threadId", threadIds);

    } else if (data.count("messageIds")) {
        vector<string> messageIds{};
        for (auto & member : data["messageIds"]) {
            messageIds.push_back(member.get<string>());
        }
        models.messages = store->findLargeSet<Message>("id", messageIds);
    }
    
    return models;
}

void TaskProcessor::performLocalChangeOnMessages(Task * task, void (*modifyLocalMessage)(Message *, json &)) {
    MailStoreTransaction transaction{store, "performLocalChangeOnMessages"};
    
    json & data = task->data();
    ChangeMailModels models = inflateMessages(data);
    bool recomputeThreadAttributes = data.count("threadIds");
    
    for (auto msg : models.messages) {
        // TEMPORARY
        if (recomputeThreadAttributes) {
            msg->_skipThreadUpdatesAfterSave = true;
        }
        
        // perform local changes
        modifyLocalMessage(msg.get(), data);

        // prevent remote changes to this message for 24 hours
        // so the changes aren't reverted by sync before we can syncback.
        msg->setSyncUnsavedChanges(msg->syncUnsavedChanges() + 1);
        msg->setSyncedAt(time(0) + 24 * 60 * 60);

        store->save(msg.get());
    }

    // TEMPORARY
    // if we were given a set of threadIds, we might as well rebalance the counters
    // and correct any refcounting issues the user may be seeing, since we already
    // have all the messages in memory.
    if (recomputeThreadAttributes) {
        vector<string> threadIds{};
        for (auto & member : data["threadIds"]) {
            threadIds.push_back(member.get<string>());
        }
        auto chunks = MailUtils::chunksOfVector(threadIds, 500);
        auto allLabels = store->allLabelsCache(task->accountId());

        for (auto chunk : chunks) {
            auto threads = store->findAllMap<Thread>(Query().equal("id", chunk), "id");

            for (auto pair : threads) {
                pair.second->resetCountedAttributes();
            }
            for (auto msg : models.messages) {
                if (threads.count(msg->threadId())) {
                    threads[msg->threadId()]->applyMessageAttributeChanges(MessageEmptySnapshot, msg.get(), allLabels);
                }
            }
            for (auto pair : threads) {
                store->save(pair.second.get());
            }
        }
    }
    // END TEMPORARY

    transaction.commit();
}

void TaskProcessor::performRemoteChangeOnMessages(Task * task, bool updatesFolder, void (*applyInFolder)(IMAPSession * session, String * path, IndexSet * uids, vector<shared_ptr<Message>> messages, json & data)) {
    // Perform the remote action on the impacted messages
    json & data = task->data();
    
    // Grab the messages, group into folders, and perform the remote changes.
    // Note that we reload the messages to update them locally because
    // this code does I/O and is not inside a transaction! Other task
    // performLocal calls could be happening at the same time.
    vector<shared_ptr<Message>> messages = inflateMessages(data).messages;
    map<string, shared_ptr<IndexSet>> uidsByFolder{};
    map<string, vector<shared_ptr<Message>>> msgsByFolder{};
    map<string, shared_ptr<Message>> messagesById{};

    for (auto msg : messages) {
        string path = msg->remoteFolder()["path"].get<string>();
        uint32_t uid = msg->remoteUID();
        if (!uidsByFolder.count(path)) {
            uidsByFolder[path] = make_shared<IndexSet>();
            msgsByFolder[path] = {};
        }
        uidsByFolder[path]->addIndex(uid);
        msgsByFolder[path].push_back(msg);
        messagesById[msg->id()] = msg;
    }
    
    for (auto pair : msgsByFolder) {
        auto & msgs = pair.second;
        IndexSet * uids = uidsByFolder[pair.first].get();
        
        // perform the action. NOTE! This function is allowed to mutate the messages,
        // for example to set their remoteUID after a move.
        applyInFolder(session, AS_MCSTR(pair.first), uids, msgs, data);
    }
    
    // Reload the messages inside a transaction, save any changes made to "remote" attributes
    // by applyInFolder and decrement locks.
    {
        MailStoreTransaction transaction{store, "performRemoteChangeOnMessages"};
        vector<shared_ptr<Message>> safeMessages = inflateMessages(data).messages;
        
        for (auto safe : safeMessages) {
            if (!messagesById.count(safe->id())) {
                logger->info("-- Could not find msg {} to apply remote changes", safe->id());
                continue;
            }
            auto unsafe = messagesById[safe->id()];

            // NOTE: We only want to apply the attributes the `applyInFolder` method modifies
            // to avoid overwriting other changes that may have happened. For example, running
            // performRemote on a ChangeUnreadTask shouldn't set setRemoteFolder.
            if (updatesFolder) {
                safe->setRemoteUID(unsafe->remoteUID());
                safe->setRemoteFolder(unsafe->remoteFolder());
            }
            int suc = safe->syncUnsavedChanges() - 1;
            safe->setSyncUnsavedChanges(suc);
            if (suc == 0) {
                safe->setSyncedAt(time(0));
            }
            store->save(safe.get());
        }
        // We know we do not need to `emit` this change, because it's all internal fields and
        // remote values, not things reflected in the client. This is good for perf because
        // the client does silly things like refresh MessageItems when new versions arrive.
        store->unsafeEraseTransactionDeltas();
        transaction.commit();
    }
}

void TaskProcessor::performLocalSaveDraft(Task * task) {
    json & draftJSON = task->data()["draft"];
    
    {
        MailStoreTransaction transaction{store, "performLocalSaveDraft"};

        // If the draft already exists, we need to visibly change it's attributes
        // to trigger the correct didSave hooks, etc. Find and update it.
        shared_ptr<Message> existing = nullptr;
        if (draftJSON.count("id")) {
            existing = store->find<Message>(Query().equal("id", draftJSON["id"].get<string>()));
        }

        Message draft = inflateClientDraftJSON(draftJSON, existing);

        if (existing) {
            // NOTE: to accept all changes we just swap the data BUT, the new data
            // may have an outdated version and the version dicates whether we
            // INSERT or UPDATE. It's critical we bump the version of `existing`.
            int existingVersion = existing->version();
            existing->_data = draft._data;
            existing->_data["v"] = existingVersion + 1;
            store->save(existing.get());
        } else {
            store->save(&draft);
        }

        if (draftJSON.count("body")) {
            SQLite::Statement insert(store->db(), "REPLACE INTO MessageBody (id, value) VALUES (?, ?)");
            insert.bind(1, draft.id());
            insert.bind(2, draftJSON["body"].get<string>());
            insert.exec();
        }
        transaction.commit();
    }
}

void TaskProcessor::performLocalDestroyDraft(Task * task) {
    vector<string> messageIds = task->data()["messageIds"];

    logger->info("-- Hiding / detatching drafts while they're deleted...");
    
    // Find the trash folder
    auto trash = store->find<Folder>(Query().equal("accountId", account->id()).equal("role", "trash"));
    if (trash == nullptr) {
        throw SyncException("no-trash-folder", "Mailspring doesn't know which folder to use for trash. Visit Preferences > Folders to assign a trash folder.", false);
    }

    auto stubIds = json::array();
    
    // we need to free up the draft ID immediately because the user can
    // switch a draft between accounts, and we may need to re-create a
    // new draft with the same acctId + hMsgId combination.
    
    // Destroy drafts locally and create stubs that prevent the sync
    // worker from replacing them while we delete them via IMAP.
    {
        MailStoreTransaction transaction{store, "performLocalDestroyDraft"};

        auto drafts = store->findLargeSet<Message>("id", messageIds);
        for (auto & draft : drafts) {
            store->remove(draft.get());
            
            auto stub = Message::messageWithDeletionPlaceholderFor(draft);
            stub->setClientFolder(trash.get());
            store->save(stub.get());
            stubIds.push_back(stub->id());

            logger->info("-- Replacing local ID {} with {}", draft->id(), stub->id());
        }

        transaction.commit();
    }
    task->data()["stubIds"] = stubIds;
}

void TaskProcessor::performRemoteDestroyDraft(Task * task) {
    vector<string> stubIds = task->data()["stubIds"];
    auto stubs = store->findLargeSet<Message>("id", stubIds);


    for (auto & stub : stubs) {
        if (stub->remoteUID() == 0) {
            continue; // not synced to server at all
        }
        auto uids = IndexSet::indexSetWithIndex(stub->remoteUID());
        String * path = AS_MCSTR(stub->remoteFolder()["path"].get<string>());

        logger->info("-- Deleting remote draft {}", stub->id());
        _removeMessagesResilient(session, store, account->id(), path, uids);

        // remove the stub from our local cache - would eventually get removed
        // during sync, but we don't want to fetch it's body or anything
        store->remove(stub.get());
    }
}

void TaskProcessor::performLocalDestroyContact(Task * task) {
    vector<string> contactIds {};
    for (json & c : task->data()["contacts"]) {
        contactIds.push_back(c["id"].get<string>());
    }

    {
        store->beginTransaction();
        auto deleted = store->findLargeSet<Contact>("id", contactIds);
        for (auto & c : deleted) {
            c->setHidden(true);
            store->save(c.get());
        }
        store->commitTransaction();
    }
}

void TaskProcessor::performRemoteDestroyContact(Task * task) {
    vector<string> contactIds {};
    for (json & c : task->data()["contacts"]) {
        contactIds.push_back(c["id"].get<string>());
    }

    if (account->provider() == "gmail") {
        auto deleted = store->findLargeSet<Contact>("id", contactIds);
        auto gpeople = make_shared<GoogleContactsWorker>(account);
        for (auto & contact : deleted) {
            gpeople->deleteContact(contact);
        }
    } else {

        auto deleted = store->findLargeSet<Contact>("id", contactIds);
        auto dav = make_shared<DAVWorker>(account);
        for (auto & contact : deleted) {
            dav->deleteContact(contact);
        }
    }
}

void TaskProcessor::performLocalSyncbackContactGroup(Task * task) {
    string id = task->data()["group"].count("id") ? task->data()["group"]["id"].get<string>() : "";
    string name = task->data()["group"]["name"].get<string>();
    shared_ptr<ContactBook> book = store->find<ContactBook>(Query().equal("accountId", account->id()));

    if (account->provider() == "gmail") {
        // Create or update ContactGroup
        if (id == "") {
            id = MailUtils::idRandomlyGenerated();
            task->data()["group"]["id"] = id;
            store->save(task);
        }
        auto local = store->find<ContactGroup>(Query().equal("id", id));
        if (!local) {
            local = make_shared<ContactGroup>(id, account->id());
        }
        local->setBookId(book->id());
        local->setName(name);
        store->save(local.get());
        
    } else {
        if (id != "") {
            // Update ContactGroup
            auto existing = store->find<ContactGroup>(Query().equal("id", id));
            if (!existing) {
                return;
            }
            existing->setName(name);
            store->save(existing.get());
            
            // Update underlying contact and VCF
            auto contact = store->find<Contact>(Query().equal("id", id));
            contact->setName(name);
            contact->mutateCardInInfo([&](shared_ptr<VCard> card) {
                if (card->getName()) {
                    card->getName()->setValue(name);
                }
                if (card->getFormattedName()) {
                    card->getFormattedName()->setValue(name);
                }
            });
            store->save(contact.get());
        } else {
            // Create vcf and autogen Contact and ContactGroup
            auto uid = MailUtils::idRandomlyGenerated();
            auto contact = make_shared<Contact>(uid, account->id(), "", CONTACT_MAX_REFS, CARDDAV_SYNC_SOURCE);
            contact->setInfo(json::object({{"vcf", "BEGIN:VCARD\r\nVERSION:3.0\r\nUID:"+uid+"\r\nEND:VCARD\r\n"}, {"href", ""}}));
            contact->setHidden(true);
            contact->setName(name);
            contact->mutateCardInInfo([&](shared_ptr<VCard> vcard) {
                vcard->setName(name);
                vcard->addProperty(make_shared<VCardProperty>("FN", name));
                vcard->addProperty(make_shared<VCardProperty>(X_VCARD3_KIND, "group"));
            });

            task->data()["group"]["id"] = uid;
            store->save(task);

            store->save(contact.get());
            auto dav = make_shared<DAVWorker>(account);
            dav->rebuildContactGroup(contact);
        }
    }
}

void TaskProcessor::performRemoteSyncbackContactGroup(Task * task) {
    string id = task->data()["group"].count("id") ? task->data()["group"]["id"].get<string>() : "";
    if (id == "") {
        logger->error("performRemoteSyncbackContactGroup: Group did not get assigned an ID.");
        return;
    }

    if (account->provider() == "gmail") {
        auto group = store->find<ContactGroup>(Query().equal("id", id));
        auto gpeople = make_shared<GoogleContactsWorker>(account);
        gpeople->upsertContactGroup(group);
    } else {
        auto contact = store->find<Contact>(Query().equal("id", id));
        auto dav = make_shared<DAVWorker>(account);
        dav->writeAndResyncContact(contact);
    }
}

void TaskProcessor::performLocalDestroyContactGroup(Task * task) {
    string id = task->data()["group"]["id"].get<string>();
    auto deleted = store->find<ContactGroup>(Query().equal("id", id));
    if (!deleted) return;
    
    if (account->provider() == "gmail") {
        task->data()["googleResourceName"] = deleted->googleResourceName();
        store->save(task);
    }
    store->remove(deleted.get());
}

void TaskProcessor::performRemoteDestroyContactGroup(Task * task) {
    string id = task->data()["group"]["id"].get<string>();

    if (account->provider() == "gmail") {
        if (!task->data().count("googleResourceName")) {
            logger->error("performRemoteDestroyContactGroup: Group did not have a googleResourceName.");
            return;
        }
        auto resourceName = task->data()["googleResourceName"].get<string>();
        auto gpeople = make_shared<GoogleContactsWorker>(account);
        gpeople->deleteContactGroup(resourceName);
    } else {
        auto contact = store->find<Contact>(Query().equal("id", id));
        if (!contact) return;
        auto dav = make_shared<DAVWorker>(account);
        dav->deleteContact(contact);
    }
}


void TaskProcessor::performLocalChangeContactGroupMembership(Task * task) {
    vector<string> contactIds {};
    for (json & c : task->data()["contacts"]) {
        contactIds.push_back(c["id"].get<string>());
    }
    auto contacts = store->findLargeSet<Contact>("id", contactIds);
    auto direction = task->data()["direction"].get<string>();
    auto groupId = task->data()["group"]["id"].get<string>();
    
    if (account->provider() == "gmail") {
        auto group = store->find<ContactGroup>(Query().equal("id", groupId));
        auto groupMemberIds = group->getMembers(store);
        if (direction == "add") {
            groupMemberIds.insert(groupMemberIds.end(), contactIds.begin(), contactIds.end());
        } else {
            for (auto id : contactIds) {
                auto pos = std::find(groupMemberIds.begin(), groupMemberIds.end(), id);
                if (pos != groupMemberIds.end()) groupMemberIds.erase(pos);
            }
        }
        group->syncMembers(store, groupMemberIds);

    } else {
        auto contactForGroup = store->find<Contact>(Query().equal("id", groupId).equal("accountId", account->id()));
       
        contactForGroup->mutateCardInInfo([&](shared_ptr<VCard> card) {
            if (direction == "add") {
                DAVUtils::addMembersToGroupCard(card, contacts);
            } else {
                DAVUtils::removeMembersFromGroupCard(card, contacts);
            }
        });
        
        auto dav = make_shared<DAVWorker>(account);
        dav->rebuildContactGroup(contactForGroup);
        store->save(contactForGroup.get());
    }
}


void TaskProcessor::performRemoteChangeContactGroupMembership(Task * task) {
    auto groupId = task->data()["group"]["id"].get<string>();
    
    if (account->provider() == "gmail") {
        auto direction = task->data()["direction"].get<string>();
        vector<string> contactIds {};
        for (json & c : task->data()["contacts"]) {
            contactIds.push_back(c["id"].get<string>());
        }
        auto contacts = store->findLargeSet<Contact>("id", contactIds);
        auto group = store->find<ContactGroup>(Query().equal("id", groupId));
        auto gpeople = make_shared<GoogleContactsWorker>(account);
        gpeople->updateContactGroupMembership(group, contacts, direction);

    } else {
        auto contactForGroup = store->find<Contact>(Query().equal("id", groupId).equal("accountId", account->id()));
        auto dav = make_shared<DAVWorker>(account);
        dav->writeAndResyncContact(contactForGroup);
    }
}


void TaskProcessor::performLocalSyncbackContact(Task * task) {
    auto clientside = make_shared<Contact>(task->data()["contact"]);
    auto source = account->provider() == "gmail" ? GOOGLE_SYNC_SOURCE : CARDDAV_SYNC_SOURCE;
    if (clientside->source() != "" && clientside->source() != source) {
        logger->error("performLocalSyncbackContact: Client picked incorrect source for new contact: {} != {}", source, clientside->source());
        return;
    }

    auto local = task->data()["contact"].count("id")
        ? store->find<Contact>(Query().equal("id", clientside->id()))
        : make_shared<Contact>(MailUtils::idRandomlyGenerated(), account->id(), "", CONTACT_MAX_REFS, source);

    // Note: The client may not be aware of all of the key/value pairs we store in contact JSON,
    // so it's JSON in the task may omit some properties. To make sure we don't damage the
    // contact, find and update only the allowed attributes.
    local->setInfo(clientside->info());
    local->setName(clientside->name());
    local->setEmail(clientside->email());
    store->save(local.get());
    
    task->data()["contact"]["id"] = local->id();
    store->save(task);
}

void TaskProcessor::performRemoteSyncbackContact(Task * task) {
    string id = task->data()["contact"]["id"].get<string>();
    auto contact = store->find<Contact>(Query().equal("id", id).equal("accountId", account->id()));

    if (contact->source() == CONTACT_SOURCE_MAIL) {
        return;
    }

    if (account->provider() == "gmail") {
        auto gpeople = make_shared<GoogleContactsWorker>(account);
        gpeople->upsertContact(contact);
    } else {
        auto dav = make_shared<DAVWorker>(account);
        dav->writeAndResyncContact(contact);
    }
}


void TaskProcessor::performLocalSyncbackCategory(Task * task) {
    
}

void TaskProcessor::performRemoteSyncbackCategory(Task * task) {
    json & data = task->data();
    string accountId = task->accountId();
    string path = data["path"].get<string>();
    string existingPath = data.count("existingPath") ? data["existingPath"].get<string>() : "";

    // if the requested path includes "/" delimiters, replace them with the real delimiter
    char delimiter = session->defaultNamespace()->mainDelimiter();
    std::replace(path.begin(), path.end(), '/', delimiter);

    // if the requested path is missing the namespace prefix, add it
    // note: the prefix may or may not end with the delimiter character
    string mainPrefix = MailUtils::namespacePrefixOrBlank(session);
    if (mainPrefix != "" && path.find(mainPrefix) != 0) {
        if (mainPrefix[mainPrefix.length() - 1] == delimiter) {
            path = mainPrefix + path;
        } else {
            path = mainPrefix + delimiter + path;
        }
    }
    
    ErrorCode err = ErrorCode::ErrorNone;

    if (existingPath != "") {
        session->renameFolder(AS_MCSTR(existingPath), AS_MCSTR(path), &err);
    } else {
        session->createFolder(AS_MCSTR(path), &err);
    }
    
    if (err != ErrorNone) {
        data["created"] = nullptr;
        logger->error("Syncback of folder/label '{}' failed.", path);
        throw SyncException(err, "create/renameFolder");
    }
    
    // must go beneath the first use of session above.
    bool isGmail = session->storedCapabilities()->containsIndex(IMAPCapabilityGmail);
    shared_ptr<Folder> localModel = nullptr;
    
    if (existingPath != "") {
        if (isGmail) {
            localModel = store->find<Label>(Query().equal("accountId", accountId).equal("id", MailUtils::idForFolder(accountId, existingPath)));
        } else {
            localModel = store->find<Folder>(Query().equal("accountId", accountId).equal("id", MailUtils::idForFolder(accountId, existingPath)));
        }
    }

    if (!localModel) {
        if (isGmail) {
            localModel = make_shared<Label>(MailUtils::idForFolder(accountId, path), accountId, 0);
        } else {
            localModel = make_shared<Folder>(MailUtils::idForFolder(accountId, path), accountId, 0);
        }
    }

    localModel->setPath(path);
    data["created"] = localModel->toJSON();
    store->save(localModel.get());
    
    logger->info("Syncback of folder/label '{}' succeeded.", path);
}


void TaskProcessor::performLocalSyncbackMetadata(Task * task) {
    json & data = task->data();
    string aid = task->accountId();
    string id = data["modelId"];
    string type = data["modelClassName"];
    string pluginId = data["pluginId"];
    json & value = data["value"];
    
    {
        MailStoreTransaction transaction{store, "performLocalSyncbackMetadata"};
        
        auto model = store->findGeneric(type, Query().equal("id", id).equal("accountId", aid));
        if (model) {
            int metadataVersion = model->upsertMetadata(pluginId, value);
            data["modelMetadataNewVersion"] = metadataVersion;
            store->save(model.get());
        } else {
            logger->info("cannot apply metadata locally because no model matching type:{} id:{}, aid:{} could be found.", type, id, aid);
        }

        transaction.commit();
    }
}


void TaskProcessor::performRemoteSyncbackMetadata(Task * task) {
    if (Identity::GetGlobal() == nullptr) {
        logger->info("Skipped metadata sync, not logged in.");
        return;
    }
    
    json & data = task->data();
    string id = data["modelId"];
    string pluginId = data["pluginId"];

    json payload = {
        {"objectType", data["modelClassName"]},
        {"version", data["modelMetadataNewVersion"]},
        {"value", data["value"]},
    };

    if (data["modelHeaderMessageId"].is_string()) {
        payload["headerMessageId"] = data["modelHeaderMessageId"].get<string>();
    }

    const json results = PerformIdentityRequest("/metadata/" + account->id() + "/" + id + "/" + pluginId, "POST", payload);
    logger->info("Syncback of metadata {}:{} = {} succeeded.", id, pluginId, payload.dump());
}

void TaskProcessor::performRemoteDestroyCategory(Task * task) {
    json & data = task->data();
    string accountId = task->accountId();
    string path = data["path"].get<string>();
    ErrorCode err = ErrorCode::ErrorNone;
    
    session->deleteFolder(AS_MCSTR(path), &err);
    
    if (err != ErrorNone) {
        throw SyncException(err, "deleteFolder");
    }
    
    logger->info("Deletion of folder/label '{}' succeeded.", path);
}

void TaskProcessor::performRemoteSendDraft(Task * task) {
    AutoreleasePool pool;
    ErrorCode err = ErrorNone;

    // We never intend for a send task to run more than once. We set this bit
    // to ensure that - even if we don't report failures properly - we never
    // get a send task "stuck" in the queue sending over and over. All retries
    // are user-triggered and create a new task.
    if (task->data().count("_performRemoteRan")) { return; }
    task->data()["_performRemoteRan"] = true;
    store->save(task);

    // load the draft and body from the task
    json & draftJSON = task->data()["draft"];
    json & perRecipientBodies = task->data()["perRecipientBodies"];
    string body = draftJSON["body"].get<string>();
    
    bool plaintext = draftJSON["plaintext"].get<bool>();
    bool multisend = perRecipientBodies.is_object();

    shared_ptr<Message> existing = nullptr;
    if (draftJSON.count("id")) {
        existing = store->find<Message>(Query().equal("id", draftJSON["id"].get<string>()));
    }
    Message draft = inflateClientDraftJSON(draftJSON, existing);
    
    logger->info("- Sending draft {}", draft.headerMessageId());

    // find the sent folder: folder OR label
    auto sent = store->find<Folder>(Query().equal("accountId", account->id()).equal("role", "sent"));
    if (sent == nullptr) {
        sent = store->find<Label>(Query().equal("accountId", account->id()).equal("role", "sent"));
        if (sent == nullptr) {
            throw SyncException("no-sent-folder", "Mailspring doesn't know which folder to use for sent mail. Visit Preferences > Folders to assign a sent folder.", false);
        }
    }
    String * sentPath = AS_MCSTR(sent->path());
    logger->info("-- Identified `sent` folder: {}", sent->path());
    
    // build the MIME message
    MessageBuilder builder;
    if (multisend) {
        if (!perRecipientBodies.count("self")) {
            throw SyncException("no-self-body", "If `perRecipientBodies` is populated, you must provide a `self` entry.", false);
        }
        if (plaintext) {
            builder.setTextBody(AS_MCSTR(perRecipientBodies["self"].get<string>()));
        } else {
            builder.setHTMLBody(AS_MCSTR(perRecipientBodies["self"].get<string>()));
        }
    } else {
        if (plaintext) {
            builder.setTextBody(AS_MCSTR(body));
        } else {
            builder.setHTMLBody(AS_MCSTR(body));
        }
    }

    builder.header()->setSubject(AS_MCSTR(draft.subject()));
    builder.header()->setMessageID(AS_MCSTR(draft.headerMessageId()));
    builder.header()->setUserAgent(MCSTR("Mailspring"));
    builder.header()->setDate(time(0));
    
    // todo: lookup thread reference entire chain?

    if (draft.replyToHeaderMessageId() != "") {
        builder.header()->setReferences(Array::arrayWithObject(AS_MCSTR(draft.replyToHeaderMessageId())));
        builder.header()->setInReplyTo(Array::arrayWithObject(AS_MCSTR(draft.replyToHeaderMessageId())));
    }
    if (draft.forwardedHeaderMessageId() != "") {
        builder.header()->setReferences(Array::arrayWithObject(AS_MCSTR(draft.forwardedHeaderMessageId())));
    }

    Array * to = new Array();
    for (json & p : draft.to()) {
        to->addObject(MailUtils::addressFromContactJSON(p));
    }
    builder.header()->setTo(to);
    
    Array * cc = new Array();
    for (json & p : draft.cc()) {
        cc->addObject(MailUtils::addressFromContactJSON(p));
    }
    builder.header()->setCc(cc);
    
    Array * bcc = new Array();
    for (json & p : draft.bcc()) {
        bcc->addObject(MailUtils::addressFromContactJSON(p));
    }
    builder.header()->setBcc(bcc);

    Array * replyTo = new Array();
    for (json & p : draft.replyTo()) {
        replyTo->addObject(MailUtils::addressFromContactJSON(p));
    }
    builder.header()->setReplyTo(replyTo);

    json & fromP = draft.from().at(0);
    builder.header()->setFrom(MailUtils::addressFromContactJSON(fromP));
    
    for (json & fileJSON : draft.files()) {
        File file{fileJSON};
        string root = MailUtils::getEnvUTF8("CONFIG_DIR_PATH") + FS_PATH_SEP + "files";
        string path = MailUtils::pathForFile(root, &file, false);
        
#ifdef _MSC_VER
        wstring_convert<codecvt_utf8<wchar_t>, wchar_t> convert;
        Attachment * a = Attachment::attachmentWithContentsOfFile(AS_WIDE_MCSTR(convert.from_bytes(path)));
#else
        Attachment * a = Attachment::attachmentWithContentsOfFile(AS_MCSTR(path));
#endif

        if (file.contentId().is_string()) {
            a->setContentID(AS_MCSTR(file.contentId().get<string>()));
            a->setInlineAttachment(true);
            builder.addRelatedAttachment(a);
        } else {
            builder.addAttachment(a);
        }
    }

    // Save the message data / body we'll write to the sent folder
    Data * messageDataForSent = builder.data();

    /*
    OK! If we've reached this point we're going to deliver the message. To do multisend,
    we need to hit the SMTP gateway more than once. If one request fails we stop and mark
    the task as failed, but keep track of who got the message.
    */

    SMTPSession smtp;
    SMTPProgress sprogress;
    MailUtils::configureSessionForAccount(smtp, account);
    string succeeded;

    if (multisend) {
        logger->info("-- Sending customized message bodies to each recipient:");

        for (json::iterator it = perRecipientBodies.begin(); it != perRecipientBodies.end(); ++it) {
            if (it.key() == "self") {
                continue;
            }
            
            logger->info("--- Sending to {}", it.key());
            if (plaintext) {
                builder.setTextBody(AS_MCSTR(it.value().get<string>()));
            } else {
                builder.setHTMLBody(AS_MCSTR(it.value().get<string>()));
            }
            Address * to = Address::addressWithMailbox(AS_MCSTR(it.key()));
            Data * messageData = builder.data();
            smtp.sendMessage(builder.header()->from(), Array::arrayWithObject(to), messageData, &sprogress, &err);
            if (err != ErrorNone) {
                break;
            }
            succeeded += "\n - " + it.key();
        }

    } else {
        logger->info("-- Sending a single message body to all recipients:");
        smtp.sendMessage(messageDataForSent, &sprogress, &err);
    }
    
    if (err != ErrorNone) {
        int e = smtp.lastLibetpanError();
        string es = LibEtPanCodeToTypeMap.count(e) ? LibEtPanCodeToTypeMap[e] : to_string(e);
        logger->info("-X An SMTP error occurred: {} LibEtPan code: {}", ErrorCodeToTypeMap[err], es);
        if (succeeded.size() > 0) {
            throw SyncException("send-partially-failed", ErrorCodeToTypeMap[err] + ":::" + succeeded, false);
        } else {
            throw SyncException("send-failed", ErrorCodeToTypeMap[err], false);
        }
    }
    
    /* 
     Sending complete! First, delete the draft from the server so the user knows it has been sent
     and we don't re-sync it to the app after we delete it below.
     */
    if (draft.remoteUID() != 0) {
        auto uids = IndexSet::indexSetWithIndex(draft.remoteUID());
        String * path = AS_MCSTR(draft.remoteFolder()["path"].get<string>());
        
        logger->info("-- Deleting remote draft with UID {}", draft.remoteUID());
        _removeMessagesResilient(session, store, account->id(), path, uids);
    }

     /* Next, scan the sent folder for the message(s) we just sent through the SMTP
     gateway and clean them up. Some mail servers automatically place messages in the sent
     folder, others don't.
     */
    uint32_t sentFolderMessageUID = 0;
    {
        // grab the last few items in the sent folder... we know we don't need more than 10
        // because multisend is capped.
        int tries = 0;
        int delay[] = {0, 1, 1, 2, 2};
        IndexSet * uids = new IndexSet();
        
        while (tries < 4 && uids->count() == 0) {
            if (delay[tries]) {
                logger->info("-- No messages found. Sleeping {} to wait for sent folder to settle...", delay[tries]);
				std::this_thread::sleep_for(std::chrono::seconds(delay[tries]));
            }
            tries ++;
            session->findUIDsOfRecentHeaderMessageID(sentPath, AS_MCSTR(draft.headerMessageId()), uids);
        }
    
        if (multisend && (uids->count() > 0)) {
            // If we sent separate messages to each recipient, we end up with a bunch of sent
            // messages. Delete all of them since they contain the targeted bodies with link/open tracking.
            logger->info("-- Deleting {} messages added to {} by the SMTP gateway.", uids->count(), sentPath->UTF8Characters());
            _removeMessagesResilient(session, store, account->id(), sentPath, uids);
            
            // In Gmail, moving the messages from Sent -> Trash and expunging them just places them in All Mail
            // for some reason. Deleting them AGAIN from All Mail works properly, so we do that here.
            auto all = store->find<Folder>(Query().equal("accountId", account->id()).equal("role", "all"));
            if (all != nullptr) {
                uids->removeAllIndexes();
                session->findUIDsOfRecentHeaderMessageID(AS_MCSTR(all->path()), AS_MCSTR(draft.headerMessageId()), uids);
                if (uids->count() > 0) {
                    logger->info("-- Deleting {} messages just moved to {} by the SMTP gateway.", uids->count(), all->path());
                    _removeMessagesResilient(session, store, account->id(), AS_MCSTR(all->path()), uids);
                }
            }

        } else if (!multisend && (uids->count() == 1)) {
            // If we find a single message in the sent folder, we'll move forward with that one.
            sentFolderMessageUID = (uint32_t)uids->allRanges()[0].location;
            logger->info("-- Found a message added to the sent folder by the SMTP gateway (UID {})", sentFolderMessageUID);
            
        } else {
            logger->info("-- No messages matching the message-id were found in the Sent folder.", uids->count());
        }
        
        if (err != ErrorNone) {
            logger->error("-X IMAP Error: {}. This may result in duplicate messages in the Sent folder.", ErrorCodeToTypeMap[err]);
            err = ErrorNone;
        }
    }

    if (sentFolderMessageUID == 0) {
        // Manually place a single message in the sent folder
        IMAPProgress iprogress;
        logger->info("-- Placing a new message with `self` body in the sent folder.");
        session->appendMessage(sentPath, messageDataForSent, MessageFlagSeen, &iprogress, &sentFolderMessageUID, &err);
        if (err != ErrorNone) {
            logger->error("-X IMAP Error: {}. Could not place a message into the Sent folder. This means no metadata will be attached!", ErrorCodeToTypeMap[err]);
            err = ErrorNone;
        }

        // If the user is on Gmail and the thread had labels, apply those same
        // labels to the new sent message. Otherwise the thread moves /only/ to
        // the sent folder.
        if (session->storedCapabilities()->containsIndex(IMAPCapabilityGmail)) {
            if (draft.threadId() != "") {
                auto thread = store->find<Thread>(Query().equal("id", draft.threadId()));
                if (thread) {
                    Array * xgmValues = new Array();
                    for (auto & l : thread->labels()) {
                        string role = l["role"].get<string>();
                        if (role == "inbox" || role == "sent" || role == "drafts") { continue; }
                        string xgm = _xgmKeyForLabel(l);
                        logger->info("-- Will add label to new message: {}", xgm);
                        xgmValues->addObject(AS_MCSTR(xgm));
                    }
                    session->storeLabelsByUID(sentPath, IndexSet::indexSetWithIndex(sentFolderMessageUID), IMAPStoreFlagsRequestKindAdd, xgmValues, &err);
                    if (err != ErrorNone) {
                        logger->error("-X IMAP Error: {}. Could not add labels to new message in sent folder. This means the thread may disappear from the inbox.", ErrorCodeToTypeMap[err]);
                        err = ErrorNone;
                    }
                }
            }
        }
    }

    if (sentFolderMessageUID == 0) {
        // If we still don't have a message in the sent folder, there's nothing we can do.
        // Delete the draft and exit.
        store->remove(&draft);
        return;
    }

    /*
     Finally, pull down the message we created to get it's labels, thread ID, etc. and
     associate our metadata with it.
     
     Note: Yes, it's a bit weird that we sync up a message to the sent folder and then
     immediately pull it's attributes, but we want to get the Thread ID on Gmail, etc.
     We don't pull down the entire message body.
     */

    MailProcessor processor{account, store};
    shared_ptr<Message> localMessage = nullptr;
    IMAPMessage * remoteMessage = nullptr;
    
    logger->info("-- Syncing sent message (UID {}) to the local mail store", sentFolderMessageUID);
    IMAPMessagesRequestKind kind = (IMAPMessagesRequestKind)(IMAPMessagesRequestKindHeaders | IMAPMessagesRequestKindFlags);
    if (session->storedCapabilities()->containsIndex(IMAPCapabilityGmail)) {
        kind = (IMAPMessagesRequestKind)(kind | IMAPMessagesRequestKindGmailLabels | IMAPMessagesRequestKindGmailThreadID | IMAPMessagesRequestKindGmailMessageID);
    }
    
    // Important: Courier (and maybe other IMAP servers) won't show us new messages we've created
    // in the folder unless we re-select the folder. (I think they're treating UIDs like sequence
    // numbers?). We must re-select the sent folder to pull down the message we created.
    session->select(sentPath, &err);

    time_t syncDataTimestamp = time(0);
    IndexSet * uids = IndexSet::indexSetWithIndex(sentFolderMessageUID);
    Array * remote = session->fetchMessagesByUID(sentPath, kind, uids, nullptr, &err);

    // Delete the draft. We do this as close as possible to when we write the message in
    // so there isn't any flicker in the client, but before error checking because we always
    // want it to always disppear since sending succeeded.
    store->remove(&draft);

    if (err != ErrorNone) {
        logger->error("-X Error: {} occurred syncing the sent message to the local mail store. Metadata will not be attached.", ErrorCodeToTypeMap[err]);
        return;
    }
    if (remote->count() == 0) {
        logger->error("-X Error: No messages were returned. Metadata will not be attached!");
        return;
    }

    MessageParser * messageParser = MessageParser::messageParserWithData(messageDataForSent);
    remoteMessage = (IMAPMessage *)(remote->lastObject());
    localMessage = processor.insertFallbackToUpdateMessage(remoteMessage, *sent, syncDataTimestamp);
    if (localMessage == nullptr) {
        logger->error("-X Error: processor.insert did not return a message.");
        return;
    }

    processor.retrievedMessageBody(localMessage.get(), messageParser);
    
    logger->info("-- Synced sent message (Sent UID {} = Local ID {})", sentFolderMessageUID, localMessage->id());
    
    // retrieve the new message and queue metadata tasks on it
    for (const auto & m : draft.metadata()) {
        auto pluginId = m["pluginId"].get<string>();
        logger->info("-- Queueing task to attach {} draft metadata to new message.", pluginId);
        
        Task mTask{"SyncbackMetadataTask", account->id(), {
            {"modelId", localMessage->id()},
            {"modelClassName", "message"},
            {"modelHeaderMessageId", localMessage->headerMessageId()},
            {"pluginId", pluginId},
            {"value", m["value"]},
        }};
        performLocal(&mTask); // will call save
    }
}

void TaskProcessor::performRemoteSendFeatureUsageEvent(Task * task) {
    if (Identity::GetGlobal() == nullptr) {
        logger->info("Skipped metadata sync, not logged in.");
        return;
    }
    const auto feature = task->data()["feature"].get<string>();
    json payload = {
        {"feature", feature}
    };

    logger->info("Incrementing usage of feature: {}", feature);
    auto result = PerformIdentityRequest("/api/feature_usage_event", "POST", payload);
    logger->info("Incrementing usage of feature succeeded: {}", result.dump());
}

void TaskProcessor::performLocalChangeRoleMapping(Task * task) {
    const auto path = task->data()["path"].get<string>();
    const auto role = task->data()["role"].get<string>();
    
    {
        MailStoreTransaction transaction{store, "performLocalChangeRoleMapping"};
        auto query = Query().equal("accountId", task->accountId()).equal("path", path);
        shared_ptr<Folder> category = store->find<Folder>(query);
        if (category == nullptr) {
            category = store->find<Label>(query);
        }
        if (category == nullptr) {
            throw SyncException("no-matching-folder", "", false);
        }
        
        // find any category with the role already and clear it
        auto existingQuery = Query().equal("accountId", task->accountId()).equal("role", role);
        shared_ptr<Folder> existing = store->find<Folder>(existingQuery);
        if (existing == nullptr) {
            existing = store->find<Label>(existingQuery);
        }
        if (existing != nullptr) {
            existing->setRole("");
            store->save(existing.get());
        }
    
        // save role onto the new category
        category->setRole(role);
        store->save(category.get());
        
        transaction.commit();
    }
}

void TaskProcessor::performRemoteExpungeAllInFolder(Task * task) {
    AutoreleasePool pool;
    ErrorCode err = ErrorNone;
    const auto path = task->data()["folder"]["path"].get<string>();
    const auto id = task->data()["folder"]["id"].get<string>();

    IndexSet set;
    set.addRange(RangeMake(1, UINT64_MAX));
    session->storeFlagsByUID(AS_MCSTR(path), &set, IMAPStoreFlagsRequestKindAdd, MessageFlagDeleted, &err);
    if (err != ErrorNone) {
        throw SyncException(err, "storeFlagsByUID");
    }
    session->expunge(AS_MCSTR(path), &err);
    if (err != ErrorNone) {
        throw SyncException(err, "expunge");
    }
    logger->info("-- Expunged {}", path);
    
    // delete all the local messages in the folder. We do this in performRemote
    // because we don't want to block in performLocal for this long. We also pause
    // as we go to allow the app to recover from the mass deletions.
    auto all = store->findAll<Message>(Query().equal("accountId", task->accountId()).equal("remoteFolderId", id));
    for (auto block : MailUtils::chunksOfVector(all, 100)) {
        {
            MailStoreTransaction t {store};
            for (auto msg : block) {
                store->remove(msg.get());
            }
            t.commit();
        }
        logger->info("-- Deleted {} local messages", block.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

void TaskProcessor::performRemoteGetMessageRFC2822(Task * task) {
    AutoreleasePool pool;
    IMAPProgress cb;
    ErrorCode err = ErrorNone;
    const auto id = task->data()["messageId"].get<string>();
    const auto filepath = task->data()["filepath"].get<string>();
    
    auto msg = store->find<Message>(Query().equal("id", id));

    Data * data = session->fetchMessageByUID(AS_MCSTR(msg->remoteFolder()["path"].get<string>()), msg->remoteUID(), &cb, &err);
    if (err != ErrorNone) {
        logger->error("Unable to fetch rfc2822 for message (UID {}). Error {}", msg->remoteUID(), ErrorCodeToTypeMap[err]);
        throw SyncException(err, "performRemoteGetMessageRFC2822");
    }
#ifdef _MSC_VER
    wstring_convert<codecvt_utf8<wchar_t>, wchar_t> convert;
    data->writeToFile(AS_WIDE_MCSTR(convert.from_bytes(filepath)));
#else
    data->writeToFile(AS_MCSTR(filepath));
#endif
}



void TaskProcessor::performRemoteSendRSVP(Task * task) {
    AutoreleasePool pool;
    ErrorCode err = ErrorNone;
    
    if (!task->data().count("ics") || !task->data().count("subject") || !task->data().count("to")) {
        throw SyncException(err, "missing-json");
    }
    
    // load the draft and body from the task
    string ics = task->data()["ics"].get<string>();
    string icsMethod = "REPLY";
    string subject = task->data()["subject"].get<string>();
    string organizer = task->data()["to"].get<string>();

    // build the MIME message
    MessageBuilder builder;
    builder.header()->setSubject(AS_MCSTR(subject));
    builder.header()->setUserAgent(MCSTR("Mailspring"));
    builder.header()->setDate(time(0));

    Array * to = new Array();
    to->addObject(Address::addressWithMailbox(AS_MCSTR(organizer)));
    builder.header()->setTo(to);
    
    Array * replyTo = new Array();
    Address * me = Address::addressWithMailbox(AS_MCSTR(account->emailAddress()));
    replyTo->addObject(me);

    builder.header()->setReplyTo(replyTo);
    builder.header()->setFrom(me);
    builder.setTextBody(MCSTR(""));
    
    Data * data;
    Attachment * icsAttachment;
    
    icsAttachment = new Attachment();
    icsAttachment->setMimeType(MCSTR("text/calendar"));
    icsAttachment->setContentTypeParameter(MCSTR("method"), AS_MCSTR(icsMethod));
    data = AS_MCSTR(ics)->dataUsingEncoding("utf-8");
    icsAttachment->setData(data);
    builder.addAttachment(icsAttachment);
    icsAttachment->autorelease();
    
    // Save the message data / body we'll write to the sent folder
    Data * messageDataForSent = builder.data();
    
    /*
     OK! If we've reached this point we're going to deliver the message. To do multisend,
     we need to hit the SMTP gateway more than once. If one request fails we stop and mark
     the task as failed, but keep track of who got the message.
     */
    
    SMTPSession smtp;
    SMTPProgress sprogress;
    MailUtils::configureSessionForAccount(smtp, account);
    string succeeded;

    logger->info("-- Sending RSVP to organizer {}", organizer);
    smtp.sendMessage(messageDataForSent, &sprogress, &err);

    if (err != ErrorNone) {
        int e = smtp.lastLibetpanError();
        string es = LibEtPanCodeToTypeMap.count(e) ? LibEtPanCodeToTypeMap[e] : to_string(e);
        logger->info("-X An SMTP error occurred: {} LibEtPan code: {}", ErrorCodeToTypeMap[err], es);
        throw SyncException("send-failed", ErrorCodeToTypeMap[err], false);
    }
}
