#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <vector>
#include <utility>
#include "Entity.h"
#include "RenderTypes.h"
#include "EntityManager.h"

struct EntityRendererConfig
{
    GLsizeiptr staticVertexCapacityBytes  = 0;
    GLsizeiptr staticIndexCapacityBytes   = 0;
    GLsizeiptr skinnedVertexCapacityBytes = 0;
    GLsizeiptr skinnedIndexCapacityBytes  = 0;
    GLsizeiptr poseBufferCapacityBytes    = 0;

    uint32_t maxEntities  = 0;
    uint32_t maxMeshTypes = 0;
    uint32_t maxMaterials = 0;

    // Every material's albedo/normal/orm textures share one TextureArray,
    // so every one of them needs to be exactly this size - anything else
    // fails to upload correctly (see TextureArray::UploadMaterial). Layer
    // count is derived as maxMaterials * 3 in Allocator's constructor: one
    // layer per map, even for materials missing one - simplest to reason
    // about, at the cost of some wasted layers if many materials are
    // missing maps.
    GLsizei materialTextureWidth  = 0;
    GLsizei materialTextureHeight = 0;
    GLenum  materialTextureFormat = GL_RGBA8;
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

        // Reserves buffer space for an entity, for its mesh if no other
        // entity has registered that mesh yet, and for its material (if it
        // has one) if no other entity has registered that material yet.
        // Reserves only, no GPU writes. Returns a tracker with an invalid
        // handle if allocation fails.
        EntityTracker Allocate(Entity& entity);

        // Releases the entity's space, and its mesh's/material's space if
        // this was the last entity using either.
        void Deallocate(Entity& entity);

        // Writes data into space that Allocate already reserved.
        bool UploadEntityData(Entity& entity);
        bool UploadMeshData(Mesh& mesh);
        bool UploadMaterialData(Material& material);

        // public buffer object accessors:
        VBO&          StaticVBO()               { return staticVBO.buffer; }
        EBO&          StaticEBO()               { return staticEBO.buffer; }
        SSBO&         StaticInstanceSSBO()      { return staticInstanceDataSSBO.buffer; }
        SSBO&         MaterialDescriptorSSBO()  { return materialDescriptorSSBO.buffer; }
        TextureArray& MaterialTextures()        { return materialTextureArray.buffer; }

        bool BuildInstanceData(Entity& entity, InstanceData& out, EntityHandle& handle);

        // Writes `count` consecutive InstanceData starting at `firstSlot` in one call.
        void UploadInstanceRange(bool skinned, uint32_t firstSlot, const InstanceData* data, uint32_t count);

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
        BufferTracker<SSBO> bonePoseSSBO;              // stores information on bone poses and animations for each mesh

        BufferTracker<SSBO> materialDescriptorSSBO;       // stores texture-array layer indices for every material
        BufferTracker<TextureArray> materialTextureArray; // holds every material's albedo/normal/orm textures as layers

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
    
    public:
        void GenerateMaterialMipmaps();
    private:
        bool materialMipsDirty = false;
};

#endif