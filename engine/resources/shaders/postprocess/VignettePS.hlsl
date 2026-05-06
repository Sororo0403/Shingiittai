cbuffer VignetteParams : register(b0) {
    float4 gColorIntensity;
    float4 gSettings;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    float intensity = saturate(gColorIntensity.a);
    if (intensity <= 0.001f) {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float2 centered = input.uv * 2.0f - 1.0f;
    centered.x *= max(gSettings.z, 0.001f);

    float distanceFromCenter = length(centered);
    float innerRadius = saturate(gSettings.x);
    float outerRatio = saturate((distanceFromCenter - innerRadius) /
                                max(1.0f - innerRadius, 0.001f));
    float falloff = pow(saturate(outerRatio), max(gSettings.y, 0.001f));
    float alpha = saturate(falloff * intensity);

    return float4(gColorIntensity.rgb, alpha);
}
