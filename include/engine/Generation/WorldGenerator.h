#ifndef WORLDGENERATOR_H
#define WORLDGENERATOR_H

#include <cstdint>
#include <FastNoiseLite/FastNoiseLite.h>
#include "Chunk.h"
#include "ChunkNeighbourhood.h"

class WorldGenerator
{
    enum class Biome : uint8_t
    {
        ColdOcean,
        SnowyMountains,
        Glacier,
        Taiga,
        Tundra,
        Ocean,
        Mountains,
        Swamp,
        Forest,
        Plains,
        WarmOcean,
        Mesa,
        Savanna,
        Jungle,
        Desert,
        Void,
        VoidShore,
        Crags,
        AcidicMarsh,
        Wasteland,
        RedWastes,
        Count
    };

public:
    WorldGenerator(int64_t seed = 1456876);
    Voxel GetBlockAt(int x, int y, int z);
    Voxel GetBlockAt(glm::vec3 position);

    const Voxel air = {0, 0, 0};
    const Voxel stone = {0, 1, 1};
    const Voxel log = {0, 1, 2};
    const Voxel mud = {0, 1, 3};
    const Voxel water = {0, 2, 4};
    const Voxel snow = {0, 1, 5};
    const Voxel sand = {0, 1, 6};
    const Voxel grass = {0, 1, 7};
    const Voxel redSand = {0, 1, 8};
    const Voxel cobblestone = {0, 1, 9};

    int32_t seed = 1456876;

private:
    FastNoiseLite continentalness;
    FastNoiseLite wetness;
    FastNoiseLite temperature;
    FastNoiseLite erosion;
    FastNoiseLite highNoise;
    FastNoiseLite height;

    static constexpr float SEA_LEVEL = -0.45f;
    static constexpr float WATER_LEVEL = 62.0f;   // water fills y <= WATER_LEVEL
    static constexpr float COLD_START = -0.7f;
    static constexpr float HOT_START = 0.7f;

    ChunkNeighbourhood BuildNeighbourhood(const glm::ivec3& c);

    // Terrain shape for a column. Returns continentalness; fills base height and erosion.
    float ColumnShape(int x, int z, float& baseHeight, float& erosionValue) const;
    // Full (unclamped) density at a block: > 0 is solid
    float Density(int x, int y, int z) const;
    // Surface slope as rise per block of run (1.0 = 45 degrees)
    float Slope(int x, int y, int z, float erosionValue) const;

    Biome GetBiome(int x, int z, float c, float wetness, float temperature);
    Biome ColdBiome(float c, float wetness);
    Biome TemperateBiome(float c, float wetness);
    Biome HotBiome(float c, float wetness);
    Biome CorruptedBiome(float c, float wetness);
};

#endif