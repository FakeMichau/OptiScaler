#include "FSRFG_Dx12.h"

#include <State.h>

#include <upscalers/IFeature.h>
#include <menu/menu_overlay_dx.h>
#include <future>

// #define USE_QUEUE_FOR_FG

typedef struct FfxSwapchainFramePacingTuning
{
    float safetyMarginInMs;  // in Millisecond. Default is 0.1ms
    float varianceFactor;    // valid range [0.0,1.0]. Default is 0.1
    bool allowHybridSpin;    // Allows pacing spinlock to sleep. Default is false.
    uint32_t hybridSpinTime; // How long to spin if allowHybridSpin is true. Measured in timer resolution units. Not
                             // recommended to go below 2. Will result in frequent overshoots. Default is 2.
    bool allowWaitForSingleObjectOnFence; // Allows WaitForSingleObject instead of spinning for fence value. Default is
                                          // false.
} FfxSwapchainFramePacingTuning;

void FSRFG_Dx12::ConfigureFramePaceTuning()
{
    State::Instance().FSRFGFTPchanged = false;

    if (_swapChainContext == nullptr || Version() < feature_version { 3, 1, 3 })
        return;

    FfxSwapchainFramePacingTuning fpt {};
    if (Config::Instance()->FGFramePacingTuning.value_or_default())
    {
        fpt.allowHybridSpin = Config::Instance()->FGFPTAllowHybridSpin.value_or_default();
        fpt.allowWaitForSingleObjectOnFence =
            Config::Instance()->FGFPTAllowWaitForSingleObjectOnFence.value_or_default();
        fpt.hybridSpinTime = Config::Instance()->FGFPTHybridSpinTime.value_or_default();
        fpt.safetyMarginInMs = Config::Instance()->FGFPTSafetyMarginInMs.value_or_default();
        fpt.varianceFactor = Config::Instance()->FGFPTVarianceFactor.value_or_default();

        ffxConfigureDescFrameGenerationSwapChainKeyValueDX12 cfgDesc {};
        cfgDesc.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_KEYVALUE_DX12;
        cfgDesc.key = 2; // FfxSwapchainFramePacingTuning
        cfgDesc.ptr = &fpt;

        auto result = FfxApiProxy::D3D12_Configure()(&_swapChainContext, &cfgDesc.header);
        LOG_DEBUG("HybridSpin D3D12_Configure result: {}", FfxApiProxy::ReturnCodeToString(result));
    }
}

void FSRFG_Dx12::GetDispatchCommandList()
{
    ffxQueryDescFrameGenerationSwapChainInterpolationCommandListDX12 queryDesc { 0 };
    queryDesc.header.type = FFX_API_QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_INTERPOLATIONCOMMANDLIST_DX12;
    queryDesc.pOutCommandList = (void**) &_dispatchCommandList;

    auto result = FfxApiProxy::D3D12_Query()(&_swapChainContext, &queryDesc.header);
    LOG_DEBUG("FG CommandList D3D12_Query result: {}", FfxApiProxy::ReturnCodeToString(result));
}

UINT64 FSRFG_Dx12::UpscaleStart()
{
    LOG_FUNC();

    _frameCount++;

    // if (!State::Instance().isShuttingDown && IsActive())
    //{
    //     auto frameIndex = GetIndex();

    //    if (Config::Instance()->FGHUDFix.value_or_default())
    //    {
    //        auto allocator = _commandAllocators[frameIndex];
    //        auto result = allocator->Reset();
    //        LOG_DEBUG("_commandAllocators[{}]->Reset(): {:X}", frameIndex, result);
    //        result = _commandList[frameIndex]->Reset(allocator, nullptr);
    //        LOG_DEBUG("_commandList[{}]->Reset(): {:X}", frameIndex, result);
    //    }
    //}

    return _frameCount;
}

void FSRFG_Dx12::UpscaleEnd() { LOG_DEBUG(""); }

feature_version FSRFG_Dx12::Version()
{
    if (FfxApiProxy::InitFfxDx12())
    {
        auto ver = FfxApiProxy::VersionDx12();
        return ver;
    }

    return { 0, 0, 0 };
}

const char* FSRFG_Dx12::Name() { return "FSR-FG"; }

// bool FSRFG_Dx12::Dispatch(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* output, double frameTime)
//{
//     LOG_DEBUG("(FG) running, frame: {0}", _frameCount);
//
//     _lastDispatchedFrame = _frameCount;
//
//     if (State::Instance().FSRFGFTPchanged)
//         ConfigureFramePaceTuning();
//
//     auto frameIndex = _frameCount % BUFFER_COUNT;
//
//     if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
//     {
//         LOG_TRACE("Waiting Mutex 1, current: {}", Mutex.getOwner());
//         Mutex.lock(1);
//         LOG_TRACE("Accuired Mutex: {}", Mutex.getOwner());
//     }
//
//     // Update frame generation config
//     ffxConfigureDescFrameGeneration m_FrameGenerationConfig = {};
//
//     DXGI_SWAP_CHAIN_DESC scDesc {};
//     _swapChain->GetDesc(&scDesc);
//     auto desc = output->GetDesc();
//     if (Config::Instance()->FGHUDFixExtended.value_or_default() && desc.Format == scDesc.BufferDesc.Format)
//     {
//         if (desc.Width == scDesc.BufferDesc.Width && desc.Height == scDesc.BufferDesc.Height)
//         {
//             if (CreateBufferResource(State::Instance().currentD3D12Device, output, D3D12_RESOURCE_STATE_COPY_DEST,
//                                      &_paramHudless[frameIndex], true, false))
//             {
//                 LOG_DEBUG("(FG) desc.Format == HooksDx::swapchainFormat, using for hudless!");
//                 cmdList->CopyResource(_paramHudless[frameIndex], output);
//
//                 m_FrameGenerationConfig.HUDLessColor =
//                     ffxApiGetResourceDX12(_paramHudless[frameIndex], FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
//             }
//         }
//         else if (Config::Instance()->FGRelaxedResolutionCheck.value_or_default() &&
//                  desc.Height >= scDesc.BufferDesc.Height - 32 && desc.Height <= scDesc.BufferDesc.Height + 32 &&
//                  desc.Width >= scDesc.BufferDesc.Width - 32 && desc.Width <= scDesc.BufferDesc.Width + 32 &&
//                  State::Instance().currentD3D12Device != nullptr)
//         {
//             if (CreateBufferResourceWithSize(State::Instance().currentD3D12Device, output,
//                                              D3D12_RESOURCE_STATE_COPY_DEST, &_paramHudless[frameIndex],
//                                              scDesc.BufferDesc.Width, scDesc.BufferDesc.Height, true, false))
//             {
//                 D3D12_TEXTURE_COPY_LOCATION srcLocation;
//                 ZeroMemory(&srcLocation, sizeof(srcLocation));
//                 srcLocation.pResource = output;
//                 srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
//                 srcLocation.SubresourceIndex = 0; // copy from mip 0, array slice 0
//
//                 D3D12_TEXTURE_COPY_LOCATION dstLocation;
//                 ZeroMemory(&dstLocation, sizeof(dstLocation));
//                 dstLocation.pResource = _paramHudless[frameIndex];
//                 dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
//                 dstLocation.SubresourceIndex = 0; // paste into mip 0, array slice 0
//
//                 D3D12_BOX srcBox;
//                 srcBox.left = 0;
//                 srcBox.top = 0;
//                 srcBox.front = 0;
//                 srcBox.back = 1;
//
//                 if (scDesc.BufferDesc.Width > desc.Width || scDesc.BufferDesc.Height > desc.Height)
//                 {
//                     srcBox.right = desc.Width;
//                     srcBox.bottom = desc.Height;
//                     UINT top = (scDesc.BufferDesc.Height - desc.Height) / 2;
//                     UINT left = (scDesc.BufferDesc.Width - desc.Width) / 2;
//
//                     cmdList->CopyTextureRegion(&dstLocation, left, top, 0, &srcLocation, &srcBox);
//                 }
//                 else
//                 {
//                     srcBox.right = scDesc.BufferDesc.Width;
//                     srcBox.bottom = scDesc.BufferDesc.Height;
//
//                     cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, &srcBox);
//                 }
//
//                 m_FrameGenerationConfig.HUDLessColor =
//                     ffxApiGetResourceDX12(_paramHudless[frameIndex], FFX_API_RESOURCE_STATE_COPY_DEST);
//             }
//         }
//     }
//     else
//     {
//         m_FrameGenerationConfig.HUDLessColor = FfxApiResource({});
//     }
//
//     m_FrameGenerationConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
//     m_FrameGenerationConfig.frameGenerationEnabled = true;
//     m_FrameGenerationConfig.flags = 0;
//
//     if (Config::Instance()->FGDebugView.value_or_default())
//         m_FrameGenerationConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW;
//
//     if (Config::Instance()->FGDebugTearLines.value_or_default())
//         m_FrameGenerationConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES;
//
//     if (Config::Instance()->FGDebugResetLines.value_or_default())
//         m_FrameGenerationConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_RESET_INDICATORS;
//
//     if (Config::Instance()->FGDebugPacingLines.value_or_default())
//         m_FrameGenerationConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES;
//
//     m_FrameGenerationConfig.allowAsyncWorkloads = Config::Instance()->FGAsync.value_or_default();
//
//     // use swapchain buffer info
//     DXGI_SWAP_CHAIN_DESC scDesc1 {};
//     if (State::Instance().currentSwapchain->GetDesc(&scDesc1) == S_OK)
//     {
//         if (State::Instance().currentFeature != nullptr)
//         {
//             m_FrameGenerationConfig.generationRect.left = Config::Instance()->FGRectLeft.value_or(
//                 (scDesc1.BufferDesc.Width - State::Instance().currentFeature->DisplayWidth()) / 2);
//             m_FrameGenerationConfig.generationRect.top = Config::Instance()->FGRectTop.value_or(
//                 (scDesc1.BufferDesc.Height - State::Instance().currentFeature->DisplayHeight()) / 2);
//         }
//         else
//         {
//             m_FrameGenerationConfig.generationRect.left = Config::Instance()->FGRectLeft.value_or(0);
//             m_FrameGenerationConfig.generationRect.top = Config::Instance()->FGRectTop.value_or(0);
//         }
//         m_FrameGenerationConfig.generationRect.width =
//             Config::Instance()->FGRectWidth.value_or(scDesc1.BufferDesc.Width);
//         m_FrameGenerationConfig.generationRect.height =
//             Config::Instance()->FGRectHeight.value_or(scDesc1.BufferDesc.Height);
//     }
//     else
//     {
//         m_FrameGenerationConfig.generationRect.left = Config::Instance()->FGRectLeft.value_or(0);
//         m_FrameGenerationConfig.generationRect.top = Config::Instance()->FGRectTop.value_or(0);
//         m_FrameGenerationConfig.generationRect.width =
//             Config::Instance()->FGRectWidth.value_or(State::Instance().currentFeature->DisplayWidth());
//         m_FrameGenerationConfig.generationRect.height =
//             Config::Instance()->FGRectHeight.value_or(State::Instance().currentFeature->DisplayHeight());
//     }
//
//     m_FrameGenerationConfig.frameGenerationCallbackUserContext = this;
//     m_FrameGenerationConfig.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params,
//                                                          void* pUserCtx) -> ffxReturnCode_t
//     {
//         FSRFG_Dx12* fsrFG = nullptr;
//
//         if (pUserCtx != nullptr)
//             fsrFG = reinterpret_cast<FSRFG_Dx12*>(pUserCtx);
//
//         if (fsrFG != nullptr)
//             return fsrFG->DispatchCallback(params);
//
//         return FFX_API_RETURN_ERROR;
//     };
//
//     m_FrameGenerationConfig.onlyPresentGenerated = State::Instance().FGonlyGenerated; // check here
//     m_FrameGenerationConfig.frameID = _frameCount;
//     m_FrameGenerationConfig.swapChain = _swapChain;
//
//     ffxReturnCode_t retCode = FfxApiProxy::D3D12_Configure()(&_fgContext, &m_FrameGenerationConfig.header);
//
//     if (retCode != FFX_API_RETURN_OK)
//         LOG_ERROR("(FG) D3D12_Configure error: {}({})", retCode, FfxApiProxy::ReturnCodeToString(retCode));
//
//     if (retCode == FFX_API_RETURN_OK)
//     {
//         ffxCreateBackendDX12Desc backendDesc {};
//         backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
//         backendDesc.device = State::Instance().currentD3D12Device;
//
//         ffxDispatchDescFrameGenerationPrepare dfgPrepare {};
//         dfgPrepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
//         dfgPrepare.header.pNext = &backendDesc.header;
//
//         // GetDispatchCommandList();
//
// #ifdef USE_QUEUE_FOR_FG
//         dfgPrepare.commandList = _commandList[frameIndex];
// #else
//         dfgPrepare.commandList = cmdList;
// #endif
//
//         dfgPrepare.frameID = _frameCount;
//         dfgPrepare.flags = m_FrameGenerationConfig.flags;
//         dfgPrepare.renderSize = { State::Instance().currentFeature->RenderWidth(),
//                                   State::Instance().currentFeature->RenderHeight() };
//
//         dfgPrepare.jitterOffset.x = _jitterX;
//         dfgPrepare.jitterOffset.y = _jitterY;
//         dfgPrepare.motionVectors = ffxApiGetResourceDX12(_paramVelocity[frameIndex],
//         FFX_API_RESOURCE_STATE_COPY_DEST); dfgPrepare.depth = ffxApiGetResourceDX12(_paramDepth[frameIndex],
//         FFX_API_RESOURCE_STATE_COPY_DEST);
//
//         dfgPrepare.motionVectorScale.x = _mvScaleX;
//         dfgPrepare.motionVectorScale.y = _mvScaleY;
//         dfgPrepare.cameraFar = _cameraFar;
//         dfgPrepare.cameraNear = _cameraNear;
//         dfgPrepare.cameraFovAngleVertical = _cameraVFov;
//         dfgPrepare.frameTimeDelta = _ftDelta;
//         dfgPrepare.viewSpaceToMetersFactor = _meterFactor;
//
//         retCode = FfxApiProxy::D3D12_Dispatch()(&_fgContext, &dfgPrepare.header);
//
//         if (retCode != FFX_API_RETURN_OK)
//         {
//             LOG_ERROR("(FG) D3D12_Dispatch result: {}({})", retCode, FfxApiProxy::ReturnCodeToString(retCode));
//         }
//         else
//         {
//             LOG_DEBUG("(FG) Dispatch ok.");
//
// #ifdef USE_QUEUE_FOR_FG
//             if (!Config::Instance()->FGHudFixCloseAfterCallback.value_or_default())
//             {
//                 ID3D12CommandList* cl[1] = { nullptr };
//                 auto result = _commandList[frameIndex]->Close();
//                 cl[0] = _commandList[frameIndex];
//                 _commandQueue->ExecuteCommandLists(1, cl);
//
//                 if (result != S_OK)
//                 {
//                     LOG_ERROR("(FG) Close result: {}", (UINT) result);
//                 }
//             }
// #endif
//         }
//     }
//
//     if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
//     {
//         LOG_TRACE("Releasing Mutex: {}", Mutex.getOwner());
//         Mutex.unlockThis(1);
//     }
//
//     _mvAndDepthReady[frameIndex] = false;
//
//     return retCode == FFX_API_RETURN_OK;
// }

bool FSRFG_Dx12::Dispatch(ID3D12GraphicsCommandList* cmdList, bool useHudless, double frameTime)
{
    _lastDispatchedFrame = _frameCount;

    LOG_DEBUG("useHudless: {}, frameTime: {}", useHudless, frameTime);

    if (State::Instance().FSRFGFTPchanged)
        ConfigureFramePaceTuning();

    auto fIndex = GetIndex();

    _noHudless[fIndex] = !useHudless;

    ffxConfigureDescFrameGeneration m_FrameGenerationConfig = {};
    m_FrameGenerationConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;

    if (useHudless && _paramHudless[fIndex] != nullptr)
    {
        LOG_TRACE("Using hudless: {:X}", (size_t) _paramHudless[fIndex]);
        m_FrameGenerationConfig.HUDLessColor =
            ffxApiGetResourceDX12(_paramHudless[fIndex], FFX_API_RESOURCE_STATE_COPY_DEST);
        _paramHudless[fIndex] = nullptr;
    }
    else
    {
        m_FrameGenerationConfig.HUDLessColor = FfxApiResource({});
    }

    m_FrameGenerationConfig.frameGenerationEnabled = true;
    m_FrameGenerationConfig.flags = 0;

    if (Config::Instance()->FGDebugView.value_or_default())
        m_FrameGenerationConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW;

    if (Config::Instance()->FGDebugTearLines.value_or_default())
        m_FrameGenerationConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES;

    if (Config::Instance()->FGDebugResetLines.value_or_default())
        m_FrameGenerationConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_RESET_INDICATORS;

    if (Config::Instance()->FGDebugPacingLines.value_or_default())
        m_FrameGenerationConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES;

    m_FrameGenerationConfig.allowAsyncWorkloads = Config::Instance()->FGAsync.value_or_default();

    // use swapchain buffer info
    DXGI_SWAP_CHAIN_DESC scDesc1 {};
    if (State::Instance().currentSwapchain->GetDesc(&scDesc1) == S_OK)
    {
        if (State::Instance().currentFeature != nullptr)
        {
            auto calculatedLeft = (scDesc1.BufferDesc.Width - State::Instance().currentFeature->DisplayWidth()) / 2;
            auto finalLeft = Config::Instance()->FGRectLeft.value_or(calculatedLeft);
            m_FrameGenerationConfig.generationRect.left = finalLeft;

            auto calculatedTop = (scDesc1.BufferDesc.Height - State::Instance().currentFeature->DisplayHeight()) / 2;
            auto finalTop = Config::Instance()->FGRectTop.value_or(calculatedTop);
            m_FrameGenerationConfig.generationRect.top = finalTop;
        }
        else
        {
            m_FrameGenerationConfig.generationRect.left = Config::Instance()->FGRectLeft.value_or(0);
            m_FrameGenerationConfig.generationRect.top = Config::Instance()->FGRectTop.value_or(0);
        }

        m_FrameGenerationConfig.generationRect.width =
            Config::Instance()->FGRectWidth.value_or(State::Instance().currentFeature->DisplayWidth());
        m_FrameGenerationConfig.generationRect.height =
            Config::Instance()->FGRectHeight.value_or(State::Instance().currentFeature->DisplayHeight());
    }
    else
    {
        m_FrameGenerationConfig.generationRect.left = Config::Instance()->FGRectLeft.value_or(0);
        m_FrameGenerationConfig.generationRect.top = Config::Instance()->FGRectTop.value_or(0);
        m_FrameGenerationConfig.generationRect.width =
            Config::Instance()->FGRectWidth.value_or(State::Instance().currentFeature->DisplayWidth());
        m_FrameGenerationConfig.generationRect.height =
            Config::Instance()->FGRectHeight.value_or(State::Instance().currentFeature->DisplayHeight());
    }

    m_FrameGenerationConfig.frameGenerationCallbackUserContext = this;
    m_FrameGenerationConfig.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params,
                                                         void* pUserCtx) -> ffxReturnCode_t
    {
        FSRFG_Dx12* fsrFG = nullptr;

        if (pUserCtx != nullptr)
            fsrFG = reinterpret_cast<FSRFG_Dx12*>(pUserCtx);

        if (fsrFG != nullptr)
            return fsrFG->DispatchCallback(params);

        return FFX_API_RETURN_ERROR;
    };

    m_FrameGenerationConfig.onlyPresentGenerated = State::Instance().FGonlyGenerated;
    m_FrameGenerationConfig.frameID = _frameCount;
    m_FrameGenerationConfig.swapChain = State::Instance().currentSwapchain;

    ffxReturnCode_t retCode = FfxApiProxy::D3D12_Configure()(&_fgContext, &m_FrameGenerationConfig.header);
    LOG_DEBUG("D3D12_Configure result: {0:X}, frame: {1}, fIndex: {2}", retCode, _frameCount, fIndex);

    if (retCode == FFX_API_RETURN_OK)
    {
        ffxCreateBackendDX12Desc backendDesc {};
        backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
        backendDesc.device = State::Instance().currentD3D12Device;

        ffxDispatchDescFrameGenerationPrepare dfgPrepare {};
        dfgPrepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
        dfgPrepare.header.pNext = &backendDesc.header;

        // if (cmdList != nullptr)
        //{
        //     dfgPrepare.commandList = cmdList;
        // }
        // else
        //{
        auto allocator = _commandAllocators[fIndex];
        auto result = allocator->Reset();
        if (result != S_OK)
            return false;

        result = _commandList[fIndex]->Reset(allocator, nullptr);
        if (result != S_OK)
            return false;

        dfgPrepare.commandList = _commandList[fIndex];
        //}

        dfgPrepare.frameID = _frameCount;
        dfgPrepare.flags = m_FrameGenerationConfig.flags;

        dfgPrepare.renderSize = { State::Instance().currentFeature->RenderWidth(),
                                  State::Instance().currentFeature->RenderHeight() };

        dfgPrepare.jitterOffset.x = _jitterX;
        dfgPrepare.jitterOffset.y = _jitterY;
        dfgPrepare.motionVectors = ffxApiGetResourceDX12(_paramVelocity[fIndex], FFX_API_RESOURCE_STATE_COPY_DEST);
        dfgPrepare.depth = ffxApiGetResourceDX12(_paramDepth[fIndex], FFX_API_RESOURCE_STATE_COPY_DEST);

        dfgPrepare.motionVectorScale.x = _mvScaleX;
        dfgPrepare.motionVectorScale.y = _mvScaleY;
        dfgPrepare.cameraFar = _cameraFar;
        dfgPrepare.cameraNear = _cameraNear;
        dfgPrepare.cameraFovAngleVertical = _cameraVFov;
        dfgPrepare.frameTimeDelta = _ftDelta;
        dfgPrepare.viewSpaceToMetersFactor = _meterFactor;

        retCode = FfxApiProxy::D3D12_Dispatch()(&_fgContext, &dfgPrepare.header);
        LOG_DEBUG("D3D12_Dispatch result: {0}, frame: {1}, fIndex: {2}, commandList: {3:X}", retCode, _frameCount,
                  fIndex, (size_t) dfgPrepare.commandList);

        if (retCode == FFX_API_RETURN_OK)
        {
            SetHudlessDispatchReady();
            _commandList[fIndex]->Close();
        }
    }

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default() && Mutex.getOwner() == 1)
    {
        LOG_TRACE("Releasing FG->Mutex: {}", Mutex.getOwner());
        Mutex.unlockThis(1);
    };

    _mvAndDepthReady[fIndex] = false;
    _hudlessReady[fIndex] = false;

    return retCode == FFX_API_RETURN_OK;
}

// ffxReturnCode_t FSRFG_Dx12::DispatchCallback(ffxDispatchDescFrameGeneration* params)
//{
//     auto fIndex = params->frameID % BUFFER_COUNT;
//
//     params->reset = (_reset != 0);
//
// #ifdef USE_QUEUE_FOR_FG
//     if (Config::Instance()->FGHudFixCloseAfterCallback.value_or_default())
//     {
//         ID3D12CommandList* cl[1] = { nullptr };
//         auto result = _commandList[fIndex]->Close();
//         cl[0] = _commandList[fIndex];
//         _commandQueue->ExecuteCommandLists(1, cl);
//
//         if (result != S_OK)
//         {
//             LOG_ERROR("(FG) Close result: {}", (UINT) result);
//         }
//     }
// #endif
//
//     // check for status
//     if (!Config::Instance()->FGEnabled.value_or_default() || _fgContext == nullptr || State::Instance().SCchanged
// #ifdef USE_QUEUE_FOR_FG
//         || _commandList[fIndex] == nullptr || _commandQueue == nullptr
// #endif
//     )
//     {
//         LOG_WARN("(FG) Cancel async dispatch fIndex: {}", fIndex);
//         params->numGeneratedFrames = 0;
//     }
//
//     // If fg is active but upscaling paused
//     if (State::Instance().currentFeature == nullptr || !_isActive || params->frameID == _lastUpscaledFrameId ||
//         State::Instance().FGchanged || State::Instance().currentFeature->FrameCount() == 0)
//     {
//         LOG_WARN("(FG) Callback without active FG! fIndex:{}", fIndex);
//
// #ifdef USE_QUEUE_FOR_FG
//         auto allocator = _commandAllocators[fIndex];
//         auto result = allocator->Reset();
//         result = _commandList[fIndex]->Reset(allocator, nullptr);
// #endif
//
//         params->numGeneratedFrames = 0;
//     }
//
//     auto dispatchResult = FfxApiProxy::D3D12_Dispatch()(&_fgContext, &params->header);
//     LOG_DEBUG("(FG) D3D12_Dispatch result: {}, fIndex: {}", (UINT) dispatchResult, fIndex);
//
//     _lastUpscaledFrameId = params->frameID;
//     return dispatchResult;
// };

ffxReturnCode_t FSRFG_Dx12::DispatchCallback(ffxDispatchDescFrameGeneration* params)
{
    // CallbackMutex.lock();

    HRESULT result;
    ffxReturnCode_t dispatchResult = FFX_API_RETURN_OK;
    int fIndex = params->frameID % BUFFER_COUNT;

    LOG_DEBUG("frameID: {}, commandList: {:X}, numGeneratedFrames: {}", params->frameID, (size_t) params->commandList,
              params->numGeneratedFrames);

    // if (params->frameID != _lastUpscaledFrameId && Config::Instance()->FGExecuteAfterCallback.value_or_default())
    //{
    //     result = _commandList[fIndex]->Close();
    //     LOG_DEBUG("fgCommandList[{}]->Close() result: {:X}", fIndex, (UINT) result);

    //    // if there is command list error return ERROR
    //    if (result == S_OK)
    //    {
    //        ID3D12CommandList* cl[] = { _commandList[fIndex] };
    //        _gameCommandQueue->ExecuteCommandLists(1, cl);
    //    }
    //    else
    //    {
    //        CallbackMutex.unlock();
    //        return FFX_API_RETURN_ERROR;
    //    }
    //}

    // check for status
    if (!Config::Instance()->FGEnabled.value_or_default() || !Config::Instance()->FGHUDFix.value_or_default() ||
        _fgContext == nullptr || State::Instance().SCchanged)
    {
        LOG_WARN("Cancel async dispatch");
        params->numGeneratedFrames = 0;
    }

    // If fg is active but upscaling paused
    if (State::Instance().currentFeature == nullptr || State::Instance().FGchanged || fIndex < 0 || !IsActive() ||
        State::Instance().currentFeature->FrameCount() == 0 || params->frameID == _lastUpscaledFrameId)
    {
        LOG_WARN("Upscaling paused! frameID: {}", params->frameID);
        params->numGeneratedFrames = 0;
    }

    dispatchResult = FfxApiProxy::D3D12_Dispatch()(&_fgContext, &params->header);
    LOG_DEBUG("D3D12_Dispatch result: {}, fIndex: {}", (UINT) dispatchResult, fIndex);

    _lastUpscaledFrameId = params->frameID;

    // CallbackMutex.unlock();

    return dispatchResult;
}

void FSRFG_Dx12::FgDone()
{
    return;

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Waiting Mutex 1, current: {}", Mutex.getOwner());
        Mutex.lock(1);
        LOG_TRACE("Accuired Mutex: {}", Mutex.getOwner());
    }

    if (!State::Instance().isShuttingDown && _fgContext != nullptr)
    {
        ffxConfigureDescFrameGeneration m_FrameGenerationConfig = {};
        m_FrameGenerationConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        m_FrameGenerationConfig.frameGenerationEnabled = false;
        m_FrameGenerationConfig.swapChain = State::Instance().currentSwapchain;
        m_FrameGenerationConfig.presentCallback = nullptr;
        m_FrameGenerationConfig.HUDLessColor = FfxApiResource({});

        ffxReturnCode_t result;
        result = FfxApiProxy::D3D12_Configure()(&_fgContext, &m_FrameGenerationConfig.header);

        if (!State::Instance().isShuttingDown)
            LOG_INFO("D3D12_Configure result: {0:X}", result);
    }

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Releasing Mutex: {}", Mutex.getOwner());
        Mutex.unlockThis(1);
    }
}

void* FSRFG_Dx12::FrameGenerationContext()
{
    LOG_DEBUG("");
    return (void*) _fgContext;
}

void* FSRFG_Dx12::SwapchainContext()
{
    LOG_DEBUG("");
    return _swapChainContext;
}

void FSRFG_Dx12::StopAndDestroyContext(bool destroy, bool shutDown, bool useMutex)
{
    _frameCount = 0;

    LOG_DEBUG("");

    bool mutexTaken = false;
    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default() && useMutex)
    {
        LOG_TRACE("Waiting Mutex 1, current: {}", Mutex.getOwner());
        Mutex.lock(1);
        mutexTaken = true;
        LOG_TRACE("Accuired Mutex: {}", Mutex.getOwner());
    }

    if (!(shutDown || State::Instance().isShuttingDown) && _fgContext != nullptr)
    {
        ffxConfigureDescFrameGeneration m_FrameGenerationConfig = {};
        m_FrameGenerationConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        m_FrameGenerationConfig.frameGenerationEnabled = false;
        m_FrameGenerationConfig.swapChain = State::Instance().currentSwapchain;
        m_FrameGenerationConfig.presentCallback = nullptr;
        m_FrameGenerationConfig.HUDLessColor = FfxApiResource({});

        ffxReturnCode_t result;
        result = FfxApiProxy::D3D12_Configure()(&_fgContext, &m_FrameGenerationConfig.header);

        _isActive = false;

        if (!(shutDown || State::Instance().isShuttingDown))
            LOG_INFO("D3D12_Configure result: {0:X}", result);
    }

    if (destroy && _fgContext != nullptr)
    {
        auto result = FfxApiProxy::D3D12_DestroyContext()(&_fgContext, nullptr);

        if (!(shutDown || State::Instance().isShuttingDown))
            LOG_INFO("D3D12_DestroyContext result: {0:X}", result);

        _fgContext = nullptr;
    }

    if (shutDown || State::Instance().isShuttingDown)
        ReleaseObjects();

    if (mutexTaken)
    {
        LOG_TRACE("Releasing Mutex: {}", Mutex.getOwner());
        Mutex.unlockThis(1);
    }
}

bool FSRFG_Dx12::CreateSwapchain(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, DXGI_SWAP_CHAIN_DESC* desc,
                                 IDXGISwapChain** swapChain)
{
    IDXGIFactory* realFactory = nullptr;
    ID3D12CommandQueue* realQueue = nullptr;

    if (!CheckForRealObject(__FUNCTION__, factory, (IUnknown**) &realFactory))
        realFactory = factory;

    if (!CheckForRealObject(__FUNCTION__, cmdQueue, (IUnknown**) &realQueue))
        realQueue = cmdQueue;

    ffxCreateContextDescFrameGenerationSwapChainNewDX12 createSwapChainDesc {};
    createSwapChainDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_NEW_DX12;
    createSwapChainDesc.dxgiFactory = realFactory;
    createSwapChainDesc.gameQueue = realQueue;
    createSwapChainDesc.desc = desc;
    createSwapChainDesc.swapchain = (IDXGISwapChain4**) swapChain;

    auto result = FfxApiProxy::D3D12_CreateContext()(&_swapChainContext, &createSwapChainDesc.header, nullptr);

    if (result == FFX_API_RETURN_OK)
    {
        ConfigureFramePaceTuning();

        _gameCommandQueue = realQueue;
        _swapChain = *swapChain;
        _hwnd = desc->OutputWindow;

        // hack for RDR1
        //_swapChain->Release();

        return true;
    }

    return false;
}

bool FSRFG_Dx12::CreateSwapchain1(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd,
                                  DXGI_SWAP_CHAIN_DESC1* desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                  IDXGISwapChain1** swapChain)
{
    IDXGIFactory* realFactory = nullptr;
    ID3D12CommandQueue* realQueue = nullptr;

    if (!CheckForRealObject(__FUNCTION__, factory, (IUnknown**) &realFactory))
        realFactory = factory;

    if (!CheckForRealObject(__FUNCTION__, cmdQueue, (IUnknown**) &realQueue))
        realQueue = cmdQueue;

    ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 createSwapChainDesc {};
    createSwapChainDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
    createSwapChainDesc.fullscreenDesc = pFullscreenDesc;
    createSwapChainDesc.hwnd = hwnd;
    createSwapChainDesc.dxgiFactory = realFactory;
    createSwapChainDesc.gameQueue = realQueue;
    createSwapChainDesc.desc = desc;
    createSwapChainDesc.swapchain = (IDXGISwapChain4**) swapChain;

    auto result = FfxApiProxy::D3D12_CreateContext()(&_swapChainContext, &createSwapChainDesc.header, nullptr);

    if (result == FFX_API_RETURN_OK)
    {
        ConfigureFramePaceTuning();

        _gameCommandQueue = realQueue;
        _swapChain = *swapChain;
        _hwnd = hwnd;

        // hack for RDR1
        //_swapChain->Release();

        return true;
    }

    return false;
}

bool FSRFG_Dx12::ReleaseSwapchain(HWND hwnd)
{
    if (hwnd != _hwnd || _hwnd == NULL)
        return false;

    LOG_DEBUG("");

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Waiting Mutex 1, current: {}", Mutex.getOwner());
        Mutex.lock(1);
        LOG_TRACE("Accuired Mutex: {}", Mutex.getOwner());
    }

    MenuOverlayDx::CleanupRenderTarget(true, NULL);

    if (_fgContext != nullptr)
        StopAndDestroyContext(true, true, false);

    if (_swapChainContext != nullptr)
    {
        auto result = FfxApiProxy::D3D12_DestroyContext()(&_swapChainContext, nullptr);
        LOG_INFO("Destroy Ffx Swapchain Result: {}({})", result, FfxApiProxy::ReturnCodeToString(result));

        _swapChainContext = nullptr;
    }

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Releasing Mutex: {}", Mutex.getOwner());
        Mutex.unlockThis(1);
    }

    return true;
}

void FSRFG_Dx12::CreateContext(ID3D12Device* device, IFeature* upscalerContext)
{
    LOG_DEBUG("");

    if (_fgContext != nullptr)
    {
        ffxConfigureDescFrameGeneration m_FrameGenerationConfig = {};
        m_FrameGenerationConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        m_FrameGenerationConfig.frameGenerationEnabled = true;
        m_FrameGenerationConfig.swapChain = State::Instance().currentSwapchain;
        m_FrameGenerationConfig.presentCallback = nullptr;
        m_FrameGenerationConfig.HUDLessColor = FfxApiResource({});

        auto result = FfxApiProxy::D3D12_Configure()(&_fgContext, &m_FrameGenerationConfig.header);

        _isActive = (result == FFX_API_RETURN_OK);

        LOG_DEBUG("Reactivate");

        return;
    }

    ffxCreateBackendDX12Desc backendDesc {};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;

    backendDesc.device = device;

    ffxCreateContextDescFrameGeneration createFg {};
    createFg.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;

    // use swapchain buffer info
    DXGI_SWAP_CHAIN_DESC desc {};
    if (State::Instance().currentSwapchain->GetDesc(&desc) == S_OK)
    {
        createFg.displaySize = { desc.BufferDesc.Width, desc.BufferDesc.Height };
        createFg.maxRenderSize = { upscalerContext->DisplayWidth(), upscalerContext->DisplayHeight() };
    }
    else
    {
        // this might cause issues
        createFg.displaySize = { upscalerContext->DisplayWidth(), upscalerContext->DisplayHeight() };
        createFg.maxRenderSize = { upscalerContext->DisplayWidth(), upscalerContext->DisplayHeight() };
    }

    createFg.flags = 0;

    if (upscalerContext->GetFeatureFlags() & NVSDK_NGX_DLSS_Feature_Flags_IsHDR)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;

    if (upscalerContext->GetFeatureFlags() & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;

    if (upscalerContext->GetFeatureFlags() & NVSDK_NGX_DLSS_Feature_Flags_MVJittered)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;

    if ((upscalerContext->GetFeatureFlags() & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) == 0)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;

    if (Config::Instance()->FGAsync.value_or_default())
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;

    createFg.backBufferFormat = ffxApiGetSurfaceFormatDX12(desc.BufferDesc.Format);
    createFg.header.pNext = &backendDesc.header;

    State::Instance().skipSpoofing = true;
    State::Instance().skipHeapCapture = true;
    ffxReturnCode_t retCode = FfxApiProxy::D3D12_CreateContext()(&_fgContext, &createFg.header, nullptr);
    State::Instance().skipHeapCapture = false;
    State::Instance().skipSpoofing = false;
    LOG_INFO("D3D12_CreateContext result: {0:X}", retCode);

    _isActive = (retCode == FFX_API_RETURN_OK);

    LOG_DEBUG("Create");
}
