#include <pch.h>
#include <Config.h>
#include "FSRDFeature.h"

double FSRDFeature::GetDeltaTime()
{
    double currentTime = Util::MillisecondsNow();
    double deltaTime = (currentTime - _lastFrameTime);
    _lastFrameTime = currentTime;
    return deltaTime;
}

FSRDFeature::FSRDFeature(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature(InHandleId, InParameters)
{
    _initParameters = SetInitParameters(InParameters);
    _lastFrameTime = Util::MillisecondsNow();
}

FSRDFeature::~FSRDFeature()
{
    if (!IsInited())
        return;

    SetInit(false);
}
