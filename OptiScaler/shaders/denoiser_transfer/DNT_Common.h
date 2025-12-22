#pragma once

#include <pch.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
using namespace DirectX;

struct DntConstants
{
    int roughnessInNormals;
    int depthNonLinear;
    int depthInverted;
    float cameraFar;
    float cameraNear;

    XMMATRIX InvProjection;
    XMMATRIX InvViewProjection;
    XMMATRIX PrevView;
};

static std::string dntCode = R"(
cbuffer Params : register(b0)
{
    int roughnessInNormals;
    int depthNonLinear;
    int depthInverted;
    float cameraFar;
    float cameraNear;
    
    matrix InvProjection; // ClipToCamera
    matrix InvViewProjection; // ClipToWorld 
    matrix PrevView; // Prev WorldToCamera
};

Texture2D<float> DepthInput : register(t0);
Texture2D<float4> NormalsInput : register(t1);
Texture2D<float> RoughnessInput : register(t2);
Texture2D<float3> SpecularAlbedoInput : register(t3);
Texture2D<float3> DiffuseAlbedoInput : register(t4);
Texture2D<float4> MotionVectorsInput : register(t5);
Texture2D<float> SpecularRayLengthInput : register(t6);
Texture2D<float3> ColorInput : register(t7);

RWTexture2D<float> LinearDepthOutput : register(u0);
RWTexture2D<float4> PackedNormalsOutput : register(u1);
RWTexture2D<float4> SpecularAlbedoOutput : register(u2);
RWTexture2D<float4> DiffuseAlbedoOutput : register(u3);
RWTexture2D<float4> FusedAlbedoOutput : register(u4);
RWTexture2D<float4> MotionVectorsOutput : register(u5);
RWTexture2D<float4> ColorOutput : register(u6);

// Directly from AMD's docs
float2 NormalToOctahedronUv(float3 N)
{
    N.xy /= abs(N.x) + abs(N.y) + abs(N.z);
    float2 k = sign(N.xy);
    float s = saturate(-N.z);
    N.xy = lerp(N.xy, (1.0 - abs(N.yx)) * k, s);
    return N.xy * 0.5 + 0.5;
}

float3 InvProjectPosition(float3 coord, float4x4 mat)
{
    coord.y = (1 - coord.y);
    coord.xy = 2 * coord.xy - 1;
    float4 projected = mul(mat, float4(coord, 1));
    projected.xyz /= projected.w;
    return projected.xyz;
}

float3 ScreenSpaceToViewSpace(float3 screen_uv_coord, float4x4 invProj)
{
    return InvProjectPosition(screen_uv_coord, invProj);
}

float3 ScreenSpaceToWorldSpace(float3 screen_space_position, float4x4 invViewProj)
{
    return InvProjectPosition(screen_space_position, invViewProj);
}

float GetNoV(float3 view, float3 normals)
{
    // TODO: assumes Normals are already in view space
    float3 N = normalize(normals * 2.0f - 1.0f);
        
    float3 V = normalize(-view);
    
    float NoV = dot(-view, normals);

    // TODO: might be wrong, we pass FFX_DENOISER_DISPATCH_NON_GAMMA_ALBEDO so this might need to be done
    return saturate(dot(N, V));
}

[numthreads(32, 32, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixelId = DTid.xy;
    float2 pixelCenter = float2(pixelId) + 0.5;
    
    float2 screenSize;
    DepthInput.GetDimensions(screenSize.x, screenSize.y);
    
    if (any(pixelId >= (uint2) screenSize))
    {
        return;
    }
    
    // TODO: do something if this depth is linear
    float nonLinearDepth = DepthInput.Load(int3(pixelId, 0)).x;
    
    float3 screenUVW = float3(pixelCenter / screenSize, nonLinearDepth);
    
    // TODO: take care of infinite far plane
    float3 viewSpacePos = ScreenSpaceToViewSpace(screenUVW, InvProjection);
    viewSpacePos.z = -viewSpacePos.z; // Correct the funny
    
    LinearDepthOutput[pixelId] = clamp(-viewSpacePos.z, cameraNear, cameraFar - 0.1f);
    
    float4 normals = NormalsInput.Load(int3(pixelId, 0));
    
    float roughness;
    if (roughnessInNormals > 0)
        roughness = normals.a;
    else
        roughness = RoughnessInput.Load(int3(pixelId, 0));
    
    // Material as 0
    PackedNormalsOutput[pixelId] = float4(NormalToOctahedronUv(normals.rgb), roughness, 0);
    
    // Linear albedo
    float3 specularAlbedo = SpecularAlbedoInput.Load(int3(pixelId, 0));
    float3 diffuseAlbedo = DiffuseAlbedoInput.Load(int3(pixelId, 0));
    
    // TODO: if depth is linear then this doesn't work
    float NoV = GetNoV(viewSpacePos, normals.xyz);
    
    float4 specularAlbedo_NoV = float4(specularAlbedo, NoV);
    float4 diffuseAlbedo_Metallic = float4(diffuseAlbedo, 0); // metalness as 0
    float3 fusedModulator = max(1e-3, max(specularAlbedo_NoV.xyz, diffuseAlbedo_Metallic.xyz));
    
    SpecularAlbedoOutput[pixelId] = specularAlbedo_NoV;
    DiffuseAlbedoOutput[pixelId] = diffuseAlbedo_Metallic;
    FusedAlbedoOutput[pixelId] = float4(fusedModulator, NoV);
    
    // Color
    float3 color = ColorInput.Load(int3(pixelId, 0));
    float specularRayLength = SpecularRayLengthInput.Load(int3(pixelId, 0));
    
    ColorOutput[pixelId] = float4(color, specularRayLength);
    
    // MVs
    float4 motionVector = MotionVectorsInput.Load(int3(pixelId, 0));
    
    float3 worldSpacePos = ScreenSpaceToWorldSpace(screenUVW, InvViewProjection);
    float3 prevViewSpacePos = mul(PrevView, float4(worldSpacePos, 1.0f)).xyz;
    prevViewSpacePos.z = -prevViewSpacePos.z; // Fix the funny
    float depthDiff = (prevViewSpacePos.z - viewSpacePos.z);
    
    MotionVectorsOutput[pixelId] = float4(motionVector.xy, depthDiff, 0.0f);
}
)";

static ID3DBlob* DNT_CompileShader(const char* shaderCode, const char* entryPoint, const char* target)
{
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr, entryPoint, target,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shaderBlob, &errorBlob);

    if (FAILED(hr))
    {
        LOG_ERROR("error while compiling shader");

        if (errorBlob)
        {
            LOG_ERROR("error while compiling shader : {0}", (char*) errorBlob->GetBufferPointer());
            errorBlob->Release();
        }

        if (shaderBlob)
            shaderBlob->Release();

        return nullptr;
    }

    if (errorBlob)
        errorBlob->Release();

    return shaderBlob;
}
