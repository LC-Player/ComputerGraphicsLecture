#pragma once

#include "Transform.h"

#include "glm/glm.hpp"
#include <string>

class Camera {
public:
	Camera() = default;
	Camera(const glm::mat4& projection) : m_Projection(projection) {}
	virtual ~Camera() = default;

	const glm::mat4& GetProjection() const { return m_Projection; }
protected:
	glm::mat4 m_Projection = glm::mat4(1.0f);
};

class SceneCamera : public Camera {
public:
	enum class ProjectionType {
		Perspective = 0,
		Orthographic = 1,
	};
public:
	SceneCamera();
	SceneCamera(const glm::mat4& projection) : Camera(projection) {}
	virtual ~SceneCamera() = default;

	void SetViewportAspectRatio(uint32_t width, uint32_t height);
	void SetAspectRatio(float aspectRatio) { m_AspectRatio = aspectRatio; RecalculateProjection(); }

	void SetOrthographic(float height, float nearClip, float farClip);
	void SetOrthographicHeight(float height) { m_OrthographicHeight = height; RecalculateProjection(); }
	void SetOrthographicNear(float nearClip) { m_OrthographicNear = nearClip; RecalculateProjection(); }
	void SetOrthographicFar(float farClip) { m_OrthographicFar = farClip; RecalculateProjection(); }
	float GetOrthographicHeight() const { return m_OrthographicHeight; }
	float GetOrthographicNear() const { return m_OrthographicNear; }
	float GetOrthographicFar() const { return m_OrthographicFar; }

	void SetPerspective(float verticalFOV, float nearClip, float farClip);
	void SetPerspectiveVerticalFOV(float verticalFOV) { m_PerspectiveVerticalFOV = verticalFOV; RecalculateProjection(); }
	void SetPerspectiveNear(float nearClip) { m_PerspectiveNear = nearClip; RecalculateProjection(); }
	void SetPerspectiveFar(float farClip) { m_PerspectiveFar = farClip; RecalculateProjection(); }
	float GetPerspectiveVerticalFOV() const { return m_PerspectiveVerticalFOV; }
	float GetPerspectiveNear() const { return m_PerspectiveNear; }
	float GetPerspectiveFar() const { return m_PerspectiveFar; }

	ProjectionType GetProjectionType() const { return m_ProjectionType; }
	void SetProjectionType(ProjectionType type) { m_ProjectionType = type; RecalculateProjection(); }
	float GetAspectRatio() const { return m_AspectRatio; }

	glm::vec3 ScreenToWorldSpace(const glm::mat4& viewMatrix, glm::vec2 ndc, float linearDepth);

	glm::mat4 GetView() {
		return glm::inverse(m_Transform());
	}

	glm::mat4 GetViewProj() {
		return GetProjection() * GetView();
	}

private:
	void RecalculateProjection();
private:
	ProjectionType m_ProjectionType = ProjectionType::Orthographic;
	float m_AspectRatio = 1;

	float m_OrthographicHeight = 1.0f;
	float m_OrthographicNear = -1.0f, m_OrthographicFar = 1.0f;

	float m_PerspectiveVerticalFOV = glm::radians(45.0f);
	float m_PerspectiveNear = 0.01f, m_PerspectiveFar = 1000.0f;

	Transform m_Transform;
};

inline const std::pair<SceneCamera::ProjectionType, const char*> ProjectionTypeMap[] = {
	{ SceneCamera::ProjectionType::Perspective,  "Perspective"  },
	{ SceneCamera::ProjectionType::Orthographic, "Orthographic" },
};

struct CameraData {
	glm::mat4 viewProj;
	glm::vec4 position; // z for padding
};
