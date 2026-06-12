// pathtracer.rchit.slang
// Closest-hit shader for Monte Carlo path tracing.
// Only returns geometry data. All shading is done in the raygen shader.

#include "pathtracer_common.slang"

[shader("closesthit")]
void closestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    uint instanceId = InstanceID();
    InstanceData inst = instanceData[instanceId];

    payload.hitPosition = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    payload.hitKind = 1;

    // Read material
    if (inst.materialId < pc.materialCount) {
        Material mat = materials[inst.materialId];
        payload.diffuseColor = mat.diffuseColor;
        payload.roughness = mat.roughness;
        payload.metallic = mat.metallic;
        payload.transparency = mat.transparency;
        payload.ior = mat.ior;
        payload.emission = mat.emission;
    } else {
        payload.diffuseColor = float3(0.5);
        payload.roughness = 0.5;
        payload.metallic = 0.0;
        payload.transparency = 0.0;
        payload.ior = 1.5;
        payload.emission = float3(0.0);
    }

    uint idxOffset = 0;
    uint vertexOff = 0;
    if (inst.modelRefIndex < pc.modelRefCount) {
        ModelRef ref = modelRefs[inst.modelRefIndex];
        vertexOff = ref.vertexOffset;
        idxOffset = ref.firstIndex;
        payload.textureIndex = ref.textureIndex;
    } else {
        payload.textureIndex = -1;
    }

    // Get triangle data
    uint primId = PrimitiveIndex();
    uint i0 = indices[idxOffset + primId * 3 + 0];
    uint i1 = indices[idxOffset + primId * 3 + 1];
    uint i2 = indices[idxOffset + primId * 3 + 2];

    GPUVertex v0 = vertices[vertexOff + i0];
    GPUVertex v1 = vertices[vertexOff + i1];
    GPUVertex v2 = vertices[vertexOff + i2];

    float2 bary = attribs.barycentrics;
    float3 objNormal = interpolateV(v0.normal, v1.normal, v2.normal, bary);
    payload.texCoord = interpolateV(v0.texCoord, v1.texCoord, v2.texCoord, bary);

    // World-space normal
    payload.hitNormal = normalize(mul(ObjectToWorld3x4(), float4(objNormal, 0.0)));
    if (length(payload.hitNormal) < 0.01)
        payload.hitNormal = normalize(cross(v1.position - v0.position, v2.position - v0.position));

    // Texture lookup
    if (payload.textureIndex >= 0) {
        float4 texColor = modelTextures[payload.textureIndex].SampleLevel(payload.texCoord, 0.0);
        payload.diffuseColor *= texColor.rgb;
    }
}
