#ifndef CHUNK_H
#define CHUNK_H

#include <cstdint>
#include <vector>

struct Voxel
{
    uint8_t voxelType;
    uint16_t ID;

    bool operator==(const Voxel& other) const
    {
        return voxelType == other.voxelType && ID == other.ID;
    }
};

class Chunk
{
    static constexpr uint32_t SIZE = 32;
    static constexpr uint32_t VOXEL_COUNT = SIZE * SIZE * SIZE;

    uint64_t coordinates;
    std::vector<Voxel> palette;
    std::vector<uint64_t> data;
    uint8_t bitsPerVoxel = 0;

    uint32_t GetIndex(uint32_t x, uint32_t y, uint32_t z) const;
    uint32_t GetPaletteIndex(uint32_t index) const;
    void SetPaletteIndex(uint32_t index, uint32_t paletteIndex);
    void Repack(uint8_t newBits);

public:
    explicit Chunk(uint64_t coords, Voxel initialVoxel);

    Voxel GetVoxel(uint32_t x, uint32_t y, uint32_t z) const;
    void SetVoxel(uint32_t x, uint32_t y, uint32_t z, Voxel voxel);
};

#endif