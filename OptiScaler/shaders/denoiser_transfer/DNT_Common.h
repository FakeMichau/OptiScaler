#pragma once

#include <pch.h>
#include <d3dcompiler.h>

struct DntConstants
{
    int depthNonLinear = false;
    int depthInverted = false;
    float cameraFar = 0.0f;
    float cameraNear = 0.0f;

    int roughnessInNormals;
};

static std::string dntCode = R"(
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
RWTexture2D<float> LinearDepthOutput : register(u0);
RWTexture2D<float4> PackedNormalsOutput : register(u1);

float2 NormalToOctahedronUv(float3 N)
{
    N.xy /= abs(N.x) + abs(N.y) + abs(N.z);
    float2 k = sign(N.xy);
    float s = saturate(-N.z);
    N.xy = lerp(N.xy, (1.0 - abs(N.yx)) * k, s);
    return N.xy * 0.5 + 0.5;
}

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
