// pathtracer.rgen.slang
// Monte Carlo path tracing ray generation shader.
// Samples BSDF randomly, uses NEE for direct lighting, accumulates over frames.

#include "pathtracer_common.slang"

RayDesc generateCameraRay(uint2 pixel, uint2 imageSize, inout uint seed)
{
    float2 jitter = rand2d(seed) - 0.5;
    float2 ndc;
    ndc.x = (float(pixel.x) + 0.5 + jitter.x) / float(imageSize.x) * 2.0 - 1.0;
    ndc.y = (float(pixel.y) + 0.5 + jitter.y) / float(imageSize.y) * 2.0 - 1.0;

    float4 nearPoint = mul(camera.invViewProj, float4(ndc.x, ndc.y, 0.0, 1.0));
    float4 farPoint  = mul(camera.invViewProj, float4(ndc.x, ndc.y, 1.0, 1.0));
    nearPoint /= nearPoint.w;
    farPoint  /= farPoint.w;

    RayDesc ray;
    ray.Origin = camera.position.xyz;
    ray.Direction = normalize(farPoint.xyz - nearPoint.xyz);
    ray.TMin = 0.001;
    ray.TMax = 1e30;
    return ray;
}

[shader("raygeneration")]
void rayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 imageSize = DispatchRaysDimensions().xy;
    if (pixel.x >= imageSize.x || pixel.y >= imageSize.y) return;

    // Seed from pixel index and accumulated sample count
    float3 accumColor = float3(0.0);

    uint pixelIdx = pixel.y * imageSize.x + pixel.x;
    for (int s = 0; s < SAMPLES_PER_FRAME; s++) {
        // Unique seed per pixel, per sample, per frame accumulation count
        uint seed = wangHash(pixelIdx) ^ wangHash(pc.totalAccumSamples + uint(s)) ^ 0xDEADBEEFu;
        float3 pathColor = float3(0.0);
        float3 throughput = float3(1.0);

        RayDesc ray = generateCameraRay(pixel, imageSize, seed);

        for (int bounce = 0; bounce < MAX_BOUNCES; bounce++) {
            Payload payload;
            payload.hitKind = 0;
            // RAY_FLAG_NONE = 0x0
            TraceRay(tlas, 0x0, 0xFF, 0, 0, 0, ray, payload);

            if (payload.hitKind == 0) {
                // Miss
                pathColor += throughput * getBackground(ray.Direction);
                break;
            }

            float3 V = -ray.Direction;
            float3 albedo = payload.diffuseColor;
            float a = max(payload.roughness, 0.001);
            float metallic = payload.metallic;
            float transparency = payload.transparency;
            float ior = max(payload.ior, 1.001);

            // Geometric normal (always outward-facing from mesh)
            float3 geoN = payload.hitNormal;
            // Shading normal (faces V for BRDF evaluation)
            float3 shadeN = dot(geoN, V) >= 0.0 ? geoN : -geoN;

            // ── NEE: explicit direct lighting ──────────────────────
            float3 dirContrib = evalDirect(payload.hitPosition, shadeN, V, albedo, a, metallic, transparency, seed);
            pathColor += throughput * dirContrib;

            // ── Emission ───────────────────────────────────────────
            pathColor += throughput * payload.emission;

            // ── Reflection vs Refraction (Fresnel-based) ──────────
            float3 wi;

            if (transparency > 0.01) {
                // Dielectric: Fresnel decides reflection vs refraction
                // Determine entering/exiting from ray direction vs geometric normal
                bool isInside = dot(ray.Direction, geoN) > 0.0;
                float3 faceN = isInside ? -geoN : geoN;

                float n1 = isInside ? ior : 1.0;
                float n2 = isInside ? 1.0 : ior;
                float eta = n1 / n2;

                float cosThetaI = abs(dot(V, faceN));
                float sin2T = eta * eta * (1.0 - cosThetaI * cosThetaI);

                float3 F0 = float3((n1 - n2) / (n1 + n2));
                F0 = F0 * F0;
                float cosThetaF = (eta > 1.0) ? sqrt(max(0.0, 1.0 - sin2T)) : cosThetaI;
                float3 fresnelR = F0 + (float3(1.0) - F0) * pow(max(0.0, 1.0 - cosThetaF), 5.0);
                float reflProb = (fresnelR.x + fresnelR.y + fresnelR.z) / 3.0;

                if (sin2T >= 1.0 || rand1d(seed) < reflProb) {
                    // Reflection (including TIR)
                    // Fresnel reflectance governs reflected energy; roughness only affects
                    // the directional distribution of the BSDF lobe, not total energy.
                    wi = reflect(ray.Direction, faceN);
                    throughput *= fresnelR;
                    ray.Origin = payload.hitPosition + faceN * 1e-3;
                    ray.Direction = wi;
                } else {
                    // Refraction
                    float cosT = sqrt(1.0 - sin2T);
                    wi = normalize(ray.Direction * eta + faceN * (eta * cosThetaI - cosT));
                    throughput *= (float3(1.0) - fresnelR) * transparency;
                    ray.Origin = payload.hitPosition - faceN * 1e-3;
                    ray.Direction = wi;
                }
            } else {
                // ── Sample BSDF for next direction ─────────────────
                float2 r2d = rand2d(seed);
                float pdfVal;
                float3 brdfVal;

                float specProb = metallic + (1.0 - metallic) * 0.5;
                if (rand1d(seed) < specProb) {
                    wi = sampleGGX(r2d, shadeN, V, a);
                    pdfVal = pdfGGX(shadeN, V, wi, a) * specProb;
                    if (pdfVal < 1e-6) { pdfVal = 1.0; wi = sampleCosineHemisphere(r2d); wi = toWorld(wi, shadeN, buildLocalFrame(shadeN)); }
                    brdfVal = evalBRDF(shadeN, V, wi, albedo, a, metallic, 0.0);
                    float3 diffContrib = albedo * (1.0 - metallic) / PI;
                    float3 specContrib = brdfVal - diffContrib;
                    brdfVal = specContrib + diffContrib * (1.0 - metallic);
                } else {
                    wi = sampleCosineHemisphere(r2d);
                    float3 T = buildLocalFrame(shadeN);
                    wi = toWorld(wi, shadeN, T);
                    pdfVal = pdfCosineHemisphere(dot(shadeN, wi)) * (1.0 - specProb);
                    brdfVal = evalBRDF(shadeN, V, wi, albedo, a, metallic, 0.0);
                }

                float cosWi = dot(shadeN, wi);
                if (cosWi <= 0.0 || pdfVal <= 1e-6) break;

                throughput *= brdfVal * cosWi / pdfVal;
                ray.Origin = payload.hitPosition + shadeN * 1e-3;
                ray.Direction = wi;
            }

            // ── Russian roulette ───────────────────────────────────
            if (bounce >= 2) {
                float survival = min(max(throughput.x, max(throughput.y, throughput.z)), 0.9);
                if (rand1d(seed) > survival) break;
                throughput /= survival;
            }
        }

        accumColor += pathColor;
    }

    accumColor /= float(SAMPLES_PER_FRAME);

    // ── Accumulate over frames ─────────────────────────────────
    float4 oldAccum = accumBuffer[pixel];
    if (pc.totalAccumSamples == 0) oldAccum = float4(0.0, 0.0, 0.0, 0.0); // reset on camera/scene change
    float3 newAccum = (oldAccum.rgb * oldAccum.w + accumColor) / (oldAccum.w + 1.0);
    accumBuffer[pixel] = float4(newAccum, oldAccum.w + 1.0);

    outputImage[pixel] = float4(newAccum, 1.0);
}
