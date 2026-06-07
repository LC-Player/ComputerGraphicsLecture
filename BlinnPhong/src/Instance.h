// Instance.h
#pragma once

#include "Vertex.h"
#include "Transform.h"
#include "Model.h"
#include <memory>
#include <string>

namespace RYBlinnPhong {

class VulkanDevice;
class Buffer;

class Instance {
public:
    std::string name;
    Model* model = nullptr;
    Transform transform;
    glm::vec4 color = glm::vec4(1.0f);

    void createBuffer(VulkanDevice* device);

    void updateBuffer();

    const std::unique_ptr<Buffer>& getBuffer() const { return m_buffer; }

private:
    std::unique_ptr<Buffer> m_buffer;
    void* m_mapped = nullptr;
};

} // namespace RYBlinnPhong
