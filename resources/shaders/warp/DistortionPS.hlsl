Texture2D sceneTex : register(t0);
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

float2 SafeNormalize(float2 v)
{
    float len = length(v);
    if (len < 0.00001f)
    {
        return float2(0.0f, 0.0f);
    }
    return v / len;
}

float SampleNoise0(float2 uv)
{
    return noiseTex0.Sample(samp, uv).r;
}

float SampleNoise1(float2 uv)
{
    return noiseTex1.Sample(samp, uv).r;
}

float CalcRingBand(float dist, float radius, float width)
{
    float v = 1.0f - saturate(abs(dist - radius) / max(width, 0.0001f));
    return v * v;
}

float4 main(PSInput input) : SV_TARGET
{
    float2 uv = input.uv;

    if (gEnabled < 0.5f || gRadius <= 0.0001f || gDistortionStrength <= 0.00001f)
    {
        return sceneTex.Sample(samp, uv);
    }

    float2 local = uv - gCenter;
    local.x *= gAspectInvAspect.x;

    float dist = length(local);
    float2 dir = SafeNormalize(local);
    float2 tangent = float2(-dir.y, dir.x);

    float ringMask = CalcRingBand(dist, gRadius, gDistortionWidth);

    // 輪の外側にも少しだけ余韻を残す
    float haloMask = CalcRingBand(dist, gRadius, gDistortionWidth * 2.2f) * 0.35f;

    float n0 = SampleNoise0(input.uv * 3.2f + float2(gTime * 0.11f, -gTime * 0.07f));
    float n1 = SampleNoise1(input.uv * 5.1f + float2(-gTime * 0.05f, gTime * 0.09f));
    float n = ((n0 + n1) * 0.5f) * 2.0f - 1.0f;

    float pulse = 0.85f + 0.15f * sin(gTime * 18.0f + dist * 80.0f);
    float radialPush = gDistortionStrength * (ringMask + haloMask) * pulse;
    float swirl = gSwirlStrength * (ringMask + haloMask) * n;

    float2 offset = dir * (radialPush * (0.6f + 0.4f * n));
    offset += tangent * swirl;

    // aspect 補正した local を使っているので UV 戻し時に x を戻す
    offset.x *= gAspectInvAspect.y;

    uv += offset;
    uv = clamp(uv, float2(0.001f, 0.001f), float2(0.999f, 0.999f));

    return sceneTex.Sample(samp, uv);
}