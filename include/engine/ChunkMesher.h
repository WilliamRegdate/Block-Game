#ifndef CHUNKMESHER_H
#define CHUNKMESHER_H

#include <vector>
#include "VoxelTypes.h"
#include "RenderTypes.h"
#include "Chunk.h"
#include "ResourceController.h"
#include "ChunkNeighbourhood.h"

class ChunkMesher
{
    public:
        ChunkMesher(ResourceController& controller);
        //turns a chunk into a list of renderable vertices and indices
        void MeshChunk(const ChunkNeighbourhood& nb, std::vector<ChunkVertex>& vertices, std::vector<uint32_t>& indices, std::vector<uint32_t>& triangleTextures);
    private:
        //gets gets the mesh for all visible faces in this voxel. (indices are relative to the vertex index.)
        // it is permitted for vertices, indices and triangleTextures to have values in them already
        
        void GetVoxelMesh(const ChunkNeighbourhood& nb, const Voxel& voxel, glm::uvec3 position,
                        std::vector<ChunkVertex>& vertices, std::vector<uint32_t>& indices,
                        std::vector<uint32_t>& triangleTextures);

        bool IsSideOccluded(const ChunkNeighbourhood& nb, const Voxel& voxel,
                            const VoxelFace& face, glm::ivec3 position);

        uint32_t GetFaceTexture(const Voxel& voxel, const VoxelFace& face);

        //returns the voxels type
        const VoxelType& GetVoxelType(const Voxel&);

        //TODO add the rest of the block types
        void GetBlockTypes();
        VoxelType cube;

        ResourceController& resourceController;

        

};


#endif