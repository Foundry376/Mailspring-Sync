#include "ChatMessageReaction.h"
#include "../Database.h"

ChatMessageReaction::ChatMessageReaction() {
}

json ChatMessageReaction::toJSON() const {
    json j;
    j["id"] = id;
    j["chatMessageId"] = chatMessageId;
    j["chatMessageType"] = chatMessageType;
    j["accountId"] = accountId;
    j["reactionType"] = reactionType;
    j["createdAt"] = createdAt;
    return j;
}

void ChatMessageReaction::fromJSON(const json& j) {
    id = j.value("id", "");
    chatMessageId = j.value("chatMessageId", "");
    chatMessageType = j.value("chatMessageType", "");
    accountId = j.value("accountId", "");
    reactionType = j.value("reactionType", "");
    createdAt = j.value("createdAt", "");
}

bool ChatMessageReaction::isValid() const {
    return !id.empty() && 
           !chatMessageId.empty() && 
           !chatMessageType.empty() && 
           !accountId.empty() && 
           !reactionType.empty();
}

std::shared_ptr<ChatMessageReaction> ChatMessageReaction::find(const std::string& id) {
    auto db = Database::instance();
    auto stmt = db->prepare("SELECT * FROM ChatMessageReaction WHERE id = ?");
    stmt->bind(1, id);
    
    if (stmt->step() == SQLITE_ROW) {
        auto reaction = std::make_shared<ChatMessageReaction>();
        reaction->id = stmt->getText(0);
        reaction->chatMessageId = stmt->getText(1);
        reaction->chatMessageType = stmt->getText(2);
        reaction->accountId = stmt->getText(3);
        reaction->reactionType = stmt->getText(4);
        reaction->createdAt = stmt->getText(5);
        return reaction;
    }
    return nullptr;
}

std::vector<std::shared_ptr<ChatMessageReaction>> ChatMessageReaction::findByChatMessageId(const std::string& chatMessageId) {
    std::vector<std::shared_ptr<ChatMessageReaction>> reactions;
    auto db = Database::instance();
    auto stmt = db->prepare("SELECT * FROM ChatMessageReaction WHERE chatMessageId = ? ORDER BY createdAt DESC");
    stmt->bind(1, chatMessageId);
    
    while (stmt->step() == SQLITE_ROW) {
        auto reaction = std::make_shared<ChatMessageReaction>();
        reaction->id = stmt->getText(0);
        reaction->chatMessageId = stmt->getText(1);
        reaction->chatMessageType = stmt->getText(2);
        reaction->accountId = stmt->getText(3);
        reaction->reactionType = stmt->getText(4);
        reaction->createdAt = stmt->getText(5);
        reactions.push_back(reaction);
    }
    return reactions;
}

std::shared_ptr<ChatMessageReaction> ChatMessageReaction::findByChatMessageIdAndAccountId(
    const std::string& chatMessageId,
    const std::string& accountId
) {
    auto db = Database::instance();
    auto stmt = db->prepare("SELECT * FROM ChatMessageReaction WHERE chatMessageId = ? AND accountId = ?");
    stmt->bind(1, chatMessageId);
    stmt->bind(2, accountId);
    
    if (stmt->step() == SQLITE_ROW) {
        auto reaction = std::make_shared<ChatMessageReaction>();
        reaction->id = stmt->getText(0);
        reaction->chatMessageId = stmt->getText(1);
        reaction->chatMessageType = stmt->getText(2);
        reaction->accountId = stmt->getText(3);
        reaction->reactionType = stmt->getText(4);
        reaction->createdAt = stmt->getText(5);
        return reaction;
    }
    return nullptr;
}

bool ChatMessageReaction::save() {
    if (!isValid()) return false;
    
    auto db = Database::instance();
    auto stmt = db->prepare(
        "INSERT OR REPLACE INTO ChatMessageReaction "
        "(id, chatMessageId, chatMessageType, accountId, reactionType, createdAt) "
        "VALUES (?, ?, ?, ?, ?, ?)"
    );
    
    stmt->bind(1, id);
    stmt->bind(2, chatMessageId);
    stmt->bind(3, chatMessageType);
    stmt->bind(4, accountId);
    stmt->bind(5, reactionType);
    stmt->bind(6, createdAt);
    
    return stmt->step() == SQLITE_DONE;
}

bool ChatMessageReaction::remove() {
    if (id.empty()) return false;
    
    auto db = Database::instance();
    auto stmt = db->prepare("DELETE FROM ChatMessageReaction WHERE id = ?");
    stmt->bind(1, id);
    
    return stmt->step() == SQLITE_DONE;
} 