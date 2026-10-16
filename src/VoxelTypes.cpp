#include "VoxelTypes.h"

namespace
{
    // Maps a unit axis direction to its FACE_* flag, or 0 if it isn't one
    uint8_t DirectionToFlag(glm::ivec3 d)
    {
        if (d == glm::ivec3( 1, 0, 0)) return FACE_X_POS;
        if (d == glm::ivec3(-1, 0, 0)) return FACE_X_NEG;
        if (d == glm::ivec3( 0, 1, 0)) return FACE_Y_POS;
        if (d == glm::ivec3( 0,-1, 0)) return FACE_Y_NEG;
        if (d == glm::ivec3( 0, 0, 1)) return FACE_Z_POS;
        if (d == glm::ivec3( 0, 0,-1)) return FACE_Z_NEG;
        return 0;
    }
}


const VoxelFace &VoxelType::GetFace(glm::ivec3 direction) const
{
    // Zero-initialised: no flags set, so IsCullFace() is false
    static const VoxelFace NO_FACE{};

    const uint8_t flag = DirectionToFlag(direction);
    if (flag == 0)
        return NO_FACE;

    for (const auto& face : faces)
    {
        if ((face.cullingFlags & FACE_DIR_MASK) == flag)
            return face;
    }

    return NO_FACE; // this type has no face in that direction
}