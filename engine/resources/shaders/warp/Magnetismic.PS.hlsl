cbuffer PSConstants : register(b1)
{
    float gTime;
    float gIntensity;
    float gAlpha;
    float gScale;

    float gSwirlA;
    float gSwirlB;
    float gNoiseScale;
    float gStepScale;

    float4 gColorA;
    float4 gColorB;
    float4 gParams0; // x=brightness y=distFade z=innerBoost w=unused
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

#define HQ_STEPS 64
#define HQ_ALPHA_WEIGHT 0.015
#define HQ_BASE_STEP 0.035

float2 Rot(float2 p, float a)
{
    float c = cos(a);
    float s = sin(a);
    return float2(c * p.x - s * p.y, s * p.x + c * p.y);
}

float Hash21(float2 n)
{
    return frac(sin(dot(n, float2(12.9898, 4.1414))) * 43758.5453);
}

float Hash31(float3 p)
{
    return frac(sin(dot(p, float3(12.9898, 78.233, 37.719))) * 43758.5453123);
}

float Noise3D(float3 p)
{
    float3 ip = floor(p);
    float3 fp = frac(p);

    fp = fp * fp * (3.0 - 2.0 * fp);

    float n000 = Hash31(ip + float3(0.0, 0.0, 0.0));
    float n100 = Hash31(ip + float3(1.0, 0.0, 0.0));
    float n010 = Hash31(ip + float3(0.0, 1.0, 0.0));
    float n110 = Hash31(ip + float3(1.0, 1.0, 0.0));
    float n001 = Hash31(ip + float3(0.0, 0.0, 1.0));
    float n101 = Hash31(ip + float3(1.0, 0.0, 1.0));
    float n011 = Hash31(ip + float3(0.0, 1.0, 1.0));
    float n111 = Hash31(ip + float3(1.0, 1.0, 1.0));

    float nx00 = lerp(n000, n100, fp.x);
    float nx10 = lerp(n010, n110, fp.x);
    float nx01 = lerp(n001, n101, fp.x);
    float nx11 = lerp(n011, n111, fp.x);

    float nxy0 = lerp(nx00, nx10, fp.y);
    float nxy1 = lerp(nx01, nx11, fp.y);

    return lerp(nxy0, nxy1, fp.z);
}

float FBM(float3 p)
{
    p *= max(gNoiseScale, 0.0001);

    float rz = 0.0;
    float z = 1.0;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float n = Noise3D(p - gTime * 0.6);
        rz += (sin(n * 4.4) - 0.45) * z;
        z *= 0.47;
        p *= 3.5;
    }

    return rz;
}

float4 MapMagnet(float3 p)
{
    float dtp = dot(p, p);

    p = 0.5 * p / (dtp + 0.2);

    p.xz = Rot(p.xz, p.y * gSwirlA);
    p.xy = Rot(p.xy, p.y * gSwirlB);

    float dtp2 = dot(p, p);

    float moY = gParams0.z;
    p = (moY + 0.6) * 3.0 * p / max(dtp2 - 5.0, -4.999);

    float r = saturate(FBM(p) * 1.5 - dtp * (gParams0.y - sin(gTime * 0.3) * 0.15));

    float3 baseCol = lerp(gColorB.rgb, gColorA.rgb, r);
    float alpha = 0.96 * r;

    float grd = saturate((dtp + 0.7) * 0.4);
    baseCol = lerp(baseCol, gColorB.rgb, grd * 0.6);

    float3 lv = lerp(p, float3(0.3, 0.3, 0.3), 2.0);
    float grd2 = clamp((alpha - FBM(p + lv * 0.05)) * 2.0, 0.01, 1.5);

    float3 tintDark = float3(0.50, 0.40, 0.65) * grd2;
    float3 tintBright = lerp(float3(2.5, 0.2, 0.8), gColorA.rgb * 3.2, 0.65);
    baseCol *= (tintDark + tintBright);

    alpha *= saturate(dtp * 2.0 - 1.0) * 0.07 + 0.87;

    return float4(baseCol, alpha);
}

float4 VMarch(float3 ro, float3 rd, float2 fragCoordLike)
{
    float4 rz = float4(0.0, 0.0, 0.0, 0.0);

    float t = 2.5;
    t += 0.03 * Hash21(fragCoordLike);

    [loop]
    for (int i = 0; i < HQ_STEPS; ++i)
    {
        if (rz.a > 0.99 || t > 6.0)
        {
            break;
        }

        float3 pos = ro + t * rd;
        float4 col = MapMagnet(pos);

        float den = col.a;

        col.a *= HQ_ALPHA_WEIGHT * gStepScale;
        col.rgb *= col.a * (1.7 * max(gParams0.x, 0.0001));

        rz += col * (1.0 - rz.a);

        float baseStep = HQ_BASE_STEP * gStepScale;
        t += baseStep - den * (baseStep - baseStep * 0.015);
    }

    return rz;
}

float RadialMask(float2 uv)
{
    float2 d = uv * 2.0 - 1.0;
    float r = length(d);

    return 1.0 - smoothstep(0.72, 1.10, r);
}

float4 main(PSInput input) : SV_TARGET
{
    float2 uv = input.uv;

    float2 p = uv * 2.0 - 1.0;
    p.x *= 0.85;
    p *= 1.1 * max(gScale, 0.0001);

    float2 mo = float2(0.5, gParams0.z);

    float camAng = 2.75 - 2.0 * (mo.x + gTime * 0.05);

    float3 ro = 4.0 * normalize(float3(
        cos(camAng),
        sin(gTime * 0.22) * 0.2,
        sin(camAng)
    ));

    float3 eye = normalize(-ro);
    float3 rgt = normalize(cross(float3(0.0, 1.0, 0.0), eye));
    float3 up = cross(eye, rgt);

    float3 rd = normalize(p.x * rgt + p.y * up + (3.3 - sin(gTime * 0.3) * 0.7) * eye);

    float4 col = saturate(VMarch(ro, rd, uv * 1024.0));
    col.rgb = pow(max(col.rgb, 0.0), 0.9);

    float2 c = uv * 2.0 - 1.0;
    float core = exp(-dot(c, c) * 9.0) * gParams0.z;
    col.rgb += gColorA.rgb * core * 0.45;

    col.rgb *= gIntensity;

    float mask = RadialMask(uv);
    col.rgb *= mask;
    col.a *= mask * gAlpha;

    return float4(col.rgb, col.a);
}