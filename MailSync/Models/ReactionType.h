#pragma once

#include <string>

enum class ReactionType {
    Like,
    Unlike,
    Helpful,
    NotHelpful,
    Custom
};

class ReactionTypeUtils {
public:
    static std::string toString(ReactionType type) {
        switch (type) {
            case ReactionType::Like:
                return "like";
            case ReactionType::Unlike:
                return "unlike";
            case ReactionType::Helpful:
                return "helpful";
            case ReactionType::NotHelpful:
                return "not_helpful";
            case ReactionType::Custom:
                return "custom";
            default:
                return "unknown";
        }
    }

    static ReactionType fromString(const std::string& str) {
        if (str == "like") return ReactionType::Like;
        if (str == "unlike") return ReactionType::Unlike;
        if (str == "helpful") return ReactionType::Helpful;
        if (str == "not_helpful") return ReactionType::NotHelpful;
        if (str == "custom") return ReactionType::Custom;
        return ReactionType::Custom;
    }

    static bool isValid(const std::string& str) {
        return str == "like" || 
               str == "unlike" || 
               str == "helpful" || 
               str == "not_helpful" || 
               str == "custom";
    }

    static std::string getDisplayName(ReactionType type) {
        switch (type) {
            case ReactionType::Like:
                return "👍 Like";
            case ReactionType::Unlike:
                return "👎 Unlike";
            case ReactionType::Helpful:
                return "✅ Helpful";
            case ReactionType::NotHelpful:
                return "❌ Not Helpful";
            case ReactionType::Custom:
                return "Custom";
            default:
                return "Unknown";
        }
    }
}; 