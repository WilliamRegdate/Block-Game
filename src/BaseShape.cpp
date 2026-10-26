// TerrainShapes.cpp
#include "Generation/BaseShape.h"
#include <algorithm>

namespace { constexpr float kNoiseReach = 1.0f; }

ColumnInfo RollingShape::Column(int x, int z, const Climate& climate) const
{
    const float fx = float(x), fz = float(z);
    ColumnInfo col;
    col.erosion    = SampleSpline(*params.erosionSpline, noise.erosion.GetNoise(fx, fz));
    col.baseHeight = SampleSpline(*params.heightSpline, climate.continentalness)
                   + noise.height.GetNoise(fx, fz) * params.heightNoiseAmp;

    const float stepAmount = std::clamp((params.stepThreshold - col.erosion) / params.stepBlend, 0.0f, 1.0f);
    if (stepAmount > 0.0f)
    {
        const float stepped = std::floor(col.baseHeight / params.stepHeight) * params.stepHeight;
        col.baseHeight = Lerp(col.baseHeight, stepped, stepAmount);
    }
    return col;
}

float RollingShape::Density(const ColumnInfo& col, int x, int y, int z) const
{
    const float bias = (float(y) - col.baseHeight) / col.erosion;
    return noise.highNoise.GetNoise(float(x), float(y) * 0.8f, float(z)) - bias;
}

bool RollingShape::Solid(const ColumnInfo& col, int x, int y, int z) const
{
    const float bias = (float(y) - col.baseHeight) / col.erosion;
    if (bias >  kNoiseReach) return false;     // noise can't change the result
    if (bias < -kNoiseReach) return true;
    return noise.highNoise.GetNoise(float(x), float(y) * 0.8f, float(z)) - bias > 0.0f;
}