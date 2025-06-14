#pragma once

#include <string>

enum class ChatMessagePriority {
    Low,        // 低优先级，可以延迟处理
    Normal,     // 普通优先级，正常处理
    High,       // 高优先级，优先处理
    Urgent,     // 紧急优先级，立即处理
    System      // 系统优先级，最高优先级
};

class ChatMessagePriorityUtils {
public:
    static std::string toString(ChatMessagePriority priority) {
        switch (priority) {
            case ChatMessagePriority::Low:
                return "low";
            case ChatMessagePriority::Normal:
                return "normal";
            case ChatMessagePriority::High:
                return "high";
            case ChatMessagePriority::Urgent:
                return "urgent";
            case ChatMessagePriority::System:
                return "system";
            default:
                return "normal";
        }
    }

    static ChatMessagePriority fromString(const std::string& str) {
        if (str == "low") return ChatMessagePriority::Low;
        if (str == "normal") return ChatMessagePriority::Normal;
        if (str == "high") return ChatMessagePriority::High;
        if (str == "urgent") return ChatMessagePriority::Urgent;
        if (str == "system") return ChatMessagePriority::System;
        return ChatMessagePriority::Normal;
    }

    static bool isValid(const std::string& str) {
        return str == "low" || 
               str == "normal" || 
               str == "high" || 
               str == "urgent" || 
               str == "system";
    }

    static std::string getDisplayName(ChatMessagePriority priority) {
        switch (priority) {
            case ChatMessagePriority::Low:
                return "⚪ Low";
            case ChatMessagePriority::Normal:
                return "🔵 Normal";
            case ChatMessagePriority::High:
                return "🟡 High";
            case ChatMessagePriority::Urgent:
                return "🔴 Urgent";
            case ChatMessagePriority::System:
                return "⚫ System";
            default:
                return "Normal";
        }
    }

    static int getWeight(ChatMessagePriority priority) {
        switch (priority) {
            case ChatMessagePriority::Low:
                return 1;
            case ChatMessagePriority::Normal:
                return 2;
            case ChatMessagePriority::High:
                return 3;
            case ChatMessagePriority::Urgent:
                return 4;
            case ChatMessagePriority::System:
                return 5;
            default:
                return 2;
        }
    }

    static bool canDowngrade(ChatMessagePriority from, ChatMessagePriority to) {
        return getWeight(from) > getWeight(to);
    }

    static bool canUpgrade(ChatMessagePriority from, ChatMessagePriority to) {
        return getWeight(from) < getWeight(to);
    }

    static ChatMessagePriority getDefaultPriority() {
        return ChatMessagePriority::Normal;
    }

    static bool requiresImmediateProcessing(ChatMessagePriority priority) {
        return priority == ChatMessagePriority::Urgent || 
               priority == ChatMessagePriority::System;
    }

    static bool allowsBatching(ChatMessagePriority priority) {
        return priority == ChatMessagePriority::Low || 
               priority == ChatMessagePriority::Normal;
    }
}; 