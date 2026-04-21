struct VSInput {
    [[vk::location(0)]] float3 localPos : POSITION;
    [[vk::location(1)]] float4 vertexColor : COLOR;
    [[vk::location(2)]] float4x4 transform;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float4 color;
};

struct CameraData {
    float4x4 viewProjection;
}

[[vk::binding(0)]] ConstantBuffer<CameraData> cameraBuffer;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = mul(
        cameraBuffer.viewProjection, 
        mul(input.transform, float4(input.localPos, 1.0))
    );
    output.color = input.vertexColor;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    return vertIn.color;
}