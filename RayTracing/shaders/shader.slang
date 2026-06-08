struct VSInput {
    [[vk::location(0)]] float3 localPos : POSITION;
    [[vk::location(1)]] float2 texCoord : TEXCOORD;
    [[vk::location(2)]] float4x4 transform;
    [[vk::location(6)]] float4 instanceColor : COLOR;
    [[vk::location(7)]] float3 normal : NORMAL;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float4 color;
    float4 cameraViewPos;
    float3 normal;
    float2 texCoord;
    float3 fragPos;
};

struct CameraData {
    float4x4 viewProjection;
    float4x4 invViewProjection;
    float4 cameraViewPos;
};

struct PointLight {
    float3 position;
    float  intensity;
    float3 color;
    float  maxDistance;
};

struct RTGlobalConstants {
    uint  sphereCount;
    uint  lightCount;
    uint  materialCount;
    float ambientStrength;
    float diffuseStrength;
    float specularStrength;
};

[[vk::push_constant]] RTGlobalConstants pc;

[[vk::binding(0, 0)]] ConstantBuffer<CameraData> cameraBuffer;
[[vk::binding(1, 0)]] StructuredBuffer<PointLight> lights;
[[vk::binding(0, 1)]] Sampler2D texture;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    float4 fragPos4 = mul(input.transform, float4(input.localPos, 1.0));
    output.fragPos = fragPos4.xyz;
    output.pos = mul(
        cameraBuffer.viewProjection,
        fragPos4
    );
    output.color = input.instanceColor;
    output.texCoord = input.texCoord;
    output.cameraViewPos = cameraBuffer.cameraViewPos;
    output.normal = mul((float3x3)(input.transform), input.normal);
    return output;
}

float getDistanceAttenuation(float3 lightPos, float3 fragPos, float intensity, float maxDistance) {
    float dist = distance(lightPos, fragPos);
    float atten = intensity / (1.0 + dist * dist);
    if (dist > maxDistance) atten = 0.0;
    return atten;
}

float3 getDiffuse(float3 norm, float3 lightDir) {
    return pc.diffuseStrength * max(dot(norm, lightDir), 0.0);
}

float3 getBlinnPhongSpecular(float3 lightDir, float3 fragDir, float3 norm, float shininess) {
    float3 halfVector = normalize(lightDir + fragDir);
    float spec = pow(max(0.0, dot(halfVector, norm)), shininess);
    return pc.specularStrength * spec;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    float3 fragDir = normalize(vertIn.cameraViewPos.xyz - vertIn.fragPos);
    float3 norm = normalize(vertIn.normal);

    float3 ambient = pc.ambientStrength * float3(0.1, 0.1, 0.1);

    float3 totalLight = ambient;
    for (uint i = 0; i < pc.lightCount; i++) {
        float atten = getDistanceAttenuation(
            lights[i].position, vertIn.fragPos,
            lights[i].intensity, lights[i].maxDistance);

        float3 lightDir = normalize(lights[i].position - vertIn.fragPos);
        float3 diffuse = getDiffuse(norm, lightDir);
        float3 specular = getBlinnPhongSpecular(lightDir, fragDir, norm, 64);

        totalLight += atten * (diffuse + specular) * lights[i].color;
    }

    totalLight = min(totalLight, float3(3.0));

    float4 texColor = texture.Sample(vertIn.texCoord);
    return vertIn.color * float4(totalLight, 1.0) * texColor;
}
