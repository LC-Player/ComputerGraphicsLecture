// pathtracer.rmiss.slang
// Miss shader for Monte Carlo path tracing.
// Handles checkerboard floor (procedural) and envmap background.

#include "pathtracer_common.slang"

[shader("miss")]
void miss(inout Payload payload)
{
    float3 ro = WorldRayOrigin();
    float3 rd = WorldRayDirection();

    // Checkerboard floor at y=-4
    float denom = dot(rd, float3(0.0, 1.0, 0.0));
    if (abs(denom) > 1e-6) {
        float t = -(ro.y + 4.0) / denom;
        if (t > 0.001 && t < 1e30) {
            float3 pt = ro + t * rd;
            if (abs(pt.x) < 10.0 && pt.z > -30.0 && pt.z < -10.0) {
                bool white = ((int(0.5 * pt.x + 1000.0) + int(0.5 * pt.z)) & 1) != 0;
                payload.hitKind = 1;
                payload.hitPosition = pt;
                payload.hitNormal = float3(0.0, 1.0, 0.0);
                payload.diffuseColor = white ? float3(0.8, 0.8, 0.8) : float3(0.3, 0.2, 0.1);
                payload.roughness = 0.8;
                payload.metallic = 0.0;
                payload.transparency = 0.0;
                payload.ior = 1.0;
                payload.emission = float3(0.0);
                payload.textureIndex = -1;
                return;
            }
        }
    }

    payload.hitKind = 0;
}
