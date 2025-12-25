#pragma once

#include <pch.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
using namespace DirectX;

struct DcConstants
{
};

static std::string dcCode = R"(
cbuffer Params : register(b0)
{
};

Texture2D<float4> FusedAlbedoInput : register(t0);
Texture2D<float3> ColorInput : register(t1);

RWTexture2D<float4> ColorOutput : register(u0);

[numthreads(32, 32, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixelId = DTid.xy;
    
    float3 fusedAlbedo = FusedAlbedoInput.Load(int3(pixelId, 0.0f)).xyz;
    fusedAlbedo = fusedAlbedo * fusedAlbedo;
    float3 color = ColorInput.Load(int3(pixelId, 0.0f)).xyz;
    ColorOutput[pixelId] = float4(color + fusedAlbedo, 0.0f);
}
)";

static ID3DBlob* DC_CompileShader(const char* shaderCode, const char* entryPoint, const char* target)
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
