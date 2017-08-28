//
//  Folder.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "TaskProcessor.hpp"
#include "MailProcessor.hpp"
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
        for (ssize_t i = labels.size() - 1; i >= 0; i --) {
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
    logger(spdlog::get("tasks")),
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
                // right now we don't syncback drafts

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
    store->beginTransaction();
    auto task = store->find<Task>(Query().equal("id", taskId).equal("accountId", account->id()));
    if (task != nullptr) {
        task->setShouldCancel();
        store->save(task.get());
    }
    store->commitTransaction();
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

    if (!draftJSON.count("threadId")) {
        draftJSON["threadId"] = "";
    }
    if (!draftJSON.count("gMsgId")) {
        draftJSON["gMsgId"] = "";
    }
    if (!draftJSON.count("files")) {
        draftJSON["files"] = json::array();
    }
    if (!draftJSON.count("from")) {
        draftJSON["from"] = json::array();
    }
    if (!draftJSON.count("to")) {
        draftJSON["to"] = json::array();
    }
    if (!draftJSON.count("cc")) {
        draftJSON["cc"] = json::array();
    }
    if (!draftJSON.count("bcc")) {
        draftJSON["bcc"] = json::array();
    }
    
    return Message{draftJSON};
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
    store->beginTransaction();
    
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
    store->commitTransaction();
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
    store->beginTransaction();
    
    for (auto msg : messages) {
        int suc = msg->syncUnsavedChanges() - 1;
        msg->setSyncUnsavedChanges(suc);
        if (suc == 0) {
            msg->setSyncedAt(time(0));
        }
        store->save(msg.get());
    }
    store->commitTransaction();
}

void TaskProcessor::performLocalSaveDraft(Task * task) {
    json & draftJSON = task->data()["draft"];
    Message msg = inflateDraft(draftJSON);
    store->beginTransaction();
    store->save(&msg);

    SQLite::Statement insert(store->db(), "REPLACE INTO MessageBody (id, value) VALUES (?, ?)");
    insert.bind(1, msg.id());
    insert.bind(2, draftJSON["body"].get<string>());
    insert.exec();

    store->commitTransaction();
}

void TaskProcessor::performLocalDestroyDraft(Task * task) {
    string headerMessageId = task->data()["headerMessageId"];

    logger->info("-- Deleting local drafts with headerMessageId {}", headerMessageId);

    // find anything with this header id and blow it away
    Query q = Query().equal("accountId", account->id()).equal("headerMessageId", headerMessageId).equal("draft", 1);
    auto drafts = store->findAll<Message>(q);
    json draftsJSON = json::array();
    
    // todo bodies?
    
    store->beginTransaction();
    for (auto & draft : drafts) {
        draftsJSON.push_back(draft->toJSON());
        logger->info("--- Deleting {}", draft->id());
        store->remove(draft.get());
    }
    store->commitTransaction();

    task->data()["drafts"] = draftsJSON;
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
    
    store->beginTransaction();
    
    unique_ptr<MailModel> model = store->findGeneric(type, Query().equal("id", id).equal("accountId", aid));
    
    if (model) {
        int metadataVersion = model->upsertMetadata(pluginId, value);
        data["modelMetadataNewVersion"] = metadataVersion;
        store->save(model.get());
    } else {
        logger->info("cannot apply metadata locally because no model matching type:{} id:{}, aid:{} could be found.", type, id, aid);
    }

    store->commitTransaction();
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

    // load the draft and body from the task
    json & draftJSON = task->data()["draft"];
    json & perRecipientBodies = task->data()["perRecipientBodies"];
    Message draft = inflateDraft(draftJSON);
    string body = draftJSON["body"].get<string>();

    logger->info("- Sending draft {}", draft.headerMessageId());

    // find the sent folder
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
    builder.setHTMLBody(AS_MCSTR(body));
    builder.header()->setSubject(AS_MCSTR(draft.subject()));
    builder.header()->setMessageID(AS_MCSTR(draft.headerMessageId()));
    
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

    // Send the message
    SMTPSession smtp;
    SMTPProgress sprogress;
    ErrorCode err = ErrorNone;
    Data * messageDataForSent = nullptr;
    bool multisend = perRecipientBodies.is_object();
    
    MailUtils::configureSessionForAccount(smtp, account);

    if (multisend) {
        logger->info("-- Sending customized message bodies to each recipient:");

        for (json::iterator it = perRecipientBodies.begin(); it != perRecipientBodies.end(); ++it) {
            builder.setHTMLBody(AS_MCSTR(it.value().get<string>()));
            if (it.key() == "self") {
                messageDataForSent = builder.data();
            } else {
                logger->info("-- Sending to {}", it.key());
                Address * to = Address::addressWithMailbox(AS_MCSTR(it.key()));
                Data * messageData = builder.data();
                smtp.sendMessage(builder.header()->from(), Array::arrayWithObject(to), messageData, &sprogress, &err);
                if (err != ErrorNone) {
                    throw SyncException(err, "SMTP: Delivering message.");
                }
            }
        }

        if (!messageDataForSent) {
            throw SyncException("no-self-body", "If `perRecipientBodies` is populated, you must provide a `self` entry.", false);
        }
    } else {
        logger->info("-- Sending a single message body to all recipients:");
        messageDataForSent = builder.data();
        smtp.sendMessage(messageDataForSent, &sprogress, &err);
        if (err != ErrorNone) {
            throw SyncException(err, "SMTP: Delivering message.");
        }
    }

    // Scan the sent folder for the message(s) we just sent through the SMTP gateway.
    // Some mail servers automatically place messages in the sent folder, others don't.
    //
    IMAPSearchExpression exp = IMAPSearchExpression::searchHeader(MCSTR("message-id"), AS_MCSTR(draft.headerMessageId()));
    auto uids = session->search(sentPath, &exp, &err);
    if (err != ErrorNone) {
        throw SyncException(err, "IMAP: Searching sent folder");
    }

    uint32_t sentFolderMessageUID = 0;
    
    if (multisend && (uids->count() > 0)) {
        // If we sent separate messages to each recipient, we end up with a bunch of sent
        // messages. Delete all of them since they contain the targeted bodies.
        logger->info("-- Deleting {} messages added to sent folder by the SMTP gateway.", uids->count());

        // Move messages to the trash folder
        HashMap * uidmap = nullptr;
        session->moveMessages(sentPath, uids, trashPath, &uidmap, &err);
        if (err != ErrorNone) {
            throw SyncException(err, "IMAP: Moving sent items to trash folder");
        }
        Array * auidsInTrash = uidmap->allValues();
        IndexSet uidsInTrash;
        for (int i = 0; i < auidsInTrash->count(); i ++) {
            uidsInTrash.addIndex(((Value*)auidsInTrash->objectAtIndex(i))->unsignedLongValue());
        }
        
        // Add the "DELETED" flag
        session->storeFlagsByUID(trashPath, &uidsInTrash, IMAPStoreFlagsRequestKindAdd, MessageFlagDeleted, &err);
        if (err != ErrorNone) {
            throw SyncException(err, "IMAP: Adding deleted flag to sent items");
        }
        
        // Call EXPUNGE to permanently delete messages
        session->expunge(trashPath, &err);
        
    } else if (!multisend && (uids->count() == 1)) {
        // If we find a single message in the sent folder, we'll move forward with that one.
        sentFolderMessageUID = (uint32_t)uids->allRanges()[0].location;
        logger->info("-- Found a message added to the sent folder by the SMTP gateway (UID {})", sentFolderMessageUID);
        
    } else {
        logger->info("-- No messages matching the message-id were found in the sent folder.", uids->count());
    }

    if (sentFolderMessageUID == 0) {
        // Manually place a single message in the sent folder
        IMAPProgress iprogress;
        logger->info("-- Placing a new message with `self` body in the sent folder.");
        session->appendMessage(sentPath, messageDataForSent, MessageFlagSeen, &iprogress, &sentFolderMessageUID, &err);
        if (err != ErrorNone) {
            throw SyncException(err, "IMAP: Adding message to sent folder.");
        }
    }

    // Delete the draft. Note: since the foreground sync worker is running on another thread, it's possible the
    // draft has already been converted into a message. In that case, this is a no-op and the code below will
    // do an UPDATE not an INSERT.
    performLocalDestroyDraft(task);
    
    if (sentFolderMessageUID != 0) {
        logger->info("-- Syncing sent message (UID {}) to the local mail store", sentFolderMessageUID);

        IMAPMessagesRequestKind kind = (IMAPMessagesRequestKind)(IMAPMessagesRequestKindHeaders | IMAPMessagesRequestKindFlags);
        if (session->storedCapabilities()->containsIndex(IMAPCapabilityGmail)) {
            kind = (IMAPMessagesRequestKind)(kind | IMAPMessagesRequestKindGmailLabels | IMAPMessagesRequestKindGmailThreadID | IMAPMessagesRequestKindGmailMessageID);
        }
        
        time_t syncDataTimestamp = time(0);
        IndexSet * uids = IndexSet::indexSetWithIndex(sentFolderMessageUID);
        Array * remote = session->fetchMessagesByUID(sentPath, kind, uids, nullptr, &err);
        if (err != ErrorNone) {
            throw SyncException(err, "IMAP: Retrieving the message placed in the Sent folder");
        }

        unique_ptr<Message> localMessage = nullptr;

        if (remote->count() > 0) {
            MailProcessor processor{account, store};
            IMAPMessage * remoteMsg = (IMAPMessage *)(remote->lastObject());
            processor.insertFallbackToUpdateMessage(remoteMsg, *sent, syncDataTimestamp);
            localMessage = store->find<Message>(Query().equal("accountId", account->id()).equal("remoteUID", sentFolderMessageUID));
            MessageParser * messageParser = MessageParser::messageParserWithData(messageDataForSent);
            processor.retrievedMessageBody(localMessage.get(), messageParser);
            logger->info("-- Synced sent message (UID {} = Local ID {})", sentFolderMessageUID, localMessage->id());
        } else {
            logger->info("-X New message (UID {}) could not be found via IMAP.", sentFolderMessageUID);
        }
        
        if (draft.metadata().size() > 0) {
            // retrieve the new message and queue metadata tasks on it
            for (const auto & m : draft.metadata()) {
                auto pluginId = m["pluginId"].get<string>();
                
                if (localMessage == nullptr) {
                    logger->info("-X Metadata {} not attached to sent message.", pluginId);
                    continue;
                }

                logger->info("-- Queueing task to attach {} draft metadata to new message.", pluginId);
                
                Task mTask{"SyncbackMetadataTask", account->id(), {
                    {"modelId", localMessage->id()},
                    {"modelClassName", "message"},
                    {"modelHeaderMessageId", localMessage->headerMessageId()},
                    {"pluginId", pluginId},
                    {"value", m["value"]},
                }};
                performLocalSyncbackMetadata(&mTask); // will save
                performRemoteSyncbackMetadata(&mTask); // will save again
            }
        }
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

