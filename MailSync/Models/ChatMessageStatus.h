#pragma once

#include <string>

enum class ChatMessageStatus {
    Pending,    // 消息正在处理中
    Sent,       // 消息已发送
    Delivered,  // 消息已送达
    Read,       // 消息已读
    Failed,     // 消息发送失败
    Deleted     // 消息已删除
};

class ChatMessageStatusUtils {
public:
    static std::string toString(ChatMessageStatus status) {
        switch (status) {
            case ChatMessageStatus::Pending:
                return "pending";
            case ChatMessageStatus::Sent:
                return "sent";
            case ChatMessageStatus::Delivered:
                return "delivered";
            case ChatMessageStatus::Read:
                return "read";
            case ChatMessageStatus::Failed:
                return "failed";
            case ChatMessageStatus::Deleted:
                return "deleted";
            default:
                return "unknown";
        }
    }

    static ChatMessageStatus fromString(const std::string& str) {
        if (str == "pending") return ChatMessageStatus::Pending;
        if (str == "sent") return ChatMessageStatus::Sent;
        if (str == "delivered") return ChatMessageStatus::Delivered;
        if (str == "read") return ChatMessageStatus::Read;
        if (str == "failed") return ChatMessageStatus::Failed;
        if (str == "deleted") return ChatMessageStatus::Deleted;
        return ChatMessageStatus::Pending;
    }

    static bool isValid(const std::string& str) {
        return str == "pending" || 
               str == "sent" || 
               str == "delivered" || 
               str == "read" || 
               str == "failed" || 
               str == "deleted";
    }

    static std::string getDisplayName(ChatMessageStatus status) {
        switch (status) {
            case ChatMessageStatus::Pending:
                return "⏳ Pending";
            case ChatMessageStatus::Sent:
                return "📤 Sent";
            case ChatMessageStatus::Delivered:
                return "📨 Delivered";
            case ChatMessageStatus::Read:
                return "✓ Read";
            case ChatMessageStatus::Failed:
                return "❌ Failed";
            case ChatMessageStatus::Deleted:
                return "🗑️ Deleted";
            default:
                return "Unknown";
        }
    }

    static bool isTerminal(ChatMessageStatus status) {
        return status == ChatMessageStatus::Read || 
               status == ChatMessageStatus::Failed || 
               status == ChatMessageStatus::Deleted;
    }

    static bool canTransition(ChatMessageStatus from, ChatMessageStatus to) {
        switch (from) {
            case ChatMessageStatus::Pending:
                return to == ChatMessageStatus::Sent || 
                       to == ChatMessageStatus::Failed;
            case ChatMessageStatus::Sent:
                return to == ChatMessageStatus::Delivered || 
                       to == ChatMessageStatus::Failed;
            case ChatMessageStatus::Delivered:
                return to == ChatMessageStatus::Read || 
                       to == ChatMessageStatus::Failed;
            case ChatMessageStatus::Read:
                return to == ChatMessageStatus::Deleted;
            case ChatMessageStatus::Failed:
                return to == ChatMessageStatus::Pending;
            case ChatMessageStatus::Deleted:
                return false;
            default:
                return false;
        }
    }
}; 