cbuffer SlashParam : register(b0)
{
    float2 gStartUv;
    float2 gEndUv;

    float gCoreThickness;
    float gGlowThickness;
    float gPhaseAlpha;
    float gActionTime;

    float4 gInnerColor;
    float4 gOuterColor;
    float4 gDarkColor;

    float gTipFade;
    float gNoiseScale;
    float gSweepFlag;
    float gPassMode;

    float gStreakOffset;
    float gStreakThicknessMul;
    float gStreakIntensity;
    float gReserved1;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float Hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float Noise21(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);

    float a = Hash21(i);
    float b = Hash21(i + float2(1.0, 0.0));
    float c = Hash21(i + float2(0.0, 1.0));
    float d = Hash21(i + float2(1.0, 1.0));

    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float DistanceToSegment(float2 p, float2 a, float2 b, out float t)
{
    float2 ab = b - a;
    float abLenSq = max(dot(ab, ab), 1e-6);
    t = saturate(dot(p - a, ab) / abLenSq);
    float2 closest = a + ab * t;
    return length(p - closest);
}

float mainMask(float2 uv, float2 startUv, float2 endUv, float thickness,
               out float alongT, out float distToLine)
{
    distToLine = DistanceToSegment(uv, startUv, endUv, alongT);
    return 1.0 - smoothstep(thickness, thickness * 1.6, distToLine);
}

float4 main(PSInput input) : SV_TARGET
{
    if (gPhaseAlpha <= 0.001f)
    {
        discard;
    }

    float2 uv = input.uv;

    float alongT = 0.0;
    float distToLine = 0.0;
    float lineMask = mainMask(uv, gStartUv, gEndUv, gGlowThickness, alongT, distToLine);

    if (lineMask <= 0.0001f)
    {
        discard;
    }

    float2 dir = normalize(gEndUv - gStartUv + 1e-6.xx);
    float2 nrm = float2(-dir.y, dir.x);

    float signedDist = dot(uv - lerp(gStartUv, gEndUv, alongT), nrm);

    float2 localUv;
    localUv.x = alongT;
    localUv.y = 0.5 + signedDist / max(gGlowThickness, 1e-5);

    float noiseA = Noise21(float2(localUv.x * gNoiseScale + gActionTime * 7.0,
                                  localUv.y * 7.0 + gActionTime * 3.0));
    float noiseB = Noise21(float2(localUv.x * (gNoiseScale * 0.65) - gActionTime * 5.0,
                                  localUv.y * 11.0 - gActionTime * 2.0));
    float noiseC = Noise21(float2(localUv.x * (gNoiseScale * 1.35) + gActionTime * 9.0,
                                  localUv.y * 15.0 - gActionTime * 4.5));

    float edgeNoise = (noiseA * 0.50 + noiseB * 0.30 + noiseC * 0.20);
    float tear = smoothstep(0.22, 0.96, edgeNoise);

    // 左右対称すぎるので、片側だけ少し膨らませる
    float asym =
        lerp(-0.16, 0.18, smoothstep(0.12, 0.88, alongT)) *
        lerp(0.65, 1.0, gSweepFlag);

    float signedBias = signedDist + gGlowThickness * asym;

    float coreMask =
        1.0 - smoothstep(gCoreThickness * 0.60, gCoreThickness * 1.18, abs(signedBias));

    float glowMask =
        1.0 - smoothstep(gGlowThickness * 0.42, gGlowThickness * 1.18, abs(signedBias));

    float tipHead = smoothstep(0.0, 0.05, alongT);
    float tipTail = pow(1.0 - smoothstep(gTipFade, 1.0, alongT), 1.8);

    // 後半で細く抜ける感じを少し強める
    float bladeTail = 1.0 - smoothstep(0.72, 1.0, alongT);
    float tipMask = tipHead * tipTail * lerp(1.0, bladeTail + 0.25, 0.55);

    float breakMask = saturate(glowMask - (1.0 - tear) * 0.58);

    float sweepAccent = lerp(0.0, smoothstep(0.15, 0.85, alongT), gSweepFlag);

    float3 coreColor = gInnerColor.rgb * (1.55 + 0.22 * sin(gActionTime * 42.0));
    float3 glowColor = gOuterColor.rgb * (0.92 + 0.42 * sweepAccent);
    float3 darkColor = gDarkColor.rgb;

    // 黒い裂け線を強める
    float streakLine =
        exp(-abs(signedDist + gGlowThickness * (0.10 + 0.08 * sin(alongT * 10.0))) /
            max(gCoreThickness * 1.6, 1e-5));

    float streakNoise =
        0.55 + 0.45 * sin(alongT * 17.0 + gActionTime * 16.0 + noiseB * 5.0);

    float darkBand =
        saturate(streakLine * streakNoise) *
        smoothstep(0.04, 0.20, alongT) *
        (1.0 - smoothstep(0.82, 1.0, alongT));

    // 外周も少し千切る
    float outerFray =
        saturate((1.0 - abs(localUv.y - 0.5) * 2.0) * 0.65 + (tear - 0.5) * 0.9);

    float pulse = 0.94 + 0.06 * sin(gActionTime * 60.0 + alongT * 8.0);

    if (gPassMode > 0.5)
    {
        // 黒 streak 専用
        float streakCenter =
            gStreakOffset +
            0.06 * sin(alongT * 12.0 + gActionTime * 10.0) +
            (noiseB - 0.5) * 0.10;

        float streakDist = abs(localUv.y - (0.5 + streakCenter));
        float streakCore =
            1.0 - smoothstep(0.02 * gStreakThicknessMul,
                             0.16 * gStreakThicknessMul,
                             streakDist);

        float streakBreak =
            smoothstep(0.18, 0.90, noiseA) *
            (0.65 + 0.35 * sin(alongT * 20.0 + gActionTime * 16.0 + noiseC * 4.0));

        float streakMask =
            saturate(streakCore * streakBreak) *
            smoothstep(0.05, 0.20, alongT) *
            (1.0 - smoothstep(0.84, 1.0, alongT)) *
            tipMask;

        float streakAlpha = saturate(streakMask * gPhaseAlpha * gStreakIntensity * 1.6);
        float3 streakColor = float3(0.0, 0.0, 0.0);

        return float4(streakColor, streakAlpha);
    }
    else
    {
        float3 color = 0.0.xxx;
        color += coreColor * coreMask * 1.45;
        color += glowColor * breakMask * outerFray * 0.88;
        color -= darkColor * darkBand * 0.28;

        float alpha =
            saturate((coreMask * 0.95 + glowMask * 0.55) * tipMask * gPhaseAlpha * pulse);

        color = max(color, 0.0.xxx);
        return float4(color, alpha);
    }
}