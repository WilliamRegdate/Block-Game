#include "EntityManager.h"

#include "Entity.h"
#include "Mesh.h"
#include "Material.h"
#include "Animation.h"

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

bool EntityManager::AddAnimation(BakedAnimationClip& clip, AnimationTracker& tracker)
{
    auto result = m_animations.emplace(clip.ID, tracker);
    return result.second; // false if this asset ID is already registered
}

void EntityManager::RemoveAnimation(uint32_t assetID)
{
    m_animations.erase(assetID);
}

AnimationTracker* EntityManager::GetAnimation(uint32_t assetID)
{
    auto it = m_animations.find(assetID);
    return it != m_animations.end() ? &it->second : nullptr;
}

bool EntityManager::AddChunk(int64_t chunkID, ChunkTracker& tracker)
{
    auto result = m_chunks.emplace(chunkID, tracker);
    return result.second; // false if this chunk is already registered
}

void EntityManager::RemoveChunk(int64_t chunkID)
{
    m_chunks.erase(chunkID);
}

ChunkTracker* EntityManager::GetChunk(int64_t chunkID)
{
    auto it = m_chunks.find(chunkID);
    return it != m_chunks.end() ? &it->second : nullptr;
}