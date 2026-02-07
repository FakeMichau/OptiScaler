#include "ResTrack_dx12.h"

#include <Config.h>
#include <State.h>
#include <Util.h>

#include <menu/menu_overlay_dx.h>

#include <algorithm>
#include <future>

#include <magic_enum_utility.hpp>
#include <include/d3dx/d3dx12.h>
#include <detours/detours.h>
#include <fstream>
#include <iterator>

#ifndef STDMETHODCALLTYPE
#include <Unknwn.h> // or <objbase.h> to get STDMETHODCALLTYPE
#endif

// Device hooks for FG
typedef void(STDMETHODCALLTYPE* PFN_CreateRenderTargetView)(ID3D12Device* This, ID3D12Resource* pResource,
                                                            D3D12_RENDER_TARGET_VIEW_DESC* pDesc,
                                                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_CreateShaderResourceView)(ID3D12Device* This, ID3D12Resource* pResource,
                                                              D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc,
                                                              D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_CreateUnorderedAccessView)(ID3D12Device* This, ID3D12Resource* pResource,
                                                               ID3D12Resource* pCounterResource,
                                                               D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc,
                                                               D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_CreateDepthStencilView)(ID3D12Device* This, ID3D12Resource* pResource,
                                                            const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc,
                                                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_CreateConstantBufferView)(ID3D12Device* This,
                                                              const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc,
                                                              D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

typedef void(STDMETHODCALLTYPE* PFN_CreateSampler)(ID3D12Device* This, const D3D12_SAMPLER_DESC* pDesc,
                                                   D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateDescriptorHeap)(ID3D12Device* This,
                                                             D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc,
                                                             REFIID riid, void** ppvHeap);
typedef ULONG(STDMETHODCALLTYPE* PFN_HeapRelease)(ID3D12DescriptorHeap* This);
typedef void(STDMETHODCALLTYPE* PFN_CopyDescriptors)(ID3D12Device* This, UINT NumDestDescriptorRanges,
                                                     D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts,
                                                     UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges,
                                                     D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts,
                                                     UINT* pSrcDescriptorRangeSizes,
                                                     D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);
typedef void(STDMETHODCALLTYPE* PFN_CopyDescriptorsSimple)(ID3D12Device* This, UINT NumDescriptors,
                                                           D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
                                                           D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
                                                           D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);
// Device hook for denoiser
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateGraphicsPipelineState)(ID3D12Device* This,
                                                                    const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
                                                                    REFIID riid, _COM_Outptr_ void** ppPipelineState);

typedef HRESULT(STDMETHODCALLTYPE* PFN_LoadGraphicsPipeline)(ID3D12PipelineLibrary1* This, LPCWSTR pName,
                                                        const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
                                                        REFIID riid, void** ppPipelineState);

// Command list hooks for FG
typedef void(STDMETHODCALLTYPE* PFN_OMSetRenderTargets)(ID3D12GraphicsCommandList* This,
                                                        UINT NumRenderTargetDescriptors,
                                                        D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors,
                                                        BOOL RTsSingleHandleToDescriptorRange,
                                                        D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_SetGraphicsRootDescriptorTable)(ID3D12GraphicsCommandList* This,
                                                                    UINT RootParameterIndex,
                                                                    D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_SetComputeRootDescriptorTable)(ID3D12GraphicsCommandList* This,
                                                                   UINT RootParameterIndex,
                                                                   D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_DrawIndexedInstanced)(ID3D12GraphicsCommandList* This, UINT IndexCountPerInstance,
                                                          UINT InstanceCount, UINT StartIndexLocation,
                                                          INT BaseVertexLocation, UINT StartInstanceLocation);
typedef void(STDMETHODCALLTYPE* PFN_DrawInstanced)(ID3D12GraphicsCommandList* This, UINT VertexCountPerInstance,
                                                   UINT InstanceCount, UINT StartVertexLocation,
                                                   UINT StartInstanceLocation);
typedef void(STDMETHODCALLTYPE* PFN_Dispatch)(ID3D12GraphicsCommandList* This, UINT ThreadGroupCountX,
                                              UINT ThreadGroupCountY, UINT ThreadGroupCountZ);
typedef void(STDMETHODCALLTYPE* PFN_ExecuteBundle)(ID3D12GraphicsCommandList* This,
                                                   ID3D12GraphicsCommandList* pCommandList);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Close)(ID3D12GraphicsCommandList* This);

typedef void(STDMETHODCALLTYPE* PFN_ExecuteCommandLists)(ID3D12CommandQueue* This, UINT NumCommandLists,
                                                         ID3D12CommandList* const* ppCommandLists);

typedef ULONG(STDMETHODCALLTYPE* PFN_Release)(ID3D12Resource* This);

// Command list hook for denoiser
typedef void(STDMETHODCALLTYPE* PFN_SetPipelineState)(ID3D12GraphicsCommandList* This, ID3D12PipelineState *pPipelineState);

// Original method calls for device
static PFN_CreateRenderTargetView o_CreateRenderTargetView = nullptr;
static PFN_CreateShaderResourceView o_CreateShaderResourceView = nullptr;
static PFN_CreateUnorderedAccessView o_CreateUnorderedAccessView = nullptr;
static PFN_CreateDepthStencilView o_CreateDepthStencilView = nullptr;
static PFN_CreateConstantBufferView o_CreateConstantBufferView = nullptr;
static PFN_CreateSampler o_CreateSampler = nullptr;
static PFN_CreateGraphicsPipelineState o_CreateGraphicsPipelineState = nullptr;
static PFN_LoadGraphicsPipeline o_LoadGraphicsPipeline = nullptr;

static PFN_CreateDescriptorHeap o_CreateDescriptorHeap = nullptr;
static PFN_HeapRelease o_HeapRelease = nullptr;
static PFN_CopyDescriptors o_CopyDescriptors = nullptr;
static PFN_CopyDescriptorsSimple o_CopyDescriptorsSimple = nullptr;

// Original method calls for command list
static PFN_Dispatch o_Dispatch = nullptr;
static PFN_DrawInstanced o_DrawInstanced = nullptr;
static PFN_DrawIndexedInstanced o_DrawIndexedInstanced = nullptr;
static PFN_SetPipelineState o_SetPipelineState = nullptr;
static PFN_ExecuteBundle o_ExecuteBundle = nullptr;
static PFN_Close o_Close = nullptr;

static PFN_ExecuteCommandLists o_ExecuteCommandLists = nullptr;
static PFN_Release o_Release = nullptr;

static PFN_OMSetRenderTargets o_OMSetRenderTargets = nullptr;
static PFN_SetGraphicsRootDescriptorTable o_SetGraphicsRootDescriptorTable = nullptr;
static PFN_SetComputeRootDescriptorTable o_SetComputeRootDescriptorTable = nullptr;

// heaps
static SpinLock _heapCreationMutex;
static std::atomic<UINT> fgHeapIndex { 0 };
static std::unique_ptr<HeapInfo> fgHeaps[1000];

static std::set<void*> _notFoundCmdLists;
static std::unordered_map<FG_ResourceType, void*> _resCmdList[BUFFER_COUNT];

struct HeapCacheTLS
{
    int index = -1;
    unsigned genSeen = 0;
};

static thread_local HeapCacheTLS cache;
static thread_local HeapCacheTLS cacheRTV;
static thread_local HeapCacheTLS cacheCBV;
static thread_local HeapCacheTLS cacheSRV;
static thread_local HeapCacheTLS cacheUAV;
static std::atomic<unsigned> gHeapGeneration { 1 };

static thread_local HeapCacheTLS cacheGR;
static thread_local HeapCacheTLS cacheCR;

bool ResTrack_Dx12::CheckResource(ID3D12Resource* resource)
{
    if (State::Instance().isShuttingDown)
        return false;

    auto resDesc = resource->GetDesc();

    if (resDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        return false;

    auto& s = State::Instance();

    if (resDesc.Height != s.currentSwapchainDesc.BufferDesc.Height ||
        resDesc.Width != s.currentSwapchainDesc.BufferDesc.Width)
    {
        auto result = Config::Instance()->FGRelaxedResolutionCheck.value_or_default() &&
                      resDesc.Height >= s.currentSwapchainDesc.BufferDesc.Height - 32 &&
                      resDesc.Height <= s.currentSwapchainDesc.BufferDesc.Height + 32 &&
                      resDesc.Width >= s.currentSwapchainDesc.BufferDesc.Width - 32 &&
                      resDesc.Width <= s.currentSwapchainDesc.BufferDesc.Width + 32;

        // TODO: narrow down to upscaler input-sized buffers
        bool cyberprankAmdDenoiser = true;
        if (cyberprankAmdDenoiser && resDesc.Width == 2293 && resDesc.Height == 960 &&
            resDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
            result = true;

        // LOG_TRACK("Resource: {}x{} ({}), Swapchain: {}x{} ({}), Relaxed Result: {}", resDesc.Width, resDesc.Height,
        //           (UINT) resDesc.Format, scDesc.BufferDesc.Width, scDesc.BufferDesc.Height,
        //           (UINT) scDesc.BufferDesc.Format, result);

        return result;
    }

    return true;
}

inline static IID streamlineRiid {};
bool ResTrack_Dx12::CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject)
{
    if (streamlineRiid.Data1 == 0)
    {
        auto iidResult = IIDFromString(L"{ADEC44E2-61F0-45C3-AD9F-1B37379284FF}", &streamlineRiid);

        if (iidResult != S_OK)
            return false;
    }

    auto qResult = pObject->QueryInterface(streamlineRiid, (void**) ppRealObject);

    if (qResult == S_OK && *ppRealObject != nullptr)
    {
        LOG_INFO("{} Streamline proxy found!", functionName);
        (*ppRealObject)->Release();
        return true;
    }

    return false;
}

#pragma region Resource methods

bool ResTrack_Dx12::CreateBufferResource(ID3D12Device* InDevice, ResourceInfo* InSource, D3D12_RESOURCE_STATES InState,
                                         ID3D12Resource** OutResource)
{
    if (InDevice == nullptr || InSource == nullptr)
        return false;

    if (*OutResource != nullptr)
    {
        auto bufDesc = (*OutResource)->GetDesc();

        if (bufDesc.Width != (UINT64) (InSource->width) || bufDesc.Height != (UINT) (InSource->height) ||
            bufDesc.Format != InSource->format)
        {
            (*OutResource)->Release();
            (*OutResource) = nullptr;
        }
        else
            return true;
    }

    D3D12_HEAP_PROPERTIES heapProperties;
    D3D12_HEAP_FLAGS heapFlags;
    HRESULT hr = InSource->buffer->GetHeapProperties(&heapProperties, &heapFlags);

    if (hr != S_OK)
    {
        LOG_ERROR("GetHeapProperties result: {0:X}", (UINT64) hr);
        return false;
    }

    D3D12_RESOURCE_DESC texDesc = InSource->buffer->GetDesc();
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = InDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &texDesc, InState, nullptr,
                                           IID_PPV_ARGS(OutResource));

    if (hr != S_OK)
    {
        LOG_ERROR("CreateCommittedResource result: {0:X}", (UINT64) hr);
        return false;
    }

    (*OutResource)->SetName(L"fgHudlessSCBufferCopy");
    return true;
}

void ResTrack_Dx12::ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                    D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = InResource;
    barrier.Transition.StateBefore = InBeforeState;
    barrier.Transition.StateAfter = InAfterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    InCommandList->ResourceBarrier(1, &barrier);
}

#pragma endregion

#pragma region Heap helpers

SIZE_T ResTrack_Dx12::GetGPUHandle(ID3D12Device* This, SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    UINT count = fgHeapIndex.load(std::memory_order_acquire);

    for (UINT i = 0; i < count; i++)
    {
        auto val = fgHeaps[i].get();
        if (val->active && val->cpuStart < cpuHandle && val->cpuEnd >= cpuHandle && val->gpuStart != 0)
        {
            auto incSize = This->GetDescriptorHandleIncrementSize(type);
            auto addr = cpuHandle - val->cpuStart;
            auto index = addr / incSize;
            auto gpuAddr = val->gpuStart + (index * incSize);

            return gpuAddr;
        }
    }

    return NULL;
}

SIZE_T ResTrack_Dx12::GetCPUHandle(ID3D12Device* This, SIZE_T gpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    UINT count = fgHeapIndex.load(std::memory_order_acquire);

    for (UINT i = 0; i < count; i++)
    {
        auto val = fgHeaps[i].get();
        if (val->active && val->gpuStart < gpuHandle && val->gpuEnd >= gpuHandle && val->cpuStart != 0)
        {
            auto incSize = This->GetDescriptorHandleIncrementSize(type);
            auto addr = gpuHandle - val->gpuStart;
            auto index = addr / incSize;
            auto cpuAddr = val->cpuStart + (index * incSize);

            return cpuAddr;
        }
    }

    return NULL;
}

HeapInfo* ResTrack_Dx12::GetHeapByCpuHandleCBV(SIZE_T cpuHandle)
{
    unsigned currentGen = gHeapGeneration.load(std::memory_order_relaxed);

    if (cacheCBV.genSeen == currentGen && cacheCBV.index != -1)
    {
        auto heapInfo = fgHeaps[cacheCBV.index].get();

        if (heapInfo->active && heapInfo->cpuStart <= cpuHandle && cpuHandle < heapInfo->cpuEnd)
            return heapInfo;
    }

    UINT count = fgHeapIndex.load(std::memory_order_acquire);

    for (UINT i = 0; i < count; i++)
    {
        if (fgHeaps[i]->active && fgHeaps[i]->cpuStart <= cpuHandle && fgHeaps[i]->cpuEnd > cpuHandle)
        {
            cacheCBV.index = i;
            cacheCBV.genSeen = currentGen;
            return fgHeaps[i].get();
        }
    }

    cacheCBV.index = -1;
    return nullptr;
}

HeapInfo* ResTrack_Dx12::GetHeapByCpuHandleRTV(SIZE_T cpuHandle)
{
    unsigned currentGen = gHeapGeneration.load(std::memory_order_relaxed);

    if (cacheRTV.genSeen == currentGen && cacheRTV.index != -1)
    {
        auto heapInfo = fgHeaps[cacheRTV.index].get();

        if (heapInfo->active && heapInfo->cpuStart <= cpuHandle && cpuHandle < heapInfo->cpuEnd)
            return heapInfo;
    }

    UINT count = fgHeapIndex.load(std::memory_order_acquire);

    for (UINT i = 0; i < count; i++)
    {
        if (fgHeaps[i]->active && fgHeaps[i]->cpuStart <= cpuHandle && fgHeaps[i]->cpuEnd > cpuHandle)
        {
            cacheRTV.index = i;
            cacheRTV.genSeen = currentGen;
            return fgHeaps[i].get();
        }
    }

    cacheRTV.index = -1;
    return nullptr;
}

HeapInfo* ResTrack_Dx12::GetHeapByCpuHandleSRV(SIZE_T cpuHandle)
{
    unsigned currentGen = gHeapGeneration.load(std::memory_order_relaxed);

    if (cacheSRV.genSeen == currentGen && cacheSRV.index != -1)
    {
        auto heapInfo = fgHeaps[cacheSRV.index].get();

        if (heapInfo->active && heapInfo->cpuStart <= cpuHandle && cpuHandle < heapInfo->cpuEnd)
            return heapInfo;
    }

    UINT count = fgHeapIndex.load(std::memory_order_acquire);

    for (UINT i = 0; i < count; i++)
    {
        if (fgHeaps[i]->active && fgHeaps[i]->cpuStart <= cpuHandle && fgHeaps[i]->cpuEnd > cpuHandle)
        {
            cacheSRV.index = i;
            cacheSRV.genSeen = currentGen;
            return fgHeaps[i].get();
        }
    }

    cacheSRV.index = -1;
    return nullptr;
}

HeapInfo* ResTrack_Dx12::GetHeapByCpuHandleUAV(SIZE_T cpuHandle)
{
    unsigned currentGen = gHeapGeneration.load(std::memory_order_relaxed);

    if (cacheUAV.genSeen == currentGen && cacheUAV.index != -1)
    {
        auto heapInfo = fgHeaps[cacheUAV.index].get();

        if (heapInfo->active && heapInfo->cpuStart <= cpuHandle && cpuHandle < heapInfo->cpuEnd)
            return heapInfo;
    }

    UINT count = fgHeapIndex.load(std::memory_order_acquire);

    for (UINT i = 0; i < count; i++)
    {
        if (fgHeaps[i]->active && fgHeaps[i]->cpuStart <= cpuHandle && fgHeaps[i]->cpuEnd > cpuHandle)
        {
            cacheUAV.index = i;
            cacheUAV.genSeen = currentGen;
            return fgHeaps[i].get();
        }
    }

    cacheUAV.index = -1;
    return nullptr;
}

HeapInfo* ResTrack_Dx12::GetHeapByCpuHandle(SIZE_T cpuHandle)
{
    unsigned currentGen = gHeapGeneration.load(std::memory_order_relaxed);

    {
        if (cache.genSeen == currentGen && cache.index != -1)
        {
            auto heapInfo = fgHeaps[cache.index].get();

            if (heapInfo->active && heapInfo->cpuStart <= cpuHandle && cpuHandle < heapInfo->cpuEnd)
                return heapInfo;
        }
    }

    UINT count = fgHeapIndex.load(std::memory_order_acquire);

    for (UINT i = 0; i < count; i++)
    {
        if (fgHeaps[i]->active && fgHeaps[i]->cpuStart <= cpuHandle && fgHeaps[i]->cpuEnd > cpuHandle)
        {
            cache.index = i;
            cache.genSeen = currentGen;
            return fgHeaps[i].get();
        }
    }

    cache.index = -1;
    return nullptr;
}

HeapInfo* ResTrack_Dx12::GetHeapByGpuHandleGR(SIZE_T gpuHandle)
{
    unsigned currentGen = gHeapGeneration.load(std::memory_order_relaxed);

    {
        if (cacheGR.genSeen == currentGen && cacheGR.index != -1)
        {
            auto heapInfo = fgHeaps[cacheGR.index].get();

            if (heapInfo->active && heapInfo->gpuStart <= gpuHandle && gpuHandle < heapInfo->gpuEnd)
                return heapInfo;
        }
    }

    UINT count = fgHeapIndex.load(std::memory_order_acquire);

    for (UINT i = 0; i < count; i++)
    {
        if (fgHeaps[i]->active && fgHeaps[i]->gpuStart <= gpuHandle && fgHeaps[i]->gpuEnd > gpuHandle)
        {
            cacheGR.index = i;
            cacheGR.genSeen = currentGen;
            return fgHeaps[i].get();
        }
    }

    cacheGR.index = -1;
    return nullptr;
}

HeapInfo* ResTrack_Dx12::GetHeapByGpuHandleCR(SIZE_T gpuHandle)
{
    if (gpuHandle == NULL)
        return nullptr;

    unsigned currentGen = gHeapGeneration.load(std::memory_order_relaxed);

    {
        if (cacheCR.genSeen == currentGen && cacheCR.index != -1)
        {
            auto heapInfo = fgHeaps[cacheCR.index].get();

            if (heapInfo->active && heapInfo->gpuStart <= gpuHandle && gpuHandle < heapInfo->gpuEnd)
                return heapInfo;
        }
    }

    UINT count = fgHeapIndex.load(std::memory_order_acquire);

    for (UINT i = 0; i < count; i++)
    {
        if (fgHeaps[i]->active && fgHeaps[i]->gpuStart <= gpuHandle && fgHeaps[i]->gpuEnd > gpuHandle)
        {
            cacheCR.index = i;
            cacheCR.genSeen = currentGen;
            return fgHeaps[i].get();
        }
    }

    cacheCR.index = -1;
    return nullptr;
}

#pragma endregion

#pragma region Hudless methods

void ResTrack_Dx12::FillResourceInfo(ID3D12Resource* resource, ResourceInfo* info)
{
    auto desc = resource->GetDesc();
    info->buffer = resource;
    info->width = desc.Width;
    info->height = desc.Height;
    info->format = desc.Format;
    info->flags = desc.Flags;
}

bool ResTrack_Dx12::IsHudFixActive()
{
    if (!Config::Instance()->FGEnabled.value_or_default() || !Config::Instance()->FGHUDFix.value_or_default())
    {
        LOG_TRACK(
            "!Config::Instance()->FGEnabled.value_or_default() || !Config::Instance()->FGHUDFix.value_or_default()");
        return false;
    }

    if (State::Instance().currentFG == nullptr || State::Instance().currentFeature == nullptr ||
        State::Instance().FGchanged)
    {
        LOG_TRACK("State::Instance().currentFG == nullptr || State::Instance().currentFeature == nullptr || "
                  "State::Instance().FGchanged");
        return false;
    }

    if (!State::Instance().currentFG->IsActive())
    {
        LOG_TRACK("!State::Instance().currentFG->IsActive()");
        return false;
    }

    if (!_presentDone)
    {
        LOG_TRACK("!_presentDone");
        return false;
    }

    if (Hudfix_Dx12::SkipHudlessChecks())
    {
        LOG_TRACK("!Hudfix_Dx12::SkipHudlessChecks()");
        return false;
    }

    if (!Hudfix_Dx12::IsResourceCheckActive())
    {
        // LOG_TRACK("!Hudfix_Dx12::IsResourceCheckActive()");
        return false;
    }

    return true;
}

#pragma endregion

#pragma region Resource input hooks

void ResTrack_Dx12::hkCreateRenderTargetView(ID3D12Device* This, ID3D12Resource* pResource,
                                             D3D12_RENDER_TARGET_VIEW_DESC* pDesc,
                                             D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    // force hdr for swapchain buffer
    if (pResource != nullptr && pDesc != nullptr && Config::Instance()->ForceHDR.value_or_default())
    {
        for (size_t i = 0; i < State::Instance().SCbuffers.size(); i++)
        {
            if (State::Instance().SCbuffers[i] == pResource)
            {
                if (Config::Instance()->UseHDR10.value_or_default())
                    pDesc->Format = DXGI_FORMAT_R10G10B10A2_UNORM;
                else
                    pDesc->Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

                break;
            }
        }
    }

    o_CreateRenderTargetView(This, pResource, pDesc, DestDescriptor);

    if (Config::Instance()->FGHudfixDisableRTV.value_or_default())
        return;

    if (pResource == nullptr)
    {
        auto heap = GetHeapByCpuHandleRTV(DestDescriptor.ptr);

        if (heap != nullptr)
            heap->ClearByCpuHandle(DestDescriptor.ptr);

        return;
    }

    if (!CheckResource(pResource))
        return;

    auto heap = GetHeapByCpuHandleRTV(DestDescriptor.ptr);
    if (heap != nullptr)
    {
        ResourceInfo resInfo {};
        FillResourceInfo(pResource, &resInfo);
        resInfo.type = RTV;
        resInfo.captureInfo = CaptureInfo::CreateRTV;
        heap->SetByCpuHandle(DestDescriptor.ptr, resInfo);
    }
    // else
    //{
    //     LOG_TRACK("Heap not found for RTV: {:X}", DestDescriptor.ptr);
    // }
}

void ResTrack_Dx12::hkCreateShaderResourceView(ID3D12Device* This, ID3D12Resource* pResource,
                                               D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc,
                                               D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    // force hdr for swapchain buffer
    if (pResource != nullptr && pDesc != nullptr && Config::Instance()->ForceHDR.value_or_default())
    {
        for (size_t i = 0; i < State::Instance().SCbuffers.size(); i++)
        {
            if (State::Instance().SCbuffers[i] == pResource)
            {
                if (Config::Instance()->UseHDR10.value_or_default())
                    pDesc->Format = DXGI_FORMAT_R10G10B10A2_UNORM;
                else
                    pDesc->Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

                break;
            }
        }
    }

    o_CreateShaderResourceView(This, pResource, pDesc, DestDescriptor);

    if (Config::Instance()->FGHudfixDisableSRV.value_or_default())
        return;

    if (pResource == nullptr)
    {
        auto heap = GetHeapByCpuHandleSRV(DestDescriptor.ptr);

        if (heap != nullptr)
            heap->ClearByCpuHandle(DestDescriptor.ptr);

        return;
    }

    if (!CheckResource(pResource))
        return;

    auto heap = GetHeapByCpuHandleSRV(DestDescriptor.ptr);
    if (heap != nullptr)
    {
        ResourceInfo resInfo {};
        FillResourceInfo(pResource, &resInfo);
        resInfo.type = SRV;
        resInfo.captureInfo = CaptureInfo::CreateSRV;
        heap->SetByCpuHandle(DestDescriptor.ptr, resInfo);
    }
    // else
    //{
    //     LOG_TRACK("Heap not found for SRV: {:X}", DestDescriptor.ptr);
    // }
}

void ResTrack_Dx12::hkCreateUnorderedAccessView(ID3D12Device* This, ID3D12Resource* pResource,
                                                ID3D12Resource* pCounterResource,
                                                D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc,
                                                D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    if (pResource != nullptr && pDesc != nullptr && Config::Instance()->ForceHDR.value_or_default())
    {
        for (size_t i = 0; i < State::Instance().SCbuffers.size(); i++)
        {
            if (State::Instance().SCbuffers[i] == pResource)
            {
                if (Config::Instance()->UseHDR10.value_or_default())
                    pDesc->Format = DXGI_FORMAT_R10G10B10A2_UNORM;
                else
                    pDesc->Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

                break;
            }
        }
    }

    o_CreateUnorderedAccessView(This, pResource, pCounterResource, pDesc, DestDescriptor);

    if (Config::Instance()->FGHudfixDisableUAV.value_or_default())
        return;

    if (pResource == nullptr)
    {
        auto heap = GetHeapByCpuHandleUAV(DestDescriptor.ptr);

        if (heap != nullptr)
            heap->ClearByCpuHandle(DestDescriptor.ptr);

        return;
    }

    if (!CheckResource(pResource))
        return;

    auto heap = GetHeapByCpuHandleUAV(DestDescriptor.ptr);
    if (heap != nullptr)
    {
        ResourceInfo resInfo {};
        FillResourceInfo(pResource, &resInfo);
        resInfo.type = UAV;
        resInfo.captureInfo = CaptureInfo::CreateUAV;
        heap->SetByCpuHandle(DestDescriptor.ptr, resInfo);
    }
    // else
    //{
    //     LOG_TRACK("Heap not found for UAV: {:X}", DestDescriptor.ptr);
    // }
}

#pragma endregion

void ResTrack_Dx12::hkExecuteCommandLists(ID3D12CommandQueue* This, UINT NumCommandLists,
                                          ID3D12CommandList* const* ppCommandLists)
{
    auto signal = false;
    auto fg = State::Instance().currentFG;

    if (fg != nullptr && fg->IsActive() && !fg->IsPaused())
    {
        LOG_TRACK("NumCommandLists: {}", NumCommandLists);

        std::vector<FG_ResourceType> found;
        auto fIndex = fg->GetIndex();

        do
        {
            std::lock_guard<std::mutex> lock2(_resourceCommandListMutex);

            if (!_notFoundCmdLists.empty())
            {
                for (size_t i = 0; i < NumCommandLists; i++)
                {
                    if (_notFoundCmdLists.contains(ppCommandLists[i]))
                    {
                        LOG_WARN("Found last frames cmdList: {:X}", (size_t) ppCommandLists[i]);
                        _notFoundCmdLists.erase(ppCommandLists[i]);
                    }
                }
            }

            if (_resCmdList[fIndex].empty())
                break;

            for (size_t i = 0; i < NumCommandLists; i++)
            {
                LOG_TRACK("ppCommandLists[{}]: {:X}", i, (size_t) ppCommandLists[i]);

                for (const auto& pair : _resCmdList[fIndex])
                {
                    if (pair.second == ppCommandLists[i])
                    {
                        LOG_DEBUG("found {} cmdList: {:X}, queue: {:X}", (UINT) pair.first, (size_t) pair.second,
                                  (size_t) This);
                        fg->SetResourceReady(pair.first);
                        found.push_back(pair.first);
                    }
                }

                for (size_t i = 0; i < found.size(); i++)
                {
                    _resCmdList[fIndex].erase(found[i]);
                }

                if (_resCmdList[fIndex].empty())
                    break;
            }

        } while (false);

        if (!found.empty())
        {
            o_ExecuteCommandLists(This, NumCommandLists, ppCommandLists);

            for (size_t i = 0; i < found.size(); i++)
            {
                fg->SetCommandQueue(found[i], This);
            }

            return;
        }
    }

    LOG_TRACK("Done NumCommandLists: {}", NumCommandLists);

    o_ExecuteCommandLists(This, NumCommandLists, ppCommandLists);
}

#pragma region Heap hooks

static ULONG STDMETHODCALLTYPE hkHeapRelease(ID3D12DescriptorHeap* This)
{
    if (State::Instance().isShuttingDown)
        return o_HeapRelease(This);

    This->AddRef();
    if (o_HeapRelease(This) <= 1)
    {
        UINT count = fgHeapIndex.load(std::memory_order_acquire);

        for (UINT i = 0; i < count; ++i)
        {
            auto& up = fgHeaps[i];

            if (up == nullptr || up->heap != This)
                continue;

            up->active = false;

            LOG_INFO("Heap released: {:X}", (size_t) This);

            // detach all slots from _trackedResources
            {
                std::scoped_lock lk(_trackedResourcesMutex);

                for (UINT j = 0; j < up->numDescriptors; ++j)
                {
                    auto& slot = up->info[j];

                    if (slot.buffer == nullptr)
                        continue;

                    if (auto it = _trackedResources.find(slot.buffer); it != _trackedResources.end())
                    {
                        auto& vec = it->second;
                        vec.erase(std::remove(vec.begin(), vec.end(), &slot), vec.end());
                        if (vec.empty())
                            _trackedResources.erase(it);
                    }

                    slot.buffer = nullptr;
                    slot.lastUsedFrame = 0;
                }
            }

            gHeapGeneration.fetch_add(1, std::memory_order_relaxed); // invalidate caches

            break;
        }
    }

    return o_HeapRelease(This);
}

HRESULT ResTrack_Dx12::hkCreateDescriptorHeap(ID3D12Device* This, D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc,
                                              REFIID riid, void** ppvHeap)
{
    auto result = o_CreateDescriptorHeap(This, pDescriptorHeapDesc, riid, ppvHeap);

    if (State::Instance().skipHeapCapture)
        return result;

    // try to calculate handle ranges for heap
    if (result == S_OK && (pDescriptorHeapDesc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
                           pDescriptorHeapDesc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV))
    {
        auto heap = (ID3D12DescriptorHeap*) (*ppvHeap);

        if (!o_HeapRelease)
        {
            PVOID* vtbl = *(PVOID**) heap;
            o_HeapRelease = (PFN_HeapRelease) vtbl[2];
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&) o_HeapRelease, hkHeapRelease);
            DetourTransactionCommit();
        }

        auto increment = This->GetDescriptorHandleIncrementSize(pDescriptorHeapDesc->Type);
        auto numDescriptors = pDescriptorHeapDesc->NumDescriptors;
        auto cpuStart = (SIZE_T) (heap->GetCPUDescriptorHandleForHeapStart().ptr);
        auto cpuEnd = cpuStart + (increment * numDescriptors);
        auto gpuStart = (SIZE_T) (heap->GetGPUDescriptorHandleForHeapStart().ptr);
        auto gpuEnd = gpuStart + (increment * numDescriptors);
        auto type = (UINT) pDescriptorHeapDesc->Type;

        LOG_TRACE("Heap: {:X}, Heap type: {}, Cpu: {}-{}, Gpu: {}-{}, Desc count: {}", (size_t) *ppvHeap, type,
                  cpuStart, cpuEnd, gpuStart, gpuEnd, numDescriptors);
        {
            std::lock_guard<SpinLock> lock(_heapCreationMutex);

            fgHeaps[fgHeapIndex] = std::make_unique<HeapInfo>(heap, cpuStart, cpuEnd, gpuStart, gpuEnd, numDescriptors,
                                                              increment, type, fgHeapIndex);
            fgHeapIndex++;
            gHeapGeneration.fetch_add(1, std::memory_order_relaxed);
        }
    }
    else
    {
        if (*ppvHeap != nullptr)
        {
            auto heap = (ID3D12DescriptorHeap*) (*ppvHeap);
            LOG_TRACE("Skipping, Heap type: {}, Cpu: {}, Gpu: {}", (UINT) pDescriptorHeapDesc->Type,
                      heap->GetCPUDescriptorHandleForHeapStart().ptr, heap->GetGPUDescriptorHandleForHeapStart().ptr);
        }
    }

    return result;
}

ULONG ResTrack_Dx12::hkRelease(ID3D12Resource* This)
{
    if (State::Instance().isShuttingDown)
        return o_Release(This);

    _trackedResourcesMutex.lock();

    This->AddRef();
    if (o_Release(This) <= 1 && _trackedResources.contains(This))
    {
        auto vector = &_trackedResources[This];

        LOG_TRACK("Resource: {:X}, Heaps: {}", (size_t) This, vector->size());

        for (size_t i = 0; i < vector->size(); i++)
        {
            // Be sure something else is not using this heap
            if (vector->at(i)->buffer == This)
            {
                LOG_TRACK("  Resource: {:X}, Clearing: {:X}", (size_t) This, (size_t) vector->at(i));
                vector->at(i)->buffer = nullptr;
                vector->at(i)->lastUsedFrame = 0;
            }
        }

        State::Instance().CapturedHudlesses.erase(This);
        _trackedResources.erase(This);
    }

    _trackedResourcesMutex.unlock();

    return o_Release(This);
}

void ResTrack_Dx12::hkCopyDescriptors(ID3D12Device* This, UINT NumDestDescriptorRanges,
                                      D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts,
                                      UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges,
                                      D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts,
                                      UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType)
{
    o_CopyDescriptors(This, NumDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
                      NumSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes, DescriptorHeapsType);

    // Early exit conditions
    if (DescriptorHeapsType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
        DescriptorHeapsType != D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
        return;

    if (NumDestDescriptorRanges == 0 || pDestDescriptorRangeStarts == nullptr)
        return;

    if (!Config::Instance()->FGAlwaysTrackHeaps.value_or_default() && !IsHudFixActive())
        return;

    const UINT inc = This->GetDescriptorHandleIncrementSize(DescriptorHeapsType);

    // Validate that we have source descriptors to copy
    bool haveSources = (NumSrcDescriptorRanges > 0 && pSrcDescriptorRangeStarts != nullptr);

    // Track positions in both source and destination ranges
    UINT srcRangeIndex = 0;
    UINT srcOffsetInRange = 0;
    UINT destRangeIndex = 0;
    UINT destOffsetInRange = 0;

    // Cache for heap lookups to avoid repeated lookups within the same range
    HeapInfo* cachedDestHeap = nullptr;
    SIZE_T cachedDestRangeStart = 0;
    HeapInfo* cachedSrcHeap = nullptr;
    SIZE_T cachedSrcRangeStart = 0;

    // Process all destination descriptors
    while (destRangeIndex < NumDestDescriptorRanges)
    {
        const UINT destRangeSize =
            (pDestDescriptorRangeSizes == nullptr) ? 1 : pDestDescriptorRangeSizes[destRangeIndex];

        // Update destination heap cache if we've moved to a new range
        if (destOffsetInRange == 0 || cachedDestHeap == nullptr)
        {
            cachedDestRangeStart = pDestDescriptorRangeStarts[destRangeIndex].ptr;
            cachedDestHeap = GetHeapByCpuHandle(cachedDestRangeStart);
        }

        // Calculate current destination handle
        const SIZE_T destHandle = cachedDestRangeStart + (static_cast<SIZE_T>(destOffsetInRange) * inc);

        // Get or update source information
        ResourceInfo* srcInfo = nullptr;
        if (haveSources && srcRangeIndex < NumSrcDescriptorRanges)
        {
            const UINT srcRangeSize =
                (pSrcDescriptorRangeSizes == nullptr) ? 1 : pSrcDescriptorRangeSizes[srcRangeIndex];

            // Update source heap cache if we've moved to a new range
            if (srcOffsetInRange == 0 || cachedSrcHeap == nullptr)
            {
                cachedSrcRangeStart = pSrcDescriptorRangeStarts[srcRangeIndex].ptr;
                cachedSrcHeap = GetHeapByCpuHandle(cachedSrcRangeStart);
            }

            // Calculate current source handle
            const SIZE_T srcHandle = cachedSrcRangeStart + (static_cast<SIZE_T>(srcOffsetInRange) * inc);

            // Get source resource info
            if (cachedSrcHeap != nullptr)
                srcInfo = cachedSrcHeap->GetByCpuHandle(srcHandle);

            // Advance source position
            srcOffsetInRange++;
            if (srcOffsetInRange >= srcRangeSize)
            {
                srcOffsetInRange = 0;
                srcRangeIndex++;
                cachedSrcHeap = nullptr; // Invalidate cache
            }
        }

        // Update destination heap tracking
        if (cachedDestHeap != nullptr)
        {
            if (srcInfo != nullptr && srcInfo->buffer != nullptr)
                cachedDestHeap->SetByCpuHandle(destHandle, *srcInfo);
            else
                cachedDestHeap->ClearByCpuHandle(destHandle);
        }

        // Advance destination position
        destOffsetInRange++;
        if (destOffsetInRange >= destRangeSize)
        {
            destOffsetInRange = 0;
            destRangeIndex++;
            cachedDestHeap = nullptr; // Invalidate cache
        }
    }
}

void ResTrack_Dx12::hkCopyDescriptorsSimple(ID3D12Device* This, UINT NumDescriptors,
                                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
                                            D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
                                            D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType)
{
    o_CopyDescriptorsSimple(This, NumDescriptors, DestDescriptorRangeStart, SrcDescriptorRangeStart,
                            DescriptorHeapsType);

    if (DescriptorHeapsType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
        DescriptorHeapsType != D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
        return;

    if (!Config::Instance()->FGAlwaysTrackHeaps.value_or_default() && !IsHudFixActive())
        return;

    auto size = This->GetDescriptorHandleIncrementSize(DescriptorHeapsType);

    for (size_t i = 0; i < NumDescriptors; i++)
    {
        HeapInfo* srcHeap = nullptr;
        SIZE_T srcHandle = 0;

        // source
        if (SrcDescriptorRangeStart.ptr != 0)
        {
            srcHandle = SrcDescriptorRangeStart.ptr + i * size;
            srcHeap = GetHeapByCpuHandle(srcHandle);
        }

        auto destHandle = DestDescriptorRangeStart.ptr + i * size;
        auto dstHeap = GetHeapByCpuHandle(destHandle);

        // destination
        if (dstHeap == nullptr)
            continue;

        if (srcHeap == nullptr)
        {
            dstHeap->ClearByCpuHandle(destHandle);
            continue;
        }

        auto buffer = srcHeap->GetByCpuHandle(srcHandle);

        if (buffer == nullptr)
        {
            dstHeap->ClearByCpuHandle(destHandle);
            continue;
        }

        dstHeap->SetByCpuHandle(destHandle, *buffer);
    }
}

template <> struct std::hash<D3D12_SHADER_BYTECODE>
{
    std::size_t operator()(const D3D12_SHADER_BYTECODE& x) const noexcept
    {
        return std::hash<std::string_view> {}({ reinterpret_cast<const char*>(x.pShaderBytecode), x.BytecodeLength });
    }
};


//bool LoadBinaryFile(const char* path, std::vector<uint8_t>& outData)
//{
//    std::ifstream file(path, std::ios::binary | std::ios::ate);
//    if (!file)
//        return false;
//
//    std::streamsize size = file.tellg();
//    file.seekg(0, std::ios::beg);
//
//    outData.resize(size);
//    return file.read(reinterpret_cast<char*>(outData.data()), size).good();
//}

bool ResTrack_Dx12::isCyberprankShader(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, void** ppPipelineState)
{
    const uint64_t TARGET_SHADER_HASH = 0x017624c170a3e307;
    auto hash = std::hash<D3D12_SHADER_BYTECODE> {};

    // static std::vector<uint8_t> shaderFile;
    // static auto loadResult =
    //     LoadBinaryFile("F:\\SteamLibrary\\steamapps\\common\\Cyberpunk 2077\\bin\\x64\\shaderRT.bin", shaderFile);
    //  && loadResult && pDesc->PS.BytecodeLength == shaderFile.size() &&std::memcmp(pDesc->PS.pShaderBytecode,
    //  shaderFile.data(), shaderFile.size()) == 0

    if (pDesc && pDesc->RTVFormats[0] == DXGI_FORMAT_R16G16B16A16_FLOAT &&
        pDesc->RTVFormats[1] == DXGI_FORMAT_R16G16B16A16_FLOAT)
    {
        // Check if this PSO uses your Pixel Shader
        if (hash(pDesc->PS) == TARGET_SHADER_HASH)
        {
            _cyberprankRTShaderPSO = (ID3D12PipelineState*) *ppPipelineState;
            return true;
        }
    }

    return false;
}

HRESULT ResTrack_Dx12::hkCreateGraphicsPipelineState(ID3D12Device* This, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
                                             REFIID riid, void** ppPipelineState)
{
    HRESULT result = o_CreateGraphicsPipelineState(This, pDesc, riid, ppPipelineState);

    if (SUCCEEDED(result) && isCyberprankShader(pDesc, ppPipelineState))
    {
        LOG_TRACE("Cyberprank shader found");
    }

    return result;
}

HRESULT ResTrack_Dx12::hkLoadGraphicsPipeline(ID3D12PipelineLibrary1* This, LPCWSTR pName,
                                                             const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid,
                                              void** ppPipelineState)
{
    auto result = o_LoadGraphicsPipeline(This, pName, pDesc, riid, ppPipelineState);

    if (SUCCEEDED(result) && isCyberprankShader(pDesc, ppPipelineState))
    {
        LOG_TRACE("Cyberprank shader found");
    }

    return result;
}

#pragma endregion

#pragma region Shader input hooks

void ResTrack_Dx12::hkSetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* This, UINT RootParameterIndex,
                                                     D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    if (Config::Instance()->FGHudfixDisableSGR.value_or_default() || BaseDescriptor.ptr == 0 || !IsHudFixActive() ||
        Hudfix_Dx12::SkipHudlessChecks())
    {
        o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    if (This == MenuOverlayDx::MenuCommandList() /*|| IsFGCommandList(This)*/)
    {
        LOG_DEBUG_ONLY("Menu cmdlist: {} || fgCommandList: {}", This == MenuOverlayDx::MenuCommandList(),
                       IsFGCommandList(This));
        o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    auto heap = GetHeapByGpuHandleGR(BaseDescriptor.ptr);
    if (heap == nullptr)
    {
        LOG_DEBUG_ONLY("No heap!");
        o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    auto capturedBuffer = heap->GetByGpuHandle(BaseDescriptor.ptr);
    if (capturedBuffer == nullptr || capturedBuffer->buffer == nullptr)
    {
        LOG_DEBUG_ONLY("Miss RootParameterIndex: {1}, CommandList: {0:X}, gpuHandle: {2}", (SIZE_T) This,
                       RootParameterIndex, BaseDescriptor.ptr);
        o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    capturedBuffer->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    capturedBuffer->captureInfo = CaptureInfo::SetGR;

    do
    {
        if (Config::Instance()->FGImmediateCapture.value_or_default() &&
            Hudfix_Dx12::CheckForHudless(This, capturedBuffer, capturedBuffer->state))
        {
            break;
        }

        auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;
        size_t shardIdx = GetShardIndex(This);
        auto& shard = _hudlessShards[fIndex][shardIdx];

        {
            std::lock_guard<SpinLock> lock(shard.mutex);

            if (!shard.map.contains(This))
            {
                ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> newMap;
                newMap.reserve(32);
                shard.map.insert_or_assign(This, newMap);
            }

            LOG_TRACK("CmdList: {:X}, AddRef Resource: {:X}, Desc: {:X}, Format: {}", (size_t) This,
                      (size_t) capturedBuffer->buffer, BaseDescriptor.ptr, (UINT) capturedBuffer->format);

            shard.map[This].insert_or_assign(capturedBuffer->buffer, *capturedBuffer);
        }

    } while (false);

    o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
}

#pragma endregion

#pragma region Shader output hooks

void ResTrack_Dx12::hkOMSetRenderTargets(ID3D12GraphicsCommandList* This, UINT NumRenderTargetDescriptors,
                                         D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors,
                                         BOOL RTsSingleHandleToDescriptorRange,
                                         D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor)
{
    bool cyberprankDenoiser = true;

    if (!cyberprankDenoiser)
    {
        if (Config::Instance()->FGHudfixDisableOM.value_or_default() || NumRenderTargetDescriptors == 0 ||
            pRenderTargetDescriptors == nullptr || !IsHudFixActive() || Hudfix_Dx12::SkipHudlessChecks())
        {
            o_OMSetRenderTargets(This, NumRenderTargetDescriptors, pRenderTargetDescriptors,
                                 RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor);
            return;
        }
    }

    LOG_DEBUG_ONLY("NumRenderTargetDescriptors: {}", NumRenderTargetDescriptors);

    if (This == MenuOverlayDx::MenuCommandList() /*|| IsFGCommandList(This)*/)
    {
        LOG_DEBUG_ONLY("Menu cmdlist: {} || fgCommandList: {}", This == MenuOverlayDx::MenuCommandList(),
                       IsFGCommandList(This));

        o_OMSetRenderTargets(This, NumRenderTargetDescriptors, pRenderTargetDescriptors,
                             RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor);
        return;
    }

    {
        {
            std::lock_guard<std::mutex> lock(_cmdStateMutex);
            auto& state = g_CmdState[This];
            state.CurrentRTVs.clear();
        }

        for (size_t i = 0; i < NumRenderTargetDescriptors; i++)
        {
            HeapInfo* heap = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE handle {};

            if (RTsSingleHandleToDescriptorRange)
            {
                heap = GetHeapByCpuHandleRTV(pRenderTargetDescriptors[0].ptr);
                if (heap == nullptr)
                {
                    LOG_DEBUG_ONLY("No heap!");
                    continue;
                }

                handle.ptr = pRenderTargetDescriptors[0].ptr + (i * heap->increment);
            }
            else
            {
                handle = pRenderTargetDescriptors[i];

                heap = GetHeapByCpuHandleRTV(handle.ptr);
                if (heap == nullptr)
                {
                    LOG_DEBUG_ONLY("No heap!");
                    continue;
                }
            }

            {
                std::lock_guard<std::mutex> lock(_cmdStateMutex);
                auto& state = g_CmdState[This];
                state.CurrentRTVs.push_back(pRenderTargetDescriptors[i]);
            }

            auto capturedBuffer = heap->GetByCpuHandle(handle.ptr);
            if (capturedBuffer == nullptr || capturedBuffer->buffer == nullptr)
            {
                LOG_DEBUG_ONLY("Miss index: {0}, cpu: {1}", i, handle.ptr);
                continue;
            }

            capturedBuffer->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
            capturedBuffer->captureInfo = CaptureInfo::OMSetRTV;

            if (Config::Instance()->FGImmediateCapture.value_or_default() &&
                Hudfix_Dx12::CheckForHudless(This, capturedBuffer, capturedBuffer->state))
            {
                break;
            }

            auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;
            size_t shardIdx = GetShardIndex(This);
            auto& shard = _hudlessShards[fIndex][shardIdx];

            {
                std::lock_guard<SpinLock> lock(shard.mutex);

                if (!shard.map.contains(This))
                {
                    ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> newMap;
                    newMap.reserve(32);
                    shard.map.insert_or_assign(This, newMap);
                }

                // add found resource
                LOG_TRACK("CmdList: {:X}, AddRef Resource: {:X}, Desc: {:X}, Format: {}", (size_t) This,
                          (size_t) capturedBuffer->buffer, handle.ptr, (UINT) capturedBuffer->format);

                shard.map[This].insert_or_assign(capturedBuffer->buffer, *capturedBuffer);
            }
        }
    }

    o_OMSetRenderTargets(This, NumRenderTargetDescriptors, pRenderTargetDescriptors, RTsSingleHandleToDescriptorRange,
                         pDepthStencilDescriptor);
}

#pragma endregion

#pragma region Compute paramter hooks

void ResTrack_Dx12::hkSetComputeRootDescriptorTable(ID3D12GraphicsCommandList* This, UINT RootParameterIndex,
                                                    D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    if (Config::Instance()->FGHudfixDisableSCR.value_or_default() || BaseDescriptor.ptr == 0 || !IsHudFixActive() ||
        Hudfix_Dx12::SkipHudlessChecks())
    {
        o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    LOG_DEBUG_ONLY("");

    if (This == MenuOverlayDx::MenuCommandList() /*|| IsFGCommandList(This)*/)
    {
        LOG_DEBUG_ONLY("Menu cmdlist: {} || fgCommandList: {}", This == MenuOverlayDx::MenuCommandList(),
                       IsFGCommandList(This));
        o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    auto heap = GetHeapByGpuHandleCR(BaseDescriptor.ptr);
    if (heap == nullptr)
    {
        LOG_DEBUG_ONLY("No heap!");
        o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    auto capturedBuffer = heap->GetByGpuHandle(BaseDescriptor.ptr);
    if (capturedBuffer == nullptr || capturedBuffer->buffer == nullptr)
    {
        LOG_DEBUG_ONLY("Miss RootParameterIndex: {1}, CommandList: {0:X}, gpuHandle: {2}", (SIZE_T) This,
                       RootParameterIndex, BaseDescriptor.ptr);
        o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    LOG_DEBUG_ONLY("CommandList: {:X}", (size_t) This);

    if (capturedBuffer->type == UAV)
        capturedBuffer->state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    else
        capturedBuffer->state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    capturedBuffer->captureInfo = CaptureInfo::SetCR;

    do
    {
        if (Config::Instance()->FGImmediateCapture.value_or_default() &&
            Hudfix_Dx12::CheckForHudless(This, capturedBuffer, capturedBuffer->state))
        {
            break;
        }

        auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;
        size_t shardIdx = GetShardIndex(This);
        auto& shard = _hudlessShards[fIndex][shardIdx];

        {
            std::lock_guard<SpinLock> lock(shard.mutex);

            if (!shard.map.contains(This))
            {
                ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> newMap;
                newMap.reserve(32);
                shard.map.insert_or_assign(This, newMap);
            }

            LOG_TRACK("CmdList: {:X}, AddRef Resource: {:X}, Desc: {:X}, Format: {}", (size_t) This,
                      (size_t) capturedBuffer->buffer, BaseDescriptor.ptr, (UINT) capturedBuffer->format);

            shard.map[This].insert_or_assign(capturedBuffer->buffer, *capturedBuffer);
        }

    } while (false);

    o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
}

#pragma endregion

#pragma region Shader finalizer hooks

bool ResTrack_Dx12::CopyResource(ID3D12GraphicsCommandList* cmdList, ResourceInfo* source, ID3D12Resource** target)
{
    auto result = true;

    ResourceBarrier(cmdList, source->buffer, source->state, D3D12_RESOURCE_STATE_COPY_SOURCE);

    if (CreateBufferResource(State::Instance().currentD3D12Device, source, D3D12_RESOURCE_STATE_COPY_DEST, target))
        cmdList->CopyResource(*target, source->buffer);
    else
        result = false;

    ResourceBarrier(cmdList, source->buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, source->state);

    return result;
}


// Capture if render target matches, wait for DrawIndexed
void ResTrack_Dx12::hkDrawInstanced(ID3D12GraphicsCommandList* This, UINT VertexCountPerInstance, UINT InstanceCount,
                                    UINT StartVertexLocation, UINT StartInstanceLocation)
{
    o_DrawInstanced(This, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);

    auto& state = g_CmdState[This];

    // IS THIS THE SHADER?
    if (_cyberprankRTShaderPSO == state.CurrentPSO)
    {

        // YES. GRAB THE RTVs.
        if (state.CurrentRTVs.size() == 2)
        {
            // 0 - sky and stuff
            // 1 - just some rays
            auto handle = state.CurrentRTVs[0];
            auto heap = GetHeapByCpuHandleRTV(handle.ptr);
            auto rtvResource = heap->GetByCpuHandle(handle.ptr);

            auto result = CopyResource(This, rtvResource, &State::Instance().fsrdRays);
        }
    }

    if (!IsHudFixActive())
    {
        LOG_TRACK("Skipping {:X}", (size_t) This);
        return;
    }

    LOG_TRACK("CmdList: {:X}", (size_t) This);

    auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;
    size_t shardIdx = GetShardIndex(This);
    auto& shard = _hudlessShards[fIndex][shardIdx];

    {
        std::lock_guard<SpinLock> lock(shard.mutex);

        if (This == MenuOverlayDx::MenuCommandList() && shard.map.contains(This))
        {
            shard.map[This].clear();
            return;
        }

        // if can't find output skip
        if (shard.map.size() == 0 || !shard.map.contains(This))
        {
            LOG_DEBUG_ONLY("Early exit");
            return;
        }

        auto val0 = shard.map[This];

        do
        {
            // if this command list does not have entries skip
            if (val0.size() == 0)
                break;

            if (Config::Instance()->FGHudfixDisableDI.value_or_default())
                break;

            for (auto& [key, val] : val0)
            {
                std::lock_guard<std::mutex> lock(_drawMutex);

                val.captureInfo |= CaptureInfo::DrawInstanced;

                if (Hudfix_Dx12::CheckForHudless(This, &val, val.state))
                    break;
            }

        } while (false);

        shard.map[This].clear();
    }
}

void ResTrack_Dx12::hkDrawIndexedInstanced(ID3D12GraphicsCommandList* This, UINT IndexCountPerInstance,
                                           UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation,
                                           UINT StartInstanceLocation)
{
    o_DrawIndexedInstanced(This, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation,
                           StartInstanceLocation);

    if (!IsHudFixActive())
    {
        LOG_TRACK("Skipping CmdList: {:X}", (size_t) This);
        return;
    }

    LOG_TRACK("CmdList: {:X}", (size_t) This);

    auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;
    size_t shardIdx = GetShardIndex(This);
    auto& shard = _hudlessShards[fIndex][shardIdx];

    {
        std::lock_guard<SpinLock> lock(shard.mutex);

        if (This == MenuOverlayDx::MenuCommandList() && shard.map.contains(This))
        {
            shard.map[This].clear();
            return;
        }

        // if can't find output skip
        if (shard.map.size() == 0 || !shard.map.contains(This))
        {
            LOG_DEBUG_ONLY("Early exit");
            return;
        }

        auto val0 = shard.map[This];

        do
        {
            // if this command list does not have entries skip
            if (val0.size() == 0)
                break;

            if (Config::Instance()->FGHudfixDisableDII.value_or_default())
                break;

            for (auto& [key, val] : val0)
            {
                // LOG_DEBUG("Waiting _drawMutex {:X}", (size_t)val.buffer);
                std::lock_guard<std::mutex> lock(_drawMutex);

                val.captureInfo |= CaptureInfo::DrawIndexedInstanced;

                if (Hudfix_Dx12::CheckForHudless(This, &val, val.state))
                    break;
            }

        } while (false);

        shard.map[This].clear();
    }
}

void ResTrack_Dx12::hkSetPipelineState(ID3D12GraphicsCommandList* This, ID3D12PipelineState* pPipelineState) 
{
    {
        std::lock_guard<std::mutex> lock(_cmdStateMutex);
        g_CmdState[This].CurrentPSO = pPipelineState;
    }

    o_SetPipelineState(This, pPipelineState);
}

void ResTrack_Dx12::hkExecuteBundle(ID3D12GraphicsCommandList* This, ID3D12GraphicsCommandList* pCommandList)
{
    LOG_WARN();

    IFGFeature_Dx12* fg = State::Instance().currentFG;
    auto index = fg != nullptr ? fg->GetIndex() : 0;

    {
        std::lock_guard<std::mutex> lock(_resourceCommandListMutex);

        if (fg != nullptr && fg->IsActive() && (_resourceCommandList[index].size() > 0 || !_resCmdList[index].empty()))
        {
            if (_notFoundCmdLists.contains(pCommandList))
                LOG_WARN("Found last frames cmdList: {:X}", (size_t) This);

            auto frameCmdList = _resourceCommandList[index];
            for (std::unordered_map<FG_ResourceType, ID3D12GraphicsCommandList*>::iterator it = frameCmdList.begin();
                 it != frameCmdList.end(); ++it)
            {
                if (it->second == pCommandList)
                    it->second = This;
            }

            for (std::unordered_map<FG_ResourceType, void*>::iterator it = _resCmdList[index].begin();
                 it != _resCmdList[index].end(); ++it)
            {
                if (it->second == pCommandList)
                    it->second = This;
            }
        }
    }

    o_ExecuteBundle(This, pCommandList);
}

HRESULT ResTrack_Dx12::hkClose(ID3D12GraphicsCommandList* This)
{
    auto fg = State::Instance().currentFG;
    auto index = fg != nullptr ? fg->GetIndex() : 0;

    if (fg != nullptr && fg->IsActive() && !fg->IsPaused() && _resourceCommandList[index].size() > 0)
    {
        LOG_TRACK("CmdList: {:X}", (size_t) This);

        std::lock_guard<std::mutex> lock(_resourceCommandListMutex);

        if (_notFoundCmdLists.contains(This))
            LOG_WARN("Found last frames cmdList: {:X}", (size_t) This);

        std::vector<FG_ResourceType> found;

        for (const auto& pair : _resourceCommandList[index])
        {
            if (This == pair.second)
            {
                if (!fg->IsResourceReady(pair.first))
                {
                    LOG_DEBUG("{} cmdList: {:X}", (UINT) pair.first, (size_t) This);
                    _resCmdList[index][pair.first] = pair.second;
                    found.push_back(pair.first);
                }
            }
        }

        for (size_t i = 0; i < found.size(); i++)
        {
            _resourceCommandList[index].erase(found[i]);
        }
    }

    return o_Close(This);
}

void ResTrack_Dx12::hkDispatch(ID3D12GraphicsCommandList* This, UINT ThreadGroupCountX, UINT ThreadGroupCountY,
                               UINT ThreadGroupCountZ)
{
    o_Dispatch(This, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);

    if (!IsHudFixActive())
    {
        LOG_TRACK("Skipping {:X}", (size_t) This);
        return;
    }

    LOG_TRACK("CmdList: {:X}", (size_t) This);

    auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;
    size_t shardIdx = GetShardIndex(This);
    auto& shard = _hudlessShards[fIndex][shardIdx];

    {
        std::lock_guard<SpinLock> lock(shard.mutex);

        if (This == MenuOverlayDx::MenuCommandList() && shard.map.contains(This))
        {
            shard.map[This].clear();
            return;
        }

        // if can't find output skip
        if (shard.map.size() == 0 || !shard.map.contains(This))
        {
            LOG_DEBUG_ONLY("Early exit");
            return;
        }

        auto val0 = shard.map[This];

        do
        {
            // if this command list does not have entries skip
            if (val0.size() == 0)
                break;

            if (Config::Instance()->FGHudfixDisableDispatch.value_or_default())
                break;

            for (auto& [key, val] : val0)
            {
                // LOG_DEBUG("Waiting _drawMutex {:X}", (size_t)val.buffer);
                std::lock_guard<std::mutex> lock(_drawMutex);

                val.captureInfo |= CaptureInfo::Dispatch;
                if (Hudfix_Dx12::CheckForHudless(This, &val, val.state))
                {
                    break;
                }
            }
        } while (false);

        shard.map[This].clear();
    }
}

#pragma endregion

void ResTrack_Dx12::HookResource(ID3D12Device* InDevice)
{
    if (o_Release != nullptr)
        return;

    ID3D12Resource* tmp = nullptr;
    auto d = CD3DX12_RESOURCE_DESC::Buffer(4);
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    HRESULT hr = InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &d,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&tmp));

    if (hr == S_OK)
    {
        PVOID* pVTable = *(PVOID**) tmp;
        o_Release = (PFN_Release) pVTable[2];

        if (o_Release != nullptr)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&) o_Release, hkRelease);
            DetourTransactionCommit();

            o_Release(tmp); // drop temp
        }
        else
        {
            tmp->Release();
        }
    }
}

void ResTrack_Dx12::HookCommandList(ID3D12Device* InDevice)
{

    if (o_OMSetRenderTargets != nullptr)
        return;

    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12CommandAllocator* commandAllocator = nullptr;

    if (InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)) == S_OK)
    {
        if (InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr,
                                        IID_PPV_ARGS(&commandList)) == S_OK)
        {
            ID3D12GraphicsCommandList* realCL = nullptr;
            if (!CheckForRealObject(__FUNCTION__, commandList, (IUnknown**) &realCL))
                realCL = commandList;

            // Get the vtable pointer
            PVOID* pVTable = *(PVOID**) realCL;

            // hudless shader
            o_OMSetRenderTargets = (PFN_OMSetRenderTargets) pVTable[46];
            o_SetGraphicsRootDescriptorTable = (PFN_SetGraphicsRootDescriptorTable) pVTable[32];

            o_DrawInstanced = (PFN_DrawInstanced) pVTable[12];
            o_DrawIndexedInstanced = (PFN_DrawIndexedInstanced) pVTable[13];
            o_Dispatch = (PFN_Dispatch) pVTable[14];
            o_Close = (PFN_Close) pVTable[9];

            // hudless compute
            o_SetComputeRootDescriptorTable = (PFN_SetComputeRootDescriptorTable) pVTable[31];

            o_ExecuteBundle = (PFN_ExecuteBundle) pVTable[27];

            o_SetPipelineState = (PFN_SetPipelineState) pVTable[25];

            if (o_OMSetRenderTargets != nullptr)
            {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                bool cyberprankDenoiser = true;

                // Only needed for hudfix
                if (cyberprankDenoiser || State::Instance().activeFgInput == FGInput::Upscaler)
                {
                    if (o_OMSetRenderTargets != nullptr)
                        DetourAttach(&(PVOID&) o_OMSetRenderTargets, hkOMSetRenderTargets);

                    if (o_SetGraphicsRootDescriptorTable != nullptr)
                        DetourAttach(&(PVOID&) o_SetGraphicsRootDescriptorTable, hkSetGraphicsRootDescriptorTable);

                    if (o_SetComputeRootDescriptorTable != nullptr)
                        DetourAttach(&(PVOID&) o_SetComputeRootDescriptorTable, hkSetComputeRootDescriptorTable);

                    if (o_DrawIndexedInstanced != nullptr)
                        DetourAttach(&(PVOID&) o_DrawIndexedInstanced, hkDrawIndexedInstanced);

                    if (o_DrawInstanced != nullptr)
                        DetourAttach(&(PVOID&) o_DrawInstanced, hkDrawInstanced);

                    if (o_Dispatch != nullptr)
                        DetourAttach(&(PVOID&) o_Dispatch, hkDispatch);
                }

                if (cyberprankDenoiser && o_SetPipelineState != nullptr)
                    DetourAttach(&(PVOID&) o_SetPipelineState, hkSetPipelineState);

                if (o_Close != nullptr)
                    DetourAttach(&(PVOID&) o_Close, hkClose);

                if (o_ExecuteBundle != nullptr)
                    DetourAttach(&(PVOID&) o_ExecuteBundle, hkExecuteBundle);

                DetourTransactionCommit();
            }

            commandList->Close();
            commandList->Release();
        }

        commandAllocator->Reset();
        commandAllocator->Release();
    }
}

void ResTrack_Dx12::HookToQueue(ID3D12Device* InDevice)
{
    if (o_ExecuteCommandLists != nullptr)
        return;

    ID3D12CommandQueue* queue = nullptr;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

    auto hr = InDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue));

    if (hr == S_OK)
    {
        ID3D12CommandQueue* realQueue = nullptr;
        if (!CheckForRealObject(__FUNCTION__, queue, (IUnknown**) &realQueue))
            realQueue = queue;

        // Get the vtable pointer
        PVOID* pVTable = *(PVOID**) realQueue;

        o_ExecuteCommandLists = (PFN_ExecuteCommandLists) pVTable[10];

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_ExecuteCommandLists != nullptr)
            DetourAttach(&(PVOID&) o_ExecuteCommandLists, hkExecuteCommandLists);

        DetourTransactionCommit();

        queue->Release();
    }
}

void ResTrack_Dx12::HookDevice(ID3D12Device* device)
{
    if (o_CreateDescriptorHeap != nullptr || State::Instance().activeFgInput == FGInput::Nukems)
        return;

    if (device == nullptr)
        return;

    _trackedResources.reserve(1024);

    LOG_FUNC();

    ID3D12Device* realDevice = nullptr;
    if (!CheckForRealObject(__FUNCTION__, device, (IUnknown**) &realDevice))
        realDevice = device;

    // Get the vtable pointer
    PVOID* pVTable = *(PVOID**) realDevice;

    // Hudfix
    o_CreateDescriptorHeap = (PFN_CreateDescriptorHeap) pVTable[14];
    o_CreateConstantBufferView = (PFN_CreateConstantBufferView) pVTable[17];
    o_CreateShaderResourceView = (PFN_CreateShaderResourceView) pVTable[18];
    o_CreateUnorderedAccessView = (PFN_CreateUnorderedAccessView) pVTable[19];
    o_CreateRenderTargetView = (PFN_CreateRenderTargetView) pVTable[20];
    o_CreateDepthStencilView = (PFN_CreateDepthStencilView) pVTable[21];
    o_CreateSampler = (PFN_CreateSampler) pVTable[22];
    o_CopyDescriptors = (PFN_CopyDescriptors) pVTable[23];
    o_CopyDescriptorsSimple = (PFN_CopyDescriptorsSimple) pVTable[24];

    // Denoiser
    o_CreateGraphicsPipelineState = (PFN_CreateGraphicsPipelineState) pVTable[10];

    ID3D12Device1* device1 = nullptr;
    HRESULT hr = realDevice->QueryInterface(IID_PPV_ARGS(&device1));
    if (SUCCEEDED(hr))
    {
        ID3D12PipelineLibrary* library = nullptr;
        auto result = device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&library));

        if (SUCCEEDED(result))
        {
            PVOID* pVTableLibrary = *(PVOID**) library;
            o_LoadGraphicsPipeline = (PFN_LoadGraphicsPipeline) pVTableLibrary[9];

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (o_LoadGraphicsPipeline != nullptr)
                DetourAttach(&(PVOID&) o_LoadGraphicsPipeline, hkLoadGraphicsPipeline);

            DetourTransactionCommit();

            library->Release();
        }

        device1->Release();
    }

    auto o_CreatePipelineLibrary = pVTable[47];

    // Apply the detour
    // Only needed for Hudfix
    bool cyberprankAmdDenoiser = true;
    bool optiFg = State::Instance().activeFgInput == FGInput::Upscaler;
    if (o_CreateDescriptorHeap != nullptr && (optiFg || cyberprankAmdDenoiser))
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_CreateDescriptorHeap != nullptr)
            DetourAttach(&(PVOID&) o_CreateDescriptorHeap, hkCreateDescriptorHeap);

        if (o_CreateRenderTargetView != nullptr)
            DetourAttach(&(PVOID&) o_CreateRenderTargetView, hkCreateRenderTargetView);

        if (o_CreateShaderResourceView != nullptr)
            DetourAttach(&(PVOID&) o_CreateShaderResourceView, hkCreateShaderResourceView);

        if (o_CreateUnorderedAccessView != nullptr)
            DetourAttach(&(PVOID&) o_CreateUnorderedAccessView, hkCreateUnorderedAccessView);

        if (optiFg && o_CopyDescriptors != nullptr)
            DetourAttach(&(PVOID&) o_CopyDescriptors, hkCopyDescriptors);

        if (optiFg && o_CopyDescriptorsSimple != nullptr)
            DetourAttach(&(PVOID&) o_CopyDescriptorsSimple, hkCopyDescriptorsSimple);

        if (cyberprankAmdDenoiser && o_CreateGraphicsPipelineState != nullptr)
            DetourAttach(&(PVOID&) o_CreateGraphicsPipelineState, hkCreateGraphicsPipelineState);

        DetourTransactionCommit();
    }

    // Only needed for FSR-FG Feature
    // if (State::Instance().activeFgOutput == FGOutput::FSRFG || State::Instance().activeFgInput == FGInput::Upscaler)
    //    HookToQueue(device);

    // if (State::Instance().activeFgOutput == FGOutput::FSRFG || State::Instance().activeFgInput == FGInput::Upscaler)
    //     HookCommandList(device);

    // Only needed for Hudfix
    if (State::Instance().activeFgInput == FGInput::Upscaler || cyberprankAmdDenoiser)
    {
        if (!cyberprankAmdDenoiser)
        {
            HookResource(device);
            HookToQueue(device);
        }
        
        HookCommandList(device);
    }
}

void ResTrack_Dx12::ReleaseHooks()
{
    LOG_DEBUG("");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    // if (o_CreateDescriptorHeap != nullptr)
    //     DetourDetach(&(PVOID&) o_CreateDescriptorHeap, hkCreateDescriptorHeap);

    // if (o_CreateRenderTargetView != nullptr)
    //     DetourDetach(&(PVOID&) o_CreateRenderTargetView, hkCreateRenderTargetView);

    // if (o_CreateShaderResourceView != nullptr)
    //     DetourDetach(&(PVOID&) o_CreateShaderResourceView, hkCreateShaderResourceView);

    // if (o_CreateUnorderedAccessView != nullptr)
    //     DetourDetach(&(PVOID&) o_CreateUnorderedAccessView, hkCreateUnorderedAccessView);

    // if (o_CopyDescriptors != nullptr)
    //     DetourDetach(&(PVOID&) o_CopyDescriptors, hkCopyDescriptors);

    // if (o_CopyDescriptorsSimple != nullptr)
    //     DetourDetach(&(PVOID&) o_CopyDescriptorsSimple, hkCopyDescriptorsSimple);

    // o_CreateDescriptorHeap = nullptr;
    // o_CreateRenderTargetView = nullptr;
    // o_CreateShaderResourceView = nullptr;
    // o_CreateUnorderedAccessView = nullptr;
    // o_CopyDescriptors = nullptr;
    // o_CopyDescriptorsSimple = nullptr;

    // if (o_ExecuteCommandLists != nullptr)
    //     DetourAttach(&(PVOID&) o_ExecuteCommandLists, hkExecuteCommandLists);

    // o_ExecuteCommandLists = nullptr;

    // if (o_Release != nullptr)
    //     DetourAttach(&(PVOID&) o_Release, hkRelease);

    // o_Release = nullptr;

    if (o_OMSetRenderTargets != nullptr)
        DetourDetach(&(PVOID&) o_OMSetRenderTargets, hkOMSetRenderTargets);

    if (o_SetGraphicsRootDescriptorTable != nullptr)
        DetourDetach(&(PVOID&) o_SetGraphicsRootDescriptorTable, hkSetGraphicsRootDescriptorTable);

    if (o_SetComputeRootDescriptorTable != nullptr)
        DetourDetach(&(PVOID&) o_SetComputeRootDescriptorTable, hkSetComputeRootDescriptorTable);

    if (o_DrawIndexedInstanced != nullptr)
        DetourDetach(&(PVOID&) o_DrawIndexedInstanced, hkDrawIndexedInstanced);

    if (o_DrawInstanced != nullptr)
        DetourDetach(&(PVOID&) o_DrawInstanced, hkDrawInstanced);

    if (o_Dispatch != nullptr)
        DetourDetach(&(PVOID&) o_Dispatch, hkDispatch);

    if (o_Close != nullptr)
        DetourDetach(&(PVOID&) o_Close, hkClose);

    if (o_ExecuteBundle != nullptr)
        DetourDetach(&(PVOID&) o_ExecuteBundle, hkExecuteBundle);

    o_OMSetRenderTargets = nullptr;
    o_SetGraphicsRootDescriptorTable = nullptr;
    o_SetComputeRootDescriptorTable = nullptr;
    o_DrawIndexedInstanced = nullptr;
    o_DrawInstanced = nullptr;
    o_Dispatch = nullptr;
    o_Close = nullptr;
    o_ExecuteBundle = nullptr;

    DetourTransactionCommit();
}

void ResTrack_Dx12::ClearPossibleHudless()
{
    LOG_DEBUG("");

    auto hfIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

    for (size_t i = 0; i < SHARD_COUNT; i++)
    {
        auto& shard = _hudlessShards[hfIndex][i];
        std::lock_guard<SpinLock> lock(shard.mutex);
        shard.map.clear();
    }

    std::lock_guard<std::mutex> lock2(_resourceCommandListMutex);

    auto fg = State::Instance().currentFG;
    if (fg != nullptr)
    {
        auto fIndex = fg->GetIndex();

        if (_notFoundCmdLists.size() > 10)
            _notFoundCmdLists.clear();

        for (const auto& pair : _resourceCommandList[fIndex])
        {
            LOG_WARN("{} cmdList: {:X}, not closed!", (UINT) pair.first, (size_t) pair.second);
            _notFoundCmdLists.insert(pair.second);
        }

        _resourceCommandList[fIndex].clear();

        for (const auto& pair : _resCmdList[fIndex])
        {
            LOG_WARN("{} cmdList: {:X}, not executed!", (UINT) pair.first, (size_t) pair.second);
            _notFoundCmdLists.insert(pair.second);
        }

        _resCmdList[fIndex].clear();
    }
}

void ResTrack_Dx12::SetResourceCmdList(FG_ResourceType type, ID3D12GraphicsCommandList* cmdList)
{
    auto fg = State::Instance().currentFG;
    if (fg != nullptr && fg->IsActive())
    {
        auto index = fg->GetIndex();

        ID3D12GraphicsCommandList* realCmdList = nullptr;
        if (!CheckForRealObject(__FUNCTION__, cmdList, (IUnknown**) &realCmdList))
            realCmdList = cmdList;

        _resourceCommandList[index][type] = realCmdList;
        LOG_DEBUG("_resourceCommandList[{}][{}]: {:X}", index, magic_enum::enum_name(type), (size_t) realCmdList);
    }
}
