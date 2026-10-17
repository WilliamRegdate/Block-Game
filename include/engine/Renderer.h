#ifndef RENDERER_H
#define RENDERER_H

#include<glad/glad.h>
#include<glm/glm.hpp>
#include<memory>
#include<vector>
#include<cstdint>
#include <cstddef>
#include <glm/gtc/constants.hpp>

#include "Entity.h"
#include "Allocator.h"
#include "EntityManager.h"
#include "RenderTypes.h"
#include "Shader.h"
#include "Camera.h"


// Edge length of a chunk in blocks.
// TODO: CHUNK - take this from the world/chunk config once it exists. It also bounds
// ChunkVertex::position (chunk-local, 0..CHUNK_SIZE, which fits a byte up to 255).
constexpr int CHUNK_SIZE = 32;

// What the renderer needs to draw one chunk: its meshed quads.
//   vertices      4 per quad, in quad order (quad i owns vertices 4i..4i+3)
//   quadTextures  1 per quad - see the chunk-material TODO in Allocator.cpp for what the value means
// TODO: CHUNK - swap for the chunk mesher's real output type once it exists; the renderer
// only needs these two arrays.
struct ChunkMeshData
{
    std::vector<ChunkVertex> vertices;
    std::vector<uint32_t>    triangleTextures;
    std::vector<uint32_t>    indices;
};


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
        // Chunks are identified by their chunk coordinate (the renderer packs it into the
        // allocator's chunk ID), not by a handle - callers already have the coordinate, and a
        // chunk's GPU ranges change on every remesh so they make a poor identity.
        //
        // AddChunk registers the chunk and uploads its mesh. Returns false if the coordinate is
        // already added or there's no GPU space; nothing is left registered on failure.
        bool        AddChunk(const glm::ivec3& chunkCoord, const ChunkMeshData& meshData);
        // Replaces the chunk's mesh (a remesh). On failure the chunk keeps its old mesh.
        bool        UpdateChunk(const glm::ivec3& chunkCoord, const ChunkMeshData& meshData);
        void        RemoveChunk(const glm::ivec3& chunkCoord);
        bool        AddChunkTexture(Texture texture, uint32_t& layer); //layer of the texture is output

        // ---------------- Per-frame ----------------
        void Render();

        void SetTimeOfDay(float t) { m_timeOfDay = t - std::floor(t); }   // 0..1
        glm::vec3 GetSunDirection() const;                                // unit vector pointing TOWARD the sun

    protected:

        void FlushDirty();          // push DirtyTracker's entries to their GPU buffers
        void CullPass();            // dispatch compute cull -> visible-index buffers
        void DrawSky();
        void DrawStatic();          // bind static pipeline, issue multidraw indirect
        void DrawAnimated();        // bind animated pipeline, issue multidraw indirect
        void DrawChunks();          // bind the chunk pipeline, issue multidraw indirect

        void UploadCameraMatrix(Shader& shader);

        void BuildDrawLists();
        void BuildChunkDrawList();  // one command per chunk that has a mesh; only does work when m_chunkListDirty

    private:
        // ---------------- Bookkeeping ----------------

        std::unique_ptr<Shader>        m_staticShader; // shader program for the static entity draws
        std::unique_ptr<Shader>        m_animatedShader; // shader program for skinned entity draws
        std::unique_ptr<Shader>        m_chunkShader; // shader program for chunk draws

        std::unique_ptr<EntityManager> m_entityManager;
        std::unique_ptr<Allocator>     m_allocator;

        std::unique_ptr<VAO>           m_staticVAO;
        std::unique_ptr<IndirectBuffer> m_staticIndirect;      // DrawIndexedIndirectCommand[maxMeshTypes], indexed by mesh descriptor slot
        std::unique_ptr<SSBO>          m_staticVisibleIndices; // uint32 instance slots, maxEntities long
        uint32_t                       m_staticDrawCount = 0;  // commands to issue; set by whatever fills the buffers

        std::unique_ptr<VAO>           m_animatedVAO;
        std::unique_ptr<IndirectBuffer> m_animatedIndirect;      // DrawIndexedIndirectCommand[maxMeshTypes], indexed by mesh descriptor slot
        std::unique_ptr<SSBO>          m_animatedVisibleIndices; // uint32 instance slots, maxEntities long
        uint32_t                       m_animatedDrawCount = 0;  // commands to issue; set by whatever fills the buffers

        std::unique_ptr<VAO>           m_chunkVAO;
        std::unique_ptr<IndirectBuffer> m_chunkIndirect;      // DrawIndexedIndirectCommand[chunkCapacity], one per drawn chunk, packed from index 0
        std::unique_ptr<SSBO>          m_chunkVisibleIndices; // uint32 chunk instance slots, chunkCapacity long; entry i belongs to command i
        uint32_t                       m_chunkDrawCount = 0;  // commands to issue; set by BuildChunkDrawList

        Camera* m_camera = nullptr;

        std::vector<DrawIndexedIndirectCommand> m_staticCommands;  // CPU copy, maxMeshTypes long
        std::vector<uint32_t>                   m_staticCursor;    // scratch, maxMeshTypes long
        std::vector<uint32_t>                   m_staticVisible;   // CPU copy, maxEntities long

        std::vector<DrawIndexedIndirectCommand> m_animatedCommands;  // CPU copy, maxMeshTypes long
        std::vector<uint32_t>                   m_animatedCursor;    // scratch, maxMeshTypes long
        std::vector<uint32_t>                   m_animatedVisible;   // CPU copy, maxEntities long

        std::vector<DrawIndexedIndirectCommand> m_chunkCommands;  // CPU copy, chunkCapacity long
        std::vector<uint32_t>                   m_chunkVisible;   // CPU copy, chunkCapacity long

        bool                                    m_listsDirty = true;
        bool                                    m_chunkListDirty = true; // set by Add/Update/RemoveChunk

        struct DirtyInstance
        {
            bool         skinned;
            uint32_t     slot;
            InstanceData data;
        };

        std::vector<Entity*>      m_dirtyEntities;  // marked by UpdateEntity, may contain duplicates
        std::vector<DirtyInstance> m_dirtyStaging;  // scratch, reused every frame
        std::vector<InstanceData>  m_runScratch;    // scratch, reused every frame


        //simple sky shader pass
        std::unique_ptr<Shader> m_skyShader;
        std::unique_ptr<VAO>    m_skyVAO;       // empty VAO: core profile needs one even with no attributes
        float m_timeOfDay = 0.25f;              // 0 = sunrise, 0.25 = noon, 0.5 = sunset, 0.75 = midnight
};


#endif