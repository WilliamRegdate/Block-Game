#include "Chunk.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <array>
#include <utility>

uint32_t Chunk::GetIndex(uint32_t x, uint32_t y, uint32_t z) const
{
    assert(x < SIZE && y < SIZE && z < SIZE);
    return x + y * SIZE + z * SIZE * SIZE;
}

uint32_t Chunk::GetPaletteIndex(uint32_t index) const
{
    if (bitsPerVoxel == 0)
        return 0;

    uint32_t bitOffset = index * bitsPerVoxel;
    uint32_t wordIndex = bitOffset / 64;
    uint32_t bitShift = bitOffset % 64;

    uint64_t value = data[wordIndex] >> bitShift;

    if (bitShift + bitsPerVoxel > 64)
        value |= data[wordIndex + 1] << (64 - bitShift);

    return value & ((uint64_t{1} << bitsPerVoxel) - 1);
}

void Chunk::SetPaletteIndex(uint32_t index, uint32_t paletteIndex)
{
    uint32_t bitOffset = index * bitsPerVoxel;
    uint32_t wordIndex = bitOffset / 64;
    uint32_t bitShift = bitOffset % 64;

    uint64_t mask = (uint64_t{1} << bitsPerVoxel) - 1;

    data[wordIndex] &= ~(mask << bitShift);
    data[wordIndex] |= (uint64_t{paletteIndex} & mask) << bitShift;

    if (bitShift + bitsPerVoxel > 64)
    {
        uint32_t spillBits = bitShift + bitsPerVoxel - 64;

        data[wordIndex + 1] &= ~((uint64_t{1} << spillBits) - 1);
        data[wordIndex + 1] |= uint64_t{paletteIndex} >> (bitsPerVoxel - spillBits);
    }
}

void Chunk::Repack(uint8_t newBits)
{
    std::vector<uint64_t> oldData = std::move(data);
    uint8_t oldBits = bitsPerVoxel;

    data.assign((VOXEL_COUNT * newBits + 63) / 64, 0);
    bitsPerVoxel = newBits;

    if (oldBits == 0)
        return;

    for (uint32_t i = 0; i < VOXEL_COUNT; ++i)
    {
        uint32_t bitOffset = i * oldBits;
        uint32_t wordIndex = bitOffset / 64;
        uint32_t bitShift = bitOffset % 64;

        uint64_t value = oldData[wordIndex] >> bitShift;

        if (bitShift + oldBits > 64)
            value |= oldData[wordIndex + 1] << (64 - bitShift);

        SetPaletteIndex(i, value & ((uint64_t{1} << oldBits) - 1));
    }
}

Chunk::Chunk(Voxel initialVoxel)
    : palette{initialVoxel}
{
}

const Voxel& Chunk::GetVoxel(uint32_t x, uint32_t y, uint32_t z) const
{
    return palette[GetPaletteIndex(GetIndex(x, y, z))];
}

void Chunk::SetVoxel(uint32_t x, uint32_t y, uint32_t z, Voxel voxel)
{
    uint32_t index = GetIndex(x, y, z);

    uint32_t paletteIndex = 0;

    for (; paletteIndex < palette.size(); ++paletteIndex)
    {
        if (palette[paletteIndex] == voxel)
            break;
    }

    if (paletteIndex == palette.size())
    {
        palette.push_back(voxel);

        uint8_t newBits = 0;
        for (size_t n = palette.size() - 1; n > 0; n >>= 1)
            ++newBits;

        if (newBits != bitsPerVoxel)
            Repack(newBits);

        if (bitsPerVoxel == 0)
            return;
    }

    if (bitsPerVoxel != 0)
        SetPaletteIndex(index, paletteIndex);
}

namespace
{
    constexpr int ROTATION_COUNT = 12;

    // Facing direction for each rotation index
    constexpr glm::vec3 FORWARDS[ROTATION_COUNT] = {
        { 0, 0, 1}, { 1, 0, 0}, { 0, 0,-1}, {-1, 0, 0}, // 0-3
        { 0, 0, 1}, { 1, 0, 0}, { 0, 0,-1}, {-1, 0, 0}, // 4-7 (upside down)
        { 0, 0, 1}, { 1, 0, 0}, { 0, 0,-1}, {-1, 0, 0}, // 8-11 (rolled)
    };

    // Up direction for each rotation index
    constexpr glm::vec3 UPS[ROTATION_COUNT] = {
        { 0, 1, 0}, { 0, 1, 0}, { 0, 1, 0}, { 0, 1, 0},
        { 0,-1, 0}, { 0,-1, 0}, { 0,-1, 0}, { 0,-1, 0},
        {-1, 0, 0}, { 0, 0,-1}, { 1, 0, 0}, { 0, 0, 1},
    };

    std::array<glm::mat3, ROTATION_COUNT> BuildRotationMatrices()
    {
        std::array<glm::mat3, ROTATION_COUNT> m;
        for (int i = 0; i < ROTATION_COUNT; ++i)
        {
            const glm::vec3 right = glm::cross(UPS[i], FORWARDS[i]);
            // columns: X axis -> right, Y axis -> up, Z axis -> forward
            m[i] = glm::mat3(right, UPS[i], FORWARDS[i]);
        }
        return m;
    }

    // Built once during static initialisation
    const std::array<glm::mat3, ROTATION_COUNT> ROTATION_MATRICES = BuildRotationMatrices();
}

const glm::mat3& Voxel::GetRotationMat() const
{
    const uint8_t rotation = GetRotation();
    return ROTATION_MATRICES[rotation < ROTATION_COUNT ? rotation : 0];
}
