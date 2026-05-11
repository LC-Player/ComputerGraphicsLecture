struct VSInput {
    [[vk::location(0)]] float3 localPos : POSITION;
    [[vk::location(1)]] float4 vertexColor : COLOR;
    [[vk::location(2)]] float2 texCoord : TEXCOORD;
    [[vk::location(3)]] float4x4 transform;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float4 color;
    float2 texCoord;
};

struct CameraData {
    float4x4 viewProjection;
}

[[vk::binding(0)]] ConstantBuffer<CameraData> cameraBuffer;
[[vk::binding(0, 1)]] Sampler2D texture;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = mul(
        cameraBuffer.viewProjection,
        mul(input.transform, float4(input.localPos, 1.0))
    );
    output.color = input.vertexColor;
    output.texCoord = input.texCoord;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    float4 texColor = texture.Sample(vertIn.texCoord);
    return vertIn.color * texColor;
}
