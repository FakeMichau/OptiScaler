cbuffer Params : register(b0)
{
};

Texture2D<float4> FusedAlbedoInput : register(t0);
Texture2D<float3> ColorInput : register(t1);
Texture2D<float3> ColorBeforeParticlesInput : register(t2);

RWTexture2D<float4> ColorOutput : register(u0);

[numthreads(32, 32, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixelId = DTid.xy;
    
    float3 fusedAlbedo = FusedAlbedoInput.Load(int3(pixelId, 0.0f)).xyz;
    float3 color = ColorInput.Load(int3(pixelId, 0.0f)).xyz;
    color += ColorBeforeParticlesInput.Load(int3(pixelId, 0.0f)).xyz;
    ColorOutput[pixelId] = float4(color * fusedAlbedo, 0.0f);
}