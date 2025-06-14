#pragma once

#include <string>
#include <vector>
#include "json.hpp"

using json = nlohmann::json;

class JsonUtils {
public:
    // 将对象转换为 JSON 字符串
    template<typename T>
    static std::string toJsonString(const T& obj) {
        return obj.toJSON().dump();
    }

    // 从 JSON 字符串创建对象
    template<typename T>
    static std::shared_ptr<T> fromJsonString(const std::string& jsonStr) {
        try {
            auto j = json::parse(jsonStr);
            auto obj = std::make_shared<T>();
            obj->fromJSON(j);
            return obj;
        } catch (const std::exception& e) {
            return nullptr;
        }
    }

    // 将对象列表转换为 JSON 字符串
    template<typename T>
    static std::string toJsonString(const std::vector<std::shared_ptr<T>>& objects) {
        json j = json::array();
        for (const auto& obj : objects) {
            j.push_back(obj->toJSON());
        }
        return j.dump();
    }

    // 从 JSON 字符串创建对象列表
    template<typename T>
    static std::vector<std::shared_ptr<T>> fromJsonStringArray(const std::string& jsonStr) {
        std::vector<std::shared_ptr<T>> objects;
        try {
            auto j = json::parse(jsonStr);
            for (const auto& item : j) {
                auto obj = std::make_shared<T>();
                obj->fromJSON(item);
                objects.push_back(obj);
            }
        } catch (const std::exception& e) {
            // 处理错误
        }
        return objects;
    }

    // 安全地获取 JSON 值
    template<typename T>
    static T getValue(const json& j, const std::string& key, const T& defaultValue) {
        try {
            return j.value(key, defaultValue);
        } catch (const std::exception& e) {
            return defaultValue;
        }
    }

    // 检查 JSON 是否包含所有必需的字段
    static bool hasRequiredFields(const json& j, const std::vector<std::string>& requiredFields) {
        for (const auto& field : requiredFields) {
            if (!j.contains(field)) {
                return false;
            }
        }
        return true;
    }
}; 