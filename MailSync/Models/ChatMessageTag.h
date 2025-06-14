#pragma once

#include <string>
#include <vector>
#include <set>

enum class ChatMessageTag {
    None,           // 无标签
    Question,       // 问题
    Answer,         // 回答
    Suggestion,     // 建议
    Feedback,       // 反馈
    Error,          // 错误
    Warning,        // 警告
    Info,           // 信息
    Success,        // 成功
    Important,      // 重要
    Draft,          // 草稿
    Archived,       // 已归档
    Pinned,         // 已置顶
    Starred,        // 已标星
    Custom          // 自定义标签
};

class ChatMessageTagUtils {
public:
    static std::string toString(ChatMessageTag tag) {
        switch (tag) {
            case ChatMessageTag::None:
                return "none";
            case ChatMessageTag::Question:
                return "question";
            case ChatMessageTag::Answer:
                return "answer";
            case ChatMessageTag::Suggestion:
                return "suggestion";
            case ChatMessageTag::Feedback:
                return "feedback";
            case ChatMessageTag::Error:
                return "error";
            case ChatMessageTag::Warning:
                return "warning";
            case ChatMessageTag::Info:
                return "info";
            case ChatMessageTag::Success:
                return "success";
            case ChatMessageTag::Important:
                return "important";
            case ChatMessageTag::Draft:
                return "draft";
            case ChatMessageTag::Archived:
                return "archived";
            case ChatMessageTag::Pinned:
                return "pinned";
            case ChatMessageTag::Starred:
                return "starred";
            case ChatMessageTag::Custom:
                return "custom";
            default:
                return "none";
        }
    }

    static ChatMessageTag fromString(const std::string& str) {
        if (str == "none") return ChatMessageTag::None;
        if (str == "question") return ChatMessageTag::Question;
        if (str == "answer") return ChatMessageTag::Answer;
        if (str == "suggestion") return ChatMessageTag::Suggestion;
        if (str == "feedback") return ChatMessageTag::Feedback;
        if (str == "error") return ChatMessageTag::Error;
        if (str == "warning") return ChatMessageTag::Warning;
        if (str == "info") return ChatMessageTag::Info;
        if (str == "success") return ChatMessageTag::Success;
        if (str == "important") return ChatMessageTag::Important;
        if (str == "draft") return ChatMessageTag::Draft;
        if (str == "archived") return ChatMessageTag::Archived;
        if (str == "pinned") return ChatMessageTag::Pinned;
        if (str == "starred") return ChatMessageTag::Starred;
        if (str == "custom") return ChatMessageTag::Custom;
        return ChatMessageTag::None;
    }

    static bool isValid(const std::string& str) {
        return str == "none" || 
               str == "question" || 
               str == "answer" || 
               str == "suggestion" || 
               str == "feedback" || 
               str == "error" || 
               str == "warning" || 
               str == "info" || 
               str == "success" || 
               str == "important" || 
               str == "draft" || 
               str == "archived" || 
               str == "pinned" || 
               str == "starred" || 
               str == "custom";
    }

    static std::string getDisplayName(ChatMessageTag tag) {
        switch (tag) {
            case ChatMessageTag::None:
                return "无标签";
            case ChatMessageTag::Question:
                return "❓ 问题";
            case ChatMessageTag::Answer:
                return "💡 回答";
            case ChatMessageTag::Suggestion:
                return "💭 建议";
            case ChatMessageTag::Feedback:
                return "📝 反馈";
            case ChatMessageTag::Error:
                return "❌ 错误";
            case ChatMessageTag::Warning:
                return "⚠️ 警告";
            case ChatMessageTag::Info:
                return "ℹ️ 信息";
            case ChatMessageTag::Success:
                return "✅ 成功";
            case ChatMessageTag::Important:
                return "❗ 重要";
            case ChatMessageTag::Draft:
                return "📄 草稿";
            case ChatMessageTag::Archived:
                return "📦 已归档";
            case ChatMessageTag::Pinned:
                return "📌 已置顶";
            case ChatMessageTag::Starred:
                return "⭐ 已标星";
            case ChatMessageTag::Custom:
                return "🏷️ 自定义";
            default:
                return "无标签";
        }
    }

    static std::string getColor(ChatMessageTag tag) {
        switch (tag) {
            case ChatMessageTag::Error:
                return "#FF0000";
            case ChatMessageTag::Warning:
                return "#FFA500";
            case ChatMessageTag::Success:
                return "#00FF00";
            case ChatMessageTag::Info:
                return "#0000FF";
            case ChatMessageTag::Important:
                return "#FF00FF";
            case ChatMessageTag::Question:
                return "#800080";
            case ChatMessageTag::Answer:
                return "#008000";
            case ChatMessageTag::Suggestion:
                return "#00FFFF";
            case ChatMessageTag::Feedback:
                return "#FFC0CB";
            case ChatMessageTag::Draft:
                return "#A9A9A9";
            case ChatMessageTag::Archived:
                return "#808080";
            case ChatMessageTag::Pinned:
                return "#FFD700";
            case ChatMessageTag::Starred:
                return "#FFD700";
            case ChatMessageTag::Custom:
                return "#000000";
            default:
                return "#000000";
        }
    }

    static bool isSystemTag(ChatMessageTag tag) {
        return tag == ChatMessageTag::Error ||
               tag == ChatMessageTag::Warning ||
               tag == ChatMessageTag::Info ||
               tag == ChatMessageTag::Success;
    }

    static bool isUserTag(ChatMessageTag tag) {
        return tag == ChatMessageTag::Question ||
               tag == ChatMessageTag::Answer ||
               tag == ChatMessageTag::Suggestion ||
               tag == ChatMessageTag::Feedback;
    }

    static bool isStatusTag(ChatMessageTag tag) {
        return tag == ChatMessageTag::Draft ||
               tag == ChatMessageTag::Archived ||
               tag == ChatMessageTag::Pinned ||
               tag == ChatMessageTag::Starred;
    }

    static std::vector<ChatMessageTag> getAllTags() {
        return {
            ChatMessageTag::None,
            ChatMessageTag::Question,
            ChatMessageTag::Answer,
            ChatMessageTag::Suggestion,
            ChatMessageTag::Feedback,
            ChatMessageTag::Error,
            ChatMessageTag::Warning,
            ChatMessageTag::Info,
            ChatMessageTag::Success,
            ChatMessageTag::Important,
            ChatMessageTag::Draft,
            ChatMessageTag::Archived,
            ChatMessageTag::Pinned,
            ChatMessageTag::Starred,
            ChatMessageTag::Custom
        };
    }

    static std::set<ChatMessageTag> getCompatibleTags(ChatMessageTag tag) {
        std::set<ChatMessageTag> compatible;
        switch (tag) {
            case ChatMessageTag::Question:
                compatible.insert({ChatMessageTag::Important, ChatMessageTag::Pinned});
                break;
            case ChatMessageTag::Answer:
                compatible.insert({ChatMessageTag::Success, ChatMessageTag::Starred});
                break;
            case ChatMessageTag::Error:
                compatible.insert({ChatMessageTag::Important});
                break;
            case ChatMessageTag::Warning:
                compatible.insert({ChatMessageTag::Important});
                break;
            case ChatMessageTag::Draft:
                compatible.insert({ChatMessageTag::Important});
                break;
            default:
                break;
        }
        return compatible;
    }
}; 