// EditorUI.cpp

#include "EditorUI.h"
#include "Transform.h"

#include "imgui.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

namespace RYRayTracing {

EditorUI::EditorUI(const EditorUIContext& ctx) : m_ctx(ctx) {}

void EditorUI::draw()
{
    ImGui::Begin("Transform");
    ImGui::SetWindowFontScale(1.4f);

    ImGui::Text("FPS: %.1f", *m_ctx.currentFps);
    ImGui::Spacing();

    drawCameraPanel();
    drawModelRefsPanel();
    drawLightsPanel();
    drawSpheresPanel();
    drawMaterialsPanel();
    drawGlobalIlluminationPanel();

    ImGui::End();
}

void EditorUI::drawCameraPanel()
{
    ImGui::Text("Camera");
    ImGui::Separator();
    bool camChanged = false;
    camChanged |= ImGui::DragFloat3("Translation", glm::value_ptr(m_ctx.cameraTransform->translation), 0.01f);
    camChanged |= ImGui::DragFloat3("Rotation", glm::value_ptr(m_ctx.cameraTransform->rotation), 0.01f);
    float fov = glm::degrees(m_ctx.camera->GetPerspectiveVerticalFOV());
    if (ImGui::DragFloat("FOV", &fov, 0.01f)) {
        m_ctx.camera->SetPerspectiveVerticalFOV(glm::radians(fov));
        camChanged = true;
    }
    if (camChanged) m_ctx.setAccumDirty();
}

void EditorUI::drawModelRefsPanel()
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Model Refs");
    ImGui::Separator();

    ImGui::PushID("##ModelRefsPanel");
    for (int i = 0; i < static_cast<int>(m_ctx.modelRefs->size()); i++) {
        ImGui::PushID(i);
        auto& src = (*m_ctx.modelSources)[(*m_ctx.modelRefSourceIdx)[i]];
        std::string header = src.name + " [" + std::to_string(i) + "]";
        if (ImGui::CollapsingHeader(header.c_str())) {
            bool changed = false;
            auto& t = (*m_ctx.modelRefTransforms)[i];
            changed |= ImGui::DragFloat3("Translation", glm::value_ptr(t.translation), 0.05f);
            changed |= ImGui::DragFloat3("Rotation", glm::value_ptr(t.rotation), 0.01f);
            float sx = t.scale.x;
            if (ImGui::DragFloat("Scale", &sx, 0.01f)) { t.scale = glm::vec3(sx); changed = true; }
            changed |= ImGui::ColorEdit4("Color", glm::value_ptr((*m_ctx.modelRefs)[i].color));

            std::vector<std::string> matNameStorage;
            for (size_t m = 0; m < m_ctx.materials->size(); m++) {
                matNameStorage.push_back("Material " + std::to_string(m));
            }
            std::vector<const char*> matNames;
            for (auto& s : matNameStorage) matNames.push_back(s.c_str());

            int matIdx = static_cast<int>((*m_ctx.modelRefs)[i].materialId);
            if (ImGui::Combo("Material", &matIdx, matNames.data(), static_cast<int>(matNames.size()))) {
                (*m_ctx.modelRefs)[i].materialId = static_cast<uint32_t>(matIdx);
                changed = true;
            }

            if (changed) {
                (*m_ctx.modelRefs)[i].invTransform = glm::inverse(t.transform());
                m_ctx.setModelRefsDirty();
            }
            if (ImGui::Button("Remove")) {
                m_ctx.modelRefs->erase(m_ctx.modelRefs->begin() + i);
                m_ctx.modelRefSourceIdx->erase(m_ctx.modelRefSourceIdx->begin() + i);
                m_ctx.modelRefTransforms->erase(m_ctx.modelRefTransforms->begin() + i);
                m_ctx.setModelRefsDirty();
                ImGui::PopID();
                continue;
            }
        }
        ImGui::PopID();
    }

    if (m_ctx.modelRefs->size() < m_ctx.maxModelRefs && !m_ctx.modelSources->empty()) {
        static int selectedSource = 0;
        if (selectedSource >= static_cast<int>(m_ctx.modelSources->size()))
            selectedSource = 0;
        std::vector<std::string> srcNames;
        std::vector<const char*> srcItems;
        for (size_t s = 0; s < m_ctx.modelSources->size(); s++) {
            srcNames.push_back((*m_ctx.modelSources)[s].name);
            srcItems.push_back(srcNames.back().c_str());
        }
        ImGui::SetNextItemWidth(200);
        ImGui::Combo("##srcCombo", &selectedSource, srcItems.data(), static_cast<int>(srcItems.size()));
        ImGui::SameLine();
        if (ImGui::Button("Add ModelRef")) {
            const auto& src = (*m_ctx.modelSources)[selectedSource];
            ModelRef ref{};
            ref.vertexOffset = src.vertexOffset;
            ref.firstIndex = src.firstIndex;
            ref.indexCount = src.indexCount;
            ref.boundingSphereCenter = src.boundingSphereCenter;
            ref.boundingSphereRadius = src.boundingSphereRadius;
            ref.textureIndex = selectedSource;
            ref.bvhRoot = src.bvhRoot;
            m_ctx.modelRefs->push_back(ref);
            m_ctx.modelRefSourceIdx->push_back(selectedSource);
            m_ctx.modelRefTransforms->push_back(Transform{});
            m_ctx.setModelRefsDirty();
        }
    }
    ImGui::PopID();
}

void EditorUI::drawLightsPanel()
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Lights");
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(m_ctx.lights->size()); i++) {
        ImGui::PushID(i);
        const char* typeNames[] = {"Point", "Spot", "Directional"};
        auto& l = (*m_ctx.lights)[i];
        std::string header = "Light " + std::to_string(i) + " (" + typeNames[l.type] + ")";
        if (ImGui::CollapsingHeader(header.c_str())) {
            bool changed = false;
            int type = l.type;
            if (ImGui::Combo("Type", &type, typeNames, 3)) { l.type = type; changed = true; }
            changed |= ImGui::ColorEdit3("Color", glm::value_ptr(l.color));
            changed |= ImGui::DragFloat("Intensity", &l.intensity, 0.1f, 0.0f, 100.0f);
            if (l.type != 2) {
                changed |= ImGui::DragFloat3("Position", glm::value_ptr(l.position), 0.1f);
                changed |= ImGui::DragFloat("Max Distance", &l.maxDistance, 0.1f, 0.0f, 200.0f);
            }
            if (l.type >= 1) {
                changed |= ImGui::DragFloat3("Direction", glm::value_ptr(l.direction), 0.1f);
            }
            if (l.type == 1) {
                float innerDeg = glm::degrees(std::acos(l.innerCos));
                float outerDeg = glm::degrees(std::acos(l.outerCos));
                if (ImGui::DragFloat("Inner Angle", &innerDeg, 0.5f, 0.0f, 90.0f)) {
                    l.innerCos = std::cos(glm::radians(innerDeg));
                    changed = true;
                }
                if (ImGui::DragFloat("Outer Angle", &outerDeg, 0.5f, 0.0f, 90.0f)) {
                    l.outerCos = std::cos(glm::radians(outerDeg));
                    changed = true;
                }
            }
            if (changed) m_ctx.setLightsDirty();
            if (ImGui::Button("Remove Light")) {
                m_ctx.lights->erase(m_ctx.lights->begin() + i);
                m_ctx.setLightsDirty();
                ImGui::PopID();
                continue;
            }
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Add Light")) {
        LightData l{};
        l.type = 0;
        l.position = {0.0f, 3.0f, 0.0f};
        l.color = {1.0f, 1.0f, 1.0f};
        l.intensity = 10.0f;
        l.maxDistance = 20.0f;
        l.direction = {0.0f, -1.0f, 0.0f};
        l.innerCos = std::cos(glm::radians(15.0f));
        l.outerCos = std::cos(glm::radians(30.0f));
        m_ctx.lights->push_back(l);
        m_ctx.setLightsDirty();
    }
}

void EditorUI::drawSpheresPanel()
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("RT Spheres");
    ImGui::Separator();

    ImGui::PushID("##SpheresPanel");
    for (int i = 0; i < static_cast<int>(m_ctx.spheres->size()); i++) {
        ImGui::PushID(i);
        std::string header = "Sphere " + std::to_string(i);
        if (ImGui::CollapsingHeader(header.c_str())) {
            bool changed = false;
            changed |= ImGui::DragFloat3("Center", glm::value_ptr((*m_ctx.spheres)[i].center), 0.05f);
            changed |= ImGui::DragFloat("Radius", &(*m_ctx.spheres)[i].radius, 0.05f, 0.01f, 100.0f);

            if (m_ctx.materials->empty()) {
                ImGui::TextDisabled("No materials available");
            } else {
                std::vector<std::string> matNameStorage;
                for (size_t m = 0; m < m_ctx.materials->size(); m++)
                    matNameStorage.push_back("Material " + std::to_string(m));
                std::vector<const char*> matNames;
                for (auto& s : matNameStorage) matNames.push_back(s.c_str());

                int matIdx = static_cast<int>((*m_ctx.spheres)[i].materialIndex);
                if (ImGui::Combo("Material", &matIdx, matNames.data(), static_cast<int>(matNames.size()))) {
                    (*m_ctx.spheres)[i].materialIndex = static_cast<uint32_t>(matIdx);
                    changed = true;
                }
            }
            if (changed) m_ctx.setSpheresDirty();
            if (ImGui::Button("Remove Sphere")) {
                m_ctx.spheres->erase(m_ctx.spheres->begin() + i);
                m_ctx.setSpheresDirty();
                ImGui::PopID();
                continue;
            }
        }
        ImGui::PopID();
    }
    if (m_ctx.spheres->size() < m_ctx.maxSpheres) {
        if (ImGui::Button("Add Sphere")) {
            // Auto-create a default material if none exist
            if (m_ctx.materials->empty() && m_ctx.materials->size() < m_ctx.maxMaterials) {
                m_ctx.materials->push_back({
                    glm::vec3(0.5f, 0.5f, 0.5f),
                    0.0f,
                    glm::vec3(0.0f),
                    0.0f,
                    0.5f,
                    1.5f
                });
                m_ctx.setMaterialsDirty();
            }
            m_ctx.spheres->push_back({glm::vec3(0.0f, -2.0f, -8.0f), 1.0f, 0, {0.0f, 0.0f, 0.0f}});
            m_ctx.setSpheresDirty();
        }
    }
    ImGui::PopID();
}

void EditorUI::drawMaterialsPanel()
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Materials");
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(m_ctx.materials->size()); i++) {
        ImGui::PushID(i);
        std::string header = "Material " + std::to_string(i);
        if (ImGui::CollapsingHeader(header.c_str())) {
            bool changed = false;
            auto& mat = (*m_ctx.materials)[i];
            changed |= ImGui::ColorEdit3("Diffuse Color", glm::value_ptr(mat.diffuseColor));
            changed |= ImGui::DragFloat("Transparency", &mat.transparency, 0.01f, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::ColorEdit3("Emission", glm::value_ptr(mat.emission));
            changed |= ImGui::DragFloat("Metallic", &mat.metallic, 0.01f, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::DragFloat("Roughness", &mat.roughness, 0.01f, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::DragFloat("IOR", &mat.ior, 0.01f, 1.0f, 3.0f, "%.3f");
            if (changed) m_ctx.setMaterialsDirty();
            if (ImGui::Button("Remove Material")) {
                m_ctx.materials->erase(m_ctx.materials->begin() + i);
                m_ctx.setMaterialsDirty();
                ImGui::PopID();
                continue;
            }
        }
        ImGui::PopID();
    }
    if (m_ctx.materials->size() < m_ctx.maxMaterials) {
        if (ImGui::Button("Add Material")) {
            m_ctx.materials->push_back({
                glm::vec3(0.5f, 0.5f, 0.5f),
                0.0f,
                glm::vec3(0.0f),
                0.0f,
                0.5f,
                1.5f
            });
            m_ctx.setMaterialsDirty();
        }
    }
}

void EditorUI::drawGlobalIlluminationPanel()
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Global Illumination");
    ImGui::Separator();
    ImGui::DragFloat("Ambient Strength", m_ctx.ambientStrength, 0.01f, 0.0f, 1.0f);
}

} // namespace RYRayTracing
