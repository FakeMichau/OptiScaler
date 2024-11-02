#pragma once

#include "pch.h"

#include "Config.h"

#include "include/detours/detours.h"

typedef struct _D3DKMT_WDDM_2_7_CAPS
{
    union
    {
        struct
        {
            UINT HwSchSupported : 1;
            UINT HwSchEnabled : 1;
            UINT HwSchEnabledByDefault : 1;
            UINT IndependentVidPnVSyncControl : 1;
            UINT Reserved : 28;
        };
        UINT Value;
    };
} D3DKMT_WDDM_2_7_CAPS;

typedef enum _KMTQUERYADAPTERINFOTYPE
{
    KMTQAITYPE_WDDM_2_7_CAPS = 70,
} KMTQUERYADAPTERINFOTYPE;

typedef struct _D3DKMT_QUERYADAPTERINFO
{
    UINT hAdapter;
    KMTQUERYADAPTERINFOTYPE Type;
    VOID* pPrivateDriverData;
    UINT PrivateDriverDataSize;
} D3DKMT_QUERYADAPTERINFO;

typedef int(*PFN_D3DKMTQueryAdapterInfo)(const D3DKMT_QUERYADAPTERINFO* data);

static PFN_D3DKMTQueryAdapterInfo o_D3DKMTQueryAdapterInfo = nullptr;

static int hkD3DKMTQueryAdapterInfo(const D3DKMT_QUERYADAPTERINFO* data) {
    auto result = o_D3DKMTQueryAdapterInfo(data);
    D3DKMT_WDDM_2_7_CAPS* d3dkmt_wddm_2_7_caps;
    if (data->Type == KMTQAITYPE_WDDM_2_7_CAPS) {
        LOG_INFO("Spoofing HAGS");
        d3dkmt_wddm_2_7_caps = static_cast<D3DKMT_WDDM_2_7_CAPS*>(data->pPrivateDriverData);
        d3dkmt_wddm_2_7_caps->HwSchSupported = 1;
        d3dkmt_wddm_2_7_caps->HwSchEnabled = 1;
        d3dkmt_wddm_2_7_caps->HwSchEnabledByDefault = 0;
        d3dkmt_wddm_2_7_caps->IndependentVidPnVSyncControl = 0;
    }
    return result;
}

// for spoofing HAGS, call early
static void hookGdi32() {
    if (Config::Instance()->SpoofHAGS.value_or(false) || Config::Instance()->DLSSGMod.value_or(false)) {
        o_D3DKMTQueryAdapterInfo = reinterpret_cast<PFN_D3DKMTQueryAdapterInfo>(DetourFindFunction("gdi32.dll", "D3DKMTQueryAdapterInfo"));

        if (o_D3DKMTQueryAdapterInfo != nullptr) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            DetourAttach(&(PVOID&)o_D3DKMTQueryAdapterInfo, hkD3DKMTQueryAdapterInfo);

            DetourTransactionCommit();
        }
    }
}

static void unhookGdi32() {
    if (o_D3DKMTQueryAdapterInfo != nullptr) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourDetach(&(PVOID&)o_D3DKMTQueryAdapterInfo, hkD3DKMTQueryAdapterInfo);
        o_D3DKMTQueryAdapterInfo = nullptr;

        DetourTransactionCommit();
    }
}
