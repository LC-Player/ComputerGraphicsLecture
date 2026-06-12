// Welzl.h
#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <random>
#include <cfloat>

namespace RYRayTracing {

struct WelzlSphere {
    glm::vec3 center{0.0f};
    float radius = 0.0f;
};

namespace detail {

inline WelzlSphere sphereFromBoundary(const std::vector<glm::vec3>& R)
{
    switch (R.size()) {
    case 0:
        return {};
    case 1:
        return {R[0], 0.0f};
    case 2: {
        glm::vec3 c = (R[0] + R[1]) * 0.5f;
        return {c, glm::distance(R[0], c)};
    }
    case 3: {
        glm::vec3 a = R[1] - R[0];
        glm::vec3 b = R[2] - R[0];
        float aa = glm::dot(a, a);
        float bb = glm::dot(b, b);
        float ab = glm::dot(a, b);
        float crossLen2 = aa * bb - ab * ab;
        if (crossLen2 < 1e-12f) {
            float d01 = glm::pow(glm::distance(R[0], R[1]), 2);
            float d02 = glm::pow(glm::distance(R[0], R[2]), 2);
            float d12 = glm::pow(glm::distance(R[1], R[2]), 2);
            if (d01 >= d02 && d01 >= d12) {
                glm::vec3 c = (R[0] + R[1]) * 0.5f;
                return {c, std::sqrt(d01) * 0.5f};
            }
            if (d02 >= d01 && d02 >= d12) {
                glm::vec3 c = (R[0] + R[2]) * 0.5f;
                return {c, std::sqrt(d02) * 0.5f};
            }
            glm::vec3 c = (R[1] + R[2]) * 0.5f;
            return {c, std::sqrt(d12) * 0.5f};
        }
        float u = bb * (aa - ab) / (2.0f * crossLen2);
        float v = aa * (bb - ab) / (2.0f * crossLen2);
        glm::vec3 c = R[0] + u * a + v * b;
        return {c, glm::distance(c, R[0])};
    }
    default: { // 4
        glm::vec3 a = R[1] - R[0];
        glm::vec3 b = R[2] - R[0];
        glm::vec3 c = R[3] - R[0];
        glm::vec3 rhs = {
            glm::dot(R[1], R[1]) - glm::dot(R[0], R[0]),
            glm::dot(R[2], R[2]) - glm::dot(R[0], R[0]),
            glm::dot(R[3], R[3]) - glm::dot(R[0], R[0])
        };
        glm::mat3 M(a.x, b.x, c.x,
                    a.y, b.y, c.y,
                    a.z, b.z, c.z);
        float det = glm::determinant(M);
        if (std::abs(det) < 1e-12f) {
            WelzlSphere best{{0, 0, 0}, FLT_MAX};
            for (int i = 0; i < 4; ++i) {
                std::vector<glm::vec3> subset;
                for (int j = 0; j < 4; ++j)
                    if (j != i) subset.push_back(R[j]);
                WelzlSphere s = sphereFromBoundary(subset);
                if (glm::distance(s.center, R[i]) <= s.radius + 1e-5f && s.radius < best.radius)
                    best = s;
            }
            return best;
        }
        glm::vec3 center = 0.5f * glm::inverse(M) * rhs;
        return {center, glm::distance(center, R[0])};
    }
    }
}

// Recurse only when a point enters the support set R (|R| ≤ 4).
// Maximum recursion depth = 4 — safe from stack overflow.
inline WelzlSphere welzlWithSupport(std::vector<glm::vec3>& P,
                                     std::vector<glm::vec3>& R,
                                     size_t n)
{
    WelzlSphere s = sphereFromBoundary(R);

    for (size_t i = 0; i < n; ++i) {
        if (glm::distance(P[i], s.center) <= s.radius + 1e-5f)
            continue;
        R.push_back(P[i]);
        s = welzlWithSupport(P, R, i);
        R.pop_back();
    }
    return s;
}

} // namespace detail

// Iterative outer loop — no deep recursion.
inline WelzlSphere computeMinEnclosingSphere(std::vector<glm::vec3> points)
{
    if (points.empty())
        return {};

    std::mt19937 rng(42);
    std::shuffle(points.begin(), points.end(), rng);

    WelzlSphere s{points[0], 0.0f};

    for (size_t i = 1; i < points.size(); ++i) {
        if (glm::distance(points[i], s.center) <= s.radius + 1e-5f)
            continue;
        std::vector<glm::vec3> R = {points[i]};
        s = detail::welzlWithSupport(points, R, i);
    }

    return s;
}

} // namespace RYRayTracing
