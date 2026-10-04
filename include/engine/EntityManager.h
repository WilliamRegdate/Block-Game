#ifndef ENTITY_MANAGER_H
#define ENTITY_MANAGER_H

#include<cstdint>
#include<unordered_map>

#include "RenderTypes.h"

// Entity.h, Mesh.h and Material.h aren't included here - only
// pointers/references to them are needed below, and Allocator.h already
// includes all three alongside this header, so forward declarations are
// enough and avoid widening EntityManager.h's own include graph.
class Entity;
class Mesh;
class Material;

struct MeshTracker;     // defined below, EntityTracker only needs the name
struct MaterialTracker; // defined below, EntityTracker only needs the name

// ------------------------------------------------------------
// Per-entity record - what EntityManager stores for one live
// entity. Just data, no behaviour: where it lives, which mesh it
// uses, and which material it uses.
// ------------------------------------------------------------
struct EntityTracker
{
    EntityHandle      handle;                    // where this entity's data lives on the GPU
    MeshTracker*      meshTracker = nullptr;      // which mesh this entity uses
    MaterialTracker*  materialTracker = nullptr;  // which material this entity uses; nullptr if it has none
};

// ------------------------------------------------------------
// Per-mesh-type record - what's known about one unique mesh.
// ------------------------------------------------------------
struct MeshTracker
{
    MeshHandle handle;      // where this mesh's data lives on the GPU
    uint32_t   assetID = 0; // Mesh::ID - the key EntityManager finds this mesh by
    int        users = 0;   // how many entities use the mesh
};

// ------------------------------------------------------------
// Per-material record - what's known about one unique material.
// ------------------------------------------------------------
struct MaterialTracker
{
    MaterialHandle handle;      // where this material's data lives on the GPU
    uint32_t       assetID = 0; // Material::ID - the key EntityManager finds this material by
    int            users = 0;   // how many entities use the material
};
//---------------------------------------------------------------
// Per Animation record
//---------------------------------------------------------------
struct AnimationTracker
{
    AnimationHandle handle;
    int users;
};



class EntityManager
{
    public:
    //iterator helper
        template<typename Fn>
        void ForEachEntity(Fn&& fn) const
        {
            for (const auto& entry : m_entities)
                fn(entry.second);
        }
        // ---- Entities ----
        // Stores `tracker` keyed by &entity. Returns false if this exact
        // Entity is already registered.
        bool AddEntity(Entity& entity, EntityTracker& tracker);
        void RemoveEntity(Entity& entity);
        // Returns nullptr if this exact Entity has no tracker registered.
        EntityTracker* GetEntity(Entity& entity);

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

    private:
        // Keyed by the entity's own address.
        // NOTE: a tracker becomes unreachable - GetEntity/RemoveEntity
        // silently stop finding it - if the Entity is ever moved or
        // reallocated (e.g. stored in a std::vector that resizes) without
        // this map being updated. If entities live somewhere that can
        // reallocate, consider keying by entity.ID instead, the same way
        // m_meshes/m_materials are keyed by their assets' IDs below rather
        // than by address.
        std::unordered_map<Entity*, EntityTracker> m_entities;
        std::unordered_map<uint32_t, MeshTracker>     m_meshes;    // keyed by Mesh::ID
        std::unordered_map<uint32_t, MaterialTracker> m_materials; // keyed by Material::ID
        std::unordered_map<uint32_t, AnimationTracker> m_animations; // keyed by hashed animation name
};

#endif