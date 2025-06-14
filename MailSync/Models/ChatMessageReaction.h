#pragma once

#include <string>
#include <memory>
#include <vector>
#include "json.hpp"
#include "Model.h"

using json = nlohmann::json;

class ChatMessageReaction : public Model {
public:
    ChatMessageReaction();
    
    std::string id;
    std::string chatMessageId;
    std::string chatMessageType;
    std::string accountId;
    std::string reactionType;
    std::string createdAt;

    // 序列化/反序列化
    json toJSON() const override;
    void fromJSON(const json& j) override;

    // 验证
    bool isValid() const override;

    // 数据库操作
    static std::shared_ptr<ChatMessageReaction> find(const std::string& id);
    static std::vector<std::shared_ptr<ChatMessageReaction>> findByChatMessageId(const std::string& chatMessageId);
    static std::shared_ptr<ChatMessageReaction> findByChatMessageIdAndAccountId(
        const std::string& chatMessageId,
        const std::string& accountId
    );
    bool save();
    bool remove();

protected:
    std::string tableName() const override { return "ChatMessageReaction"; }
}; 