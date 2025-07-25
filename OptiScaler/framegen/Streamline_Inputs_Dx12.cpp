#include "Streamline_Inputs_Dx12.h"
#include "IFGFeature_Dx12.h"
#include <Config.h>

bool Sl_Inputs_Dx12::setConstants(const sl::Constants& values) 
{
    constants = sl::Constants {};

    if (constants.has_value() && values.structVersion == constants.value().structVersion)
    {
        constants = values;
        return true;
    }

    constants.reset();

    LOG_ERROR("Wrong constant struct version");

    return false;
}

bool Sl_Inputs_Dx12::evaluateState(ID3D12Device* device)
{ 
    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

    if (fgOutput == nullptr)
        return false;

    if (!constants.has_value())
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

    if (constants.value().depthInverted)
        fgConstants.flags |= FG_Flags::InvertedDepth;

    if (constants.value().motionVectorsJittered)
        fgConstants.flags |= FG_Flags::JitteredMVs;

    if (constants.value().motionVectorsDilated)
        fgConstants.flags |= FG_Flags::DisplayResolutionMVs;

    if (Config::Instance()->FGAsync.value_or_default())
        fgConstants.flags |= FG_Flags::Async;

    fgOutput->UpdateFrameCount();
    fgOutput->EvaluateState(device, fgConstants);

    return true;
}

bool Sl_Inputs_Dx12::reportResource(const sl::ResourceTag& tag, ID3D12GraphicsCommandList* cmdBuffer)
{
    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

    if (fgOutput == nullptr)
        return false;

    if (tag.type == sl::kBufferTypeHUDLessColor)
    {
        auto hudlessResource = (ID3D12Resource*) tag.resource->native;

        fgOutput->SetHudless(cmdBuffer, hudlessResource,
                        (D3D12_RESOURCE_STATES) tag.resource->state, tag.lifecycle == sl::eOnlyValidNow);

        LOG_TRACE("HUDLESS set");
    } 
    else if (tag.type == sl::kBufferTypeDepth || tag.type == sl::kBufferTypeHiResDepth ||
        tag.type == sl::kBufferTypeLinearDepth)
    {
        auto depthResource = (ID3D12Resource*) tag.resource->native;

        Config::Instance()->FGMakeDepthCopy.set_volatile_value(tag.lifecycle == sl::eOnlyValidNow);
        // Config::Instance()->FGMakeDepthCopy.set_volatile_value(false);

        fgOutput->SetDepth(cmdBuffer, depthResource,
                        (D3D12_RESOURCE_STATES) tag.resource->state);

        LOG_TRACE("DEPTH set");
    }
    else if (tag.type == sl::kBufferTypeMotionVectors)
    {
        auto mvResource = (ID3D12Resource*) tag.resource->native;

        auto fg = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

        Config::Instance()->FGMakeMVCopy.set_volatile_value(tag.lifecycle == sl::eOnlyValidNow);
        // Config::Instance()->FGMakeMVCopy.set_volatile_value(false);

        fgOutput->SetVelocity(cmdBuffer, mvResource,
                        (D3D12_RESOURCE_STATES) tag.resource->state);

        LOG_TRACE("VELOCITY set");
    }

    return true; 
}

bool Sl_Inputs_Dx12::dispatchFG(ID3D12GraphicsCommandList* cmdBuffer)
{
    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

    if (fgOutput == nullptr)
        return false;

    if (!constants.has_value())
        return false;

    auto loadCameraMatrix = [&]()
    {
        if (constants.value().orthographicProjection)
            return false;

        float projMatrix[4][4];
        memcpy(projMatrix, (void*)&constants.value().cameraViewToClip, sizeof(projMatrix));

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
            constants.value().cameraNear = static_cast<float>((c == 0.0) ? 0.0 : (d / c));
            constants.value().cameraFar = static_cast<float>(d / (c + 1.0));
        }
        else
        {
            constants.value().cameraNear = static_cast<float>((c == 0.0) ? 0.0 : (-d / c));
            constants.value().cameraFar = static_cast<float>(-d / (c - 1.0));
        }

        if (constants.value().depthInverted)
            std::swap(constants.value().cameraNear, constants.value().cameraFar);

        constants.value().cameraFOV = static_cast<float>(2.0 * std::atan(1.0 / b));
        return true;
    };

    //loadCameraMatrix();

    if (constants.value().cameraNear != 0.0f && constants.value().cameraFar == 0.0f)
    {
        // A CameraFar value of zero indicates an infinite far plane. Due to a bug in FSR's
        // setupDeviceDepthToViewSpaceDepthParams function, CameraFar must always be greater than
        // CameraNear when in use.

        // TODO: Enabling this would require FG recreate for us
        //desc.DepthPlaneInfinite = true;
        constants.value().cameraFar = constants.value().cameraNear + 1.0f;
    }

    // TODO: pass rest of the camera data
    fgOutput->SetCameraValues(constants.value().cameraNear, constants.value().cameraFar,
                                  constants.value().cameraFOV);
    fgOutput->SetJitter(constants.value().jitterOffset.x, constants.value().jitterOffset.y);
    fgOutput->SetMVScale(constants.value().mvecScale.x, constants.value().mvecScale.y, true);

    // cameraPosition[3], float cameraUp[3], float cameraRight[3], float cameraForward[3]
    fgOutput->SetCameraData(reinterpret_cast<float*>(&constants.value().cameraPos),
                            reinterpret_cast<float*>(&constants.value().cameraUp),
                            reinterpret_cast<float*>(&constants.value().cameraRight), 
                            reinterpret_cast<float*>(&constants.value().cameraFwd)
    );

    return fgOutput->Dispatch(cmdBuffer, nullptr, State::Instance().lastFrameTime);
}
