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
    config.materialTextureWidth  = 512;
    config.materialTextureHeight = 512;
    config.materialTextureFormat = GL_RGBA8;

    // Manager first: the Allocator keeps a pointer to it.
    m_entityManager = std::make_unique<EntityManager>();
    m_allocator     = std::make_unique<Allocator>(config, m_entityManager.get());

    // TODO(dirty): create m_dirtyTracker here.
}

void Renderer::Shutdown()
{
    if (m_allocator)
        m_allocator->Shutdown(); // delete GL objects while the context is still alive

    // Allocator before manager, since the allocator points at the manager.
    m_allocator.reset();
    m_entityManager.reset();
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
    return true;
}

void Renderer::UpdateEntity(Entity& entity)
{
    if (!m_allocator)
        return;

    // TODO(dirty): mark the entity dirty and let FlushDirty upload it once per frame.
    // Uploading immediately is fine until then. Does nothing if the entity isn't registered.
    m_allocator->UploadEntityData(entity);
}

void Renderer::RemoveEntity(Entity& entity)
{
    if (!m_allocator)
        return;

    // Deallocate also frees the mesh/material once their last user is gone.
    // TODO(dirty): clear any pending dirty flag for this entity.
    m_allocator->Deallocate(entity);
}

// ============================================================
// Per-frame (all unimplemented)
// ============================================================

void Renderer::Render()
{
    // TODO: FlushDirty(); CullPass(); DrawStatic(); DrawAnimated(); DrawChunks();
}

void Renderer::FlushDirty()  { /* TODO(dirty) */ }
void Renderer::CullPass()    { /* TODO(cull) */ }
void Renderer::DrawStatic()  { /* TODO(draw) */ }
void Renderer::DrawAnimated(){ /* TODO(draw) */ }
void Renderer::DrawChunks()  { /* TODO(chunks) */ }