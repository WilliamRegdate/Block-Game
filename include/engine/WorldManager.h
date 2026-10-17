#ifndef CHUNK_MANAGER_H
#define CHUNK_MANAGER_H

#include "WorldGenerator.h"
#include "ChunkMesher.h"
#include "ResourceController.h"
#include "ChunkStorage.h"
#include "JobSystem.h"


class WorldManager
{
    public:
        uint32_t renderDistance = 16;
        
        explicit WorldManager(Renderer& renderer, ResourceController& manager);
        ~WorldManager();
        
        //gets a chunk from either storage or loaded from storage
        Chunk LoadChunk(glm::ivec3 chunkPos);

        Renderer& renderer;
        ResourceController& resourceController;
        /*
        threaded loads chunks around center at a radius of renderDistance
        @param center
        CHUNKCOORDS at which chunks are loaded around
        */
       void Update(const glm::ivec3& center);   // call once per frame, main thread
       
       
    private:
       
       ChunkStorage storage;
       ChunkMesher mesher;
       WorldGenerator generator;
       
       
       //chunk load specific attributes
       struct GenResult
       {
           glm::ivec3 coord;
           std::shared_ptr<const Chunk> chunk;
        };
        
        
        ChunkNeighbourhood BuildNeighbourhood(const glm::ivec3& c);
        void LoadChunks(const glm::ivec3& center);
        void DrainGenerated();
        void SubmitGeneration();
        
        glm::ivec3              lastCenter{ INT_MAX }; // last tried center for loadChunks
        std::vector<glm::ivec3> pendingGeneration;   // Requested chunks, sorted FURTHEST first (pop_back = nearest)

        //result from worker meshing a chunk
        struct MeshResult
        {
            glm::ivec3    coord;
            uint32_t      version;
            ChunkMeshData data;
        };

        void ScheduleMeshing(const glm::ivec3& center);
        void DrainMeshed();
        void UploadMeshed();

        int                    generatingInFlight = 0;
        int                    meshingInFlight = 0;

        static inline const unsigned WORKERS = std::max(1u, std::thread::hardware_concurrency() - 1);
        const int   MAX_MESH_IN_FLIGHT = WORKERS *2;
        const int   MAX_GEN_IN_FLIGHT = WORKERS *2;

        static constexpr int   MAX_UPLOADS_PER_FRAME = 4;

        std::vector<glm::ivec3> readyToUpload; // stores chunk coords of  chunks readyto be  uploaded

        ResultQueue<GenResult>  genResults; //generation results output from workers
        ResultQueue<MeshResult> meshResults; // mesh results output from workers
        ThreadPool              pool{ WORKERS };  // keep LAST

};




#endif