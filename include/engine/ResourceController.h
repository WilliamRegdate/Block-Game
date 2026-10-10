#ifndef RESOURCE_CONTROLLER
#define RESOURCE_CONTROLLER

#include <string>
#include <unordered_map>

#include "Entity.h"
#include "MeshLoader.h"
#include "MaterialLoader.h"

// One entry of entities.json. Only ids are stored, so swapping a mesh or
// material is just changing a string.
struct EntityBlueprint
{
    std::string id;
    std::string meshID;      // folder name under models/ (holds model.gltf)
    std::string materialID;  // "id" of an entry in materials.json
};

//takes care of resource storing and loading.
//materials
//animaitons
//meshes
//
// This class only LOADS. Editing entities.json / materials.json lives in the
// derived ResourceEditor, which the entity editor uses.
class ResourceController
{
    public:
        // virtual: ResourceEditor derives from this
        virtual ~ResourceController();

        //loads an entity and links / loads its resources from a json file
        Entity LoadEntity(std::string ID, bool& failed);

        // Builds an entity straight from a blueprint (loading/caching its mesh
        // and material) without reading entities.json. LoadEntity uses this.
        Entity CreateEntity(const EntityBlueprint& blueprint, bool& failed);

        // Reads one entry of entities.json. False (and logs) if it isn't there.
        bool GetBlueprint(const std::string& ID, EntityBlueprint& out);

        std::string EntityFilePath() const { return resourcePath + entityPath; }
        std::string MaterialFilePath() const { return resourcePath + materialPath; }

        Mesh* GetMesh(uint32_t ID);
        Material* GetMaterial(uint32_t ID);

    protected:

        // protected so the derived editing class can find the same files
        const std::string resourcePath = "../resources/";
        const std::string entityPath = "blueprints/entities.json";
        const std::string modeldir = "models/";
        const std::string materialPath = "materials/materials.json";

    private:

        MaterialLoader materialLoader;
        MeshLoader meshLoader;

        //resource containers
        std::unordered_map<uint32_t, Mesh*> meshContainer;
        std::unordered_map<uint32_t, Material> materialContainer;
        std::unordered_map<uint32_t, BakedAnimationClip> animationContainer;

        std::string MeshFilePath(const std::string& meshID) const;

        bool AddMesh(Mesh* mesh);
        bool AddMaterial(Material material);
};

#endif