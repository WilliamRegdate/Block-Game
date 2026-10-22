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

ResourceController::ResourceController(Renderer &renderer)
    : renderer(renderer)
{
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
    const std::string meshFile = resourcePath + modeldir + blueprint.meshID + "/model.gltf";
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
    entity.instance.mesh = mesh;

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
    entity.instance.material = material;

    //load animation clips from mesh
    if (mesh->isSkinned)
    {
        SkinnedMesh* skinned = static_cast<SkinnedMesh*>(mesh);
        for (auto& clip : skinned->animations)
        {
            entity.instance.animationPlayer.AddClip(clip.name, &clip);
        }
    }

    entity.transform = Transform();
    entity.instance.ID = Hash(blueprint.id.c_str());
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

BlockType* ResourceController::GetOrLoadBlockType(uint32_t ID)
{
    auto it = blockTypeContainer.find(ID);
    if (it != blockTypeContainer.end())
    {
        //does exist dont need anything else
        return &it->second;
    } 
    //try to load block
    std::vector<std::string> textures;
    BlockType block = blockLoader.Load((resourcePath + blockPath + std::to_string(ID) + ".json").c_str(), textures);

    //check if textures exist already
    //if not store them
    for (size_t i = 0; i < textures.size(); i++)
    {
        uint32_t layer = GetChunkTexture(textures[i]);
        if (layer == UINT32_MAX)
        {
            renderer.AddChunkTexture((resourcePath + blockTexturePath + textures[i] + ".png").c_str(), layer);
            AddChunkTexture(textures[i], layer);
            
        }
        //set the layers of this block
        if (block.tex_x_plus == i)
            block.tex_x_plus = layer;
        if (block.tex_x_minus == i)
            block.tex_x_minus = layer;
        if (block.tex_y_plus == i)
            block.tex_y_plus = layer;
        if (block.tex_y_minus == i)
            block.tex_y_minus = layer;
        if (block.tex_z_plus == i)
            block.tex_z_plus = layer;
        if (block.tex_z_minus == i)
            block.tex_z_minus = layer;
    }
    AddBlockType(block);

    return nullptr;
}
uint32_t ResourceController::GetChunkTexture(std::string name)
{
    auto it = chunkTextureContainer.find(Hash(name.c_str()));
    return it != chunkTextureContainer.end() ? it->second : UINT32_MAX;
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

bool ResourceController::AddBlockType(BlockType type)
{
    auto pair = blockTypeContainer.emplace(type.id, type);
    return pair.second;
}

bool ResourceController::AddChunkTexture(std::string name, uint32_t layer)
{
    auto pair = chunkTextureContainer.emplace(Hash(name.c_str()), layer);
    return pair.second;
}
