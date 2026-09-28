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

        Transform transform;

        std::vector <StaticVertex> vertices;
        std::vector <uint32_t> indices;
        std::vector <Texture> textures;

};
class SkinnedMesh
{
    
    public:
        uint32_t ID;

        Transform transform;

        std::vector <SkinnedVertex> vertices;
        std::vector <GLuint> indices;
        std::vector <Texture> textures;
};

#endif