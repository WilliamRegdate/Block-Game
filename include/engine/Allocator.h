#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <vector>
#include <utility>
#include "Entity.h"
#include "RenderTypes.h"
#include "EntityManager.h"
#include "Animation.h"

struct EntityRendererConfig
{
    GLsizeiptr staticVertexCapacityBytes  = 0;
    GLsizeiptr staticIndexCapacityBytes   = 0;
    GLsizeiptr skinnedVertexCapacityBytes = 0;
    GLsizeiptr skinnedIndexCapacityBytes  = 0;
    GLsizeiptr poseBufferCapacityBytes    = 0;

    // ---- chunks ----
    GLsizeiptr chunkVertexCapacityBytes = 0;
    // The chunk quad SSBO is sized from this: one uint32 per 6 indices.
    // The usable index count is rounded down to a whole number of quads.
    GLsizeiptr chunkIndexCapacityBytes  = 0;
    GLsizeiptr chunkCapacity = 0; // max live chunks (instance slots)

    uint32_t maxEntities  = 0;
    uint32_t maxMeshTypes = 0;
    uint32_t maxMaterials = 0;


    GLsizei materialTextureWidth  = 0;
    GLsizei materialTextureHeight = 0;
    GLenum  materialTextureFormat = GL_RGBA8;

    uint32_t maxChunkMaterials = 0;
    GLsizei  chunkMaterialTextureWidth  = 16;
    GLsizei  chunkMaterialTextureHeight = 16;
    GLenum   chunkMaterialTextureFormat = GL_RGBA8;

    uint32_t maxAnimations = 0;


};

struct emptySlot
{
    uint32_t start;
    uint32_t size;
};
template<typename BufferT>
struct BufferTracker
{
    BufferT buffer;
    std::vector<emptySlot> emptySlots;

    // Forwards whatever arguments BufferT's own constructor needs.
    // Unchanged behaviour for VBO/EBO/SSBO/IndirectBuffer (a single
    // GLsizeiptr); also works for TextureArray, whose constructor takes
    // width/height/layerCount/format instead.
    template<typename... Args>
    BufferTracker(Args&&... args) : buffer(std::forward<Args>(args)...) {}
};

class Allocator
{
    public:

        // entityManager is not owned. It must outlive the Allocator.
        Allocator(EntityRendererConfig config, EntityManager* entityManager);
        // Deletes every GL buffer/texture the allocator created. Safe to call twice.
        void Shutdown();

        // Reserves buffer space for an entity, its mesh, material and animation,
        // only allocates space if they dont already exist on the GPU
        EntityTracker Allocate(Entity& entity);

        // Releases the entity's space, and its mesh's/material's space if
        // this was the last entity using either.
        void Deallocate(Entity& entity);

        // Writes data into space that Allocate already reserved.
        bool UploadEntityData(Entity& entity);
        bool UploadMeshData(Mesh& mesh);
        bool UploadMaterialData(Material& material);
        bool UploadAnimationData(BakedAnimationClip& clip); //uploads a single animation

        // ---- Chunks ----
        // Chunks follow the same allocate / upload / deallocate shape as
        // entities, but split differently because a chunk's mesh changes size
        // every time it is remeshed:
        //   AllocateChunk      - registers the chunk and reserves its instance slot only.
        //   UploadChunkMesh    - (re)reserves vertex/index space to fit the new quad count
        //                        and writes vertices, indices, per-quad textures and the
        //                        chunk's instance data. Call again on every remesh.
        //   DeallocateChunk    - frees the instance slot and any mesh space.
        //
        // TODO: CHUNK - the Chunk class doesn't exist yet, so these take plain
        // data. Add overloads taking Chunk& (key from chunk.ID, position from
        // the chunk's world position, mesh data from its mesher output) once it does.

        // Returns a tracker with an invalid handle on failure (no free instance
        // slot, or this chunkID is already registered).
        ChunkTracker AllocateChunk(uint64_t chunkID, const glm::vec3& worldPosition);
        void DeallocateChunk(uint64_t chunkID);

        // vertices.size() must equal quadTextures.size() * 4: every quad owns 4
        // consecutive vertices, in quad order. quadTextures holds one value per
        // quad (see the chunk-material TODO in Allocator.cpp for what it means).
        // An empty mesh is valid and frees the chunk's mesh space.
        // On failure (no space) the chunk keeps whatever mesh it had before.
        bool UploadChunkMesh(uint64_t chunkID,
                                const glm::vec3& worldPosition,
                                const std::vector<ChunkVertex>& vertices,
                                const std::vector<uint32_t>& indices,
                                const std::vector<uint32_t>& triangleTextures);

        // Rewrites the chunk's ChunkInstanceData entry from its tracker.
        bool UploadChunkInstanceData(const glm::ivec3& chunkPosition, uint64_t chunkID);

        // Fills the indirect command that draws this chunk. Returns false if the
        // chunk is unknown or has no mesh (nothing to draw).
        // baseInstance is the chunk's instance slot - the shader reads gl_BaseInstance.
        bool BuildChunkDrawCommand(uint64_t chunkID, DrawIndexedIndirectCommand& out);

        // public buffer object accessors:
        VBO&          StaticVBO()               { return staticVBO.buffer; }
        EBO&          StaticEBO()               { return staticEBO.buffer; }
        SSBO&         StaticInstanceSSBO()      { return staticInstanceDataSSBO.buffer; }

        VBO&          AnimatedVBO()             { return skinnedVBO.buffer; }
        EBO&          AnimatedEBO()             { return skinnedEBO.buffer; }
        SSBO&         AnimatedInstanceSSBO()    { return skinnedInstanceDataSSBO.buffer; }

        SSBO&         MaterialDescriptorSSBO()  { return materialDescriptorSSBO.buffer; }
        TextureArray& MaterialTextures()        { return materialTextureArray.buffer; }
        SSBO&         BonePoseSSBO()            { return bonePoseSSBO.buffer; }
        SSBO&         AnimationDescriptorSSBO() { return animationDescriptorSSBO.buffer; }

        //chunk buffers
        VBO&          ChunkVBO()                { return chunkVBO.buffer; }
        EBO&          ChunkEBO()                { return chunkEBO.buffer; }
        SSBO&         ChunkInstanceSSBO()       { return chunkInstanceDataSSBO.buffer; }
        SSBO&         ChunkTriTextureSSBO()     { return chunkTriTextureSSBO.buffer; }
        TextureArray& ChunkTextureArray()       { return chunkTextureArray.buffer;}


        bool BuildInstanceData(Entity& entity, InstanceData& out, EntityHandle& handle);
        bool BuildChunkInstanceData(const glm::ivec3& chunkPosition, uint64_t chunkID, ChunkInstanceData& out, ChunkHandle& handle);

        // Writes `count` consecutive InstanceData starting at `firstSlot` in one call.
        void UploadInstanceRange(bool skinned, uint32_t firstSlot, const InstanceData* data, uint32_t count);
        
        static constexpr uint32_t INVALID_CHUNK_TEXTURE = 0xFFFFFFFFu;
        void GenerateMaterialMipmaps();
        void GenerateChunkTextureMipmaps();
        uint32_t AddChunkTexture(Texture& texture);

    private:
        EntityManager* entityManager; // not owned, declared first: constructed first

        BufferTracker<VBO> staticVBO; // stores vertices for static meshes
        BufferTracker<EBO> staticEBO; // stores indices for static meshes

        BufferTracker<SSBO> staticMeshDescriptorSSBO; // stores information on how to render all static meshes
        BufferTracker<SSBO> staticInstanceDataSSBO;   // stores information about each static render instance


        BufferTracker<VBO> skinnedVBO; // stores vertices for animated meshes
        BufferTracker<EBO> skinnedEBO; // stores indices for animated meshes

        BufferTracker<SSBO> skinnedMeshDescriptorSSBO; // stores information on how to render all animated meshes
        BufferTracker<SSBO> skinnedInstanceDataSSBO;   // stores information about each animated render instance
        BufferTracker<SSBO> bonePoseSSBO;              // stores baked skinning matrices for every registered animation

        BufferTracker<SSBO> materialDescriptorSSBO;       // stores texture-array layer indices for every material
        BufferTracker<TextureArray> materialTextureArray; // holds every material's albedo/normal/orm textures as layers

        BufferTracker<SSBO> animationDescriptorSSBO; // stores pose-buffer location + frame/duration info for every animation

        //chunk buffers - declared after the animation buffers; the constructor's init list matches this order
        BufferTracker<VBO> chunkVBO; // stores chunk vertices (4 per quad)
        BufferTracker<EBO> chunkEBO; // stores chunk indices (6 per quad, chunk-local, allocated in whole quads)
        BufferTracker<SSBO> chunkInstanceDataSSBO; // stores per chunk data
        // One uint32 per quad. Has NO free list of its own: it mirrors chunkEBO's
        // address space, so a chunk's quads live at indexRange.start / 6.
        BufferTracker<SSBO> chunkTriTextureSSBO;
        BufferTracker<TextureArray> chunkTextureArray; // holds every  chunk material's albedo/normal/orm textures as layers

        // 0,1,2, 2,1,3, 4,5,6, 6,5,7, ... Every chunk's indices are chunk-local, so a
        // chunk with N quads uses the first N*6 entries. Grown on demand by UploadChunkMesh.
        std::vector<uint32_t> chunkIndexPattern;


        // Reserves vertex, index and descriptor space for a mesh and fills `out`.
        // Takes nothing on failure.
        bool AllocateMesh(Mesh& mesh, MeshTracker& out);

        void DeallocateEntity(Entity& entity); // frees the instance slot, forgets the entity
        void DeallocateMesh(MeshTracker& mesh); // frees geometry + descriptor, unregisters the mesh

        // Just the buffer-side free for a mesh's ranges, no entityManager
        // call. Shared by DeallocateMesh and by Allocate's rollback when a
        // freshly reserved MeshTracker never makes it into entityManager.
        void FreeMeshRanges(MeshTracker& mesh);

        bool MeshHasUser(MeshTracker& mesh);

        // Reserves a texture-array layer for each of material's present
        // maps (albedo/normal/orm - absent ones are simply skipped, not an
        // error) and a MaterialDescriptor slot. Either everything is
        // reserved and `out` is filled, or nothing is taken.
        bool AllocateMaterial(Material& material, MaterialTracker& out);
        

        void DeallocateMaterial(MaterialTracker& material); // frees layers + descriptor, unregisters the material
        void FreeMaterialRanges(MaterialTracker& material); // buffer-side free only, same reasoning as FreeMeshRanges
        bool MaterialHasUser(MaterialTracker& material);

        //allocates space in the pose buffer and animation descriptor buffer for the animation
        bool AllocateAnimation(const BakedAnimationClip& clip, AnimationTracker& out);

        void DeallocateAnimation(AnimationTracker& animation); // frees pose range + descriptor, unregisters the animation
        void FreeAnimationRanges(AnimationTracker& animation); // buffer-side free only, same reasoning as FreeMeshRanges
        bool AnimationHasUser(AnimationTracker& animation);

        // Frees only the chunk's mesh ranges (vertices + indices) and marks them
        // invalid in the handle. The instance slot and the tracker are untouched.
        // Used by remesh (when the size changes), by empty meshes, and by DeallocateChunk.
        void FreeChunkMeshRanges(ChunkHandle& handle);

        

        // Frees the layer. Chunks whose meshes still use this index will sample
        // whatever is uploaded there next, so only remove textures nothing references.
        bool RemoveChunkTexture(const std::string& path);
        bool materialMipsDirty = false;
        bool chunkMipsDirty = false;
};

#endif