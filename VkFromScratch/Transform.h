#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


struct Transform {
    glm::vec3 translation = { 0, 0, 0 };
    glm::vec3 rotation = { 0, 0, 0 };
    glm::vec3 scale = { 1, 1, 1 };

    glm::mat4 transform() const {
        glm::mat4 rotMat =
            glm::rotate(glm::mat4(1.0f), rotation.z, { 0, 0, 1 }) *
            glm::rotate(glm::mat4(1.0f), rotation.y, { 0, 1, 0 }) *
            glm::rotate(glm::mat4(1.0f), rotation.x, { 1, 0, 0 });
        return glm::translate(glm::mat4(1.0f), translation) *
            rotMat *
            glm::scale(glm::mat4(1.0f), scale);
    }

    glm::mat4 operator()() const {
        return transform();
    }
};