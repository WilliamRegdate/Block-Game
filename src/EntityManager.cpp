#include "EntityManager.h"

#include "Entity.h"
#include "Mesh.h"

bool EntityManager::AddEntity(Entity& entity, EntityTracker& tracker)
{
    auto result = m_entities.emplace(&entity, tracker);
    return result.second; // false if this exact Entity is already registered
}

bool EntityManager::AddMesh(Mesh& mesh, MeshTracker& tracker)
{
    auto result = m_meshes.emplace(mesh.ID, tracker);
    return result.second; // false if this asset ID is already registered
}

void EntityManager::RemoveEntity(Entity& entity)
{
    m_entities.erase(&entity);
}

void EntityManager::RemoveMesh(uint32_t assetID)
{
    m_meshes.erase(assetID);
}

EntityTracker* EntityManager::GetEntity(Entity& entity)
{
    auto it = m_entities.find(&entity);
    return it != m_entities.end() ? &it->second : nullptr;
}

MeshTracker* EntityManager::GetMesh(uint32_t assetID)
{
    auto it = m_meshes.find(assetID);
    return it != m_meshes.end() ? &it->second : nullptr;
}