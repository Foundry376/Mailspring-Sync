#include "ThreadChatMessage.h"
#include "../Database.h"

ThreadChatMessage::ThreadChatMessage() : ChatMessage() {
}

json ThreadChatMessage::toJSON() const {
    json j = ChatMessage::toJSON();
    j["threadId"] = threadId;
    j["conversationId"] = conversationId;
    return j;
}

void ThreadChatMessage::fromJSON(const json& j) {
    ChatMessage::fromJSON(j);
    threadId = j.value("threadId", "");
    conversationId = j.value("conversationId", "");
}

bool ThreadChatMessage::isValid() const {
    return ChatMessage::isValid() && !threadId.empty();
}

std::shared_ptr<ThreadChatMessage> ThreadChatMessage::find(const std::string& id) {
    auto db = Database::instance();
    auto stmt = db->prepare("SELECT * FROM ThreadChatMessage WHERE id = ?");
    stmt->bind(1, id);
    
    if (stmt->step() == SQLITE_ROW) {
        auto message = std::make_shared<ThreadChatMessage>();
        message->id = stmt->getText(0);
        message->threadId = stmt->getText(1);
        message->accountId = stmt->getText(2);
        message->version = stmt->getInt(3);
        message->messageType = stmt->getText(4);
        message->content = stmt->getText(5);
        message->aiProvider = stmt->getText(6);
        message->conversationId = stmt->getText(7);
        message->likeCount = stmt->getInt(8);
        message->unlikeCount = stmt->getInt(9);
        message->createdAt = stmt->getText(10);
        message->updatedAt = stmt->getText(11);
        return message;
    }
    return nullptr;
}

std::vector<std::shared_ptr<ThreadChatMessage>> ThreadChatMessage::findByThreadId(const std::string& threadId) {
    std::vector<std::shared_ptr<ThreadChatMessage>> messages;
    auto db = Database::instance();
    auto stmt = db->prepare("SELECT * FROM ThreadChatMessage WHERE threadId = ? ORDER BY createdAt DESC");
    stmt->bind(1, threadId);
    
    while (stmt->step() == SQLITE_ROW) {
        auto message = std::make_shared<ThreadChatMessage>();
        message->id = stmt->getText(0);
        message->threadId = stmt->getText(1);
        message->accountId = stmt->getText(2);
        message->version = stmt->getInt(3);
        message->messageType = stmt->getText(4);
        message->content = stmt->getText(5);
        message->aiProvider = stmt->getText(6);
        message->conversationId = stmt->getText(7);
        message->likeCount = stmt->getInt(8);
        message->unlikeCount = stmt->getInt(9);
        message->createdAt = stmt->getText(10);
        message->updatedAt = stmt->getText(11);
        messages.push_back(message);
    }
    return messages;
}

std::vector<std::shared_ptr<ThreadChatMessage>> ThreadChatMessage::findByConversationId(const std::string& conversationId) {
    std::vector<std::shared_ptr<ThreadChatMessage>> messages;
    auto db = Database::instance();
    auto stmt = db->prepare("SELECT * FROM ThreadChatMessage WHERE conversationId = ? ORDER BY createdAt DESC");
    stmt->bind(1, conversationId);
    
    while (stmt->step() == SQLITE_ROW) {
        auto message = std::make_shared<ThreadChatMessage>();
        message->id = stmt->getText(0);
        message->threadId = stmt->getText(1);
        message->accountId = stmt->getText(2);
        message->version = stmt->getInt(3);
        message->messageType = stmt->getText(4);
        message->content = stmt->getText(5);
        message->aiProvider = stmt->getText(6);
        message->conversationId = stmt->getText(7);
        message->likeCount = stmt->getInt(8);
        message->unlikeCount = stmt->getInt(9);
        message->createdAt = stmt->getText(10);
        message->updatedAt = stmt->getText(11);
        messages.push_back(message);
    }
    return messages;
} 