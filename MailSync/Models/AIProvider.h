#pragma once

#include <string>

enum class AIProvider {
    GPT4,
    GPT35,
    Claude,
    Gemini,
    Custom
};

class AIProviderUtils {
public:
    static std::string toString(AIProvider provider) {
        switch (provider) {
            case AIProvider::GPT4:
                return "gpt-4";
            case AIProvider::GPT35:
                return "gpt-3.5-turbo";
            case AIProvider::Claude:
                return "claude-3";
            case AIProvider::Gemini:
                return "gemini-pro";
            case AIProvider::Custom:
                return "custom";
            default:
                return "unknown";
        }
    }

    static AIProvider fromString(const std::string& str) {
        if (str == "gpt-4") return AIProvider::GPT4;
        if (str == "gpt-3.5-turbo") return AIProvider::GPT35;
        if (str == "claude-3") return AIProvider::Claude;
        if (str == "gemini-pro") return AIProvider::Gemini;
        if (str == "custom") return AIProvider::Custom;
        return AIProvider::Custom;
    }

    static bool isValid(const std::string& str) {
        return str == "gpt-4" || 
               str == "gpt-3.5-turbo" || 
               str == "claude-3" || 
               str == "gemini-pro" || 
               str == "custom";
    }

    static std::string getModelName(AIProvider provider) {
        switch (provider) {
            case AIProvider::GPT4:
                return "GPT-4";
            case AIProvider::GPT35:
                return "GPT-3.5 Turbo";
            case AIProvider::Claude:
                return "Claude 3";
            case AIProvider::Gemini:
                return "Gemini Pro";
            case AIProvider::Custom:
                return "Custom Model";
            default:
                return "Unknown Model";
        }
    }
}; 