#ifndef RENDERER_H
#define RENDERER_H

#include<glad/glad.h>
#include<glm/glm.hpp>
#include<memory>
#include<cstdint>
#include<cstddef>

#include "Entity.h"
#include "Allocator.h"
#include "EntityManager.h"
#include "RenderTypes.h"
#include "Shader.h"
#include "Camera.h"

// ------------------------------------------------------------
// Forward declarations - these don't exist yet. This header only
// needs to know they're types; nothing here requires their full
// definition.
// ------------------------------------------------------------
class DirtyTracker;


struct ChunkHandle; 
struct ChunkMeshData; 

using MeshAssetID = uint64_t;   // interned/hashed identifier, assumed to come from your asset system


class Renderer
{
    public:
        Renderer(Camera* camera);   // was Renderer(); you wanted the constructor to take a camera
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

        void UploadCameraMatrix();

        void BuildStaticDrawLists();

    private:
        // ---------------- Bookkeeping ----------------
        
        std::unique_ptr<Shader>        m_staticShader; // shader program for the static entity draws

        std::unique_ptr<EntityManager> m_entityManager;
        std::unique_ptr<Allocator>     m_allocator;
        // TODO(dirty): std::unique_ptr<DirtyTracker> m_dirtyTracker;
        // Left out for now: unique_ptr can't be destroyed while DirtyTracker is
        // an incomplete type, so the member would break compilation.

        std::unique_ptr<VAO>           m_staticVAO;
        std::unique_ptr<IndirectBuffer> m_staticIndirect;      // DrawIndexedIndirectCommand[maxMeshTypes], indexed by mesh descriptor slot
        std::unique_ptr<SSBO>          m_staticVisibleIndices; // uint32 instance slots, maxEntities long
        uint32_t                       m_staticDrawCount = 0;  // commands to issue; set by whatever fills the buffers

        Camera* m_camera = nullptr;

        std::vector<DrawIndexedIndirectCommand> m_staticCommands;  // CPU copy, maxMeshTypes long
        std::vector<uint32_t>                   m_staticCursor;    // scratch, maxMeshTypes long
        std::vector<uint32_t>                   m_staticVisible;   // CPU copy, maxEntities long
        bool                                    m_staticListsDirty = true;


        struct DirtyInstance
        {
            bool         skinned;
            uint32_t     slot;
            InstanceData data;
        };

        std::vector<Entity*>      m_dirtyEntities;  // marked by UpdateEntity, may contain duplicates
        std::vector<DirtyInstance> m_dirtyStaging;  // scratch, reused every frame
        std::vector<InstanceData>  m_runScratch;    // scratch, reused every frame
};


#endif