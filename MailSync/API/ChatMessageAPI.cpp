#include "ChatMessageAPI.h"
#include "../Database.h"
#include <sstream>
#include <ctime>
#include <random>
#include <iomanip>

std::shared_ptr<ChatMessage> ChatMessageAPI::createChatMessage(
    const std::string& type,
    const std::string& parentId,
    const std::string& content,
    const std::string& aiProvider
) {
    std::shared_ptr<ChatMessage> message;
    
    if (type == "thread") {
        auto threadMessage = std::make_shared<ThreadChatMessage>();
        threadMessage->threadId = parentId;
        threadMessage->conversationId = generateUUID();
        message = threadMessage;
    } else {
        auto msgMessage = std::make_shared<MessageChatMessage>();
        msgMessage->messageId = parentId;
        message = msgMessage;
    }
    
    message->id = generateUUID();
    message->messageType = type;
    message->content = content;
    message->aiProvider = aiProvider;
    message->version = 1;
    message->createdAt = getCurrentTimestamp();
    message->updatedAt = message->createdAt;
    
    if (saveChatMessage(message)) {
        return message;
    }
    return nullptr;
}

std::vector<std::shared_ptr<ChatMessage>> ChatMessageAPI::getChatHistory(
    const std::string& type,
    const std::string& parentId
) {
    if (type == "thread") {
        return ThreadChatMessage::findByThreadId(parentId);
    } else {
        return MessageChatMessage::findByMessageId(parentId);
    }
}

bool ChatMessageAPI::toggleReaction(
    const std::string& chatMessageId,
    const std::string& reactionType
) {
    auto message = loadChatMessage(chatMessageId);
    if (!message) return false;

    auto reaction = ChatMessageReaction::findByChatMessageIdAndAccountId(chatMessageId, message->accountId);
    if (reaction) {
        // 如果已经存在反应，则删除
        if (!deleteReaction(chatMessageId, message->accountId)) {
            return false;
        }
        
        // 更新消息计数
        if (reaction->reactionType == "like") {
            message->likeCount--;
        } else {
            message->unlikeCount--;
        }
    } else {
        // 创建新的反应
        reaction = std::make_shared<ChatMessageReaction>();
        reaction->id = generateUUID();
        reaction->chatMessageId = chatMessageId;
        reaction->chatMessageType = message->messageType;
        reaction->accountId = message->accountId;
        reaction->reactionType = reactionType;
        reaction->createdAt = getCurrentTimestamp();
        
        if (!saveReaction(reaction)) {
            return false;
        }
        
        // 更新消息计数
        if (reactionType == "like") {
            message->likeCount++;
        } else {
            message->unlikeCount++;
        }
    }
    
    return updateChatMessage(message);
}

bool ChatMessageAPI::deleteChatMessage(const std::string& chatMessageId) {
    return deleteChatMessageFromDB(chatMessageId);
}

json ChatMessageAPI::getReactionStats(const std::string& chatMessageId) {
    auto reactions = getReactions(chatMessageId);
    json stats;
    stats["likeCount"] = 0;
    stats["unlikeCount"] = 0;
    
    for (const auto& reaction : reactions) {
        if (reaction->reactionType == "like") {
            stats["likeCount"] = stats["likeCount"].get<int>() + 1;
        } else {
            stats["unlikeCount"] = stats["unlikeCount"].get<int>() + 1;
        }
    }
    
    return stats;
}

bool ChatMessageAPI::updateChatMessage(
    const std::string& chatMessageId,
    const std::string& content
) {
    auto message = loadChatMessage(chatMessageId);
    if (!message) return false;

    message->content = content;
    message->version++;
    message->updatedAt = getCurrentTimestamp();
    
    return updateChatMessage(message);
}

std::shared_ptr<ChatMessage> ChatMessageAPI::getChatMessage(const std::string& chatMessageId) {
    return loadChatMessage(chatMessageId);
}

std::vector<std::shared_ptr<ChatMessage>> ChatMessageAPI::getConversationHistory(
    const std::string& conversationId
) {
    // 首先尝试从 ThreadChatMessage 获取
    auto threadMessages = ThreadChatMessage::findByConversationId(conversationId);
    if (!threadMessages.empty()) {
        return std::vector<std::shared_ptr<ChatMessage>>(threadMessages.begin(), threadMessages.end());
    }
    
    // 如果没有找到，返回空列表
    return std::vector<std::shared_ptr<ChatMessage>>();
}

// 私有辅助方法
bool ChatMessageAPI::saveChatMessage(const std::shared_ptr<ChatMessage>& message) {
    return message->save();
}

bool ChatMessageAPI::updateChatMessage(const std::shared_ptr<ChatMessage>& message) {
    message->updatedAt = getCurrentTimestamp();
    return message->save();
}

bool ChatMessageAPI::deleteChatMessageFromDB(const std::string& id) {
    auto message = loadChatMessage(id);
    if (!message) return false;
    return message->remove();
}

std::shared_ptr<ChatMessage> ChatMessageAPI::loadChatMessage(const std::string& id) {
    // 尝试从 ThreadChatMessage 加载
    auto threadMessage = ThreadChatMessage::find(id);
    if (threadMessage) return threadMessage;
    
    // 尝试从 MessageChatMessage 加载
    auto messageMessage = MessageChatMessage::find(id);
    if (messageMessage) return messageMessage;
    
    return nullptr;
}

bool ChatMessageAPI::saveReaction(const std::shared_ptr<ChatMessageReaction>& reaction) {
    return reaction->save();
}

bool ChatMessageAPI::deleteReaction(const std::string& chatMessageId, const std::string& accountId) {
    auto reaction = ChatMessageReaction::findByChatMessageIdAndAccountId(chatMessageId, accountId);
    if (!reaction) return false;
    return reaction->remove();
}

std::vector<std::shared_ptr<ChatMessageReaction>> ChatMessageAPI::getReactions(const std::string& chatMessageId) {
    return ChatMessageReaction::findByChatMessageId(chatMessageId);
}

// 工具函数
std::string ChatMessageAPI::generateUUID() {
    // 实现 UUID 生成逻辑
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static const char* hex = "0123456789abcdef";
    
    std::string uuid;
    uuid.reserve(36);
    
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            uuid += '-';
        } else {
            uuid += hex[dis(gen)];
        }
    }
    
    return uuid;
}

std::string ChatMessageAPI::getCurrentTimestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
} 