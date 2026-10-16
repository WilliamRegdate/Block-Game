#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace ChunkCoord
{
    constexpr int64_t  BIAS = 1 << 20;
    constexpr uint64_t MASK = (1ull << 21) - 1;

    inline uint64_t ToID(const glm::ivec3& c)
    {
        const uint64_t x = static_cast<uint64_t>(static_cast<int64_t>(c.x) + BIAS) & MASK;
        const uint64_t y = static_cast<uint64_t>(static_cast<int64_t>(c.y) + BIAS) & MASK;
        const uint64_t z = static_cast<uint64_t>(static_cast<int64_t>(c.z) + BIAS) & MASK;
        return x | (y << 21) | (z << 42);
    }

    inline glm::ivec3 FromID(uint64_t id)
    {
        return glm::ivec3(
            static_cast<int>(static_cast<int64_t>( id        & MASK) - BIAS),
            static_cast<int>(static_cast<int64_t>((id >> 21) & MASK) - BIAS),
            static_cast<int>(static_cast<int64_t>((id >> 42) & MASK) - BIAS));
    }
}