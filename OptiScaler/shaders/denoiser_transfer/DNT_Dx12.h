#pragma once

#include <pch.h>

#include "DNT_Common.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>
#include <magic_enum.hpp>

#define DNT_NUM_OF_HEAPS 2

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
        int roughnessInNormals;
        int depthNonLinear;
        int depthInverted;
        float cameraFar;
        float cameraNear;

        XMMATRIX InvProjection;
        XMMATRIX InvViewProjection;
        XMMATRIX PrevView;
    };

    FrameDescriptorHeap _frameHeaps[DNT_NUM_OF_HEAPS];

    DNT_Dx12::ResourceWithState linearDepth {};
    DNT_Dx12::ResourceWithState normals {};
    DNT_Dx12::ResourceWithState roughness {};
    DNT_Dx12::ResourceWithState specularAlbedo {};
    DNT_Dx12::ResourceWithState diffuseAlbedo {};
    DNT_Dx12::ResourceWithState fusedAlbedo {};
    DNT_Dx12::ResourceWithState motionVectors {};
    DNT_Dx12::ResourceWithState color {};
    DNT_Dx12::ResourceWithState specularRayLength {};

    uint32_t InNumThreadsX = 32;
    uint32_t InNumThreadsY = 32;

  public:
    bool Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InDepth,
                  ID3D12Resource* InNormals, ID3D12Resource* InRoughness, ID3D12Resource* InSpecularAlbedo,
                  ID3D12Resource* InDiffuseAlbedo, ID3D12Resource* InMotionVectors, ID3D12Resource* InSpecularRayLength,
                  ID3D12Resource* InColor, DntConstants InConstants);

    // Depth
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

    // Normals
    bool CreateNormalsResource(ID3D12Device* InDevice, ID3D12Resource* InNormals, D3D12_RESOURCE_STATES InState)
    {
        auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                             D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

        // TODO: consider DXGI_FORMAT_R10G10B10A2_UNORM
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

    // Specular Albedo
    bool CreateSpecularAlbedoResource(ID3D12Device* InDevice, ID3D12Resource* InSpecularAlbedo,
                                      D3D12_RESOURCE_STATES InState)
    {
        auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                             D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

        auto result =
            Shader_Dx12::CreateBufferResource(InDevice, InSpecularAlbedo, InState, &specularAlbedo.rawResource,
                                              resourceFlags, 0, 0, DXGI_FORMAT_R8G8B8A8_UNORM);

        if (result && specularAlbedo.state == D3D12_INVALID_STATE)
        {
            specularAlbedo.rawResource->SetName(L"DNT_specularAlbedo");
            specularAlbedo.state = InState;
        }

        return result;
    }

    void SetSpecularAlbedoState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
    {
        return specularAlbedo.SetBufferState(InCommandList, InState);
    }

    // Diffuse Albedo
    bool CreateDiffuseAlbedoResource(ID3D12Device* InDevice, ID3D12Resource* InDiffuseAlbedo,
                                     D3D12_RESOURCE_STATES InState)
    {
        auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                             D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

        auto result = Shader_Dx12::CreateBufferResource(InDevice, InDiffuseAlbedo, InState, &diffuseAlbedo.rawResource,
                                                        resourceFlags, 0, 0, DXGI_FORMAT_R8G8B8A8_UNORM);

        if (result && diffuseAlbedo.state == D3D12_INVALID_STATE)
        {
            diffuseAlbedo.rawResource->SetName(L"DNT_diffuseAlbedo");
            diffuseAlbedo.state = InState;
        }

        // Fused Albedo
        if (result)
        {
            result = Shader_Dx12::CreateBufferResource(InDevice, InDiffuseAlbedo, InState, &fusedAlbedo.rawResource,
                                                       resourceFlags, 0, 0, DXGI_FORMAT_R8G8B8A8_UNORM);

            if (result && fusedAlbedo.state == D3D12_INVALID_STATE)
            {
                fusedAlbedo.rawResource->SetName(L"DNT_fusedAlbedo");
                fusedAlbedo.state = InState;
            }
        }

        return result;
    }

    void SetDiffuseAlbedoState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
    {
        return diffuseAlbedo.SetBufferState(InCommandList, InState);
    }

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
        // auto result = Shader_Dx12::CreateBufferResource(InDevice, InColor, InState, &color.rawResource,
        //                                                 resourceFlags, 0, 0, DXGI_FORMAT_R16G16B16A16_FLOAT);

        if (result && color.state == D3D12_INVALID_STATE)
        {
            color.rawResource->SetName(L"DNT_color");
            color.state = InState;
        }

        return result;
    }

    void SetColorState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
    {
        return color.SetBufferState(InCommandList, InState);
    }

    // Depth
    bool CreateMotionVectorsResource(ID3D12Device* InDevice, ID3D12Resource* InMotionVectors,
                                     D3D12_RESOURCE_STATES InState)
    {
        auto result = motionVectors.CreateBufferResource(InDevice, InMotionVectors, InState);

        if (result)
            motionVectors.rawResource->SetName(L"DNT_motionVectors");

        return result;
    }

    void SetMotionVectorsState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
    {
        return motionVectors.SetBufferState(InCommandList, InState);
    }

    ID3D12Resource* LinearDepth() { return linearDepth.rawResource; }
    ID3D12Resource* Normals() { return normals.rawResource; }
    ID3D12Resource* SpecularAlbedo() { return specularAlbedo.rawResource; }
    ID3D12Resource* DiffuseAlbedo() { return diffuseAlbedo.rawResource; }
    ID3D12Resource* FusedAlbedo() { return fusedAlbedo.rawResource; }
    ID3D12Resource* MotionVectors() { return motionVectors.rawResource; }
    ID3D12Resource* Color() { return color.rawResource; }

    // TODO: fix
    bool CanRender() const { return _init && linearDepth.rawResource != nullptr; }

    DNT_Dx12(std::string InName, ID3D12Device* InDevice);

    ~DNT_Dx12();
};
