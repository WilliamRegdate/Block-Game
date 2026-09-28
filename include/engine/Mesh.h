#ifndef MESH_H
#define MESH_H

#include "Transform.h"
#include "Material.h"

#include "RenderTypes.h"
#include <vector>

class Mesh
{

    public:
        uint32_t ID;
        bool isSkinned;

        Transform transform;
        std::vector <uint32_t> indices;
        std::vector <Texture> textures;

};

// public: Allocator casts a Mesh& to SkinnedMesh&/StaticMesh& depending on
// isSkinned, which needs Mesh to be an accessible base from outside these
// classes. Private (the default for `class X : Y`) blocks that cast.
class SkinnedMesh : public Mesh
{
    public:
        std::vector <SkinnedVertex> vertices;
};
class StaticMesh : public Mesh
{
    public:
        std::vector <StaticVertex> vertices;
};


#endif