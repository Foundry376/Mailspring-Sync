//
//  Folder.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "TaskProcessor.hpp"
#include "MailProcessor.hpp"
#include "MailStoreTransaction.hpp"
#include "MailUtils.hpp"
#include "Thread.hpp"
#include "Message.hpp"
#include "MailUtils.hpp"
#include "File.hpp"
#include "constants.h"
#include "ProgressCollectors.hpp"
#include "SyncException.hpp"
#include "NetworkRequestUtils.hpp"

using namespace std;
using namespace mailcore;
using namespace nlohmann;


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
    ErrorCode err = ErrorCode::ErrorNone;
    Folder destFolder{data["folder"]};
    String destPath = mailcore::String(destFolder.path().c_str());
    
    HashMap * uidmap = nullptr;
    session->moveMessages(path, uids, &destPath, &uidmap, &err);
    if (err != ErrorCode::ErrorNone) {
        throw SyncException(err, "moveMessages");
    }
    if (uidmap == nullptr) {
        throw SyncException("generic", "move did not return a uidmap - maybe the UIDs are no longer in the folder?", false);
    }
    for (auto msg : messages) {
        Value * currentUID = Value::valueWithUnsignedLongValue(msg->remoteUID());
        Value * newUID = (Value *)uidmap->objectForKey(currentUID);
        if (!newUID) {
            throw SyncException("generic", "move did not provide new UID.", false);
        }
        msg->setRemoteFolder(&destFolder);
        msg->setRemoteUID(newUID->unsignedIntValue());
    }
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
        String * str = new mailcore::String(_xgmKeyForLabel(item).c_str());
        toAdd->addObject(str);
        str->autorelease();
    }

    Array * toRemove = new mailcore::Array{};
    toRemove->autorelease();
    for (auto & item : data["labelsToRemove"]) {
        String * str = new mailcore::String(_xgmKeyForLabel(item).c_str());
        toRemove->addObject(str);
        str->autorelease();
    }
    
    if (toRemove->count() > 0) {
        session->storeLabelsByUID(path, uids, IMAPStoreFlagsRequestKindRemove, toRemove, &err);
        if (err != ErrorCode::ErrorNone) {
            throw SyncException(err, "storeLabelsByUID - remove");
        }
    }
    if (toAdd->count() > 0) {
        session->storeLabelsByUID(path, uids, IMAPStoreFlagsRequestKindAdd, toAdd, &err);
        if (err != ErrorCode::ErrorNone) {
            throw SyncException(err, "storeLabelsByUID - add");
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
                performRemoteChangeOnMessages(task, _applyUnreadInIMAPFolder);
                
            } else if (cname == "ChangeStarredTask") {
                performRemoteChangeOnMessages(task, _applyStarredInIMAPFolder);
                
            } else if (cname == "ChangeFolderTask") {
                performRemoteChangeOnMessages(task, _applyFolderMoveInIMAPFolder);

            } else if (cname == "ChangeLabelsTask") {
                performRemoteChangeOnMessages(task, _applyLabelChangeInIMAPFolder);
                
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
    MailStoreTransaction transaction{store};
    auto task = store->find<Task>(Query().equal("id", taskId).equal("accountId", account->id()));
    if (task != nullptr) {
        task->setShouldCancel();
        store->save(task.get());
    }
    transaction.commit();
}

#pragma mark Privates

Message TaskProcessor::inflateDraft(json & draftJSON) {
    Query q = Query().equal("accountId", account->id()).equal("role", "drafts");
    auto folder = store->find<Folder>(q);
    if (folder.get() == nullptr) {
        q = Query().equal("accountId", account->id()).equal("role", "all");
        folder = store->find<Folder>(q);
    }
    if (folder == nullptr) {
        throw SyncException("no-drafts-folder", "This account does not have a folder with `role` = `drafts` or `all`.", false);
    }

    draftJSON["folder"] = folder->toJSON();
    draftJSON["remoteFolder"] = folder->toJSON();
    
    // set other JSON attributes the client may not have populated,
    // but we require to be non-null
    draftJSON["remoteUID"] = 0;
    draftJSON["draft"] = true;
    draftJSON["unread"] = false;
    draftJSON["starred"] = false;
    draftJSON["date"] = time(0);
    draftJSON["_sa"] = 0;
    draftJSON["_suc"] = 0;
    draftJSON["labels"] = json::array();

    if (!draftJSON.count("id") || draftJSON["id"].is_null()) {
        draftJSON["id"] = MailUtils::idForDraftHeaderMessageId(draftJSON["aid"], draftJSON["hMsgId"]);
    }
    if (!draftJSON.count("threadId") || draftJSON["threadId"].is_null()) {
        draftJSON["threadId"] = "";
    }
    if (!draftJSON.count("gMsgId") || draftJSON["gMsgId"].is_null()) {
        draftJSON["gMsgId"] = "";
    }
    if (!draftJSON.count("files") || draftJSON["files"].is_null()) {
        draftJSON["files"] = json::array();
    }
    if (!draftJSON.count("from") || draftJSON["from"].is_null()) {
        draftJSON["from"] = json::array();
    }
    if (!draftJSON.count("to") || draftJSON["to"].is_null()) {
        draftJSON["to"] = json::array();
    }
    if (!draftJSON.count("cc") || draftJSON["cc"].is_null()) {
        draftJSON["cc"] = json::array();
    }
    if (!draftJSON.count("bcc") || draftJSON["bcc"].is_null()) {
        draftJSON["bcc"] = json::array();
    }
    if (!draftJSON.count("replyTo") || draftJSON["replyTo"].is_null()) {
        draftJSON["replyTo"] = json::array();
    }

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
        Query byThreadId = Query().equal("threadId", threadIds);
        models.messages = store->findAll<Message>(byThreadId);
        
    } else if (data.count("messageIds")) {
        vector<string> messageIds{};
        for (auto & member : data["messageIds"]) {
            messageIds.push_back(member.get<string>());
        }
        Query byId = Query().equal("id", messageIds);
        models.messages = store->findAll<Message>(byId);
    }
    
    return models;
}

void TaskProcessor::performLocalChangeOnMessages(Task * task, void (*modifyLocalMessage)(Message *, json &)) {
    MailStoreTransaction transaction{store};
    
    json & data = task->data();
    ChangeMailModels models = inflateMessages(data);
    
    for (auto msg : models.messages) {
        // perform local changes
        modifyLocalMessage(msg.get(), data);

        // prevent remote changes to this message for 24 hours
        // so the changes aren't reverted by sync before we can syncback.
        msg->setSyncUnsavedChanges(msg->syncUnsavedChanges() + 1);
        msg->setSyncedAt(time(0) + 24 * 60 * 60);

        store->save(msg.get());
    }

    transaction.commit();
}

void TaskProcessor::performRemoteChangeOnMessages(Task * task, void (*applyInFolder)(IMAPSession * session, String * path, IndexSet * uids, vector<shared_ptr<Message>> messages, json & data)) {
    // perform the remote action on the impacted messages
    json & data = task->data();
    
    // because the messages have had their syncedAt date set to something high,
    // it's safe to load, mutate, save without wrapping in a transaction, because
    // the other sync worker will not change them and we only run one task at a time.
    vector<shared_ptr<Message>> messages = inflateMessages(data).messages;
    map<string, shared_ptr<IndexSet>> uidsByFolder{};
    map<string, vector<shared_ptr<Message>>> msgsByFolder{};
    
    for (auto msg : messages) {
        string path = msg->remoteFolder()["path"].get<string>();
        uint32_t uid = msg->remoteUID();
        if (!uidsByFolder.count(path)) {
            uidsByFolder[path] = make_shared<IndexSet>();
            msgsByFolder[path] = {};
        }
        uidsByFolder[path]->addIndex(uid);
        msgsByFolder[path].push_back(msg);
    }
    
    for (auto pair : msgsByFolder) {
        auto & msgs = pair.second;
        String path = mailcore::String(pair.first.c_str());
        IndexSet * uids = uidsByFolder[pair.first].get();
        
        // perform the action. NOTE! This function is allowed to mutate the messages,
        // for example to set their remoteUID after a move.
        applyInFolder(session, &path, uids, msgs, data);
    }
    
    // save any changes made by applyInFolder and decrement locks
    {
        MailStoreTransaction transaction{store};

        for (auto msg : messages) {
            int suc = msg->syncUnsavedChanges() - 1;
            msg->setSyncUnsavedChanges(suc);
            if (suc == 0) {
                msg->setSyncedAt(time(0));
            }
            store->save(msg.get());
        }
        transaction.commit();
    }
}

void TaskProcessor::performLocalSaveDraft(Task * task) {
    json & draftJSON = task->data()["draft"];
    Message msg = inflateDraft(draftJSON);

    {
        MailStoreTransaction transaction{store};

        // If the draft already exists, we need to visibly change it's attributes
        // to trigger the correct didSave hooks, etc. Find and update it.
        auto existing = store->find<Message>(Query().equal("accountId", msg.accountId()).equal("id", msg.id()));
        if (existing) {
            existing->_data = msg._data;
            store->save(existing.get());
        } else {
            store->save(&msg);
        }

        if (draftJSON.count("body")) {
            SQLite::Statement insert(store->db(), "REPLACE INTO MessageBody (id, value) VALUES (?, ?)");
            insert.bind(1, msg.id());
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
        throw SyncException("no-trash-folder", "", false);
    }

    auto stubIds = json::array();
    
    // we need to free up the draft ID immediately because the user can
    // switch a draft between accounts, and we may need to re-create a
    // new draft with the same acctId + hMsgId combination.
    
    // Destroy drafts locally and create stubs that prevent the sync
    // worker from replacing them while we delete them via IMAP.
    {
        MailStoreTransaction transaction{store};

        auto drafts = store->findAll<Message>(Query().equal("id", messageIds));
        for (auto & draft : drafts) {
            store->remove(draft.get());
            
            auto stub = Message::messageWithDeletionPlaceholderFor(draft);
            stub.setClientFolder(trash.get());
            store->save(&stub);
            stubIds.push_back(stub.id());

            logger->info("-- Replacing local ID {} with {}", draft->id(), stub.id());
        }

        transaction.commit();
    }
    task->data()["stubIds"] = stubIds;
}

void TaskProcessor::performRemoteDestroyDraft(Task * task) {
    vector<string> stubIds = task->data()["stubIds"];
    auto stubs = store->findAll<Message>(Query().equal("id", stubIds));

    // Find the trash folder
    auto trash = store->find<Folder>(Query().equal("accountId", account->id()).equal("role", "trash"));
    if (trash == nullptr) {
        throw SyncException("no-trash-folder", "", false);
    }
    String * trashPath = AS_MCSTR(trash->path());
    logger->info("-- Identified `trash` folder: {}", trash->path());

    for (auto & stub : stubs) {
        if (stub->remoteUID() == 0) {
            continue; // not synced to server at all
        }
        auto uids = IndexSet::indexSetWithIndex(stub->remoteUID());
        string folderPath = stub->remoteFolder()["path"].get<string>();

        logger->info("-- Deleting remote draft {} ({} UID {})", stub->id(), folderPath, stub->remoteUID());

        // Move to the trash folder
        HashMap * uidmap = nullptr;
        ErrorCode err = ErrorCode::ErrorNone;
        Array * auidsInTrash = nullptr;
        IndexSet uidsInTrash;
        session->moveMessages(AS_MCSTR(folderPath), uids, trashPath, &uidmap, &err);
        if (err != ErrorNone) {
            logger->info("X- ignoring moveMessages failure (error: {})", ErrorCodeToTypeMap[err]);
            continue; // eh, draft may be gone, oh well
        }
        if (uidmap == nullptr) {
            logger->info("X- ignoring moveMessages did not return uidmap");
            continue; // eh, draft may be gone, oh well
        }
        
        // Add the "DELETED" flag
        auidsInTrash = uidmap->allValues();
        for (int i = 0; i < auidsInTrash->count(); i ++) {
            uidsInTrash.addIndex(((Value*)auidsInTrash->objectAtIndex(i))->unsignedLongValue());
        }
        session->storeFlagsByUID(trashPath, &uidsInTrash, IMAPStoreFlagsRequestKindAdd, MessageFlagDeleted, &err);
        if (err != ErrorNone) {
            logger->info("X- ignoring storeFlagsByUID could not add deleted flag (error: {})", ErrorCodeToTypeMap[err]);
            continue; // eh, draft may be gone, oh well
        }
        
        // Call EXPUNGE to permanently delete messages
        session->expunge(trashPath, &err);

        // remove the stub from our local cache - would eventually get removed
        // during sync, but we don't want to fetch it's body or anything
        store->remove(stub.get());
    }
}

void TaskProcessor::performLocalSyncbackCategory(Task * task) {
    
}

void TaskProcessor::performRemoteSyncbackCategory(Task * task) {
    json & data = task->data();
    string accountId = task->accountId();
    string path = data["path"].get<string>();
    
    // if the requested path is missing the namespace prefix, add it
    string mainPrefix = MailUtils::namespacePrefixOrBlank(session);
    if (mainPrefix != "" && path.find(mainPrefix) != 0) {
        path = mainPrefix + path;
    }
    
    // if the requested path includes "/" delimiters, replace them with the real delimiter
    char delimiter = session->defaultNamespace()->mainDelimiter();
    std::replace(path.begin(), path.end(), '/', delimiter);

    ErrorCode err = ErrorCode::ErrorNone;
    
    if (data.count("existingPath")) {
        String existing = String(data["existingPath"].get<string>().c_str());
        String next = String(path.c_str());
        session->renameFolder(&existing, &next, &err);
    } else {
        String next = String(path.c_str());
        session->createFolder(&next, &err);
    }
    
    if (err != ErrorNone) {
        data["created"] = nullptr;
        throw SyncException(err, "create/renameFolder");
    }
    
    // must go beneath the first use of session above.
    bool isGmail = session->storedCapabilities()->containsIndex(IMAPCapabilityGmail);
    shared_ptr<Folder> created = nullptr;
    
    if (isGmail) {
        created = make_shared<Label>(MailUtils::idForFolder(accountId, path), accountId, 0);
    } else {
        created = make_shared<Folder>(MailUtils::idForFolder(accountId, path), accountId, 0);
    }
    created->setPath(path);
    data["created"] = created->toJSON();
    store->save(created.get());
    
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
        MailStoreTransaction transaction{store};
        
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
    json & data = task->data();
    string id = data["modelId"];
    string pluginId = data["pluginId"];

    const json payload = {
        {"objectType", data["modelClassName"]},
        {"version", data["modelMetadataNewVersion"]},
        {"headerMessageId", data["modelHeaderMessageId"]},
        {"value", data["value"]},
    };
    const json results = MakeIdentityRequest("/metadata/" + account->id() + "/" + id + "/" + pluginId, "POST", payload);
    logger->info("Syncback of metadata {}:{} = {} succeeded.", id, pluginId, payload.dump());
}

void TaskProcessor::performRemoteDestroyCategory(Task * task) {
    json & data = task->data();
    string accountId = task->accountId();
    string path = data["path"].get<string>();
    String mpath = String(path.c_str());
    ErrorCode err = ErrorCode::ErrorNone;
    
    session->deleteFolder(&mpath, &err);
    
    if (err != ErrorNone) {
        throw SyncException(err, "deleteFolder");
    }
    
    logger->info("Deletion of folder/label '{}' succeeded.", path);
}

void TaskProcessor::performRemoteSendDraft(Task * task) {
    AutoreleasePool pool;
    ErrorCode err = ErrorNone;

    // We never intend for a send task to run more than once. As soon as we
    // successfully send a single SMTP request successfully, we set this bit
    // to ensure that - even if we don't report failures properly - we never
    // get a send task "stuck" in the queue sending over and over.
    if (task->data().count("_performRemoteRan")) { return; }
    task->data()["_performRemoteRan"] = true;
    store->save(task);

    // load the draft and body from the task
    json & draftJSON = task->data()["draft"];
    json & perRecipientBodies = task->data()["perRecipientBodies"];
    Message draft = inflateDraft(draftJSON);
    string body = draftJSON["body"].get<string>();
    bool multisend = perRecipientBodies.is_object();
    
    logger->info("- Sending draft {}", draft.headerMessageId());

    // find the sent folder: folder OR label
    auto sent = store->find<Folder>(Query().equal("accountId", account->id()).equal("role", "sent"));
    if (sent == nullptr) {
        sent = store->find<Label>(Query().equal("accountId", account->id()).equal("role", "sent"));
        if (sent == nullptr) {
            throw SyncException("no-sent-folder", "", false);
        }
    }
    String * sentPath = AS_MCSTR(sent->path());
    logger->info("-- Identified `sent` folder: {}", sent->path());

    // find the trash folder
    auto trash = store->find<Folder>(Query().equal("accountId", account->id()).equal("role", "trash"));
    if (trash == nullptr) {
        throw SyncException("no-trash-folder", "", false);
    }
    String * trashPath = AS_MCSTR(trash->path());
    logger->info("-- Identified `trash` folder: {}", trash->path());
    
    // build the MIME message
    MessageBuilder builder;
    if (multisend) {
        if (!perRecipientBodies.count("self")) {
            throw SyncException("no-self-body", "If `perRecipientBodies` is populated, you must provide a `self` entry.", false);
        }
        builder.setHTMLBody(AS_MCSTR(perRecipientBodies["self"].get<string>()));
    } else {
        builder.setHTMLBody(AS_MCSTR(body));
    }
    builder.header()->setSubject(AS_MCSTR(draft.subject()));
    builder.header()->setMessageID(AS_MCSTR(draft.headerMessageId()));
    builder.header()->setUserAgent(MCSTR("Mailspring"));
    builder.header()->setDate(time(0));
    
    if (draft.replyToHeaderMessageId() != "") {
        // todo: lookup thread reference entire chain?
        builder.header()->setReferences(Array::arrayWithObject(AS_MCSTR(draft.replyToHeaderMessageId())));
        builder.header()->setInReplyTo(Array::arrayWithObject(AS_MCSTR(draft.replyToHeaderMessageId())));
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
        string root = string(getenv("CONFIG_DIR_PATH")) + FS_PATH_SEP + "files";
        string path = MailUtils::pathForFile(root, &file, false);
        Attachment * a = Attachment::attachmentWithContentsOfFile(AS_MCSTR(path));
        if (file.contentId().is_string()) {
            a->setContentID(AS_MCSTR(file.contentId().get<string>()));
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
            builder.setHTMLBody(AS_MCSTR(it.value().get<string>()));
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
        logger->info("-X An SMTP error occurred: {}", ErrorCodeToTypeMap[err]);
        if (succeeded.size() > 0) {
            throw SyncException("send-partially-failed", ErrorCodeToTypeMap[err] + ":::" + succeeded, false);
        } else {
            throw SyncException("send-failed", ErrorCodeToTypeMap[err], false);
        }
    }
    
    /* 
     Sending complete! Next, scan the sent folder for the message(s) we just sent through the SMTP
     gateway and clean them up. Some mail servers automatically place messages in the sent
     folder, others don't.
     */

    uint32_t sentFolderMessageUID = 0;
    {
        HashMap * uidmap = nullptr;
        Array * auidsInTrash = nullptr;
        IndexSet uidsInTrash;
        
        // grab the last few items in the sent folder... we know we don't need more than 10
        // because multisend is capped.
        int tries = 0;
        int delay[] = {0, 1, 3, 3, 3, 5};
        IndexSet * uids = new IndexSet();
        
        while (tries < 5) {
            if (delay[tries]) {
                logger->info("-- No messages found. Sleeping {} to wait for sent folder to settle...", delay[tries]);
				std::this_thread::sleep_for(std::chrono::seconds(delay[tries]));
            }
            
            // note: we must implement tries /before/ any continue or break statements
            // or user could get into an infinite loop.
            tries ++;

            session->select(sentPath, &err);
            if (err != ErrorNone) {
                continue;
            }

            logger->info("-- Fetching the last 10 messages in the sent folder.");
            IMAPProgress cb;
            IndexSet * set = new IndexSet();
            int min = session->lastFolderMessageCount() > 10 ? (session->lastFolderMessageCount() - 10) : 0;
            set->addRange(RangeMake(min, session->lastFolderMessageCount() - min));
            Array * lastFew = session->fetchMessagesByNumber(sentPath, IMAPMessagesRequestKindHeaders, set, &cb, &err);
            if (err != ErrorNone) {
                continue;
            }

            for (int ii = 0; ii < lastFew->count(); ii ++) {
                auto msg = (IMAPMessage *)lastFew->objectAtIndex(ii);
                string msgId = msg->header()->messageID()->UTF8Characters();
                if (msgId == draft.headerMessageId()) {
                    logger->info("--- Found message-id match");
                    uids->addIndex(msg->uid());
                }
            }
            
            if (uids->count() > 0) {
                break;
            }
        }
    
        if (multisend && (uids->count() > 0)) {
            // If we sent separate messages to each recipient, we end up with a bunch of sent
            // messages. Delete all of them since they contain the targeted bodies.
            logger->info("-- Deleting {} messages added to sent folder by the SMTP gateway.", uids->count());
            
            // Move messages to the trash folder
            session->moveMessages(sentPath, uids, trashPath, &uidmap, &err);
            if (err != ErrorNone) {
                goto endSentCleanup;
            }

            // Add the "DELETED" flag
            auidsInTrash = uidmap->allValues();
            for (int i = 0; i < auidsInTrash->count(); i ++) {
                uidsInTrash.addIndex(((Value*)auidsInTrash->objectAtIndex(i))->unsignedLongValue());
            }
            session->storeFlagsByUID(trashPath, &uidsInTrash, IMAPStoreFlagsRequestKindAdd, MessageFlagDeleted, &err);
            if (err != ErrorNone) {
                goto endSentCleanup;
            }
            
            // Call EXPUNGE to permanently delete messages
            session->expunge(trashPath, &err);
            
        } else if (!multisend && (uids->count() == 1)) {
            // If we find a single message in the sent folder, we'll move forward with that one.
            sentFolderMessageUID = (uint32_t)uids->allRanges()[0].location;
            logger->info("-- Found a message added to the sent folder by the SMTP gateway (UID {})", sentFolderMessageUID);
            
        } else {
            logger->info("-- No messages matching the message-id were found in the Sent folder.", uids->count());
        }
        
    endSentCleanup:
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
                auto thread = store->find<Thread>(Query().equal("accountId", draft.accountId()).equal("id", draft.threadId()));
                if (thread) {
                    Array * xgmValues = new Array();
                    for (auto & l : thread->labels()) {
                        if (l["role"] == "inbox") { continue; }
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
        // If we still don't have a message in the sent folder, there's nothing left to do.
        // Delete the draft and exit.
        store->remove(&draft);
        return;
    }

    /*
     Finally, pull down the message we created to get it's labels, thread ID, etc. and
     associate our metadata with it.
     */

    MailProcessor processor{account, store};
    shared_ptr<Message> localMessage = nullptr;
    IMAPMessage * remoteMessage = nullptr;
    
    logger->info("-- Syncing sent message (UID {}) to the local mail store", sentFolderMessageUID);
    IMAPMessagesRequestKind kind = (IMAPMessagesRequestKind)(IMAPMessagesRequestKindHeaders | IMAPMessagesRequestKindFlags);
    if (session->storedCapabilities()->containsIndex(IMAPCapabilityGmail)) {
        kind = (IMAPMessagesRequestKind)(kind | IMAPMessagesRequestKindGmailLabels | IMAPMessagesRequestKindGmailThreadID | IMAPMessagesRequestKindGmailMessageID);
    }
    
    time_t syncDataTimestamp = time(0);
    IndexSet * uids = IndexSet::indexSetWithIndex(sentFolderMessageUID);
    Array * remote = session->fetchMessagesByUID(sentPath, kind, uids, nullptr, &err);

    // Delete the draft. We do this as close as possible to when we write the message in
    // so there isn't any flicker in the client.
    store->remove(&draft);

    if (err != ErrorNone) {
        logger->error("-X Error: {} occurred syncing the sent message to the local mail store. Metadata will not be attached.", ErrorCodeToTypeMap[err]);
        return;
    }
    if (remote->count() == 0) {
        logger->error("-X Error: No messages were returned. Metadata will not be attached.");
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
    const auto feature = task->data()["feature"].get<string>();
    json payload = {
        {"feature", feature}
    };

    logger->info("Incrementing usage of feature: {}", feature);
    auto result = MakeIdentityRequest("/api/feature_usage_event", "POST", payload);
    logger->info("Incrementing usage of feature succeeded: {}", result.dump());
}

void TaskProcessor::performLocalChangeRoleMapping(Task * task) {
    const auto path = task->data()["path"].get<string>();
    const auto role = task->data()["role"].get<string>();
    
    {
        MailStoreTransaction transaction{store};
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
