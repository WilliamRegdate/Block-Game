#ifndef MATERIALLOADER_H
#define MATERIALLOADER_H

#include <string>
#include <vector>

#include <json/json.h>
#include "Material.h"

using json = nlohmann::json;

class MaterialLoader
{
    public:
        std::vector<Material> Load(const char* file);
        Material LoadByID(const char* file, const char* ID);

    private:
        const char* file = nullptr;
        json JSON;

        Material loadMaterial(const json& entry);

        bool fileExists(const std::string& fileName);
};

#endif