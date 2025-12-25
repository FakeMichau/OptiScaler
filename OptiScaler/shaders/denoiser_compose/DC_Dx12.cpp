#include "DC_Dx12.h"

#include "precompile/DC_Shader.h"

#include <Config.h>

bool DC_Dx12::ResourceWithState::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource,
                                             D3D12_RESOURCE_STATES InState)
{
    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                         D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    auto result = Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &rawResource, resourceFlags);

    // If CreateBufferResource create a new resource then we need to track the initial state
    if (result && state == D3D12_INVALID_STATE)
        state = InState;

    return result;
}

void DC_Dx12::ResourceWithState::SetBufferState(ID3D12GraphicsCommandList* InCommandList,
                                                 D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, rawResource, &state);
}

bool DC_Dx12::Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList,
                        ID3D12Resource* InFusedAlbedo, ID3D12Resource* InColor,
                        DcConstants InConstants)
{
    if (!_init || InDevice == nullptr || InCmdList == nullptr || InColor == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _counter++;
    _counter = _counter % DC_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    // Fused Albedo
    auto inFusedAlbedoDesc = InFusedAlbedo->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC fusedAlbedoDesc = {};
    fusedAlbedoDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    fusedAlbedoDesc.Format = Shader_Dx12::TranslateTypelessFormats(inFusedAlbedoDesc.Format);
    fusedAlbedoDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    fusedAlbedoDesc.Texture2D.MipLevels = 1;

    InDevice->CreateShaderResourceView(InFusedAlbedo, &fusedAlbedoDesc, currentHeap.GetSrvCPU(0));

    // Color
    auto inColorDesc = InColor->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC colorDesc = {};
    colorDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    colorDesc.Format = Shader_Dx12::TranslateTypelessFormats(inColorDesc.Format);
    colorDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    colorDesc.Texture2D.MipLevels = 1;

    InDevice->CreateShaderResourceView(InColor, &colorDesc, currentHeap.GetSrvCPU(1));

    /// Outputs

    // Color
    auto outColorDesc = color.rawResource->GetDesc();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavColorDesc = {};
    uavColorDesc.Format = Shader_Dx12::TranslateTypelessFormats(outColorDesc.Format);
    uavColorDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavColorDesc.Texture2D.MipSlice = 0;

    InDevice->CreateUnorderedAccessView(color.rawResource, nullptr, &uavColorDesc,
                                        currentHeap.GetUavCPU(0));

    InternalConstants constants {};
    // TODO: No constants, remove CBV ???

    // Copy the updated constant buffer data to the constant buffer resource
    BYTE* pCBDataBegin;
    CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU
    auto result = _constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCBDataBegin));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] _constantBuffer->Map error {1:x}", _name, (unsigned int) result);
        return false;
    }

    if (pCBDataBegin == nullptr)
    {
        _constantBuffer->Unmap(0, nullptr);
        LOG_ERROR("[{0}] pCBDataBegin is null!", _name);
        return false;
    }

    memcpy(pCBDataBegin, &constants, sizeof(constants));
    _constantBuffer->Unmap(0, nullptr);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = _constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = sizeof(constants);
    InDevice->CreateConstantBufferView(&cbvDesc, currentHeap.GetCbvCPU(0));

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);

    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    UINT dispatchWidth = 0;
    UINT dispatchHeight = 0;

    dispatchWidth = static_cast<UINT>((inColorDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    dispatchHeight = (inColorDesc.Height + InNumThreadsY - 1) / InNumThreadsY;

    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

DC_Dx12::DC_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
        // 2 SRVs starting at register t0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0),

        // 1 UAV starting at register u0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0),

        // 1 CBV starting at register b0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0)
    };

    CD3DX12_ROOT_PARAMETER1 rootParameter {};
    rootParameter.InitAsDescriptorTable(std::size(descriptorRanges), descriptorRanges);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(1, &rootParameter);

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(InternalConstants));
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    auto result =
        InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&_constantBuffer));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource error {1:x}", _name, (unsigned int) result);
        return;
    }

    ID3DBlob* errorBlob;
    ID3DBlob* signatureBlob;

    do
    {
        auto hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signatureBlob, &errorBlob);

        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] D3D12SerializeVersionedRootSignature error {1:x}", _name, (unsigned int) hr);
            break;
        }

        hr = InDevice->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
                                           IID_PPV_ARGS(&_rootSignature));

        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateRootSignature error {1:x}", _name, (unsigned int) hr);
            break;
        }

    } while (false);

    if (errorBlob != nullptr)
    {
        errorBlob->Release();
        errorBlob = nullptr;
    }

    if (signatureBlob != nullptr)
    {
        signatureBlob->Release();
        signatureBlob = nullptr;
    }

    if (_rootSignature == nullptr)
    {
        LOG_ERROR("[{0}] _rootSignature is null!", _name);
        return;
    }

    // TODO: Add precompiled shaders
    //if (Config::Instance()->UsePrecompiledShaders.value_or_default())
    //{
    //    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    //    computePsoDesc.pRootSignature = _rootSignature;
    //    computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    //    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(rcas_cso), sizeof(rcas_cso));
    //    auto hr = InDevice->CreateComputePipelineState(&computePsoDesc, __uuidof(ID3D12PipelineState*),
    //                                                   (void**) &_pipelineState);

    //    if (FAILED(hr))
    //    {
    //        LOG_ERROR("[{0}] CreateComputePipelineState error: {1:X}", _name, hr);
    //        return;
    //    }
    //}
    //else
    {
        // Compile shader blobs
        ID3DBlob* _recEncodeShader = DC_CompileShader(dcCode.c_str(), "CSMain", "cs_5_0");

        if (_recEncodeShader == nullptr)
        {
            LOG_ERROR("[{0}] DC_CompileShader error!", _name);
            return;
        }

        // create pso objects
        if (!Shader_Dx12::CreateComputeShader(InDevice, _rootSignature, &_pipelineState, _recEncodeShader))
        {
            LOG_ERROR("[{0}] CreateComputeShader error!", _name);
            return;
        }

        if (_recEncodeShader != nullptr)
        {
            _recEncodeShader->Release();
            _recEncodeShader = nullptr;
        }
    }

    State::Instance().skipHeapCapture = true;

    for (int i = 0; i < DC_NUM_OF_HEAPS; i++)
    {
        if (!_frameHeaps[i].Initialize(InDevice, 2, 1, 1))
        {
            LOG_ERROR("[{0}] Failed to init heap", _name);
            _init = false;
            State::Instance().skipHeapCapture = false;
            return;
        }
    }

    State::Instance().skipHeapCapture = false;

    _init = true;
}

DC_Dx12::~DC_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    if (_rootSignature != nullptr)
    {
        _rootSignature->Release();
        _rootSignature = nullptr;
    }

    if (_pipelineState != nullptr)
    {
        _pipelineState->Release();
        _pipelineState = nullptr;
    }

    for (int i = 0; i < DC_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }

    if (color.rawResource != nullptr)
    {
        color.rawResource->Release();
        color.rawResource = nullptr;
    }

    if (_constantBuffer != nullptr)
    {
        _constantBuffer->Release();
        _constantBuffer = nullptr;
    }
}
