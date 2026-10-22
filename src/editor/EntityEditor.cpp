#include "EntityEditor.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdio>

#include "Mesh.h"
#include "Renderer.h"

namespace
{
    const ImVec4 kRed   (1.0f, 0.4f, 0.4f, 1.0f);
    const ImVec4 kGreen (0.4f, 1.0f, 0.5f, 1.0f);
    const ImVec4 kYellow(1.0f, 0.85f, 0.3f, 1.0f);

    bool Contains(const std::vector<std::string>& items, const std::string& value)
    {
        return std::find(items.begin(), items.end(), value) != items.end();
    }

    // The UI edits rotation as euler degrees; Transform::rotation is assumed to
    // be a glm::quat. If yours is a vec3, assign the euler value directly instead.
    glm::quat ToQuat(const glm::vec3& eulerDegrees)
    {
        return glm::quat(glm::radians(eulerDegrees));
    }

    bool Same(const EntityBlueprint& a, const EntityBlueprint& b)
    {
        return a.id == b.id && a.meshID == b.meshID && a.materialID == b.materialID;
    }

    bool Same(const MaterialBlueprint& a, const MaterialBlueprint& b)
    {
        return a.id == b.id && a.albedoPath == b.albedoPath
            && a.normalPath == b.normalPath && a.ormPath == b.ormPath;
    }

    // One optional texture slot. Lists the image files under textures/ and
    // stores the full relative path in the same format the JSON uses
    // ("../resources/textures/chicken.png"). "<none>" leaves the slot empty.
    void TextureSlot(const char* label, std::string& path,
                     const std::string& textureDir, const std::vector<std::string>& files)
    {
        const bool inDir = !path.empty() && path.rfind(textureDir, 0) == 0;
        const std::string shortName = inDir ? path.substr(textureDir.size()) : path;

        const char* preview = path.empty() ? "<none>" : shortName.c_str();
        if (ImGui::BeginCombo(label, preview))
        {
            if (ImGui::Selectable("<none>", path.empty()))
                path.clear();

            for (size_t i = 0; i < files.size(); ++i)
            {
                const std::string full = textureDir + files[i];
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(files[i].c_str(), full == path))
                    path = full;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        // A path that points outside textures/, or at a file that is gone.
        if (!path.empty() && (!inDir || !Contains(files, shortName)))
            ImGui::TextColored(kRed, "File not found: %s", path.c_str());
    }

    // Combo box over a list of strings. Returns true when the user picked a
    // different item (and writes it into `current`).
    bool ComboStrings(const char* label, std::string& current, const std::vector<std::string>& items)
    {
        bool changed = false;
        const char* preview = current.empty() ? "<none>" : current.c_str();
        if (ImGui::BeginCombo(label, preview))
        {
            for (size_t i = 0; i < items.size(); ++i)
            {
                const bool selected = (items[i] == current);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(items[i].c_str(), selected) && !selected)
                {
                    current = items[i];
                    changed = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        return changed;
    }
}


EntityEditor::EntityEditor(GLFWwindow* window, ResourceEditor& resources, Renderer& renderer)
    : m_window(window), m_resources(resources), m_renderer(renderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    RefreshLists();
    NewBlueprint();
    NewMaterial();
}

EntityEditor::~EntityEditor()
{
    DespawnPreview();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}


// ============================================================
// Per-frame
// ============================================================

void EntityEditor::Update(double delta)
{
    const bool f1Down = glfwGetKey(m_window, GLFW_KEY_F1) == GLFW_PRESS;
    if (f1Down && !m_f1WasDown)
        SetActive(!m_active);
    m_f1WasDown = f1Down;

    if (m_active && m_hasPreview)
    {
        m_renderer.UpdateInstance(m_preview.instance);
        m_preview.instance.animationPlayer.Update(delta);
    }
}

void EntityEditor::Render()
{
    if (!m_active)
        return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    DrawWindow();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void EntityEditor::SetActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;

    if (active)
    {
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        RefreshLists();
        RespawnPreview();
    }
    else
    {
        DespawnPreview();

        // Camera::Inputs measures mouse movement from the window centre; put
        // the cursor back there so the view doesn't jump when control returns.
        int w = 0, h = 0;
        glfwGetWindowSize(m_window, &w, &h);
        glfwSetCursorPos(m_window, w / 2.0, h / 2.0);
        // Camera::Inputs hides the cursor again on its next call.
    }
}


// ============================================================
// UI
// ============================================================

void EntityEditor::DrawWindow()
{
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0f, 520.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Entity Editor"))
    {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("F1: close the editor and return to the camera");

    if (ImGui::BeginTabBar("EditorTabs"))
    {
        if (ImGui::BeginTabItem("Entity"))
        {
            DrawEntityTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Material"))
        {
            DrawMaterialTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (!m_status.empty())
    {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, m_statusOk ? kGreen : kRed);
        ImGui::TextWrapped("%s", m_status.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::End();
}

void EntityEditor::DrawEntityTab()
{
    // ---- blueprint selection ----
    ImGui::Separator();
    ImGui::TextUnformatted("Blueprint");

    std::string loaded = m_saved.id;
    if (ComboStrings("Load", loaded, m_entityIDs))
        LoadBlueprint(loaded);

    if (ImGui::Button("New"))
        NewBlueprint();
    ImGui::SameLine();
    if (ImGui::Button("Rescan files"))
        RefreshLists();

    // ---- properties ----
    ImGui::Separator();
    ImGui::TextUnformatted("Properties");

    if (ImGui::InputText("ID", m_idBuffer, sizeof(m_idBuffer)))
        m_edit.id = m_idBuffer;

    const bool idExists = Contains(m_entityIDs, m_edit.id);
    ImGui::TextDisabled("%s", idExists ? "Saving overwrites this entry in entities.json"
                                       : "Saving adds a new entry to entities.json");

    bool rebuild = false;
    rebuild |= ComboStrings("Mesh",     m_edit.meshID,     m_meshIDs);
    rebuild |= ComboStrings("Material", m_edit.materialID, m_materialIDs);

    const bool meshOk     = Contains(m_meshIDs,     m_edit.meshID);
    const bool materialOk = Contains(m_materialIDs, m_edit.materialID);
    if (!meshOk)
        ImGui::TextColored(kRed, "Mesh \"%s\" not found in models/", m_edit.meshID.c_str());
    if (!materialOk)
        ImGui::TextColored(kRed, "Material \"%s\" not found in materials.json", m_edit.materialID.c_str());

    if (rebuild)
        RespawnPreview();

    // ---- preview (not stored in the blueprint) ----
    ImGui::Separator();
    ImGui::TextUnformatted("Preview (not saved)");

    if (ImGui::DragFloat3("Position", glm::value_ptr(m_previewPosition), 0.1f) && m_hasPreview)
        m_preview.transform.position = m_previewPosition;

    if (ImGui::DragFloat3("Rotation", glm::value_ptr(m_previewRotation), 1.0f, -360.0f, 360.0f, "%.1f deg") && m_hasPreview)
        m_preview.transform.rotation = ToQuat(m_previewRotation);

    if (m_clipNames.empty())
    {
        ImGui::TextDisabled("No animations on this mesh");
    }
    else
    {
        const char* current = (m_clipIndex >= 0 && m_clipIndex < static_cast<int>(m_clipNames.size()))
                                  ? m_clipNames[m_clipIndex].c_str() : "<none>";
        if (ImGui::BeginCombo("Animation", current))
        {
            for (int i = 0; i < static_cast<int>(m_clipNames.size()); ++i)
            {
                ImGui::PushID(i);
                if (ImGui::Selectable(m_clipNames[i].c_str(), i == m_clipIndex))
                {
                    m_clipIndex = i;
                    if (m_hasPreview)
                        m_preview.instance.animationPlayer.Play(m_clipNames[i].c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        // Ctrl+click the slider to type an exact value.
        if (ImGui::SliderFloat("Anim speed", &m_previewAnimSpeed, 0.0f, 100.0f, "%.2fx") && m_hasPreview)
            m_preview.instance.animationPlayer.speed = m_previewAnimSpeed;
    }

    // ---- save ----
    ImGui::Separator();

    const bool canSave = !m_edit.id.empty() && meshOk && materialOk;
    const bool dirty   = !Same(m_edit, m_saved);

    ImGui::BeginDisabled(!canSave);
    if (ImGui::Button("Save to entities.json"))
        SaveBlueprint();
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        if (m_saved.id.empty())
            NewBlueprint();
        else
            LoadBlueprint(m_saved.id);
    }

    if (dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(kYellow, "unsaved changes");
    }

}

void EntityEditor::DrawMaterialTab()
{
    ImGui::TextDisabled("Edits to a material that is already loaded apply after a restart.");
    ImGui::TextDisabled("A new material can be used on an entity straight after saving.");

    // ---- material selection ----
    ImGui::Separator();
    ImGui::TextUnformatted("Material");

    std::string loaded = m_matSaved.id;
    if (ComboStrings("Load##mat", loaded, m_materialIDs))
        LoadMaterial(loaded);

    if (ImGui::Button("New##mat"))
        NewMaterial();
    ImGui::SameLine();
    if (ImGui::Button("Rescan files##mat"))
        RefreshLists();

    // ---- properties ----
    ImGui::Separator();
    ImGui::TextUnformatted("Properties");

    if (ImGui::InputText("ID##mat", m_matIdBuffer, sizeof(m_matIdBuffer)))
        m_matEdit.id = m_matIdBuffer;

    const bool idExists = Contains(m_materialIDs, m_matEdit.id);
    ImGui::TextDisabled("%s", idExists ? "Saving overwrites this entry in materials.json"
                                       : "Saving adds a new entry to materials.json");

    // ---- textures (each one optional) ----
    ImGui::Separator();
    ImGui::TextUnformatted("Textures (all optional)");

    const std::string textureDir = m_resources.TextureDir();
    TextureSlot("Albedo", m_matEdit.albedoPath, textureDir, m_textureFiles);
    TextureSlot("Normal", m_matEdit.normalPath, textureDir, m_textureFiles);
    TextureSlot("ORM",    m_matEdit.ormPath,    textureDir, m_textureFiles);

    if (m_textureFiles.empty())
        ImGui::TextColored(kYellow, "No images found in %s", textureDir.c_str());

    // ---- save ----
    ImGui::Separator();

    const bool canSave = !m_matEdit.id.empty();
    const bool dirty   = !Same(m_matEdit, m_matSaved);

    ImGui::BeginDisabled(!canSave);
    if (ImGui::Button("Save to materials.json"))
        SaveMaterial();
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Revert##mat"))
    {
        if (m_matSaved.id.empty())
            NewMaterial();
        else
            LoadMaterial(m_matSaved.id);
    }

    if (dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(kYellow, "unsaved changes");
    }

    // Hand the saved material to the entity being edited.
    const bool usable = !dirty && Contains(m_materialIDs, m_matEdit.id);
    ImGui::BeginDisabled(!usable);
    if (ImGui::Button("Use on current entity"))
    {
        m_edit.materialID = m_matEdit.id;
        RespawnPreview();
        SetStatus(true, "Entity \"" + m_edit.id + "\" now uses material \"" + m_matEdit.id + "\" (save the entity to keep it)");
    }
    ImGui::EndDisabled();
}


// ============================================================
// Blueprint handling
// ============================================================

void EntityEditor::RefreshLists()
{
    m_entityIDs   = m_resources.ListEntityIDs();
    m_meshIDs     = m_resources.ListMeshIDs();
    m_materialIDs = m_resources.ListMaterialIDs();
    m_textureFiles = m_resources.ListTextureFiles();
}

void EntityEditor::NewBlueprint()
{
    m_edit  = EntityBlueprint{};
    m_saved = EntityBlueprint{};
    m_edit.id = "newEntity";
    if (!m_meshIDs.empty())     m_edit.meshID     = m_meshIDs.front();
    if (!m_materialIDs.empty()) m_edit.materialID = m_materialIDs.front();
    SyncIdBuffer();

    if (m_active)
        RespawnPreview();
}

void EntityEditor::LoadBlueprint(const std::string& id)
{
    EntityBlueprint blueprint;
    if (!m_resources.GetBlueprint(id, blueprint))
    {
        SetStatus(false, "Could not load blueprint \"" + id + "\" (see console)");
        return;
    }

    m_edit  = blueprint;
    m_saved = blueprint;
    SyncIdBuffer();
    SetStatus(true, "Loaded \"" + id + "\"");
    RespawnPreview();
}

void EntityEditor::SaveBlueprint()
{
    if (m_resources.SaveBlueprint(m_edit))
    {
        m_saved = m_edit;
        m_entityIDs = m_resources.ListEntityIDs();
        SetStatus(true, "Saved \"" + m_edit.id + "\" to " + m_resources.EntityFilePath());
    }
    else
    {
        SetStatus(false, "Save failed (see console)");
    }
}

void EntityEditor::SyncIdBuffer()
{
    std::snprintf(m_idBuffer, sizeof(m_idBuffer), "%s", m_edit.id.c_str());
}


// ============================================================
// Material handling
// ============================================================

void EntityEditor::NewMaterial()
{
    m_matEdit  = MaterialBlueprint{};
    m_matSaved = MaterialBlueprint{};
    m_matEdit.id = "newMaterial";
    SyncMaterialIdBuffer();
}

void EntityEditor::LoadMaterial(const std::string& id)
{
    MaterialBlueprint material;
    if (!m_resources.GetMaterialBlueprint(id, material))
    {
        SetStatus(false, "Could not load material \"" + id + "\" (see console)");
        return;
    }

    m_matEdit  = material;
    m_matSaved = material;
    SyncMaterialIdBuffer();
    SetStatus(true, "Loaded material \"" + id + "\"");
}

void EntityEditor::SaveMaterial()
{
    if (m_resources.SaveMaterialBlueprint(m_matEdit))
    {
        m_matSaved = m_matEdit;
        m_materialIDs = m_resources.ListMaterialIDs();
        SetStatus(true, "Saved material \"" + m_matEdit.id + "\" to " + m_resources.MaterialFilePath());
    }
    else
    {
        SetStatus(false, "Material save failed (see console)");
    }
}

void EntityEditor::SyncMaterialIdBuffer()
{
    std::snprintf(m_matIdBuffer, sizeof(m_matIdBuffer), "%s", m_matEdit.id.c_str());
}

void EntityEditor::SetStatus(bool ok, const std::string& message)
{
    m_statusOk = ok;
    m_status = message;
}


// ============================================================
// Preview entity
// ============================================================

void EntityEditor::RespawnPreview()
{
    DespawnPreview();

    if (m_edit.meshID.empty() || m_edit.materialID.empty())
        return;

    bool failed = false;
    Entity entity = m_resources.CreateEntity(m_edit, failed);
    if (failed)
    {
        SetStatus(false, "Preview failed: could not build \"" + m_edit.id + "\" (see console)");
        return;
    }

    m_preview = entity;
    m_preview.transform.position = m_previewPosition;
    m_preview.transform.rotation = ToQuat(m_previewRotation);
    m_preview.instance.animationPlayer.speed = m_previewAnimSpeed;
    m_renderer.AddInstance(m_preview.instance);          // ADAPT if your Renderer API differs
    m_hasPreview = true;

    // Collect the mesh's clips; keep the same animation selected across swaps if it exists.
    const std::string previous = (m_clipIndex >= 0 && m_clipIndex < static_cast<int>(m_clipNames.size()))
                                     ? m_clipNames[m_clipIndex] : std::string();
    m_clipNames.clear();
    m_clipIndex = -1;

    if (m_preview.instance.mesh != nullptr && m_preview.instance.mesh->isSkinned)
    {
        const auto* skinned = static_cast<const SkinnedMesh*>(m_preview.instance.mesh);
        for (const auto& clip : skinned->animations)
            m_clipNames.push_back(clip.name);
    }

    if (!m_clipNames.empty())
    {
        auto it = std::find(m_clipNames.begin(), m_clipNames.end(), previous);
        m_clipIndex = (it != m_clipNames.end()) ? static_cast<int>(it - m_clipNames.begin()) : 0;
        m_preview.instance.animationPlayer.Play(m_clipNames[m_clipIndex].c_str());
    }
}

void EntityEditor::DespawnPreview()
{
    if (!m_hasPreview)
        return;

    m_renderer.RemoveInstance(m_preview.instance);
    m_hasPreview = false;
}