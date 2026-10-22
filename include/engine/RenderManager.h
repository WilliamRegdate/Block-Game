
#ifndef ENTITY_MANAGER_H
#define ENTITY_MANAGER_H

#include<cstdint>
#include<unordered_map>
#include<utility>
#include <string>

#include "RenderTypes.h"
#include "VisualInstance.h"

// Entity.h, Mesh.h, Material.h and Animation.h aren't included here - only
// pointers/references to them are needed below, and Allocator.h already
// includes what's needed alongside this header, so forward declarations
// are enough and avoid widening RenderManager.h's own include graph.

struct MeshTracker;      // defined below, InstanceTracker only needs the name
struct MaterialTracker;  // defined below, InstanceTracker only needs the name

// ------------------------------------------------------------
// Per-instance record - what RenderManager stores for one live
// instance. Just data, no behaviour: where it lives, which mesh it
// uses, and which material it uses.
// ------------------------------------------------------------
struct InstanceTracker
{
    InstanceHandle      handle;                    // where this instance's data lives on the GPU
    MeshTracker*      meshTracker = nullptr;      // which mesh this instance uses
    MaterialTracker*  materialTracker = nullptr;  // which material this instance uses; nullptr if it has none
};

// ------------------------------------------------------------
// Per-mesh-type record - what's known about one unique mesh.
// ------------------------------------------------------------
struct MeshTracker
{
    MeshHandle handle;      // where this mesh's data lives on the GPU
    uint32_t   assetID = 0; // Mesh::ID - the key RenderManager finds this mesh by
    int        users = 0;   // how many entities use the mesh
};

// ------------------------------------------------------------
// Per-material record - what's known about one unique material.
// ------------------------------------------------------------
struct MaterialTracker
{
    MaterialHandle handle;      // where this material's data lives on the GPU
    uint32_t       assetID = 0; // Material::ID - the key RenderManager finds this material by
    int            users = 0;   // how many entities use the material
};

// ------------------------------------------------------------
// Per-animation record - what's known about one unique baked clip.
// Mirrors MeshTracker/MaterialTracker exactly.
// ------------------------------------------------------------
struct AnimationTracker
{
    AnimationHandle handle;      // where this animation's data lives on the GPU
    uint32_t        assetID = 0; // BakedAnimationClip::ID - the key RenderManager finds this animation by
    int             users = 0;   // how many AnimationPlayers currently reference this animation
};

// ------------------------------------------------------------
// Per-chunk record - what's known about one live chunk.
//
// Differs from MeshTracker in two ways:
//   - no `users` count: a chunk's mesh belongs to exactly one chunk, it is
//     never shared the way a Mesh asset is shared between entities.
//   - the mesh ranges inside `handle` change when the chunk is remeshed;
//     the instance slot does not.
//
// worldPosition is kept here so the chunk's instance data can be rewritten
// (after a remesh moves its ranges) without asking the world for it again.
// ------------------------------------------------------------
struct ChunkTracker
{
    ChunkHandle handle;             // where this chunk's data lives on the GPU
    uint64_t    chunkID = 0;        // the key RenderManager finds this chunk by
};
struct ChunkTextureTracker
{
    BufferRange layer;        // the single texture-array layer this texture occupies
    std::string path;         // the key RenderManager finds this texture by
};


class RenderManager
{
    public:

        bool AddInstance(VisualInstance& instance, InstanceTracker& tracker);
        void RemoveInstance(VisualInstance& instance);
        // Returns nullptr if this exact Entity has no tracker registered.
        InstanceTracker* GetInstance(VisualInstance& instance);

        // Calls func(const InstanceTracker&) once per registered instance.
        // Read-only by design - mutating the tracker from inside func would
        // bypass Allocator's bookkeeping (user counts, free lists).
        template<typename Func>
        void ForEachEntity(Func&& func) const
        {
            for (const auto& pair : m_instances)
                func(pair.second);
        }

        // ---- Meshes ----
        // Stores `tracker` keyed by mesh.ID. Returns false if this asset ID
        // is already registered.
        bool AddMesh(Mesh& mesh, MeshTracker& tracker);
        void RemoveMesh(uint32_t assetID); // takes the ID, not a Mesh&: by the
                                            // time this runs the original Mesh
                                            // may not be reachable, only the
                                            // assetID stored in MeshTracker
        // Returns nullptr if this asset ID has no tracker registered.
        MeshTracker* GetMesh(uint32_t assetID);

        // ---- Materials ----
        // Stores `tracker` keyed by material.ID. Returns false if this
        // asset ID is already registered.
        bool AddMaterial(Material& material, MaterialTracker& tracker);
        void RemoveMaterial(uint32_t assetID); // same reasoning as RemoveMesh
        // Returns nullptr if this asset ID has no tracker registered.
        MaterialTracker* GetMaterial(uint32_t assetID);

        // ---- Animations ----
        // Stores `tracker` keyed by clip.ID. Returns false if this asset ID
        // is already registered.
        bool AddAnimation(BakedAnimationClip& clip, AnimationTracker& tracker);
        void RemoveAnimation(uint32_t assetID); // same reasoning as RemoveMesh
        // Returns nullptr if this asset ID has no tracker registered.
        AnimationTracker* GetAnimation(uint32_t assetID);

        bool AddChunk(int64_t chunkID, ChunkTracker& tracker);
        void RemoveChunk(int64_t chunkID);
        // Returns nullptr if this chunk has no tracker registered.
        ChunkTracker* GetChunk(int64_t chunkID);

         // ---- Chunk textures ----
        // Stores `tracker` keyed by path. Returns false if already registered.
        bool AddChunkTexture(const std::string& path, ChunkTextureTracker& tracker);
        void RemoveChunkTexture(const std::string& path);
        // Returns nullptr if this path has no tracker registered.
        ChunkTextureTracker* GetChunkTexture(const std::string& path);

        // Calls func(const ChunkTracker&) once per registered chunk.
        // Read-only for the same reason as ForEachEntity.
        template<typename Func>
        void ForEachChunk(Func&& func) const
        {
            for (const auto& pair : m_chunks)
                func(pair.second);
        }

    private:
        // Keyed by the instance's own address.
        // NOTE: a tracker becomes unreachable - GetEntity/RemoveEntity
        // silently stop finding it - if the Entity is ever moved or
        // reallocated (e.g. stored in a std::vector that resizes) without
        // this map being updated. If entities live somewhere that can
        // reallocate, consider keying by instance.ID instead, the same way
        // m_meshes/m_materials/m_animations are keyed by their assets' IDs
        // below rather than by address.
        std::unordered_map<VisualInstance*, InstanceTracker> m_instances;
        std::unordered_map<uint32_t, MeshTracker>      m_meshes;     // keyed by Mesh::ID
        std::unordered_map<uint32_t, MaterialTracker>  m_materials;  // keyed by Material::ID
        std::unordered_map<uint32_t, AnimationTracker> m_animations; // keyed by BakedAnimationClip::ID


        std::unordered_map<int64_t, ChunkTracker>     m_chunks;     // keyed by chunk ID
        std::unordered_map<std::string, ChunkTextureTracker> m_chunkTextures; //keyed by filepath
};

#endif