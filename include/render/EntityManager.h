#ifndef ENTITY_MANAGER_H
#define ENTITY_MANAGER_H

#include<cstdint>
#include<unordered_map>

#include "RenderTypes.h"

// ------------------------------------------------------------
// Per-entity record - what EntityManager stores for one live
// entity. Just data, no behaviour: where it lives and which mesh
// it uses.
// ------------------------------------------------------------
struct EntityTracker
{
    EntityHandle handle; // stores information about where it is in the buffer
    MeshTracker* meshTracker; //which mesh this entity uses
};

// ------------------------------------------------------------
// Per-mesh-type record - what's known about one unique mesh.
// ------------------------------------------------------------
struct MeshTracker
{
    MeshHandle handle;
    int users = 0; // how many entities use the mesh
    bool isSkinned;
    int assetID;
};


class EntityManager
{
    public:
        // Returns nullptr if the handle is invalid, unknown, or stale
        // (its generation doesn't match what's currently stored for
        // that slot). This is the direct replacement for a C#-style
        // TryGetValue(key, out value) - a null result IS the failure
        // signal, no separate out-parameter needed.

        bool AddEntity(Entity& entity,EntityTracker& tracker);
        bool AddMesh(Mesh& mesh, MeshTracker& tracker);
        void RemoveEntity(Entity& entity);
        void RemoveMesh(uint32_t assetID);

        //tries to get entity from the map returns nullptr if get fails.
        EntityTracker* GetEntity(Entity& entity);

        // tries to get mesh from the map returns nullptr if get fails.
        MeshTracker* GetMesh(uint32_t meshID);

    private:
        // Keyed by the handle's Index() (a plain uint32_t), not the
        // full handle - the generation check happens separately, against
        // EntityTracker::generation / MeshTracker::generation, inside
        // GetEntity/GetMesh above.
        std::unordered_map<Entity*, EntityTracker> m_entities;
        std::unordered_map<uint32_t, MeshTracker>   m_meshes;
};

#endif