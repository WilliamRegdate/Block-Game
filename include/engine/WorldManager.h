#ifndef CHUNK_MANAGER_H
#define CHUNK_MANAGER_H

#include "WorldGenerator.h"
#include "ChunkMesher.h"
#include "ResourceController.h"
#include "ChunkStorage.h"
#include "JobSystem.h"

#include "AABB.h"

#include "ChunkIO.h"


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

        //fills the overlapping vector with AABBs  that overlap with the given area
        //nothing is added if the area is unloaded
        

        //TODO get collisions
        // collision functionality should be defined in the block's json file
        // for example  if a block is a regular solid 
        // of if a block has another function like applying an effect/ damage to the player
        
        const std::vector<AABB>& GetShapeAt(int x, int y, int z) const;

        void RegisterShape(uint32_t voxelType, std::vector<AABB> shape);
        //decides whether to get from storage of generate new
        const Voxel& GetBlockAt(int x, int y, int z);
        const Voxel& GetBlockAt(const glm::ivec3& pos);

        bool SetBlockAt(const glm::ivec3& pos, const Voxel& voxel);   // false if the chunk isn't loaded
       
    private:
       
        ChunkStorage storage;
        ChunkMesher mesher;
        WorldGenerator generator;
        
        std::unordered_map<uint32_t, std::vector<AABB>> shapeColliders;
        void MarkDirty(const glm::ivec3& chunkCoord, ChunkEntry& entry);
        std::vector<glm::ivec3> dirtyChunks;   // coords awaiting a priority remesh
       
        
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


        struct SaveResult { glm::ivec3 coord; uint32_t version; bool ok; };
        ResultQueue<SaveResult> saveResults;
        void SubmitSaves(const glm::ivec3& center);
        void DrainSaved();
        
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