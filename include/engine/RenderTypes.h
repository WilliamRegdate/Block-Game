#ifndef RENDER_TYPES_H
#define RENDER_TYPES_H

#include<glad/glad.h>
#include<glm/glm.hpp>
#include<cstdint>


constexpr uint32_t HANDLE_INDEX_BITS   = 24;
constexpr uint32_t HANDLE_INDEX_MASK   = (1u << HANDLE_INDEX_BITS) - 1;
constexpr uint32_t INVALID_HANDLE_VALUE = 0xFFFFFFFFu;

// A contiguous range of elements inside one GPU buffer. The unit depends on
// which buffer it indexes: vertices for a VBO, indices for an EBO, slots
// for a descriptor/instance SSBO, mat4s for the pose buffer.
struct BufferRange
{
    uint32_t start = INVALID_HANDLE_VALUE;
    uint32_t size  = 0;

    bool IsValid() const { return start != INVALID_HANDLE_VALUE; }
};

// Everything needed to find and upload one entity's GPU data: which pool
// it's in (static or skinned), and its slot in that pool's InstanceData SSBO.
//
// NOTE: unlike the old packed-uint32 handle, there is no generation field
// here, so nothing currently guards against a stale copy of this handle
// (e.g. held elsewhere after Deallocate) being mistaken for valid. Lookups
// go through EntityManager (keyed by Entity*, not by this handle) so it
// isn't needed for that path. Add one back if you need to validate a raw
// handle on its own later - e.g. GPU picking, reading a slot index back from
// a buffer and turning it into a handle.
struct EntityHandle
{
    bool        isSkinned = false;
    BufferRange instanceRange; // in the static or skinned InstanceData SSBO; size is always 1

    bool IsValid() const { return instanceRange.IsValid(); }

    bool operator==(const EntityHandle& other) const
    {
        return isSkinned == other.isSkinned && instanceRange.start == other.instanceRange.start;
    }
    bool operator!=(const EntityHandle& other) const { return !(*this == other); }
};

// Everything needed to find and upload one mesh's GPU data: which pool it's
// in, and its ranges in that pool's VBO, EBO and MeshDescriptor SSBO.
// Same generation caveat as EntityHandle above.
struct MeshHandle
{
    bool        isSkinned = false;
    BufferRange vertexRange;     // in the static or skinned VBO, in vertices
    BufferRange indexRange;      // in the static or skinned EBO, in indices
    BufferRange descriptorRange; // in the static or skinned MeshDescriptor SSBO; size is always 1

    bool IsValid() const { return vertexRange.IsValid(); }

    bool operator==(const MeshHandle& other) const
    {
        return isSkinned == other.isSkinned && descriptorRange.start == other.descriptorRange.start;
    }
    bool operator!=(const MeshHandle& other) const { return !(*this == other); }
};


// ============================================================
// Chunk constants
// ============================================================
// Chunk geometry is built from quads drawn as GL_TRIANGLES: 4 vertices per
// quad, 6 indices per quad (0,1,2, 2,1,3 offset by 4 per quad).
constexpr uint32_t CHUNK_INDICES_PER_TRI = 3;

// Everything needed to find and upload one chunk's GPU data.
//
// Unlike MeshHandle, a chunk's mesh ranges are not fixed for its lifetime -
// they are re-reserved whenever the chunk is remeshed and its quad count
// changes. So the handle has two parts with different lifetimes:
//   - instanceRange: reserved when the chunk is registered, held until it's
//     removed. This is what IsValid() checks.
//   - vertexRange / indexRange: reserved by the latest mesh upload. Invalid
//     while the chunk has no mesh (not meshed yet, or an empty chunk).
//
// The chunk quad SSBO has no range of its own: it mirrors the chunk EBO's
// address space, so quad slot N belongs to indices [N*6, N*6+6).
// QuadStart() is that mapping. This only holds because every index
// allocation in the chunk EBO is a whole number of quads.
struct ChunkHandle
{
    BufferRange instanceRange; // in the chunk InstanceData SSBO; size is always 1
    BufferRange vertexRange;   // in the chunk VBO, in vertices (quadCount * 4)
    BufferRange indexRange;    // in the chunk EBO, in indices (quadCount * 6, start is a multiple of 6)

    bool IsValid() const { return instanceRange.IsValid(); }
    bool HasMesh() const { return indexRange.IsValid(); }

    // Only meaningful when HasMesh().
    uint32_t QuadStart() const { return indexRange.start / CHUNK_INDICES_PER_TRI; }
    uint32_t QuadCount() const { return indexRange.size  / CHUNK_INDICES_PER_TRI; }
};


// ============================================================
// Vertex formats
// ============================================================

struct StaticVertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct SkinnedVertex
{
    glm::vec3  position;
    glm::vec3  normal;
    glm::vec2  uv;
    glm::uvec4 boneIndices;
    glm::vec4  boneWeights;
};

// 8 bytes. Upload with glVertexAttribIPointer (integer attributes) -
// the normalized-float path would mangle the packed bit fields.
//
//   normalLight  bits 0-5 : normal, 2 bits per axis (0 -> 0, 1 -> +1, 2 -> -1),
//                           normalized in the shader (gives 45 degree slopes).
//                           An all-zero normal decodes to a zero vector - never emit it.
//                bits 6-7 : light strength, low 2 bits
//   uvLight      bits 0-5 : uv, 3 bits x then 3 bits y (0..7)
//                bit  6   : spare
//                bit  7   : light strength, high bit (strength is 3 bits total)
//   position     chunk-local, 0..255 per axis
//   color        vertex tint (light colour), rgb

struct ChunkVertex
{
    uint8_t normalLight; // bits 0-5: normal, bits 6-7: light bits 0-1
    uint8_t uvLight;     // bits 0-5: uv,     bit 6: light bit 3, bit 7: light bit 2
    glm::u8vec3 position;
    glm::u8vec3 color;   // vertex tint

    // Each axis is -1, 0 or +1. Encoded per axis as 0 -> 0, 1 -> +1, 2 -> -1,
    // 2 bits per axis (x in bits 0-1, y in 2-3, z in 4-5).
    void SetNormal(int x, int y, int z)
    {
        auto enc = [](int v) -> uint8_t { return v > 0 ? 1u : (v < 0 ? 2u : 0u); };
        const uint8_t n = enc(x) | (enc(y) << 2) | (enc(z) << 4);
        normalLight = (normalLight & 0xC0) | n;
    }
    void SetNormal(const glm::ivec3& n) { SetNormal(n.x, n.y, n.z); }

    // u and v are 0..7 (3 bits each); higher bits are masked off.
    void SetUV(uint8_t u, uint8_t v)
    {
        uvLight = (uvLight & 0xC0) | (u & 7u) | ((v & 7u) << 3);
    }

    // Light strength 0..15 (0 = no light, 15 = fully lit).
    //   bits 0-1 -> normalLight bits 6-7
    //   bit  2   -> uvLight bit 7
    //   bit  3   -> uvLight bit 6
    void SetLight(uint8_t light)
    {
        light &= 15u;
        normalLight = (normalLight & 0x3F) | ((light & 3u) << 6);
        uvLight     = (uvLight & 0x3F)
                    | (((light >> 2) & 1u) << 7)
                    | (((light >> 3) & 1u) << 6);
    }
};


// ============================================================
// GPU-resident lookup data
// ============================================================
// These mirror what the shaders read back via SSBO (std430).
// Watch alignment when editing - vec3 has a 16-byte base
// alignment in std430 same as vec4, even though sizeof(glm::vec3)
// is 12 on the CPU side, so a raw vec3 field can silently drift
// out of sync with the GLSL-side layout. Safer options: pad
// explicitly, or switch to vec4 / a bounding sphere (center+radius
// in one vec4) if you want a tightly packed struct.

struct AABB
{
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
};

struct MeshDescriptor
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int32_t  baseVertex = 0;
    uint32_t materialID = 0;
    uint32_t skeletonID = 0;   // only meaningful if isAnimated
    uint32_t isAnimated = 0;   // bool kept as uint32 for std430
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
};



constexpr uint32_t MATERIAL_HAS_ALBEDO = 1u << 0;
constexpr uint32_t MATERIAL_HAS_NORMAL = 1u << 1;
constexpr uint32_t MATERIAL_HAS_ORM    = 1u << 2;

struct MaterialDescriptor
{
    uint32_t  albedoTexIndex = 0; // layer in the material TextureArray - meaningless unless flags says it's present
    uint32_t  normalTexIndex = 0;
    uint32_t  ormTexIndex    = 0;
    uint32_t  flags          = 0; // MATERIAL_HAS_* bits - which maps are actually present
    glm::vec4 tint{ 1.0f };
};


struct AnimationDescriptor
{
    uint32_t poseStartIndex = 0; // first mat4 of this animation's baked poses, in bonePoseSSBO
    uint32_t frameCount     = 0; // sampled frames in this clip
    uint32_t boneCount      = 0; // bones per frame - poseStartIndex + frame*boneCount + bone indexes one matrix
    float    duration       = 0.0f; // seconds, matches BakedAnimationClip::duration
};

// Not included here - only a pointer to it is needed for TextureArray's
// declaration below, and Material.h doesn't need anything from RenderTypes.h.
class Material;

// Everything needed to find a material's GPU data: its three texture-array
// layers (any of which may be absent - IsValid() on the individual
// BufferRange tells you) and its slot in the MaterialDescriptor SSBO.
struct MaterialHandle
{
    BufferRange albedoLayer;     // in the material TextureArray; invalid if this material has no albedo map
    BufferRange normalLayer;     // in the material TextureArray; invalid if this material has no normal map
    BufferRange ormLayer;        // in the material TextureArray; invalid if this material has no orm map
    BufferRange descriptorRange; // in the MaterialDescriptor SSBO; size is always 1

    bool IsValid() const { return descriptorRange.IsValid(); }
};

// Everything needed to find an animation's GPU data: its range of baked
// pose matrices in bonePoseSSBO, and its slot in the AnimationDescriptor
// SSBO. Mirrors MeshHandle/MaterialHandle's shape.
struct AnimationHandle
{
    BufferRange poseRange;       // in bonePoseSSBO, counted in mat4s (frameCount * boneCount of them)
    BufferRange descriptorRange; // in the AnimationDescriptor SSBO; size is always 1

    bool IsValid() const { return descriptorRange.IsValid(); }
};


// One entry per live entity slot. Never reordered or compacted -
// this is the stable-slot buffer. Per-frame visibility is resolved
// through a separate index list (bound as a divisor-1 vertex
// attribute), not by moving this data around.
struct InstanceData
{
    glm::mat4 transform{ 1.0f };
    uint32_t  meshID      = 0;   // -> MeshDescriptor
    uint32_t  materialID  = 0;   // default from MeshDescriptor, may be overridden
    float  animTime  = 0;   // -> progress into animation
    uint32_t  animIndex = 0;   // -> AnimationDescriptor index
};

// One entry per live chunk slot, in the chunk instance SSBO. The shader finds
// its entry through gl_BaseInstance (the indirect command's baseInstance is
// the chunk's slot), then finds a quad's texture at
//     firstIndex / 6 + ((gl_VertexID - gl_BaseVertex) >> 2)
// in the chunk quad SSBO.
//
// CPU size is padded to 32 bytes on purpose: in std430 a vec3 forces 16-byte
// struct alignment, so the GLSL array stride is 32 even though the fields
// only add up to 24. Declare the GLSL struct as
//     vec3 position; uint firstIndex; uint indexCount; uint _pad;
// and the strides match.
struct ChunkInstanceData
{
    glm::vec3 position = glm::vec3(0);
    uint32_t firstIndex = 0;   // in indices, always a multiple of 6 (or 0 when indexCount is 0)
    uint32_t indexCount = 0;   // 0 means the chunk draws nothing
    uint32_t _pad[3]    = { 0, 0, 0 }; // pads to 32 bytes to match the std430 stride
};
static_assert(sizeof(ChunkInstanceData) == 32, "ChunkInstanceData must match the std430 array stride (32)");

// Matches OpenGL's DrawElementsIndirectCommand layout exactly -
// field order and sizes must not change.
struct DrawIndexedIndirectCommand
{
    uint32_t indexCount    = 0;
    uint32_t instanceCount = 0;
    uint32_t firstIndex    = 0;
    int32_t  baseVertex    = 0;
    uint32_t baseInstance  = 0;
};


// ============================================================
// Buffer objects
// ============================================================

// Common base for the wrappers below - owns the GL buffer ID and
// the sub-range upload path they all share. No Bind() here: each
// derived type binds to a different target with different
// semantics (VBO/EBO/IndirectBuffer bind to their own target on
// demand; SSBO binds to an indexed binding point instead).
class GPUBuffer
{
    public:
        GLuint ID = 0;

        GPUBuffer(GLsizeiptr capacityBytes, GLenum target, GLenum usage);

        // Writes into an existing sub-range - this is how a mesh's
        // geometry lands in the shared buffer at load time, and how
        // per-frame dynamic data gets updated if you're not using
        // persistent mapping.
        void Upload(GLintptr offsetBytes, const void* data, GLsizeiptr sizeBytes);

        void Delete();

    protected:
        GLenum m_target;
};

// Global vertex buffer - one for StaticVertex, one for
// SkinnedVertex. Allocated once at a fixed capacity; meshes are
// written into sub-ranges as they load, not given their own
// buffer each.
class VBO : public GPUBuffer
{
public:
    VBO(GLsizeiptr capacityBytes, GLenum usage = GL_STATIC_DRAW)
        : GPUBuffer(capacityBytes, GL_ARRAY_BUFFER, usage) {}

    void Bind();
    void Unbind();
};

// Global index buffer, same shape as VBO - kept as its own type
// so it can't accidentally be bound as a vertex source.
class EBO : public GPUBuffer
{
public:
    EBO(GLsizeiptr capacityBytes, GLenum usage = GL_STATIC_DRAW)
        : GPUBuffer(capacityBytes, GL_ELEMENT_ARRAY_BUFFER, usage) {}

    void Bind();
    void Unbind();
};

// Shader Storage Buffer - random-access data a shader indexes
// into directly rather than fetching via fixed-function vertex
// pulling. Used for the MeshDescriptor table, the Material table,
// the InstanceData buffer, and baked pose data.
class SSBO : public GPUBuffer
{
    public:
        SSBO(GLsizeiptr capacityBytes, GLenum usage = GL_DYNAMIC_DRAW)
            : GPUBuffer(capacityBytes, GL_SHADER_STORAGE_BUFFER, usage) {}

        // Binds to a specific GL_SHADER_STORAGE_BUFFER binding point,
        // matching the `layout(std430, binding = N)` in the shader.
        void BindBase(GLuint bindingIndex);
};

// Holds the DrawIndexedIndirectCommand array consumed by
// glMultiDrawElementsIndirect - one instance of this for the
// static pipeline, one for the animated pipeline. Rewritten each
// frame from the cull pass's per-mesh visible counts.
class IndirectBuffer : public GPUBuffer
{
    public:
        IndirectBuffer(GLsizeiptr capacityBytes, GLenum usage = GL_DYNAMIC_DRAW)
            : GPUBuffer(capacityBytes, GL_DRAW_INDIRECT_BUFFER, usage) {}
        void Bind();
        void Unbind();
};

// Wraps vertex attribute state for one vertex format (static or
// skinned). Bound once per pipeline per pass, not once per mesh.
class VAO
{
    public:
        GLuint ID = 0;

        VAO();

        // divisor = 0 -> per-vertex attribute (position, normal, uv...)
        // divisor = 1 -> per-instance attribute - this is where the
        //                culled visible-index buffer gets bound, giving
        //                each instance its real InstanceData slot number
        //                via automatic baseInstance offsetting.
        void LinkAttrib(GPUBuffer& buffer, GLuint layout, GLuint numComponents,
                        GLenum type, GLsizeiptr stride, void* offset,
                        GLuint divisor = 0);

        void Bind();
        void Unbind();
        void Delete();
};

// Holds every material's albedo/normal/orm textures as layers of one
// GL_TEXTURE_2D_ARRAY, so materials can be swapped by index at draw time
// instead of rebinding a different texture per material. Every layer
// shares width, height and internalFormat, fixed at construction - an
// image uploaded into a layer must already match those dimensions exactly.
//
// This class doesn't decide which layer a given texture goes into - same
// as VBO/EBO/SSBO, that's Allocator's job via a BufferTracker<TextureArray>
// and its emptySlots free list. UploadLayer/UploadMaterial just write
// pixels into whichever layer they're told to.
class TextureArray
{
    public:
        GLuint ID = 0;

        TextureArray(GLsizei width, GLsizei height, GLsizei layerCount,
                     GLenum internalFormat = GL_RGBA8);

        // Uploads one already-decoded image into one layer. pixelData must
        // already be width x height, matching this array's fixed size - no
        // resizing happens here.
        void UploadLayer(GLint layer, const void* pixelData,
                          GLenum format = GL_RGBA, GLenum type = GL_UNSIGNED_BYTE);

        // Loads material's up-to-three textures from disk (via
        // Texture::GetImage) and uploads each into the given layer -
        // -1 for any layer means "this material has no such map, skip it".
        void UploadMaterial(Material* material, GLint albedoLayer, GLint normalLayer, GLint ormLayer);

        void GenerateMipmaps();
        void Bind(GLuint unit);

        void Delete();

    private:
        GLsizei m_width      = 0;
        GLsizei m_height     = 0;
        GLsizei m_layerCount = 0;
};

#endif