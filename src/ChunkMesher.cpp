#include "ChunkMesher.h"
#include "ChunkNeighbourhood.h"

namespace
{
    // Brightness for AO level 0 (darkest) .. 3 (open). Tune by eye.
    constexpr float AO_CURVE[4] = { 0.45f, 0.65f, 0.85f, 1.0f };

    int VertexAO(bool side1, bool side2, bool corner)
    {
        if (side1 && side2)
            return 0; // corner block is hidden behind the two sides
        return 3 - (int(side1) + int(side2) + int(corner));
    }

    // Anything non-air occludes for now. Later: ask the voxel type (glass/leaves shouldn't).
    // Accepts -1..32 on each axis; missing neighbour chunks read as air.
    bool IsOccluder(const ChunkNeighbourhood& nb, const glm::ivec3& p)
    {
        return !nb.GetVoxel(p.x, p.y, p.z).IsAir();
    }

    // The face's direction in the voxel's own (unrotated) space.
    glm::ivec3 LocalFaceDir(const VoxelFace& face)
    {
        if (face.FacingXPos()) return {  1, 0, 0 };
        if (face.FacingXNeg()) return { -1, 0, 0 };
        if (face.FacingYPos()) return { 0,  1, 0 };
        if (face.FacingYNeg()) return { 0, -1, 0 };
        if (face.FacingZPos()) return { 0, 0,  1 };
        if (face.FacingZNeg()) return { 0, 0, -1 };
        return { 0, 0, 0 };
    }
}

ChunkMesher::ChunkMesher(ResourceController& controller)
    : resourceController(controller)
{
    GetBlockTypes();
}

void ChunkMesher::MeshChunk(const ChunkNeighbourhood& nb,
                            std::vector<ChunkVertex>& vertices,
                            std::vector<uint32_t>& indices,
                            std::vector<uint32_t>& triangleTextures)
{
    const Chunk& center = nb.Center();

    // Empty chunk: nothing to mesh (big win when the player is on the surface).
    if (center.IsEmpty())
        return;

    vertices.reserve(8192);
    indices.reserve(8192);
    triangleTextures.reserve(4096);

    for (int x = 0; x < 32; x++)
    for (int y = 0; y < 32; y++)
    for (int z = 0; z < 32; z++)
        GetVoxelMesh(nb, center.GetVoxel(x, y, z), glm::uvec3(x, y, z),
                     vertices, indices, triangleTextures);
}

void ChunkMesher::GetVoxelMesh(const ChunkNeighbourhood& nb, const Voxel& voxel, glm::uvec3 position,
                               std::vector<ChunkVertex>& vertices,
                               std::vector<uint32_t>& indices,
                               std::vector<uint32_t>& triangleTextures)
{
    if (voxel.IsAir())
        return;

    const VoxelType& type = GetVoxelType(voxel);
    const glm::mat3& rot = voxel.GetRotationMat();

    const glm::vec3 voxelCenter = glm::vec3(Voxel::PRECISION >> 1);

    auto toWorld = [&](const glm::ivec3& d)
    {
        return glm::ivec3(glm::round(rot * glm::vec3(d)));
    };

    for (const auto& face : type.faces)
    {
        if (IsSideOccluded(nb, voxel, face, position))
            continue;

        const uint32_t base    = static_cast<uint32_t>(vertices.size());
        const uint32_t texture = GetFaceTexture(voxel, face);

        const glm::ivec3 localN = LocalFaceDir(face);
        const glm::ivec3 worldN = toWorld(localN);   // rotations are 90-degree steps: exact axis

        int aoLevel[4] = { 3, 3, 3, 3 };

        for (size_t i = 0; i < face.vertices.size(); ++i)
        {
            const ChunkVertex& v = face.vertices[i];
            ChunkVertex out = v;

            // Round before converting, so 3.9999 doesn't truncate to 3 and open gaps.
            out.position = glm::u8vec3(glm::round(rot * (glm::vec3(v.position) - voxelCenter) + voxelCenter));
            out.position += position * glm::uint(Voxel::PRECISION);
            out.SetNormal(worldN);
            out.SetLight(15);

            // ---- AO ----
            // Which way does this vertex sit along the two axes the face spans?
            const glm::vec3 rel = glm::vec3(v.position) - voxelCenter;
            glm::ivec3 tA(0), tB(0);
            int found = 0;
            for (int a = 0; a < 3; ++a)
            {
                if (localN[a] != 0) continue;
                (found == 0 ? tA : tB)[a] = rel[a] > 0.0f ? 1 : -1;
                ++found;
            }

            // The three blocks in the layer in front of the face that touch this corner.
            const glm::ivec3 origin = glm::ivec3(position);
            const bool s1 = IsOccluder(nb, origin + toWorld(localN + tA));
            const bool s2 = IsOccluder(nb, origin + toWorld(localN + tB));
            const bool c  = IsOccluder(nb, origin + toWorld(localN + tA + tB));

            aoLevel[i] = VertexAO(s1, s2, c);
            const uint8_t shade = static_cast<uint8_t>(255.0f * AO_CURVE[aoLevel[i]]);
            out.color = glm::u8vec3(shade); // multiply into the base tint here if you add biome tints

            vertices.push_back(out);
        }

        // Split along the diagonal that avoids interpolating across the biggest AO difference.
        const bool flip = face.vertices.size() == 4 && face.indices.size() == 6
                       && (aoLevel[0] + aoLevel[2] < aoLevel[1] + aoLevel[3]);

        if (flip)
        {
            constexpr uint8_t FLIPPED[6] = { 1, 2, 3, 1, 3, 0 };
            for (uint8_t idx : FLIPPED)
                indices.push_back(base + idx);
        }
        else
        {
            for (uint8_t idx : face.indices)
                indices.push_back(base + idx);
        }

        // one texture entry per triangle
        triangleTextures.insert(triangleTextures.end(), face.indices.size() / 3, texture);
    }

    // light goes here once implemented
}

uint32_t ChunkMesher::GetFaceTexture(const Voxel& voxel, const VoxelFace& face)
{
    // Read-only lookup: workers must never trigger a load (cache write = data race).
    // All block types are preloaded on the main thread before meshing starts.
    const BlockType* type = resourceController.GetOrLoadBlockType(voxel.ID);
    if (type == nullptr)
        return UINT32_MAX; // TODO: return a reserved "missing texture" layer instead

    switch (face.cullingFlags & FACE_DIR_MASK)
    {
        case FACE_X_POS: return type->tex_x_plus;
        case FACE_X_NEG: return type->tex_x_minus;
        case FACE_Y_POS: return type->tex_y_plus;
        case FACE_Y_NEG: return type->tex_y_minus;
        case FACE_Z_POS: return type->tex_z_plus;
        case FACE_Z_NEG: return type->tex_z_minus;
    }
    return UINT32_MAX;
}

bool ChunkMesher::IsSideOccluded(const ChunkNeighbourhood& nb, const Voxel& voxel,
                                 const VoxelFace& face, glm::ivec3 position)
{
    if (!face.IsCullable())
        return false;

    const glm::ivec3 localDir = LocalFaceDir(face);
    if (localDir == glm::ivec3(0))
        return false;

    const glm::ivec3 offset = glm::ivec3(glm::round(voxel.GetRotationMat() * glm::vec3(localDir)));

    // -1..32 is valid: out-of-chunk lookups are routed to the neighbouring chunk.
    const glm::ivec3 n = position + offset;
    const Voxel& neighbour = nb.GetVoxel(n.x, n.y, n.z);

    if (neighbour.GetLayer() != voxel.GetLayer())
        return false;

    const glm::ivec3 neighbourLocalDir =
        glm::ivec3(glm::round(glm::vec3(-offset) * neighbour.GetRotationMat()));

    return GetVoxelType(neighbour).GetFace(neighbourLocalDir).IsCullFace();
}

const VoxelType& ChunkMesher::GetVoxelType(const Voxel& voxel)
{
    // TODO: needs to return the correct type instead of cube
    return cube;
}

namespace
{
    // Normal is NOT set here: it depends on the voxel's rotation, so GetVoxelMesh sets it per voxel.
    ChunkVertex MakeVertex(uint8_t x, uint8_t y, uint8_t z, uint8_t u, uint8_t v)
    {
        ChunkVertex out{};
        out.position = glm::u8vec3(x, y, z);
        out.SetUV(u, v);
        out.color = glm::u8vec3(255, 255, 255);   // no tint
        return out;
    }

    VoxelFace MakeQuad(const glm::u8vec3 c[4], uint8_t flags)
    {
        constexpr uint8_t U[4] = { 0, 1, 1, 0 };
        constexpr uint8_t V[4] = { 0, 0, 1, 1 };

        VoxelFace face;
        face.vertices.reserve(4);
        for (int i = 0; i < 4; ++i)
            face.vertices.push_back(MakeVertex(c[i].x, c[i].y, c[i].z, U[i], V[i]));

        face.indices      = { 0, 1, 2, 0, 2, 3 };
        face.cullingFlags = flags;
        return face;
    }
}

void ChunkMesher::GetBlockTypes()
{
    // BUILD CUBE
    constexpr uint8_t N = Voxel::PRECISION;                // far corner of the block
    constexpr uint8_t common = FACE_CULLABLE | FACE_CULL;  // hides neighbours and can be hidden

    // X+ : u along -Z, v along +Y
    const glm::u8vec3 xPos[4] = { {N,0,N}, {N,0,0}, {N,N,0}, {N,N,N} };
    cube.faces[0] = MakeQuad(xPos, FACE_X_POS | common);

    // X- : u along +Z, v along +Y
    const glm::u8vec3 xNeg[4] = { {0,0,0}, {0,0,N}, {0,N,N}, {0,N,0} };
    cube.faces[1] = MakeQuad(xNeg, FACE_X_NEG | common);

    // Y+
    const glm::u8vec3 yPos[4] = { {0,N,0}, {0,N,N}, {N,N,N}, {N,N,0} };
    cube.faces[2] = MakeQuad(yPos, FACE_Y_POS | common);

    // Y-
    const glm::u8vec3 yNeg[4] = { {0,0,N}, {0,0,0}, {N,0,0}, {N,0,N} };
    cube.faces[3] = MakeQuad(yNeg, FACE_Y_NEG | common);

    // Z-
    const glm::u8vec3 zNeg[4] = { {N,0,0}, {0,0,0}, {0,N,0}, {N,N,0} };
    cube.faces[4] = MakeQuad(zNeg, FACE_Z_NEG | common);

    // Z+
    const glm::u8vec3 zPos[4] = { {0,0,N}, {N,0,N}, {N,N,N}, {0,N,N} };
    cube.faces[5] = MakeQuad(zPos, FACE_Z_POS | common);
}