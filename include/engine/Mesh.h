#ifndef MESH_H
#define MESH_H

#include "Transform.h"
#include "Material.h"
#include "Skeleton.h"
#include "Animation.h"

#include "RenderTypes.h"
#include <vector>
#include <list>
#include <cstdint>

class Mesh
{
    public:
        virtual ~Mesh() = default;

        uint32_t ID = 0;
        bool isSkinned = false;

        Transform transform;
        std::vector<uint32_t> indices;

    protected:

        Mesh(std::vector<uint32_t> indices);
};

// public: Allocator casts a Mesh& to SkinnedMesh&/StaticMesh& depending on
// isSkinned, which needs Mesh to be an accessible base from outside these
// classes. Private (the default for `class X : Y`) blocks that cast.
class SkinnedMesh : public Mesh
{
    public:
        SkinnedMesh(std::vector<SkinnedVertex> vertices, std::vector<uint32_t> indices);
        std::vector<SkinnedVertex> vertices;

        std::list<BakedAnimationClip> animations;
        
};
class StaticMesh : public Mesh
{
    public:
        StaticMesh(std::vector<StaticVertex> vertices, std::vector<uint32_t> indices);
        std::vector<StaticVertex> vertices;
};


#endif