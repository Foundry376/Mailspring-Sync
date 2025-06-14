#pragma once

#include "ChatMessage.h"

class ThreadChatMessage : public ChatMessage {
public:
    ThreadChatMessage();
    
    std::string threadId;
    std::string conversationId;

    // 重写基类方法
    json toJSON() const override;
    void fromJSON(const json& j) override;
    bool isValid() const override;

    // 数据库操作
    static std::shared_ptr<ThreadChatMessage> find(const std::string& id);
    static std::vector<std::shared_ptr<ThreadChatMessage>> findByThreadId(const std::string& threadId);
    static std::vector<std::shared_ptr<ThreadChatMessage>> findByConversationId(const std::string& conversationId);

protected:
    std::string tableName() const override { return "ThreadChatMessage"; }
}; 