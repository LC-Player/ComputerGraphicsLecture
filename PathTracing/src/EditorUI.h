// EditorUI.h
#pragma once

#include <functional>
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>
#include "Scene.h"
#include "Light.h"
#include "Camera.h"

struct Transform;

namespace RYRayTracing {

struct EditorUIContext {
    // Camera
    Transform* cameraTransform = nullptr;
    SceneCamera* camera = nullptr;

    // Model refs
    std::vector<ModelRef>* modelRefs = nullptr;
    std::vector<int>* modelRefSourceIdx = nullptr;
    std::vector<Transform>* modelRefTransforms = nullptr;
    std::vector<ModelSource>* modelSources = nullptr;
    std::vector<MaterialData>* materials = nullptr;
    size_t maxModelRefs = 64;

    // Lights
    std::vector<LightData>* lights = nullptr;

    // Spheres
    std::vector<SphereData>* spheres = nullptr;
    size_t maxSpheres = 32;

    // Materials
    size_t maxMaterials = 32;

    // Global illumination
    float* ambientStrength = nullptr;

    // FPS (read via pointer each frame)
    const float* currentFps = nullptr;

    // Dirty callbacks
    std::function<void()> setLightsDirty;
    std::function<void()> setSpheresDirty;
    std::function<void()> setMaterialsDirty;
    std::function<void()> setModelRefsDirty;
    std::function<void()> setAccumDirty;
};

class EditorUI {
public:
    explicit EditorUI(const EditorUIContext& ctx);
    void draw();

private:
    EditorUIContext m_ctx;

    void drawCameraPanel();
    void drawModelRefsPanel();
    void drawLightsPanel();
    void drawSpheresPanel();
    void drawMaterialsPanel();
    void drawGlobalIlluminationPanel();
};

} // namespace RYRayTracing
