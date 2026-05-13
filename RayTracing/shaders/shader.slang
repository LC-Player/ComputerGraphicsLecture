struct VSInput {
    [[vk::location(0)]] float3 localPos : POSITION;
    [[vk::location(1)]] float2 texCoord : TEXCOORD;
    [[vk::location(2)]] float4x4 transform;
    [[vk::location(6)]] float4 instanceColor : COLOR;
    [[vk::location(7)]] float3 normal : POSITION;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float4 color;
    float4 cameraViewPos;
    float3 normal;
    float2 texCoord;
};

struct CameraData {
    float4x4 viewProjection;
    float4 cameraViewPos;
}


struct PointLight {
    float3 position;      // offset 0,  aligned 16
    float3 color;         // offset 16, aligned 16
    float  intensity;     // offset 32
    float  maxDistance;   // offset 36
    float2 _pad;          // offset 40 — 填充到 48
};

struct SpotLight {
    float3 position;      // offset 0
    float3 color;         // offset 16
    float3 direction;     // offset 32
    float  intensity;     // offset 44
    float  maxDistance;   // offset 48
    float  innerAngleCos; // offset 52
    float  outerAngleCos; // offset 56
    float  _pad;          // offset 60 — 填充到 64
};

struct DirectionalLight {
    float3 direction;     // offset 0
    float3 color;         // offset 16
    float  intensity;     // offset 32
    float3 _pad;          // 填充到 48
};

struct LightInfo {
    PointLight pointLight1;
    PointLight pointLight2;
    SpotLight spotLight;
    DirectionalLight directionalLight;
    float4 ambientArgs;  // ambientColor = (x, y, z), w : strength
    float diffuseStrength; // 漫反射
    float specularStrength; // 镜面反射
    float2 _pad;
};


[[vk::binding(0, 0)]] ConstantBuffer<CameraData> cameraBuffer;
[[vk::binding(1, 0)]] ConstantBuffer<LightInfo> lightInfo;
[[vk::binding(0, 1)]] Sampler2D texture;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = mul(
        cameraBuffer.viewProjection,
        mul(input.transform, float4(input.localPos, 1.0))
    );
    output.color = input.instanceColor;
    output.texCoord = input.texCoord;
    output.cameraViewPos = cameraBuffer.cameraViewPos;
    output.normal = input.normal;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    float4 texColor = texture.Sample(vertIn.texCoord);
    return vertIn.color * texColor;
}
