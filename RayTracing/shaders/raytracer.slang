// raytracer.slang
// Compute shader for real-time ray tracing.
// Entry point: computeMain

struct CameraData {
    float4x4 viewProj;
    float4x4 invViewProj;
    float4 position;       // camera world position (w unused)
};

//struct Material {
//    float4 diffuseColor;
//    float reflectivity;    // 0.0 = diffuse, 1.0 = perfect mirror
//    float ior;             // index of refraction (e.g. 1.5)
//    float specularComponent; // specular exponent (shininess)
//    float diffuseWeight;     // 漫反射分量权重
//    float specularWeight;    // 镜面反射分量权重
//    float reflectionWeight;  // 反射分量权重
//    float refractionWeight;  // 折射分量权重
//    float _pad;
//};
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
    float ambientStrength;
    float diffuseStrength;
    float specularStrength;
};

// ── Bindings ──────────────────────────────────────────────────────

[[vk::push_constant]] RTGlobalConstants pc;

// Set 0: per-frame data (shared with raster pipeline)
[[vk::binding(0, 0)]] ConstantBuffer<CameraData> camera;
[[vk::binding(1, 0)]] StructuredBuffer<LightData> lights;
// Set 1: RT-specific resources
[[vk::binding(0, 1)]] StructuredBuffer<Sphere>    spheres;
[[vk::binding(1, 1)]] RWTexture2D<float4>        outputImage;
[[vk::binding(2, 1)]] StructuredBuffer<Material>  materials;

// ── Ray-sphere intersection ───────────────────────────────────────

/** Returns true and fills t0/t1 if the ray intersects the sphere.
 *  t0, t1: intersection distances along the ray (t0 <= t1).
 *  If the ray originates inside the sphere, t0 may be negative. */
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

/** Build a world-space ray from the camera through pixel (x,y).
 *  Uses inverse view-projection to map NDC → world. */
Ray generateCameraRay(uint2 pixel, uint2 imageSize)
{
    // Map pixel to NDC [-1, 1]
    float2 ndc;
    ndc.x = (float(pixel.x) + 0.5) / float(imageSize.x) * 2.0 - 1.0;
    ndc.y = (float(pixel.y) + 0.5) / float(imageSize.y) * 2.0 - 1.0;

    // Un-project near and far points to get world-space direction
    float4 nearPoint = mul(camera.invViewProj, float4(ndc.x, ndc.y, 0.0, 1.0));
    float4 farPoint  = mul(camera.invViewProj, float4(ndc.x, ndc.y, 1.0, 1.0));

    nearPoint /= nearPoint.w;
    farPoint  /= farPoint.w;

    Ray ray;
    ray.origin = camera.position.xyz;
    ray.direction = normalize(farPoint.xyz - nearPoint.xyz);
    return ray;
}

// ── Scene trace (spheres only — MVP) ──────────────────────────────

HitResult traceScene(Ray ray)
{
    HitResult result;
    result.hit = 0;
    result.t = 1e30;

    for (uint i = 0; i < pc.sphereCount; i++) {
        float t0, t1;
        if (!intersectSphere(ray, spheres[i], t0, t1)) continue;

        // Accept the nearest positive intersection
        float t = (t0 > 0.001) ? t0 : t1;
        if (t <= 0.001 || t >= result.t) continue;

        result.hit = 1;
        result.t = t;
        result.point = ray.origin + t * ray.direction;
        result.normal = normalize(result.point - spheres[i].center);
        result.materialIndex = spheres[i].materialIndex;
    }

    //*****************************************************************
    // TODO A: Triangle intersection for OBJ mesh models
    //   - Iterate over all ModelRef entries
    //   - Test ray against model bounding sphere (fast reject)
    //   - For models that pass, test each triangle using Moller-Trumbore
    //   - Interpolate vertex normals using barycentric coordinates (u,v)
    //   - Update result if a closer hit is found
    //
    //   Hints:
    //     bool intersectTriangle(Ray ray, float3 v0, v1, v2,
    //                            out float t, out float u, out float v);
    //     float3 normal = normalize((1-u-v)*n0 + u*n1 + v*n2);
    //
    //   show your code
    //
    //*****************************************************************

    return result;
}

// ── Background (MVP: simple gradient) ─────────────────────────────

float3 getBackground(float3 direction)
{
    //*****************************************************************
    // TODO B: Skybox / environment map
    //   Replace this gradient with a lookup into an environment map.
    //
    //   Spherical parameterization:
    //     longitude = atan2(direction.z, direction.x)  // [-pi, pi]
    //     latitude  = asin(direction.y)                 // [-pi/2, pi/2]
    //     u = (longitude + PI) / (2.0 * PI)
    //     v = (latitude + PI/2.0) / PI
    //     return skyboxTexture.Sample(skySampler, float2(u, v)).rgb;
    //
    //   show your code
    //
    //*****************************************************************

    // Simple sky gradient (replace with skybox above)
    float t = 0.5 * (direction.y + 1.0);
    return lerp(float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), t);
}

// ── Shading ───────────────────────────────────────────────────────

float3 reflectDir(float3 I, float3 N) {
    return I - N * 2.0 * dot(I, N);
}

float3 refractDir(float3 I, float3 N, float eta_t, float eta_i) {
    float cosi = -max(-1.f, min(1.f, dot(I, N)));

    float3 finalN = N;
    float finalEta_i = eta_i;
    float finalEta_t = eta_t;

    if (cosi < 0) {
        finalN = -N;
        finalEta_i = eta_t;
        finalEta_t = eta_i;
        cosi = -cosi;
    }

    float eta = finalEta_i / finalEta_t;
    float k = 1.0 - eta * eta * (1.0 - cosi * cosi);

    if (k < 0) {
        return float3(0.0, 0.0, 0.0);
    }

    return I * eta + finalN * (eta * cosi - sqrt(k));
}

struct CastRayStackNode {
    Ray ray;
    float3 throughput; // 当前光线的颜色贡献权重（吞吐量）
    int depth;
};

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (float3(1.0) - F0) * pow(1.0 - cosTheta, 5.0);
}

// 法线分布函数：GGX / Trowbridge-Reitz
float D_GGX(float NoH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (NoH * NoH * (a2 - 1.0) + 1.0);
    return a2 / (3.1415926 * denom * denom);
}

// 几何遮蔽函数：Smith with GGX (height‑correlated)
float G_Smith(float NoV, float NoL, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0; // 简化: k = (roughness+1)^2/8
    float G1_V = NoV / (NoV * (1.0 - k) + k);
    float G1_L = NoL / (NoL * (1.0 - k) + k);
    return G1_V * G1_L;
}

// 在命中点, 计算 BRDF 的直接光照
float3 computeDirectBRDF(HitResult hit, Ray ray, Material mat) {
    float3 V = -ray.direction; // 视线方向（指向相机）
    float3 N = hit.normal;
    float3 Lo = float3(0.0, 0.0, 0.0);

    if (mat.roughness < 0.001 && mat.metallic > 0.99) {
        return mat.emission;
    }

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

        // specular
        float D = D_GGX(NoH, roughness);
        float G = G_Smith(NoV, NoL, mat.roughness);
        float3 F = fresnelSchlick(VoH, F0);
        float3 specular = (D * G * F) / (4.0 * NoV * NoL + 0.001);

        // diffuse
        float3 kD = (1.0 - F) * (1.0 - mat.metallic);
        float3 diffuse = kD * mat.diffuseColor / 3.1415926;

        // light
        float distance = length(lights[i].position - hit.point);
        float attenuation = 1.0 / (distance * distance);  
        float3 radiance = lights[i].color * lights[i].intensity * attenuation;
        Lo += (diffuse + specular) * radiance * NoL;
    }
    Lo += mat.emission;
    Lo += mat.diffuseColor * pc.ambientStrength;
    return Lo;
}

float3 castRay(Ray rayInitial) {

    float3 color = float3(0.0);
#define MAX_STACK 8
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
            //******************************************************************************************************************
            // note: whitted style rt formula: sum contritions from local (diffuse, specular), reflection(mirror) and refraction
            //******************************************************************************************************************

            Material mat = materials[hit.materialIndex];
            float3 direct = computeDirectBRDF(hit, ray, mat);
            color += throughput * direct;

            float cosTheta = abs(dot(ray.direction, hit.normal));
            float3 F0 = lerp(0.04, mat.diffuseColor, mat.metallic);
            float3 reflectWeight = fresnelSchlick(cosTheta, F0);
            float3 refractWeight = float3(1.0) - reflectWeight;

            if (mat.transparency < 0.05) refractWeight = 0.0;

            //*************************************************************************************************************
            // Note: (1) recursive calling reflect() and refract() without conditioning
            //       (2) refract() use default value of refraction ray and recursive calling itself for changing inside-out
            //**************************************************************************************************************
            float3 reflect_dir = normalize(reflectDir(ray.direction, hit.normal));
            float3 refract_dir = normalize(refractDir(ray.direction, hit.normal, mat.ior, 1.0));


            bool isInside = dot(ray.direction, hit.normal) > 0.0;
            float3 faceNormal = isInside ? -hit.normal : hit.normal;
            float3 reflect_orig = hit.point + faceNormal * 1e-3;
            float3 refract_orig = hit.point - faceNormal * 1e-3;

            if (depth < 4 && stackPtr < MAX_STACK) {
                if (any(refractWeight > 0.0)) {
                    float3 next_refract_throughput = throughput * refractWeight;
                    stack[stackPtr].ray = Ray(refract_orig, refract_dir);
                    stack[stackPtr].throughput = next_refract_throughput;
                    stack[stackPtr].depth = depth + 1;
                    stackPtr += 1;
                }

                if (any(reflectWeight > 0)) {
                    float3 next_reflect_throughput = throughput * reflectWeight;
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

// ── Compute entry point ───────────────────────────────────────────

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
