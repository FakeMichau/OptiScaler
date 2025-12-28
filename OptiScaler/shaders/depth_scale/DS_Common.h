#pragma once
#include <pch.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

using namespace DirectX;

struct alignas(256) DSConstants
{
    int depthInverted;
    float cameraFar;
    float cameraNear;
};

inline static std::string shaderCode = R"(
cbuffer Params : register(b0)
{
    int depthInverted;
    float cameraFar;
    float cameraNear;
};

// Input texture
Texture2D<float> SourceTexture : register(t0);

// Output texture
RWTexture2D<float> DestinationTexture : register(u0);

// Compute shader thread group size
[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixelId = DTid.xy;

    float linearDepth = SourceTexture.Load(int3(pixelId, 0));
    
    if (depthInverted)
    {
        DestinationTexture[pixelId] = saturate(
            (cameraFar / linearDepth - 1.0f) / 
            (cameraFar / cameraNear - 1.0f)
        );
    }
    else
    {
        DestinationTexture[pixelId] = saturate(
            (linearDepth * cameraFar - cameraFar * cameraNear) /
            (linearDepth * (cameraFar - cameraNear))
        );
    }
}
)";

inline static ID3DBlob* DS_CompileShader(const char* shaderCode, const char* entryPoint, const char* target)
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
