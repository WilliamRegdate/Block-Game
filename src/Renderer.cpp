#include <algorithm>
#include <chrono>
#include <cmath>

#include "Renderer.h"
#include "ChunkHelpers.h"

Renderer::Renderer(Camera* camera)
    : m_camera(camera)
{
    Init();
}

Renderer::~Renderer()
{
    Shutdown();
}

void Renderer::Init()
{
    //setup config
    EntityRendererConfig config;
    config.staticVertexCapacityBytes  = 64 * 1024 * 1024;
    config.staticIndexCapacityBytes   = 32 * 1024 * 1024;
    config.skinnedVertexCapacityBytes = 32 * 1024 * 1024;
    config.skinnedIndexCapacityBytes  = 16 * 1024 * 1024;
    config.poseBufferCapacityBytes    = 16 * 1024 * 1024;
    config.maxEntities  = 65536;
    config.maxMeshTypes = 1024;
    config.maxMaterials = 600;
    config.materialTextureWidth  = 128;
    config.materialTextureHeight = 128;
    config.materialTextureFormat = GL_RGBA8;

    config.chunkCapacity  = 65536;

    // Chunk geometry: 8-byte vertices, 4 per quad, so 128 MiB = 16M vertices = 4M quads.
    // The index buffer has to hold 6 indices per quad (4M * 6 * 4 bytes = 96 MiB). The quad
    // SSBO is sized from the index buffer, so keep these two in this ratio or one runs out first.
    config.chunkVertexCapacityBytes = 128 * 1024 * 1024;
    config.chunkIndexCapacityBytes  =  96 * 1024 * 1024;

    //good enough for now
    config.maxChunkMaterials = 256;

    config.maxAnimations = 1024;

    // Manager first: the Allocator keeps a pointer to it.
    m_entityManager = std::make_unique<EntityManager>();
    m_allocator     = std::make_unique<Allocator>(config, m_entityManager.get());

    // setup shaders
    m_staticShader = std::make_unique<Shader>("../resources/shaders/static.vert", "../resources/shaders/static.frag");
    m_animatedShader = std::make_unique<Shader>("../resources/shaders/skinned.vert", "../resources/shaders/skinned.frag");
    m_chunkShader = std::make_unique<Shader>("../resources/shaders/chunk.vert", "../resources/shaders/chunk.frag");

    m_staticIndirect       = std::make_unique<IndirectBuffer>(config.maxMeshTypes * sizeof(DrawIndexedIndirectCommand));
    m_staticVisibleIndices = std::make_unique<SSBO>(config.maxEntities * sizeof(uint32_t));
    m_staticVAO            = std::make_unique<VAO>();

    m_animatedIndirect     = std::make_unique<IndirectBuffer>(config.maxMeshTypes * sizeof(DrawIndexedIndirectCommand));
    m_animatedVisibleIndices = std::make_unique<SSBO>(config.maxEntities * sizeof(uint32_t));
    m_animatedVAO            = std::make_unique<VAO>();

    m_chunkIndirect     = std::make_unique<IndirectBuffer>(config.chunkCapacity * sizeof(DrawIndexedIndirectCommand));
    m_chunkVisibleIndices = std::make_unique<SSBO>(config.chunkCapacity * sizeof(uint32_t));
    m_chunkVAO            = std::make_unique<VAO>();

    VBO& staticVbo = m_allocator->StaticVBO();
    GLsizeiptr staticStride = sizeof(StaticVertex);

    m_staticVAO->LinkAttrib(staticVbo, 0, 3, GL_FLOAT,staticStride, (void*)offsetof(StaticVertex, position));
    m_staticVAO->LinkAttrib(staticVbo, 1, 3, GL_FLOAT,staticStride, (void*)offsetof(StaticVertex, normal));
    m_staticVAO->LinkAttrib(staticVbo, 2, 2, GL_FLOAT,staticStride, (void*)offsetof(StaticVertex, uv));
    m_staticVAO->LinkAttrib(*m_staticVisibleIndices, 3, 1, GL_UNSIGNED_INT,sizeof(uint32_t), (void*)0, 1);

    // The element buffer binding is VAO state, and LinkAttrib doesn't set it.
    m_staticVAO->Bind();
    m_allocator->StaticEBO().Bind();
    m_staticVAO->Unbind();   // unbind the VAO first, so EBO::Unbind() can't clear this binding

    m_staticCommands.assign(config.maxMeshTypes, DrawIndexedIndirectCommand{});
    m_staticCursor.assign(config.maxMeshTypes, 0);
    m_staticVisible.assign(config.maxEntities, 0);

    VBO& animatedVbo = m_allocator->AnimatedVBO();
    GLsizeiptr animatedStride = sizeof(SkinnedVertex);

    m_animatedVAO->LinkAttrib(animatedVbo, 0, 3, GL_FLOAT,animatedStride, (void*)offsetof(SkinnedVertex, position));
    m_animatedVAO->LinkAttrib(animatedVbo, 1, 3, GL_FLOAT,animatedStride, (void*)offsetof(SkinnedVertex, normal));
    m_animatedVAO->LinkAttrib(animatedVbo, 2, 2, GL_FLOAT,animatedStride, (void*)offsetof(SkinnedVertex, uv));
    m_animatedVAO->LinkAttrib(animatedVbo, 3, 4, GL_UNSIGNED_INT,animatedStride, (void*)offsetof(SkinnedVertex, boneIndices));
    m_animatedVAO->LinkAttrib(animatedVbo, 4, 4, GL_FLOAT,animatedStride, (void*)offsetof(SkinnedVertex, boneWeights));
    m_animatedVAO->LinkAttrib(*m_animatedVisibleIndices, 5, 1, GL_UNSIGNED_INT,sizeof(uint32_t), (void*)0, 1);

    m_animatedVAO->Bind();
    m_allocator->AnimatedEBO().Bind();
    m_animatedVAO->Unbind();

    m_animatedCommands.assign(config.maxMeshTypes, DrawIndexedIndirectCommand{});
    m_animatedCursor.assign(config.maxMeshTypes, 0);
    m_animatedVisible.assign(config.maxEntities, 0);


    // Chunk vertex attributes. ChunkVertex is 8 packed bytes, so every attribute is read as
    // unsigned BYTES (a GL_UNSIGNED_INT read here would pull 4 bytes per component from the
    // wrong offsets). The shader declares them as uint / uint / uvec3 / uvec3, so LinkAttrib
    // must use glVertexAttribIPointer for integer types - the float path would convert them.
    VBO& chunkVbo = m_allocator->ChunkVBO();
    GLsizeiptr chunkStride = sizeof(ChunkVertex);

    m_chunkVAO->LinkAttrib(chunkVbo, 0, 1, GL_UNSIGNED_BYTE,chunkStride, (void*)offsetof(ChunkVertex, normalLight));
    m_chunkVAO->LinkAttrib(chunkVbo, 1, 1, GL_UNSIGNED_BYTE,chunkStride, (void*)offsetof(ChunkVertex, uvLight));
    m_chunkVAO->LinkAttrib(chunkVbo, 2, 3, GL_UNSIGNED_BYTE,chunkStride, (void*)offsetof(ChunkVertex, position));
    m_chunkVAO->LinkAttrib(chunkVbo, 3, 3, GL_UNSIGNED_BYTE,chunkStride, (void*)offsetof(ChunkVertex, color));
    // Per-instance: the chunk's slot in the chunk instance SSBO. Each command draws one
    // instance, and its baseInstance indexes this list.
    m_chunkVAO->LinkAttrib(*m_chunkVisibleIndices, 4, 1, GL_UNSIGNED_INT,sizeof(uint32_t), (void*)0, 1);

    m_chunkVAO->Bind();
    m_allocator->ChunkEBO().Bind();
    m_chunkVAO->Unbind();

    m_chunkCommands.assign(config.chunkCapacity, DrawIndexedIndirectCommand{});
    m_chunkVisible.assign(config.chunkCapacity, 0);

    m_skyShader = std::make_unique<Shader>("../resources/shaders/sky.vert", "../resources/shaders/sky.frag");
    m_skyVAO    = std::make_unique<VAO>();  

}

void Renderer::Shutdown()
{
    if (m_allocator)
        m_allocator->Shutdown(); // delete GL objects while the context is still alive

    if (m_staticVAO)            m_staticVAO->Delete();
    if (m_staticIndirect)       m_staticIndirect->Delete();
    if (m_staticVisibleIndices) m_staticVisibleIndices->Delete();

    if (m_animatedVAO)            m_animatedVAO->Delete();
    if (m_animatedIndirect)       m_animatedIndirect->Delete();
    if (m_animatedVisibleIndices) m_animatedVisibleIndices->Delete();

    if (m_chunkVAO)            m_chunkVAO->Delete();
    if (m_chunkIndirect)       m_chunkIndirect->Delete();
    if (m_chunkVisibleIndices) m_chunkVisibleIndices->Delete();


    // Allocator before manager, since the allocator points at the manager.
    m_allocator.reset();
    m_entityManager.reset();

    //free shaders
    if (m_staticShader)
        m_staticShader->Delete();
    if (m_animatedShader)
        m_animatedShader->Delete();
    if (m_chunkShader)
        m_chunkShader->Delete();

    if (m_skyVAO)    m_skyVAO->Delete();
    if (m_skyShader) m_skyShader->Delete();
}

// ============================================================
// Entities
// ============================================================

bool Renderer::AddEntity(Entity& entity)
{
    if (!m_allocator || entity.mesh == nullptr)
        return false;

    const bool meshIsNew = (m_entityManager->GetMesh(entity.mesh->ID) == nullptr);
    const bool materialIsNew = (entity.material != nullptr) &&
                               (m_entityManager->GetMaterial(entity.material->ID) == nullptr);
    std::vector<bool> areAnimationsNew;
    std::vector<BakedAnimationClip*> animations = entity.animationPlayer.GetClips();
    for (unsigned int i = 0; i < animations.size(); i++)
    {
        areAnimationsNew.push_back(m_entityManager->GetAnimation(animations[i]->ID) == nullptr);
    }

    EntityTracker tracker = m_allocator->Allocate(entity);
    if (!tracker.handle.IsValid())
        return false;

    bool ok = true;
    if (meshIsNew)     ok = ok && m_allocator->UploadMeshData(*entity.mesh);
    if (materialIsNew) ok = ok && m_allocator->UploadMaterialData(*entity.material);
    for (unsigned int i = 0; i < animations.size(); i++)
    {
        if (areAnimationsNew[i]) ok = ok && m_allocator->UploadAnimationData(*animations[i]);
    }

    ok = ok && m_allocator->UploadEntityData(entity);

    if (!ok)
    {
        // Deallocate drops the user counts and frees the new mesh/material,
        // since this entity was their only user.
        m_allocator->Deallocate(entity);
        return false;
    }
    m_listsDirty = true;
    return true;
}

void Renderer::UpdateEntity(Entity& entity)
{
    if (!m_allocator)
        return;
    m_dirtyEntities.push_back(&entity);
}

void Renderer::RemoveEntity(Entity& entity)
{
    if (!m_allocator)
        return;
    m_listsDirty = true;
    m_allocator->Deallocate(entity);
}

// ============================================================
// Chunks
// ============================================================
bool Renderer::AddChunk(const glm::ivec3& chunkCoord, const ChunkMeshData& meshData)
{
    if (!m_allocator)
        return false;

    const uint64_t id = ChunkCoord::ToID(chunkCoord);
    const glm::vec3 worldPosition = glm::vec3(chunkCoord) * static_cast<float>(CHUNK_SIZE);

    ChunkTracker tracker = m_allocator->AllocateChunk(id, worldPosition);
    if (!tracker.handle.IsValid())
        return false; // already added, or out of chunk instance slots

    if (!m_allocator->UploadChunkMesh(id, chunkCoord, meshData.vertices, meshData.indices, meshData.triangleTextures))
    {
        m_allocator->DeallocateChunk(id);
        return false;
    }

    m_chunkListDirty = true;
    return true;
}

bool Renderer::UpdateChunk(const glm::ivec3& chunkCoord, const ChunkMeshData& meshData)
{
    if (!m_allocator)
        return false;

    // On failure the Allocator keeps the chunk's previous mesh, so the draw list stays valid.
    if (!m_allocator->UploadChunkMesh(ChunkCoord::ToID(chunkCoord), chunkCoord, meshData.vertices, meshData.indices, meshData.triangleTextures))
        return false;

    m_chunkListDirty = true;
    return true;
}

void Renderer::RemoveChunk(const glm::ivec3& chunkCoord)
{
    if (!m_allocator)
        return;

    m_chunkListDirty = true;
    m_allocator->DeallocateChunk(ChunkCoord::ToID(chunkCoord));
}

bool Renderer::AddChunkTexture(Texture texture, uint32_t& layer)
{
    layer = Allocator::INVALID_CHUNK_TEXTURE; // what the caller sees if anything below fails
    if (!m_allocator)
    return false;
    layer = m_allocator->AddChunkTexture(texture);
    m_allocator->GenerateChunkTextureMipmaps();
    if (layer == Allocator::INVALID_CHUNK_TEXTURE)
        return false;
    return true;
}

// ============================================================
// Per-frame
// ============================================================

void Renderer::Render()
{
    m_camera->updateMatrix(90, 0.01f, 5000.0f);
    FlushDirty();// CullPass();
    DrawSky();
    DrawStatic();
    DrawAnimated();
    DrawChunks();
}
void Renderer::UploadCameraMatrix(Shader& shader)
{
    if (m_camera == nullptr)
        return;

    shader.SetMat4("viewProjection", m_camera->cameraMatrix);
}

void Renderer::CullPass()    { /* TODO(cull) */ }

glm::vec3 Renderer::GetSunDirection() const
{
    const float a = m_timeOfDay * glm::two_pi<float>();
    return glm::normalize(glm::vec3(std::cos(a), std::sin(a), 0.25f));
}

void Renderer::DrawSky()
{
    if (!m_skyShader || !m_skyVAO || !m_camera)
        return;

    static const auto start = std::chrono::steady_clock::now();
    const float time = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();

    // Sky is a fullscreen triangle drawn first, so it needs no depth test or culling.
    const GLboolean depthWasOn = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullWasOn  = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    m_skyShader->Activate();
    m_skyShader->SetMat4("uInvViewProj", glm::inverse(m_camera->cameraMatrix));
    m_skyShader->SetVec3("uSunDir", GetSunDirection());
    m_skyShader->SetFloat("uTime", time);

    m_skyVAO->Bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    m_skyVAO->Unbind();

    if (depthWasOn) glEnable(GL_DEPTH_TEST);
    if (cullWasOn)  glEnable(GL_CULL_FACE);
}

void Renderer::DrawStatic()
{
    if (!m_staticShader || !m_allocator || !m_staticVAO || m_staticDrawCount == 0)
        return;

    // Cheap flag check; only does work if a material was uploaded since last draw.
    m_allocator->GenerateMaterialMipmaps();

    m_staticShader->Activate();
    UploadCameraMatrix(*m_staticShader);                         // needs the program active

    m_staticShader->SetVec3("uSunDir", GetSunDirection());

    // Shared by every draw - bound once, not per mesh/material.
    m_allocator->StaticInstanceSSBO().BindBase(0);      // layout(std430, binding = 0) in the vert shader
    m_allocator->MaterialDescriptorSSBO().BindBase(1);  // layout(std430, binding = 1) in the frag shader
    m_allocator->MaterialTextures().Bind(0);            // layout(binding = 0) sampler2DArray

    m_staticVAO->Bind();          // static VBO + EBO + visible-index attribute
    m_staticIndirect->Bind();     // GL_DRAW_INDIRECT_BUFFER is global state, not VAO state

    // One command per mesh type. Each command's baseInstance offsets into the
    // visible-index buffer via the divisor-1 attribute, so the shader never sees it.
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr,
                                static_cast<GLsizei>(m_staticDrawCount),
                                0); // stride 0 = tightly packed (DrawIndexedIndirectCommand is 20 bytes)

    m_staticIndirect->Unbind();
    m_staticVAO->Unbind();
}

void Renderer::DrawAnimated()
{
    if (!m_animatedShader || !m_allocator || !m_animatedVAO || m_animatedDrawCount == 0)
        return;

    m_allocator->GenerateMaterialMipmaps();

    m_animatedShader->Activate();
    UploadCameraMatrix(*m_animatedShader);

    m_animatedShader->SetVec3("uSunDir", GetSunDirection());

    // Shared by every draw - bound once, not per mesh/material.
    m_allocator->AnimatedInstanceSSBO().BindBase(0);      // layout(std430, binding = 0) in the vert shader
    m_allocator->MaterialDescriptorSSBO().BindBase(1);    // layout(std430, binding = 1) in the frag shader
    m_allocator->AnimationDescriptorSSBO().BindBase(2);   // layout(std430, binding = 2) in the vert shader
    m_allocator->BonePoseSSBO().BindBase(3);              // layout(std430, binding = 3) in the vert shader

    m_allocator->MaterialTextures().Bind(0);            // layout(binding = 0) sampler2DArray

    m_animatedVAO->Bind();
    m_animatedIndirect->Bind();

    // One command per mesh type. Each command's baseInstance offsets into the
    // visible-index buffer via the divisor-1 attribute, so the shader never sees it.
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr,
                                static_cast<GLsizei>(m_animatedDrawCount),
                                0); // stride 0 = tightly packed (DrawIndexedIndirectCommand is 20 bytes)

    m_animatedIndirect->Unbind();
    m_animatedVAO->Unbind();
}


void Renderer::DrawChunks()
{
    if (!m_chunkShader || !m_allocator || !m_chunkVAO || m_chunkDrawCount == 0)
        return;

    m_allocator->GenerateChunkTextureMipmaps();
    m_chunkShader->Activate();
    UploadCameraMatrix(*m_chunkShader);                         // needs the program active

    m_chunkShader->SetVec3("uSunDir", GetSunDirection());

    // optional fog
    m_chunkShader->SetVec3("uCameraPos", m_camera->position);   // use whatever your Camera calls its position
    m_chunkShader->SetFloat("uFogEnd", 480.0f);                 // your view distance in blocks, or less

    m_allocator->ChunkInstanceSSBO().BindBase(0);       // layout(std430, binding = 0) in the vert shader
    m_allocator->ChunkTriTextureSSBO().BindBase(1);    // layout(std430, binding = 1) in the vert shader
    //m_allocator->MaterialTextures().Bind(0);
    m_allocator->ChunkTextureArray().Bind(0);           // layout(binding = 0) sampler2DArray

    m_chunkVAO->Bind();          // chunk VBO + EBO + visible-index attribute
    m_chunkIndirect->Bind();     // GL_DRAW_INDIRECT_BUFFER is global state, not VAO state

    // One command per chunk, one instance each. Command i's baseInstance is i, so the divisor-1
    // attribute reads m_chunkVisible[i]: that chunk's slot in the chunk instance SSBO.
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr,
                                static_cast<GLsizei>(m_chunkDrawCount),
                                0); // stride 0 = tightly packed (DrawIndexedIndirectCommand is 20 bytes)

    m_chunkIndirect->Unbind();
    m_chunkVAO->Unbind();
}

void Renderer::BuildDrawLists()
{
    if (!m_listsDirty || !m_entityManager)
        return;
    m_listsDirty = false;

    std::fill(m_staticCommands.begin(), m_staticCommands.end(), DrawIndexedIndirectCommand{});
    std::fill(m_animatedCommands.begin(), m_animatedCommands.end(), DrawIndexedIndirectCommand{});

    // Pass 1: one command per mesh slot, counting its instances - static and
    // animated entities route into their own command/cursor arrays based on
    // handle.isSkinned, in the same walk rather than two separate full
    // iterations over every entity.
    m_entityManager->ForEachEntity([&](const EntityTracker& t)
    {
        if (!t.handle.IsValid() || t.meshTracker == nullptr)
            return;

        const MeshHandle& mh = t.meshTracker->handle;
        DrawIndexedIndirectCommand& cmd = t.handle.isSkinned
            ? m_animatedCommands[mh.descriptorRange.start]
            : m_staticCommands[mh.descriptorRange.start];

        if (cmd.instanceCount == 0) // first instance of this mesh: fill the geometry fields
        {
            cmd.indexCount = mh.indexRange.size;
            cmd.firstIndex = mh.indexRange.start;
            cmd.baseVertex = static_cast<int32_t>(mh.vertexRange.start);
        }
        cmd.instanceCount++;
    });

    // Prefix sum: each mesh gets a contiguous run in its own pipeline's
    // visible-index buffer. Static and animated stay independent ranges -
    // they read from different InstanceData SSBOs, so there's no reason
    // for their baseInstance space to overlap or share one counter.
    auto prefixSum = [](std::vector<DrawIndexedIndirectCommand>& commands,
                         std::vector<uint32_t>& cursor) -> std::pair<uint32_t, uint32_t>
    {
        uint32_t total     = 0;
        uint32_t drawCount = 0; // highest used slot + 1, so trailing empty slots aren't submitted
        for (uint32_t i = 0; i < commands.size(); ++i)
        {
            DrawIndexedIndirectCommand& cmd = commands[i];
            cmd.baseInstance = total;
            cursor[i] = total;
            total += cmd.instanceCount;
            if (cmd.instanceCount > 0)
                drawCount = i + 1;
        }
        return { total, drawCount };
    };

    const auto [staticTotal, staticDrawCount]     = prefixSum(m_staticCommands, m_staticCursor);
    const auto [animatedTotal, animatedDrawCount] = prefixSum(m_animatedCommands, m_animatedCursor);

    // Pass 2: write each entity's instance slot into its mesh's run.
    m_entityManager->ForEachEntity([&](const EntityTracker& t)
    {
        if (!t.handle.IsValid() || t.meshTracker == nullptr)
            return;

        const uint32_t slot = t.meshTracker->handle.descriptorRange.start;
        if (t.handle.isSkinned)
            m_animatedVisible[m_animatedCursor[slot]++] = t.handle.instanceRange.start;
        else
            m_staticVisible[m_staticCursor[slot]++] = t.handle.instanceRange.start;
    });

    m_staticDrawCount   = staticDrawCount;
    m_animatedDrawCount = animatedDrawCount;

    if (staticDrawCount > 0)
    {
        m_staticIndirect->Upload(0, m_staticCommands.data(),
                                 static_cast<GLsizeiptr>(staticDrawCount * sizeof(DrawIndexedIndirectCommand)));
        m_staticVisibleIndices->Upload(0, m_staticVisible.data(),
                                       static_cast<GLsizeiptr>(staticTotal * sizeof(uint32_t)));
    }

    if (animatedDrawCount > 0)
    {
        m_animatedIndirect->Upload(0, m_animatedCommands.data(),
                                   static_cast<GLsizeiptr>(animatedDrawCount * sizeof(DrawIndexedIndirectCommand)));
        m_animatedVisibleIndices->Upload(0, m_animatedVisible.data(),
                                         static_cast<GLsizeiptr>(animatedTotal * sizeof(uint32_t)));
    }
}

// Rebuilds the chunk commands: one per chunk that currently has a mesh, packed from index 0
// with no gaps (chunks with no mesh - empty or not meshed yet - are skipped, not drawn).
// Unlike entities there's no instancing to count, since every chunk is its own mesh, so there's
// no prefix sum: command i simply draws instance i, and m_chunkVisible[i] says which chunk.
//
// TODO: CHUNK - this is where culling goes. CullPass would filter this list (frustum, then the
// occlusion/face-connectivity work) instead of every chunk with a mesh being added.
void Renderer::BuildChunkDrawList()
{
    if (!m_chunkListDirty || !m_entityManager)
        return;
    m_chunkListDirty = false;

    uint32_t count = 0;
    m_entityManager->ForEachChunk([&](const ChunkTracker& t)
    {
        const ChunkHandle& h = t.handle;
        if (!h.IsValid() || !h.HasMesh())
            return;
        if (count >= m_chunkCommands.size())
            return; // can't happen while chunkCapacity bounds the instance slots

        DrawIndexedIndirectCommand& cmd = m_chunkCommands[count];
        cmd.indexCount    = h.indexRange.size;
        cmd.instanceCount = 1;
        cmd.firstIndex    = h.indexRange.start;                          // chunk-local indices...
        cmd.baseVertex    = static_cast<int32_t>(h.vertexRange.start);   // ...moved to this chunk's vertices
        cmd.baseInstance  = count;                                       // position in m_chunkVisible, not the slot

        m_chunkVisible[count] = h.instanceRange.start;
        ++count;
    });

    m_chunkDrawCount = count;

    if (count > 0)
    {
        m_chunkIndirect->Upload(0, m_chunkCommands.data(),
                                static_cast<GLsizeiptr>(count * sizeof(DrawIndexedIndirectCommand)));
        m_chunkVisibleIndices->Upload(0, m_chunkVisible.data(),
                                      static_cast<GLsizeiptr>(count * sizeof(uint32_t)));
    }
}

void Renderer::FlushDirty()
{
    if (m_allocator && !m_dirtyEntities.empty())
    {
        // Build the new InstanceData for everything marked dirty. Entities removed
        // since being marked aren't registered any more, so Build fails and they're skipped.
        m_dirtyStaging.clear();
        for (Entity* e : m_dirtyEntities)
        {
            DirtyInstance d;
            EntityHandle handle;
            if (!m_allocator->BuildInstanceData(*e, d.data, handle))
                continue;
            d.skinned = handle.isSkinned;
            d.slot    = handle.instanceRange.start;
            m_dirtyStaging.push_back(d);
        }
        m_dirtyEntities.clear();

        // Sort by (pool, slot), then drop duplicates (same entity updated twice).
        auto keyLess = [](const DirtyInstance& a, const DirtyInstance& b)
        {
            return a.skinned != b.skinned ? a.skinned < b.skinned : a.slot < b.slot;
        };
        std::sort(m_dirtyStaging.begin(), m_dirtyStaging.end(), keyLess);
        m_dirtyStaging.erase(
            std::unique(m_dirtyStaging.begin(), m_dirtyStaging.end(),
                [](const DirtyInstance& a, const DirtyInstance& b)
                { return a.skinned == b.skinned && a.slot == b.slot; }),
            m_dirtyStaging.end());

        // One upload per run of consecutive slots in the same pool.
        const size_t n = m_dirtyStaging.size();
        size_t i = 0;
        while (i < n)
        {
            size_t j = i + 1;
            while (j < n &&
                   m_dirtyStaging[j].skinned == m_dirtyStaging[i].skinned &&
                   m_dirtyStaging[j].slot    == m_dirtyStaging[j - 1].slot + 1)
                ++j;

            m_runScratch.clear();
            for (size_t k = i; k < j; ++k)
                m_runScratch.push_back(m_dirtyStaging[k].data);

            m_allocator->UploadInstanceRange(m_dirtyStaging[i].skinned, m_dirtyStaging[i].slot,
                                             m_runScratch.data(), static_cast<uint32_t>(j - i));
            i = j;
        }
    }

    BuildDrawLists();
    BuildChunkDrawList();
}