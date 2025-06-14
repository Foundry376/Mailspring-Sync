#pragma once

#include "ChatMessage.h"

class MessageChatMessage : public ChatMessage {
public:
    MessageChatMessage();
    
    std::string messageId;
    json contextData;

    // 重写基类方法
    json toJSON() const override;
    void fromJSON(const json& j) override;
    bool isValid() const override;

    // 数据库操作
    static std::shared_ptr<MessageChatMessage> find(const std::string& id);
    static std::vector<std::shared_ptr<MessageChatMessage>> findByMessageId(const std::string& messageId);

protected:
    std::string tableName() const override { return "MessageChatMessage"; }
}; 