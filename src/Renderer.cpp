#include "Allocator.h"

#include <algorithm>
#include <cstdint>

// ============================================================
// Free-list helpers
// ============================================================
// These take just the vector of empty slots, not a BufferTracker,
// so the allocation logic has no GL dependency and can be tested
// on its own.
constexpr uint32_t INVALID_RANGE = 0xFFFFFFFFu;

// Static and skinned pools each hand out slots starting at 0, so a raw
// slot number would collide between the two in EntityManager's maps
// (keyed by handle.Index()). The top bit of the handle's 24-bit index
// says which pool the slot belongs to.
constexpr uint32_t SKINNED_FLAG    = 1u << (HANDLE_INDEX_BITS - 1);
constexpr uint32_t SLOT_MASK       = SKINNED_FLAG - 1;
constexpr uint32_t GENERATION_MASK = (1u << (32 - HANDLE_INDEX_BITS)) - 1;

uint32_t PackIndex(uint32_t slot, bool skinned)
{
    return skinned ? (slot | SKINNED_FLAG) : slot;
}

bool IsSkinnedIndex(uint32_t index) { return (index & SKINNED_FLAG) != 0; }
uint32_t SlotOf(uint32_t index)     { return index & SLOT_MASK; }


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


// ============================================================
// Construction
// ============================================================

Allocator::Allocator(EntityRendererConfig config)
// Members are constructed in declaration order, so this list matches
// the order in Allocator.h. Each buffer is created at its full size.
: staticVBO(config.staticVertexCapacityBytes),
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

// One generation counter per slot. A slot's generation goes up each time
// it is freed, so handles from its previous occupant stop matching.
staticEntityGeneration.assign(config.maxEntities, 0);
skinnedEntityGeneration.assign(config.maxEntities, 0);
staticMeshGeneration.assign(config.maxMeshTypes, 0);
skinnedMeshGeneration.assign(config.maxMeshTypes, 0);
}


// ============================================================
// Allocate
// ============================================================

EntityTracker Allocator::Allocate(Entity& entity)
{
// Value-initialised: invalid handle and null mesh pointer, which is
// what the caller gets back if allocation fails.
EntityTracker tracker{};

// TODO(entity): need a value that uniquely identifies this entity's mesh
// (e.g. its asset ID), so two entities using the same mesh find the same
// MeshTracker.
uint64_t meshKey = 0;

// TODO(entity): need to know whether this mesh is skinned (has bones and
// animation) or static. This decides which set of buffers is used.
bool skinned = false;

// 1. Find the mesh, or register it if this is the first entity using it.
MeshTracker* mesh = FindMesh(meshKey);
if (mesh == nullptr)
{
    MeshRegistration info{};
    info.key     = meshKey;
    info.skinned = skinned;

    // TODO(entity / resourceLoader): need the loaded mesh data to fill in:
    //   info.vertexCount, info.indexCount   element counts, not bytes
    //   info.vertexData,  info.indexData    pointers to the CPU-side arrays
    //   info.poseCount,   info.poseData     skinned meshes only: number of bone
    //                                       matrices and a pointer to them
    //   info.bounds                         the mesh's local AABB

    mesh = RegisterMesh(info);
    if (mesh == nullptr)
        return tracker;   // out of vertex / index / descriptor / pose space
}

// 2. Reserve a slot for this entity in the matching instance buffer.
BufferTracker<SSBO>&   instances   = skinned ? skinnedInstanceDataSSBO : staticInstanceDataSSBO;
std::vector<uint32_t>& generations = skinned ? skinnedEntityGeneration : staticEntityGeneration;

const uint32_t slot = AllocRange(instances.emptySlots, 1);
if (slot == INVALID_RANGE)
{
    // Out of entity slots. If we only just registered the mesh for this
    // call, don't leave its space allocated with nobody using it.
    if (!MeshHasUser(*mesh))
        DeallocateMesh(*mesh);
    return tracker;
}

// 3. Build the handle and fill in the tracker.
tracker.handle      = EntityHandle::Make(PackIndex(slot, skinned), generations[slot]);
tracker.meshTracker = mesh;
mesh->users++;

// TODO(entity): the instance data itself is not written here (see the
// questions in the reply). If Allocate should write it, it needs from the
// entity: position, rotation and scale (combined into the transform), and
// the texture ID. Roughly:
//   InstanceData data{};
//   data.transform  = <entity's position / rotation / scale as a mat4>;
//   data.meshID     = SlotOf(mesh->handle.Index());
//   data.materialID = <entity's texture ID>;
//   instances.buffer.Upload(static_cast<GLintptr>(slot * sizeof(InstanceData)),
//                           &data, sizeof(InstanceData));

return tracker;
}


// ============================================================
// Deallocate
// ============================================================

void Allocator::Deallocate(EntityTracker tracker)
{
if (!tracker.handle.IsValid() || tracker.meshTracker == nullptr)
    return;

const uint32_t index   = tracker.handle.Index();
const bool     skinned = IsSkinnedIndex(index);
const uint32_t slot    = SlotOf(index);

std::vector<uint32_t>& generations = skinned ? skinnedEntityGeneration : staticEntityGeneration;

// Stale handle or double free: this slot has already been released
// (and possibly reused), so its generation no longer matches.
if (slot >= generations.size() || generations[slot] != tracker.handle.Generation())
    return;

// Give the slot back. The GPU may still be drawing this slot from an
// in-flight frame, but reuse is safe as long as instance data is written
// with glBufferSubData, since the driver synchronises those writes. If you
// ever switch to persistent mapping, delay this release by a few frames.
BufferTracker<SSBO>& instances = skinned ? skinnedInstanceDataSSBO : staticInstanceDataSSBO;
FreeRange(instances.emptySlots, slot, 1);
generations[slot] = (generations[slot] + 1) & GENERATION_MASK;

MeshTracker* mesh = tracker.meshTracker;
if (mesh->users > 0)
    mesh->users--;

if (!MeshHasUser(*mesh))
    DeallocateMesh(*mesh);
}


// ============================================================
// Mesh helpers
// ============================================================

MeshTracker* Allocator::FindMesh(uint64_t key)
{
auto it = meshTrackers.find(key);
return (it != meshTrackers.end()) ? &it->second : nullptr;
}

MeshTracker* Allocator::RegisterMesh(const MeshRegistration& info)
{
const bool skinned  = info.skinned;
const bool needPose = skinned && info.poseCount > 0;

BufferTracker<VBO>&    vbo         = skinned ? skinnedVBO : staticVBO;
BufferTracker<EBO>&    ebo         = skinned ? skinnedEBO : staticEBO;
BufferTracker<SSBO>&   desc        = skinned ? skinnedMeshDescriptorSSBO : staticMeshDescriptorSSBO;
std::vector<uint32_t>& generations = skinned ? skinnedMeshGeneration : staticMeshGeneration;

// Reserve everything first. If any of it fails, hand back what we took.
const uint32_t vertexStart = AllocRange(vbo.emptySlots, info.vertexCount);
const uint32_t indexStart  = AllocRange(ebo.emptySlots, info.indexCount);
const uint32_t descSlot    = AllocRange(desc.emptySlots, 1);
const uint32_t poseStart   = needPose ? AllocRange(BonePoseSSBO.emptySlots, info.poseCount) : 0;

const bool ok = vertexStart != INVALID_RANGE && indexStart != INVALID_RANGE &&
                descSlot    != INVALID_RANGE && poseStart  != INVALID_RANGE;
if (!ok)
{
    if (vertexStart != INVALID_RANGE) FreeRange(vbo.emptySlots, vertexStart, info.vertexCount);
    if (indexStart  != INVALID_RANGE) FreeRange(ebo.emptySlots, indexStart, info.indexCount);
    if (descSlot    != INVALID_RANGE) FreeRange(desc.emptySlots, descSlot, 1);
    if (needPose && poseStart != INVALID_RANGE) FreeRange(BonePoseSSBO.emptySlots, poseStart, info.poseCount);
    return nullptr;
}

// Upload the geometry. Free space is tracked in elements, uploads are in bytes.
const size_t vertexStride = skinned ? sizeof(SkinnedVertex) : sizeof(StaticVertex);
vbo.buffer.Upload(static_cast<GLintptr>(vertexStart * vertexStride),
                    info.vertexData,
                    static_cast<GLsizeiptr>(info.vertexCount * vertexStride));

ebo.buffer.Upload(static_cast<GLintptr>(indexStart * sizeof(uint32_t)),
                    info.indexData,
                    static_cast<GLsizeiptr>(info.indexCount * sizeof(uint32_t)));

if (needPose)
{
    BonePoseSSBO.buffer.Upload(static_cast<GLintptr>(poseStart * sizeof(glm::mat4)),
                                info.poseData,
                                static_cast<GLsizeiptr>(info.poseCount * sizeof(glm::mat4)));
}

// Write this mesh's descriptor into its slot.
MeshDescriptor descriptor{};
descriptor.firstIndex  = indexStart;
descriptor.indexCount  = info.indexCount;
descriptor.baseVertex  = static_cast<int32_t>(vertexStart);
descriptor.materialID  = 0;                 // texture is per entity, so it goes in InstanceData
descriptor.skeletonID  = poseStart;         // start of this mesh's poses (see questions in the reply)
descriptor.isAnimated  = skinned ? 1u : 0u;
descriptor.localBounds = info.bounds;

desc.buffer.Upload(static_cast<GLintptr>(descSlot * sizeof(MeshDescriptor)),
                    &descriptor, sizeof(MeshDescriptor));

// Record everything DeallocateMesh will need to give the space back.
MeshTracker mesh{};
mesh.handle      = MeshHandle::Make(PackIndex(descSlot, skinned), generations[descSlot]);
mesh.users       = 0;
mesh.key         = info.key;
mesh.skinned     = skinned;
mesh.vertexStart = vertexStart;
mesh.vertexCount = info.vertexCount;
mesh.indexStart  = indexStart;
mesh.indexCount  = info.indexCount;
mesh.poseStart   = poseStart;
mesh.poseCount   = needPose ? info.poseCount : 0;

// unordered_map keeps element addresses stable until erase, so the
// pointer we return (and that entities store) stays valid while the
// mesh has users.
return &meshTrackers.emplace(info.key, mesh).first->second;
}

bool Allocator::MeshHasUser(MeshTracker mesh)
{
return mesh.users > 0;
}

void Allocator::DeallocateMesh(MeshTracker mesh)
{
const bool skinned = mesh.skinned;

BufferTracker<VBO>&    vbo         = skinned ? skinnedVBO : staticVBO;
BufferTracker<EBO>&    ebo         = skinned ? skinnedEBO : staticEBO;
BufferTracker<SSBO>&   desc        = skinned ? skinnedMeshDescriptorSSBO : staticMeshDescriptorSSBO;
std::vector<uint32_t>& generations = skinned ? skinnedMeshGeneration : staticMeshGeneration;

FreeRange(vbo.emptySlots, mesh.vertexStart, mesh.vertexCount);
FreeRange(ebo.emptySlots, mesh.indexStart, mesh.indexCount);

if (mesh.poseCount > 0)
    FreeRange(BonePoseSSBO.emptySlots, mesh.poseStart, mesh.poseCount);

const uint32_t descSlot = SlotOf(mesh.handle.Index());
FreeRange(desc.emptySlots, descSlot, 1);
generations[descSlot] = (generations[descSlot] + 1) & GENERATION_MASK;

// `mesh` is a copy, so erasing the map entry here is safe.
meshTrackers.erase(mesh.key);
