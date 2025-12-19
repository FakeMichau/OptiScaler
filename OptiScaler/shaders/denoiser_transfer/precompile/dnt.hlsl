cbuffer Params : register(b0)
{
    int depthNonLinear;
    int depthInverted;
    float cameraFar;
    float cameraNear;
    
    int roughnessInNormals;
};

Texture2D<float> DepthInput : register(t0);
Texture2D<float4> NormalsInput : register(t1);
Texture2D<float> RoughnessInput : register(t2);
Texture2D<float3> SpecularAlbedoInput : register(t3);
Texture2D<float3> DiffuseAlbedoInput : register(t4);

RWTexture2D<float> LinearDepthOutput : register(u0);
RWTexture2D<float4> PackedNormalsOutput : register(u1);
RWTexture2D<float4> SpecularAlbedoOutput : register(u2);
RWTexture2D<float4> DiffuseAlbedoOutput : register(u3);
RWTexture2D<float4> FusedAlbedoOutput : register(u4);

// Directly from AMD's docs
float2 NormalToOctahedronUv(float3 N)
{
    N.xy /= abs(N.x) + abs(N.y) + abs(N.z);
    float2 k = sign(N.xy);
    float s = saturate(-N.z);
    N.xy = lerp(N.xy, (1.0 - abs(N.yx)) * k, s);
    return N.xy * 0.5 + 0.5;
}

// TODO: add support for infinite far plane
// TODO: pass (cameraNear * cameraFar) as constants to avoid recomputing
float DepthToLinear(float depth)
{
    if (depthNonLinear == 0)
        return depth;
    
    float range = cameraFar - cameraNear;

    float linearDepth;
    if (depthInverted)
        linearDepth = (cameraNear * cameraFar) / (cameraNear + depth * range);
    else
        linearDepth = (cameraNear * cameraFar) / (cameraFar - depth * range);
    
    return saturate((linearDepth - cameraNear) / range);
}

[numthreads(32, 32, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    float depth = DepthInput.Load(int3(DTid.xy, 0)).x;
    LinearDepthOutput[DTid.xy] = DepthToLinear(depth);
    
    float4 normals = NormalsInput.Load(int3(DTid.xy, 0));
    
    float roughness;
    if (roughnessInNormals > 0)
        roughness = normals.a;
    else
        roughness = RoughnessInput.Load(int3(DTid.xy, 0));
    
    // Material as 0
    PackedNormalsOutput[DTid.xy] = float4(NormalToOctahedronUv(normals.rgb), roughness, 0);
    
    float3 specularAlbedo = SpecularAlbedoInput.Load(int3(DTid.xy, 0));
    float3 diffuseAlbedo = DiffuseAlbedoInput.Load(int3(DTid.xy, 0));
    
    // TODO: NoV as 0
    SpecularAlbedoOutput[DTid.xy] = float4(specularAlbedo, 0); // metalness as 0
    DiffuseAlbedoOutput[DTid.xy] = float4(diffuseAlbedo, 0);
    FusedAlbedoOutput[DTid.xy] = float4(max(specularAlbedo, diffuseAlbedo), 0);
}