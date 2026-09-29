#include "Mesh.h"

#include <utility>

Mesh::Mesh(std::vector<uint32_t> indices)
    : indices(std::move(indices))
{
}

StaticMesh::StaticMesh(std::vector<StaticVertex> vertices, std::vector<uint32_t> indices)
    : Mesh(std::move(indices)), vertices(std::move(vertices))
{
    isSkinned = false;
}

SkinnedMesh::SkinnedMesh(std::vector<SkinnedVertex> vertices, std::vector<uint32_t> indices)
    : Mesh(std::move(indices)), vertices(std::move(vertices))
{
    isSkinned = true;
}