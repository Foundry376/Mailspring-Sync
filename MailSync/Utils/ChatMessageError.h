#pragma once

#include <string>
#include <stdexcept>

class ChatMessageError : public std::runtime_error {
public:
    enum class ErrorType {
        InvalidMessage,
        DatabaseError,
        NotFound,
        InvalidReaction,
        InvalidOperation,
        NetworkError,
        Unknown
    };

    ChatMessageError(ErrorType type, const std::string& message)
        : std::runtime_error(message), type_(type) {}

    ErrorType type() const { return type_; }

    static std::string typeToString(ErrorType type) {
        switch (type) {
            case ErrorType::InvalidMessage:
                return "InvalidMessage";
            case ErrorType::DatabaseError:
                return "DatabaseError";
            case ErrorType::NotFound:
                return "NotFound";
            case ErrorType::InvalidReaction:
                return "InvalidReaction";
            case ErrorType::InvalidOperation:
                return "InvalidOperation";
            case ErrorType::NetworkError:
                return "NetworkError";
            default:
                return "Unknown";
        }
    }

private:
    ErrorType type_;
}; 