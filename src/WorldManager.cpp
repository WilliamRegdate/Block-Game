#include "WorldManager.h"
WorldManager::WorldManager(Renderer &renderer, ResourceController& manager)
    : renderer(renderer), resourceController(manager), mesher(manager)
{
    //TODO: need more robust block type preloading tokeep thread safety
    resourceController.GetOrLoadBlockType(generator.stone.ID);
    resourceController.GetOrLoadBlockType(generator.cobblestone.ID);
    resourceController.GetOrLoadBlockType(generator.log.ID);
    resourceController.GetOrLoadBlockType(generator.mud.ID);
    resourceController.GetOrLoadBlockType(generator.water.ID);
    resourceController.GetOrLoadBlockType(generator.snow.ID);
    resourceController.GetOrLoadBlockType(generator.sand.ID);
    resourceController.GetOrLoadBlockType(generator.grass.ID);
    resourceController.GetOrLoadBlockType(generator.redSand.ID);

}

WorldManager::~WorldManager()
{
}

Chunk WorldManager::LoadChunk(glm::ivec3 chunkPos)
{
    Chunk chunk = Chunk(Voxel()); //init with air
    glm::ivec3 pos;
    for (pos.x = 0; pos.x < Chunk::SIZE; pos.x++)
    {
        for (pos.y = 0; pos.y < Chunk::SIZE; pos.y++)
        {
            for (pos.z = 0; pos.z < Chunk::SIZE; pos.z++)
            {
                chunk.SetVoxel(pos.x, pos.y, pos.z, generator.GetBlockAt(chunkPos.x * Chunk::SIZE + pos.x, chunkPos.y * Chunk::SIZE + pos.y, chunkPos.z * Chunk::SIZE + pos.z));
            }
        }
    }
    chunk.position = chunkPos;

    //set a task to get vetices for special data that should be updated (tickable blocks for example)
    
    return chunk;
}
void WorldManager::Update(const glm::ivec3& center)
{
    LoadChunks(center);
    DrainGenerated();
    SubmitGeneration();
    ScheduleMeshing(center);
    DrainMeshed();
    UploadMeshed();
}

void WorldManager::SubmitGeneration()
{
    while (generatingInFlight < MAX_GEN_IN_FLIGHT && !pendingGeneration.empty())
    {
        const glm::ivec3 coord = pendingGeneration.back();
        pendingGeneration.pop_back();

        ChunkEntry* entry = storage.Find(coord);
        if (entry == nullptr || entry->state != ChunkState::Requested)
            continue;                       // unloaded, or already handled

        entry->state = ChunkState::Generating;
        ++generatingInFlight;

        pool.Submit([this, coord]
        {
            // WORKER THREAD: no storage, no renderer, no GL.
            auto chunk = std::make_shared<const Chunk>(LoadChunk(coord));
            genResults.Push({ coord, std::move(chunk) });
        });
    }
}

void WorldManager::DrainGenerated()
{
    std::vector<GenResult> results;
    genResults.Drain(results);

    for (GenResult& r : results)
    {
        --generatingInFlight;               // every job pushes exactly one result, stale or not

        ChunkEntry* entry = storage.Find(r.coord);
        if (entry == nullptr || entry->state != ChunkState::Generating)
            continue;                       // erased while the worker ran: drop it

        entry->chunk = std::move(r.chunk);
        entry->state = ChunkState::Generated;
    }
}
void WorldManager::ScheduleMeshing(const glm::ivec3& center)
{
    //do dirty chunks first
    std::vector<glm::ivec3> deferred;
    while (!dirtyChunks.empty())
    {
        const glm::ivec3 coord = dirtyChunks.back();
        dirtyChunks.pop_back();

        ChunkEntry* e = storage.Find(coord);
        if (!e || e->dirty != ChunkDirty::Dirty) continue;
        if (!e->HasData() || !storage.FaceNeighboursReady(coord)) { deferred.push_back(coord); continue; }

        e->dirty = ChunkDirty::Clean;
        e->state = ChunkState::Meshing;
        ++meshingInFlight;

        ChunkNeighbourhood nb = BuildNeighbourhood(coord);
        const uint32_t version = e->version;

        pool.Submit([this, coord, nb = std::move(nb), version]
        {
            MeshResult res{ coord, version, {} };
            mesher.MeshChunk(nb, res.data.vertices, res.data.indices, res.data.triangleTextures);
            meshResults.Push(std::move(res));
        });
    }

    const int r = static_cast<int>(renderDistance);

    auto distSq = [&](const glm::ivec3& c)
    {
        const glm::ivec3 d = c - center;
        return d.x * d.x + d.y * d.y + d.z * d.z;
    };

    // Collect chunks that are ready to mesh.
    std::vector<glm::ivec3> candidates;
    storage.ForEach([&](const glm::ivec3& c, ChunkEntry& e)
    {
        if (e.state != ChunkState::Generated) return;
        if (distSq(c) > r * r) return;                 // only mesh inside radius R
        if (!storage.FaceNeighboursReady(c)) return;   // needs all 6 neighbours generated
        candidates.push_back(c);
    });

    // Nearest first.
    std::sort(candidates.begin(), candidates.end(),
              [&](const glm::ivec3& a, const glm::ivec3& b) { return distSq(a) < distSq(b); });

    for (const glm::ivec3& c : candidates)
    {
        if (meshingInFlight >= MAX_MESH_IN_FLIGHT) break;

        ChunkEntry* e = storage.Find(c);
        e->state = ChunkState::Meshing;
        ++meshingInFlight;

        ChunkNeighbourhood nb = BuildNeighbourhood(c);   // main thread: snapshot of 27 shared_ptrs
        const uint32_t version = e->version;

        pool.Submit([this, c, nb = std::move(nb), version]
        {
            MeshResult res{ c, version, {} };
            mesher.MeshChunk(nb, res.data.vertices, res.data.indices, res.data.triangleTextures);
            meshResults.Push(std::move(res));
        });
    }
}

void WorldManager::DrainMeshed()
{
    std::vector<MeshResult> results;
    meshResults.Drain(results);

    for (MeshResult& r : results)
    {
        --meshingInFlight;
        ChunkEntry* e = storage.Find(r.coord);
        if (e == nullptr || e->state != ChunkState::Meshing || e->version != r.version)
            continue;

        e->pendingMesh = std::make_unique<ChunkMeshData>(std::move(r.data));
        e->state = ChunkState::Meshed;
        readyToUpload.push_back(r.coord);
    }
}

void WorldManager::UploadMeshed()
{
    int uploaded = 0;
    while (uploaded < MAX_UPLOADS_PER_FRAME && !readyToUpload.empty())
    {
        const glm::ivec3 c = readyToUpload.back();
        readyToUpload.pop_back();

        ChunkEntry* e = storage.Find(c);
        if (e == nullptr || e->state != ChunkState::Meshed || !e->pendingMesh)
            continue;

        const bool ok = e->onGPU ? renderer.UpdateChunk(c, *e->pendingMesh)
                                 : renderer.AddChunk(c, *e->pendingMesh);

        if (ok)
        {
            e->state = ChunkState::Uploaded;
            e->onGPU = true;
        }
        else
        {
            e->state = ChunkState::Generated;   // onGPU unchanged: whatever was there still is
        }
        e->pendingMesh.reset();
        ++uploaded;
    }
}


void WorldManager::LoadChunks(const glm::ivec3& center)
{
    if (center == lastCenter)
        return;
    lastCenter = center;

    const int r         = static_cast<int>(renderDistance);
    const int genRadius = r + 1;
    const int genSq     = genRadius * genRadius;
    const int unloadSq  = (r + 2) * (r + 2);

    // Defined first so everything below can use it.
    auto distSq = [&](const glm::ivec3& c)
    {
        const glm::ivec3 d = c - center;
        return d.x * d.x + d.y * d.y + d.z * d.z;
    };

    // 1. Register everything in range that isn't registered yet.
    for (int x = -genRadius; x <= genRadius; ++x)
    for (int y = -genRadius; y <= genRadius; ++y)
    for (int z = -genRadius; z <= genRadius; ++z)
    {
        if (x * x + y * y + z * z > genSq)
            continue;

        const glm::ivec3 coord = center + glm::ivec3(x, y, z);
        auto [entry, inserted] = storage.TryInsert(coord);
        if (inserted)
            pendingGeneration.push_back(coord);
    }

    // Sort ONCE, after adding. FURTHEST first, so pop_back() in SubmitGeneration gives the nearest.
    std::sort(pendingGeneration.begin(), pendingGeneration.end(),
              [&](const glm::ivec3& a, const glm::ivec3& b) { return distSq(a) > distSq(b); });

    // 2. Unload anything beyond R + 2.
    storage.EraseIf(
    [&](const glm::ivec3& coord, const ChunkEntry&) { return distSq(coord) > unloadSq; },
    [&](const glm::ivec3& coord, const ChunkEntry& entry)
    {
        if (entry.onGPU)
            renderer.RemoveChunk(coord);
    });

    const int demoteSq = (r + 1) * (r + 1);

    //demote chunks that are not rendered but still need to be stored
    storage.ForEach([&](const glm::ivec3& coord, ChunkEntry& entry)
    {
        if (distSq(coord) <= demoteSq) return;

        if (entry.onGPU)
        {
            renderer.RemoveChunk(coord);
            entry.onGPU = false;
            entry.pendingMesh.reset();
            entry.state = ChunkState::Generated;
        }
        else if (entry.state == ChunkState::Meshed)
        {
            entry.pendingMesh.reset();
            entry.state = ChunkState::Generated;
        }
    });
}

ChunkNeighbourhood WorldManager::BuildNeighbourhood(const glm::ivec3& c)
{
    ChunkNeighbourhood nb;
    for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
    {
        const ChunkEntry* e = storage.Find(c + glm::ivec3(dx, dy, dz));
        if (e && e->HasData())
            nb.chunks[(dx + 1) + 3 * (dy + 1) + 9 * (dz + 1)] = e->chunk;
    }
    return nb;
}


const std::vector<AABB>& WorldManager::GetShapeAt(int x, int y, int z) const
{
    static const std::vector<AABB> kNone;
    static const std::vector<AABB> kFull = { { glm::vec3(0.0f), glm::vec3(1.0f) } };

    const ChunkEntry* entry = storage.Find(glm::ivec3(x,y,z));
    if (!entry)
        return kFull;                    // unloaded counts as solid, so nobody falls through the world

    const Chunk* chunk = entry->chunk.get();

    auto it = shapeColliders.find(chunk->GetVoxel(x, y, z).voxelType);
    return  it == shapeColliders.end() ? kNone : it->second;
}


void WorldManager::RegisterShape(uint32_t voxelType, std::vector<AABB> shape)
{
    shapeColliders.emplace(voxelType, shape);
}
constexpr int CHUNK_BITS = 5;
constexpr int CHUNK_MASK = Chunk::SIZE - 1;
const Voxel& WorldManager::GetBlockAt(int x, int y, int z)
{
    static const Voxel AIR{};

    const glm::ivec3 chunkPos(x >> CHUNK_BITS, y >> CHUNK_BITS, z >> CHUNK_BITS);

    auto* entry = storage.Find(chunkPos); 
    if (!entry || !entry->chunk) return AIR;    // chunk not loaded

    return entry->chunk->GetVoxel(x & CHUNK_MASK, y & CHUNK_MASK, z & CHUNK_MASK);
}
bool WorldManager::SetBlockAt(const glm::ivec3& pos, const Voxel& voxel)
{
    const glm::ivec3 cc    = pos >> CHUNK_BITS;      // floor division, correct for negatives
    const glm::ivec3 local = pos &  CHUNK_MASK;

    ChunkEntry* entry = storage.Find(cc);
    if (!entry || !entry->HasData()) return false;

    // copy-on-write: workers may still be reading the old snapshot
    auto copy = std::make_shared<Chunk>(*entry->chunk);
    copy->SetVoxel(local.x, local.y, local.z, voxel);   // adjust to your Chunk API
    entry->chunk = std::move(copy);
    entry->version++;
    MarkDirty(cc, *entry);

    // a voxel on a chunk border changes the neighbour's mesh too (face culling / AO)
    for (int axis = 0; axis < 3; ++axis)
    {
        for (int dir : { -1, 1 })
        {
            const bool onEdge = (dir < 0) ? local[axis] == 0 : local[axis] == CHUNK_MASK;
            if (!onEdge) continue;

            glm::ivec3 nc = cc;
            nc[axis] += dir;
            ChunkEntry* n = storage.Find(nc);
            // chunks not meshed yet will pick up the change when they're meshed normally
            if (n && n->HasData() && n->state >= ChunkState::Meshing)
            {
                n->version++;
                MarkDirty(nc, *n);
            }
        }
    }
    return true;
}

void WorldManager::MarkDirty(const glm::ivec3& coord, ChunkEntry& entry)
{
    if (entry.dirty == ChunkDirty::Dirty) return;   // already queued
    entry.dirty = ChunkDirty::Dirty;
    dirtyChunks.push_back(coord);
}

const Voxel &WorldManager::GetBlockAt(const glm::ivec3& pos)
{
    static const Voxel AIR{};
    constexpr int CHUNK_BITS = 5;
    constexpr int CHUNK_MASK = Chunk::SIZE - 1;

    const glm::ivec3 chunkPos(pos.x >> CHUNK_BITS, pos.y >> CHUNK_BITS, pos.z >> CHUNK_BITS);

    auto* entry = storage.Find(chunkPos); 
    if (!entry || !entry->chunk) return AIR;    // chunk not loaded

    return entry->chunk->GetVoxel(pos.x & CHUNK_MASK, pos.y & CHUNK_MASK, pos.z & CHUNK_MASK);
}
