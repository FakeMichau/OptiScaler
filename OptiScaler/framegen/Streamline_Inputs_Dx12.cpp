#include "Streamline_Inputs_Dx12.h"
#include "IFGFeature_Dx12.h"
#include <Config.h>

bool Sl_Inputs_Dx12::setConstants(const sl::Constants& values) 
{
    slConstants = sl::Constants {};

    if (slConstants.has_value() && values.structVersion == slConstants.value().structVersion)
    {
        slConstants = values;
        return true;
    }

    slConstants.reset();

    LOG_ERROR("Wrong constant struct version");

    return false;
}

bool Sl_Inputs_Dx12::evaluateState(ID3D12Device* device)
{ 
    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

    if (fgOutput == nullptr)
        return false;

    if (!slConstants.has_value())
    {
        LOG_ERROR("Called without constants being set");
        return false;
    }

    FG_Constants fgConstants {};

    // TODO
    fgConstants.displayWidth = 0;
    fgConstants.displayHeight = 0;

    //if ()
    //    fgConstants.flags |= FG_Flags::Hdr;

    if (slConstants.value().depthInverted)
        fgConstants.flags |= FG_Flags::InvertedDepth;

    if (slConstants.value().motionVectorsJittered)
        fgConstants.flags |= FG_Flags::JitteredMVs;

    if (slConstants.value().motionVectorsDilated)
        fgConstants.flags |= FG_Flags::DisplayResolutionMVs;

    if (Config::Instance()->FGAsync.value_or_default())
        fgConstants.flags |= FG_Flags::Async;

    if (infiniteDepth)
        fgConstants.flags |= FG_Flags::InfiniteDepth;

    fgOutput->UpdateFrameCount();
    fgOutput->EvaluateState(device, fgConstants);

    return true;
}

bool Sl_Inputs_Dx12::reportResource(const sl::ResourceTag& tag, ID3D12GraphicsCommandList* cmdBuffer)
{
    // TODO: disable if FG disabled

    if (!cmdBuffer)
        LOG_ERROR("cmdBuffer is null");

    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

    // It's possible for some only resources to be marked ready if FGEnabled is enabled during resource tagging
    if (fgOutput == nullptr || !fgOutput->IsActive() || !Config::Instance()->FGEnabled.value_or_default())
        return false;

    if (tag.type == sl::kBufferTypeHUDLessColor)
    {
        auto hudlessResource = (ID3D12Resource*) tag.resource->native;

        fgOutput->SetHudless(cmdBuffer, hudlessResource,
                        (D3D12_RESOURCE_STATES) tag.resource->state, tag.lifecycle == sl::eOnlyValidNow);
        fgOutput->SetHudlessReady();

        auto static lastFormat = DXGI_FORMAT_UNKNOWN;
        auto format = hudlessResource->GetDesc().Format;

        // This might be specific to FSR FG
        // Hopefully we don't need to use ffxCreateContextDescFrameGenerationHudless
        if (lastFormat != DXGI_FORMAT_UNKNOWN && lastFormat != format)
        {
            State::Instance().FGchanged = true;
            LOG_DEBUG("HUDLESS format changed, triggering FG reinit");
        }

        lastFormat = format;
    } 
    else if (tag.type == sl::kBufferTypeDepth || tag.type == sl::kBufferTypeHiResDepth ||
        tag.type == sl::kBufferTypeLinearDepth)
    {
        auto depthResource = (ID3D12Resource*) tag.resource->native;

        Config::Instance()->FGMakeDepthCopy.set_volatile_value(tag.lifecycle == sl::eOnlyValidNow);

        fgOutput->SetDepth(cmdBuffer, depthResource,
                        (D3D12_RESOURCE_STATES) tag.resource->state);
        fgOutput->SetDepthReady();
    }
    else if (tag.type == sl::kBufferTypeMotionVectors)
    {
        auto mvResource = (ID3D12Resource*) tag.resource->native;

        auto fg = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

        Config::Instance()->FGMakeMVCopy.set_volatile_value(tag.lifecycle == sl::eOnlyValidNow);

        fgOutput->SetVelocity(cmdBuffer, mvResource,
                        (D3D12_RESOURCE_STATES) tag.resource->state);
        fgOutput->SetVelocityReady();
    }

    return true; 
}

bool Sl_Inputs_Dx12::readyToDispatch() 
{
    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

    if (fgOutput == nullptr)
        return false;

    return fgOutput->ReadyForExecute();
}

bool Sl_Inputs_Dx12::dispatchFG(ID3D12GraphicsCommandList* cmdBuffer)
{
    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

    if (fgOutput == nullptr)
        return false;

    if (!slConstants.has_value())
        return false;

    if (State::Instance().FGchanged)
        return false;

    // Nukem's function, licensed under GPLv3
    auto loadCameraMatrix = [&]()
    {
        if (slConstants.value().orthographicProjection)
            return false;

        float projMatrix[4][4];
        memcpy(projMatrix, (void*)&slConstants.value().cameraViewToClip, sizeof(projMatrix));

        // BUG: Various RTX Remix-based games pass in an identity matrix which is completely useless. No
        // idea why.
        const bool isEmptyOrIdentityMatrix = [&]()
        {
            float m[4][4] = {};
            if (memcmp(projMatrix, m, sizeof(m)) == 0)
                return true;

            m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
            return memcmp(projMatrix, m, sizeof(m)) == 0;
        }();

        if (isEmptyOrIdentityMatrix)
            return false;

        // a 0 0 0
        // 0 b 0 0
        // 0 0 c e
        // 0 0 d 0
        const double b = projMatrix[1][1];
        const double c = projMatrix[2][2];
        const double d = projMatrix[3][2];
        const double e = projMatrix[2][3];

        if (e < 0.0)
        {
            slConstants.value().cameraNear = static_cast<float>((c == 0.0) ? 0.0 : (d / c));
            slConstants.value().cameraFar = static_cast<float>(d / (c + 1.0));
        }
        else
        {
            slConstants.value().cameraNear = static_cast<float>((c == 0.0) ? 0.0 : (-d / c));
            slConstants.value().cameraFar = static_cast<float>(-d / (c - 1.0));
        }

        if (slConstants.value().depthInverted)
            std::swap(slConstants.value().cameraNear, slConstants.value().cameraFar);

        slConstants.value().cameraFOV = static_cast<float>(2.0 * std::atan(1.0 / b));
        return true;
    };

    LOG_DEBUG("Pre camera recalc near: {}, far: {}", slConstants.value().cameraNear, slConstants.value().cameraFar);

    // UE seems to not be passing the correct cameraViewToClip
    // and we can't use it to calculate cameraNear and cameraFar.
    if (engineType != sl::EngineType::eUnreal)
        loadCameraMatrix();

    infiniteDepth = false;
    if (slConstants.value().cameraNear != 0.0f && slConstants.value().cameraFar == 0.0f)
    {
        // A CameraFar value of zero indicates an infinite far plane. Due to a bug in FSR's
        // setupDeviceDepthToViewSpaceDepthParams function, CameraFar must always be greater than
        // CameraNear when in use.

        infiniteDepth = true;
        slConstants.value().cameraFar = slConstants.value().cameraNear + 1.0f;
    }

    LOG_DEBUG("Post camera recalc near: {}, far: {}", slConstants.value().cameraNear, slConstants.value().cameraFar);

    fgOutput->SetCameraValues(slConstants.value().cameraNear, slConstants.value().cameraFar,
                              slConstants.value().cameraFOV, 0.0f);

    fgOutput->SetJitter(slConstants.value().jitterOffset.x, slConstants.value().jitterOffset.y);

    bool multiplyByResolution = slConstants.value().mvecScale.x != 1.f || slConstants.value().mvecScale.y != 1.f;
    fgOutput->SetMVScale(slConstants.value().mvecScale.x, slConstants.value().mvecScale.y, multiplyByResolution);

    fgOutput->SetCameraData(reinterpret_cast<float*>(&slConstants.value().cameraPos),
                            reinterpret_cast<float*>(&slConstants.value().cameraUp),
                            reinterpret_cast<float*>(&slConstants.value().cameraRight), 
                            reinterpret_cast<float*>(&slConstants.value().cameraFwd)
    );

    return fgOutput->Dispatch(cmdBuffer, nullptr, State::Instance().lastFrameTime);
}
