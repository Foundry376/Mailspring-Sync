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
#include "Event.hpp"
#include "icalendar.h"
#include "constants.h"
#include "ProgressCollectors.hpp"
#include "SyncException.hpp"
#include "NetworkRequestUtils.hpp"

#include <sstream>
#include <algorithm>
#include <set>
#include <deque>
#include <iomanip>
#include <thread>
#include <chrono>

#if defined(_MSC_VER)
#include <direct.h>
#include <codecvt>
#include <locale>
#include <sys/utime.h>
#else
#include <sys/time.h>
#endif

using namespace std;
using namespace mailcore;
using namespace nlohmann;

static void setFileModificationTime(const string & filepath, time_t timestamp) {
#ifdef _MSC_VER
    wstring_convert<codecvt_utf8<wchar_t>, wchar_t> convert;
    struct _utimbuf times;
    times.actime = timestamp;
    times.modtime = timestamp;
    _wutime(convert.from_bytes(filepath).c_str(), &times);
#else
    struct timeval times[2];
    times[0].tv_sec = timestamp;
    times[0].tv_usec = 0;
    times[1].tv_sec = timestamp;
    times[1].tv_usec = 0;
    utimes(filepath.c_str(), times);
#endif
}

struct PlacementMove {
    Placement placement;
    string destFolderId;
};

static IndexSet * _uidsOf(vector<TaskPlacement *> & items) {
    IndexSet * uids = IndexSet::indexSet();
    for (auto item : items) {
        uids->addIndex(item->placement.remoteUID);
    }
    return uids;
}

static vector<string> _labelsOf(const Placement & p) {
    vector<string> labels;
    for (auto & l : p.labels) {
        labels.push_back(l.get<string>());
    }
    return labels;
}

static json _clientVisibleState(Message & msg) {
    return json{
        {"folders", msg.folders()},
        {"labels", msg.labels()},
        {"unread", msg.isUnread()},
        {"starred", msg.isStarred()},
        {"draft", msg.isDraft()},
    };
}

static bool _hasLiveCopyIn(const vector<Placement> & placements, const string & folderId);

static bool _hasLiveCopyIn(MailStore * store, Message & msg, const string & folderId) {
    return _hasLiveCopyIn(store->placementsForMessage(msg.id()), folderId);
}

// The UID each item's copy received in `dest`, aligned with `items` (0 when unknown):
// from the COPYUID map when the server has UIDPLUS, otherwise by fetching the newest
// headers in the destination and matching them by message id, which is what makes the
// fallback work - the id does not depend on the folder.
static vector<uint32_t> _resolveNewUIDs(IMAPSession * session, HashMap * uidmap, Folder & dest, vector<TaskPlacement *> & items, const vector<uint32_t> & sourceUIDs) {
    vector<uint32_t> result(items.size(), 0);
    ErrorCode err = ErrorCode::ErrorNone;
    String * destPath = AS_MCSTR(dest.path());

    if (uidmap != nullptr) {
        for (size_t i = 0; i < items.size(); i++) {
            Value * newUID = (Value *)uidmap->objectForKey(Value::valueWithUnsignedLongValue(sourceUIDs[i]));
            if (newUID) {
                result[i] = newUID->unsignedIntValue();
            }
        }
        return result;
    }

    auto status = session->folderStatus(destPath, &err);
    if (status == nullptr) {
        return result;
    }
    // Moves append at the top of the destination; twice the item count covers gaps in
    // UID assignment without underflowing below 1.
    IMAPMessagesRequestKind kind = MailUtils::messagesRequestKindFor(session->storedCapabilities(), true);
    uint32_t uidNext = status->uidNext();
    uint32_t searchRange = (uint32_t)items.size() * 2;
    uint32_t min = (uidNext > searchRange) ? (uidNext - searchRange) : 1;
    IndexSet * set = IndexSet::indexSetWithRange(RangeMake(min, UINT64_MAX));
    Array * recent = session->fetchMessagesByUID(destPath, kind, set, nullptr, &err);
    if (recent == nullptr) {
        return result;
    }
    // Several copies of one message (Exchange duplicates) arrive as several UIDs that
    // hash to the same id; each moved copy takes one so no two commit to the same UID.
    map<string, deque<uint32_t>> uidsById;
    for (unsigned int ii = 0; ii < recent->count(); ii++) {
        IMAPMessage * m = (IMAPMessage *)recent->objectAtIndex(ii);
        uidsById[MailUtils::idForMessage(dest.accountId(), dest.path(), m)].push_back(m->uid());
    }
    for (auto & pair : uidsById) {
        std::sort(pair.second.begin(), pair.second.end());
    }
    for (size_t i = 0; i < items.size(); i++) {
        auto it = uidsById.find(items[i]->message->id());
        if (it != uidsById.end() && !it->second.empty()) {
            result[i] = it->second.front();
            it->second.pop_front();
        }
    }
    return result;
}

// Moves the items' copies out of `path` into `dest` with UID MOVE, or COPY + \Deleted +
// EXPUNGE when the server lacks MOVE, and records each copy's new UID on the item. A copy
// whose new UID cannot be determined is logged and left as it was: its row keeps the
// optimistic marker and the destination's next scan records it.
static void _moveMessagesResilient(IMAPSession * session, String * path, Folder & dest, vector<TaskPlacement *> & items) {
    ErrorCode err = ErrorCode::ErrorNone;
    HashMap * uidmap = nullptr;
    String * destPath = AS_MCSTR(dest.path());
    IndexSet * uids = _uidsOf(items);
    bool mustApplyAttributes = false;

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

    vector<uint32_t> sourceUIDs;
    for (auto item : items) {
        sourceUIDs.push_back(item->placement.remoteUID);
    }
    auto newUIDs = _resolveNewUIDs(session, uidmap, dest, items, sourceUIDs);
    for (size_t i = 0; i < items.size(); i++) {
        if (newUIDs[i] == 0) {
            spdlog::get("logger")->error("-- Could not find new UID for message {} moved to {}", items[i]->message->id(), dest.path());
            continue;
        }
        items[i]->moved = true;
        items[i]->movedUID = newUIDs[i];
    }

    if (mustApplyAttributes) {
        for (auto item : items) {
            if (!item->moved) {
                continue;
            }
            auto & p = item->placement;
            MessageFlag flags = MessageFlagNone;
            if (p.starred)
                flags = (MessageFlag)(flags | MessageFlagFlagged);
            if (!p.unread)
                flags = (MessageFlag)(flags | MessageFlagSeen);
            if (p.draft)
                flags = (MessageFlag)(flags | MessageFlagDraft);
            if (flags != MessageFlagNone) {
                session->storeFlagsByUID(destPath, IndexSet::indexSetWithIndex(item->movedUID), IMAPStoreFlagsRequestKindSet, flags, &err);
            }
        }
    }
}

// Copies the items' moved copies from `path` into `dest` and returns the UID each one
// received there, aligned with `items` (0 when unknown).
static vector<uint32_t> _copyMessagesResilient(IMAPSession * session, String * path, Folder & dest, vector<TaskPlacement *> & items) {
    ErrorCode err = ErrorCode::ErrorNone;
    HashMap * uidmap = nullptr;
    IndexSet * uids = IndexSet::indexSet();
    vector<uint32_t> sourceUIDs;
    for (auto item : items) {
        uids->addIndex(item->movedUID);
        sourceUIDs.push_back(item->movedUID);
    }
    session->copyMessages(path, uids, AS_MCSTR(dest.path()), &uidmap, &err);
    if (err != ErrorCode::ErrorNone) {
        throw SyncException(err, "copyMessages");
    }
    return _resolveNewUIDs(session, uidmap, dest, items, sourceUIDs);
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

// Deletes the message's live server copies, grouped by folder. Used for drafts, whose
// copies are never moved anywhere.
static void _removeMessageCopiesResilient(IMAPSession * session, MailStore * store, string accountId, Message & msg) {
    map<string, IndexSet *> uidsByPath;
    for (auto & p : store->placementsForMessage(msg.id())) {
        if (!p.isLive() || p.remoteUID == 0) {
            continue;
        }
        auto folder = store->folderById(accountId, p.folderId);
        if (folder == nullptr) {
            continue;
        }
        if (!uidsByPath.count(folder->path())) {
            uidsByPath[folder->path()] = IndexSet::indexSet();
        }
        uidsByPath[folder->path()]->addIndex(p.remoteUID);
    }
    for (auto & pair : uidsByPath) {
        spdlog::get("logger")->info("-- Deleting {} copies of {} from {}", pair.second->count(), msg.id(), pair.first);
        _removeMessagesResilient(session, store, accountId, AS_MCSTR(pair.first), pair.second);
    }
}


// Small functions that we pass to the generic ChangeMessages runner.

void _applyUnread(MailStore * store, Message * msg, const vector<Placement> & placements, json & data) {
    store->setPlacementUnread(*msg, data["unread"].get<bool>());
}

void _applyUnreadInIMAPFolder(IMAPSession * session, MailStore * store, string accountId, Folder & source, vector<TaskPlacement *> & items, json & data) {
    ErrorCode err = ErrorCode::ErrorNone;
    String * path = AS_MCSTR(source.path());
    IndexSet * uids = _uidsOf(items);
    if (data["unread"].get<bool>() == false) {
        session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindAdd, MessageFlagSeen, &err);
    } else {
        session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindRemove, MessageFlagSeen, &err);
    }
    if (err != ErrorCode::ErrorNone) {
        throw SyncException(err, "storeFlagsByUID");
    }
}

void _applyStarred(MailStore * store, Message * msg, const vector<Placement> & placements, json & data) {
    store->setPlacementStarred(*msg, data["starred"].get<bool>());
}

void _applyStarredInIMAPFolder(IMAPSession * session, MailStore * store, string accountId, Folder & source, vector<TaskPlacement *> & items, json & data) {
    ErrorCode err = ErrorCode::ErrorNone;
    String * path = AS_MCSTR(source.path());
    IndexSet * uids = _uidsOf(items);
    if (data["starred"].get<bool>() == true) {
        session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindAdd, MessageFlagFlagged, &err);
    } else {
        session->storeFlagsByUID(path, uids, IMAPStoreFlagsRequestKindRemove, MessageFlagFlagged, &err);
    }
    if (err != ErrorCode::ErrorNone) {
        throw SyncException(err, "storeFlagsByUID");
    }
}

static vector<string> _restoreFolderIdsFor(Message * msg, json & data) {
    vector<string> ids;
    if (!data.count("restorePlacements") || !data["restorePlacements"].is_object()) {
        return ids;
    }
    auto & restore = data["restorePlacements"];
    if (!restore.count(msg->id()) || !restore[msg->id()].is_array()) {
        return ids;
    }
    for (auto & entry : restore[msg->id()]) {
        if (entry.count("removed") && entry["removed"].is_boolean() && entry["removed"].get<bool>()) {
            continue;
        }
        ids.push_back(entry["folderId"].get<string>());
    }
    return ids;
}

/*
 Which copies a ChangeFolderTask moves, and where. Both phases derive this from the task
 data and the message's current rows, so the remote phase finds the same copies after the
 local phase (which only marks them) and after an earlier task has moved them on. A copy
 at UID 0 is never selected: no scan can report where it went, so a marker on it would
 show it in the destination forever.

 An undo task carries `restorePlacements` ({ messageId: [{folderId, remoteUID, removed?}] },
 the `undoPlacements` recorded by the original task) and its `folder` / `sourceFolderIds`
 are ignored. The copies to move back are those now outside the recorded folders - a copy
 in a recorded folder is home, and Sent / Drafts copies are only ever taken when the user
 pointed at that folder. They go back to the recorded folders in order, surplus ones to
 the first; a recorded folder left empty is filled by COPY (_restoreAdditionalCopies). A
 copy that is home but still marked pending is moved in place so the marker clears. A
 `removed` entry names a copy the original task deleted because the destination already
 held one; it is not a recorded folder, so that pre-existing copy is left alone.

 Otherwise a move to Trash or Spam takes every copy; any other move takes the copies in
 `sourceFolderIds` when the client named the folder it was looking at, else every copy
 outside Sent and Drafts. A copy is matched by the folder it is in or the one it is
 optimistically shown in, a copy already marked for this destination is always included
 so a re-run finishes it, and a copy already in the destination is only touched to pull
 it back from a pending move elsewhere.
 */
static vector<PlacementMove> _movesForMessage(MailStore * store, Message * msg, const vector<Placement> & placements, json & data) {
    vector<PlacementMove> moves;

    if (data.count("restorePlacements") && data["restorePlacements"].is_object()) {
        vector<string> targets = _restoreFolderIdsFor(msg, data);
        if (targets.empty()) {
            return moves;
        }
        set<string> home(targets.begin(), targets.end());
        vector<Placement> displaced;
        for (auto & p : placements) {
            if (!p.isLive() || p.remoteUID == 0) {
                continue;
            }
            string role = msg->folderRole(store, p.folderId);
            if (home.count(p.folderId) || role == "sent" || role == "drafts") {
                if (!p.pendingFolderId.empty()) {
                    moves.push_back({p, p.folderId});
                }
                continue;
            }
            displaced.push_back(p);
        }
        std::sort(displaced.begin(), displaced.end(), [](const Placement & a, const Placement & b) {
            return a.folderId != b.folderId ? a.folderId < b.folderId : a.remoteUID < b.remoteUID;
        });
        for (size_t i = 0; i < displaced.size(); i++) {
            moves.push_back({displaced[i], i < targets.size() ? targets[i] : targets[0]});
        }
        return moves;
    }

    string dest = data["folder"]["id"].get<string>();
    string destRole = data["folder"].count("role") && data["folder"]["role"].is_string() ? data["folder"]["role"].get<string>() : "";
    bool everyCopy = destRole == "trash" || destRole == "spam";
    set<string> sources;
    if (data.count("sourceFolderIds") && data["sourceFolderIds"].is_array()) {
        for (auto & id : data["sourceFolderIds"]) {
            sources.insert(id.get<string>());
        }
    }

    auto matches = [&](const string & folderId) {
        if (everyCopy) {
            return true;
        }
        if (!sources.empty()) {
            return sources.count(folderId) > 0;
        }
        string role = msg->folderRole(store, folderId);
        return role != "sent" && role != "drafts";
    };

    for (auto & p : placements) {
        if (!p.isLive() || p.remoteUID == 0) {
            continue;
        }
        bool pendingElsewhere = !p.pendingFolderId.empty() && p.pendingFolderId != dest;
        bool selected;
        if (p.pendingFolderId == dest) {
            selected = true;
        } else if (p.folderId == dest) {
            selected = pendingElsewhere && matches(p.pendingFolderId);
        } else {
            selected = matches(p.folderId) || (pendingElsewhere && matches(p.pendingFolderId));
        }
        if (selected) {
            moves.push_back({p, dest});
        }
    }
    return moves;
}

static bool _hasLiveCopyIn(const vector<Placement> & placements, const string & folderId) {
    for (auto & p : placements) {
        if (p.isLive() && p.remoteUID > 0 && p.folderId == folderId) {
            return true;
        }
    }
    return false;
}

// Marks the selected copies so the client sees them in the destination immediately, and
// records what moved as `undoPlacements` on the task for the client's undo. A copy bound
// for a folder that already holds one is recorded as `removed`: the remote phase deletes
// it rather than moving it (_applyFolderMoveInIMAPFolder), so an undo has nothing to
// move back.
void _applyFolder(MailStore * store, Message * msg, const vector<Placement> & placements, json & data) {
    json undo = json::array();
    for (auto & move : _movesForMessage(store, msg, placements, data)) {
        json entry = {{"folderId", move.placement.folderId}, {"remoteUID", move.placement.remoteUID}};
        if (move.placement.folderId != move.destFolderId && _hasLiveCopyIn(placements, move.destFolderId)) {
            entry["removed"] = true;
        }
        undo.push_back(entry);
        store->beginPlacementMove(*msg, move.placement.folderId, move.placement.remoteUID, move.destFolderId);
    }
    if (!undo.empty()) {
        data["undoPlacements"][msg->id()] = undo;
    }
}

static shared_ptr<Folder> _moveDestination(MailStore * store, string accountId, string folderId, json & data) {
    auto folder = store->folderById(accountId, folderId);
    if (folder == nullptr && data["folder"].is_object() && data["folder"]["id"].get<string>() == folderId) {
        folder = make_shared<Folder>(data["folder"]);
    }
    if (folder == nullptr) {
        throw SyncException("no-matching-folder", "The destination folder no longer exists.", false);
    }
    return folder;
}

/*
 Moves the items' copies out of `source`. A copy of a message that already has a live copy
 in the destination is deleted instead of moved, so the user ends up with one copy there.
 */
void _applyFolderMoveInIMAPFolder(IMAPSession * session, MailStore * store, string accountId, Folder & source, vector<TaskPlacement *> & items, json & data) {
    String * path = AS_MCSTR(source.path());

    map<string, vector<TaskPlacement *>> byDest;
    for (auto item : items) {
        byDest[item->destFolderId].push_back(item);
    }

    for (auto & pair : byDest) {
        auto dest = _moveDestination(store, accountId, pair.first, data);
        vector<TaskPlacement *> toMove;
        vector<TaskPlacement *> toRemove;
        for (auto item : pair.second) {
            if (item->placement.folderId == dest->id()) {
                item->moved = true;
                item->movedUID = item->placement.remoteUID;
            } else if (_hasLiveCopyIn(store, *item->message, dest->id())) {
                toRemove.push_back(item);
            } else {
                toMove.push_back(item);
            }
        }
        if (!toMove.empty()) {
            _moveMessagesResilient(session, path, *dest, toMove);
        }
        if (!toRemove.empty()) {
            spdlog::get("logger")->info("-- {} already holds {} of the messages, deleting their copies in {}", dest->path(), toRemove.size(), source.path());
            _removeMessagesResilient(session, store, accountId, path, _uidsOf(toRemove));
            for (auto item : toRemove) {
                item->removed = true;
            }
        }
    }
}

// Undo of a move that collapsed several copies of a message into one folder: after the
// copies that came back have been spread over the recorded folders, a recorded folder
// still without a copy gets one by COPY from the first restored copy. Runs once over all
// of a task's items because a message's copies can come back from different folders.
static void _restoreAdditionalCopies(IMAPSession * session, MailStore * store, string accountId, deque<TaskPlacement> & items, json & data) {
    if (!data.count("restorePlacements") || !data["restorePlacements"].is_object()) {
        return;
    }
    map<string, vector<TaskPlacement *>> movedByMessage;
    for (auto & item : items) {
        if (item.moved) {
            movedByMessage[item.message->id()].push_back(&item);
        }
    }

    map<pair<string, string>, vector<TaskPlacement *>> copiesByFolders;
    for (auto & pair : movedByMessage) {
        TaskPlacement * source = pair.second.front();
        set<string> covered;
        for (auto item : pair.second) {
            covered.insert(item->destFolderId);
        }
        for (auto & folderId : _restoreFolderIdsFor(source->message.get(), data)) {
            if (covered.count(folderId) || _hasLiveCopyIn(store, *source->message, folderId)) {
                continue;
            }
            covered.insert(folderId);
            copiesByFolders[{source->destFolderId, folderId}].push_back(source);
        }
    }
    for (auto & pair : copiesByFolders) {
        auto from = store->folderById(accountId, pair.first.first);
        auto to = store->folderById(accountId, pair.first.second);
        if (from == nullptr || to == nullptr) {
            continue;
        }
        spdlog::get("logger")->info("-- Restoring {} copies to {}", pair.second.size(), to->path());
        auto newUIDs = _copyMessagesResilient(session, AS_MCSTR(from->path()), *to, pair.second);
        for (size_t i = 0; i < pair.second.size(); i++) {
            if (newUIDs[i] == 0) {
                spdlog::get("logger")->error("-- Could not find new UID for message {} copied to {}", pair.second[i]->message->id(), to->path());
                continue;
            }
            pair.second[i]->copies.push_back({to->id(), newUIDs[i]});
        }
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

void _applyLabels(MailStore * store, Message * msg, const vector<Placement> & placements, json & data) {
    json & toAdd = data["labelsToAdd"];
    json & toRemove = data["labelsToRemove"];
    vector<string> labels;
    for (auto & existing : msg->labels()) {
        labels.push_back(existing.get<string>());
    }
    
    for (auto & item : toAdd) {
        string xgmValue = _xgmKeyForLabel(item);
        if (std::find(labels.begin(), labels.end(), xgmValue) == labels.end()) {
            labels.push_back(xgmValue);
        }
    }
    for (auto & item : toRemove) {
        string xgmValue = _xgmKeyForLabel(item);
        labels.erase(std::remove(labels.begin(), labels.end(), xgmValue), labels.end());
    }
    // MessageAttributesForMessage sorts the labels it reads from the server and
    // MessageAttributesMatch compares the arrays positionally, so an unsorted local
    // set would register as a change on the next scan and cost a no-op update + delta.
    sort(labels.begin(), labels.end());
    store->setPlacementLabels(*msg, labels);
}

void _applyLabelChangeInIMAPFolder(IMAPSession * session, MailStore * store, string accountId, Folder & source, vector<TaskPlacement *> & items, json & data) {
    AutoreleasePool pool;

    ErrorCode err = ErrorCode::ErrorNone;
    String * path = AS_MCSTR(source.path());
    IndexSet * uids = _uidsOf(items);
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

// A task whose JSON has (say) a null where we expect a string throws json::type_error,
// which isn't a SyncException and used to reach the worker's `catch (...) { abort(); }`,
// leaving the row at status=remote to kill the next launch too. Fail the task instead
// so it drains — the client reports this through Task.onError().
static json errorJSONForUnexpectedException(string what) {
    // Goes into the task's data JSON, which MailStore dumps to SQLite, and dump()
    // throws on invalid UTF-8 — so scrub to printable ASCII.
    string safe;
    safe.reserve(what.size());
    for (char c : what) {
        safe += (c >= 0x20 && c <= 0x7E) ? c : '?';
    }
    if (safe.size() > 1024) {
        safe.resize(1024);
    }
    return {
        {"what", safe},
        {"key", "unhandled-exception"},
        {"debuginfo", safe},
        {"retryable", false},
        {"offline", false},
    };
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
            task->data()["undoPlacements"] = json::object();
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

        } else if (cname == "GetManyRFC2822Task") {
            // nothing — all work happens in performRemote

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

        } else if (cname == "SyncbackEventTask") {
            performLocalSyncbackEvent(task);

        } else if (cname == "DestroyEventTask") {
            performLocalDestroyEvent(task);

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

    } catch (SQLite::Exception & ex) {
        // Database errors are usually transient (another mailsync process holding a
        // lock, a busy disk) and say nothing about whether this task is runnable, so
        // keep the existing behavior: leave the task as-is and let it escape, rather
        // than discarding work the user asked for on a problem that will clear.
        logger->error("[{}] -- Database error, not marking the task complete: {}", task->id(), ex.what());
        logger->flush();
        throw;

    } catch (std::exception & ex) {
        logger->error("[{}] -- Failed with an unexpected exception ({}). Changing status to `complete`", task->id(), ex.what());
        logger->flush();
        task->setError(errorJSONForUnexpectedException(ex.what()));
        task->setStatus("complete");

    } catch (...) {
        logger->error("[{}] -- Failed with an unknown exception. Changing status to `complete`", task->id());
        logger->flush();
        task->setError(errorJSONForUnexpectedException("Unknown exception"));
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

            } else if (cname == "GetManyRFC2822Task") {
                performRemoteGetManyRFC2822(task);

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

            } else if (cname == "SyncbackEventTask") {
                performRemoteSyncbackEvent(task);

            } else if (cname == "DestroyEventTask") {
                performRemoteDestroyEvent(task);

            } else {
                logger->error("Unsure of how to process this task type {}", cname);
            }

            // A long-running task (e.g. GetManyRFC2822Task) can observe a
            // cancel request while it runs and stop early, setting should_cancel
            // on the task. Honor that here so it ends up "cancelled" rather than
            // "complete"; the pre-run check above only covers cancels that
            // arrive before the task starts.
            if (task->shouldCancel()) {
                logger->info("[{}] -- Cancelled. Changing status to `cancelled`", task->id());
                task->setStatus("cancelled");
            } else {
                logger->info("[{}] -- Succeeded. Changing status to `complete`", task->id());
                task->setStatus("complete");
            }
        }
    } catch (SyncException & ex) {
        logger->error("[{}] -- Failed ({}). Changing status to `complete`", task->id(), ex.toJSON().dump());
        logger->flush();
        task->setError(ex.toJSON());
        task->setStatus("complete");

    } catch (SQLite::Exception & ex) {
        // See the note in performLocal: a database error is not the task's fault, so
        // let it escape and leave the task queued for the next pass / next launch.
        logger->error("[{}] -- Database error, not marking the task complete: {}", task->id(), ex.what());
        logger->flush();
        throw;

    } catch (std::exception & ex) {
        logger->error("[{}] -- Failed with an unexpected exception ({}). Changing status to `complete`", task->id(), ex.what());
        logger->flush();
        task->setError(errorJSONForUnexpectedException(ex.what()));
        task->setStatus("complete");

    } catch (...) {
        logger->error("[{}] -- Failed with an unknown exception. Changing status to `complete`", task->id());
        logger->flush();
        task->setError(errorJSONForUnexpectedException("Unknown exception"));
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

/*
 Builds the engine's Message for draft JSON the client sent. The client serializes only
 the fields it knows about, so the local copy of the draft (when there is one) supplies
 the rest: engine bookkeeping, metadata and the "folders" snapshot. A "folders" echoed
 by the client may be stale and is discarded; a brand-new draft gets its Drafts placement
 from performLocalSaveDraft.
 */
Message TaskProcessor::inflateClientDraftJSON(json & draftJSON, shared_ptr<Message> existing = nullptr) {
    draftJSON.erase("folders");

    json base;
    if (existing) {
        base = existing->_data;
    } else {
        base = {
            {"draft", true},
            {"unread", false},
            {"starred", false},
            {"folders", json::object()},
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

    // Keys the client sent win; the base fills in the rest.
    draftJSON.insert(base.begin(), base.end());

    // Always update the timestamp
    draftJSON["date"] = time(0);
    
    auto msg = Message{draftJSON};
    
    if (msg.accountId() != account->id()) {
        throw SyncException("bad-accountid", "The draft in this task has the wrong account ID.", false);
    }

    return msg;
}

shared_ptr<Folder> TaskProcessor::draftsFolder() {
    auto folder = store->find<Folder>(Query().equal("accountId", account->id()).equal("role", "drafts"));
    if (folder == nullptr) {
        folder = store->find<Folder>(Query().equal("accountId", account->id()).equal("role", "all"));
    }
    if (folder == nullptr) {
        throw SyncException("no-drafts-folder", "Mailspring can't find your Drafts folder. To create and send mail, visit Preferences > Folders and choose a Drafts folder.", false);
    }
    return folder;
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

void TaskProcessor::performLocalChangeOnMessages(Task * task, LocalChangeFn modifyLocalMessage) {
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
        auto placements = store->placementsForMessage(msg->id());
        modifyLocalMessage(store, msg.get(), placements, data);

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

        for (auto chunk : chunks) {
            auto threads = store->findAllMap<Thread>(Query().equal("id", chunk), "id");

            for (auto pair : threads) {
                pair.second->resetCountedAttributes();
            }
            for (auto msg : models.messages) {
                if (threads.count(msg->threadId())) {
                    threads[msg->threadId()]->applyMessageAttributeChanges(MessageEmptySnapshot, msg.get(), store);
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

/*
 Runs the server side of a message task: every live copy the task addresses is grouped by
 the folder holding it and `applyInFolder` runs once per folder. The network I/O happens
 outside any transaction, so the messages are reloaded afterwards and the outcome applied
 to their rows then, together with releasing the syncedAt lock. The confirm save usually
 changes nothing the client can see (the optimistic marker already reported the
 destination), so its deltas are dropped unless a copy was restored or a duplicate removed.
 */
void TaskProcessor::performRemoteChangeOnMessages(Task * task, bool isMove, RemoteChangeFn applyInFolder) {
    json & data = task->data();
    vector<shared_ptr<Message>> messages = inflateMessages(data).messages;

    deque<TaskPlacement> items;
    for (auto msg : messages) {
        auto placements = store->placementsForMessage(msg->id());
        if (isMove) {
            for (auto & move : _movesForMessage(store, msg.get(), placements, data)) {
                items.push_back(TaskPlacement{msg, move.placement, move.destFolderId});
            }
        } else {
            for (auto & p : placements) {
                if (p.isLive() && p.remoteUID > 0) {
                    items.push_back(TaskPlacement{msg, p, ""});
                }
            }
        }
    }

    map<string, vector<TaskPlacement *>> itemsByFolder;
    map<string, vector<TaskPlacement *>> itemsByMessage;
    for (auto & item : items) {
        itemsByFolder[item.placement.folderId].push_back(&item);
        itemsByMessage[item.message->id()].push_back(&item);
    }
    for (auto & pair : itemsByFolder) {
        auto folder = store->folderById(account->id(), pair.first);
        if (folder == nullptr) {
            logger->warn("-- {} copies are in a folder ({}) that no longer exists, skipping", pair.second.size(), pair.first);
            continue;
        }
        applyInFolder(session, store, account->id(), *folder, pair.second, data);
    }
    if (isMove) {
        _restoreAdditionalCopies(session, store, account->id(), items, data);
    }
    
    {
        MailStoreTransaction transaction{store, "performRemoteChangeOnMessages"};
        bool clientVisibleChange = false;
        vector<shared_ptr<Message>> safeMessages = inflateMessages(data).messages;
        
        vector<string> displacedIds;
        for (auto safe : safeMessages) {
            json before = _clientVisibleState(*safe);
            auto rows = store->placementsForMessage(safe->id());
            for (auto item : itemsByMessage[safe->id()]) {
                string displaced = confirmPlacementChange(*safe, *item, rows);
                if (!displaced.empty()) {
                    displacedIds.push_back(displaced);
                }
            }
            if (_clientVisibleState(*safe) != before) {
                clientVisibleChange = true;
            }

            int suc = safe->syncUnsavedChanges() - 1;
            safe->setSyncUnsavedChanges(suc);
            if (suc == 0) {
                safe->setSyncedAt(time(0));
            }
            store->save(safe.get());
        }
        // A message that lost a stale (folder, UID) row to a moved copy has a snapshot
        // listing a copy it no longer has.
        for (auto & id : displacedIds) {
            auto displaced = store->find<Message>(Query().equal("id", id));
            if (displaced == nullptr) {
                continue;
            }
            logger->warn("-- Message {} lost a placement to a moved copy at the same UID", id);
            store->refreshMessageFromPlacements(*displaced);
            store->save(displaced.get());
            clientVisibleChange = true;
        }
        if (!clientVisibleChange) {
            store->unsafeEraseTransactionDeltas();
        }
        transaction.commit();
    }
}

// Applies one item's outcome to the reloaded message. A row that no longer matches its
// captured (folder, UID) - reset to UID 0 by a UIDVALIDITY change while the task ran -
// is left alone with its marker; the folder's next scan records where the copy is.
// A marker a later task's local phase put on the row (an undo issued before this move
// reached the server) must survive the commit, which clears it. Returns the id of another
// message displaced from the destination UID, or "" (MailStore::commitPlacementMove).
string TaskProcessor::confirmPlacementChange(Message & msg, TaskPlacement & item, const vector<Placement> & rows) {
    auto & p = item.placement;

    if (item.removed) {
        store->removePlacement(msg, p.folderId, p.remoteUID);
        return "";
    }
    if (!item.moved) {
        return "";
    }

    const Placement * current = nullptr;
    for (auto & r : rows) {
        if (r.folderId == p.folderId && r.remoteUID == p.remoteUID) {
            current = &r;
            break;
        }
    }
    if (current == nullptr) {
        logger->warn("-- Message {} no longer has a placement at ({}, {}); leaving its move to be re-derived", msg.id(), p.folderId, p.remoteUID);
        return "";
    }
    string laterPending = current->pendingFolderId;
    string displaced = store->commitPlacementMove(msg, p.folderId, p.remoteUID, item.destFolderId, item.movedUID);
    if (!laterPending.empty() && laterPending != item.destFolderId) {
        store->beginPlacementMove(msg, item.destFolderId, item.movedUID, laterPending);
    }

    for (auto & copy : item.copies) {
        auto folder = store->folderById(msg.accountId(), copy.first);
        if (folder == nullptr) {
            continue;
        }
        MessageAttributes attrs{copy.second, p.unread, p.starred, p.draft, _labelsOf(p)};
        store->upsertPlacement(msg, *folder, copy.second, attrs);
    }
    return displaced;
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
            ensureDraftPlacement(*existing);
            store->save(existing.get());
        } else {
            ensureDraftPlacement(draft);
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

// A draft with no live copy anywhere would be swept as an orphan at the end of the next
// sync pass. A brand-new draft, or one whose server copy vanished while the user kept
// editing it, is given a Drafts-folder placement at UID 0 (not on the server).
void TaskProcessor::ensureDraftPlacement(Message & draft) {
    for (auto & p : store->placementsForMessage(draft.id())) {
        if (p.isLive()) {
            return;
        }
    }
    auto folder = draftsFolder();
    MessageAttributes attrs{0, draft.isUnread(), draft.isStarred(), true, {}};
    store->upsertPlacement(draft, *folder, 0, attrs);
}

/*
 Destroys drafts locally right away and leaves an invisible placeholder in their place
 until the deletion reaches the server. The placeholder takes over each draft's server
 placement, so the Drafts scan keeps recognising the UID instead of re-inserting the
 draft, and reports it under Trash so the thread leaves the Drafts view. The draft's id
 is freed immediately because the user can switch a draft between accounts and re-create
 one with the same accountId + headerMessageId.
 */
void TaskProcessor::performLocalDestroyDraft(Task * task) {
    vector<string> messageIds = task->data()["messageIds"];

    logger->info("-- Hiding / detatching drafts while they're deleted...");

    auto trash = store->find<Folder>(Query().equal("accountId", account->id()).equal("role", "trash"));
    auto stubIds = json::array();
    
    {
        MailStoreTransaction transaction{store, "performLocalDestroyDraft"};

        auto drafts = store->findLargeSet<Message>("id", messageIds);
        for (auto & draft : drafts) {
            auto placements = store->placementsForMessage(draft->id());
            store->remove(draft.get());
            
            auto stub = Message::messageWithDeletionPlaceholderFor(draft);
            for (auto & p : placements) {
                if (!p.isLive() || p.remoteUID == 0) {
                    continue;
                }
                auto folder = store->folderById(account->id(), p.folderId);
                if (folder == nullptr) {
                    continue;
                }
                MessageAttributes attrs{p.remoteUID, p.unread, p.starred, p.draft, _labelsOf(p)};
                store->upsertPlacement(*stub, *folder, p.remoteUID, attrs);
                if (trash != nullptr) {
                    store->beginPlacementMove(*stub, p.folderId, p.remoteUID, trash->id());
                }
            }
            // The placement keeps the draft's flags so the Drafts scan sees no change;
            // the placeholder itself must not appear in the draft list.
            stub->setDraft(false);
            stub->setUnread(false);
            stub->setStarred(false);
            store->save(stub.get());
            stubIds.push_back(stub->id());

            logger->info("-- Replacing local ID {} with {}", draft->id(), stub->id());
        }

        transaction.commit();
    }
    task->data()["stubIds"] = stubIds;
}

// Placeholders with no placement (the draft was never on the server) are removed here
// too: the orphan sweep only runs at the end of a sync pass and a stub carries a
// syncedAt lock, so nothing else would clean them up promptly.
void TaskProcessor::performRemoteDestroyDraft(Task * task) {
    vector<string> stubIds = task->data()["stubIds"];
    auto stubs = store->findLargeSet<Message>("id", stubIds);

    for (auto & stub : stubs) {
        logger->info("-- Deleting remote draft {}", stub->id());
        _removeMessageCopiesResilient(session, store, account->id(), *stub);

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
        MailStoreTransaction transaction{store, "performLocalDestroyContact"};
        auto deleted = store->findLargeSet<Contact>("id", contactIds);
        for (auto & c : deleted) {
            c->setHidden(true);
            store->save(c.get());
        }
        transaction.commit();
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
    if (contact == nullptr) {
        throw SyncException("not-found", "Contact not found for syncback", false);
    }

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


void TaskProcessor::performLocalSyncbackEvent(Task * task) {
    json & eventJSON = task->data()["event"];
    string calendarId = task->data()["calendarId"].get<string>();

    // Check if event already exists
    string eventId = eventJSON.count("id") ? eventJSON["id"].get<string>() : "";
    shared_ptr<Event> existing = nullptr;

    if (eventId != "") {
        existing = store->find<Event>(Query().equal("id", eventId));
    }

    if (existing) {
        // UPDATE: Merge client changes into existing event
        if (eventJSON.count("ics")) {
            existing->setIcsData(eventJSON["ics"].get<string>());
            // Re-parse ICS to update recurrence fields
            ICalendar cal(existing->icsData());
            if (!cal.Events.empty()) {
                // Find the VEVENT matching our event's recurrenceId
                string eventRecurrenceId = existing->recurrenceId();
                ICalendarEvent* matchingEvent = nullptr;

                for (auto icsEvent : cal.Events) {
                    if (icsEvent->RecurrenceId == eventRecurrenceId) {
                        matchingEvent = icsEvent;
                        break;
                    }
                }

                // Fall back to first event if no match
                if (!matchingEvent) {
                    matchingEvent = cal.Events.front();
                }

                existing->_data["rs"] = matchingEvent->DtStart.toUnix();
                existing->_data["re"] = endOf(matchingEvent).toUnix();
                existing->_data["icsuid"] = matchingEvent->UID;
                existing->setRecurrenceId(matchingEvent->RecurrenceId);
                existing->setStatus(matchingEvent->Status.empty() ? "CONFIRMED" : matchingEvent->Status);
            }
        }
        store->save(existing.get());
        task->data()["event"]["id"] = existing->id();
    } else {
        // CREATE: Generate new event with temporary ID
        string tempId = MailUtils::idRandomlyGenerated();
        string icsData = eventJSON["ics"].get<string>();
        ICalendar cal(icsData);

        if (cal.Events.empty()) {
            throw SyncException("invalid-ics", "ICS data does not contain any events", false);
        }

        // For new event creation, use the first VEVENT (typically only one)
        // The Event constructor now handles recurrenceId from the ICalendarEvent
        auto icsEvent = cal.Events.front();
        Event event("", account->id(), calendarId, icsData, icsEvent);
        event._data["id"] = tempId;  // Temporary ID until server assigns etag
        store->save(&event);

        task->data()["event"]["id"] = tempId;
    }

    store->save(task);
}

void TaskProcessor::performRemoteSyncbackEvent(Task * task) {
    string eventId = task->data()["event"]["id"].get<string>();
    auto event = store->find<Event>(Query().equal("id", eventId).equal("accountId", account->id()));

    if (event == nullptr) {
        throw SyncException("not-found", "Event not found for syncback", false);
    }

    auto dav = make_shared<DAVWorker>(account);
    dav->writeAndResyncEvent(event);
}

void TaskProcessor::performLocalDestroyEvent(Task * task) {
    vector<string> eventIds {};
    for (json & e : task->data()["events"]) {
        eventIds.push_back(e["id"].get<string>());
    }

    // Mark events as hidden locally (they'll be fully removed after remote delete succeeds)
    // Note: Unlike contacts, we don't have a "hidden" field on events, so we just
    // leave them in place until performRemote completes.
}

void TaskProcessor::performRemoteDestroyEvent(Task * task) {
    vector<string> eventIds {};
    for (json & e : task->data()["events"]) {
        eventIds.push_back(e["id"].get<string>());
    }

    auto events = store->findLargeSet<Event>("id", eventIds);
    auto dav = make_shared<DAVWorker>(account);

    for (auto & event : events) {
        dav->deleteEvent(event);
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
        auto query = Query().equal("accountId", accountId).equal("id", MailUtils::idForFolder(accountId, existingPath));
        localModel = isGmail ? store->find<Label>(query) : store->find<Folder>(query);
    }

    if (!localModel) {
        string id = MailUtils::idForFolder(accountId, path);
        localModel = isGmail ? make_shared<Label>(id, accountId, 0) : make_shared<Folder>(id, accountId, 0);
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

    Array * to = Array::array();
    for (json & p : draft.to()) {
        to->addObject(MailUtils::addressFromContactJSON(p));
    }
    builder.header()->setTo(to);

    Array * cc = Array::array();
    for (json & p : draft.cc()) {
        cc->addObject(MailUtils::addressFromContactJSON(p));
    }
    builder.header()->setCc(cc);

    Array * bcc = Array::array();
    for (json & p : draft.bcc()) {
        bcc->addObject(MailUtils::addressFromContactJSON(p));
    }
    builder.header()->setBcc(bcc);

    Array * replyTo = Array::array();
    for (json & p : draft.replyTo()) {
        replyTo->addObject(MailUtils::addressFromContactJSON(p));
    }
    builder.header()->setReplyTo(replyTo);

    json & fromP = draft.from().at(0);
    builder.header()->setFrom(MailUtils::addressFromContactJSON(fromP));

    // Inject importance/priority headers for max client compatibility.
    // Front-end stores the canonical value under `hImportance` ("high" | "low" | "normal").
    if (draft._data.count("hImportance") && draft._data["hImportance"].is_string()) {
        string importance = draft._data["hImportance"].get<string>();
        if (importance == "high") {
            builder.header()->setExtraHeader(MCSTR("Importance"), MCSTR("high"));
            builder.header()->setExtraHeader(MCSTR("X-Priority"), MCSTR("1 (Highest)"));
            builder.header()->setExtraHeader(MCSTR("X-MSMail-Priority"), MCSTR("High"));
        } else if (importance == "low") {
            builder.header()->setExtraHeader(MCSTR("Importance"), MCSTR("low"));
            builder.header()->setExtraHeader(MCSTR("X-Priority"), MCSTR("5 (Lowest)"));
            builder.header()->setExtraHeader(MCSTR("X-MSMail-Priority"), MCSTR("Low"));
        }
    }

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
    _removeMessageCopiesResilient(session, store, account->id(), draft);

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
        IndexSet * uids = IndexSet::indexSet();
        
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
                    Array * xgmValues = Array::array();
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

     On Gmail the sent "folder" is a label and the copy's real home is All Mail, the one
     folder the sync worker SELECTs. The placement is recorded there when Gmail already
     shows the message in All Mail; if it lags, the copy is filed under the Sent label
     for now and the next All Mail scan replaces that transient placement (Gmail keeps
     one placement per message - see MailStore::removePlacementsOutsideFolder).
     */

    MailProcessor processor{account, store};
    shared_ptr<Message> localMessage = nullptr;
    IMAPMessage * remoteMessage = nullptr;
    shared_ptr<Folder> placementFolder = sent;
    String * placementPath = sentPath;
    uint32_t placementUID = sentFolderMessageUID;

    IMAPMessagesRequestKind kind = (IMAPMessagesRequestKind)(IMAPMessagesRequestKindHeaders | IMAPMessagesRequestKindFlags);
    if (session->storedCapabilities()->containsIndex(IMAPCapabilityGmail)) {
        kind = (IMAPMessagesRequestKind)(kind | IMAPMessagesRequestKindGmailLabels | IMAPMessagesRequestKindGmailThreadID | IMAPMessagesRequestKindGmailMessageID);

        auto all = store->find<Folder>(Query().equal("accountId", account->id()).equal("role", "all"));
        if (all != nullptr) {
            String * allPath = AS_MCSTR(all->path());
            IndexSet * allUIDs = IndexSet::indexSet();
            session->select(allPath, &err);
            if (err == ErrorNone) {
                session->findUIDsOfRecentHeaderMessageID(allPath, AS_MCSTR(draft.headerMessageId()), allUIDs);
            }
            err = ErrorNone;
            if (allUIDs->count() == 1) {
                placementFolder = all;
                placementPath = allPath;
                placementUID = (uint32_t)allUIDs->allRanges()[0].location;
                logger->info("-- Found the sent message in {} (UID {})", all->path(), placementUID);
            } else {
                logger->info("-- Sent message not yet visible in {} ({} matches), recording it under {} until the next scan", all->path(), allUIDs->count(), sent->path());
            }
        }
    }

    logger->info("-- Syncing sent message ({} UID {}) to the local mail store", placementFolder->path(), placementUID);
    
    // Important: Courier (and maybe other IMAP servers) won't show us new messages we've created
    // in the folder unless we re-select the folder. (I think they're treating UIDs like sequence
    // numbers?). We must re-select the sent folder to pull down the message we created.
    session->select(placementPath, &err);

    time_t syncDataTimestamp = time(0);
    IndexSet * uids = IndexSet::indexSetWithIndex(placementUID);
    Array * remote = session->fetchMessagesByUID(placementPath, kind, uids, nullptr, &err);

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
    localMessage = processor.insertFallbackToUpdateMessage(remoteMessage, *placementFolder, syncDataTimestamp);
    if (localMessage == nullptr) {
        logger->error("-X Error: processor.insert did not return a message.");
        return;
    }

    processor.retrievedMessageBody(localMessage.get(), messageParser);
    
    logger->info("-- Synced sent message ({} UID {} = Local ID {})", placementFolder->path(), placementUID, localMessage->id());
    
    // retrieve the new message and queue metadata tasks on it.
    // Metadata entries whose pluginId starts with "thread:" are promoted to the
    // thread (with the prefix stripped) rather than attached to the message.
    // This lets plugins declare thread-level intent at draft-write time.
    static const string THREAD_PREFIX = "thread:";
    for (const auto & m : draft.metadata()) {
        auto pluginId = m["pluginId"].get<string>();

        if (pluginId.substr(0, THREAD_PREFIX.size()) == THREAD_PREFIX) {
            string actualPluginId = pluginId.substr(THREAD_PREFIX.size());
            string threadId = localMessage->threadId();
            logger->info("-- Queueing task to attach {} draft metadata to thread {} (thread: prefix).", actualPluginId, threadId);
            Task mTask{"SyncbackMetadataTask", account->id(), {
                {"modelId", threadId},
                {"modelClassName", "thread"},
                {"pluginId", actualPluginId},
                {"value", m["value"]},
            }};
            performLocal(&mTask); // will call save
        } else {
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
    
    // Drop the folder's placements, then rewrite the affected messages: a message whose
    // only copy was here is removed (store->remove balances its thread and deletes the
    // body), one with copies elsewhere just loses this folder. This runs in performRemote
    // because it takes too long for performLocal, in chunks with a pause so the app can
    // keep up with the mass deletion.
    vector<string> affected;
    {
        MailStoreTransaction t{store, "performRemoteExpungeAllInFolder"};
        affected = store->deletePlacementsForFolder(id);
        t.commit();
    }
    for (auto chunk : MailUtils::chunksOfVector(affected, 100)) {
        int removed = 0;
        {
            MailStoreTransaction t{store, "performRemoteExpungeAllInFolder"};
            auto messages = store->findAll<Message>(Query().equal("id", chunk));
            for (auto & msg : messages) {
                store->refreshMessageFromPlacements(*msg);
                if (store->placementsForMessage(msg->id()).empty()) {
                    store->remove(msg.get());
                    removed++;
                } else {
                    store->save(msg.get());
                }
            }
            t.commit();
        }
        logger->info("-- Deleted {} local messages, {} kept copies elsewhere", removed, chunk.size() - removed);
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
    if (msg == nullptr) {
        throw SyncException("not-found", "Message not found for RFC2822 fetch", false);
    }

    // Any live copy will do; one outside Spam or Trash is less likely to be purged
    // by the server between our scan and this fetch.
    shared_ptr<Folder> folder = nullptr;
    uint32_t uid = 0;
    for (auto & p : store->placementsForMessage(msg->id())) {
        if (!p.isLive() || p.remoteUID == 0) {
            continue;
        }
        auto candidate = store->folderById(msg->accountId(), p.folderId);
        if (candidate == nullptr) {
            continue;
        }
        bool preferred = candidate->role() != "spam" && candidate->role() != "trash";
        if (folder == nullptr || preferred) {
            folder = candidate;
            uid = p.remoteUID;
            if (preferred) {
                break;
            }
        }
    }
    if (folder == nullptr) {
        throw SyncException(ErrorFetch, "performRemoteGetMessageRFC2822 - no copy on the server");
    }

    Data * data = session->fetchMessageByUID(AS_MCSTR(folder->path()), uid, &cb, &err);
    if (err != ErrorNone) {
        logger->error("Unable to fetch rfc2822 for message ({} UID {}). Error {}", folder->path(), uid, ErrorCodeToTypeMap[err]);
        throw SyncException(err, "performRemoteGetMessageRFC2822");
    }
    if (data == nullptr) {
        logger->error("fetchMessageByUID returned null data for message ({} UID {})", folder->path(), uid);
        throw SyncException(ErrorFetch, "performRemoteGetMessageRFC2822 - null data");
    }
#ifdef _MSC_VER
    wstring_convert<codecvt_utf8<wchar_t>, wchar_t> convert;
    data->writeToFile(AS_WIDE_MCSTR(convert.from_bytes(filepath)));
#else
    data->writeToFile(AS_MCSTR(filepath));
#endif
    setFileModificationTime(filepath, msg->date());
}

std::string TaskProcessor::sanitizeEmlFilename(const std::string & subject, time_t date, int index) {
    // Start with the subject, or "untitled" if empty
    std::string safe = subject.empty() ? "untitled" : subject;

    // Truncate to 80 characters (respecting multi-byte boundaries isn't critical
    // since illegal chars are replaced anyway, and truncation mid-char produces
    // a replacement underscore at worst)
    if (safe.size() > 80) {
        safe.resize(80);
    }

    // Replace filesystem-illegal characters and control characters with '_'
    for (size_t i = 0; i < safe.size(); i++) {
        unsigned char c = static_cast<unsigned char>(safe[i]);
        if (c <= 0x1f || c == 0x7f ||
            safe[i] == '/' || safe[i] == '?' || safe[i] == '<' || safe[i] == '>' ||
            safe[i] == '\\' || safe[i] == ':' || safe[i] == '*' || safe[i] == '|' || safe[i] == '"') {
            safe[i] = '_';
        }
    }

    // Strip trailing dots and spaces (Windows requirement)
    while (!safe.empty() && (safe.back() == '.' || safe.back() == ' ')) {
        safe.pop_back();
    }
    if (safe.empty()) {
        safe = "untitled";
    }

    // Format date as YYYY-MM-DD in UTC
    char dateBuf[16];
    struct tm utc;
#ifdef _MSC_VER
    gmtime_s(&utc, &date);
#else
    gmtime_r(&date, &utc);
#endif
    strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &utc);

    // Build filename: {index} - {subject} - {date}.eml
    std::ostringstream oss;
    oss << std::setw(5) << std::setfill('0') << index
        << " - " << safe << " - " << dateBuf << ".eml";

    std::string filename = oss.str();

#ifdef _MSC_VER
    // Clamp to 200 characters for Windows MAX_PATH safety
    if (filename.size() > 200) {
        // Rebuild with a truncated subject to fit
        size_t overhead = 5 + 3 + 3 + strlen(dateBuf) + 4; // index + " - " + " - " + date + ".eml"
        size_t maxSubject = 200 - overhead;
        if (safe.size() > maxSubject) {
            safe.resize(maxSubject);
            // Re-strip trailing dots/spaces after truncation
            while (!safe.empty() && (safe.back() == '.' || safe.back() == ' ')) {
                safe.pop_back();
            }
            if (safe.empty()) safe = "untitled";
        }
        oss.str("");
        oss.clear();
        oss << std::setw(5) << std::setfill('0') << index
            << " - " << safe << " - " << dateBuf << ".eml";
        filename = oss.str();
    }
#endif

    return filename;
}

void TaskProcessor::performRemoteGetManyRFC2822(Task * task) {
    const auto folderId = task->data()["folderId"].get<string>();
    const auto folderPath = task->data()["folderPath"].get<string>();
    const auto outputDir = task->data()["outputDir"].get<string>();

    // Get total count without loading all message objects into memory
    int total = 0;
    {
        // The same join as the page query below, so a placement whose Message row is
        // missing is not counted as an export that never happens.
        SQLite::Statement count(store->db(),
            "SELECT COUNT(*) FROM MessageFolder INNER JOIN Message ON Message.id = MessageFolder.messageId "
            "WHERE MessageFolder.accountId = ? AND MessageFolder.folderId = ? AND MessageFolder.remoteUID > 0 AND MessageFolder.unlinkedAt IS NULL");
        count.bind(1, task->accountId());
        count.bind(2, folderId);
        if (count.executeStep()) {
            total = count.getColumn(0).getInt();
        }
    }

    // Initialize or resume progress
    json progress;
    progress["total"] = total;
    progress["exported"] = 0;
    progress["failed"] = 0;
    progress["errors"] = json::array();

    // If resuming, carry forward previous progress
    uint32_t cursorUID = 0;
    int globalIndex = 0;
    if (task->data().count("progress")) {
        auto & prev = task->data()["progress"];
        if (prev.count("exported")) {
            progress["exported"] = prev["exported"];
            globalIndex = prev["exported"].get<int>();
        }
        if (prev.count("failed")) {
            progress["failed"] = prev["failed"];
        }
        if (prev.count("errors")) {
            progress["errors"] = prev["errors"];
        }
        if (prev.count("lastUID")) {
            cursorUID = prev["lastUID"].get<uint32_t>();
        }
    }

    int exported = progress["exported"].get<int>();
    int failed = progress["failed"].get<int>();
    const int chunkSize = 50;

    // Paginate through the folder's placements ordered by remoteUID ascending, using a
    // cursor to avoid skipping/duplicating messages if the folder changes during export.
    // This walks MessageFolderUIDIndex (accountId, folderId, remoteUID) and joins the
    // Message row only for the subject and date the filename needs.
    struct Export {
        uint32_t uid;
        string id;
        string subject;
        time_t date;
    };
    while (true) {
        AutoreleasePool pool;

        vector<Export> messages;
        {
            SQLite::Statement page(store->db(),
                "SELECT MessageFolder.remoteUID, Message.id, Message.subject, Message.date FROM MessageFolder "
                "INNER JOIN Message ON Message.id = MessageFolder.messageId "
                "WHERE MessageFolder.accountId = ? AND MessageFolder.folderId = ? AND MessageFolder.remoteUID > ? AND MessageFolder.unlinkedAt IS NULL "
                "ORDER BY MessageFolder.remoteUID ASC LIMIT ?");
            page.bind(1, task->accountId());
            page.bind(2, folderId);
            page.bind(3, (long long)cursorUID);
            page.bind(4, chunkSize);
            while (page.executeStep()) {
                messages.push_back({
                    (uint32_t)page.getColumn(0).getInt64(),
                    page.getColumn(1).getString(),
                    page.getColumn(2).isNull() ? "" : page.getColumn(2).getString(),
                    (time_t)page.getColumn(3).getDouble(),
                });
            }
        }

        if (messages.empty()) {
            break;
        }

        for (auto & msg : messages) {
            std::string filename = sanitizeEmlFilename(msg.subject, msg.date, globalIndex);
            std::string filepath = outputDir + FS_PATH_SEP + filename;

            IMAPProgress cb;
            ErrorCode err = ErrorNone;

            try {
                Data * data = session->fetchMessageByUID(
                    AS_MCSTR(folderPath), msg.uid, &cb, &err);

                if (err != ErrorNone) {
                    throw SyncException(err, "GetManyRFC2822 fetch");
                }
                if (data == nullptr) {
                    throw SyncException(ErrorFetch, "GetManyRFC2822 - null data");
                }

#ifdef _MSC_VER
                wstring_convert<codecvt_utf8<wchar_t>, wchar_t> convert;
                data->writeToFile(AS_WIDE_MCSTR(convert.from_bytes(filepath)));
#else
                data->writeToFile(AS_MCSTR(filepath));
#endif
                setFileModificationTime(filepath, msg.date);
                exported++;
            } catch (SyncException & ex) {
                logger->error("GetManyRFC2822: failed to export message {} (UID {}): {}",
                    msg.id, msg.uid, ex.toJSON().dump());
                failed++;
                json errEntry;
                errEntry["messageId"] = msg.id;
                errEntry["subject"] = msg.subject;
                errEntry["error"] = ex.toJSON()["error"];
                progress["errors"].push_back(errEntry);
            }

            cursorUID = msg.uid;
            globalIndex++;
        }

        // After each chunk, persist progress and check for a cancel request in
        // a single write transaction.
        //
        // Electron sets should_cancel on this same Task row from the main
        // thread (TaskProcessor::cancel), using a different MailStore
        // connection. Previously this code saved our in-memory task copy — in
        // which should_cancel is false — and only then re-read the row, so the
        // progress save clobbered a concurrently-set cancel flag and the
        // re-read almost never saw it: once the export had started it could not
        // be cancelled. Re-reading and saving inside one BEGIN IMMEDIATE
        // transaction serializes against that cancel write, so a request is
        // either already visible here or ordered strictly after our commit and
        // seen on the next chunk — never lost.
        progress["exported"] = exported;
        progress["failed"] = failed;
        progress["lastUID"] = cursorUID;

        bool cancelled = false;
        {
            MailStoreTransaction transaction{store, "GetManyRFC2822 progress"};
            auto refreshed = store->find<Task>(Query().equal("id", task->id()));
            if (refreshed != nullptr && refreshed->shouldCancel()) {
                cancelled = true;
                progress["cancelled"] = true;
                // Carry the flag onto our in-memory copy so this save preserves
                // it and performRemote marks the task cancelled, not complete.
                task->setShouldCancel();
            }
            task->data()["progress"] = progress;
            store->save(task);
            transaction.commit();
        }
        if (cancelled) {
            logger->info("GetManyRFC2822: cancelled after exporting {} of {} messages", exported, total);
            return;
        }

        // Sleep to let sync and other tasks breathe
        if ((int)messages.size() == chunkSize) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // Write final result
    json result;
    result["total"] = total;
    result["exported"] = exported;
    result["failed"] = failed;
    result["outputDir"] = outputDir;
    result["errors"] = progress["errors"];
    task->data()["result"] = result;
    store->save(task);

    logger->info("GetManyRFC2822: completed. Exported {} of {} messages ({} failed)",
        exported, total, failed);
}

void TaskProcessor::performRemoteSendRSVP(Task * task) {
    AutoreleasePool pool;
    ErrorCode err = ErrorNone;

    // Validate required fields exist
    if (!task->data().count("ics") || !task->data().count("subject") || !task->data().count("to")) {
        throw SyncException("missing-json", "Missing required fields: ics, subject, or to", false);
    }

    // Load task data
    string ics = task->data()["ics"].get<string>();
    string subject = task->data()["subject"].get<string>();
    string organizer = task->data()["to"].get<string>();
    string icsRSVPStatus = task->data().count("icsRSVPStatus")
        ? task->data()["icsRSVPStatus"].get<string>()
        : "ACCEPTED";

    // =========================================================================
    // RFC 5546/6047 Validation
    // =========================================================================

    // Validation 1: Check ICS contains METHOD:REPLY (RFC 5546 requirement)
    if (ics.find("METHOD:REPLY") == string::npos) {
        throw SyncException("invalid-ics", "ICS data must contain METHOD:REPLY for an RSVP response", false);
    }

    // Parse the ICS to validate and extract event information
    ICalendar cal(ics);

    if (cal.Events.empty()) {
        throw SyncException("invalid-ics", "ICS data does not contain any VEVENT components", false);
    }

    ICalendarEvent* event = cal.Events.front();

    // Validation 2: UID is required (RFC 5546 Section 3.2.3 - MUST match original REQUEST)
    if (event->UID.empty()) {
        throw SyncException("invalid-ics",
            "ICS REPLY must contain UID property matching the original invitation", false);
    }

    // Validation 3: DTSTAMP is required (RFC 5546 Section 3.2.3)
    if (event->DtStamp.IsEmpty()) {
        throw SyncException("invalid-ics",
            "ICS REPLY must contain DTSTAMP property", false);
    }

    // Validation 4: ORGANIZER is required (RFC 5546 Section 3.2.3)
    if (event->Organizer.empty()) {
        throw SyncException("invalid-ics",
            "ICS REPLY must contain ORGANIZER property", false);
    }

    // Validation 5: REPLY must contain exactly one ATTENDEE (RFC 5546 Section 3.2.3)
    if (event->Attendees.size() != 1) {
        throw SyncException("invalid-ics",
            "ICS REPLY must contain exactly one ATTENDEE (the replying user), found " + to_string(event->Attendees.size()), false);
    }

    // Validation 6: Check ATTENDEE has valid PARTSTAT (RFC 5545 Section 3.2.12)
    bool hasValidPartstat = (ics.find("PARTSTAT=ACCEPTED") != string::npos ||
                             ics.find("PARTSTAT=DECLINED") != string::npos ||
                             ics.find("PARTSTAT=TENTATIVE") != string::npos);
    if (!hasValidPartstat) {
        throw SyncException("invalid-ics",
            "ATTENDEE must have valid PARTSTAT parameter (ACCEPTED, DECLINED, or TENTATIVE)", false);
    }

    // Extract attendee email for From address validation
    // The ICalendar library returns attendee as "Name <email>" or just "email"
    string attendeeInfo = event->Attendees.front();
    string attendeeEmail;
    size_t emailStart = attendeeInfo.find('<');
    size_t emailEnd = attendeeInfo.find('>');
    if (emailStart != string::npos && emailEnd != string::npos && emailEnd > emailStart) {
        attendeeEmail = attendeeInfo.substr(emailStart + 1, emailEnd - emailStart - 1);
    } else {
        attendeeEmail = attendeeInfo;
    }

    // Validation 7: Verify From address matches ATTENDEE email (RFC 6047 requirement)
    // Mismatches may cause the RSVP to be rejected by the organizer's calendar
    string fromEmail = account->emailAddress();
    string lowerFromEmail = fromEmail;
    string lowerAttendeeEmail = attendeeEmail;
    transform(lowerFromEmail.begin(), lowerFromEmail.end(), lowerFromEmail.begin(), ::tolower);
    transform(lowerAttendeeEmail.begin(), lowerAttendeeEmail.end(), lowerAttendeeEmail.begin(), ::tolower);

    if (lowerFromEmail != lowerAttendeeEmail) {
        // Warn but don't fail - email aliases and forwarding may cause legitimate mismatches
        logger->warn("RSVP From address ({}) does not match ATTENDEE email in ICS ({}). "
                     "This may cause the RSVP to be rejected.", fromEmail, attendeeEmail);
    }

    // =========================================================================
    // Build RFC 6047-compliant iMIP message with multipart/alternative structure
    // =========================================================================

    // Extract event summary for human-readable message
    string eventSummary = event->Summary.empty() ? "Calendar Event" : event->Summary;

    // Generate human-readable text based on RSVP status (per RFC 6047 recommendation)
    string humanReadableText;
    if (icsRSVPStatus == "ACCEPTED") {
        humanReadableText = fromEmail + " has accepted the invitation to: " + eventSummary;
    } else if (icsRSVPStatus == "DECLINED") {
        humanReadableText = fromEmail + " has declined the invitation to: " + eventSummary;
    } else if (icsRSVPStatus == "TENTATIVE") {
        humanReadableText = fromEmail + " has tentatively accepted the invitation to: " + eventSummary;
    } else {
        humanReadableText = fromEmail + " has responded to the invitation: " + eventSummary;
    }

    // Generate a unique boundary for multipart message
    string boundary = "----=_Mailspring_RSVP_" + to_string(time(0)) + "_" + to_string(rand());

    // Base64 encode the ICS data (RFC 6047 recommends base64 for maximum compatibility)
    Data * icsData = AS_MCSTR(ics)->dataUsingEncoding("utf-8");
    String * icsBase64 = icsData->base64String();

    // Build MIME headers
    MessageBuilder builder;
    builder.header()->setSubject(AS_MCSTR(subject));
    builder.header()->setUserAgent(MCSTR("Mailspring"));
    builder.header()->setDate(time(0));

    Array * toArray = Array::array();
    toArray->addObject(Address::addressWithMailbox(AS_MCSTR(organizer)));
    builder.header()->setTo(toArray);

    Address * me = Address::addressWithMailbox(AS_MCSTR(fromEmail));
    builder.header()->setFrom(me);
    builder.header()->setReplyTo(Array::arrayWithObject(me));

    // Construct the multipart/alternative body per RFC 6047 Section 2.4
    // Structure: text/plain (human-readable) + text/calendar (machine-readable)
    stringstream mimeBody;
    mimeBody << "--" << boundary << "\r\n";
    mimeBody << "Content-Type: text/plain; charset=UTF-8\r\n";
    mimeBody << "Content-Transfer-Encoding: 7bit\r\n";
    mimeBody << "\r\n";
    mimeBody << humanReadableText << "\r\n";
    mimeBody << "\r\n";
    mimeBody << "--" << boundary << "\r\n";
    // Critical: Content-Type MUST include method=REPLY parameter (RFC 6047 Section 2.4)
    mimeBody << "Content-Type: text/calendar; method=REPLY; charset=UTF-8\r\n";
    mimeBody << "Content-Transfer-Encoding: base64\r\n";
    // Use inline disposition, not attachment (RFC 6047 Section 2.4)
    mimeBody << "Content-Disposition: inline; filename=\"invite.ics\"\r\n";
    mimeBody << "\r\n";
    mimeBody << icsBase64->UTF8Characters() << "\r\n";
    mimeBody << "--" << boundary << "--\r\n";

    // Build the complete message by getting headers and appending our body
    // We set a dummy body first, then replace it
    builder.setTextBody(MCSTR("placeholder"));
    Data * headerData = builder.data();
    string headerStr = string(headerData->bytes(), headerData->length());

    // Find where headers end (double CRLF) and extract just the headers
    size_t headerEnd = headerStr.find("\r\n\r\n");
    if (headerEnd == string::npos) {
        headerEnd = headerStr.find("\n\n");
    }

    // Reconstruct message with correct Content-Type and our multipart body
    stringstream fullMessage;

    // Write original headers but replace Content-Type
    string headers = headerStr.substr(0, headerEnd);

    // Remove the original Content-Type and Content-Transfer-Encoding headers
    string newHeaders;
    istringstream headerStream(headers);
    string line;
    while (getline(headerStream, line)) {
        // Remove \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // Skip Content-Type and Content-Transfer-Encoding headers (we'll add our own)
        string lowerLine = line;
        transform(lowerLine.begin(), lowerLine.end(), lowerLine.begin(), ::tolower);
        if (lowerLine.find("content-type:") == 0 || lowerLine.find("content-transfer-encoding:") == 0) {
            continue;
        }
        newHeaders += line + "\r\n";
    }

    fullMessage << newHeaders;
    fullMessage << "Content-Type: multipart/alternative; boundary=\"" << boundary << "\"\r\n";
    fullMessage << "MIME-Version: 1.0\r\n";
    fullMessage << "\r\n";
    fullMessage << mimeBody.str();

    string messageStr = fullMessage.str();
    Data * messageData = Data::dataWithBytes(messageStr.c_str(), (unsigned int)messageStr.length());

    // =========================================================================
    // Send the RSVP via SMTP
    // =========================================================================

    SMTPSession smtp;
    SMTPProgress sprogress;
    MailUtils::configureSessionForAccount(smtp, account);

    logger->info("-- Sending RFC 6047-compliant RSVP ({}) to organizer {}", icsRSVPStatus, organizer);
    smtp.sendMessage(messageData, &sprogress, &err);

    if (err != ErrorNone) {
        int e = smtp.lastLibetpanError();
        string es = LibEtPanCodeToTypeMap.count(e) ? LibEtPanCodeToTypeMap[e] : to_string(e);
        logger->info("-X An SMTP error occurred: {} LibEtPan code: {}", ErrorCodeToTypeMap[err], es);
        throw SyncException("send-failed", ErrorCodeToTypeMap[err], false);
    }

    logger->info("-- RSVP sent successfully");
}
