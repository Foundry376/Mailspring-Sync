//
//  Placement.cpp
//  MailSync
//
//  Copyright © 2026 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "Placement.hpp"

Placement Placement::fromRow(SQLite::Statement & row) {
    Placement p;
    p.rowid = row.getColumn("rowid").getInt64();
    p.accountId = row.getColumn("accountId").getString();
    p.messageId = row.getColumn("messageId").getString();
    p.folderId = row.getColumn("folderId").getString();
    p.remoteUID = (uint32_t)row.getColumn("remoteUID").getInt64();
    p.unread = row.getColumn("unread").getInt() != 0;
    p.starred = row.getColumn("starred").getInt() != 0;
    p.draft = row.getColumn("draft").getInt() != 0;
    p.syncedAt = (time_t)row.getColumn("syncedAt").getInt64();

    auto unlinked = row.getColumn("unlinkedAt");
    p.unlinkedAt = unlinked.isNull() ? 0 : (time_t)unlinked.getInt64();

    auto pending = row.getColumn("pendingFolderId");
    p.pendingFolderId = pending.isNull() ? "" : pending.getString();

    p.labels = json::array();
    auto labelsText = row.getColumn("remoteXGMLabels").getString();
    if (!labelsText.empty()) {
        json parsed = json::parse(labelsText, nullptr, false);
        if (parsed.is_array()) {
            p.labels = parsed;
        }
    }
    return p;
}

bool Placement::isLive() const {
    return unlinkedAt == 0;
}

int Placement::flagBits() const {
    return (unread ? PLACEMENT_FLAG_UNREAD : 0)
         | (starred ? PLACEMENT_FLAG_STARRED : 0)
         | (draft ? PLACEMENT_FLAG_DRAFT : 0);
}

string Placement::reportedFolderId() const {
    return pendingFolderId.empty() ? folderId : pendingFolderId;
}
