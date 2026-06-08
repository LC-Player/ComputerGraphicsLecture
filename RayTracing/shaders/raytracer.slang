// raytracer.slang
// Compute shader for real-time ray tracing.
// Entry point: computeMain

struct CameraData {
    float4x4 viewProj;
    float4x4 invViewProj;
    float4 position;       // camera world position (w unused)
};

struct Sphere {
    float3 center;
    float radius;
    float4 color;
    float reflectivity;    // 0.0 = diffuse, 1.0 = perfect mirror
    float ior;             // index of refraction (e.g. 1.5)
    float _pad;
};

struct LightData {
    float3 position;
    float intensity;
    float3 color;
    float maxDistance;
}

struct Ray {
    float3 origin;
    float3 direction;
};

struct HitResult {
    int   hit;             // 1 if something was hit
    float t;               // distance along ray to hit point
    float3 point;          // world-space hit position
    float3 normal;         // surface normal at hit point
    float4 color;          // surface color
    float reflectivity;
    float ior;
};

struct RTGlobalConstants {
    uint sphereCount;
    uint lightCount;
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
[[vk::binding(0, 1)]] StructuredBuffer<Sphere>   spheres;
[[vk::binding(1, 1)]] RWTexture2D<float4>        outputImage;

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
        result.color = spheres[i].color;
        result.reflectivity = spheres[i].reflectivity;
        result.ior = spheres[i].ior;
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

float3 castRay(Ray rayInitial) {

    float3 color = float3(0.0);
    CastRayStackNode stack[8];
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

            float diffuse_light_intensity = 0, specular_light_intensity = 0;
            for (uint i = 0; i < pc.lightCount; i++) {
                float3 light_dir = normalize(lights[i].position - hit.point);
                float light_distance = length(lights[i].position - hit.point);

                float3 shadow_orig = select(dot(light_dir, hit.normal) < 0, hit.point - hit.normal * 1e-3, hit.point + hit.normal * 1e-3); // checking if the point lies in the shadow of the lights[i]
                HitResult hitResult = traceScene(Ray(shadow_orig, light_dir));

                // shadow ray
                if (hitResult.hit != 0 && length(hit.point - shadow_orig) < light_distance) {
                    continue;
                }

                // local illumination: diffuse, specular
                diffuse_light_intensity += lights[i].intensity * max(0.0, dot(light_dir, hit.normal));
                specular_light_intensity += pow(
                    max(0.0, 
                        -dot(reflect(-light_dir, hit.normal), ray.direction)),
                    60 /* specular component, TODO */
                ) * lights[i].intensity;
            }
            float3 localShading = hit.color.xyz * diffuse_light_intensity * 0.1 /* material.albedo[0], TODO */
                                  + specular_light_intensity * 0.8;             /* material.albedo[1], TODO */
            color += throughput * localShading;
            //*************************************************************************************************************
            // Note: (1) recursive calling reflect() and refract() without conditioning
            //       (2) refract() use default value of refraction ray and recursive calling itself for changing inside-out
            //**************************************************************************************************************
            float3 reflect_dir = normalize(reflectDir(ray.direction, hit.normal));
            float3 refract_dir = normalize(refractDir(ray.direction, hit.normal, hit.ior, 1.0));
            float3 reflect_orig = select( dot(reflect_dir, hit.normal) < 0, hit.point - hit.normal * 1e-3, hit.point + hit.normal * 1e-3); // offset the original point to avoid occlusion by the object itself
            float3 refract_orig = select(dot(refract_dir, hit.normal) < 0, hit.point - hit.normal * 1e-3, hit.point + hit.normal * 1e-3);
            if (depth < 4 && stackPtr < 8) {
                float3 next_refract_throughput = throughput * 0.0; /* material.albedo[3] */ 
                if (any(next_refract_throughput > 0.0)) {
                    stack[stackPtr].ray = Ray(refract_orig, refract_dir);
                    stack[stackPtr].throughput = next_refract_throughput;
                    stack[stackPtr].depth = depth + 1;
                    stackPtr += 1;
                }

                float3 next_reflect_throughput = throughput * 0.2; /* material.albedo[2] */
                if (stackPtr < 8) {
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
