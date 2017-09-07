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
    msg->setClientFolder(folder);
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
        msg->setRemoteFolder(destFolder);
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

        } else {
            logger->error("Unsure of how to process this task type {}", cname);
        }

        logger->info("[{}] -- Succeeded. Changing status to `remote`", task->id());
        task->setStatus("remote");

    } catch (SyncException & ex) {
        logger->info("[{}] -- Failed ({}). Changing status to `complete`", task->id(), ex.toJSON());
        task->setError(ex.toJSON());
        task->setStatus("complete");
    }
    
    store->save(task);
}

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

            } else {
                logger->error("Unsure of how to process this task type {}", cname);
            }

            logger->info("[{}] -- Succeeded. Changing status to `complete`", task->id());
            task->setStatus("complete");
        }
    } catch (SyncException & ex) {
        logger->info("[{}] -- Failed ({}). Changing status to `complete`", task->id(), ex.toJSON());
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
        throw;
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

ChangeMailModels TaskProcessor::inflateThreadsAndMessages(json & data) {
    ChangeMailModels models;

    if (data.count("threadIds")) {
        vector<string> threadIds{};
        for (auto & member : data["threadIds"]) {
            threadIds.push_back(member.get<string>());
        }
        Query byId = Query().equal("id", threadIds);
        models.threads = store->findAllMap<Thread>(byId, "id");
        Query byThreadId = Query().equal("threadId", threadIds);
        models.messages = store->findAll<Message>(byThreadId);
        
    } else if (data.count("messageIds")) {
        vector<string> messageIds{};
        for (auto & member : data["messageIds"]) {
            messageIds.push_back(member.get<string>());
        }
        Query byId = Query().equal("id", messageIds); // TODO This will break if more than 500 messages on a thread
        models.messages = store->findAll<Message>(byId);
        vector<string> threadIds{};
        for (auto & msg : models.messages) {
            threadIds.push_back(msg->threadId()); // todo sending dupes here
        }
        byId = Query().equal("id", threadIds);
        models.threads = store->findAllMap<Thread>(byId, "id");
    }
    
    return models;
}

void TaskProcessor::performLocalChangeOnMessages(Task * task, void (*modifyLocalMessage)(Message *, json &)) {
    MailStoreTransaction transaction{store};
    
    json & data = task->data();
    ChangeMailModels models = inflateThreadsAndMessages(data);
    
    auto allLabels = store->allLabelsCache(task->accountId());
    
    for (auto pair : models.threads) {
        auto thread = pair.second;

        for (auto msg : models.messages) {
            if (msg->threadId() != pair.first) {
                continue;
            }

            // perform local changes
            thread->prepareToReaddMessage(msg.get(), allLabels);
            modifyLocalMessage(msg.get(), data);
            
            // prevent remote changes to this message for 24 hours
            // so the changes aren't reverted by sync before we can syncback.
            msg->setSyncUnsavedChanges(msg->syncUnsavedChanges() + 1);
            msg->setSyncedAt(time(0) + 24 * 60 * 60);
            
            thread->addMessage(msg.get(), allLabels);
            store->save(msg.get());
        }

        store->save(thread.get());
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
        store->save(&msg);

        SQLite::Statement insert(store->db(), "REPLACE INTO MessageBody (id, value) VALUES (?, ?)");
        insert.bind(1, msg.id());
        insert.bind(2, draftJSON["body"].get<string>());
        insert.exec();

        transaction.commit();
    }
}

void TaskProcessor::performLocalDestroyDraft(Task * task) {
    vector<string> messageIds = task->data()["messageIds"];

    logger->info("-- Deleting local drafts by ID");

    // find anything with this header id and blow it away
    json draftsJSON = json::array();
    SQLite::Statement removeBody(store->db(), "DELETE FROM MessageBody WHERE id = ?");
    auto drafts = store->findAll<Message>(Query().equal("id", messageIds));
    
    {
        MailStoreTransaction transaction{store};

        for (auto & draft : drafts) {
            if (!draft->isDraft()) {
                continue;
            }
            if (draft->accountId() != account->id()) {
                continue;
            }
            draftsJSON.push_back(draft->toJSON());
            logger->info("--- Deleting {}", draft->id());
            store->remove(draft.get());
            
            removeBody.bind(1, draft->id());
            removeBody.exec();
        }
        transaction.commit();
    }
    
    task->data()["drafts"] = draftsJSON;
}

void TaskProcessor::performRemoteDestroyDraft(Task * task) {
    json & deletedDraftJSONs = task->data()["drafts"];
    
    for (json & d : deletedDraftJSONs) {
        auto draft = Message(d);
        auto uids = IndexSet::indexSetWithIndex(draft.remoteUID());

        logger->info("-- Deleting remote draft {}", draft.id());

        // Find the trash folder
        auto trash = store->find<Folder>(Query().equal("accountId", account->id()).equal("role", "trash"));
        if (trash == nullptr) {
            throw SyncException("no-trash-folder", "", false);
        }
        String * trashPath = AS_MCSTR(trash->path());
        logger->info("-- Identified `trash` folder: {}", trash->path());

        // Move to the trash folder
        HashMap * uidmap = nullptr;
        ErrorCode err = ErrorCode::ErrorNone;
        Array * auidsInTrash = nullptr;
        IndexSet uidsInTrash;
        String * folder = AS_MCSTR(draft.remoteFolder()["path"].get<string>());
        session->moveMessages(folder, uids, trashPath, &uidmap, &err);
        if (err != ErrorNone) {
            continue; // eh, draft may be gone, oh well
        }
        if (uidmap == nullptr) {
            continue; // eh, draft may be gone, oh well
        }
        
        // Add the "DELETED" flag
        auidsInTrash = uidmap->allValues();
        for (int i = 0; i < auidsInTrash->count(); i ++) {
            uidsInTrash.addIndex(((Value*)auidsInTrash->objectAtIndex(i))->unsignedLongValue());
        }
        session->storeFlagsByUID(trashPath, &uidsInTrash, IMAPStoreFlagsRequestKindAdd, MessageFlagDeleted, &err);
        if (err != ErrorNone) {
            throw SyncException(err, "add deleted flag");
        }
    }
}

void TaskProcessor::performLocalSyncbackCategory(Task * task) {
    
}

void TaskProcessor::performRemoteSyncbackCategory(Task * task) {
    json & data = task->data();
    string accountId = task->accountId();
    string path = data["path"].get<string>();
    
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
    
    if (isGmail) {
        Label l = Label(MailUtils::idForFolder(accountId, path), accountId, 0);
        l.setPath(path);
        data["created"] = l.toJSON();
    } else {
        Folder f = Label(MailUtils::idForFolder(accountId, path), accountId, 0);
        f.setPath(path);
        data["created"] = f.toJSON();
    }
    
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
    const json results = MakeAccountsRequest(account, "/metadata/" + id + "/" + pluginId, "POST", payload);
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

    json & fromP = draft.from().at(0);
    builder.header()->setFrom(MailUtils::addressFromContactJSON(fromP));
    
    for (json & fileJSON : draft.files()) {
        File file{fileJSON};
        string root = string(getenv("CONFIG_DIR_PATH")) + "/files";
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
        int delay[] = {0, 1, 3, 5, 5, 10};
        IndexSet * uids = new IndexSet();

        while (tries < 5) {
            if (delay[tries]) {
                logger->info("-- No messages found. Sleeping {} to wait for sent folder to settle...", delay[tries]);
				std::this_thread::sleep_for(std::chrono::seconds(delay[tries]));
            }

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
            tries ++;
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

