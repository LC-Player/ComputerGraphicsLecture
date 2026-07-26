// raytracer.slang
// Compute shader for real-time ray tracing.
// Entry point: computeMain

#define CHECKERBOARD_FLOOR 1
#define FLOOR_SENTINEL 0xFFFFFFFFu

struct CameraData {
    float4x4 viewProj;
    float4x4 invViewProj;
    float4 position;       // camera world position (w unused)
};

struct Material {
    float3 diffuseColor;
    float transparency; // 1 for fully transparent, transparency ~ 1 - alpha
    float3 emission;
    float metallic; // 1 like metal
    float roughness; // 0 = mirror, 1 = very rough
    float ior; // index of refraction
};

struct Sphere {
    float3 center;
    float radius;
    uint materialIndex;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

struct LightData {
    float3 position;     // unused for directional
    int type;            // 0=point, 1=spot, 2=directional
    float3 color;
    float intensity;
    float3 direction;    // spot / directional only
    float maxDistance;   // point / spot only
    float innerCos;      // cos(innerAngle), spot only
    float outerCos;      // cos(outerAngle), spot only
    float2 _pad;
};

struct Ray {
    float3 origin;
    float3 direction;
};

struct HitResult {
    int   hit;
    float t;
    float3 point;
    float3 normal;
    uint  materialIndex;
    int   textureIndex;
    float2 texCoord;
};

struct GPUVertex {
    float3 position;       // offset 0,  align 16
    float3 normal;         // offset 16, align 16
    float2 texCoord;       // offset 32
    float2 _pad;           // → 48 bytes
};

struct ModelRef {
    uint   firstIndex;
    uint   indexCount;
    uint   vertexOffset;
    uint   materialId;
    float4x4 invTransform;
    float4   color;
    float3   boundingSphereCenter;
    float    boundingSphereRadius;
    int      textureIndex;
    int      bvhRoot;
    float    _pad0;
    float    _pad1;
};

// std430; 48 bytes per node
struct BVHNode {
    float3 bboxMin;
    float3 bboxMax;
    int    leftOrFirst;   // internal: left child idx; leaf: first global index
    int    rightOrCount;  // internal: right child idx; leaf: triangle count
    int    splitAxis;     // -1=leaf, 0/1/2=internal split axis
};

struct RTGlobalConstants {
    uint sphereCount;
    uint lightCount;
    uint materialCount;
    uint modelRefCount;
    float ambientStrength;
};

// ── Bindings ──────────────────────────────────────────────────────

[[vk::push_constant]] RTGlobalConstants pc;

// Set 0: per-frame data (shared with raster pipeline)
[[vk::binding(0, 0)]] ConstantBuffer<CameraData> camera;
[[vk::binding(1, 0)]] StructuredBuffer<LightData> lights;
// Set 1: RT-specific resources
[[vk::binding(0, 1)]] StructuredBuffer<Sphere>    spheres;
[[vk::binding(1, 1)]] RWTexture2D<float4>         outputImage;
[[vk::binding(2, 1)]] StructuredBuffer<Material>   materials;
[[vk::binding(3, 1)]] StructuredBuffer<GPUVertex>  vertices;
[[vk::binding(4, 1)]] StructuredBuffer<uint>       indices;
[[vk::binding(5, 1)]] StructuredBuffer<ModelRef>   modelRefs;
[[vk::binding(6, 1)]] Sampler2D modelTextures[8];
[[vk::binding(7, 1)]] Sampler2D envmap;
[[vk::binding(8, 1)]] StructuredBuffer<BVHNode> bvhNodes;
[[vk::binding(9, 1)]] StructuredBuffer<uint> bvhTriRemap;

// ── Ray-sphere intersection ───────────────────────────────────────

bool intersectSphere(Ray ray, Sphere sphere, out float t0, out float t1)
{
    t0 = 0.0;
    t1 = 0.0;
    float3 oc = ray.origin - sphere.center;
    float a = dot(ray.direction, ray.direction);
    float b = 2.0 * dot(oc, ray.direction);
    float c = dot(oc, oc) - sphere.radius * sphere.radius;
    float discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) { t0 = 0.0; t1 = 0.0; return false; }

    float sqrtD = sqrt(discriminant);
    t0 = (-b - sqrtD) / (2.0 * a);
    t1 = (-b + sqrtD) / (2.0 * a);
    return true;
}

// ── Ray-triangle intersection (Möller–Trumbore) ─────────────────────

bool intersectTriangle(Ray ray, float3 v0, float3 v1, float3 v2, out float t, out float2 uv)
{
    t = 0.0;
    uv = float2(0.0);
    float3 e1 = v1 - v0;
    float3 e2 = v2 - v0;
    float3 h = cross(ray.direction, e2);
    float a = dot(e1, h);
    if (abs(a) < 1e-7) return false;
    float f = 1.0 / a;
    float3 s = ray.origin - v0;
    float u = f * dot(s, h);
    if (u < 0.0 || u > 1.0) return false;
    float3 q = cross(s, e1);
    float v = f * dot(ray.direction, q);
    if (v < 0.0 || u + v > 1.0) return false;
    t = f * dot(e2, q);
    if (t < 0.001) return false;
    uv = float2(u, v);
    return true;
}

// ── Ray-AABB intersection ──────────────────────────────────────────

bool intersectAABB(Ray ray, float3 bboxMin, float3 bboxMax,
                   out float tNear, out float tFar)
{
    float3 invD = 1.0 / ray.direction;
    float3 t0 = (bboxMin - ray.origin) * invD;
    float3 t1 = (bboxMax - ray.origin) * invD;
    float3 ts = min(t0, t1);
    float3 tl = max(t0, t1);
    tNear = max(ts.x, max(ts.y, ts.z));
    tFar  = min(tl.x, min(tl.y, tl.z));
    return tFar >= max(0.001, tNear);
}

// ── Ray generation ────────────────────────────────────────────────

Ray generateCameraRay(uint2 pixel, uint2 imageSize)
{
    float2 ndc;
    ndc.x = (float(pixel.x) + 0.5) / float(imageSize.x) * 2.0 - 1.0;
    ndc.y = (float(pixel.y) + 0.5) / float(imageSize.y) * 2.0 - 1.0;

    float4 nearPoint = mul(camera.invViewProj, float4(ndc.x, ndc.y, 0.0, 1.0));
    float4 farPoint  = mul(camera.invViewProj, float4(ndc.x, ndc.y, 1.0, 1.0));

    nearPoint /= nearPoint.w;
    farPoint  /= farPoint.w;

    Ray ray;
    ray.origin = camera.position.xyz;
    ray.direction = normalize(farPoint.xyz - nearPoint.xyz);
    return ray;
}

// ── Scene trace ───────────────────────────────────────────────────

HitResult traceScene(Ray ray)
{
    HitResult result;
    result.hit = 0;
    result.t = 1e30;

    for (uint i = 0; i < pc.sphereCount; i++) {
        float t0, t1;
        if (!intersectSphere(ray, spheres[i], t0, t1)) continue;

        float t = (t0 > 0.001) ? t0 : t1;
        if (t <= 0.001 || t >= result.t) continue;

        result.hit = 1;
        result.t = t;
        result.point = ray.origin + t * ray.direction;
        result.normal = normalize(result.point - spheres[i].center);
        result.materialIndex = spheres[i].materialIndex;
        result.textureIndex = -1;
        result.texCoord = float2(0.0);
    }

    for (uint m = 0; m < pc.modelRefCount; m++) {
        ModelRef ref = modelRefs[m];

        // Transform ray to object space
        float4 objOrigin4 = mul(ref.invTransform, float4(ray.origin, 1.0));
        float4 objDir4    = mul(ref.invTransform, float4(ray.direction, 0.0));
        float3 objOrigin   = objOrigin4.xyz;
        float3 objDir      = objDir4.xyz;
        Ray objRay;
        objRay.origin = objOrigin;
        objRay.direction = objDir;

        // Bounding sphere early rejection
        float3 oc = objOrigin - ref.boundingSphereCenter;
        float a = dot(objDir, objDir);
        float b = 2.0 * dot(oc, objDir);
        float c = dot(oc, oc) - ref.boundingSphereRadius * ref.boundingSphereRadius;
        float discriminant = b * b - 4.0 * a * c;
        if (discriminant < 0.0) continue;

        float sqrtD = sqrt(discriminant);
        float bsNear = (-b - sqrtD) / (2.0 * a);
        float bsFar  = (-b + sqrtD) / (2.0 * a);
        if (bsFar < 0.001 || bsNear >= result.t) continue;
        if (ref.bvhRoot < 0) continue;

        // BVH stack traversal
        int stack[64];
        int stackPtr = 1;
        stack[0] = ref.bvhRoot;

        while (stackPtr > 0) {
            stackPtr--;
            int nodeIdx = stack[stackPtr];
            BVHNode node = bvhNodes[nodeIdx];

            float tMinAABB, tMaxAABB;
            if (!intersectAABB(objRay, node.bboxMin, node.bboxMax, tMinAABB, tMaxAABB))
                continue;
            if (tMinAABB >= result.t) continue;

            if (node.splitAxis == -1) {
                // Leaf: leftOrFirst is position in bvhTriRemap
                int triCount = node.rightOrCount;
                int remapBase = node.leftOrFirst;
                for (int t = 0; t < triCount; t++) {
                    int globalIdx = int(bvhTriRemap[remapBase + t]);
                    uint i0 = indices[globalIdx + 0];
                    uint i1 = indices[globalIdx + 1];
                    uint i2 = indices[globalIdx + 2];

                    GPUVertex v0 = vertices[ref.vertexOffset + i0];
                    GPUVertex v1 = vertices[ref.vertexOffset + i1];
                    GPUVertex v2 = vertices[ref.vertexOffset + i2];

                    float tHit;
                    float2 uv;
                    if (!intersectTriangle(objRay, v0.position, v1.position, v2.position, tHit, uv)) continue;
                    if (tHit <= 0.001 || tHit >= result.t) continue;

                    float w = 1.0 - uv.x - uv.y;
                    float3 objNormal = normalize(v0.normal * w + v1.normal * uv.x + v2.normal * uv.y);
                    float3 worldNormal = normalize(mul(objNormal, (float3x3)ref.invTransform));

                    result.hit = 1;
                    result.t = tHit;
                    result.point = ray.origin + tHit * ray.direction;
                    result.normal = worldNormal;
                    result.materialIndex = ref.materialId;
                    result.textureIndex = ref.textureIndex;
                    result.texCoord = v0.texCoord * (1.0 - uv.x - uv.y) + v1.texCoord * uv.x + v2.texCoord * uv.y;
                }
            } else {
                // Internal: push right first (LIFO → left processed first)
                int left  = node.leftOrFirst;
                int right = node.rightOrCount;
                if (stackPtr < 63) stack[stackPtr++] = right;
                if (stackPtr < 63) stack[stackPtr++] = left;
            }
        }
    }
#if CHECKERBOARD_FLOOR
    {
        float denom = dot(ray.direction, float3(0.0, 1.0, 0.0));
        if (abs(denom) > 1e-6) {
            float t = -(ray.origin.y + 4.0) / denom;
            if (t > 0.001 && t < result.t) {
                float3 pt = ray.origin + t * ray.direction;
                if (abs(pt.x) < 10.0 && pt.z > -30.0 && pt.z < -10.0) {
                    result.hit = 1;
                    result.t = t;
                    result.point = pt;
                    result.normal = float3(0.0, 1.0, 0.0);
                    result.materialIndex = FLOOR_SENTINEL;
                    result.textureIndex = -1;
                    result.texCoord = float2(0.0);
                }
            }
        }
    }
#endif
    return result;
}

// ── Background ────────────────────────────────────────────────────

float3 getBackground(float3 direction)
{
    float3 dir = normalize(direction);
    float2 uv;
    uv.x = (atan2(dir.z, dir.x) + 3.1415926) / (2.0 * 3.1415926);
    uv.y = (asin(clamp(dir.y, -1.0, 1.0)) + 3.1415926 * 0.5) / 3.1415926;
    return envmap.SampleLevel(uv, 0.0).rgb;
}

// ── Shading ───────────────────────────────────────────────────────

float3 reflectDir(float3 I, float3 N) {
    return I - N * 2.0 * dot(I, N);
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (float3(1.0) - F0) * pow(max(0.0, 1.0 - cosTheta), 5.0);
}

float D_GGX(float NoH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (NoH * NoH * (a2 - 1.0) + 1.0);
    return a2 / (3.1415926 * denom * denom);
}

float G_Smith(float NoV, float NoL, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float G1_V = NoV / (NoV * (1.0 - k) + k);
    float G1_L = NoL / (NoL * (1.0 - k) + k);
    return G1_V * G1_L;
}

float3 computeDirectBRDF(HitResult hit, Ray ray, inout Material mat) {
#if CHECKERBOARD_FLOOR
    if (hit.materialIndex == FLOOR_SENTINEL) {
        mat.roughness = 0.8;
        mat.metallic = 0.0;
        mat.transparency = 0.0;
        mat.ior = 1.0;
        bool white = ((int(0.5 * hit.point.x + 1000.0) + int(0.5 * hit.point.z)) & 1) != 0;
        mat.diffuseColor = white ? float3(0.8, 0.8, 0.8) : float3(0.3, 0.2, 0.1);
    }
#endif
    if (hit.textureIndex >= 0) {
        float4 texColor = modelTextures[hit.textureIndex].SampleLevel(hit.texCoord, 0.0);
        mat.diffuseColor *= texColor.rgb;
    }
    float3 V = -ray.direction;
    float3 N = hit.normal;
    float3 Lo = float3(0.0, 0.0, 0.0);

    float roughness = max(mat.roughness, 0.001);
    float3 F0 = lerp(0.04, mat.diffuseColor, mat.metallic);

    for (uint i = 0; i < pc.lightCount; i++) {
        float3 L;
        float lightDistance;
        float attenuation;

        if (lights[i].type == 2) {
            // Directional light
            L = normalize(-lights[i].direction);
            lightDistance = 1e10;
            attenuation = 1.0;
        } else {
            // Point or spot light
            L = normalize(lights[i].position - hit.point);
            lightDistance = length(lights[i].position - hit.point);
            attenuation = 1.0 / (lightDistance * lightDistance);

            if (lights[i].type == 1) {
                // Spot light cone attenuation
                float3 spotDir = normalize(-lights[i].direction);
                float cosTheta = dot(normalize(lights[i].position - hit.point), spotDir);
                float spotAtten = smoothstep(lights[i].outerCos, lights[i].innerCos, cosTheta);
                attenuation *= spotAtten;
            }
        }

        float3 H = normalize(V + L);
        float NoL = max(0.0, dot(N, L));
        if (NoL <= 0.0) continue;

        float3 shadowOrig = hit.point + N * 2e-3;
        float shadowMaxDist = (lights[i].type == 2) ? 1e10 : lightDistance;
        HitResult shadowHit = traceScene(Ray(shadowOrig, L));
        if (shadowHit.hit != 0 && shadowHit.t < shadowMaxDist) continue;

        float NoV = max(0.001, dot(N, V));
        float NoH = max(0.001, dot(N, H));
        float VoH = max(0.001, dot(V, H));

        float D = D_GGX(NoH, roughness);
        float G = G_Smith(NoV, NoL, mat.roughness);
        float3 F = fresnelSchlick(VoH, F0);
        float denom = 4.0 * max(0.001, NoV) * max(0.001, NoL);
        float3 specular = (D * G * F) / denom;

        float3 kD = (1.0 - F) * (1.0 - mat.metallic);
        // 考虑到透明物体的内部不应该产生不透明的漫反射，结合真实感压制漫反射
        float3 diffuse = kD * mat.diffuseColor * (1.0 - mat.transparency) / 3.1415926;

        float3 radiance = lights[i].color * lights[i].intensity * attenuation;
        Lo += (diffuse + specular) * radiance * NoL;
    }
    Lo += mat.diffuseColor * pc.ambientStrength * (1.0 - mat.metallic);
    Lo += mat.emission;
    return Lo;
}

struct CastRayStackNode {
    Ray ray;
    float3 throughput; 
    int depth;
};

float3 castRay(Ray rayInitial) {
    float3 color = float3(0.0);
#define MAX_STACK 17
    CastRayStackNode stack[MAX_STACK];
    int stackPtr = 0;
    stack[stackPtr].ray = rayInitial;
    stack[stackPtr].throughput = float3(1.0);
    stack[stackPtr].depth = 0;
    stackPtr += 1;

    while (stackPtr > 0) {
        stackPtr -= 1;
        Ray ray = stack[stackPtr].ray;
        float3 throughput = stack[stackPtr].throughput;
        int depth = stack[stackPtr].depth;
        HitResult hit = traceScene(ray);

        if (hit.hit != 0)
        {
#if CHECKERBOARD_FLOOR
            if (hit.materialIndex == FLOOR_SENTINEL) {
                Material floorMat;
                floorMat.diffuseColor = float3(0.0); // filled by computeDirectBRDF
                floorMat.roughness = 0.8;
                floorMat.metallic = 0.0;
                floorMat.transparency = 0.0;
                floorMat.ior = 1.0;
                floorMat.emission = float3(0.0);
                float3 direct = computeDirectBRDF(hit, ray, floorMat);
                color += throughput * direct;
                // No reflection / refraction for floor
                continue;
            }
#endif
            Material mat = materials[hit.materialIndex];
            float3 direct = computeDirectBRDF(hit, ray, mat);
            color += throughput * direct;
            
            float3 V = -ray.direction; // 反光线
            bool isInside = dot(ray.direction, hit.normal) > 0.0; // 在球内时，光线和法线方向相同，所以大于0
            float3 faceNormal = isInside ? -hit.normal : hit.normal; // 以“击点”为视角，如果从内射击，则faceNormal=反一下的法线（法线朝内）
            
            float cosThetaI = dot(V, faceNormal);

            float eta_i = isInside ? mat.ior : 1.0; // 入射介质折射率
            float eta_t = isInside ? 1.0 : mat.ior; // 出射介质折射率
            float eta = eta_i / eta_t;

            // 折射角正弦值的平方，当全反射时（TIR），此值可 > 1（后面判断）
            float sin2ThetaT = eta * eta * (1.0 - cosThetaI * cosThetaI);

            float3 reflectWeight = float3(0.0);
            float3 refractWeight = float3(0.0);
            float3 refract_dir = float3(0.0);
            bool hasRefractRay = false;

            if (sin2ThetaT >= 1.0) { // 全反射
                reflectWeight = float3(1.0);
                refractWeight = float3(0.0);
            } else { // 非全反射

                // 物理修正：当从高折射率介质射向低折射率介质时，Schlick 近似应当采用 cosThetaT
                float cosThetaFresnel = (eta > 1.0) ? sqrt(1.0 - sin2ThetaT) : cosThetaI;
                
                float3 F0 = lerp(0.04, mat.diffuseColor, mat.metallic);
                reflectWeight = fresnelSchlick(cosThetaFresnel, F0);
                refractWeight = float3(1.0) - reflectWeight;

                // 物理约束：金属表面不产生折射
                if (mat.metallic > 0.5) {
                    refractWeight = float3(0.0);
                }

                // 折射分量必须受到透明度参数的深度控制，保证能量守恒
                refractWeight *= mat.transparency;

                // 只有当有能量通过时，才计算折射向量，彻底避免对 0 归一化产生的 NaN 崩溃
                if (any(refractWeight > 1e-4) && mat.transparency >= 0.05) {
                    float cosThetaT = sqrt(max(0.0, 1.0 - sin2ThetaT));
                    refract_dir = normalize(ray.direction * eta + faceNormal * (eta * cosThetaI - cosThetaT));
                    hasRefractRay = true;
                }
            }

            float3 reflect_dir = normalize(reflectDir(ray.direction, hit.normal));

            // 基于规范化 faceNormal 衍生出的安全原点偏移
            float3 reflect_orig = hit.point + faceNormal * 1e-3;
            float3 refract_orig = hit.point - faceNormal * 1e-3;

            if (depth < 4 && stackPtr < MAX_STACK - 1) {
                // 折射
                if (hasRefractRay) {
                    float3 next_refract_throughput = throughput * refractWeight;
                    stack[stackPtr].ray = Ray(refract_orig, refract_dir);
                    stack[stackPtr].throughput = next_refract_throughput;
                    stack[stackPtr].depth = depth + 1;
                    stackPtr += 1;
                }

                // 反射 (增加微弱阈值判定，剔除无用光线，节省开销)
                if (any(reflectWeight > 1e-4) && stackPtr < MAX_STACK - 1) {
                    float3 next_reflect_throughput = throughput * reflectWeight;
                    // 艺术拟合：让粗糙材质的间接反射随粗糙度变暗
                    next_reflect_throughput *= (1.0 - mat.roughness);
                    stack[stackPtr].ray = Ray(reflect_orig, reflect_dir);
                    stack[stackPtr].throughput = next_reflect_throughput;
                    stack[stackPtr].depth = depth + 1;
                    stackPtr += 1;
                }
            }
        }
        else {
            color += throughput * getBackground(ray.direction);
        }
    }

    return color;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;
    uint2 imageSize;
    outputImage.GetDimensions(imageSize.x, imageSize.y);

    if (pixel.x >= imageSize.x || pixel.y >= imageSize.y) return;

    Ray ray = generateCameraRay(pixel, imageSize);
    float3 color = castRay(ray);

    outputImage[pixel] = float4(color, 1.0);
}