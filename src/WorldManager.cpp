#include "WorldManager.h"

WorldManager::WorldManager(Renderer &renderer, ResourceController& manager)
    : renderer(renderer), resourceController(manager), mesher(manager)
{
    //TODO: need more robust block type preloading tokeep thread safety
    resourceController.GetOrLoadBlockType(generator.stone.ID);
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

        if (renderer.AddChunk(c, *e->pendingMesh))
            e->state = ChunkState::Uploaded;
        else
            e->state = ChunkState::Generated;   // GPU full or already added; retry later
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
            if (entry.state == ChunkState::Uploaded)
                renderer.RemoveChunk(coord);
        });

    const int demoteSq = (r + 1) * (r + 1);

    //demote chunks that are not rendered but still need to be stored
    storage.ForEach([&](const glm::ivec3& coord, ChunkEntry& entry)
    {
        if (distSq(coord) <= demoteSq)
            return;

        if (entry.state == ChunkState::Uploaded)
        {
            renderer.RemoveChunk(coord);
            entry.state = ChunkState::Generated;   // voxel data kept, only needs a remesh if the player returns
        }
        else if (entry.state == ChunkState::Meshed)
        {
            entry.pendingMesh.reset();             // meshed but never uploaded
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
