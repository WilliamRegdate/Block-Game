#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <vector>
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

class Entity;

class Allocator
{
    public:
        Allocator(EntityRendererConfig config);
        //allocates a buffer slot for an entity and its mesh if it does not already have a slot
        EntityTracker Allocate(Entity& entity);
        void Deallocate(EntityTracker& tracker); // deallocates the space used by the entity

    private:
        EntityManager* entityManager;

        BufferTracker<VBO> staticVBO; // stores vertices for static meshes
        BufferTracker<EBO> staticEBO; // stores indices for static meshes
        
        BufferTracker<SSBO> staticMeshDescriptorSSBO; // stores information on how to render all static meshes
        BufferTracker<SSBO> staticInstanceDataSSBO; // stores information about each static render instance


        BufferTracker<VBO> skinnedVBO; // stores vertices for animated meshes
        BufferTracker<EBO> skinnedEBO; // stores indices for animated meshes

        BufferTracker<SSBO> skinnedMeshDescriptorSSBO; // stores information on how to render all animated meshes
        BufferTracker<SSBO> skinnedInstanceDataSSBO; // stores information about each animated render instance
        BufferTracker<SSBO> BonePoseSSBO; // stores information on bone poses and animations for each mesh

        void DeallocateMesh(MeshTracker mesh);
        bool MeshHasUser(MeshTracker mesh);
};

#endif