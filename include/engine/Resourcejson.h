#ifndef RESOURCE_JSON_H
#define RESOURCE_JSON_H

// Small JSON helpers shared by ResourceController (loading) and
// ResourceEditor (editing), so neither has its own copy.

#include <fstream>
#include <string>

#include <json/json.h>

namespace ResourceJson
{
    // Parses a JSON file. False if it can't be opened or isn't valid JSON.
    // Templated so it works for both json and ordered_json.
    template <class J>
    bool ReadJsonFile(const std::string& path, J& out)
    {
        std::ifstream stream(path);
        if (!stream.is_open())
            return false;
        out = J::parse(stream, nullptr, false);
        return !out.is_discarded();
    }

    // "" if the key is missing or isn't a string (entry.value("id", "")
    // throws on a non-string "id").
    template <class J>
    std::string StringField(const J& entry, const char* key)
    {
        if (!entry.is_object())
            return std::string();
        auto it = entry.find(key);
        if (it == entry.end() || !it->is_string())
            return std::string();
        return it->template get<std::string>();
    }
}

#endif