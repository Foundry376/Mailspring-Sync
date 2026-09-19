//
//  Placement.hpp
//  MailSync
//
//  Copyright © 2026 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef Placement_hpp
#define Placement_hpp

#include <string>
#include <vector>
#include <SQLiteCpp/SQLiteCpp.h>
#include "json.hpp"

using namespace std;
using namespace nlohmann;

// Per-copy flag bits stored in Message JSON under "folders": { "<folderId>": bits }.
// The client and the thread refcount diff read these; they never carry UIDs.
static const int PLACEMENT_FLAG_UNREAD  = 1;
static const int PLACEMENT_FLAG_STARRED = 2;
static const int PLACEMENT_FLAG_DRAFT   = 4;

// UIDs above this were "unlink sentinels" (UINT32_MAX - phase) in the single-folder
// representation. The migration and the Phase 1 mirror turn them into tombstones.
// TEMPORARY(placements): removed in Phase 3
static const uint32_t LEGACY_UNLINK_SENTINEL_MIN = 4294967290u;

/*
 One physical copy of a message on the server: a row of the MessageFolder table.
 A Placement is a plain value, not a MailModel - it has no __cls, is never streamed
 to the client, and is only ever written through the MailStore placement helpers so
 that the Message's "folders" snapshot and derived flags stay consistent with it.
 */
struct Placement {
    long long rowid = 0;
    string accountId;
    string messageId;
    string folderId;
    uint32_t remoteUID = 0;      // 0 = not on the server (local draft, UIDVALIDITY reset)
    bool unread = false;
    bool starred = false;
    bool draft = false;
    json labels = json::array(); // X-GM-LABELS of this copy (Gmail only)
    time_t syncedAt = 0;
    time_t unlinkedAt = 0;       // 0 = live; otherwise the tombstone timestamp
    string pendingFolderId;      // set while an optimistic move is in flight

    static Placement fromRow(SQLite::Statement & row);

    bool isLive() const;
    int flagBits() const;

    // The folder the client should see this copy in: the optimistic destination while
    // a move is in flight, the server folder otherwise.
    string reportedFolderId() const;
};

#endif /* Placement_hpp */
