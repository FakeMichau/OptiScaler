#pragma once

#include <pch.h>

#include "DC_Common.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>
#include <magic_enum.hpp>

#define DC_NUM_OF_HEAPS 2

class DC_Dx12 : public Shader_Dx12
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
    };

    FrameDescriptorHeap _frameHeaps[DC_NUM_OF_HEAPS];

    DC_Dx12::ResourceWithState fusedAlbedo {};
    DC_Dx12::ResourceWithState color {};

    uint32_t InNumThreadsX = 32;
    uint32_t InNumThreadsY = 32;

  public:
    bool Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList,
                  ID3D12Resource* InFusedAlbedo, ID3D12Resource* InColor, DcConstants InConstants);

    // Color
    bool CreateColorResource(ID3D12Device* InDevice, ID3D12Resource* InColor, D3D12_RESOURCE_STATES InState)
    {
        auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                             D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

        // TODO: create a resource with an alpha channel if the original one doesn't have it
        // might require some function to convert between non-alpha into alpha without losing precision.
        // Might also need to worry about radiance.output's format

        if (InColor)
        {
            auto desc = InColor->GetDesc();
            auto format = desc.Format;
            LOG_DEBUG("Color format: {}", magic_enum::enum_name(format));
        }

        auto result = Shader_Dx12::CreateBufferResource(InDevice, InColor, InState, &color.rawResource, resourceFlags);
        //auto result = Shader_Dx12::CreateBufferResource(InDevice, InColor, InState, &color.rawResource,
        //                                                resourceFlags, 0, 0, DXGI_FORMAT_R16G16B16A16_FLOAT);

        if (result && color.state == D3D12_INVALID_STATE)
        {
            color.rawResource->SetName(L"DC_color");
            color.state = InState;
        }

        return result;
    }

    void SetColorState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
    {
        return color.SetBufferState(InCommandList, InState);
    }

    ID3D12Resource* Color() { return color.rawResource; }

    // TODO: fix
    bool CanRender() const { return _init && color.rawResource != nullptr; }

    DC_Dx12(std::string InName, ID3D12Device* InDevice);

    ~DC_Dx12();
};
