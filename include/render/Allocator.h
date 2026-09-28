#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <vector>
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

    BufferTracker(GLsizeiptr capacityBytes) : buffer(capacityBytes) {}
};

class Allocator
{
    public:
        // entityManager is not owned. It must outlive the Allocator.
        Allocator(EntityRendererConfig config, EntityManager* entityManager);

        // Reserves buffer space for an entity, and for its mesh if no other
        // entity has registered that mesh yet. Reserves only, no GPU writes.
        // Returns a tracker with an invalid handle if allocation fails.
        EntityTracker Allocate(Entity& entity);

        // Releases the entity's space, and its mesh's space if this was the
        // last entity using the mesh.
        void Deallocate(Entity& entity);

        // Writes data into space that Allocate already reserved.
        void UploadEntityData(EntityTracker& tracker);
        void UploadMeshData(MeshTracker& tracker);

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
        BufferTracker<SSBO> BonePoseSSBO;              // stores information on bone poses and animations for each mesh

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
};

#endif