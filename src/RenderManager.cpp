#include "RenderManager.h"

#include "VisualInstance.h"
#include "Mesh.h"
#include "Material.h"
#include "Animation.h"

bool RenderManager::AddInstance(VisualInstance& instance, InstanceTracker& tracker)
{
    auto result = m_instances.emplace(&instance, tracker);
    return result.second; // false if this exact Entity is already registered
}

void RenderManager::RemoveInstance(VisualInstance& instance)
{
    m_instances.erase(&instance);
}

InstanceTracker* RenderManager::GetInstance(VisualInstance& instance)
{
    auto it = m_instances.find(&instance);
    return it != m_instances.end() ? &it->second : nullptr;
}

bool RenderManager::AddMesh(Mesh& mesh, MeshTracker& tracker)
{
    auto result = m_meshes.emplace(mesh.ID, tracker);
    return result.second; // false if this asset ID is already registered
}

void RenderManager::RemoveMesh(uint32_t assetID)
{
    m_meshes.erase(assetID);
}

MeshTracker* RenderManager::GetMesh(uint32_t assetID)
{
    auto it = m_meshes.find(assetID);
    return it != m_meshes.end() ? &it->second : nullptr;
}

bool RenderManager::AddMaterial(Material& material, MaterialTracker& tracker)
{
    auto result = m_materials.emplace(material.ID, tracker);
    return result.second; // false if this asset ID is already registered
}

void RenderManager::RemoveMaterial(uint32_t assetID)
{
    m_materials.erase(assetID);
}

MaterialTracker* RenderManager::GetMaterial(uint32_t assetID)
{
    auto it = m_materials.find(assetID);
    return it != m_materials.end() ? &it->second : nullptr;
}

bool RenderManager::AddAnimation(BakedAnimationClip& clip, AnimationTracker& tracker)
{
    auto result = m_animations.emplace(clip.ID, tracker);
    return result.second; // false if this asset ID is already registered
}

void RenderManager::RemoveAnimation(uint32_t assetID)
{
    m_animations.erase(assetID);
}

AnimationTracker* RenderManager::GetAnimation(uint32_t assetID)
{
    auto it = m_animations.find(assetID);
    return it != m_animations.end() ? &it->second : nullptr;
}

bool RenderManager::AddChunk(int64_t chunkID, ChunkTracker& tracker)
{
    auto result = m_chunks.emplace(chunkID, tracker);
    return result.second; // false if this chunk is already registered
}

void RenderManager::RemoveChunk(int64_t chunkID)
{
    m_chunks.erase(chunkID);
}

ChunkTracker* RenderManager::GetChunk(int64_t chunkID)
{
    auto it = m_chunks.find(chunkID);
    return it != m_chunks.end() ? &it->second : nullptr;
}
bool RenderManager::AddChunkTexture(const std::string& path, ChunkTextureTracker& tracker)
{
    auto result = m_chunkTextures.emplace(path, tracker);
    return result.second; // false if this path is already registered
}

void RenderManager::RemoveChunkTexture(const std::string& path)
{
    m_chunkTextures.erase(path);
}

ChunkTextureTracker* RenderManager::GetChunkTexture(const std::string& path)
{
    auto it = m_chunkTextures.find(path);
    return it != m_chunkTextures.end() ? &it->second : nullptr;
}