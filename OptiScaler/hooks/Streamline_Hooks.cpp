#include "Streamline_Hooks.h"

#include <json.hpp>
#include "detours/detours.h"

#include <Util.h>
#include <Config.h>
#include <proxies/KernelBase_Proxy.h>

// interposer
decltype(&slInit) StreamlineHooks::o_slInit = nullptr;
decltype(&sl1::slInit) StreamlineHooks::o_slInit_sl1 = nullptr;
decltype(&slSetTag) StreamlineHooks::o_slSetTag = nullptr;
decltype(&slGetFeatureFunction) StreamlineHooks::o_slGetFeatureFunction = nullptr;
decltype(&slDLSSSetOptions) StreamlineHooks::o_slDLSSSetOptions = nullptr;
decltype(&slDLSSGSetOptions) StreamlineHooks::o_slDLSSGSetOptions = nullptr;

sl::PFun_LogMessageCallback* StreamlineHooks::o_logCallback = nullptr;
sl1::pfunLogMessageCallback* StreamlineHooks::o_logCallback_sl1 = nullptr;

StreamlineHooks::PFN_slGetPluginFunction StreamlineHooks::o_dlss_slGetPluginFunction = nullptr;
StreamlineHooks::PFN_slOnPluginLoad StreamlineHooks::o_dlss_slOnPluginLoad = nullptr;
StreamlineHooks::PFN_slGetPluginFunction StreamlineHooks::o_dlssg_slGetPluginFunction = nullptr;
StreamlineHooks::PFN_slOnPluginLoad StreamlineHooks::o_dlssg_slOnPluginLoad = nullptr;
StreamlineHooks::PFN_slGetPluginFunction StreamlineHooks::o_common_slGetPluginFunction = nullptr;
StreamlineHooks::PFN_slOnPluginLoad StreamlineHooks::o_common_slOnPluginLoad = nullptr;

sl::param::IParameters* StreamlineHooks::slDlssgParams = nullptr;

char* StreamlineHooks::trimStreamlineLog(const char* msg)
{
    int bracket_count = 0;

    char* result = (char*) malloc(strlen(msg) + 1);
    if (!result)
        return NULL;

    strcpy(result, msg);

    size_t length = strlen(result);
    if (length > 0 && result[length - 1] == '\n')
    {
        result[length - 1] = '\0';
    }

    return result;
}

void StreamlineHooks::streamlineLogCallback(sl::LogType type, const char* msg)
{
    char* trimmed_msg = trimStreamlineLog(msg);

    switch (type)
    {
    case sl::LogType::eWarn:
        LOG_WARN("{}", trimmed_msg);
        break;
    case sl::LogType::eInfo:
        LOG_INFO("{}", trimmed_msg);
        break;
    case sl::LogType::eError:
        LOG_ERROR("{}", trimmed_msg);
        break;
    case sl::LogType::eCount:
        LOG_ERROR("{}", trimmed_msg);
        break;
    }

    free(trimmed_msg);

    if (o_logCallback != nullptr)
        o_logCallback(type, msg);
}

sl::Result StreamlineHooks::hkslInit(sl::Preferences* pref, uint64_t sdkVersion)
{
    LOG_FUNC();

    if (pref->logMessageCallback != &streamlineLogCallback)
        o_logCallback = pref->logMessageCallback;
    pref->logLevel = sl::LogLevel::eCount;
    pref->logMessageCallback = &streamlineLogCallback;

    sl::Preferences prefCopy = *pref;

    std::vector<sl::Feature> featureVec(prefCopy.featuresToLoad, prefCopy.featuresToLoad + prefCopy.numFeaturesToLoad);

    // TODO: always adds DLSSG, make config
    if (std::find(featureVec.begin(), featureVec.end(), sl::kFeatureDLSS_G) == featureVec.end())
    {
        featureVec.push_back(sl::kFeatureDLSS_G);
    }

    //delete[] prefCopy.featuresToLoad;
    prefCopy.numFeaturesToLoad = featureVec.size();
    auto mutableList = new sl::Feature[prefCopy.numFeaturesToLoad];
    std::copy(featureVec.begin(), featureVec.end(), mutableList);

    prefCopy.featuresToLoad = mutableList;

    auto result = o_slInit(prefCopy, sdkVersion);

    return result;
}

// Might need to get updated soon, when Cyberpunk updates streamline version
sl::Result StreamlineHooks::hkslSetTag(sl::ViewportHandle& viewport, sl::ResourceTag* tags, uint32_t numTags,
                                       sl::CommandBuffer* cmdBuffer)
{
    for (uint32_t i = 0; i < numTags; i++)
    {
        if (State::Instance().gameQuirk == Cyberpunk && tags[i].type == 2 &&
            tags[i].resource->state ==
                (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
        {
            tags[i].resource->state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            LOG_TRACE("Changing hudless resource state");
        }
    }
    auto result = o_slSetTag(viewport, tags, numTags, cmdBuffer);
    return result;
}

sl::Result StreamlineHooks::hkslGetFeatureFunction(sl::Feature feature, const char* functionName, void*& function) {
    auto result = o_slGetFeatureFunction(feature, functionName, function);
    LOG_TRACE("Feature: {}, function name: {}, result:{}", (uint32_t)feature, functionName, (uint32_t)result);
    return result;
}

void StreamlineHooks::streamlineLogCallback_sl1(sl1::LogType type, const char* msg)
{
    char* trimmed_msg = trimStreamlineLog(msg);

    switch (type)
    {
    case sl1::LogType::eLogTypeWarn:
        LOG_WARN("{}", trimmed_msg);
        break;
    case sl1::LogType::eLogTypeInfo:
        LOG_INFO("{}", trimmed_msg);
        break;
    case sl1::LogType::eLogTypeError:
        LOG_ERROR("{}", trimmed_msg);
        break;
    case sl1::LogType::eLogTypeCount:
        LOG_ERROR("{}", trimmed_msg);
        break;
    }

    free(trimmed_msg);

    if (o_logCallback_sl1)
        o_logCallback_sl1(type, msg);
}

bool StreamlineHooks::hkslInit_sl1(sl1::Preferences* pref, int applicationId)
{
    LOG_FUNC();
    if (pref->logMessageCallback != &streamlineLogCallback_sl1)
        o_logCallback_sl1 = pref->logMessageCallback;
    pref->logLevel = sl1::LogLevel::eLogLevelCount;
    pref->logMessageCallback = &streamlineLogCallback_sl1;
    return o_slInit_sl1(*pref, applicationId);
}

bool StreamlineHooks::hkslOnPluginLoad(PFN_slOnPluginLoad o_slOnPluginLoad, std::string& config,
                                       sl::param::IParameters* params,
                                       const char* loaderJSON, const char** pluginJSON)
{
    LOG_FUNC();

    bool result;

    if (Config::Instance()->StreamlineSpoofing.has_value() && Config::Instance()->StreamlineSpoofing.value() &&
        !Config::Instance()->DxgiSpoofing.value_or_default())
    {
        // Dirty way of doing this but having "forceSpoofing" state seems annoying
        // and DxgiSpoofing already does everything needed here
        auto spoofing = Config::Instance()->DxgiSpoofing;
        Config::Instance()->DxgiSpoofing = true;

        LOG_INFO("Enabling spoofing for just Streamline");

        result = o_slOnPluginLoad(params, loaderJSON, pluginJSON);

        Config::Instance()->DxgiSpoofing = spoofing;
    }
    else
    {
        result = o_slOnPluginLoad(params, loaderJSON, pluginJSON);
    }

    if (Config::Instance()->VulkanExtensionSpoofing.value_or_default() && Config::Instance()->FGType == Nukems)
    {
        nlohmann::json configJson = nlohmann::json::parse(*pluginJSON);

        if (Config::Instance()->VulkanExtensionSpoofing.value_or_default())
        {
            configJson["external"]["vk"]["instance"]["extensions"].clear();
            configJson["external"]["vk"]["device"]["extensions"].clear();
            configJson["external"]["vk"]["device"]["1.2_features"].clear();
        }

        if (Config::Instance()->FGType == Nukems)
        {
            if (configJson["external"].contains("hws"))
            {
                configJson["external"]["hws"]["detected"] = false;
                configJson["external"]["hws"]["required"] = false;
                configJson["external"]["hws"]["supported"] = false;
            }

            for (auto& adapter : configJson["external"]["adapters"])
            {
                adapter["detected"] = 400; // Ada
                adapter["required"] = 0;
                adapter["supported"] = true;
            }

            configJson["external"]["driver"]["detected"] = "999.99.0";
            configJson["external"]["driver"]["required"] = "512.15.0";
            configJson["external"]["driver"]["supported"] = true;

            configJson["supportedAdapters"] = 1;
        }

        config = configJson.dump();

        LOG_TRACE("{}", config);

        *pluginJSON = config.c_str();
    }

    return result;
}

bool StreamlineHooks::hkdlss_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                            const char** pluginJSON)
{
    // TODO: do it better than "static" and hoping for the best
    static std::string config;
    return hkslOnPluginLoad(o_dlss_slOnPluginLoad, config, params, loaderJSON, pluginJSON);
}

bool StreamlineHooks::hkdlssg_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                             const char** pluginJSON)
{
    // TODO: do it better than "static" and hoping for the best
    static std::string config;

    slDlssgParams = params;

    return hkslOnPluginLoad(o_dlssg_slOnPluginLoad, config, params, loaderJSON, pluginJSON);
}

bool StreamlineHooks::hkcommon_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                             const char** pluginJSON)
{
    // TODO: do it better than "static" and hoping for the best
    static std::string config;
    return hkslOnPluginLoad(o_common_slOnPluginLoad, config, params, loaderJSON, pluginJSON);
}

void* StreamlineHooks::hkdlss_slGetPluginFunction(const char* functionName)
{
    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_dlss_slOnPluginLoad = (PFN_slOnPluginLoad) o_dlss_slGetPluginFunction(functionName);
        return &hkdlss_slOnPluginLoad;
    }

    if (strcmp(functionName, "slDLSSSetOptions") == 0)
    {
        o_slDLSSSetOptions = reinterpret_cast<decltype(&slDLSSSetOptions)>(o_dlss_slGetPluginFunction(functionName));
        return &hkslDLSSSetOptions;
    }

    return o_dlss_slGetPluginFunction(functionName);
}

void* StreamlineHooks::hkdlssg_slGetPluginFunction(const char* functionName)
{
    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_dlssg_slOnPluginLoad = (PFN_slOnPluginLoad) o_dlssg_slGetPluginFunction(functionName);
        return &hkdlssg_slOnPluginLoad;
    }

    return o_dlssg_slGetPluginFunction(functionName);
}

void* StreamlineHooks::hkcommon_slGetPluginFunction(const char* functionName)
{
    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_common_slOnPluginLoad = (PFN_slOnPluginLoad) o_common_slGetPluginFunction(functionName);
        return &hkcommon_slOnPluginLoad;
    }

    return o_common_slGetPluginFunction(functionName);
}

sl::Result StreamlineHooks::hkslDLSSGSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options)
{
    // return o_slDLSSGSetOptions(viewport, options);

    // This only gets hooked when ConvertDLSStoDLSSG is enabled
    // Doing this prevents double calling of slDLSSGSetOptions
    return sl::Result::eOk;
}

sl::Result StreamlineHooks::hkslDLSSSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSOptions& options)
{
    LOG_FUNC();

    if (Config::Instance()->ConvertDLSStoDLSSG.value_or_default())
    {
        if (!o_slDLSSGSetOptions && o_slGetFeatureFunction)
        {
            void* ptrToSlDLSSGSetOptions;
            auto result = o_slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", ptrToSlDLSSGSetOptions);

            if (result == sl::Result::eOk)
                o_slDLSSGSetOptions = reinterpret_cast<decltype(&slDLSSGSetOptions)>(ptrToSlDLSSGSetOptions);

            if (o_slDLSSGSetOptions)
            {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                DetourAttach(&(PVOID&) o_slDLSSGSetOptions, hkslDLSSGSetOptions);

                DetourTransactionCommit();
            }
        }

        if (o_slDLSSGSetOptions)
        {
            // struct could be too new
            sl::DLSSGOptions dlssgOptions {};
            dlssgOptions.mode = sl::DLSSGMode::eOn;
            dlssgOptions.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
            o_slDLSSGSetOptions(viewport, dlssgOptions);
        }
    }

    return o_slDLSSSetOptions(viewport, options);
}

// SL INTERPOSER

void StreamlineHooks::unhookInterposer()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_slSetTag)
    {
        DetourDetach(&(PVOID&) o_slSetTag, hkslSetTag);
        o_slSetTag = nullptr;
    }

    if (o_slInit)
    {
        DetourDetach(&(PVOID&) o_slInit, hkslInit);
        o_slInit = nullptr;
    }

    if (o_slInit_sl1)
    {
        DetourDetach(&(PVOID&) o_slInit_sl1, hkslInit_sl1);
        o_slInit_sl1 = nullptr;
    }

    o_logCallback_sl1 = nullptr;
    o_logCallback = nullptr;

    DetourTransactionCommit();
}

// Call it just after sl.interposer's load or if sl.interposer is already loaded
void StreamlineHooks::hookInterposer(HMODULE slInterposer)
{
    LOG_FUNC();

    if (!slInterposer)
    {
        LOG_WARN("Streamline module in NULL");
        return;
    }

    // Looks like when reading DLL version load methods are called
    // To prevent loops disabling checks for sl.interposer.dll
    State::DisableChecks(7, "sl.interposer");

    if (o_slSetTag || o_slInit || o_slInit_sl1)
        unhookInterposer();

    {
        char dllPath[MAX_PATH];
        GetModuleFileNameA(slInterposer, dllPath, MAX_PATH);

        // TODO: fix, likes to give weird results
        Util::version_t sl_version;
        Util::GetDLLVersion(string_to_wstring(dllPath), &sl_version);
        LOG_INFO("Streamline version: {}.{}.{}", sl_version.major, sl_version.minor, sl_version.patch);

        if (sl_version.major >= 2)
        {
            o_slSetTag =
                reinterpret_cast<decltype(&slSetTag)>(KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetTag"));
            o_slInit = reinterpret_cast<decltype(&slInit)>(KernelBaseProxy::GetProcAddress_()(slInterposer, "slInit"));

            o_slGetFeatureFunction = reinterpret_cast<decltype(&slGetFeatureFunction)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slGetFeatureFunction"));

            if (o_slSetTag && o_slInit && o_slGetFeatureFunction)
            {
                LOG_TRACE("Hooking v2");
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (Config::Instance()->FGType.value_or_default() == FGType::Nukems)
                    DetourAttach(&(PVOID&) o_slSetTag, hkslSetTag);

                DetourAttach(&(PVOID&) o_slInit, hkslInit);
                DetourAttach(&(PVOID&) o_slGetFeatureFunction, hkslGetFeatureFunction);

                DetourTransactionCommit();
            }
        }
        else if (sl_version.major == 1)
        {
            o_slInit_sl1 =
                reinterpret_cast<decltype(&sl1::slInit)>(KernelBaseProxy::GetProcAddress_()(slInterposer, "slInit"));

            if (o_slInit_sl1)
            {
                LOG_TRACE("Hooking v1");
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                DetourAttach(&(PVOID&) o_slInit_sl1, hkslInit_sl1);

                DetourTransactionCommit();
            }
        }
    }

    State::EnableChecks(7);
}

// SL DLSS

void StreamlineHooks::unhookDlss()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_dlss_slGetPluginFunction)
    {
        DetourDetach(&(PVOID&) o_dlss_slGetPluginFunction, hkdlss_slGetPluginFunction);
        o_dlss_slGetPluginFunction = nullptr;
    }

    DetourTransactionCommit();
}

void StreamlineHooks::hookDlss(HMODULE slDlss)
{
    LOG_FUNC();

    if (!slDlss)
    {
        LOG_WARN("Dlss module in NULL");
        return;
    }

    if (o_dlss_slGetPluginFunction)
        unhookDlss();

    o_dlss_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slDlss, "slGetPluginFunction"));

    if (o_dlss_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.dlss");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_dlss_slGetPluginFunction, hkdlss_slGetPluginFunction);

        DetourTransactionCommit();
    }
}

// SL DLSSG

void StreamlineHooks::unhookDlssg()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_dlssg_slGetPluginFunction)
    {
        DetourDetach(&(PVOID&) o_dlssg_slGetPluginFunction, hkdlssg_slGetPluginFunction);
        o_dlssg_slGetPluginFunction = nullptr;
    }

    DetourTransactionCommit();
}

void StreamlineHooks::hookDlssg(HMODULE slDlssg)
{
    LOG_FUNC();

    if (!slDlssg)
    {
        LOG_WARN("Dlssg module in NULL");
        return;
    }

    if (o_dlssg_slGetPluginFunction)
        unhookDlssg();

    o_dlssg_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slDlssg, "slGetPluginFunction"));

    if (o_dlssg_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.dlssg");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_dlssg_slGetPluginFunction, hkdlssg_slGetPluginFunction);

        DetourTransactionCommit();
    }
}

// SL COMMON

void StreamlineHooks::unhookCommon()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_common_slGetPluginFunction)
    {
        DetourDetach(&(PVOID&) o_common_slGetPluginFunction, hkcommon_slGetPluginFunction);
        o_common_slGetPluginFunction = nullptr;
    }

    DetourTransactionCommit();
}

void StreamlineHooks::hookCommon(HMODULE slCommon)
{
    LOG_FUNC();

    if (!slCommon)
    {
        LOG_WARN("Common module in NULL");
        return;
    }

    if (o_common_slGetPluginFunction)
        unhookCommon();

    o_common_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slCommon, "slGetPluginFunction"));

    if (o_common_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.common");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_common_slGetPluginFunction, hkcommon_slGetPluginFunction);

        DetourTransactionCommit();
    }
}
