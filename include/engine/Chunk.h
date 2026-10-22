#ifndef CHUNK_H
#define CHUNK_H

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>


enum class VoxelLayer : uint8_t
{
    Air         = 0,
    Solid      = 1,
    Liquid      = 2,
    Transparent = 3,
};

//rotation order
//facing
//0: 0,0,1
//1: 1,0,0
//2: 0,0,-1
//3: -1,0,0
//4: 0,0,1
//5: 1,0,0
//6: 0,0,-1
//7: -1,0,0
//8: 0,0,1
//9: 1,0,0
//10: 0,0,-1
//11: -1,0,0

//up direction
//8: -1,0,0
//9: 0,0,-1
//10: 1,0,0
//11: 0,0,1

struct Voxel
{
    static constexpr uint8_t PRECISION = 4;
    // high nibble = type, low nibble = rotation
    uint8_t  voxelType = 0;
    uint8_t  voxelLayer = 0;
    uint16_t ID = 0;

    bool operator==(const Voxel& other) const
    {
        return voxelType == other.voxelType && ID == other.ID;
    }

    uint8_t GetType() const     { return voxelType >> 4; }
    uint8_t GetRotation() const { return voxelType & 0x0F; }

    // Returns a precomputed rotation matrix for this voxel's rotation index.
    const glm::mat3& GetRotationMat() const;
    VoxelLayer GetLayer() const { return static_cast<VoxelLayer>(voxelLayer); }
    bool IsAir() const          { return voxelLayer == 0; }

};

class Chunk
{
    
    std::vector<Voxel> palette;
    std::vector<uint64_t> data;
    uint8_t bitsPerVoxel = 0;
    
    uint32_t GetIndex(uint32_t x, uint32_t y, uint32_t z) const;
    uint32_t GetPaletteIndex(uint32_t index) const;
    void SetPaletteIndex(uint32_t index, uint32_t paletteIndex);
    void Repack(uint8_t newBits);
    
    public:
        //the position of this chunk - world space
        glm::ivec3 position;
        explicit Chunk(Voxel initialVoxel);
        
        const Voxel& GetVoxel(uint32_t x, uint32_t y, uint32_t z) const;
        void SetVoxel(uint32_t x, uint32_t y, uint32_t z, Voxel voxel);
        inline bool IsEmpty() const {return bitsPerVoxel == 0;}

        static constexpr uint32_t SIZE = 32;
        static constexpr uint32_t VOXEL_COUNT = SIZE * SIZE * SIZE;
};

#endif