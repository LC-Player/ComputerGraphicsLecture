// fullscreen.slang
// Minimal fullscreen-quad shader for displaying a texture.
// Renders a single triangle covering the entire viewport without vertex buffers.

[[vk::binding(0, 0)]] Sampler2D rtImage;

struct VSOutput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertMain(uint vertexID : SV_VertexID)
{
    // Generate a fullscreen triangle from vertex ID (no vertex buffer needed):
    //   vertex 0 → (-1, -3)   vertex 1 → (-1,  1)   vertex 2 → ( 3,  1)
    // This covers the entire NDC [-1,1]×[-1,1] with a single triangle.
    float x = (vertexID == 2) ?  3.0 : -1.0;
    float y = (vertexID == 0) ? -3.0 :  1.0;

    VSOutput output;
    output.pos = float4(x, y, 0.0, 1.0);
    output.uv  = float2((x + 1.0) * 0.5, (y + 1.0) * 0.5);
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET
{
    return rtImage.Sample(input.uv);
}
