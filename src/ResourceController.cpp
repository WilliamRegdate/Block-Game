#include "ResourceController.h"

#include <functional>
#include <iostream>

#include "ResourceJson.h"

using json = nlohmann::json;
using ResourceJson::ReadJsonFile;
using ResourceJson::StringField;

namespace
{
    uint32_t Hash(const char* string)
    {
        return static_cast<uint32_t>(std::hash<std::string>{}(string));
    }
}

ResourceController::~ResourceController()
{
    for (auto& mesh : meshContainer)
    {
        delete(mesh.second);
    }
}

// ============================================================
// Entities
// ============================================================

Entity ResourceController::LoadEntity(std::string ID, bool& failed)
{
    EntityBlueprint blueprint;
    if (!GetBlueprint(ID, blueprint))
    {
        failed = true;
        return Entity();
    }
    return CreateEntity(blueprint, failed);
}

Entity ResourceController::CreateEntity(const EntityBlueprint& blueprint, bool& failed)
{
    failed = false;

    Entity entity;

    //load/ find model
    const std::string meshFile = MeshFilePath(blueprint.meshID);
    Mesh* mesh = GetMesh(Hash(meshFile.c_str()));
    if (mesh == nullptr)
    {
        mesh = meshLoader.Load(meshFile.c_str());
        if (mesh == nullptr)
        {
            std::cerr << "EntityLoader: mesh failed to load: " << meshFile << "\n";
            failed = true;
            return Entity();
        }
        //register the mesh
        AddMesh(mesh);
    }
    entity.mesh = mesh;

    //load/ find material
    Material* material = GetMaterial(Hash(blueprint.materialID.c_str()));
    if (material == nullptr)
    {
        AddMaterial(materialLoader.LoadByID(MaterialFilePath().c_str(), blueprint.materialID.c_str()));
        material = GetMaterial(Hash(blueprint.materialID.c_str()));

        if (material == nullptr)
        {
            std::cerr << "EntityLoader: material \"" << blueprint.materialID << "\" failed to load\n";
            failed = true;
            return Entity();
        }
    }
    entity.material = material;

    //load animation clips from mesh
    if (mesh->isSkinned)
    {
        SkinnedMesh* skinned = static_cast<SkinnedMesh*>(mesh);
        for (auto& clip : skinned->animations)
        {
            entity.animationPlayer.AddClip(clip.name, &clip);
        }
    }

    entity.transform = Transform();
    entity.ID = Hash(blueprint.id.c_str());
    return entity;
}

bool ResourceController::GetBlueprint(const std::string& ID, EntityBlueprint& out)
{
    json root;
    if (!ReadJsonFile(EntityFilePath(), root))
    {
        std::cerr << "EntityLoader: could not read " << EntityFilePath() << "\n";
        return false;
    }

    if (!root.contains("entities") || !root["entities"].is_array())
    {
        std::cerr << "EntityLoader: no \"entities\" array in " << EntityFilePath() << "\n";
        return false;
    }

    for (const auto& entry : root["entities"])
    {
        if (StringField(entry, "id") != ID)
            continue;

        out.id         = ID;
        out.meshID     = StringField(entry, "meshID");
        out.materialID = StringField(entry, "materialID");
        return true;
    }

    std::cerr << "EntityLoader: no entity blueprint found with id: " << ID << "\n";
    return false;
}

// ============================================================
// Containers
// ============================================================

std::string ResourceController::MeshFilePath(const std::string& meshID) const
{
    return resourcePath + modeldir + meshID + "/model.gltf";
}

Mesh* ResourceController::GetMesh(uint32_t ID)
{
    auto it = meshContainer.find(ID);
    return it != meshContainer.end() ? it->second : nullptr;
}
Material* ResourceController::GetMaterial(uint32_t ID)
{
    auto it = materialContainer.find(ID);
    return it != materialContainer.end() ? &it->second : nullptr;
}

bool ResourceController::AddMesh(Mesh* mesh)
{
    if (mesh == nullptr) return false;
    auto pair = meshContainer.emplace(mesh->ID, mesh);
    return pair.second;
}
bool ResourceController::AddMaterial(Material material)
{
    if (!material.ID) return false;
    auto pair = materialContainer.emplace(material.ID, material);
    return pair.second;
}