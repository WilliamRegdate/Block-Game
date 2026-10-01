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
        MaterialLoader();

        // Loads every material listed in a materials JSON file and returns
        // one Material per entry, in file order. A material missing one of
        // albedoPath/normalPath/ormPath just leaves that Texture unset -
        // not an error.
        //
        // Material::ID is a hash of the entry's "id" string, so the same
        // id always maps to the same numeric ID.
        //
        // Expected format:
        // {
        //   "materials": [
        //     { "id": "metal_wall", "albedoPath": "...", "normalPath": "...", "ormPath": "..." }
        //   ]
        // }
        std::vector<Material> Load(const char* filePath);

    private:
        const char* file = nullptr;
        json JSON;

        Material loadMaterial(const json& entry);

        bool fileExists(const std::string& fileName);
};

#endif