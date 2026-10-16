#ifndef CHUNKSTORAGE_H
#define CHUNKSTORAGE_H

#include <memory>
#include <unordered_map>
#include "ChunkHelpers.h"
#include "Chunk.h"

// Order matters: everything >= Generated has voxel data available.
enum class ChunkState : uint8_t
{
    Requested,   // registered, job queued
    Generating,  // a worker is generating it
    Generated,   // voxel data ready, not meshed (also where shell chunks stay)
    Meshing,     // a worker is meshing it
    Meshed,      // mesh ready, waiting for GL upload
    Uploaded     // in the renderer; leaving radius R goes back to Generated
};

struct ChunkEntry
{
    ChunkState state   = ChunkState::Requested;
    uint32_t   version = 0;                    // bump on edit; mesh results with an old version are dropped
    std::shared_ptr<const Chunk> chunk;        // null until generated
    std::unique_ptr<ChunkMeshData> pendingMesh;

    bool HasData() const { return state >= ChunkState::Generated && chunk != nullptr; }
};

// MAIN THREAD ONLY. Workers get coordinates or shared_ptr snapshots, never this object.
class ChunkStorage
{
    public:
        // Returns the entry and true if newly inserted, or the existing entry and false.
        std::pair<ChunkEntry*, bool> TryInsert(const glm::ivec3& coord)
        {
            auto [it, inserted] = m_chunks.try_emplace(ChunkCoord::ToID(coord));
            return { &it->second, inserted };
        }

        ChunkEntry* Find(const glm::ivec3& coord)
        {
            auto it = m_chunks.find(ChunkCoord::ToID(coord));
            return it != m_chunks.end() ? &it->second : nullptr;
        }
        const ChunkEntry* Find(const glm::ivec3& coord) const
        {
            auto it = m_chunks.find(ChunkCoord::ToID(coord));
            return it != m_chunks.end() ? &it->second : nullptr;
        }

        bool Contains(const glm::ivec3& coord) const { return Find(coord) != nullptr; }
        bool Erase(const glm::ivec3& coord)          { return m_chunks.erase(ChunkCoord::ToID(coord)) > 0; }
        size_t Size() const                          { return m_chunks.size(); }

        // True if all 6 face neighbours exist and have voxel data. This is the gate for meshing.
        bool FaceNeighboursReady(const glm::ivec3& coord) const
        {
            static constexpr glm::ivec3 dirs[6] = {
                { 1,0,0 }, { -1,0,0 }, { 0,1,0 }, { 0,-1,0 }, { 0,0,1 }, { 0,0,-1 }
            };
            for (const glm::ivec3& d : dirs)
            {
                const ChunkEntry* e = Find(coord + d);
                if (e == nullptr || !e->HasData())
                    return false;
            }
            return true;
        }

        // Calls func(coord, entry) for every chunk.
        template<typename Func>
        void ForEach(Func&& func)
        {
            for (auto& [id, entry] : m_chunks)
                func(ChunkCoord::FromID(id), entry);
        }

        // Erases every chunk where pred(coord, entry) is true; calls onErase(coord, entry) first
        // so the caller can e.g. renderer.RemoveChunk for Uploaded chunks.
        template<typename Pred, typename OnErase>
        void EraseIf(Pred&& pred, OnErase&& onErase)
        {
            for (auto it = m_chunks.begin(); it != m_chunks.end(); )
            {
                const glm::ivec3 coord = ChunkCoord::FromID(it->first);
                if (pred(coord, it->second))
                {
                    onErase(coord, it->second);
                    it = m_chunks.erase(it);
                }
                else ++it;
            }
        }

    private:
        std::unordered_map<uint64_t, ChunkEntry> m_chunks;
};

#endif