// raytracer.slang
// Compute shader for real-time ray tracing.
// Entry point: computeMain

// ── Data structures (must match CPU-side definitions) ──────────────

struct CameraData {
    float4x4 viewProj;     // unused in compute, needed for UBO layout match
    float4x4 invViewProj;  // inverse(view * projection) for ray generation
    float4 position;       // camera world position (w = sphere count)
};

struct Sphere {
    float3 center;
    float radius;
    float4 color;
    float reflectivity;    // 0.0 = diffuse, 1.0 = perfect mirror
    float refractivity;    // 0.0 = opaque, 1.0 = pure glass
    float ior;             // index of refraction (e.g. 1.5)
    float _pad;
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
    float4 color;          // surface color
    float reflectivity;
    float refractivity;
    float ior;
};

// ── Bindings ──────────────────────────────────────────────────────

[[vk::binding(0, 0)]] ConstantBuffer<CameraData> camera;
[[vk::binding(1, 0)]] StructuredBuffer<Sphere>   spheres;
[[vk::binding(2, 0)]] RWTexture2D<float4>        outputImage;

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

    // sphere count is packed in camera.position.w
    uint sphereCount = uint(camera.position.w);

    for (uint i = 0; i < sphereCount; i++) {
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
        result.refractivity = spheres[i].refractivity;
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
    return lerp(float3(0.5, 0.7, 0.9), float3(0.9, 0.95, 1.0), t);
}

// ── Shading ───────────────────────────────────────────────────────

float3 shadeHit(Ray ray, HitResult hit)
{
    // Normal visualization (MVP)
    float3 color = hit.color.rgb * (hit.normal * 0.5 + 0.5);

    //*****************************************************************
    // TODO C: Blinn-Phong shading
    //   Replace the normal visualization above with proper lighting.
    //
    //   For each light source:
    //     float3 lightDir = normalize(lightPos - hit.point);
    //     float3 viewDir  = -ray.direction;
    //     float3 halfVec  = normalize(lightDir + viewDir);
    //
    //     // Diffuse (Lambert)
    //     float diff = max(dot(hit.normal, lightDir), 0.0);
    //
    //     // Specular (Blinn-Phong)
    //     float spec = pow(max(dot(hit.normal, halfVec), 0.0), 64.0);
    //
    //     // Attenuation
    //     float d = distance(lightPos, hit.point);
    //     float atten = intensity / (1.0 + d * d);
    //
    //     color += atten * (diff + spec) * lightColor * hit.color.rgb;
    //
    //   show your code
    //
    //*****************************************************************

    //*****************************************************************
    // TODO D: Shadow rays
    //   For each light, trace a shadow ray from hit.point towards the
    //   light. If any object blocks it before reaching the light, skip
    //   that light's contribution.
    //
    //   Hint: offset = hit.point + hit.normal * 0.001;
    //         shadowRay = Ray(offset, lightDir);
    //         shadowHit = traceScene(shadowRay);
    //         if (shadowHit.hit && shadowHit.t < lightDistance) → in shadow
    //
    //   show your code
    //
    //*****************************************************************

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
    HitResult hit = traceScene(ray);

    float3 color;
    if (hit.hit != 0)
    {
        color = shadeHit(ray, hit);

        //*************************************************************
        // TODO E: Reflection (recursive ray tracing)
        //   If hit.reflectivity > 0:
        //     float3 reflectDir = reflect(ray.direction, hit.normal);
        //     Ray reflRay = { hit.point + hit.normal*0.001, reflectDir };
        //     HitResult reflHit = traceScene(reflRay);
        //     if (reflHit.hit) ... add reflected color weighted by hit.reflectivity
        //
        //   show your code
        //
        //*************************************************************

        //*************************************************************
        // TODO F: Refraction (transmission)
        //   If hit.refractivity > 0:
        //     Use Snell's law: n1*sin(theta1) = n2*sin(theta2)
        //     eta = (front-face) ? 1.0/hit.ior : hit.ior
        //     float3 refrDir = refract(ray.direction, hit.normal, eta);
        //     Ray refrRay = { hit.point - hit.normal*0.001, refrDir };
        //     HitResult refrHit = traceScene(refrRay);
        //     ... add refracted color weighted by hit.refractivity
        //
        //   show your code
        //
        //*************************************************************
    }
    else
    {
        color = getBackground(ray.direction);
    }

    outputImage[pixel] = float4(color, 1.0);
}
