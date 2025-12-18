#pragma once

#include <pch.h>
#include <d3dcompiler.h>

struct DntConstants
{
    float Sharpness;

    // Motion Vector Stuff
    bool DynamicSharpenEnabled;
    bool DisplaySizeMV;
    bool Debug;

    float MotionSharpness;
    float MotionTextureScale;
    float MvScaleX;
    float MvScaleY;
    float Threshold;
    float ScaleLimit;

    int RenderWidth;
    int RenderHeight;
    int DisplayWidth;
    int DisplayHeight;
};

static std::string dntCode = R"(

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
