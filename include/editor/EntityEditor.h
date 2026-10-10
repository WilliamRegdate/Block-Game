#ifndef ENTITY_EDITOR_H
#define ENTITY_EDITOR_H

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Entity.h"
#include "ResourceEditor.h"

class Renderer;
struct GLFWwindow;

// Dear ImGui editor for the blueprints in entities.json.
//
//  - F1 toggles the editor. While it is open the cursor is released and the
//    caller should skip Camera::Inputs (see IsActive()).
//  - Picking a mesh/material rebuilds a live preview entity through
//    ResourceController::CreateEntity, so nothing is loaded by hand here.
//  - "Save" writes the blueprint back through
//    ResourceEditor::SaveBlueprint.
//
// Create it after the Renderer and destroy it before the window / GL context.
class EntityEditor
{
    public:
        EntityEditor(GLFWwindow* window, ResourceEditor& resources, Renderer& renderer);
        ~EntityEditor();

        EntityEditor(const EntityEditor&) = delete;
        EntityEditor& operator=(const EntityEditor&) = delete;

        // Once per frame, before Renderer::Render(): polls F1 and advances the preview.
        void Update(double delta);

        // Once per frame, after Renderer::Render() and before swapping buffers.
        void Render();

        bool IsActive() const { return m_active; }

    private:
        void SetActive(bool active);
        void DrawWindow();
        void DrawEntityTab();
        void DrawMaterialTab();

        void RefreshLists();
        void NewBlueprint();
        void LoadBlueprint(const std::string& id);
        void SaveBlueprint();
        void SyncIdBuffer();

        void NewMaterial();
        void LoadMaterial(const std::string& id);
        void SaveMaterial();
        void SyncMaterialIdBuffer();
        void SetStatus(bool ok, const std::string& message);

        void RespawnPreview();
        void DespawnPreview();

        GLFWwindow* m_window;
        ResourceEditor& m_resources;
        Renderer& m_renderer;

        bool m_active = false;
        bool m_f1WasDown = false;

        // picker contents
        std::vector<std::string> m_entityIDs;
        std::vector<std::string> m_meshIDs;
        std::vector<std::string> m_materialIDs;

        // m_edit is what the UI changes, m_saved is what is currently in the file
        EntityBlueprint m_edit;
        EntityBlueprint m_saved;
        char m_idBuffer[64] = "";

        // material editing (materials.json)
        std::vector<std::string> m_textureFiles;   // image files under textures/
        MaterialBlueprint m_matEdit;
        MaterialBlueprint m_matSaved;
        char m_matIdBuffer[64] = "";

        // preview entity (lives in the scene only while the editor is open)
        Entity m_preview;
        bool m_hasPreview = false;
        glm::vec3 m_previewPosition{0.0f};
        glm::vec3 m_previewRotation{0.0f};   // euler angles in degrees (x = pitch, y = yaw, z = roll)
        std::vector<std::string> m_clipNames;
        int m_clipIndex = -1;
    float m_previewAnimSpeed = 1.0f;     // applied to animationPlayer.speed

        std::string m_status;
        bool m_statusOk = true;
};

#endif