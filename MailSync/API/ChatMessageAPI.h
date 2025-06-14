#pragma once

#include <string>
#include <memory>
#include <vector>
#include <random>
#include <iomanip>
#include "json.hpp"
#include "../Models/ChatMessage.h"
#include "../Models/ThreadChatMessage.h"
#include "../Models/MessageChatMessage.h"
#include "../Models/ChatMessageReaction.h"

using json = nlohmann::json;

class ChatMessageAPI {
public:
    // 创建聊天消息
    static std::shared_ptr<ChatMessage> createChatMessage(
        const std::string& type,
        const std::string& parentId,
        const std::string& content,
        const std::string& aiProvider
    );

    // 获取聊天历史
    static std::vector<std::shared_ptr<ChatMessage>> getChatHistory(
        const std::string& type,
        const std::string& parentId
    );

    // 点赞/点踩操作
    static bool toggleReaction(
        const std::string& chatMessageId,
        const std::string& reactionType
    );

    // 删除聊天消息
    static bool deleteChatMessage(const std::string& chatMessageId);

    // 获取反应统计
    static json getReactionStats(const std::string& chatMessageId);

    // 更新聊天消息
    static bool updateChatMessage(
        const std::string& chatMessageId,
        const std::string& content
    );

    // 获取单个聊天消息
    static std::shared_ptr<ChatMessage> getChatMessage(const std::string& chatMessageId);

    // 获取会话历史
    static std::vector<std::shared_ptr<ChatMessage>> getConversationHistory(
        const std::string& conversationId
    );

private:
    // 数据库操作
    static bool saveChatMessage(const std::shared_ptr<ChatMessage>& message);
    static bool updateChatMessage(const std::shared_ptr<ChatMessage>& message);
    static bool deleteChatMessageFromDB(const std::string& id);
    static std::shared_ptr<ChatMessage> loadChatMessage(const std::string& id);
    
    // 反应操作
    static bool saveReaction(const std::shared_ptr<ChatMessageReaction>& reaction);
    static bool deleteReaction(const std::string& chatMessageId, const std::string& accountId);
    static std::vector<std::shared_ptr<ChatMessageReaction>> getReactions(const std::string& chatMessageId);

    // 工具函数
    static std::string generateUUID();
    static std::string getCurrentTimestamp();
}; 