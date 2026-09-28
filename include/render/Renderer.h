#ifndef RENDERER_H
#define RENDERER_H

#include<glad/glad.h>
#include<glm/glm.hpp>
#include<memory>
#include<cstdint>

#include "Entity.h"
#include"EntityManager.h"
#include"RenderTypes.h"   // EntityHandle, MeshTypeHandle, VBO/EBO/SSBO/IndirectBuffer/VAO, InstanceData, MeshDescriptor...

// ------------------------------------------------------------
// Forward declarations - these don't exist yet. This header only
// needs to know they're types; nothing here requires their full
// definition.
// ------------------------------------------------------------
class MeshTracker;
class DirtyTracker;
class Allocator;
class Camera;

struct ChunkHandle; 
struct ChunkMeshData; 

using MeshAssetID = uint64_t;   // interned/hashed identifier, assumed to come from your asset system


class Renderer
{
    public:
        Renderer();

        ~Renderer();

        void Init();
        void Shutdown();

        void SetCamera(Camera* camera) { m_camera = camera; }

        // ---------------- Entities ----------------
        void         AddEntity(Entity entity);
        void         UpdateEntity(Entity entity);
        void         RemoveEntity(Entity entity);

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
        std::unique_ptr<EntityManager> m_entityTracker;  // keeps track of where entities are stored
        std::unique_ptr<MeshTracker>   m_meshTracker;    // keeps track of where meshes are stored
        std::unique_ptr<DirtyTracker>  m_dirtyTracker;   // what needs re-uploading this frame
        std::unique_ptr<Allocator>     m_allocator;      // free-slot tracking + alloc for the entity and mesh-geometry buffers

        Camera* m_camera = nullptr;  // not owned - used for culling and for this pass's view/proj

        // ---------------- GPU resources ----------------
        // unique_ptr rather than plain members: VBO/EBO/SSBO/etc. have
        // no default constructor (they need a real capacity up front),
        // so construction is deferred to Init() rather than happening
        // implicitly whenever an Renderer is constructed.
        std::unique_ptr<VBO> m_staticVBO;
        std::unique_ptr<EBO> m_staticEBO;
        std::unique_ptr<VBO> m_skinnedVBO;
        std::unique_ptr<EBO> m_skinnedEBO;

        std::unique_ptr<VAO> m_staticVAO;
        std::unique_ptr<VAO> m_skinnedVAO;

        std::unique_ptr<SSBO> m_meshDescriptors;
        std::unique_ptr<SSBO> m_materials;
        std::unique_ptr<SSBO> m_instanceData;
        std::unique_ptr<SSBO> m_bonePoses;

        std::unique_ptr<SSBO> m_staticVisibleIndex;
        std::unique_ptr<SSBO> m_animatedVisibleIndex;

        std::unique_ptr<IndirectBuffer> m_staticDrawArgs;
        std::unique_ptr<IndirectBuffer> m_animatedDrawArgs;

};


#endif