#include "Allocator.h"

#include <algorithm>
#include <cstdint>

// Everything in this block is file-local: the free-list logic has no GL
// dependency and can be tested on its own.
namespace
{

constexpr uint32_t INVALID_RANGE = 0xFFFFFFFFu;

void SeedSlots(std::vector<emptySlot>& slots, uint32_t count)
{
    if (count > 0)
        slots.push_back(emptySlot{ 0, count });
}

// First-fit. Returns the start of a free range of `size` elements,
// or INVALID_RANGE if no free range is big enough.
uint32_t AllocRange(std::vector<emptySlot>& slots, uint32_t size)
{
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i].size < size)
            continue;

        const uint32_t start = slots[i].start;
        slots[i].start += size;
        slots[i].size  -= size;

        if (slots[i].size == 0)
            slots.erase(slots.begin() + i);
        return start;
    }
    return INVALID_RANGE;
}

// Returns a range to the free list, keeping the list sorted by start
// and merging with any free neighbours so space doesn't fragment.
void FreeRange(std::vector<emptySlot>& slots, uint32_t start, uint32_t size)
{
    auto it = std::lower_bound(slots.begin(), slots.end(), start,
        [](const emptySlot& s, uint32_t value) { return s.start < value; });

    size_t i = static_cast<size_t>(it - slots.begin());
    slots.insert(it, emptySlot{ start, size });

    // merge with the slot after
    if (i + 1 < slots.size() && slots[i].start + slots[i].size == slots[i + 1].start)
    {
        slots[i].size += slots[i + 1].size;
        slots.erase(slots.begin() + i + 1);
    }

    // merge with the slot before
    if (i > 0 && slots[i - 1].start + slots[i - 1].size == slots[i].start)
    {
        slots[i - 1].size += slots[i].size;
        slots.erase(slots.begin() + i);
    }
}

} // namespace


// ============================================================
// Construction
// ============================================================

Allocator::Allocator(EntityRendererConfig config, EntityManager* manager)
// Members are constructed in declaration order, so this list matches
// the order in Allocator.h. Each buffer is created at its full size.
: entityManager(manager),
    staticVBO(config.staticVertexCapacityBytes),
    staticEBO(config.staticIndexCapacityBytes),
    staticMeshDescriptorSSBO(config.maxMeshTypes * sizeof(MeshDescriptor)),
    staticInstanceDataSSBO(config.maxEntities * sizeof(InstanceData)),
    skinnedVBO(config.skinnedVertexCapacityBytes),
    skinnedEBO(config.skinnedIndexCapacityBytes),
    skinnedMeshDescriptorSSBO(config.maxMeshTypes * sizeof(MeshDescriptor)),
    skinnedInstanceDataSSBO(config.maxEntities * sizeof(InstanceData)),
    BonePoseSSBO(config.poseBufferCapacityBytes)
{
    // Start every buffer as one big empty range.
    SeedSlots(staticVBO.emptySlots, static_cast<uint32_t>(config.staticVertexCapacityBytes / sizeof(StaticVertex)));
    SeedSlots(staticEBO.emptySlots, static_cast<uint32_t>(config.staticIndexCapacityBytes / sizeof(uint32_t)));
    SeedSlots(staticMeshDescriptorSSBO.emptySlots, config.maxMeshTypes);
    SeedSlots(staticInstanceDataSSBO.emptySlots, config.maxEntities);

    SeedSlots(skinnedVBO.emptySlots, static_cast<uint32_t>(config.skinnedVertexCapacityBytes / sizeof(SkinnedVertex)));
    SeedSlots(skinnedEBO.emptySlots, static_cast<uint32_t>(config.skinnedIndexCapacityBytes / sizeof(uint32_t)));
    SeedSlots(skinnedMeshDescriptorSSBO.emptySlots, config.maxMeshTypes);
    SeedSlots(skinnedInstanceDataSSBO.emptySlots, config.maxEntities);

    // Pose space is counted in bone matrices.
    SeedSlots(BonePoseSSBO.emptySlots, static_cast<uint32_t>(config.poseBufferCapacityBytes / sizeof(glm::mat4)));
}


// ============================================================
// Allocate
// ============================================================

EntityTracker Allocator::Allocate(Entity& entity)
{
    // Value-initialised: invalid handle and null mesh pointer, which is
    // what the caller gets back if allocation fails.
    EntityTracker tracker{};

    if (entity.mesh == nullptr)
        return tracker;

    Mesh& mesh = *entity.mesh;
    const bool skinned = mesh.isSkinned;

    // 1. Find the mesh, or register it if this is the first entity using it.
    MeshTracker* meshTracker = entityManager->GetMesh(mesh.ID);
    const bool meshIsNew = (meshTracker == nullptr);
    if (meshIsNew)
    {
        MeshTracker fresh;
        if (!AllocateMesh(mesh, fresh))
            return tracker; // no buffer space, can't fit the mesh

        if (!entityManager->AddMesh(mesh, fresh))
        {
            // Someone else registered this asset ID between the GetMesh
            // check above and here. Give back what AllocateMesh just
            // reserved rather than leaking it.
            FreeMeshRanges(fresh);
            return tracker;
        }

        // AddMesh only reports success/failure, not a pointer to the
        // stored copy, so fetch it now that it's actually in the map.
        meshTracker = entityManager->GetMesh(mesh.ID);
    }

    // 2. One slot in the instance buffer for this entity.
    BufferTracker<SSBO>& instances = skinned ? skinnedInstanceDataSSBO : staticInstanceDataSSBO;

    const uint32_t slot = AllocRange(instances.emptySlots, 1);
    if (slot == INVALID_RANGE)
    {
        // Don't leave a mesh registered that nothing ended up using.
        if (meshIsNew)
            DeallocateMesh(*meshTracker);
        return tracker;
    }

    tracker.handle.isSkinned     = skinned;
    tracker.handle.instanceRange = BufferRange{ slot, 1 };
    tracker.meshTracker          = meshTracker;

    meshTracker->users++;

    if (!entityManager->AddEntity(entity, tracker))
    {
        // This exact Entity was already registered. Undo the slot and the
        // user-count bump so state stays consistent with what's actually
        // stored in entityManager.
        meshTracker->users--;
        FreeRange(instances.emptySlots, slot, 1);
        if (meshIsNew && !MeshHasUser(*meshTracker))
            DeallocateMesh(*meshTracker);
        return EntityTracker{};
    }

    return tracker;
}

// Reserves vertex, index and descriptor space for one mesh.
// Either everything is reserved and `out` is filled, or nothing is taken.
bool Allocator::AllocateMesh(Mesh& mesh, MeshTracker& out)
{
    const bool skinned = mesh.isSkinned;

    BufferTracker<VBO>&  vbo         = skinned ? skinnedVBO : staticVBO;
    BufferTracker<EBO>&  ebo         = skinned ? skinnedEBO : staticEBO;
    BufferTracker<SSBO>& descriptors = skinned ? skinnedMeshDescriptorSSBO : staticMeshDescriptorSSBO;

    // Vertices live in the derived class, so the cast depends on the flag.
    const uint32_t vertexCount = skinned
        ? static_cast<uint32_t>(static_cast<SkinnedMesh&>(mesh).vertices.size())
        : static_cast<uint32_t>(static_cast<StaticMesh&>(mesh).vertices.size());
    const uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size());

    // A zero-sized range would be "successfully" allocated and then never freed.
    if (vertexCount == 0 || indexCount == 0)
        return false;

    const uint32_t vertexStart = AllocRange(vbo.emptySlots, vertexCount);
    if (vertexStart == INVALID_RANGE)
        return false;

    const uint32_t indexStart = AllocRange(ebo.emptySlots, indexCount);
    if (indexStart == INVALID_RANGE)
    {
        FreeRange(vbo.emptySlots, vertexStart, vertexCount);
        return false;
    }

    const uint32_t descriptorSlot = AllocRange(descriptors.emptySlots, 1);
    if (descriptorSlot == INVALID_RANGE)
    {
        FreeRange(vbo.emptySlots, vertexStart, vertexCount);
        FreeRange(ebo.emptySlots, indexStart, indexCount);
        return false;
    }

    // TODO(pose): if the mesh is skinned, reserve space in BonePoseSSBO for
    // its baked bone poses (boneCount * frameCount mat4s, counted in mat4s
    // to match how the pose buffer's free list was seeded). Free everything
    // taken above if this fails, and store the range on out.handle.

    out.handle.isSkinned      = skinned;
    out.handle.vertexRange     = BufferRange{ vertexStart, vertexCount };
    out.handle.indexRange      = BufferRange{ indexStart,  indexCount };
    out.handle.descriptorRange = BufferRange{ descriptorSlot, 1 };
    out.assetID = mesh.ID;
    out.users   = 0;

    return true;
}


// ============================================================
// Deallocate
// ============================================================

void Allocator::Deallocate(Entity& entity)
{
    EntityTracker* tracker = entityManager->GetEntity(entity);
    if (tracker == nullptr || !tracker->handle.IsValid())
        return;

    // Captured before DeallocateEntity erases the map entry `tracker` points into.
    MeshTracker* mesh = tracker->meshTracker;

    DeallocateEntity(entity);

    if (mesh != nullptr)
    {
        mesh->users--;

        // Mesh goes last: it removes the MeshTracker that `mesh` refers to.
        if (!MeshHasUser(*mesh))
            DeallocateMesh(*mesh);
    }
}

// Frees the entity's instance slot and forgets the entity.
// Does not touch the mesh - Deallocate decides that.
void Allocator::DeallocateEntity(Entity& entity)
{
    EntityTracker* tracker = entityManager->GetEntity(entity);
    if (tracker == nullptr)
        return;

    BufferTracker<SSBO>& instances = tracker->handle.isSkinned ? skinnedInstanceDataSSBO : staticInstanceDataSSBO;
    FreeRange(instances.emptySlots, tracker->handle.instanceRange.start, tracker->handle.instanceRange.size);

    entityManager->RemoveEntity(entity);
}

// Frees the mesh's geometry and descriptor slot and unregisters it.
// `mesh` must not be used after this call.
void Allocator::DeallocateMesh(MeshTracker& mesh)
{
    FreeMeshRanges(mesh);
    entityManager->RemoveMesh(mesh.assetID); // erases the MeshTracker, `mesh` dangles after this
}

// Just the buffer-side free for a mesh's ranges - no entityManager call.
// Shared by DeallocateMesh and by Allocate's rollback path, where a
// freshly reserved MeshTracker never made it into entityManager at all.
void Allocator::FreeMeshRanges(MeshTracker& mesh)
{
    const bool skinned = mesh.handle.isSkinned;

    BufferTracker<VBO>&  vbo         = skinned ? skinnedVBO : staticVBO;
    BufferTracker<EBO>&  ebo         = skinned ? skinnedEBO : staticEBO;
    BufferTracker<SSBO>& descriptors = skinned ? skinnedMeshDescriptorSSBO : staticMeshDescriptorSSBO;

    FreeRange(vbo.emptySlots, mesh.handle.vertexRange.start, mesh.handle.vertexRange.size);
    FreeRange(ebo.emptySlots, mesh.handle.indexRange.start,  mesh.handle.indexRange.size);
    FreeRange(descriptors.emptySlots, mesh.handle.descriptorRange.start, mesh.handle.descriptorRange.size);

    // TODO(pose): if skinned, free the mesh's range in BonePoseSSBO here.
}


bool Allocator::MeshHasUser(MeshTracker& mesh)
{
    return mesh.users > 0;
}