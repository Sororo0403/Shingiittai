struct ParticleGpu
{
    float3 position;
    float life;

    float3 velocity;
    float maxLife;

    float4 color;

    float2 size;
    float rotation;
    float alive;

    float3 accel;
    float type;
};

struct CameraCB
{
    float4x4 viewProj;
    float4x4 invView;
};

cbuffer gCamera : register(b0)
{
    CameraCB gCameraData;
}

StructuredBuffer<ParticleGpu> gParticles : register(t0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float life01 : TEXCOORD2;
    float type : TEXCOORD3;
};

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output;

    ParticleGpu p = gParticles[instanceID];

    // quad corners for triangle strip
    float2 quad;
    if (vertexID == 0)
    {
        quad = float2(-1.0, -1.0);
        output.uv = float2(0.0, 1.0);
    }
    else if (vertexID == 1)
    {
        quad = float2(-1.0, 1.0);
        output.uv = float2(0.0, 0.0);
    }
    else if (vertexID == 2)
    {
        quad = float2(1.0, -1.0);
        output.uv = float2(1.0, 1.0);
    }
    else
    {
        quad = float2(1.0, 1.0);
        output.uv = float2(1.0, 0.0);
    }

    float life01 = saturate(p.life / max(p.maxLife, 1e-5));
    output.life01 = life01;
    output.type = p.type;

    if (p.alive < 0.5 || life01 <= 0.0)
    {
        output.position = float4(0.0, 0.0, 0.0, 0.0);
        output.color = float4(0.0, 0.0, 0.0, 0.0);
        return output;
    }

    // カメラ right / up を invView から取る
    float3 camRight = normalize(float3(gCameraData.invView[0][0],
                                       gCameraData.invView[0][1],
                                       gCameraData.invView[0][2]));
    float3 camUp = normalize(float3(gCameraData.invView[1][0],
                                       gCameraData.invView[1][1],
                                       gCameraData.invView[1][2]));

    float3 velDir = normalize(p.velocity + float3(1e-5, 0.0, 1e-5));

    // 横幅は camRight 基準、縦方向は velocity に少し寄せる
    float stretchBlend = saturate(length(p.velocity) * 0.05);
    float3 majorAxis = normalize(lerp(camUp, velDir, 0.72 * stretchBlend));
    float3 minorAxis = normalize(cross(majorAxis, float3(gCameraData.invView[2][0],
                                                         gCameraData.invView[2][1],
                                                         gCameraData.invView[2][2])));
    if (length(minorAxis) < 1e-5)
    {
        minorAxis = camRight;
    }
    minorAxis = normalize(minorAxis);

    float s = sin(p.rotation);
    float c = cos(p.rotation);

    float2 rotated =
        float2(quad.x * c - quad.y * s,
               quad.x * s + quad.y * c);

    float3 worldPos =
        p.position +
        minorAxis * (rotated.x * p.size.x) +
        majorAxis * (rotated.y * p.size.y);

    float4 clip = mul(float4(worldPos, 1.0), gCameraData.viewProj);
    output.position = clip;
    output.color = p.color;

    return output;
}