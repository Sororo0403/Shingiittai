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

struct EmitRequest
{
    float3 start;
    float pad0;

    float3 end;
    float emitScale;

    uint count;
    uint isSweep;
    float pad1;
    float pad2;
};

struct SimCB
{
    float deltaTime;
    float time;
    uint emitRequestCount;
    uint maxParticles;
};

struct ControlData
{
    uint head;
    uint pad0;
    uint pad1;
    uint pad2;
};

cbuffer gSim : register(b0)
{
    SimCB gSimData;
}

StructuredBuffer<EmitRequest> gEmitRequests : register(t0);
RWStructuredBuffer<ParticleGpu> gParticles : register(u0);
RWStructuredBuffer<ControlData> gControl : register(u1);

float Hash11(float p)
{
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

float Hash21(float2 p)
{
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float3 Hash31(float p)
{
    float3 p3 = frac(float3(p, p + 1.23, p + 2.54) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xxy + p3.yzz) * p3.zyx);
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID,
          uint3 groupID : SV_GroupID,
          uint3 groupThreadID : SV_GroupThreadID)
{
    uint requestIndex = groupID.x;
    if (requestIndex >= gSimData.emitRequestCount)
    {
        return;
    }

    EmitRequest req = gEmitRequests[requestIndex];
    uint localIndex = groupThreadID.x;

    if (localIndex >= req.count)
    {
        return;
    }

    float3 startPos = req.start;
    float3 endPos = req.end;
    float3 dir = endPos - startPos;
    float len = length(dir);
    if (len < 1e-5)
    {
        dir = float3(0.0, 0.0, 1.0);
        len = 1.0;
    }
    else
    {
        dir /= len;
    }

    float3 upRef = abs(dir.y) > 0.85 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0);
    float3 right = normalize(cross(upRef, dir));
    float3 up = normalize(cross(dir, right));

    float seedBase = gSimData.time * 17.13 + requestIndex * 41.7 + localIndex * 3.1;

    float t = (req.count <= 1) ? 0.5 : (float) localIndex / (float) (req.count - 1);
    float alongJitter = Hash11(seedBase + 0.13) * 0.16 - 0.08;
    float alongT = saturate(t + alongJitter);

    float radialA = Hash11(seedBase + 1.7) * 6.2831853;
    float radialR = (Hash11(seedBase + 3.9) * 2.0 - 1.0);

    float widthScale = (req.isSweep != 0) ? 0.22 : 0.12;
    widthScale *= max(0.2, req.emitScale);

    float3 basePos = lerp(startPos, endPos, alongT);
    float3 radialOffset =
        right * cos(radialA) * radialR * widthScale +
        up * sin(radialA) * radialR * widthScale * 0.55;

    float3 spawnPos = basePos + radialOffset;

    float forwardSpeed =
        ((req.isSweep != 0) ? 7.5 : 9.5) +
        Hash11(seedBase + 5.2) * ((req.isSweep != 0) ? 6.0 : 8.0);

    float sideSpeed = (Hash11(seedBase + 8.7) * 2.0 - 1.0) *
                      ((req.isSweep != 0) ? 3.0 : 1.4);

    float upSpeed = Hash11(seedBase + 11.4) *
                    ((req.isSweep != 0) ? 1.8 : 1.2);

    float3 velocity = dir * forwardSpeed + right * sideSpeed + up * upSpeed;

    // sweep は少し横へ散らして派手に
    if (req.isSweep != 0)
    {
        velocity += right * ((Hash11(seedBase + 13.2) * 2.0 - 1.0) * 3.2);
    }

    float life = ((req.isSweep != 0) ? 0.18 : 0.12) +
                 Hash11(seedBase + 15.9) * ((req.isSweep != 0) ? 0.20 : 0.12);

    float2 size;
    if (req.isSweep != 0)
    {
        size.x = 0.03 + Hash11(seedBase + 18.2) * 0.03;
        size.y = 0.14 + Hash11(seedBase + 21.5) * 0.18;
    }
    else
    {
        size.x = 0.025 + Hash11(seedBase + 24.8) * 0.02;
        size.y = 0.10 + Hash11(seedBase + 27.7) * 0.12;
    }

    float4 color;
    if (req.isSweep != 0)
    {
        color = float4(1.0, 0.9 + Hash11(seedBase + 31.2) * 0.1,
                       1.0, 1.0);
    }
    else
    {
        color = float4(0.85 + Hash11(seedBase + 33.6) * 0.15,
                       0.95 + Hash11(seedBase + 35.1) * 0.05,
                       1.0, 1.0);
    }

    float rotation = Hash11(seedBase + 37.9) * 6.2831853;
    float type = (req.isSweep != 0) ? 1.0 : 0.0;

    float3 accel = float3(0.0, -5.5, 0.0);

    uint dstIndex;
    InterlockedAdd(gControl[0].head, 1, dstIndex);
    dstIndex = dstIndex % max(gSimData.maxParticles, 1);

    ParticleGpu p;
    p.position = spawnPos;
    p.life = life;
    p.velocity = velocity;
    p.maxLife = max(life, 1e-4);
    p.color = color;
    p.size = size;
    p.rotation = rotation;
    p.alive = 1.0;
    p.accel = accel;
    p.type = type;

    gParticles[dstIndex] = p;
}