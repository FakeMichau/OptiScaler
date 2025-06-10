#pragma once

#include "pch.h"

#include <d3d12.h>
#include <sl.h>
#include <sl_dlss_g.h>
#include <sl_dlss.h>
#include <sl.param/parameters.h>
#include <sl1.h>

class StreamlineHooks
{
  public:
    typedef void* (*PFN_slGetPluginFunction)(const char* functionName);
    typedef bool (*PFN_slOnPluginLoad)(void* params, const char* loaderJSON, const char** pluginJSON);
    typedef bool (*PFN_slOnPluginStartup)(const char* jsonConfig, void* device);

    static sl::param::IParameters* slDlssgParams;

    static void unhookInterposer();
    static void hookInterposer(HMODULE slInterposer);

    static void unhookDlss();
    static void hookDlss(HMODULE slDlss);
    
    static void unhookDlssg();
    static void hookDlssg(HMODULE slDlssg);

    static void unhookCommon();
    static void hookCommon(HMODULE slCommon);

  private:
    // interposer
    static decltype(&slInit) o_slInit;
    static decltype(&sl1::slInit) o_slInit_sl1;
    static decltype(&slSetTag) o_slSetTag;
    static decltype(&slGetFeatureFunction) o_slGetFeatureFunction;
    static decltype(&slDLSSSetOptions) o_slDLSSSetOptions;
    static decltype(&slDLSSGSetOptions) o_slDLSSGSetOptions;

    static sl::PFun_LogMessageCallback* o_logCallback;
    static sl1::pfunLogMessageCallback* o_logCallback_sl1;

    // dlss / dlssg
    static PFN_slGetPluginFunction o_dlss_slGetPluginFunction;
    static PFN_slOnPluginLoad o_dlss_slOnPluginLoad;
    static PFN_slGetPluginFunction o_dlssg_slGetPluginFunction;
    static PFN_slOnPluginLoad o_dlssg_slOnPluginLoad;
    static PFN_slGetPluginFunction o_common_slGetPluginFunction;
    static PFN_slOnPluginLoad o_common_slOnPluginLoad;

    static char* trimStreamlineLog(const char* msg);
    static void streamlineLogCallback(sl::LogType type, const char* msg);
    static void streamlineLogCallback_sl1(sl1::LogType type, const char* msg);

    static sl::Result hkslInit(sl::Preferences* pref, uint64_t sdkVersion);
    static sl::Result hkslSetTag(sl::ViewportHandle& viewport, sl::ResourceTag* tags, uint32_t numTags,
                                 sl::CommandBuffer* cmdBuffer);
    static sl::Result hkslGetFeatureFunction(sl::Feature feature, const char* functionName, void*& function);
    static sl::Result hkslDLSSSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSOptions& options);
    static sl::Result hkslDLSSGSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options);
    static bool hkslInit_sl1(sl1::Preferences* pref, int applicationId);

    static bool hkslOnPluginLoad(PFN_slOnPluginLoad o_slOnPluginLoad, std::string& config,
                                 sl::param::IParameters* params,
                                 const char* loaderJSON, const char** pluginJSON);
    static bool hkdlss_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON, const char** pluginJSON);
    static bool hkdlssg_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON, const char** pluginJSON);
    static bool hkcommon_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON, const char** pluginJSON);
    static void* hkdlss_slGetPluginFunction(const char* functionName);
    static void* hkdlssg_slGetPluginFunction(const char* functionName);
    static void* hkcommon_slGetPluginFunction(const char* functionName);
};
