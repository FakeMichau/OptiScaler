#pragma once

#include <pch.h>

#include "DNT_Common.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>

#define DNT_NUM_OF_HEAPS 2
#define D3D12_INVALID_STATE (D3D12_RESOURCE_STATES) 0xFFFFFFFF

class DNT_Dx12 : public Shader_Dx12
{
  private:
    struct ResourceWithState
    {
        ID3D12Resource* rawResource = nullptr;
        D3D12_RESOURCE_STATES state = D3D12_INVALID_STATE;

        bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState);
        void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);
    };

    struct alignas(256) InternalConstants
    {
        int depthNonLinear;
        int depthInverted;
        float cameraFar;
        float cameraNear;

        int roughnessInNormals;
    };

    FrameDescriptorHeap _frameHeaps[DNT_NUM_OF_HEAPS];

    DNT_Dx12::ResourceWithState linearDepth {};
    DNT_Dx12::ResourceWithState normals {};
    DNT_Dx12::ResourceWithState roughness {};

    uint32_t InNumThreadsX = 32;
    uint32_t InNumThreadsY = 32;

  public:
    bool Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InDepth,
                  ID3D12Resource* InNormals, ID3D12Resource* InRoughness, DntConstants InConstants);

    bool CreateDepthResource(ID3D12Device* InDevice, ID3D12Resource* InDepth, D3D12_RESOURCE_STATES InState) 
    { 
        auto result = linearDepth.CreateBufferResource(InDevice, InDepth, InState);

        if (result)
            linearDepth.rawResource->SetName(L"DNT_linearDepth");

        return result;
    }

    void SetDepthState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState) 
    {
        return linearDepth.SetBufferState(InCommandList, InState);
    }


    bool CreateNormalsResource(ID3D12Device* InDevice, ID3D12Resource* InNormals, D3D12_RESOURCE_STATES InState)
    {
        auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                             D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

        auto result = Shader_Dx12::CreateBufferResource(InDevice, InNormals, InState, &normals.rawResource,
                                                        resourceFlags, 0, 0, DXGI_FORMAT_R16G16B16A16_FLOAT);

        if (result && normals.state == D3D12_INVALID_STATE)
        {
            normals.rawResource->SetName(L"DNT_normals");
            normals.state = InState;
        }

        return result;
    }


    void SetNormalsState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
    {
        return normals.SetBufferState(InCommandList, InState);
    }

    ID3D12Resource* LinearDepth() { return linearDepth.rawResource; }
    ID3D12Resource* Normals() { return normals.rawResource; }

    bool CanRender() const { return _init && linearDepth.rawResource != nullptr; }

    DNT_Dx12(std::string InName, ID3D12Device* InDevice);

    ~DNT_Dx12();
};
