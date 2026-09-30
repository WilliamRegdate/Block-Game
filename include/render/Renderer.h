#ifndef RENDERER_H
#define RENDERER_H

#include<glad/glad.h>
#include<glm/glm.hpp>
#include<memory>
#include<cstdint>

#include "Entity.h"
#include "Allocator.h"
#include "EntityManager.h"
#include "RenderTypes.h"

// ------------------------------------------------------------
// Forward declarations - these don't exist yet. This header only
// needs to know they're types; nothing here requires their full
// definition.
// ------------------------------------------------------------
class DirtyTracker;
class Camera;

struct ChunkHandle; 
struct ChunkMeshData; 

using MeshAssetID = uint64_t;   // interned/hashed identifier, assumed to come from your asset system


class Renderer
{
    public:
        explicit Renderer(Camera* camera);   // was Renderer(); you wanted the constructor to take a camera
        ~Renderer();

        void Init();
        void Shutdown();

        void SetCamera(Camera* camera) { m_camera = camera; }

        
        bool         AddEntity(Entity& entity);
        void         UpdateEntity(Entity& entity);
        void         RemoveEntity(Entity& entity);

        // ---------------- Chunks ----------------
        void        AddChunk(const glm::ivec3& chunkCoord, const ChunkMeshData& meshData);
        void        UpdateChunk(ChunkHandle handle, const ChunkMeshData& meshData);
        void        RemoveChunk(ChunkHandle handle);

        // ---------------- Per-frame ----------------
        void Render();

    protected:

        void FlushDirty();          // push DirtyTracker's entries to their GPU buffers
        void CullPass();            // dispatch compute cull -> visible-index buffers
        void DrawStatic();          // bind static pipeline, issue multidraw indirect
        void DrawAnimated();        // bind animated pipeline, issue multidraw indirect
        void DrawChunks();          //bind the chunk pipeline, issue multidraw indirect

    private:
        // ---------------- Bookkeeping ----------------
            private:
        std::unique_ptr<EntityManager> m_entityManager;
        std::unique_ptr<Allocator>     m_allocator;
        // TODO(dirty): std::unique_ptr<DirtyTracker> m_dirtyTracker;
        // Left out for now: unique_ptr can't be destroyed while DirtyTracker is
        // an incomplete type, so the member would break compilation.

        Camera* m_camera = nullptr;
};


#endif