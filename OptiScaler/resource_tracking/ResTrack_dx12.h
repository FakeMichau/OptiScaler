#pragma once

#include <pch.h>

#include <hudfix/Hudfix_Dx12.h>
#include <framegen/IFGFeature_Dx12.h>

#include <ankerl/unordered_dense.h>

#include <new>
#include <mutex>
#include <atomic>
#include <shared_mutex>

// #define DEBUG_TRACKING

#ifdef DEBUG_TRACKING
static void TestResource(ResourceInfo* info)
{
    if (info == nullptr || info->buffer == nullptr)
        return;

    auto desc = info->buffer->GetDesc();

    if (desc.Width != info->width || desc.Height != info->height || desc.Format != info->format)
    {
        LOG_TRACK("Resource mismatch: {:X}, info: {:X}", (size_t) info->buffer, (size_t) info);

        // LOG_WARN("Resource mismatch: {:X}, info: {:X}", (size_t) info->buffer, (size_t) info);
        //__debugbreak();
    }
}
#endif

#ifdef __cpp_lib_hardware_interference_size
constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
constexpr size_t CACHE_LINE_SIZE = 64;
#endif

struct SpinLock
{
    std::atomic<bool> _lock = { false };

    __forceinline void lock()
    {
        // Fast path: try to grab immediately
        if (!_lock.exchange(true, std::memory_order_acquire))
            return;

        // Slow path: spin
        while (true)
        {
            // Spin read (avoids cache coherency traffic while waiting)
            while (_lock.load(std::memory_order_relaxed))
            {
                _mm_pause(); // CPU hint: "I am spinning"
            }

            // Try to grab again
            if (!_lock.exchange(true, std::memory_order_acquire))
                return;
        }
    }

    __forceinline void unlock() { _lock.store(false, std::memory_order_release); }
};

static ankerl::unordered_dense::map<ID3D12Resource*, std::vector<ResourceInfo*>> _trackedResources;
static SpinLock _trackedResourcesMutex;

struct HeapInfo
{
    // mutable std::shared_mutex mutex;

    ID3D12DescriptorHeap* heap = nullptr;
    SIZE_T cpuStart = NULL;
    SIZE_T cpuEnd = NULL;
    SIZE_T gpuStart = NULL;
    SIZE_T gpuEnd = NULL;
    UINT numDescriptors = 0;
    UINT increment = 0;
    UINT type = 0;
    std::shared_ptr<ResourceInfo[]> info;
    UINT lastOffset = 0;
    bool active = true;

    HeapInfo(ID3D12DescriptorHeap* heap, SIZE_T cpuStart, SIZE_T cpuEnd, SIZE_T gpuStart, SIZE_T gpuEnd,
             UINT numResources, UINT increment, UINT type, UINT mutexIndex)
        : cpuStart(cpuStart), cpuEnd(cpuEnd), gpuStart(gpuStart), gpuEnd(gpuEnd), numDescriptors(numResources),
          increment(increment), info(new ResourceInfo[numResources]), type(type), heap(heap)
    {
        for (size_t i = 0; i < numDescriptors; i++)
        {
            info[i].buffer = nullptr;
        }
    }

    void DetachFromOldResource(SIZE_T index) const
    {
        if (info[index].buffer == nullptr)
            return;

        std::scoped_lock lock(_trackedResourcesMutex);
        LOG_TRACK("Heap: {:X}, Index: {}, Resource: {:X}, Res: {}x{}, Format: {}", (size_t) this, index,
                  (size_t) info[index].buffer, info[index].width, info[index].height, (UINT) info[index].format);
        auto it = _trackedResources.find(info[index].buffer);
        if (it != _trackedResources.end())
        {
            auto& vec = it->second;
            vec.erase(std::remove(vec.begin(), vec.end(), &info[index]), vec.end());
            if (vec.empty())
                _trackedResources.erase(it);
        }
    }

    void AttachToNewResource(SIZE_T index) const
    {
        std::scoped_lock lock(_trackedResourcesMutex);
        LOG_TRACK("Heap: {:X}, Index: {}, Resource: {:X}, Res: {}x{}, Format: {}", (size_t) this, index,
                  (size_t) info[index].buffer, info[index].width, info[index].height, (UINT) info[index].format);
        auto& vec = _trackedResources[info[index].buffer];
        if (std::find(vec.begin(), vec.end(), &info[index]) == vec.end())
            vec.push_back(&info[index]);
    }

    ResourceInfo* GetByCpuHandle(SIZE_T cpuHandle) const
    {
        auto index = (cpuHandle - cpuStart) / increment;

        if (index >= numDescriptors)
            return nullptr;

        // std::shared_lock<std::shared_mutex> lock(mutex);

        if (info[index].buffer == nullptr)
            return nullptr;

#ifdef DEBUG_TRACKING
        TestResource(&info[index]);
#endif

        return &info[index];
    }

    ResourceInfo* GetByGpuHandle(SIZE_T gpuHandle) const
    {
        auto index = (gpuHandle - gpuStart) / increment;

        if (index >= numDescriptors)
            return nullptr;

        // std::shared_lock<std::shared_mutex> lock(mutex);

        if (info[index].buffer == nullptr)
            return nullptr;

#ifdef DEBUG_TRACKING
        TestResource(&info[index]);
#endif

        return &info[index];
    }

    void SetByCpuHandle(SIZE_T cpuHandle, ResourceInfo setInfo) const
    {
        auto index = (cpuHandle - cpuStart) / increment;

        if (index >= numDescriptors)
            return;

        // std::unique_lock<std::shared_mutex> lock(mutex);

#ifdef DEBUG_TRACKING
        TestResource(&setInfo);
#endif
        if (info[index].buffer != setInfo.buffer)
        {
            DetachFromOldResource(index);
            info[index] = setInfo;
            AttachToNewResource(index);
        }
    }

    void SetByGpuHandle(SIZE_T gpuHandle, ResourceInfo setInfo) const
    {
        auto index = (gpuHandle - gpuStart) / increment;

        if (index >= numDescriptors)
            return;

        // std::unique_lock<std::shared_mutex> lock(mutex);

#ifdef DEBUG_TRACKING
        TestResource(&setInfo);
#endif

        if (info[index].buffer != setInfo.buffer)
        {
            DetachFromOldResource(index);
            info[index] = setInfo;
            AttachToNewResource(index);
        }
    }

    void ClearByCpuHandle(SIZE_T cpuHandle) const
    {
        auto index = (cpuHandle - cpuStart) / increment;

        if (index >= numDescriptors)
            return;

        // std::unique_lock<std::shared_mutex> lock(mutex);

        if (info[index].buffer != nullptr)
        {
            LOG_TRACK("Resource: {:X}, Res: {}x{}, Format: {}", (size_t) info[index].buffer, info[index].width,
                      info[index].height, (UINT) info[index].format);

            DetachFromOldResource(index);
        }

        info[index].buffer = nullptr;
        info[index].lastUsedFrame = 0;
    }

    void ClearByGpuHandle(SIZE_T gpuHandle) const
    {
        auto index = (gpuHandle - gpuStart) / increment;

        if (index >= numDescriptors)
            return;

        // std::unique_lock<std::shared_mutex> lock(mutex);

        if (info[index].buffer != nullptr)
        {
            LOG_TRACK("Resource: {:X}, Res: {}x{}, Format: {}", (size_t) info[index].buffer, info[index].width,
                      info[index].height, (UINT) info[index].format);

            DetachFromOldResource(index);
        }

        info[index].buffer = nullptr;
        info[index].lastUsedFrame = 0;
    }
};

struct ResourceHeapInfo
{
    SIZE_T cpuStart = NULL;
    SIZE_T gpuStart = NULL;
};

// Force each struct to start on a new cache line
struct alignas(CACHE_LINE_SIZE) CommandListShard
{
    SpinLock mutex;
    ankerl::unordered_dense::map<ID3D12GraphicsCommandList*,
                                 ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo>>
        map;

    char padding[CACHE_LINE_SIZE - (sizeof(SpinLock) + sizeof(void*) % CACHE_LINE_SIZE)] = {};
};

class ResTrack_Dx12
{
  private:
    inline static bool _presentDone = true;
    inline static std::mutex _drawMutex;

    inline static std::mutex _resourceCommandListMutex;
    inline static std::mutex _cmdStateMutex;
    inline static std::unordered_map<FG_ResourceType, ID3D12GraphicsCommandList*> _resourceCommandList[BUFFER_COUNT];

    inline static ULONG64 _lastHudlessFrame = 0;
    inline static std::mutex _hudlessMutex;
    inline static void* _hudlessMutexQueue = nullptr;

    struct CmdListState
    {
        ID3D12PipelineState* CurrentPSO = nullptr;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> CurrentRTVs;
    };
    inline static std::unordered_map<ID3D12GraphicsCommandList*, CmdListState> g_CmdState;
    inline static ID3D12PipelineState* _cyberprankRTShaderPSO = nullptr;

    static bool IsHudFixActive();

    // static bool IsFGCommandList(IUnknown* cmdList);

    static void hkCopyDescriptors(ID3D12Device* This, UINT NumDestDescriptorRanges,
                                  D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts,
                                  UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges,
                                  D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts,
                                  UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);
    static void hkCopyDescriptorsSimple(ID3D12Device* This, UINT NumDescriptors,
                                        D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
                                        D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
                                        D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);

    static bool isCyberprankShader(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, void** ppPipelineState);

    static HRESULT hkCreateGraphicsPipelineState(ID3D12Device* This, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
                                                 REFIID riid, void** ppPipelineState);

    static HRESULT hkLoadGraphicsPipeline(ID3D12PipelineLibrary1* This, LPCWSTR pName,
                                   const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid,
                                          void** ppPipelineState);

    static HRESULT hkLoadPipeline(ID3D12PipelineLibrary1* This, LPCWSTR pName,
                                 const D3D12_PIPELINE_STATE_STREAM_DESC* pDesc,
                           REFIID riid, void** ppPipelineState);

    static void hkSetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* This, UINT RootParameterIndex,
                                                 D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);
    static void hkOMSetRenderTargets(ID3D12GraphicsCommandList* This, UINT NumRenderTargetDescriptors,
                                     D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors,
                                     BOOL RTsSingleHandleToDescriptorRange,
                                     D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor);
    static void hkSetComputeRootDescriptorTable(ID3D12GraphicsCommandList* This, UINT RootParameterIndex,
                                                D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);

    static void hkDrawInstanced(ID3D12GraphicsCommandList* This, UINT VertexCountPerInstance, UINT InstanceCount,
                                UINT StartVertexLocation, UINT StartInstanceLocation);
    static void hkDrawIndexedInstanced(ID3D12GraphicsCommandList* This, UINT IndexCountPerInstance, UINT InstanceCount,
                                       UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);
    static void hkSetPipelineState(ID3D12GraphicsCommandList* This, ID3D12PipelineState* pPipelineState);
    static void hkDispatch(ID3D12GraphicsCommandList* This, UINT ThreadGroupCountX, UINT ThreadGroupCountY,
                           UINT ThreadGroupCountZ);

    static void hkExecuteBundle(ID3D12GraphicsCommandList* This, ID3D12GraphicsCommandList* pCommandList);

    static HRESULT hkClose(ID3D12GraphicsCommandList* This);

    static void hkCreateRenderTargetView(ID3D12Device* This, ID3D12Resource* pResource,
                                         D3D12_RENDER_TARGET_VIEW_DESC* pDesc,
                                         D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
    static void hkCreateShaderResourceView(ID3D12Device* This, ID3D12Resource* pResource,
                                           D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc,
                                           D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
    static void hkCreateUnorderedAccessView(ID3D12Device* This, ID3D12Resource* pResource,
                                            ID3D12Resource* pCounterResource, D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc,
                                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

    static void hkExecuteCommandLists(ID3D12CommandQueue* This, UINT NumCommandLists,
                                      ID3D12CommandList* const* ppCommandLists);

    static HRESULT hkCreateDescriptorHeap(ID3D12Device* This, D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc,
                                          REFIID riid, void** ppvHeap);

    static ULONG hkRelease(ID3D12Resource* This);

    static void HookCommandList(ID3D12Device* InDevice);
    static void HookToQueue(ID3D12Device* InDevice);
    static void HookResource(ID3D12Device* InDevice);

    static bool CheckResource(ID3D12Resource* resource);

    static bool CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject);

    static bool CreateBufferResource(ID3D12Device* InDevice, ResourceInfo* InSource, D3D12_RESOURCE_STATES InState,
                                     ID3D12Resource** OutResource);

    static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

    static bool CopyResource(ID3D12GraphicsCommandList* cmdList, ResourceInfo* source, ID3D12Resource** target);

    static SIZE_T GetGPUHandle(ID3D12Device* This, SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type);
    static SIZE_T GetCPUHandle(ID3D12Device* This, SIZE_T gpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type);

    static HeapInfo* GetHeapByCpuHandleCBV(SIZE_T cpuHandle);
    static HeapInfo* GetHeapByCpuHandleRTV(SIZE_T cpuHandle);
    static HeapInfo* GetHeapByCpuHandleSRV(SIZE_T cpuHandle);
    static HeapInfo* GetHeapByCpuHandleUAV(SIZE_T cpuHandle);
    static HeapInfo* GetHeapByCpuHandle(SIZE_T cpuHandle);
    static HeapInfo* GetHeapByGpuHandleGR(SIZE_T gpuHandle);
    static HeapInfo* GetHeapByGpuHandleCR(SIZE_T gpuHandle);

    static void FillResourceInfo(ID3D12Resource* resource, ResourceInfo* info);

    // Sharding
    inline static constexpr size_t SHARD_COUNT = 16;

    inline static CommandListShard _hudlessShards[BUFFER_COUNT][SHARD_COUNT];

    // Shifts bits to ignore alignment (pointers are usually 8-byte aligned)
    // and maps the pointer address to 0..63
    static inline size_t GetShardIndex(ID3D12GraphicsCommandList* ptr)
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        return (addr >> 4) % SHARD_COUNT;
    }

  public:
    static void HookDevice(ID3D12Device* device);
    static void ReleaseHooks();
    static void ClearPossibleHudless();
    static void SetResourceCmdList(FG_ResourceType type, ID3D12GraphicsCommandList* cmdList);
};
