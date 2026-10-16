#include "MaterialLoader.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <functional>

// File-local, same reasoning as MeshLoader.cpp's copy of these.
namespace
{

std::string get_file_contents(const char* filename)
{
    std::FILE* fp = std::fopen(filename, "rb");
    if (!fp)
        throw std::runtime_error(std::string("Could not open file: ") + filename);

    std::string contents;
    std::fseek(fp, 0, SEEK_END);
    contents.resize(static_cast<size_t>(std::ftell(fp)));
    std::rewind(fp);
    std::fread(&contents[0], 1, contents.size(), fp);
    std::fclose(fp);
    return contents;
}

// Truncated to 32 bits to match Material::ID's width, same tradeoff as
// MeshLoader's HashPath.
uint32_t HashID(const std::string& id)
{
    return static_cast<uint32_t>(std::hash<std::string>{}(id));
}

// TODO: "res://" is a placeholder scheme - stripped and treated as relative
// to the working directory for now. Replace with whatever your actual
// asset-root resolution ends up being (once ResourceContainer exists).
std::string ResolveResourcePath(const std::string& path)
{
    const std::string prefix = "res://";
    if (path.rfind(prefix, 0) == 0)
        return path.substr(prefix.size());
    return path;
}

} // namespace

std::vector<Material> MaterialLoader::Load(const char* filePath)
{
    std::vector<Material> materials;

    if (!fileExists(filePath))
    {
        std::cerr << "Materials file not found: " << filePath << std::endl;
        return materials;
    }

    file = filePath;
    JSON = json::parse(get_file_contents(filePath));

    if (!JSON.contains("materials"))
    {
        std::cerr << "Materials file has no \"materials\" array: " << filePath << std::endl;
        return materials;
    }

    materials.reserve(JSON["materials"].size());
    for (const auto& entry : JSON["materials"])
        materials.push_back(loadMaterial(entry));

    return materials;
}

Material MaterialLoader::loadMaterial(const json& entry)
{
    Material material;

    if (!entry.contains("id"))
        std::cerr << "Material entry missing \"id\" in: " << file << std::endl;

    const std::string id = entry.value("id", std::string());
    material.ID = HashID(id);

    if (entry.contains("albedoPath"))
        material.albedo.emplace(ResolveResourcePath(entry["albedoPath"]).c_str());
    if (entry.contains("normalPath"))
        material.normal.emplace(ResolveResourcePath(entry["normalPath"]).c_str());
    if (entry.contains("ormPath"))
        material.orm.emplace(ResolveResourcePath(entry["ormPath"]).c_str());

    return material;
}
Material MaterialLoader::LoadByID(const char* filePath, const char* ID)
{
    if (!fileExists(filePath))
    {
        std::cerr << "Materials file not found: " << filePath << std::endl;
        return Material();
    }

    file = filePath;
    JSON = json::parse(get_file_contents(filePath));

    if (!JSON.contains("materials"))
    {
        std::cerr << "Materials file has no \"materials\" array: " << filePath << std::endl;
        return Material();
    }

    for (const auto& entry : JSON["materials"])
    {
        // Compare the string id first so we only load textures for the match.
        if (entry.value("id", std::string()) == ID)
            return loadMaterial(entry);
    }

    std::cerr << "Material \"" << ID << "\" not found in: " << filePath << std::endl;
    return Material();
}

bool MaterialLoader::fileExists(const std::string& fileName)
{
    std::ifstream infile(fileName);
    return infile.good();
}