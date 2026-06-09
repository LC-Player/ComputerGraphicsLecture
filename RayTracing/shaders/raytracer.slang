// raytracer.slang
// Compute shader for real-time ray tracing.
// Entry point: computeMain

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
    float3 position;
    float intensity;
    float3 color;
    float maxDistance;
};

struct Ray {
    float3 origin;
    float3 direction;
};

struct HitResult {
    int   hit;             // 1 if something was hit
    float t;               // distance along ray to hit point
    float3 point;          // world-space hit position
    float3 normal;         // surface normal at hit point
    uint  materialIndex;   // index into material array
};

struct RTGlobalConstants {
    uint sphereCount;
    uint lightCount;
    uint materialCount;
    float diffuseStrength;
    float specularStrength;
};

// ── Bindings ──────────────────────────────────────────────────────

[[vk::push_constant]] RTGlobalConstants pc;

// Set 0: per-frame data (shared with raster pipeline)
[[vk::binding(0, 0)]] ConstantBuffer<CameraData> camera;
[[vk::binding(1, 0)]] StructuredBuffer<LightData> lights;
// Set 1: RT-specific resources
[[vk::binding(0, 1)]] StructuredBuffer<Sphere>   spheres;
[[vk::binding(1, 1)]] RWTexture2D<float4>        outputImage;
[[vk::binding(2, 1)]] StructuredBuffer<Material>  materials;

// ── Ray-sphere intersection ───────────────────────────────────────

bool intersectSphere(Ray ray, Sphere sphere, out float t0, out float t1)
{
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
    }
    return result;
}

// ── Background ────────────────────────────────────────────────────

float3 getBackground(float3 direction)
{
    float t = 0.5 * (direction.y + 1.0);
    return lerp(float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), t);
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

float3 computeDirectBRDF(HitResult hit, Ray ray, Material mat) {
    float3 V = -ray.direction;
    float3 N = hit.normal;
    float3 Lo = float3(0.0, 0.0, 0.0);

    float roughness = max(mat.roughness, 0.001);
    float3 F0 = lerp(0.04, mat.diffuseColor, mat.metallic);

    for (uint i = 0; i < pc.lightCount; i++) {
        float3 L = normalize(lights[i].position - hit.point);
        float3 H = normalize(V + L);
        float NoL = max(0.0, dot(N, L));
        if (NoL <= 0.0) continue;

        float lightDistance = length(lights[i].position - hit.point);
        float3 shadowOrig = hit.point + N * 2e-3;
        HitResult shadowHit = traceScene(Ray(shadowOrig, L));
        if (shadowHit.hit != 0 && shadowHit.t < lightDistance) continue;

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

        float attenuation = 1.0 / (lightDistance * lightDistance);  
        float3 radiance = lights[i].color * lights[i].intensity * attenuation;
        Lo += (diffuse + specular) * radiance * NoL;
    }
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
            Material mat = materials[hit.materialIndex];
            float3 direct = computeDirectBRDF(hit, ray, mat);
            color += throughput * direct;
            
            float3 V = -ray.direction; // 反光线
            bool isInside = dot(ray.direction, hit.normal) > 0.0; // 在球内时，光线和法线方向相同，所以大于0
            float3 faceNormal = isInside ? -hit.normal : hit.normal; // 以“击点”为视角，如果从内射击，则faceNormal=反一下的法线（法线朝内）
            
            float cosThetaI = dot(V, faceNormal); // note: max 是否可以删除？从外部击中，V朝外，faceNormal朝外，方向一致，从内部击中，V朝内，faceNormal朝内，方向也一致

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