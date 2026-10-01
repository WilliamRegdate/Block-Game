#include <algorithm>
#include "Renderer.h"

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
    config.maxMaterials = 32;
    config.materialTextureWidth  = 128;
    config.materialTextureHeight = 128;
    config.materialTextureFormat = GL_RGBA8;

    // Manager first: the Allocator keeps a pointer to it.
    m_entityManager = std::make_unique<EntityManager>();
    m_allocator     = std::make_unique<Allocator>(config, m_entityManager.get());

    // setup shaders
    m_staticShader = std::make_unique<Shader>("../resources/shaders/static.vert", "../resources/shaders/static.frag");

    m_staticIndirect       = std::make_unique<IndirectBuffer>(config.maxMeshTypes * sizeof(DrawIndexedIndirectCommand));
    m_staticVisibleIndices = std::make_unique<SSBO>(config.maxEntities * sizeof(uint32_t));
    m_staticVAO            = std::make_unique<VAO>();

    VBO& vbo = m_allocator->StaticVBO();
    const GLsizeiptr stride = sizeof(StaticVertex);
    m_staticVAO->LinkAttrib(vbo, 0, 3, GL_FLOAT, stride, (void*)offsetof(StaticVertex, position));
    m_staticVAO->LinkAttrib(vbo, 1, 3, GL_FLOAT, stride, (void*)offsetof(StaticVertex, normal));
    m_staticVAO->LinkAttrib(vbo, 2, 2, GL_FLOAT, stride, (void*)offsetof(StaticVertex, uv));
    m_staticVAO->LinkAttrib(*m_staticVisibleIndices, 3, 1, GL_UNSIGNED_INT, sizeof(uint32_t), (void*)0, 1);

    // The element buffer binding is VAO state, and LinkAttrib doesn't set it.
    m_staticVAO->Bind();
    m_allocator->StaticEBO().Bind();
    m_staticVAO->Unbind();   // unbind the VAO first, so EBO::Unbind() can't clear this binding

    m_staticCommands.assign(config.maxMeshTypes, DrawIndexedIndirectCommand{});
    m_staticCursor.assign(config.maxMeshTypes, 0);
    m_staticVisible.assign(config.maxEntities, 0);
}

void Renderer::Shutdown()
{
    if (m_allocator)
        m_allocator->Shutdown(); // delete GL objects while the context is still alive

    if (m_staticVAO)            m_staticVAO->Delete();
    if (m_staticIndirect)       m_staticIndirect->Delete();
    if (m_staticVisibleIndices) m_staticVisibleIndices->Delete();

    // Allocator before manager, since the allocator points at the manager.
    m_allocator.reset();
    m_entityManager.reset();

    //free shaders
    if (m_staticShader)
        m_staticShader->Delete();
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

    EntityTracker tracker = m_allocator->Allocate(entity);
    if (!tracker.handle.IsValid())
        return false;

    bool ok = true;
    if (meshIsNew)     ok = ok && m_allocator->UploadMeshData(*entity.mesh);
    if (materialIsNew) ok = ok && m_allocator->UploadMaterialData(*entity.material);
    ok = ok && m_allocator->UploadEntityData(entity);

    if (!ok)
    {
        // Deallocate drops the user counts and frees the new mesh/material,
        // since this entity was their only user.
        m_allocator->Deallocate(entity);
        return false;
    }
    m_staticListsDirty = true;
    return true;
}

void Renderer::UpdateEntity(Entity& entity)
{
    if (!m_allocator)
        return;

    // Just record it; FlushDirty builds and uploads everything once per frame.
    // Not setting m_staticListsDirty: moving an entity doesn't change which
    // instance slots belong to which mesh, so the draw lists stay valid.
    m_dirtyEntities.push_back(&entity);
}

void Renderer::RemoveEntity(Entity& entity)
{
    if (!m_allocator)
        return;

    // Deallocate also frees the mesh/material once their last user is gone.
    // TODO(dirty): clear any pending dirty flag for this entity.
    m_staticListsDirty = true;
    m_allocator->Deallocate(entity);
}

// ============================================================
// Per-frame (all unimplemented)
// ============================================================

void Renderer::Render()
{
    m_camera->updateMatrix(90, 0.01f, 5000.0f);
    FlushDirty();// CullPass(); DrawAnimated(); DrawChunks();
    DrawStatic();
}
void Renderer::UploadCameraMatrix()
{
    if (m_camera == nullptr || !m_staticShader)
        return;
 
    m_staticShader->SetMat4("viewProjection", m_camera->cameraMatrix);
}

void Renderer::CullPass()    { /* TODO(cull) */ }

void Renderer::DrawStatic()
{
    if (!m_staticShader || !m_allocator || !m_staticVAO || m_staticDrawCount == 0)
        return;

    // Cheap flag check; only does work if a material was uploaded since last draw.
    m_allocator->GenerateMaterialMipmaps();

    m_staticShader->Activate();
    UploadCameraMatrix();                         // needs the program active

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

void Renderer::DrawAnimated(){ /* TODO(draw) */ }
void Renderer::DrawChunks()  { /* TODO(chunks) */ }

void Renderer::BuildStaticDrawLists()
{
    if (!m_staticListsDirty || !m_entityManager)
        return;
    m_staticListsDirty = false;

    std::fill(m_staticCommands.begin(), m_staticCommands.end(), DrawIndexedIndirectCommand{});

    // Pass 1: one command per mesh slot, counting its instances.
    m_entityManager->ForEachEntity([&](const EntityTracker& t)
    {
        if (!t.handle.IsValid() || t.handle.isSkinned || t.meshTracker == nullptr)
            return;

        const MeshHandle& mh = t.meshTracker->handle;
        DrawIndexedIndirectCommand& cmd = m_staticCommands[mh.descriptorRange.start];

        if (cmd.instanceCount == 0) // first instance of this mesh: fill the geometry fields
        {
            cmd.indexCount = mh.indexRange.size;
            cmd.firstIndex = mh.indexRange.start;
            cmd.baseVertex = static_cast<int32_t>(mh.vertexRange.start);
        }
        cmd.instanceCount++;
    });

    // Prefix sum: each mesh gets a contiguous run in the visible-index buffer.
    uint32_t total     = 0;
    uint32_t drawCount = 0; // highest used slot + 1, so trailing empty slots aren't submitted
    for (uint32_t i = 0; i < m_staticCommands.size(); ++i)
    {
        DrawIndexedIndirectCommand& cmd = m_staticCommands[i];
        cmd.baseInstance = total;
        m_staticCursor[i] = total;
        total += cmd.instanceCount;
        if (cmd.instanceCount > 0)
            drawCount = i + 1;
    }

    // Pass 2: write each entity's instance slot into its mesh's run.
    m_entityManager->ForEachEntity([&](const EntityTracker& t)
    {
        if (!t.handle.IsValid() || t.handle.isSkinned || t.meshTracker == nullptr)
            return;

        const uint32_t slot = t.meshTracker->handle.descriptorRange.start;
        m_staticVisible[m_staticCursor[slot]++] = t.handle.instanceRange.start;
    });

    m_staticDrawCount = drawCount;
    if (drawCount == 0)
        return;

    m_staticIndirect->Upload(0, m_staticCommands.data(),
                             static_cast<GLsizeiptr>(drawCount * sizeof(DrawIndexedIndirectCommand)));
    m_staticVisibleIndices->Upload(0, m_staticVisible.data(),
                                   static_cast<GLsizeiptr>(total * sizeof(uint32_t)));
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

    BuildStaticDrawLists(); // from earlier; only does work when the entity set changed
}