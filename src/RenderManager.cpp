#include "RenderManager.h"

#include "VisualInstance.h"
#include "Mesh.h"
#include "Material.h"
#include "Animation.h"

bool RenderManager::AddInstance(VisualInstance& entity, InstanceTracker& tracker)
{
    auto result = m_instances.emplace(&entity, tracker);
    return result.second; // false if this exact Entity is already registered
}

void RenderManager::RemoveInstance(VisualInstance& entity)
{
    m_instances.erase(&entity);
}

InstanceTracker* RenderManager::GetInstance(VisualInstance& entity)
{
    auto it = m_instances.find(&entity);
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

bool RenderManage::AddMaterial(Material& material, MaterialTracker& tracker)
{
    auto result = m_materials.emplace(material.ID, tracker);
    return result.second; // false if this asset ID is already registered
}

void RenderManage::RemoveMaterial(uint32_t assetID)
{
    m_materials.erase(assetID);
}

MaterialTracker* RenderManage::GetMaterial(uint32_t assetID)
{
    auto it = m_materials.find(assetID);
    return it != m_materials.end() ? &it->second : nullptr;
}

bool RenderManage::AddAnimation(BakedAnimationClip& clip, AnimationTracker& tracker)
{
    auto result = m_animations.emplace(clip.ID, tracker);
    return result.second; // false if this asset ID is already registered
}

void RenderManage::RemoveAnimation(uint32_t assetID)
{
    m_animations.erase(assetID);
}

AnimationTracker* RenderManage::GetAnimation(uint32_t assetID)
{
    auto it = m_animations.find(assetID);
    return it != m_animations.end() ? &it->second : nullptr;
}

bool RenderManage::AddChunk(int64_t chunkID, ChunkTracker& tracker)
{
    auto result = m_chunks.emplace(chunkID, tracker);
    return result.second; // false if this chunk is already registered
}

void RenderManage::RemoveChunk(int64_t chunkID)
{
    m_chunks.erase(chunkID);
}

ChunkTracker* RenderManage::GetChunk(int64_t chunkID)
{
    auto it = m_chunks.find(chunkID);
    return it != m_chunks.end() ? &it->second : nullptr;
}
bool RenderManage::AddChunkTexture(const std::string& path, ChunkTextureTracker& tracker)
{
    auto result = m_chunkTextures.emplace(path, tracker);
    return result.second; // false if this path is already registered
}

void RenderManage::RemoveChunkTexture(const std::string& path)
{
    m_chunkTextures.erase(path);
}

ChunkTextureTracker* RenderManage::GetChunkTexture(const std::string& path)
{
    auto it = m_chunkTextures.find(path);
    return it != m_chunkTextures.end() ? &it->second : nullptr;
}