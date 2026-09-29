#ifndef MESH_H
#define MESH_H

#include "Transform.h"
#include "Material.h"

#include "RenderTypes.h"
#include <vector>

class Mesh
{

    public:
        Mesh(std::vector<uint32_t> indices);   

        uint32_t ID;
        bool isSkinned;

        Transform transform;
        std::vector <uint32_t> indices;
        Material* material;
};

class SkinnedMesh : public Mesh
{
    public:
        SkinnedMesh(std::vector<SkinnedVertex> vertices, std::vector<uint32_t> indices);
        std::vector<SkinnedVertex> vertices;
};
class StaticMesh : public Mesh
{
    public:
        StaticMesh(std::vector<StaticVertex> vertices, std::vector<uint32_t> indices);
        std::vector<StaticVertex> vertices;
};


#endif