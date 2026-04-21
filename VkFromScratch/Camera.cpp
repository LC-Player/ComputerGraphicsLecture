#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "Camera.h"

SceneCamera::SceneCamera() : Camera() {
	RecalculateProjection();
}

void SceneCamera::SetOrthographic(float height, float nearClip, float farClip) {
	m_ProjectionType = ProjectionType::Orthographic;
	m_OrthographicHeight = height;
	m_OrthographicNear = nearClip;
	m_OrthographicFar = farClip;
	RecalculateProjection();
}

void SceneCamera::SetViewportAspectRatio(uint32_t width, uint32_t height) {
	m_AspectRatio = static_cast<float>(width) / static_cast<float>(height);
	RecalculateProjection();
}

void SceneCamera::SetPerspective(float verticalFOV, float nearClip, float farClip) {
	m_ProjectionType = ProjectionType::Perspective;
	m_PerspectiveVerticalFOV = verticalFOV;
	m_PerspectiveNear = nearClip;
	m_PerspectiveFar = farClip;
	RecalculateProjection();
}

void SceneCamera::RecalculateProjection() {
	switch (m_ProjectionType) {
	case ProjectionType::Orthographic: {
		float left = -m_OrthographicHeight * m_AspectRatio * 0.5;
		float right = m_OrthographicHeight * m_AspectRatio * 0.5;
		float bottom = -m_OrthographicHeight * 0.5;
		float top = m_OrthographicHeight * 0.5;
		m_Projection = glm::orthoZO(left, right, bottom, top, m_OrthographicNear, m_OrthographicFar);
		break;
	}

	case ProjectionType::Perspective: {
		m_Projection = glm::perspectiveZO(m_PerspectiveVerticalFOV, m_AspectRatio, m_PerspectiveNear, m_PerspectiveFar);
	}

	}
	m_Projection[1][1] *= -1.0f; // flip y

}

glm::vec3 SceneCamera::ScreenToWorldSpace(const glm::mat4& viewMatrix, glm::vec2 ndc, float linearDepth) {
	glm::vec4 viewSpacePos(0.0f, 0.0f, 0.0f, 1.0f);

	if (m_ProjectionType == ProjectionType::Perspective) {
		// 透视投影：利用相似三角形原理，通过视锥体参数和线性深度直接推导 View 坐标
		float halfFOV = m_PerspectiveVerticalFOV * 0.5f;
		float tanHalfFOV = std::tan(halfFOV);

		viewSpacePos.x = ndc.x * linearDepth * m_AspectRatio * tanHalfFOV;
		viewSpacePos.y = ndc.y * linearDepth * tanHalfFOV;
		viewSpacePos.z = -linearDepth; // 标准右手坐标系，相机看向 -Z 轴
	}
	else if (m_ProjectionType == ProjectionType::Orthographic) {
		// 正交投影：View 空间的 XY 分布是固定的，不随深度(Z)发散
		float halfHeight = m_OrthographicHeight * 0.5f;
		float halfWidth = halfHeight * m_AspectRatio;

		viewSpacePos.x = ndc.x * halfWidth;
		viewSpacePos.y = ndc.y * halfHeight;
		viewSpacePos.z = -linearDepth;
	}

	// 计算 View Matrix 的逆矩阵，将 View 空间坐标转换到 World 空间
	glm::mat4 inverseView = glm::inverse(viewMatrix);
	glm::vec4 worldSpacePos = inverseView * viewSpacePos;

	return glm::vec3(worldSpacePos);
}