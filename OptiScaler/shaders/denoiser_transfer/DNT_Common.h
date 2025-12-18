#pragma once

#include <pch.h>
#include <d3dcompiler.h>

struct DntConstants
{
    int depthNonLinear = false;
    int depthInverted = false;
    float cameraFar = 0.0f;
    float cameraNear = 0.0f;
};

static std::string dntCode = R"(
cbuffer Params : register(b0)
{
    int depthNonLinear;
    int depthInverted;
    float cameraFar;
    float cameraNear;
};

Texture2D<float> DepthInput : register(t0);
RWTexture2D<float> LinearDepthOutput : register(u0);

[numthreads(32, 32, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    float depth = DepthInput.Load(int3(DTid.xy, 0)).x;
    
    float linearDepth = 0.0f;
    
    if (depthInverted > 0)
        linearDepth = (cameraNear * cameraFar) / (cameraFar - depth * (cameraFar - cameraNear));
    else
        linearDepth = (cameraNear * cameraFar) / (cameraNear + depth * (cameraFar - cameraNear));
    
    LinearDepthOutput[DTid.xy] = (linearDepth - cameraNear) / (cameraFar - cameraNear);
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
