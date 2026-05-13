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
    float3 fragPos;
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
    float4 fragPos4 = mul(input.transform, float4(input.localPos, 1.0));
    output.fragPos = fragPos4.xyz;
    output.pos = mul(
        cameraBuffer.viewProjection,
        fragPos4
    );
    output.color = input.instanceColor;
    output.texCoord = input.texCoord;
    output.cameraViewPos = cameraBuffer.cameraViewPos;
    output.normal = input.normal;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    float3 ambient = lightInfo.ambientArgs.w * float3(lightInfo.ambientArgs.x, lightInfo.ambientArgs.y, lightInfo.ambientArgs.z);

    // point light 1
    float3 pointResult_1;
    {
        float lightToPosDistance = distance(lightInfo.pointLight1.position, vertIn.fragPos); // 光到片元的距离
        // attenuation: 衰减，intensity：强度
        // 衰减之后的强度
        float lightAttenuation = lightInfo.pointLight1.intensity / (1 + lightToPosDistance * lightToPosDistance);

        // 超过最大距离归零
        if (lightToPosDistance > lightInfo.pointLight1.maxDistance){
            lightAttenuation = 0.0;
        }
        // diffuse 漫反射 specular 镜面反射
        float3 norm = normalize(vertIn.normal); // for safe, 单位化之后的法线
        float3 lightDir = normalize(lightInfo.pointLight1.position  - vertIn.fragPos); // 从光源指向片元
        float diff = max(dot(norm, lightDir), 0.0); // 夹角
        float3 diffuse = lightInfo.diffuseStrength * diff * lightInfo.pointLight1.color; // 漫反射强度 * 有多少是有效的 * 颜色

        /* --------------------------------------------- */
        // Task 3.9:  Implement Blinn-Phong Shader for point light 1
        /* --------------------------------------------- */
        
        pointResult_1 = diffuse  * lightAttenuation; // 距离衰减后的漫反射强度
    }


    /* --------------------------------------------- */
    // Task 3.10:  Add point light 2 (Blinn-Phong)
    /* --------------------------------------------- */

    // point light 2
    float3 pointResult_2;
    {
        float lightToPosDistance = distance(lightInfo.pointLight2.position, vertIn.fragPos); // 光到片元的距离
        // attenuation: 衰减，intensity：强度
        // 衰减之后的强度
        float lightAttenuation = lightInfo.pointLight2.intensity / (1 + lightToPosDistance * lightToPosDistance);

        // 超过最大距离归零
        if (lightToPosDistance > lightInfo.pointLight2.maxDistance) {
            lightAttenuation = 0.0;
        }
        // diffuse 漫反射 specular 镜面反射
        float3 norm = normalize(vertIn.normal);                                       // for safe, 单位化之后的法线
        float3 lightDir = normalize(lightInfo.pointLight2.position - vertIn.fragPos); // 从光源指向片元
        float diff = max(dot(norm, lightDir), 0.0);                                   // 夹角
        float3 diffuse = lightInfo.diffuseStrength * diff * lightInfo.pointLight2.color; // 漫反射强度 * 有多少是有效的 * 颜色

        /* --------------------------------------------- */
        // Task 3.9:  Implement Blinn-Phong Shader for point light 1
        /* --------------------------------------------- */

        pointResult_2 = diffuse * lightAttenuation; // 距离衰减后的漫反射强度
    }

    /* --------------------------------------------- */
    // Task 3.11:  Add spot light (Blinn-Phong)
    /* --------------------------------------------- */

    // spot light
    //float3 pointResult_3;
    //{
    //    float lightToPosDistance = distance(lightInfo.spotLight.position, vertIn.fragPos); // 光到片元的距离
    //    // attenuation: 衰减，intensity：强度
    //    // 衰减之后的强度
    //    float lightAttenuation = lightInfo.spotLight.intensity / (1 + lightToPosDistance * lightToPosDistance);
    //
    //    // 超过最大距离归零
    //    if (lightToPosDistance > lightInfo.spotLight.maxDistance) {
    //        lightAttenuation = 0.0;
    //    }
    //    // diffuse 漫反射 specular 镜面反射
    //    float3 norm = normalize(vertIn.normal);                                       // for safe, 单位化之后的法线
    //    float3 lightDir = normalize(lightInfo.spotLight.position - vertIn.fragPos); // 从光源指向片元
    //    float diff = max(dot(norm, lightDir), 0.0);                                   // 夹角
    //    float3 diffuse = lightInfo.diffuseStrength * diff * lightInfo.spotLight.color; // 漫反射强度 * 有多少是有效的 * /颜/色
    //
    //    /* --------------------------------------------- */
    //    // Task 3.9:  Implement Blinn-Phong Shader for point light 1
    //    /* --------------------------------------------- */
    //
    //    pointResult_3 = diffuse * lightAttenuation; // 距离衰减后的漫反射强度
    //}

    /* --------------------------------------------- */
    // Task 3.12:  Add directional light (Blinn-Phong)
    /* --------------------------------------------- */

    float3 totalLight = pointResult_1 + pointResult_2 + ambient; // 加上环境光
    totalLight = min(totalLight, float3(3.0)); // 不要太炸

    float4 texColor = texture.Sample(vertIn.texCoord);
    return vertIn.color * float4(totalLight, 1.0) * texColor;
}
