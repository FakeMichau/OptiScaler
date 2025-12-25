#include "DNT_Dx12.h"

#include "precompile/DNT_Shader.h"

#include <Config.h>

bool DNT_Dx12::ResourceWithState::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource,
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

void DNT_Dx12::ResourceWithState::SetBufferState(ID3D12GraphicsCommandList* InCommandList,
                                                 D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, rawResource, &state);
}

bool DNT_Dx12::Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InDepth,
                        ID3D12Resource* InNormals, ID3D12Resource* InRoughness, ID3D12Resource* InSpecularAlbedo,
                        ID3D12Resource* InDiffuseAlbedo, ID3D12Resource* InMotionVectors,
                        ID3D12Resource* InSpecularRayLength, ID3D12Resource* InColor, DntConstants InConstants)
{
    // TODO: add all the checks
    if (!_init || InDevice == nullptr || InCmdList == nullptr || InDepth == nullptr ||
        linearDepth.rawResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _counter++;
    _counter = _counter % DNT_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    // Depth
    auto inDepthDesc = InDepth->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC depthDesc = {};
    depthDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthDesc.Format = Shader_Dx12::TranslateTypelessFormats(inDepthDesc.Format);
    depthDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthDesc.Texture2D.MipLevels = 1;

    InDevice->CreateShaderResourceView(InDepth, &depthDesc, currentHeap.GetSrvCPU(0));

    // Normals
    auto inNormalsDesc = InNormals->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC normalsDesc = {};
    normalsDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    normalsDesc.Format = Shader_Dx12::TranslateTypelessFormats(inNormalsDesc.Format);
    normalsDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    normalsDesc.Texture2D.MipLevels = 1;

    InDevice->CreateShaderResourceView(InNormals, &normalsDesc, currentHeap.GetSrvCPU(1));

    // Roughness (optional if packed into Normals)
    if (!InConstants.roughnessInNormals)
    {
        auto inRoughnessDesc = InRoughness->GetDesc();

        D3D12_SHADER_RESOURCE_VIEW_DESC roughnessDesc = {};
        roughnessDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        roughnessDesc.Format = Shader_Dx12::TranslateTypelessFormats(inRoughnessDesc.Format);
        roughnessDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        roughnessDesc.Texture2D.MipLevels = 1;

        InDevice->CreateShaderResourceView(InRoughness, &roughnessDesc, currentHeap.GetSrvCPU(2));
    }
    else
    {
        // Put normals into shader as roughness as dummy data
        InDevice->CreateShaderResourceView(InNormals, &normalsDesc, currentHeap.GetSrvCPU(2));
    }

    // Specular Albedo
    auto inSpecularAlbedoDesc = InSpecularAlbedo->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC specularAlbedoDesc = {};
    specularAlbedoDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    specularAlbedoDesc.Format = Shader_Dx12::TranslateTypelessFormats(inSpecularAlbedoDesc.Format);
    specularAlbedoDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    specularAlbedoDesc.Texture2D.MipLevels = 1;

    InDevice->CreateShaderResourceView(InSpecularAlbedo, &specularAlbedoDesc, currentHeap.GetSrvCPU(3));

    // Diffuse Albedo
    auto inDiffuseAlbedoDesc = InDiffuseAlbedo->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC diffuseAlbedoDesc = {};
    diffuseAlbedoDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    diffuseAlbedoDesc.Format = Shader_Dx12::TranslateTypelessFormats(inDiffuseAlbedoDesc.Format);
    diffuseAlbedoDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    diffuseAlbedoDesc.Texture2D.MipLevels = 1;

    InDevice->CreateShaderResourceView(InDiffuseAlbedo, &diffuseAlbedoDesc, currentHeap.GetSrvCPU(4));

    // Motion Vectors
    auto inMotionVectorsDesc = InMotionVectors->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC motionVectorsDesc = {};
    motionVectorsDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    motionVectorsDesc.Format = Shader_Dx12::TranslateTypelessFormats(inMotionVectorsDesc.Format);
    motionVectorsDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    motionVectorsDesc.Texture2D.MipLevels = 1;

    InDevice->CreateShaderResourceView(InMotionVectors, &motionVectorsDesc, currentHeap.GetSrvCPU(5));

    // Specular Ray Length
    auto inSpecularRayLengthDesc = InSpecularRayLength->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC specularRayLengthDesc = {};
    specularRayLengthDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    specularRayLengthDesc.Format = Shader_Dx12::TranslateTypelessFormats(inSpecularRayLengthDesc.Format);
    specularRayLengthDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    specularRayLengthDesc.Texture2D.MipLevels = 1;

    InDevice->CreateShaderResourceView(InSpecularRayLength, &specularRayLengthDesc, currentHeap.GetSrvCPU(6));

    // Color
    auto inColorDesc = InColor->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC colorDesc = {};
    colorDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    colorDesc.Format = Shader_Dx12::TranslateTypelessFormats(inColorDesc.Format);
    colorDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    colorDesc.Texture2D.MipLevels = 1;

    InDevice->CreateShaderResourceView(InColor, &colorDesc, currentHeap.GetSrvCPU(7));

    /// Outputs

    // Linear Depth
    auto outDepthDesc = linearDepth.rawResource->GetDesc();
    D3D12_UNORDERED_ACCESS_VIEW_DESC outLinearDepthDesc = {};
    outLinearDepthDesc.Format = Shader_Dx12::TranslateTypelessFormats(outDepthDesc.Format);
    outLinearDepthDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    outLinearDepthDesc.Texture2D.MipSlice = 0;

    InDevice->CreateUnorderedAccessView(linearDepth.rawResource, nullptr, &outLinearDepthDesc,
                                        currentHeap.GetUavCPU(0));

    // Packed Normals
    auto outNormalsDesc = normals.rawResource->GetDesc();
    D3D12_UNORDERED_ACCESS_VIEW_DESC outPackedNormalsDesc = {};
    outPackedNormalsDesc.Format = Shader_Dx12::TranslateTypelessFormats(outNormalsDesc.Format);
    outPackedNormalsDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    outPackedNormalsDesc.Texture2D.MipSlice = 0;

    InDevice->CreateUnorderedAccessView(normals.rawResource, nullptr, &outPackedNormalsDesc, currentHeap.GetUavCPU(1));

    // Specular Albedo
    auto outSpecularAlbedoDesc = specularAlbedo.rawResource->GetDesc();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavSpecularAlbedoDesc = {};
    uavSpecularAlbedoDesc.Format = Shader_Dx12::TranslateTypelessFormats(outSpecularAlbedoDesc.Format);
    uavSpecularAlbedoDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavSpecularAlbedoDesc.Texture2D.MipSlice = 0;

    InDevice->CreateUnorderedAccessView(specularAlbedo.rawResource, nullptr, &uavSpecularAlbedoDesc,
                                        currentHeap.GetUavCPU(2));

    // Diffuse Albedo
    auto outDiffuseAlbedoDesc = diffuseAlbedo.rawResource->GetDesc();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDiffuseAlbedoDesc = {};
    uavDiffuseAlbedoDesc.Format = Shader_Dx12::TranslateTypelessFormats(outDiffuseAlbedoDesc.Format);
    uavDiffuseAlbedoDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDiffuseAlbedoDesc.Texture2D.MipSlice = 0;

    InDevice->CreateUnorderedAccessView(diffuseAlbedo.rawResource, nullptr, &uavDiffuseAlbedoDesc,
                                        currentHeap.GetUavCPU(3));

    // Fused Albedo
    auto outFusedAlbedoDesc = fusedAlbedo.rawResource->GetDesc();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavFusedAlbedoDesc = {};
    uavFusedAlbedoDesc.Format = Shader_Dx12::TranslateTypelessFormats(outFusedAlbedoDesc.Format);
    uavFusedAlbedoDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavFusedAlbedoDesc.Texture2D.MipSlice = 0;

    InDevice->CreateUnorderedAccessView(fusedAlbedo.rawResource, nullptr, &uavFusedAlbedoDesc,
                                        currentHeap.GetUavCPU(4));

    // Motion Vectors
    auto outMotionVectorsDesc = motionVectors.rawResource->GetDesc();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavMotionVectorsDesc = {};
    uavMotionVectorsDesc.Format = Shader_Dx12::TranslateTypelessFormats(outMotionVectorsDesc.Format);
    uavMotionVectorsDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavMotionVectorsDesc.Texture2D.MipSlice = 0;

    InDevice->CreateUnorderedAccessView(motionVectors.rawResource, nullptr, &uavMotionVectorsDesc,
                                        currentHeap.GetUavCPU(5));

    // Color
    auto outColorDesc = color.rawResource->GetDesc();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavColorDesc = {};
    uavColorDesc.Format = Shader_Dx12::TranslateTypelessFormats(outColorDesc.Format);
    uavColorDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavColorDesc.Texture2D.MipSlice = 0;

    InDevice->CreateUnorderedAccessView(color.rawResource, nullptr, &uavColorDesc, currentHeap.GetUavCPU(6));

    InternalConstants constants {};

    constants.depthNonLinear = InConstants.depthNonLinear;
    constants.depthInverted = InConstants.depthInverted;
    constants.cameraFar = InConstants.cameraFar;
    constants.cameraNear = InConstants.cameraNear;
    memcpy(&constants.InvProjection, &InConstants.InvProjection, sizeof(constants.InvProjection));
    memcpy(&constants.InvViewProjection, &InConstants.InvViewProjection, sizeof(constants.InvViewProjection));
    memcpy(&constants.PrevView, &InConstants.PrevView, sizeof(constants.PrevView));

    constants.roughnessInNormals = InConstants.roughnessInNormals;

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

    dispatchWidth = static_cast<UINT>((inDepthDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    dispatchHeight = (inDepthDesc.Height + InNumThreadsY - 1) / InNumThreadsY;

    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

DNT_Dx12::DNT_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
        // 7 SRVs starting at register t0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0),

        // 6 UAVs starting at register u0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 7, 0, 0),

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
    // if (Config::Instance()->UsePrecompiledShaders.value_or_default())
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
    // else
    {
        // Compile shader blobs
        ID3DBlob* _recEncodeShader = DNT_CompileShader(dntCode.c_str(), "CSMain", "cs_5_0");

        if (_recEncodeShader == nullptr)
        {
            LOG_ERROR("[{0}] DNT_CompileShader error!", _name);
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

    for (int i = 0; i < DNT_NUM_OF_HEAPS; i++)
    {
        if (!_frameHeaps[i].Initialize(InDevice, 8, 7, 1))
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

DNT_Dx12::~DNT_Dx12()
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

    for (int i = 0; i < DNT_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }

    // TODO: release resources

    if (linearDepth.rawResource != nullptr)
    {
        linearDepth.rawResource->Release();
        linearDepth.rawResource = nullptr;
    }

    if (_constantBuffer != nullptr)
    {
        _constantBuffer->Release();
        _constantBuffer = nullptr;
    }
}
