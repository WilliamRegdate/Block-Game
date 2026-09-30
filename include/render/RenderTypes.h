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
    glm::vec4 boundsMin;
    glm::vec4 boundsMax;
};

struct MeshDescriptor
{
    uint32_t firstIndex;
    uint32_t indexCount;
    int32_t  baseVertex;
    uint32_t materialID;
    uint32_t skeletonID;
    uint32_t isAnimated;
    uint32_t _pad0;
    uint32_t _pad1;
    glm::vec4 boundsMin;   // xyz used
    glm::vec4 boundsMax;   // xyz used
};

// Renamed from `Material` (then briefly `MaterialGPU`) - that name belongs
// to the CPU-side asset class in Material.h, which owns actual Texture
// objects. This is only the flat GPU-mirror of one (indices into the
// material TextureArray, no ownership), so it gets a distinct name rather
// than colliding when both headers end up in the same translation unit (as
// they do via Mesh.h).


constexpr uint32_t MATERIAL_HAS_ALBEDO = 1u << 0;
constexpr uint32_t MATERIAL_HAS_NORMAL = 1u << 1;
constexpr uint32_t MATERIAL_HAS_ORM    = 1u << 2;

struct MaterialDescriptor
{
    uint32_t  albedoTexIndex = 0;
    uint32_t  normalTexIndex = 0;
    uint32_t  ormTexIndex    = 0;
    uint32_t  flags          = 0; // MATERIAL_HAS_* bits; a clear bit means "don't sample that map"
    glm::vec4 tint{ 1.0f };
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


// One entry per live entity slot. Never reordered or compacted -
// this is the stable-slot buffer. Per-frame visibility is resolved
// through a separate index list (bound as a divisor-1 vertex
// attribute), not by moving this data around.
struct InstanceData
{
    glm::mat4 transform{ 1.0f };
    uint32_t  meshID     = 0;   // -> MeshDescriptor
    uint32_t  materialID = 0;   // default from MeshDescriptor, may be overridden
    uint32_t  animOffset = 0;   // -> baked pose buffer; unused if mesh is static
    uint32_t  _pad0      = 0;
};

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