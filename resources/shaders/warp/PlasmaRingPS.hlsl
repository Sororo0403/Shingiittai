Texture2D noiseTex0 : register(t1);
Texture2D noiseTex1 : register(t2);
SamplerState samp : register(s0);

cbuffer ElectricRingParam : register(b0)
{
    float2 gCenter;
    float gRadius;
    float gTime;

    float gRingWidth;
    float gDistortionWidth;
    float gDistortionStrength;
    float gSwirlStrength;

    float gCloudScale;
    float gCloudIntensity;
    float gBrightness;
    float gHaloIntensity;

    float2 gAspectInvAspect; // x=aspect, y=1/aspect
    float gInnerFade;
    float gOuterFade;
    float gEnabled;
};

struct PSInput
{
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float Noise0(float2 uv)
{
    return noiseTex0.Sample(samp, uv).r;
}

float Noise1(float2 uv)
{
    return noiseTex1.Sample(samp, uv).r;
}

float FBM(float2 p)
{
    float v = 0.0f;
    float a = 0.5f;
    float2x2 m = float2x2(1.6f, 1.2f, -1.2f, 1.6f);

    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        float n0 = Noise0(p);
        float n1 = Noise1(p * 0.73f + 0.17f);
        float n = (n0 + n1) * 0.5f;
        v += abs(n * 2.0f - 1.0f) * a;
        p = mul(m, p) * 1.9f;
        a *= 0.5f;
    }
    return v;
}

float DualFBM(float2 p, float time)
{
    float2 q = p * 0.7f;
    float bx = FBM(q + float2(-time * 0.24f, time * 0.11f));
    float by = FBM(q + float2(time * 0.17f, -time * 0.19f));
    float2 basis = (float2(bx, by) - 0.5f) * 0.35f;
    p += basis;
    return FBM(p + float2(time * 0.03f, -time * 0.02f));
}

float RingMask(float dist, float radius, float width)
{
    float v = 1.0f - saturate(abs(dist - radius) / max(width, 0.0001f));
    return v;
}

float4 main(PSInput input) : SV_TARGET
{
    if (gEnabled < 0.5f || gRadius <= 0.0001f)
    {
        return float4(0, 0, 0, 0);
    }

    float2 local = input.uv - gCenter;
    local.x *= gAspectInvAspect.x;

    float dist = length(local);

    float2 p = local * gCloudScale;

    // ベースの電気雲
    float cloudA = DualFBM(p * 1.05f, gTime);
    float cloudB = DualFBM(p * 1.75f + 2.3f, gTime * 1.13f);
    float cloud = lerp(cloudA, cloudB, 0.45f);
    cloud = saturate(pow(abs(cloud), 1.35f));

    // 輪の縁をノイズで壊す
    float edgeNoise = DualFBM(p * 2.6f + 1.7f, gTime * 0.9f);
    float radiusOffset = (edgeNoise - 0.5f) * (gRingWidth * 3.5f);

    float ring = RingMask(dist, gRadius + radiusOffset, gRingWidth);
    ring = pow(saturate(ring), 3.5f);

    float halo = RingMask(dist, gRadius + radiusOffset, gRingWidth * 3.0f);
    halo = pow(saturate(halo), 2.0f);

    // リング近傍の雲を強化
    float cloudRingMask = RingMask(dist, gRadius, gRingWidth * 6.0f);
    cloudRingMask = saturate(cloudRingMask);

    // 中心内部にも少し残す
    float innerMask = saturate(1.0f - dist / max(gRadius * max(gInnerFade, 0.1f), 0.0001f));
    innerMask = pow(innerMask, 1.5f);

    float outerMask = saturate(1.0f - dist / max(gRadius * (1.0f + gOuterFade * 2.0f), 0.0001f));

    float plasma = cloud * (0.70f * innerMask + 1.85f * gCloudIntensity * cloudRingMask + 0.55f * outerMask);

    // 色
    float3 baseCloudColor = float3(0.08f, 0.02f, 0.20f);
    float3 plasmaColor = float3(0.2f, 0.0f, 1.0f); // 深い紫
    float3 hotColor = float3(1.0f, 1.0f, 1.0f); // 白飛び

    float pulse = 0.92f + 0.08f * sin(gTime * 22.0f + dist * 110.0f);

    float3 col = 0.0f.xxx;
    col += baseCloudColor * plasma * 0.45f;
    col += plasmaColor * plasma * 1.35f;
    col += plasmaColor * halo * gHaloIntensity * 1.5f;
    col += hotColor * ring * 6.0f * pulse;

    col *= gBrightness;

    // 加算合成前提なので alpha は控えめに使う
    float alpha = saturate(max(max(col.r, col.g), col.b));
    alpha = saturate(alpha * 0.35f + ring * 0.5f);

    return float4(col, alpha);
}