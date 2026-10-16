// ChunkNeighbourhood.h
#pragma once
#include <array>
#include <memory>
#include "Chunk.h"

class ChunkNeighbourhood
{
public:
    // Centre chunk is slot 13. Slot = (dx+1) + 3*(dy+1) + 9*(dz+1).
    std::array<std::shared_ptr<const Chunk>, 27> chunks;

    const Chunk& Center() const { return *chunks[13]; }

    // Accepts -1..SIZE (one chunk out on any axis). Missing neighbour -> air.
    const Voxel& GetVoxel(int x, int y, int z) const
    {
        constexpr int S = static_cast<int>(Chunk::SIZE);

        // Fast path: inside the centre chunk, which is nearly every call.
        if (static_cast<unsigned>(x) < S && static_cast<unsigned>(y) < S && static_cast<unsigned>(z) < S)
            return chunks[13]->GetVoxel(x, y, z);

        const int cx = (x < 0) ? 0 : (x >= S ? 2 : 1);
        const int cy = (y < 0) ? 0 : (y >= S ? 2 : 1);
        const int cz = (z < 0) ? 0 : (z >= S ? 2 : 1);

        const Chunk* c = chunks[cx + 3 * cy + 9 * cz].get();
        if (c == nullptr)
            return AirVoxel();

        // Wrap into the neighbour's local range.
        return c->GetVoxel((x + S) % S, (y + S) % S, (z + S) % S);
    }

    static const Voxel& AirVoxel()
    {
        static const Voxel air{};   // default Voxel is air (your Chunk(Voxel()) init relies on this)
        return air;
    }
};