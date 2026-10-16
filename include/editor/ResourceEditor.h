#ifndef RESOURCE_EDITOR
#define RESOURCE_EDITOR

#include <string>
#include <vector>

#include "ResourceController.h"

// One entry of materials.json. An empty path means that slot is not used
// (a material can have any of the three textures, or none).
// Paths keep the format used in the file: "../resources/textures/chicken.png"
struct MaterialBlueprint
{
    std::string id;
    std::string albedoPath;
    std::string normalPath;
    std::string ormPath;
};

// A ResourceController that can also browse the resource folders and edit
// entities.json / materials.json. Only the entity editor needs this; the game
// itself can use the plain ResourceController, which only loads. Because it
// derives from ResourceController, it can be passed anywhere one is expected.
class ResourceEditor : public ResourceController
{
    public:
        ResourceEditor(Renderer& renderer);
        // ---- entities.json ----
        std::vector<std::string> ListEntityIDs();
        // Replaces the entry with the same "id", or appends a new one. Other
        // keys in an existing entry and every other entry are left untouched.
        bool SaveBlueprint(const EntityBlueprint& blueprint);

        // ---- asset discovery (for pickers) ----
        std::vector<std::string> ListMeshIDs();      // folders in models/ containing model.gltf
        std::vector<std::string> ListMaterialIDs();  // ids in materials.json
        std::vector<std::string> ListTextureFiles(); // image files under textures/, relative to TextureDir()

        // ---- materials.json ----
        bool GetMaterialBlueprint(const std::string& ID, MaterialBlueprint& out);
        // Replaces the entry with the same "id", or appends a new one. Empty
        // paths are removed from the entry; other keys are left untouched.
        // Note: a material that is already loaded keeps its old textures until
        // the program restarts (the renderer registered it by ID).
        bool SaveMaterialBlueprint(const MaterialBlueprint& material);

        std::string TextureDir() const { return resourcePath + texturedir; }  // "../resources/textures/"

    private:
        const std::string texturedir = "textures/";
};

#endif