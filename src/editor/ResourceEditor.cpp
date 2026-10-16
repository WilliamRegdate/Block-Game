#include "ResourceEditor.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "ResourceJson.h"

using json = nlohmann::json;
using ResourceJson::ReadJsonFile;
using ResourceJson::StringField;

namespace
{
    // ordered_json keeps the key order of the existing file, so saving doesn't
    // reshuffle a file you edit by hand. (Needs nlohmann >= 3.9.)
    using ojson = nlohmann::ordered_json;

    // ---- shared by the Save* functions ----

    // Loads `file` into `root` for editing (an empty object if the file
    // doesn't exist yet). Refuses files that aren't valid JSON, so a save
    // can never overwrite something it couldn't read.
    bool OpenForEdit(const std::string& file, ojson& root)
    {
        root = ojson::object();

        std::error_code ec;
        if (std::filesystem::exists(file, ec) && !ReadJsonFile(file, root))
        {
            std::cerr << "ResourceController: " << file << " is not valid JSON, not overwriting it\n";
            return false;
        }
        if (!root.is_object())
        {
            std::cerr << "ResourceController: " << file << " root is not an object, not overwriting it\n";
            return false;
        }
        return true;
    }

    // The entry in root[arrayKey] whose "id" matches, appending an empty one
    // if there isn't one (and creating the array if needed).
    ojson& FindOrAddEntry(ojson& root, const char* arrayKey, const std::string& id)
    {
        if (!root.contains(arrayKey) || !root[arrayKey].is_array())
            root[arrayKey] = ojson::array();

        for (auto& entry : root[arrayKey])
        {
            if (StringField(entry, "id") == id)
                return entry;
        }

        root[arrayKey].push_back(ojson::object());
        return root[arrayKey].back();
    }

    void SetOrErase(ojson& entry, const char* key, const std::string& value)
    {
        if (value.empty())
            entry.erase(key);
        else
            entry[key] = value;
    }

    // Serializes fully before touching the file.
    bool WriteJson(const std::string& file, const ojson& root)
    {
        const std::string text = root.dump(4) + "\n";

        std::ofstream out(file, std::ios::trunc);
        if (!out.is_open())
        {
            std::cerr << "ResourceController: could not write " << file << "\n";
            return false;
        }
        out << text;
        out.close();
        return !out.fail();
    }
}

// ============================================================
// entities.json
// ============================================================

ResourceEditor::ResourceEditor(Renderer &renderer)
    : ResourceController(renderer)
{
}

std::vector<std::string> ResourceEditor::ListEntityIDs()
{
    std::vector<std::string> ids;

    json root;
    if (!ReadJsonFile(EntityFilePath(), root))
        return ids;
    if (!root.contains("entities") || !root["entities"].is_array())
        return ids;

    for (const auto& entry : root["entities"])
    {
        const std::string id = StringField(entry, "id");
        if (!id.empty())
            ids.push_back(id);
    }
    return ids;
}

bool ResourceEditor::SaveBlueprint(const EntityBlueprint& blueprint)
{
    if (blueprint.id.empty() || blueprint.meshID.empty() || blueprint.materialID.empty())
    {
        std::cerr << "EntityLoader: refusing to save a blueprint with an empty id/meshID/materialID\n";
        return false;
    }

    const std::string file = EntityFilePath();

    ojson root;
    if (!OpenForEdit(file, root))
        return false;

    ojson& entry = FindOrAddEntry(root, "entities", blueprint.id);
    entry["id"]         = blueprint.id;
    entry["meshID"]     = blueprint.meshID;
    entry["materialID"] = blueprint.materialID;

    return WriteJson(file, root);
}

// ============================================================
// materials.json
// ============================================================

bool ResourceEditor::GetMaterialBlueprint(const std::string& ID, MaterialBlueprint& out)
{
    json root;
    if (!ReadJsonFile(MaterialFilePath(), root))
    {
        std::cerr << "MaterialLoader: could not read " << MaterialFilePath() << "\n";
        return false;
    }

    if (!root.contains("materials") || !root["materials"].is_array())
    {
        std::cerr << "MaterialLoader: no \"materials\" array in " << MaterialFilePath() << "\n";
        return false;
    }

    for (const auto& entry : root["materials"])
    {
        if (StringField(entry, "id") != ID)
            continue;

        out.id         = ID;
        out.albedoPath = StringField(entry, "albedoPath");
        out.normalPath = StringField(entry, "normalPath");
        out.ormPath    = StringField(entry, "ormPath");
        return true;
    }

    std::cerr << "MaterialLoader: no material found with id: " << ID << "\n";
    return false;
}

bool ResourceEditor::SaveMaterialBlueprint(const MaterialBlueprint& material)
{
    if (material.id.empty())
    {
        std::cerr << "MaterialLoader: refusing to save a material with an empty id\n";
        return false;
    }

    const std::string file = MaterialFilePath();

    ojson root;
    if (!OpenForEdit(file, root))
        return false;

    ojson& entry = FindOrAddEntry(root, "materials", material.id);
    entry["id"] = material.id;
    SetOrErase(entry, "albedoPath", material.albedoPath);
    SetOrErase(entry, "normalPath", material.normalPath);
    SetOrErase(entry, "ormPath",    material.ormPath);

    return WriteJson(file, root);
}

// ============================================================
// Asset discovery
// ============================================================

std::vector<std::string> ResourceEditor::ListMeshIDs()
{
    namespace fs = std::filesystem;

    std::vector<std::string> ids;
    std::error_code ec;
    for (const auto& dir : fs::directory_iterator(resourcePath + modeldir, ec))
    {
        if (dir.is_directory(ec) && fs::exists(dir.path() / "model.gltf", ec))
            ids.push_back(dir.path().filename().string());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> ResourceEditor::ListMaterialIDs()
{
    std::vector<std::string> ids;

    json root;
    if (!ReadJsonFile(MaterialFilePath(), root))
        return ids;
    if (!root.contains("materials") || !root["materials"].is_array())
        return ids;

    for (const auto& entry : root["materials"])
    {
        const std::string id = StringField(entry, "id");
        if (!id.empty())
            ids.push_back(id);
    }
    return ids;
}

std::vector<std::string> ResourceEditor::ListTextureFiles()
{
    namespace fs = std::filesystem;

    std::vector<std::string> files;
    const fs::path root(TextureDir());

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(root, ec))
    {
        if (!entry.is_regular_file(ec))
            continue;

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        // formats stb_image can read
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp")
            files.push_back(entry.path().lexically_relative(root).generic_string());
    }
    std::sort(files.begin(), files.end());
    return files;
}