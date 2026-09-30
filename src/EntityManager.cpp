#include "EntityManager.h"

#include "Entity.h"
#include "Mesh.h"
#include "Material.h"

bool EntityManager::AddEntity(Entity& entity, EntityTracker& tracker)
{
    auto result = m_entities.emplace(&entity, tracker);
    return result.second; // false if this exact Entity is already registered
}

void EntityManager::RemoveEntity(Entity& entity)
{
    m_entities.erase(&entity);
}

EntityTracker* EntityManager::GetEntity(Entity& entity)
{
    auto it = m_entities.find(&entity);
    return it != m_entities.end() ? &it->second : nullptr;
}

bool EntityManager::AddMesh(Mesh& mesh, MeshTracker& tracker)
{
    auto result = m_meshes.emplace(mesh.ID, tracker);
    return result.second; // false if this asset ID is already registered
}

void EntityManager::RemoveMesh(uint32_t assetID)
{
    m_meshes.erase(assetID);
}

MeshTracker* EntityManager::GetMesh(uint32_t assetID)
{
    auto it = m_meshes.find(assetID);
    return it != m_meshes.end() ? &it->second : nullptr;
}

bool EntityManager::AddMaterial(Material& material, MaterialTracker& tracker)
{
    auto result = m_materials.emplace(material.ID, tracker);
    return result.second; // false if this asset ID is already registered
}

void EntityManager::RemoveMaterial(uint32_t assetID)
{
    m_materials.erase(assetID);
}

MaterialTracker* EntityManager::GetMaterial(uint32_t assetID)
{
    auto it = m_materials.find(assetID);
    return it != m_materials.end() ? &it->second : nullptr;
}