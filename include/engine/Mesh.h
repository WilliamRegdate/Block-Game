#ifndef MESH_H
#define MESH_H

#include "Transform.h"
#include "Material.h"
#include "Skeleton.h"

#include "RenderTypes.h"
#include <vector>
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

class SkinnedMesh : public Mesh
{
    public:
        SkinnedMesh(std::vector<SkinnedVertex> vertices, std::vector<uint32_t> indices);
        std::vector<SkinnedVertex> vertices;
        Skeleton skeleton;
        
};
class StaticMesh : public Mesh
{
    public:
        StaticMesh(std::vector<StaticVertex> vertices, std::vector<uint32_t> indices);
        std::vector<StaticVertex> vertices;
};


#endif