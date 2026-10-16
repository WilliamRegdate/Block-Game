#include <array>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

#include "RenderTypes.h"

namespace
{
    constexpr uint8_t FACE_CULL      = 1 << 0;
    constexpr uint8_t FACE_CULLABLE  = 1 << 1;
    constexpr uint8_t FACE_X_POS     = 1 << 2;
    constexpr uint8_t FACE_X_NEG     = 1 << 3;
    constexpr uint8_t FACE_Y_POS     = 1 << 4;
    constexpr uint8_t FACE_Y_NEG     = 1 << 5;
    constexpr uint8_t FACE_Z_NEG     = 1 << 6;
    constexpr uint8_t FACE_Z_POS     = 1 << 7;
    constexpr uint8_t FACE_DIR_MASK  = 0b11111100;
}
struct VoxelFace
{
    std::vector<ChunkVertex> vertices;
    std::vector<uint8_t> indices;
    uint8_t cullingFlags;
    bool FacingXPos() const { return (cullingFlags & FACE_DIR_MASK) == FACE_X_POS; }
    bool FacingXNeg() const { return (cullingFlags & FACE_DIR_MASK) == FACE_X_NEG; }
    bool FacingYPos() const { return (cullingFlags & FACE_DIR_MASK) == FACE_Y_POS; }
    bool FacingYNeg() const { return (cullingFlags & FACE_DIR_MASK) == FACE_Y_NEG; }
    bool FacingZPos() const { return (cullingFlags & FACE_DIR_MASK) == FACE_Z_POS; }
    bool FacingZNeg() const { return (cullingFlags & FACE_DIR_MASK) == FACE_Z_NEG; }
    bool IsCullable() const { return (cullingFlags & FACE_CULLABLE) != 0; }
    bool IsCullFace() const { return (cullingFlags & FACE_CULL) != 0; }
    
};
struct BaseVoxelVertex
{
    glm::u8vec3 position;
    uint8_t normal;
    uint8_t uv;
};
class VoxelType
{
    public:
        const VoxelFace& GetFace(glm::ivec3 direction) const;
        std::array<VoxelFace, 6> faces;
    private:
};