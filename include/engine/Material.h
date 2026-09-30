#ifndef MATERIAL_H
#define MATERIAL_H

#include "Texture.h"
#include <optional>
#include <cstdint>

class Material
{
    public:
        // Hash of the material's "id" string from its JSON entry (see
        // MaterialLoader) - the same id always produces the same ID, the
        // same way Mesh::ID works. This is what Allocator checks to decide
        // whether a material is already registered.
        uint32_t ID = 0;

        // Empty when the material's JSON entry didn't include that path -
        // not every material has all three.
        std::optional<Texture> albedo;
        std::optional<Texture> normal;
        std::optional<Texture> orm;
};

#endif