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

RWStructuredBuffer<ParticleGpu> gParticles : register(u0);
RWStructuredBuffer<ControlData> gControl : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint index = dispatchThreadID.x;
    if (index >= gSimData.maxParticles)
    {
        return;
    }

    ParticleGpu p = gParticles[index];

    if (p.alive < 0.5)
    {
        return;
    }

    float dt = max(gSimData.deltaTime, 0.0);

    p.life -= dt;
    if (p.life <= 0.0)
    {
        p.life = 0.0;
        p.alive = 0.0;
        gParticles[index] = p;
        return;
    }

    float life01 = saturate(p.life / max(p.maxLife, 1e-5));

    // 少しだけ減衰
    float drag = lerp(0.985, 0.94, 1.0 - life01);
    p.velocity *= pow(drag, dt * 60.0);

    // 加速度
    p.velocity += p.accel * dt;

    // 位置更新
    p.position += p.velocity * dt;

    // 回転更新
    p.rotation += dt * (2.2 + p.type * 1.5);

    // 終盤はサイズを細くしつつ少し長さを残す
    p.size.x *= lerp(1.0, 0.92, dt * 60.0);
    p.size.y *= lerp(1.0, 0.985, dt * 60.0);

    // 色の減衰は Draw 側でも行うが、ここで少しだけ暗くする
    p.color.rgb *= lerp(1.0, 0.985, dt * 60.0);

    gParticles[index] = p;
}