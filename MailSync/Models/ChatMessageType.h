#pragma once

#include <string>

enum class ChatMessageType {
    Thread,
    Message,
    System,
    Error
};

class ChatMessageTypeUtils {
public:
    static std::string toString(ChatMessageType type) {
        switch (type) {
            case ChatMessageType::Thread:
                return "thread";
            case ChatMessageType::Message:
                return "message";
            case ChatMessageType::System:
                return "system";
            case ChatMessageType::Error:
                return "error";
            default:
                return "unknown";
        }
    }

    static ChatMessageType fromString(const std::string& str) {
        if (str == "thread") return ChatMessageType::Thread;
        if (str == "message") return ChatMessageType::Message;
        if (str == "system") return ChatMessageType::System;
        if (str == "error") return ChatMessageType::Error;
        return ChatMessageType::Error;
    }

    static bool isValid(const std::string& str) {
        return str == "thread" || 
               str == "message" || 
               str == "system" || 
               str == "error";
    }
}; 