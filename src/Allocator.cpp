#include "Allocator.h"

#include <algorithm>
#include <cstdint>

// Everything in this block is file-local: the free-list logic has no GL
// dependency and can be tested on its own.
namespace
{

    template<typename VertexT>
    AABB ComputeBounds(const std::vector<VertexT>& vertices)
    {
        glm::vec3 mn(FLT_MAX);
        glm::vec3 mx(-FLT_MAX);
        for (const VertexT& v : vertices)
        {
            mn = glm::min(mn, v.position);
            mx = glm::max(mx, v.position);
        }
        return AABB{ glm::vec4(mn, 0.0f), glm::vec4(mx, 0.0f) };
    }

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
    bonePoseSSBO(config.poseBufferCapacityBytes),
    materialDescriptorSSBO(config.maxMaterials * sizeof(MaterialDescriptor)),
    materialTextureArray(config.materialTextureWidth, config.materialTextureHeight,
                          static_cast<GLsizei>(config.maxMaterials) * 3, config.materialTextureFormat)
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
    SeedSlots(bonePoseSSBO.emptySlots, static_cast<uint32_t>(config.poseBufferCapacityBytes / sizeof(glm::mat4)));

    SeedSlots(materialDescriptorSSBO.emptySlots, config.maxMaterials);
    // Layers aren't typed by texture kind - any layer can hold an albedo,
    // normal or orm map, since the shader just samples whichever index the
    // MaterialDescriptor gives it. One shared free list over all of them.
    SeedSlots(materialTextureArray.emptySlots, config.maxMaterials * 3);
}


// ============================================================
// Allocate
// ============================================================

EntityTracker Allocator::Allocate(Entity& entity)
{
    // Value-initialised: invalid handle and null mesh/material pointers,
    // which is what the caller gets back if allocation fails.
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

    // 2. Find the material, or register it if this is the first entity
    // using it. A null material is allowed - the entity just renders
    // without one; tracker.materialTracker stays nullptr, same as if
    // entityManager->GetMaterial simply never found anything.
    MaterialTracker* materialTracker = nullptr;
    bool materialIsNew = false;
    if (entity.material != nullptr)
    {
        Material& material = *entity.material;
        materialTracker = entityManager->GetMaterial(material.ID);
        materialIsNew = (materialTracker == nullptr);
        if (materialIsNew)
        {
            MaterialTracker fresh;
            if (!AllocateMaterial(material, fresh))
            {
                if (meshIsNew)
                    DeallocateMesh(*meshTracker);
                return tracker;
            }

            if (!entityManager->AddMaterial(material, fresh))
            {
                FreeMaterialRanges(fresh);
                if (meshIsNew)
                    DeallocateMesh(*meshTracker);
                return tracker;
            }

            materialTracker = entityManager->GetMaterial(material.ID);
        }
    }

    // 3. One slot in the instance buffer for this entity.
    BufferTracker<SSBO>& instances = skinned ? skinnedInstanceDataSSBO : staticInstanceDataSSBO;

    const uint32_t slot = AllocRange(instances.emptySlots, 1);
    if (slot == INVALID_RANGE)
    {
        // Don't leave a mesh or material registered that nothing ended up using.
        if (materialIsNew)
            DeallocateMaterial(*materialTracker);
        if (meshIsNew)
            DeallocateMesh(*meshTracker);
        return tracker;
    }

    tracker.handle.isSkinned     = skinned;
    tracker.handle.instanceRange = BufferRange{ slot, 1 };
    tracker.meshTracker          = meshTracker;
    tracker.materialTracker      = materialTracker;

    meshTracker->users++;
    if (materialTracker != nullptr)
        materialTracker->users++;

    if (!entityManager->AddEntity(entity, tracker))
    {
        // This exact Entity was already registered. Undo the slot and both
        // user-count bumps so state stays consistent with what's actually
        // stored in entityManager.
        meshTracker->users--;
        FreeRange(instances.emptySlots, slot, 1);
        if (meshIsNew && !MeshHasUser(*meshTracker))
            DeallocateMesh(*meshTracker);

        if (materialTracker != nullptr)
        {
            materialTracker->users--;
            if (materialIsNew && !MaterialHasUser(*materialTracker))
                DeallocateMaterial(*materialTracker);
        }

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

    // TODO(pose): if the mesh is skinned, reserve space in bonePoseSSBO for
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

// Reserves a texture-array layer for each map material actually has, and a
// MaterialDescriptor slot. Either everything asked for is reserved and
// `out` is filled, or nothing is taken.
bool Allocator::AllocateMaterial(Material& material, MaterialTracker& out)
{
    BufferRange albedoLayer, normalLayer, ormLayer;

    auto rollbackLayers = [&]()
    {
        if (albedoLayer.IsValid()) FreeRange(materialTextureArray.emptySlots, albedoLayer.start, 1);
        if (normalLayer.IsValid()) FreeRange(materialTextureArray.emptySlots, normalLayer.start, 1);
        if (ormLayer.IsValid())    FreeRange(materialTextureArray.emptySlots, ormLayer.start, 1);
    };

    if (material.albedo.has_value())
    {
        const uint32_t layer = AllocRange(materialTextureArray.emptySlots, 1);
        if (layer == INVALID_RANGE) { rollbackLayers(); return false; }
        albedoLayer = BufferRange{ layer, 1 };
    }
    if (material.normal.has_value())
    {
        const uint32_t layer = AllocRange(materialTextureArray.emptySlots, 1);
        if (layer == INVALID_RANGE) { rollbackLayers(); return false; }
        normalLayer = BufferRange{ layer, 1 };
    }
    if (material.orm.has_value())
    {
        const uint32_t layer = AllocRange(materialTextureArray.emptySlots, 1);
        if (layer == INVALID_RANGE) { rollbackLayers(); return false; }
        ormLayer = BufferRange{ layer, 1 };
    }

    const uint32_t descriptorSlot = AllocRange(materialDescriptorSSBO.emptySlots, 1);
    if (descriptorSlot == INVALID_RANGE)
    {
        rollbackLayers();
        return false;
    }

    out.handle.albedoLayer     = albedoLayer;
    out.handle.normalLayer     = normalLayer;
    out.handle.ormLayer        = ormLayer;
    out.handle.descriptorRange = BufferRange{ descriptorSlot, 1 };
    out.assetID = material.ID;
    out.users   = 0;

    //upload actual material data here because it only needs doing once
    UploadMaterialData(material);

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
    MeshTracker*     mesh     = tracker->meshTracker;
    MaterialTracker* material = tracker->materialTracker;

    DeallocateEntity(entity);

    if (mesh != nullptr)
    {
        mesh->users--;

        // Mesh goes last: it removes the MeshTracker that `mesh` refers to.
        if (!MeshHasUser(*mesh))
            DeallocateMesh(*mesh);
    }

    if (material != nullptr)
    {
        material->users--;
        if (!MaterialHasUser(*material))
            DeallocateMaterial(*material);
    }
}

// Frees the entity's instance slot and forgets the entity.
// Does not touch the mesh or material - Deallocate decides that.
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

    // TODO(pose): if skinned, free the mesh's range in bonePoseSSBO here.
}

// Frees the material's texture-array layers and descriptor slot and
// unregisters it. `material` must not be used after this call.
void Allocator::DeallocateMaterial(MaterialTracker& material)
{
    FreeMaterialRanges(material);
    entityManager->RemoveMaterial(material.assetID); // erases the MaterialTracker, `material` dangles after this
}

// Just the buffer-side free for a material's ranges - no entityManager
// call. Same reasoning as FreeMeshRanges.
void Allocator::FreeMaterialRanges(MaterialTracker& material)
{
    if (material.handle.albedoLayer.IsValid())
        FreeRange(materialTextureArray.emptySlots, material.handle.albedoLayer.start, 1);
    if (material.handle.normalLayer.IsValid())
        FreeRange(materialTextureArray.emptySlots, material.handle.normalLayer.start, 1);
    if (material.handle.ormLayer.IsValid())
        FreeRange(materialTextureArray.emptySlots, material.handle.ormLayer.start, 1);

    FreeRange(materialDescriptorSSBO.emptySlots, material.handle.descriptorRange.start, material.handle.descriptorRange.size);
}

bool Allocator::UploadMeshData(Mesh& mesh)
{
    MeshTracker* tracker = entityManager->GetMesh(mesh.ID);
    if (tracker == nullptr || !tracker->handle.IsValid())
        return false;

    const MeshHandle& h = tracker->handle;
    const bool skinned = h.isSkinned;

    BufferTracker<VBO>&  vbo         = skinned ? skinnedVBO : staticVBO;
    BufferTracker<EBO>&  ebo         = skinned ? skinnedEBO : staticEBO;
    BufferTracker<SSBO>& descriptors = skinned ? skinnedMeshDescriptorSSBO : staticMeshDescriptorSSBO;

    // The reserved ranges were sized from the mesh at Allocate time. If the
    // mesh has changed size since, writing would spill into a neighbour's range.
    if (mesh.indices.size() != h.indexRange.size)
        return false;

    MeshDescriptor descriptor;
    descriptor.firstIndex = h.indexRange.start;
    descriptor.indexCount = h.indexRange.size;
    descriptor.baseVertex = static_cast<int32_t>(h.vertexRange.start);
    descriptor.materialID = 0; // meshes don't know their material; InstanceData supplies it
    descriptor.skeletonID = 0; // TODO(pose): bonePoseSSBO range once that's reserved
    descriptor.isAnimated = skinned ? 1u : 0u;

    if (skinned)
    {
        SkinnedMesh& m = static_cast<SkinnedMesh&>(mesh);
        if (m.vertices.size() != h.vertexRange.size)
            return false;

        vbo.buffer.Upload(static_cast<GLintptr>(h.vertexRange.start) * sizeof(SkinnedVertex),
                          m.vertices.data(),
                          static_cast<GLsizeiptr>(m.vertices.size() * sizeof(SkinnedVertex)));
                          
        AABB aabb = ComputeBounds(m.vertices);
        descriptor.boundsMin = aabb.boundsMin;
        descriptor.boundsMin = aabb.boundsMax;
    }
    else
    {
        StaticMesh& m = static_cast<StaticMesh&>(mesh);
        if (m.vertices.size() != h.vertexRange.size)
            return false;

        vbo.buffer.Upload(static_cast<GLintptr>(h.vertexRange.start) * sizeof(StaticVertex),
                          m.vertices.data(),
                          static_cast<GLsizeiptr>(m.vertices.size() * sizeof(StaticVertex)));
        AABB aabb = ComputeBounds(m.vertices);
        descriptor.boundsMin = aabb.boundsMin;
        descriptor.boundsMin = aabb.boundsMax;
    }

    // Indices stay relative to the mesh's own vertices. baseVertex in the
    // descriptor shifts them to the mesh's place in the shared VBO at draw time.
    ebo.buffer.Upload(static_cast<GLintptr>(h.indexRange.start) * sizeof(uint32_t),
                      mesh.indices.data(),
                      static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)));

    descriptors.buffer.Upload(static_cast<GLintptr>(h.descriptorRange.start) * sizeof(MeshDescriptor),
                              &descriptor, sizeof(MeshDescriptor));
    return true;
}

bool Allocator::UploadEntityData(Entity& entity)
{
    EntityTracker* tracker = entityManager->GetEntity(entity);
    if (tracker == nullptr || !tracker->handle.IsValid() || tracker->meshTracker == nullptr)
        return false;

    BufferTracker<SSBO>& instances = tracker->handle.isSkinned ? skinnedInstanceDataSSBO
                                                               : staticInstanceDataSSBO;

    InstanceData data;
    data.transform  = entity.transform.GetMatrix(); // assumption: see below
    data.meshID     = tracker->meshTracker->handle.descriptorRange.start;
    data.materialID = tracker->materialTracker != nullptr
                          ? tracker->materialTracker->handle.descriptorRange.start
                          : 0;
    data.animOffset = 0; // TODO(pose)

    instances.buffer.Upload(static_cast<GLintptr>(tracker->handle.instanceRange.start) * sizeof(InstanceData),
                            &data, sizeof(InstanceData));
    return true;
}

void Allocator::Shutdown()
{
    staticVBO.buffer.Delete();
    staticEBO.buffer.Delete();
    staticMeshDescriptorSSBO.buffer.Delete();
    staticInstanceDataSSBO.buffer.Delete();

    skinnedVBO.buffer.Delete();
    skinnedEBO.buffer.Delete();
    skinnedMeshDescriptorSSBO.buffer.Delete();
    skinnedInstanceDataSSBO.buffer.Delete();
    bonePoseSSBO.buffer.Delete();

    materialDescriptorSSBO.buffer.Delete();
    materialTextureArray.buffer.Delete();
}

bool Allocator::UploadMaterialData(Material& material)
{
    
    MaterialTracker* tracker = entityManager->GetMaterial(material.ID);
    if (tracker == nullptr || !tracker->handle.IsValid())
        return false;

    const MaterialHandle& h = tracker->handle;

    // -1 tells UploadMaterial "no such map, skip it".
    auto layerOrSkip = [](const BufferRange& r) -> GLint
    {
        return r.IsValid() ? static_cast<GLint>(r.start) : -1;
    };

    materialTextureArray.buffer.UploadMaterial(&material,
                                               layerOrSkip(h.albedoLayer),
                                               layerOrSkip(h.normalLayer),
                                               layerOrSkip(h.ormLayer));

    // Absent maps get index 0 here, since the descriptor fields are plain
    // uint32s. See the note below.
    MaterialDescriptor descriptor;
    descriptor.flags = 0;
    if (h.albedoLayer.IsValid()) descriptor.flags |= MATERIAL_HAS_ALBEDO;
    if (h.normalLayer.IsValid()) descriptor.flags |= MATERIAL_HAS_NORMAL;
    if (h.ormLayer.IsValid())    descriptor.flags |= MATERIAL_HAS_ORM;
    // tint stays at its default (white) for now.

    materialDescriptorSSBO.buffer.Upload(
        static_cast<GLintptr>(h.descriptorRange.start) * sizeof(MaterialDescriptor),
        &descriptor, sizeof(MaterialDescriptor));

    materialMipsDirty = true;
    
    return true;
}

bool Allocator::MeshHasUser(MeshTracker& mesh)
{
    return mesh.users > 0;
}

bool Allocator::MaterialHasUser(MaterialTracker& material)
{
    return material.users > 0;
}