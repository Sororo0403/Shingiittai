#include "Model.hlsli"

Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);

float4 main(ModelVSOutput input) : SV_TARGET
{
    return tex0.Sample(samp0, input.uv);
}
