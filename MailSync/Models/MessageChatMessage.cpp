#include "MessageChatMessage.h"
#include "../Database.h"

MessageChatMessage::MessageChatMessage() : ChatMessage() {
}

json MessageChatMessage::toJSON() const {
    json j = ChatMessage::toJSON();
    j["messageId"] = messageId;
    j["contextData"] = contextData;
    return j;
}

void MessageChatMessage::fromJSON(const json& j) {
    ChatMessage::fromJSON(j);
    messageId = j.value("messageId", "");
    if (j.contains("contextData")) {
        contextData = j["contextData"];
    }
}

bool MessageChatMessage::isValid() const {
    return ChatMessage::isValid() && !messageId.empty();
}

std::shared_ptr<MessageChatMessage> MessageChatMessage::find(const std::string& id) {
    auto db = Database::instance();
    auto stmt = db->prepare("SELECT * FROM MessageChatMessage WHERE id = ?");
    stmt->bind(1, id);
    
    if (stmt->step() == SQLITE_ROW) {
        auto message = std::make_shared<MessageChatMessage>();
        message->id = stmt->getText(0);
        message->messageId = stmt->getText(1);
        message->accountId = stmt->getText(2);
        message->version = stmt->getInt(3);
        message->messageType = stmt->getText(4);
        message->content = stmt->getText(5);
        message->aiProvider = stmt->getText(6);
        message->contextData = json::parse(stmt->getText(7));
        message->likeCount = stmt->getInt(8);
        message->unlikeCount = stmt->getInt(9);
        message->createdAt = stmt->getText(10);
        message->updatedAt = stmt->getText(11);
        return message;
    }
    return nullptr;
}

std::vector<std::shared_ptr<MessageChatMessage>> MessageChatMessage::findByMessageId(const std::string& messageId) {
    std::vector<std::shared_ptr<MessageChatMessage>> messages;
    auto db = Database::instance();
    auto stmt = db->prepare("SELECT * FROM MessageChatMessage WHERE messageId = ? ORDER BY createdAt DESC");
    stmt->bind(1, messageId);
    
    while (stmt->step() == SQLITE_ROW) {
        auto message = std::make_shared<MessageChatMessage>();
        message->id = stmt->getText(0);
        message->messageId = stmt->getText(1);
        message->accountId = stmt->getText(2);
        message->version = stmt->getInt(3);
        message->messageType = stmt->getText(4);
        message->content = stmt->getText(5);
        message->aiProvider = stmt->getText(6);
        message->contextData = json::parse(stmt->getText(7));
        message->likeCount = stmt->getInt(8);
        message->unlikeCount = stmt->getInt(9);
        message->createdAt = stmt->getText(10);
        message->updatedAt = stmt->getText(11);
        messages.push_back(message);
    }
    return messages;
} 