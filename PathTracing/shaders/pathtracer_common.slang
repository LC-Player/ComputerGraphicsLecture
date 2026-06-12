// pathtracer_common.slang
// Shared structures and math for Monte Carlo path tracing.

#define MAX_BOUNCES 6
#define SAMPLES_PER_FRAME 4
#define PI 3.14159265359

struct CameraData {
    float4x4 viewProj;
    float4x4 invViewProj;
    float4 position;
};

struct LightData {
    float3 position;
    int type;
    float3 color;
    float intensity;
    float3 direction;
    float maxDistance;
    float innerCos;
    float outerCos;
    float2 _pad;
};

struct Material {
    float3 diffuseColor;
    float transparency;
    float3 emission;
    float metallic;
    float roughness;
    float ior;
};

struct GPUVertex {
    float3 position;
    float3 normal;
    float2 texCoord;
    float2 _pad;
};

struct ModelRef {
    uint firstIndex;
    uint indexCount;
    uint vertexOffset;
    uint materialId;
    float4x4 invTransform;
    float4 color;
    float3 boundingSphereCenter;
    float boundingSphereRadius;
    int textureIndex;
    int _unused;
    float2 _pad;
};

struct Payload {
    float3 hitPosition;
    float3 hitNormal;
    int hitKind;          // 0=miss, 1=hit
    float3 diffuseColor;
    float roughness;
    float metallic;
    float transparency;
    float ior;
    float3 emission;
    int textureIndex;
    float2 texCoord;
    float visibility;    // for shadow rays
};

struct RTGlobalConstants {
    uint sphereCount;
    uint lightCount;
    uint materialCount;
    uint modelRefCount;
    uint sphereInstanceBase;
    uint totalAccumSamples; // total samples accumulated so far (0 initially)
    float ambientStrength;
    uint _pad2;
};

struct InstanceData {
    uint materialId;
    uint textureIndex;
    uint modelRefIndex;
    uint _pad;
};

// ── Bindings ──────────────────────────────────────────────────────

[[vk::push_constant]] RTGlobalConstants pc;

// Set 0: TLAS
[[vk::binding(0, 0)]] RaytracingAccelerationStructure tlas;

// Set 1: per-frame
[[vk::binding(0, 1)]] ConstantBuffer<CameraData> camera;
[[vk::binding(1, 1)]] StructuredBuffer<LightData> lights;
[[vk::binding(2, 1)]] RWTexture2D<float4> outputImage;
[[vk::binding(3, 1)]] StructuredBuffer<InstanceData> instanceData;
[[vk::binding(4, 1)]] RWTexture2D<float4> accumBuffer; // rgb=accum color, a=sample count

// Set 2: scene
[[vk::binding(0, 2)]] StructuredBuffer<Material> materials;
[[vk::binding(1, 2)]] StructuredBuffer<GPUVertex> vertices;
[[vk::binding(2, 2)]] StructuredBuffer<uint> indices;
[[vk::binding(3, 2)]] StructuredBuffer<ModelRef> modelRefs;
[[vk::binding(4, 2)]] Sampler2D modelTextures[8];
[[vk::binding(5, 2)]] Sampler2D envmap;

// ── PCG random ──────────────────────────────────────────────────

uint wangHash(uint seed) {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

float rand1d(inout uint seed) {
    seed = wangHash(seed);
    return float(seed & 0x00FFFFFFu) / float(0x01000000u);
}

float2 rand2d(inout uint seed) {
    return float2(rand1d(seed), rand1d(seed));
}

// ── Background ──────────────────────────────────────────────────

float3 getBackground(float3 direction) {
    float3 dir = normalize(direction);
    float2 uv;
    uv.x = (atan2(dir.z, dir.x) + PI) / (2.0 * PI);
    uv.y = (asin(clamp(dir.y, -1.0, 1.0)) + PI * 0.5) / PI;
    return envmap.SampleLevel(uv, 0.0).rgb;
}

// ── Tangent space helpers ──────────────────────────────────────

float3 buildLocalFrame(float3 N) {
    float3 up = abs(N.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 T = normalize(cross(up, N));
    return T;
}

float3 toWorld(float3 v, float3 N, float3 T) {
    float3 B = cross(N, T);
    return T * v.x + N * v.y + B * v.z;
}

// ── BSDF evaluation ────────────────────────────────────────────

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float denom = (NoH * NoH * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float G_Smith(float NoV, float NoL, float a) {
    float k = (a + 1.0) * (a + 1.0) / 8.0;
    return (NoV / (NoV * (1.0 - k) + k)) * (NoL / (NoL * (1.0 - k) + k));
}

float3 fresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (float3(1.0) - F0) * pow(max(0.0, 1.0 - cosTheta), 5.0);
}

float3 evalBRDF(float3 N, float3 V, float3 L, float3 albedo, float a, float metallic, float transparency) {
    float3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4);
    float NoL = max(dot(N, L), 1e-4);
    float NoH = max(dot(N, H), 1e-4);
    float VoH = max(dot(V, H), 1e-4);

    float D = D_GGX(NoH, a);
    float G = G_Smith(NoV, NoL, a);
    float3 F0 = lerp(0.04, albedo, metallic);
    float3 F = fresnelSchlick(VoH, F0);

    float3 spec = D * G * F / (4.0 * NoV * NoL);
    float3 kd = (float3(1.0) - F) * (1.0 - metallic);
    float3 diff = kd * albedo * (1.0 - transparency) / PI;

    return diff + spec;
}

// ── BSDF sampling ──────────────────────────────────────────────

float3 sampleCosineHemisphere(float2 uv) {
    float phi = 2.0 * PI * uv.x;
    float cosTheta = sqrt(uv.y);
    float sinTheta = sqrt(1.0 - uv.y);
    return float3(cos(phi) * sinTheta, cosTheta, sin(phi) * sinTheta);
}

float pdfCosineHemisphere(float cosTheta) {
    return cosTheta / PI;
}

float3 sampleGGX(float2 uv, float3 N, float3 V, float a) {
    float a2 = a * a;
    float phi = 2.0 * PI * uv.x;
    float cosTheta = sqrt((1.0 - uv.y) / (1.0 + (a2 - 1.0) * uv.y));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float3 H = float3(cos(phi) * sinTheta, cosTheta, sin(phi) * sinTheta);
    float3 T = buildLocalFrame(N);
    H = toWorld(H, N, T);
    return normalize(reflect(-V, H));
}

float pdfGGX(float3 N, float3 V, float3 L, float a) {
    float3 H = normalize(V + L);
    float NoH = max(dot(N, H), 1e-4);
    float VoH = max(dot(V, H), 1e-4);
    return D_GGX(NoH, a) * NoH / (4.0 * VoH);
}

// ── Light sampling ─────────────────────────────────────────────

void sampleLight(uint lightIdx, float3 hitPos, inout uint seed,
    out float3 L, out float3 radiance, out float dist, out float pdf) {
    if (lights[lightIdx].type == 2) {
        // Directional: uniform direction
        L = normalize(-lights[lightIdx].direction);
        dist = 1e10;
        pdf = 1.0;
        radiance = lights[lightIdx].color * lights[lightIdx].intensity;
    } else {
        // Point/Spot: sample point on light
        L = normalize(lights[lightIdx].position - hitPos);
        dist = length(lights[lightIdx].position - hitPos);
        float attenuation = 1.0 / (dist * dist);
        pdf = 1.0; // simplified for now
        radiance = lights[lightIdx].color * lights[lightIdx].intensity * attenuation;
        if (lights[lightIdx].type == 1) {
            float cosTheta = dot(normalize(lights[lightIdx].position - hitPos), normalize(-lights[lightIdx].direction));
            float spotAtten = smoothstep(lights[lightIdx].outerCos, lights[lightIdx].innerCos, cosTheta);
            radiance *= spotAtten;
        }
    }
}

// ── NEE: sample one light for direct illumination ──────────────

float3 evalDirect(float3 hitPos, float3 N, float3 V, float3 albedo, float a, float metallic, float transparency, inout uint seed) {
    if (pc.lightCount == 0) return float3(0.0);

    // Pick a random light
    uint lightIdx = uint(rand1d(seed) * float(pc.lightCount));
    lightIdx = min(lightIdx, pc.lightCount - 1);

    float3 L, radiance;
    float dist, lightPdf;
    sampleLight(lightIdx, hitPos, seed, L, radiance, dist, lightPdf);

    float NoL = dot(N, L);
    if (NoL <= 0.0) return float3(0.0);

    // Shadow ray
    float maxDist = (lights[lightIdx].type == 2) ? 1e10 : dist;
    RayDesc shadowRay;
    shadowRay.Origin = hitPos + N * 1e-3;
    shadowRay.Direction = L;
    shadowRay.TMin = 0.001;
    shadowRay.TMax = maxDist;

    Payload shadowPayload;
    shadowPayload.visibility = 0.0; // assume shadowed
    // RAY_FLAG_SKIP_CLOSEST_HIT_SHADER = 0x10
    TraceRay(tlas, 0x10, 0xFF, 1, 0, 1, shadowRay, shadowPayload);

    if (shadowPayload.visibility < 0.5) return float3(0.0);

    float3 brdf = evalBRDF(N, V, L, albedo, a, metallic, transparency);
    return brdf * radiance * NoL / lightPdf * float(pc.lightCount);
}

// ── Vertex interpolation ──────────────────────────────────────

float3 interpolateV(float3 v0, float3 v1, float3 v2, float2 bary) {
    float w = 1.0 - bary.x - bary.y;
    return v0 * w + v1 * bary.x + v2 * bary.y;
}

float2 interpolateV(float2 v0, float2 v1, float2 v2, float2 bary) {
    float w = 1.0 - bary.x - bary.y;
    return v0 * w + v1 * bary.x + v2 * bary.y;
}
