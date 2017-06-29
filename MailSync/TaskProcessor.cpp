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

using namespace std;
using namespace mailcore;

// Small functions that we pass to the generic ChangeMessages runner

void _applyUnread(Message * msg, json & data) {
    msg->setUnread(data["unread"].get<bool>());
}

void _applyUnreadInIMAPFolder(IMAPSession * session, String * path, IndexSet * uids, json & data) {
    ErrorCode err = ErrorCode::ErrorNone;
    if (data["unread"].get<bool>() == false) {
        session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindAdd, MessageFlagSeen, &err);
    } else {
        session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindRemove, MessageFlagSeen, &err);
    }
    if (err != ErrorCode::ErrorNone) {
        throw err;
    }
}

void _applyStarred(Message * msg, json & data) {
    msg->setStarred(data["starred"].get<bool>());
}

void _applyStarredInIMAPFolder(IMAPSession * session, String * path, IndexSet * uids, json & data) {
    ErrorCode err = ErrorCode::ErrorNone;
    if (data["starred"].get<bool>() == true) {
        session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindAdd, MessageFlagFlagged, &err);
    } else {
        session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindRemove, MessageFlagFlagged, &err);
    }
    if (err != ErrorCode::ErrorNone) {
        throw err;
    }
}

void _applyFolder(Message * msg, json & data) {
    Folder folder{data["folder"]};
    msg->setFolder(folder);
}

void _applyFolderMoveInIMAPFolder(IMAPSession * session, String * path, IndexSet * uids, json & data) {
    ErrorCode err = ErrorCode::ErrorNone;
    String dest = mailcore::String(data["folder"]["path"].get<string>().c_str());

    session->moveMessages(path, uids, &dest, nil, &err);
    if (err != ErrorCode::ErrorNone) {
        throw err;
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
    json & labels = msg->folderImapXGMLabels();
    
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
    msg->setFolderImapXGMLabels(labels);
}

void _applyLabelChangeInIMAPFolder(IMAPSession * session, String * path, IndexSet * uids, json & data) {
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
            throw err;
        }
    }
    if (toAdd->count() > 0) {
        session->storeLabelsByUID(path, uids, IMAPStoreFlagsRequestKindAdd, toAdd, &err);
        if (err != ErrorCode::ErrorNone) {
            throw err;
        }
    }
}


TaskProcessor::TaskProcessor(MailStore * store, shared_ptr<spdlog::logger> logger, IMAPSession * session) :
store(store), logger(logger), session(session) {
}

void TaskProcessor::performLocal(Task * task) {
    string cname = task->constructorName();
    
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
        
    } else if (cname == "SyncbackCategoryTask") {
        performLocalSyncbackCategory(task);
        
    } else if (cname == "DestroyDraftTask") {
        performLocalDestroyDraft(task);

    } else {
        logger->error("Unsure of how to process this task type {}", cname);
    }
    
    task->setStatus("remote");
    store->save(task);
}

void TaskProcessor::performRemote(Task * task) {
    string cname = task->constructorName();
    
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
        
    } else if (cname == "SyncbackCategoryTask") {
        performRemoteSyncbackCategory(task);
        
    } else if (cname == "DestroyDraftTask") {
        
    } else {
        logger->error("Unsure of how to process this task type {}", cname);
    }

    task->setStatus("complete");
    store->save(task);
}

#pragma mark Privates

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
        Query byId = Query().equal("id", messageIds);
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

    auto allLabels = store->allLabelsCache();
    json imapData {};
    
    for (auto pair : models.threads) {
        auto thread = pair.second;

        for (auto msg : models.messages) {
            if (msg->threadId() != pair.first) {
                continue;
            }

            // accumulate folder paths / folder UIDs to make performRemote easy
            string folderPath = msg->folder()["path"].get<string>();
            if (!imapData.count(folderPath)) {
                imapData[folderPath] = json::array();
            }
            imapData[folderPath].push_back(msg->folderImapUID());

            // perform local changes
            thread->prepareToReaddMessage(msg.get(), allLabels);
            modifyLocalMessage(msg.get(), data);
            thread->addMessage(msg.get(), allLabels);
            store->save(msg.get());
        }

        store->save(thread.get());
    }
    
    data["imapData"] = imapData;
    store->commitTransaction();
}

void TaskProcessor::performRemoteChangeOnMessages(Task * task, void (*applyInFolder)(IMAPSession * session, String * path, IndexSet * uids, json & data)) {
    // perform the remote action on the impacted messages
    json & data = task->data();
    
    if (!data.count("imapData")) {
        throw "PerformLocal must be run first.";
    }
    
    json & imapData = data["imapData"];
    for (json::iterator it = imapData.begin(); it != imapData.end(); ++it) {
        String path = mailcore::String(it.key().c_str());
        IndexSet uids;
        for (auto & id : it.value()) {
            uids.addIndex(id.get<uint32_t>());
        }
        applyInFolder(session, &path, &uids, data);
    }
}

void TaskProcessor::performLocalSaveDraft(Task * task) {
    json & draftJSON = task->data()["draft"];
    
    Query q = Query().equal("role", "drafts");
    auto folder = store->find<Folder>(q);
    if (folder.get() == nullptr) {
        q = Query().equal("role", "all");
        folder = store->find<Folder>(q);
    }
    if (folder == nullptr) {
        return;
    }
    draftJSON["folder"] = folder->toJSON();
    
    // set other JSON attributes the client may not have populated,
    // but we require to be non-null
    draftJSON["draft"] = true;
    draftJSON["date"] = time(0);
    if (!draftJSON.count("threadId")) {
        draftJSON["threadId"] = "";
    }
    if (!draftJSON.count("gMsgId")) {
        draftJSON["gMsgId"] = "";
    }

    Message msg{draftJSON};
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
    Query q = Query().equal("headerMessageId", headerMessageId).equal("draft", 1);
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
        logger->error("Creating a folder/label failed: {}", err);
        data["created"] = nullptr;
        return;
    }
    
    // must go beneath the first use of session above.
    bool isGmail = session->storedCapabilities()->containsIndex(IMAPCapabilityGmail);
    
    if (isGmail) {
        Label l = Label(MailUtils::idForFolder(path), accountId, 0);
        l.setPath(path);
        data["created"] = l.toJSON();
    } else {
        Folder f = Label(MailUtils::idForFolder(path), accountId, 0);
        f.setPath(path);
        data["created"] = f.toJSON();
    }
    
    logger->info("Syncback of folder/label '{}' succeeded.", path);
}
