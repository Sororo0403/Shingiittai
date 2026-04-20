cbuffer TrailMaterial : register(b0)
{
    float4x4 gViewProj;

    float4 gInnerColor;
    float4 gOuterColor;
    float4 gDarkColor;

    float gGlobalAlpha;
    float gNoiseScale;
    float gCorePower;
    float gEdgePower;

    float gTime;
    float gPad1;
    float gPad2;
    float gPad3;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float  side     : TEXCOORD0;
    float  t        : TEXCOORD1;
    float  alpha    : TEXCOORD2;
    float  width    : TEXCOORD3;
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

float4 main(PSInput input) : SV_TARGET
{
    float u = input.t;
    float v = input.side; // 0=base, 1=tip

    // 帯の中央からの距離
    float centerDist = abs(v - 0.5) * 2.0;

    float noiseA = Noise21(float2(u * gNoiseScale + gTime * 4.0,
                                  v * 8.0 + gTime * 2.0));
    float noiseB = Noise21(float2(u * (gNoiseScale * 0.55) - gTime * 2.5,
                                  v * 13.0 - gTime * 1.5));
    float noiseC = Noise21(float2(u * (gNoiseScale * 1.35) + gTime * 6.0,
                                  v * 17.0 - gTime * 3.5));

    float noiseMix = noiseA * 0.50 + noiseB * 0.30 + noiseC * 0.20;

    // ---------------------------------------------------------
    // 形
    // ---------------------------------------------------------

    // 黒芯: 中央をしっかり暗く残す
    float darkCore = 1.0 - smoothstep(0.00, 0.42, centerDist);

    // 紫外周: 中心より外側に出す
    float outerBand =
        smoothstep(0.10, 0.42, centerDist) *
        (1.0 - smoothstep(0.58, 1.02, centerDist));

    // 白ハイライト: 片側にだけ少し寄せる
    float highlightCenter = 0.34 + (noiseB - 0.5) * 0.08;
    float highlightDist = abs(v - highlightCenter);
    float whiteHighlight =
        1.0 - smoothstep(0.00, 0.10, highlightDist);

    // ---------------------------------------------------------
    // 時間方向の見え方
    // ---------------------------------------------------------

    float headFade = smoothstep(0.0, 0.03, u);
    float tailFade = 1.0 - smoothstep(0.82, 1.0, u);
    float trailFade = headFade * tailFade;

    // 後半で少し強く見せる
    float alongBoost = lerp(0.72, 1.18, smoothstep(0.10, 0.72, u));

    // 先端は細く鋭く
    float tipSharp = 1.0 - smoothstep(0.76, 1.0, u);
    tipSharp = lerp(1.0, pow(tipSharp, 1.35), 0.65);

    // ---------------------------------------------------------
    // 裂け・ゆらぎ
    // ---------------------------------------------------------

    // 外周をランダムにちぎる
    float edgeBreak = smoothstep(0.22, 0.95, noiseMix);
    float fray = saturate(outerBand - (1.0 - edgeBreak) * 0.45);

    // 黒芯も少し波打たせる
    float darkRipple =
        0.82 + 0.18 * sin(u * 22.0 + gTime * 7.0 + noiseA * 4.0);

    // 白ハイライトは連続しすぎないよう切る
    float whiteBreak =
        smoothstep(0.28, 0.88, noiseC) *
        (0.72 + 0.28 * sin(u * 18.0 + gTime * 9.0));

    // ---------------------------------------------------------
    // 色
    // ---------------------------------------------------------

    float3 color = 0.0.xxx;

    // まず黒芯を主役に
    color += gDarkColor.rgb * darkCore * darkRipple * 1.15;

    // 紫外周
    color += gOuterColor.rgb * fray * alongBoost * 1.22;

    // 白は控えめに、外周の一部だけ
    color += gInnerColor.rgb * whiteHighlight * whiteBreak * alongBoost * 0.55;

    // ほんの少し紫を後半に寄せて強める
    color *= lerp(0.90, 1.08, smoothstep(0.18, 0.78, u));

    // ---------------------------------------------------------
    // α
    // ---------------------------------------------------------

    float alpha =
        saturate((darkCore * 0.30 + fray * 0.95 + whiteHighlight * 0.35) *
                 trailFade *
                 tipSharp *
                 input.alpha *
                 gGlobalAlpha);

    if (alpha <= 0.001f)
    {
        discard;
    }

    return float4(color, alpha);
}