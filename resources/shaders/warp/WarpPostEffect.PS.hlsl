Texture2D sceneTex : register(t0);
SamplerState samp : register(s0);

cbuffer WarpParam : register(b0)
{
    float2 gCenter;
    float gRadius;
    float gStrength;
    float gTime;

    float2 gCenter2;
    float gRadius2;
    float gStrength2;
    float gEnabled;

    float2 gSlashStart;
    float2 gSlashEnd;
    float gSlashThickness;
    float gSlashStrength;
    float gSlashEnabled;
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

float2 CalcLocalWarpOffset(
    float2 uv,
    float2 center,
    float radius,
    float strength,
    float time,
    float swirlScale,
    float ringScale)
{
    if (radius <= 0.00001f || strength <= 0.00001f)
    {
        return float2(0.0f, 0.0f);
    }

    float2 d = uv - center;
    float dist = length(d);

    if (dist >= radius)
    {
        return float2(0.0f, 0.0f);
    }

    float2 dir = SafeNormalize(d);
    float2 tangent = float2(-dir.y, dir.x);

    float t = 1.0f - saturate(dist / radius);
    float core = t * t;

    // 軽い吸い込み
    float pull = -strength * core * 0.55f;

    // メルゼナ風に、ねじれは控えめ
    float swirl = sin(time * 12.0f + dist * 42.0f) * strength * swirlScale * core;

    // 到着時にだけ効きやすい輪状ショック
    float ring = sin(dist * 70.0f - time * 18.0f) * strength * ringScale * core;

    return dir * (pull + ring) + tangent * swirl;
}

float2 CalcSlashWarpOffset(
    float2 uv,
    float2 startUv,
    float2 endUv,
    float thickness,
    float strength,
    float time)
{
    if (thickness <= 0.00001f || strength <= 0.00001f)
    {
        return float2(0.0f, 0.0f);
    }

    float2 seg = endUv - startUv;
    float segLenSq = dot(seg, seg);
    if (segLenSq <= 0.0000001f)
    {
        return float2(0.0f, 0.0f);
    }

    float t = saturate(dot(uv - startUv, seg) / segLenSq);
    float2 nearest = lerp(startUv, endUv, t);
    float2 d = uv - nearest;
    float dist = length(d);
    if (dist >= thickness)
    {
        return float2(0.0f, 0.0f);
    }

    float2 dir = normalize(seg);
    float2 perp = float2(-dir.y, dir.x);
    float side = dot(d, perp);
    float sideSign = (side >= 0.0f) ? 1.0f : -1.0f;

    float lineFade = 1.0f - saturate(dist / thickness);
    lineFade *= lineFade;
    float tipFade = sin(t * 3.14159265f);
    tipFade = max(tipFade, 0.0f);
    tipFade *= tipFade;

    float pulse = 0.75f + 0.25f * sin(time * 28.0f + t * 21.0f);
    float ripple = sin(time * 24.0f + t * 32.0f - dist * 210.0f);
    float sweep = 0.45f + 0.55f * t;

    float sidePush = strength * lineFade * tipFade * pulse;
    float forwardPull = strength * 0.45f * lineFade * sweep * ripple;

    return perp * sideSign * sidePush + dir * forwardPull;
}

float4 main(PSInput input) : SV_TARGET
{
    float2 uv = input.uv;

    if (gEnabled > 0.5f)
    {
        // departure 側はかなり弱く
        float2 offsetDeparture = CalcLocalWarpOffset(
            uv,
            gCenter2,
            gRadius2,
            gStrength2,
            gTime + 0.11f,
            0.08f,
            0.06f);

        // arrival 側を主役にする
        float2 offsetArrival = CalcLocalWarpOffset(
            uv,
            gCenter,
            gRadius,
            gStrength,
            gTime,
            0.14f,
            0.22f);

        // 長い裂け目はやめて、中心間のごく短い局所乱れだけ残す
        float2 ab = gCenter - gCenter2;
        float abLen = length(ab);
        float2 shortRiftOffset = float2(0.0f, 0.0f);

        if (abLen > 0.0001f && gStrength > 0.0001f && gStrength2 > 0.0001f)
        {
            float2 dir = ab / abLen;
            float2 perp = float2(-dir.y, dir.x);

            float midT = 0.75f; // 到着側寄り
            float2 mid = lerp(gCenter2, gCenter, midT);

            float2 dm = uv - mid;
            float distMid = length(dm);
            float localRadius = min(gRadius, 0.045f);

            if (distMid < localRadius)
            {
                float localFade = 1.0f - saturate(distMid / localRadius);
                localFade *= localFade;

                float side = sign(dot(dm, perp));
                if (side == 0.0f)
                    side = 1.0f;

                float flicker = sin(gTime * 26.0f + dot(dm, dir) * 180.0f);
                shortRiftOffset =
                    perp * side * (gStrength * 0.10f) * localFade +
                    dir * (gStrength * 0.03f) * localFade * flicker;
            }
        }

        float2 totalOffset = offsetDeparture + offsetArrival + shortRiftOffset;

        if (gSlashEnabled > 0.5f)
        {
            totalOffset += CalcSlashWarpOffset(
                uv,
                gSlashStart,
                gSlashEnd,
                gSlashThickness,
                gSlashStrength,
                gTime);
        }

        uv += totalOffset;
        uv = clamp(uv, float2(0.001f, 0.001f), float2(0.999f, 0.999f));
    }
    else if (gSlashEnabled > 0.5f)
    {
        uv += CalcSlashWarpOffset(
            uv,
            gSlashStart,
            gSlashEnd,
            gSlashThickness,
            gSlashStrength,
            gTime);
        uv = clamp(uv, float2(0.001f, 0.001f), float2(0.999f, 0.999f));
    }

    return sceneTex.Sample(samp, uv);
}
