#include "ChatMessage.h"
#include "../Database.h"
#include <sstream>

ChatMessage::ChatMessage() : 
    version(1),
    likeCount(0),
    unlikeCount(0) {
}

json ChatMessage::toJSON() const {
    json j;
    j["id"] = id;
    j["accountId"] = accountId;
    j["version"] = version;
    j["messageType"] = messageType;
    j["content"] = content;
    j["aiProvider"] = aiProvider;
    j["likeCount"] = likeCount;
    j["unlikeCount"] = unlikeCount;
    j["createdAt"] = createdAt;
    j["updatedAt"] = updatedAt;
    return j;
}

void ChatMessage::fromJSON(const json& j) {
    id = j.value("id", "");
    accountId = j.value("accountId", "");
    version = j.value("version", 1);
    messageType = j.value("messageType", "");
    content = j.value("content", "");
    aiProvider = j.value("aiProvider", "");
    likeCount = j.value("likeCount", 0);
    unlikeCount = j.value("unlikeCount", 0);
    createdAt = j.value("createdAt", "");
    updatedAt = j.value("updatedAt", "");
}

bool ChatMessage::isValid() const {
    return !id.empty() && 
           !accountId.empty() && 
           !messageType.empty() && 
           !content.empty() && 
           !aiProvider.empty();
}

std::shared_ptr<ChatMessage> ChatMessage::find(const std::string& id) {
    auto db = Database::instance();
    auto stmt = db->prepare("SELECT * FROM " + tableName() + " WHERE id = ?");
    stmt->bind(1, id);
    
    if (stmt->step() == SQLITE_ROW) {
        auto message = std::make_shared<ChatMessage>();
        message->id = stmt->getText(0);
        message->accountId = stmt->getText(1);
        message->version = stmt->getInt(2);
        message->messageType = stmt->getText(3);
        message->content = stmt->getText(4);
        message->aiProvider = stmt->getText(5);
        message->likeCount = stmt->getInt(6);
        message->unlikeCount = stmt->getInt(7);
        message->createdAt = stmt->getText(8);
        message->updatedAt = stmt->getText(9);
        return message;
    }
    return nullptr;
}

std::vector<std::shared_ptr<ChatMessage>> ChatMessage::findAll(const std::string& accountId) {
    std::vector<std::shared_ptr<ChatMessage>> messages;
    auto db = Database::instance();
    auto stmt = db->prepare("SELECT * FROM " + tableName() + " WHERE accountId = ? ORDER BY createdAt DESC");
    stmt->bind(1, accountId);
    
    while (stmt->step() == SQLITE_ROW) {
        auto message = std::make_shared<ChatMessage>();
        message->id = stmt->getText(0);
        message->accountId = stmt->getText(1);
        message->version = stmt->getInt(2);
        message->messageType = stmt->getText(3);
        message->content = stmt->getText(4);
        message->aiProvider = stmt->getText(5);
        message->likeCount = stmt->getInt(6);
        message->unlikeCount = stmt->getInt(7);
        message->createdAt = stmt->getText(8);
        message->updatedAt = stmt->getText(9);
        messages.push_back(message);
    }
    return messages;
}

bool ChatMessage::save() {
    if (!isValid()) return false;
    
    auto db = Database::instance();
    auto stmt = db->prepare(
        "INSERT OR REPLACE INTO " + tableName() + 
        " (id, accountId, version, messageType, content, aiProvider, likeCount, unlikeCount, createdAt, updatedAt) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );
    
    stmt->bind(1, id);
    stmt->bind(2, accountId);
    stmt->bind(3, version);
    stmt->bind(4, messageType);
    stmt->bind(5, content);
    stmt->bind(6, aiProvider);
    stmt->bind(7, likeCount);
    stmt->bind(8, unlikeCount);
    stmt->bind(9, createdAt);
    stmt->bind(10, updatedAt);
    
    return stmt->step() == SQLITE_DONE;
}

bool ChatMessage::remove() {
    if (id.empty()) return false;
    
    auto db = Database::instance();
    auto stmt = db->prepare("DELETE FROM " + tableName() + " WHERE id = ?");
    stmt->bind(1, id);
    
    return stmt->step() == SQLITE_DONE;
} 