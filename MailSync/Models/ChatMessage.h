#pragma once

#include <string>
#include <memory>
#include <vector>
#include "json.hpp"
#include "Model.h"

using json = nlohmann::json;

class ChatMessage : public Model {
public:
    ChatMessage();
    virtual ~ChatMessage() = default;

    // 基本属性
    std::string id;
    std::string accountId;
    int version;
    std::string messageType;
    std::string content;
    std::string aiProvider;
    int likeCount;
    int unlikeCount;
    std::string createdAt;
    std::string updatedAt;

    // 序列化/反序列化
    virtual json toJSON() const override;
    virtual void fromJSON(const json& j) override;

    // 验证
    virtual bool isValid() const override;

    // 数据库操作
    static std::shared_ptr<ChatMessage> find(const std::string& id);
    static std::vector<std::shared_ptr<ChatMessage>> findAll(const std::string& accountId);
    bool save();
    bool remove();

protected:
    virtual std::string tableName() const = 0;
}; 