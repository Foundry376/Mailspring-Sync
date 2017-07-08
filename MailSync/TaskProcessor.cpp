//
//  Folder.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "TaskProcessor.hpp"
#include "MailUtils.hpp"
#include "Thread.hpp"
#include "Message.hpp"
#include "MailUtils.hpp"
#include "File.hpp"
#include "constants.h"
#include "ProgressCollectors.hpp"
#include "SyncException.hpp"

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
    
    try {
        string accountId = task->data()["accountId"].get<string>();
        if (accountId != account->id()) {
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

        } else {
            logger->error("Unsure of how to process this task type {}", cname);
        }

        task->setStatus("remote");
    } catch (SyncException & ex) {
        task->setError(ex.toJSON());
        task->setStatus("complete");
    }
    
    store->save(task);
}

void TaskProcessor::performRemote(Task * task) {
    string cname = task->constructorName();
    try {
        string accountId = task->data()["accountId"].get<string>();
        if (accountId != account->id()) {
            throw SyncException("generic", "You must provide an account id.", false);
        }

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

        } else {
            logger->error("Unsure of how to process this task type {}", cname);
        }
        task->setStatus("complete");
    } catch (SyncException & ex) {
        task->setError(ex.toJSON());
        task->setStatus("complete");
    }
    store->save(task);
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
    draftJSON["date"] = time(0);
    if (!draftJSON.count("threadId")) {
        draftJSON["threadId"] = "";
    }
    if (!draftJSON.count("gMsgId")) {
        draftJSON["gMsgId"] = "";
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

    // find anything with this header id and blow it away
    Query q = Query().equal("accountId", account->id()).equal("headerMessageId", headerMessageId).equal("draft", 1);
    auto drafts = store->findAll<Message>(q);
    json draftsJSON = json::array();
    
    // todo bodies?
    
    store->beginTransaction();
    for (auto & draft : drafts) {
        draftsJSON.push_back(draft->toJSON());
        store->remove(draft.get());
    }
    store->commitTransaction();

    task->data()["drafts"] = draftsJSON;
}

void TaskProcessor::performLocalSyncbackCategory(Task * task) {
    
}

void TaskProcessor::performRemoteSyncbackCategory(Task * task) {
    json & data = task->data();
    string path = data["path"].get<string>();
    string accountId = data["accountId"].get<string>();
    
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

void TaskProcessor::performRemoteDestroyCategory(Task * task) {
    json & data = task->data();
    string accountId = data["accountId"].get<string>();
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
    
    // find the sent folder
    auto sent = store->find<Folder>(Query().equal("accountId", account->id()).equal("role", "sent"));
    if (sent == nullptr) {
        sent = store->find<Label>(Query().equal("accountId", account->id()).equal("role", "sent"));
        if (sent == nullptr) {
            throw SyncException("no-sent-folder", "", false);
        }
    }
    String * sentPath = AS_MCSTR(sent->path());

    // find the trash folder
    auto trash = store->find<Folder>(Query().equal("accountId", account->id()).equal("role", "trash"));
    if (trash == nullptr) {
        throw SyncException("no-trash-folder", "", false);
    }
    String * trashPath = AS_MCSTR(trash->path());

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
        string path = MailUtils::pathForFile(FILES_ROOT, &file, false);
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
    
    MailUtils::configureSessionForAccount(smtp, account);

    if (perRecipientBodies.is_object()) {
        Data * selfMessageData = nullptr;

        // Send a copy of the message for each recipient
        for (json::iterator it = perRecipientBodies.begin(); it != perRecipientBodies.end(); ++it) {
            builder.setHTMLBody(AS_MCSTR(it.value().get<string>()));
            if (it.key() == "self") {
                selfMessageData = builder.data();
            } else {
                Address * to = Address::addressWithMailbox(AS_MCSTR(it.key()));
                Data * messageData = builder.data();
                smtp.sendMessage(builder.header()->from(), Array::arrayWithObject(to), messageData, &sprogress, &err);
                if (err != ErrorNone) {
                    throw SyncException(err, "SMTP: Delivering message.");
                }
            }
        }

        if (!selfMessageData) {
            throw SyncException("no-self-body", "If `perRecipientBodies` is populated, you must provide a `self` entry.", false);
        }
        
        // Scan the sent folder, deleting anything with our header messageId.
        // Gmail and possibly others place messages here automatically when we send.
        IMAPSearchExpression exp = IMAPSearchExpression::searchHeader(MCSTR("message-id"), AS_MCSTR(draft.headerMessageId()));
        
        auto uids = session->search(sentPath, &exp, &err);
        if (err != ErrorNone) {
            throw SyncException(err, "IMAP: Searching sent folder");
        }
        
        if (uids->count() > 0) {
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
            session->storeFlagsByUID(sentPath, &uidsInTrash, IMAPStoreFlagsRequestKindAdd, MessageFlagDeleted, &err);
            if (err != ErrorNone) {
                throw SyncException(err, "IMAP: Adding deleted flag to sent items");
            }
        }

        // Place in sent folder
        IMAPProgress iprogress;
        uint32_t uid;
        session->appendMessage(sentPath, selfMessageData, MessageFlagSeen, &iprogress, &uid, &err);
        if (err != ErrorNone) {
            throw SyncException(err, "IMAP: Adding message to sent folder.");
        }

    } else {
        smtp.sendMessage(builder.data(), &sprogress, &err);
        if (err != ErrorNone) {
            throw SyncException(err, "SMTP: Delivering message.");
        }
    }

    // Immediately sync the new message in the sent folder?

    // Delete the local draft if one exists
    performLocalDestroyDraft(task);
//    performRemoteDestroyDraft();
}
