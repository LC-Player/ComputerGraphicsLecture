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
}


struct PointLight {
    float3 position;
    float3 color;
    float  intensity;
    float  maxDistance;
    float2 _pad;
};

struct SpotLight {
    float3 position;
    float3 color;
    float3 direction;
    float  innerAngleCos;
    float  outerAngleCos;
    float  intensity;
    float  maxDistance;
    float  _pad;
};

struct DirectionalLight {
    float3 direction;
    float3 color;
    float  intensity;
    float3 _pad;
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
    output.normal = mul((float3x3)(input.transform), input.normal);
    return output;
}

float getDistanceAttenuation(float3 position, float3 fragPos, float intensity, float maxDistance) {
    float lightToPosDistance = distance(position, fragPos); // 光到片元的距离
    // attenuation: 衰减，intensity：强度
    // 衰减之后的强度
    float lightAttenuation = intensity / (1 + lightToPosDistance * lightToPosDistance);

    // 超过最大距离归零
    if (lightToPosDistance > maxDistance){
        lightAttenuation = 0.0;
    }
    return lightAttenuation;
}
/**
  * @param norm - from frag, normalized
  * @param lightDir - from frag to light, normalized
  */
float3 getDiffuse(float3 norm, float3 lightDir, float diffuseStrength) {
    float cosAngle = max(dot(norm, lightDir), 0.0); // 夹角
    return diffuseStrength * cosAngle; // 漫反射强度 * 有多少是有效的
}

/**
  * @param lightDir - from frag to light, normalized
  * @param fragDir - from frag to camera, normalized
  * @param norm - from frag, normalized
  */
float3 getBlinnPhongSpecular(float3 lightDir, float3 fragDir, float3 norm, float shininess) {
    float3 halfVector = normalize(lightDir + fragDir);
    float cosAngle = max(0.0, dot(halfVector, norm));
    float3 specular = pow(cosAngle, shininess);
    specular *= lightInfo.specularStrength;
    return specular;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    float3 fragDir = normalize(vertIn.cameraViewPos.xyz - vertIn.fragPos); // 从片元指向相机
    float3 norm = normalize(vertIn.normal); // 单位化之后的法线

    // ambient light
    float3 ambient = lightInfo.ambientArgs.w * float3(lightInfo.ambientArgs.x, lightInfo.ambientArgs.y, lightInfo.ambientArgs.z);

    // point light 1
    float3 pointResult_1;
    {
        float lightAttenuation = getDistanceAttenuation(
            lightInfo.pointLight1.position,
            vertIn.fragPos,
            lightInfo.pointLight1.intensity,
            lightInfo.pointLight1.maxDistance
        );

        // diffuse 漫反射 specular 镜面反射
        float3 lightDir = normalize(lightInfo.pointLight1.position - vertIn.fragPos); // 从片元指向光源

        float3 diffuse = getDiffuse(norm, lightDir, lightInfo.diffuseStrength);
        float3 specular = getBlinnPhongSpecular(lightDir, fragDir, norm, 64);
        
        pointResult_1 = (specular + diffuse)  * lightAttenuation * lightInfo.pointLight1.color; // 距离衰减后的漫反射强度
    }

    // point light 2
    float3 pointResult_2;
    {
        float lightAttenuation = getDistanceAttenuation(
            lightInfo.pointLight2.position,
            vertIn.fragPos,
            lightInfo.pointLight2.intensity,
            lightInfo.pointLight2.maxDistance
        );

        float3 lightDir = normalize(lightInfo.pointLight2.position - vertIn.fragPos); // 从片元指向光源

        // diffuse 漫反射 specular 镜面反射
        float3 diffuse = getDiffuse(norm, lightDir, lightInfo.diffuseStrength);

        float3 specular = getBlinnPhongSpecular(lightDir, fragDir, norm, 64);

        pointResult_2 = (diffuse + specular) * lightAttenuation * lightInfo.pointLight2.color;
    }

    // spot light
    float3 spotResult;
    {
        float lightAttenuation = getDistanceAttenuation(
            lightInfo.spotLight.position,
            vertIn.fragPos,
            lightInfo.spotLight.intensity,
            lightInfo.spotLight.maxDistance
        );

        float3 lightDir = normalize(lightInfo.spotLight.position - vertIn.fragPos); // 从片元指向光源

        // 圆锥衰减
        float cosine = dot(-lightDir, lightInfo.spotLight.direction);
        float decay = smoothstep(lightInfo.spotLight.outerAngleCos, lightInfo.spotLight.innerAngleCos, cosine); // 最终值

        // diffuse 漫反射 specular 镜面反射
        float3 diffuse = getDiffuse(norm, lightDir, lightInfo.diffuseStrength);

        float3 specular = getBlinnPhongSpecular(lightDir, fragDir, norm, 64);

        spotResult = lightAttenuation * decay * (diffuse + specular) * lightInfo.spotLight.color;
    }

    // directionalLight
    float3 directionalResult;
    {
        // diffuse 漫反射 specular 镜面反射
        float3 lightDir = -lightInfo.directionalLight.direction;

        float3 diffuse = getDiffuse(norm, lightDir, lightInfo.diffuseStrength);

        float3 specular = getBlinnPhongSpecular(lightDir, fragDir, norm, 64);

        directionalResult = (specular + diffuse) * lightInfo.directionalLight.intensity * lightInfo.directionalLight.color;
    }

    float3 totalLight = pointResult_1 + pointResult_2 + spotResult + directionalResult + ambient;
    totalLight = min(totalLight, float3(3.0)); // 不要太炸

    float4 texColor = texture.Sample(vertIn.texCoord);
    return vertIn.color * float4(totalLight, 1.0) * texColor;
}
