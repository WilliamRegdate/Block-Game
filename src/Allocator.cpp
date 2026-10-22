#include "Allocator.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cfloat>
#include "AABB.h"

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
        return AABB{ mn, mx };
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

    // How many whole quads the chunk EBO can hold. The EBO free list is seeded
    // with exactly quads * 6 indices and every chunk allocation is a multiple of
    // 6, so every firstIndex is a multiple of 6 and firstIndex / 6 is a valid
    // slot in the quad SSBO. The quad SSBO is sized from this same number.
    uint32_t ChunkTriCapacity(const EntityRendererConfig& config)
    {
        const uint32_t indexCapacity = static_cast<uint32_t>(config.chunkIndexCapacityBytes / sizeof(uint32_t));
        return indexCapacity / CHUNK_INDICES_PER_TRI;
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
                          static_cast<GLsizei>(config.maxMaterials) * 3, config.materialTextureFormat),
    animationDescriptorSSBO(config.maxAnimations * sizeof(AnimationDescriptor)),
    // Chunk meshes are rewritten on every remesh, so they're dynamic, not static.
    chunkVBO(config.chunkVertexCapacityBytes, GL_DYNAMIC_DRAW),
    chunkEBO(config.chunkIndexCapacityBytes, GL_DYNAMIC_DRAW),
    chunkInstanceDataSSBO(static_cast<GLsizeiptr>(config.chunkCapacity * sizeof(ChunkInstanceData))),
    chunkTriTextureSSBO(static_cast<GLsizeiptr>(ChunkTriCapacity(config) * sizeof(uint32_t))),
    chunkTextureArray(config.chunkMaterialTextureWidth, config.chunkMaterialTextureHeight,
                      static_cast<GLsizei>(config.maxChunkMaterials) * 3, config.chunkMaterialTextureFormat)
    
                      
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


    SeedSlots(bonePoseSSBO.emptySlots, static_cast<uint32_t>(config.poseBufferCapacityBytes / sizeof(glm::mat4)));

    SeedSlots(materialDescriptorSSBO.emptySlots, config.maxMaterials);
    // Layers aren't typed by texture kind - any layer can hold an albedo,
    // normal or orm map, since the shader just samples whichever index the
    // MaterialDescriptor gives it. One shared free list over all of them.
    SeedSlots(materialTextureArray.emptySlots, config.maxMaterials * 3);

    SeedSlots(animationDescriptorSSBO.emptySlots, config.maxAnimations);

    // ---- chunks ----
    SeedSlots(chunkVBO.emptySlots, static_cast<uint32_t>(config.chunkVertexCapacityBytes / sizeof(ChunkVertex)));
    // Seeded in whole quads (see ChunkQuadCapacity), so every index range handed out
    // starts on a multiple of 6.
    SeedSlots(chunkEBO.emptySlots, ChunkTriCapacity(config) * CHUNK_INDICES_PER_TRI);
    SeedSlots(chunkInstanceDataSSBO.emptySlots, static_cast<uint32_t>(config.chunkCapacity));
    // chunkQuadTextureSSBO is deliberately NOT seeded: it has no free list, it mirrors the EBO's.
    // TODO: CHUNK - layers are seeded like the entity material array. Revisit when chunk
    // materials exist (e.g. allocate albedo/normal/orm as one contiguous triplet instead).
    SeedSlots(chunkTextureArray.emptySlots, config.maxChunkMaterials * 3);
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
                return tracker; //failed to add material
            }
                
            if (!entityManager->AddMaterial(material, fresh))
            {
                FreeMaterialRanges(fresh);
                if (meshIsNew)
                    DeallocateMesh(*meshTracker);
                return tracker; //failed to add  mesh to tracker
            }

            materialTracker = entityManager->GetMaterial(material.ID);
        }
    }
    // 2. Find the animations, or register them if this is the first entity
    // using the animation. no animation is also allowed; 
    if (skinned)
    {
        std::vector<AnimationTracker*> grantedThisCall;
        std::vector<BakedAnimationClip*> clips = entity.animationPlayer.GetClips();

        auto rollbackAnimations = [&]()
        {
            for (AnimationTracker* granted : grantedThisCall)
            {
                granted->users--;
                if (granted->users == 0)
                    DeallocateAnimation(*granted);   // assumed name, mirrors DeallocateMesh/DeallocateMaterial -
                                                    // adjust if Allocator.h declares it differently
            }
        };

        for (BakedAnimationClip* clip : clips)
        {
            AnimationTracker* animationTracker = entityManager->GetAnimation(clip->ID);
            const bool animationIsNew = (animationTracker == nullptr);

            if (animationIsNew)
            {
                AnimationTracker fresh;

                if (!AllocateAnimation(*clip, fresh))
                {
                    rollbackAnimations();
                    if (meshIsNew)
                        DeallocateMesh(*meshTracker);
                    if (materialIsNew)
                        DeallocateMaterial(*materialTracker);
                    return tracker;
                }

                if (!entityManager->AddAnimation(*clip, fresh))
                {
                    FreeAnimationRanges(fresh);
                    rollbackAnimations();
                    if (meshIsNew)
                        DeallocateMesh(*meshTracker);
                    if (materialIsNew)
                        DeallocateMaterial(*materialTracker);
                    return tracker;
                }

                animationTracker = entityManager->GetAnimation(clip->ID);
            }

            animationTracker->users++;
            grantedThisCall.push_back(animationTracker);
        }
    }

    // 4. One slot in the instance buffer for this entity.
    BufferTracker<SSBO>& instances = skinned ? skinnedInstanceDataSSBO : staticInstanceDataSSBO;

    const uint32_t slot = AllocRange(instances.emptySlots, 1);
    if (slot == INVALID_RANGE)
    {
        // Don't leave a mesh or material registered that nothing ended up using.
        if (materialIsNew)
            DeallocateMaterial(*materialTracker);
        if (meshIsNew)
            DeallocateMesh(*meshTracker);

        //dealloc/ remove users from animations
        AnimationTracker* animationTracker;
        std::vector<BakedAnimationClip*> clips = entity.animationPlayer.GetClips();
        for (BakedAnimationClip* clip : clips)
        {
            animationTracker = entityManager->GetAnimation(clip->ID);
            animationTracker->users--;
            if(AnimationHasUser(*animationTracker))
            {
                DeallocateAnimation(*animationTracker);
            }
        }
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

            //dealloc or remove users from animations
            AnimationTracker* animationTracker;
            std::vector<BakedAnimationClip*> clips = entity.animationPlayer.GetClips();
            for (BakedAnimationClip* clip : clips)
            {
                animationTracker = entityManager->GetAnimation(clip->ID);
                animationTracker->users--;
                if(AnimationHasUser(*animationTracker))
                {
                    DeallocateAnimation(*animationTracker);
                }
            }
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

bool Allocator::AllocateAnimation(const BakedAnimationClip& clip, AnimationTracker& out)
{
    if (clip.boneCount == 0 || clip.frameCount == 0)
        return false;
    if (clip.poses.size() != static_cast<size_t>(clip.frameCount) * clip.boneCount)
        return false; // boneCount doesn't match what this clip was actually baked against

    const uint32_t poseCount = clip.frameCount * clip.boneCount;

    const uint32_t poseStart = AllocRange(bonePoseSSBO.emptySlots, poseCount);
    if (poseStart == INVALID_RANGE)
        return false;

    const uint32_t descriptorSlot = AllocRange(animationDescriptorSSBO.emptySlots, 1);
    if (descriptorSlot == INVALID_RANGE)
    {
        FreeRange(bonePoseSSBO.emptySlots, poseStart, poseCount);
        return false;
    }

    out.handle.poseRange       = BufferRange{ poseStart, poseCount };
    out.handle.descriptorRange = BufferRange{ descriptorSlot, 1 };
    out.assetID = clip.ID;
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

    //removes users or deallocates for each animation used
    AnimationTracker* animationTracker;
    std::vector<BakedAnimationClip*> clips = entity.animationPlayer.GetClips();
    for (BakedAnimationClip* clip : clips)
    {
        animationTracker = entityManager->GetAnimation(clip->ID);
        animationTracker->users--;
        if(AnimationHasUser(*animationTracker))
        {
            DeallocateAnimation(*animationTracker);
        }
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

// Frees the animation's pose range and descriptor slot and unregisters it.
// `animation` must not be used after this call. Mirrors DeallocateMesh.
void Allocator::DeallocateAnimation(AnimationTracker& animation)
{
    FreeAnimationRanges(animation);
    entityManager->RemoveAnimation(animation.assetID);
}

// Just the buffer-side free for an animation's ranges - no entityManager
// call. Same reasoning as FreeMeshRanges/FreeMaterialRanges.
void Allocator::FreeAnimationRanges(AnimationTracker& animation)
{
    FreeRange(bonePoseSSBO.emptySlots, animation.handle.poseRange.start, animation.handle.poseRange.size);
    FreeRange(animationDescriptorSSBO.emptySlots, animation.handle.descriptorRange.start, animation.handle.descriptorRange.size);
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
    descriptor.skeletonID = 0; // TODO: no skeleton registry yet - SkinnedMesh::skeleton isn't tracked
                                // by ID anywhere, only the one animation currently playing is (via
                                // InstanceData.animationID, set in BuildInstanceData below)
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
        descriptor.boundsMin = aabb.min;
        descriptor.boundsMax = aabb.max;
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
        descriptor.boundsMin = aabb.min;
        descriptor.boundsMax = aabb.max;
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

// Uploads clip's baked poses into bonePoseSSBO and an AnimationDescriptor
// into animationDescriptorSSBO, at whatever ranges AllocateAnimation
// already reserved (tracker.handle). Mirrors UploadMeshData's shape.
bool Allocator::UploadAnimationData(BakedAnimationClip& clip)
{
    AnimationTracker* tracker = entityManager->GetAnimation(clip.ID);

    if (tracker == nullptr || !tracker->handle.IsValid())
        return false;

    // Same guard as UploadMeshData: the reserved range was sized from the
    // clip at AllocateAnimation time - if it's changed size since, writing
    // would spill into a neighbour's range.
    if (clip.poses.size() != tracker->handle.poseRange.size)
        return false;

    bonePoseSSBO.buffer.Upload(
        static_cast<GLintptr>(tracker->handle.poseRange.start) * sizeof(glm::mat4),
        clip.poses.data(),
        static_cast<GLsizeiptr>(clip.poses.size() * sizeof(glm::mat4)));

    const uint32_t boneCount = tracker->handle.poseRange.size / clip.frameCount;

    AnimationDescriptor descriptor;
    descriptor.poseStartIndex = tracker->handle.poseRange.start;
    descriptor.frameCount     = clip.frameCount;
    descriptor.boneCount      = boneCount;
    descriptor.duration       = clip.duration;

    animationDescriptorSSBO.buffer.Upload(
        static_cast<GLintptr>(tracker->handle.descriptorRange.start) * sizeof(AnimationDescriptor),
        &descriptor, sizeof(AnimationDescriptor));

    return true;
}

bool Allocator::BuildInstanceData(Entity& entity, InstanceData& out, EntityHandle& handle)
{
    EntityTracker* tracker = entityManager->GetEntity(entity);
    if (tracker == nullptr || !tracker->handle.IsValid() || tracker->meshTracker == nullptr)
        return false;

    handle = tracker->handle;

    out.transform  = entity.transform.GetMatrix();
    out.meshID     = tracker->meshTracker->handle.descriptorRange.start;
    out.materialID = tracker->materialTracker != nullptr
                         ? tracker->materialTracker->handle.descriptorRange.start
                         : 0;

    out.animTime  = 0;
    out.animIndex = 0;

    if (handle.isSkinned)
    {
        BakedAnimationClip* current = entity.animationPlayer.GetCurrentClip();
        if (current != nullptr)
        {
            AnimationTracker* animTracker = entityManager->GetAnimation(current->ID);
            if (animTracker != nullptr && animTracker->handle.IsValid() && current->frameCount > 0)
            {
                out.animTime  = entity.animationPlayer.progress;
                out.animIndex = animTracker->handle.descriptorRange.start;
            }
        }
    }

    return true;
}

void Allocator::UploadInstanceRange(bool skinned, uint32_t firstSlot, const InstanceData* data, uint32_t count)
{
    BufferTracker<SSBO>& instances = skinned ? skinnedInstanceDataSSBO : staticInstanceDataSSBO;
    instances.buffer.Upload(static_cast<GLintptr>(firstSlot) * sizeof(InstanceData),
                            data, static_cast<GLsizeiptr>(count) * sizeof(InstanceData));
}

bool Allocator::UploadEntityData(Entity& entity)
{
    InstanceData data;
    EntityHandle handle;
    if (!BuildInstanceData(entity, data, handle))
        return false;

    UploadInstanceRange(handle.isSkinned, handle.instanceRange.start, &data, 1);
    return true;
}


uint32_t Allocator::AddChunkTexture(Texture& texture)
{
    // Already registered: hand back the same layer rather than spending another one.
    if (ChunkTextureTracker* existing = entityManager->GetChunkTexture(texture.filepath))
    return existing->layer.start;
    
    int width = 0, height = 0;
    unsigned char* pixels = texture.GetImage(width, height);
    if (pixels == nullptr)
    return INVALID_CHUNK_TEXTURE;
    
    // Every layer in a texture array has the same dimensions.
    if (width != 16 || height != 16)
    {
        Texture::FreeImage(pixels);
        return INVALID_CHUNK_TEXTURE;
    }
    
    const uint32_t layer = AllocRange(chunkTextureArray.emptySlots, 1);
    if (layer == INVALID_RANGE)
    {
        Texture::FreeImage(pixels);
        return INVALID_CHUNK_TEXTURE;
    }
    
    ChunkTextureTracker tracker;
    tracker.layer = BufferRange{ layer, 1 };
    tracker.path  = texture.filepath;
    
    if (!entityManager->AddChunkTexture(texture.filepath, tracker))
    {
        FreeRange(chunkTextureArray.emptySlots, layer, 1);
        Texture::FreeImage(pixels);
        return INVALID_CHUNK_TEXTURE;
    }
    
    // ASSUMPTION: adjust to TextureArray's real UploadLayer signature.
    chunkTextureArray.buffer.UploadLayer(static_cast<GLint>(layer), pixels);
    
    chunkMipsDirty = true;
    Texture::FreeImage(pixels);
    return layer;
}

bool Allocator::RemoveChunkTexture(const std::string& path)
{
    ChunkTextureTracker* tracker = entityManager->GetChunkTexture(path);
    if (tracker == nullptr)
        return false;

    FreeRange(chunkTextureArray.emptySlots, tracker->layer.start, 1);
    entityManager->RemoveChunkTexture(path); // `tracker` dangles after this
    return true;
}

ChunkTracker Allocator::AllocateChunk(uint64_t chunkID, const glm::vec3& worldPosition)
{
    // Value-initialised: invalid handle, which is what the caller gets back on failure.
    ChunkTracker tracker{};

    if (entityManager->GetChunk(chunkID) != nullptr)
        return tracker; // this chunk is already registered

    const uint32_t slot = AllocRange(chunkInstanceDataSSBO.emptySlots, 1);
    if (slot == INVALID_RANGE)
        return tracker; // out of chunk instance slots

    tracker.chunkID                  = chunkID;
    tracker.handle.instanceRange     = BufferRange{ slot, 1 };

    if (!entityManager->AddChunk(chunkID, tracker))
    {
        FreeRange(chunkInstanceDataSSBO.emptySlots, slot, 1);
        return ChunkTracker{};
    }

    // Write an empty instance (indexCount 0) so this slot is never left holding
    // whatever the previous owner of the slot had.
    UploadChunkInstanceData(worldPosition, chunkID);

    return tracker;
}

void Allocator::DeallocateChunk(uint64_t chunkID)
{
    ChunkTracker* tracker = entityManager->GetChunk(chunkID);
    if (tracker == nullptr || !tracker->handle.IsValid())
        return;

    FreeChunkMeshRanges(tracker->handle);

    // Zero the GPU slot first so a stale indirect command that still points at it
    // draws nothing instead of the old chunk.
    const ChunkInstanceData empty{};
    chunkInstanceDataSSBO.buffer.Upload(
        static_cast<GLintptr>(tracker->handle.instanceRange.start) * sizeof(ChunkInstanceData),
        &empty, sizeof(ChunkInstanceData));

    FreeRange(chunkInstanceDataSSBO.emptySlots,
              tracker->handle.instanceRange.start, tracker->handle.instanceRange.size);

    entityManager->RemoveChunk(chunkID); // erases the ChunkTracker, `tracker` dangles after this
}

// Frees just the vertex and index ranges and marks them invalid in the handle.
// The quad SSBO needs nothing: it has no free list, its slots go free with the EBO range.
void Allocator::FreeChunkMeshRanges(ChunkHandle& handle)
{
    if (handle.vertexRange.IsValid())
        FreeRange(chunkVBO.emptySlots, handle.vertexRange.start, handle.vertexRange.size);
    if (handle.indexRange.IsValid())
        FreeRange(chunkEBO.emptySlots, handle.indexRange.start, handle.indexRange.size);

    handle.vertexRange = BufferRange{};
    handle.indexRange  = BufferRange{};
}

bool Allocator::UploadChunkMesh(uint64_t chunkID,
                                const glm::vec3& worldPosition,
                                const std::vector<ChunkVertex>& vertices,
                                const std::vector<uint32_t>& indices,
                                const std::vector<uint32_t>& triangleTextures)
{
    ChunkTracker* tracker = entityManager->GetChunk(chunkID);
    if (tracker == nullptr || !tracker->handle.IsValid())
        return false;

    ChunkHandle& h = tracker->handle;

    if (indices.empty())
    {
        FreeChunkMeshRanges(h);
        return UploadChunkInstanceData(worldPosition, chunkID);
    }

    const uint32_t vertexCount = static_cast<uint32_t>(vertices.size());
    const uint32_t indexCount  = static_cast<uint32_t>(indices.size());
    const uint32_t triCount    = indexCount / CHUNK_INDICES_PER_TRI;

    // These replace the old vertices.size() check. They guard what actually matters:
    if (indexCount % CHUNK_INDICES_PER_TRI != 0) return false;   // whole triangles only
    if (triangleTextures.size() != triCount)     return false;   // one texture per triangle
    // (optional, debug only) every index must point inside this chunk's vertices:
    // assert(*std::max_element(indices.begin(), indices.end()) < vertexCount);

    const bool reuseRanges = h.HasMesh()
                          && h.indexRange.size  == indexCount
                          && h.vertexRange.size == vertexCount;   // vertex count now varies independently
    if (!reuseRanges)
    {
        const uint32_t vertexStart = AllocRange(chunkVBO.emptySlots, vertexCount);
        if (vertexStart == INVALID_RANGE)
            return false;

        const uint32_t indexStart = AllocRange(chunkEBO.emptySlots, indexCount);
        if (indexStart == INVALID_RANGE)
        {
            FreeRange(chunkVBO.emptySlots, vertexStart, vertexCount);
            return false;
        }

        FreeChunkMeshRanges(h);
        h.vertexRange = BufferRange{ vertexStart, vertexCount };
        h.indexRange  = BufferRange{ indexStart,  indexCount };
    }

    assert(h.indexRange.start % CHUNK_INDICES_PER_TRI == 0);

    chunkVBO.buffer.Upload(static_cast<GLintptr>(h.vertexRange.start) * sizeof(ChunkVertex),
                           vertices.data(), vertexCount * sizeof(ChunkVertex));

    chunkEBO.buffer.Upload(static_cast<GLintptr>(h.indexRange.start) * sizeof(uint32_t),
                           indices.data(), indexCount * sizeof(uint32_t));

    chunkTriTextureSSBO.buffer.Upload(
        static_cast<GLintptr>(h.indexRange.start / CHUNK_INDICES_PER_TRI) * sizeof(uint32_t),
        triangleTextures.data(), triCount * sizeof(uint32_t));

    return UploadChunkInstanceData(worldPosition, chunkID);
}

bool Allocator::BuildChunkInstanceData(const glm::ivec3& chunkPosition, uint64_t chunkID, ChunkInstanceData& out, ChunkHandle& handle)
{
    ChunkTracker* tracker = entityManager->GetChunk(chunkID);
    if (tracker == nullptr || !tracker->handle.IsValid())
        return false;

    handle = tracker->handle;

    out = ChunkInstanceData{};   // zeroes the padding too
    out.position   = chunkPosition;
    out.firstIndex = handle.HasMesh() ? handle.indexRange.start : 0;
    out.indexCount = handle.HasMesh() ? handle.indexRange.size  : 0;
    return true;
}

bool Allocator::UploadChunkInstanceData(const glm::ivec3& chunkPosition, uint64_t chunkID)
{
    ChunkInstanceData data;
    ChunkHandle handle;
    if (!BuildChunkInstanceData(chunkPosition ,chunkID, data, handle))
        return false;

    chunkInstanceDataSSBO.buffer.Upload(
        static_cast<GLintptr>(handle.instanceRange.start) * sizeof(ChunkInstanceData),
        &data, sizeof(ChunkInstanceData));
    return true;
}

bool Allocator::BuildChunkDrawCommand(uint64_t chunkID, DrawIndexedIndirectCommand& out)
{
    ChunkTracker* tracker = entityManager->GetChunk(chunkID);
    if (tracker == nullptr || !tracker->handle.IsValid() || !tracker->handle.HasMesh())
        return false;

    const ChunkHandle& h = tracker->handle;

    // Indices are chunk-local, so baseVertex moves them to this chunk's vertices.
    // baseInstance is the instance slot: the vertex shader reads gl_BaseInstance
    // to find this chunk's ChunkInstanceData (and from there, its quad textures).
    out.indexCount    = h.indexRange.size;
    out.instanceCount = 1;
    out.firstIndex    = h.indexRange.start;
    out.baseVertex    = static_cast<int32_t>(h.vertexRange.start);
    out.baseInstance  = h.instanceRange.start;
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

    animationDescriptorSSBO.buffer.Delete();

    chunkVBO.buffer.Delete();
    chunkEBO.buffer.Delete();
    chunkInstanceDataSSBO.buffer.Delete();
    chunkTriTextureSSBO.buffer.Delete();
    chunkTextureArray.buffer.Delete();
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

       descriptor.albedoTexIndex = h.albedoLayer.start;
        descriptor.normalTexIndex = h.normalLayer.start;
        descriptor.ormTexIndex    = h.ormLayer.start;

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

bool Allocator::AnimationHasUser(AnimationTracker& animation)
{
    return animation.users > 0;
}

void Allocator::GenerateMaterialMipmaps()
{
    if (!materialMipsDirty)
        return;
    materialTextureArray.buffer.GenerateMipmaps();
    materialMipsDirty = false;
}
void Allocator::GenerateChunkTextureMipmaps()
{
    if (!chunkMipsDirty)
        return;
    chunkTextureArray.buffer.GenerateMipmaps();
    chunkMipsDirty = false;
}