#pragma once
#include "Generation/WorldNoise.h"
#include "Generation/Spline.h"

// What a shape computes once per (x,z) column.
struct ColumnInfo { float baseHeight; float erosion; };

class TerrainShape
{
public:
    
protected:
    const WorldNoise& noise;
};