#pragma once
#include "pch.h"
#include <optional>
#include <filesystem>
#include <SimpleIni.h>
#include "backends/IFeature.h"

typedef enum NVNGX_Api
{
	NVNGX_NOT_SELECTED = 0,
	NVNGX_DX11,
	NVNGX_DX12,
	NVNGX_VULKAN,
} NVNGX_Api;

class Config
{
public:
	Config(std::wstring fileName);

	// Depth
	std::optional<bool> DepthInverted;

	// Color
	std::optional<bool> AutoExposure;
	std::optional<bool> HDR;

	// Motion
	std::optional<bool> JitterCancellation;
	std::optional<bool> DisplayResolution;

	// Logging
	std::optional<bool> LoggingEnabled;
	std::optional<bool> LogToFile;
	std::optional<bool> LogToConsole;
	std::optional<bool> LogToNGX;
	std::optional<bool> OpenConsole;
	std::optional<int> LogLevel;
	std::optional<std::string> LogFileName;

	// XeSS
	std::optional<bool> BuildPipelines;
	std::optional<int32_t> NetworkModel;
	std::optional<bool> CreateHeaps;
	std::optional<std::string> XeSSLibrary;

	//DLSS
	std::optional<std::string> DLSSLibrary;
	std::optional<bool> RenderPresetOverride;
	std::optional<int> RenderPresetDLAA;
	std::optional<int> RenderPresetUltraQuality;
	std::optional<int> RenderPresetQuality;
	std::optional<int> RenderPresetBalanced;
	std::optional<int> RenderPresetPerformance;
	std::optional<int> RenderPresetUltraPerformance;

	// CAS
	std::optional<bool> RcasEnabled;
	std::optional<bool> MotionSharpnessEnabled;
	std::optional<bool> MotionSharpnessDebug;
	std::optional<float> MotionSharpness;
	std::optional<float> MotionThreshold;
	std::optional<float> MotionScaleLimit;

	//Sharpness 
	std::optional<bool> OverrideSharpness;
	std::optional<float> Sharpness;

	// menu
	std::optional<float> MenuScale;
	std::optional<bool> OverlayMenu;
	std::optional<int> ShortcutKey;
	std::optional<int> ResetKey;
	std::optional<int> MenuInitDelay;

	// hooks
	std::optional<bool> HookOriginalNvngxOnly;
	std::optional<bool> DisableEarlyHooking;
	std::optional<bool> HookD3D12;
	std::optional<bool> HookSLProxy;
	std::optional<bool> HookFSR3Proxy;

	// Upscale Ratio Override
	std::optional<bool> UpscaleRatioOverrideEnabled;
	std::optional<float> UpscaleRatioOverrideValue;

	// DRS
	std::optional<bool> DrsMinOverrideEnabled;
	std::optional<bool> DrsMaxOverrideEnabled;

	// Quality Overrides
	std::optional<bool> QualityRatioOverrideEnabled;
	std::optional<float> QualityRatio_DLAA;
	std::optional<float> QualityRatio_UltraQuality;
	std::optional<float> QualityRatio_Quality;
	std::optional<float> QualityRatio_Balanced;
	std::optional<float> QualityRatio_Performance;
	std::optional<float> QualityRatio_UltraPerformance;

	//Hotfixes
	std::optional<bool> DisableReactiveMask;
	std::optional<float> MipmapBiasOverride;
	std::optional<int> RoundInternalResolution;

	std::optional<bool> RestoreComputeSignature;
	std::optional<bool> RestoreGraphicSignature;

	std::optional<int32_t> ColorResourceBarrier;
	std::optional<int32_t> MVResourceBarrier;
	std::optional<int32_t> DepthResourceBarrier;
	std::optional<int32_t> ExposureResourceBarrier;
	std::optional<int32_t> MaskResourceBarrier;
	std::optional<int32_t> OutputResourceBarrier;

	// Upscalers
	std::optional<std::string> Dx11Upscaler;
	std::optional<std::string> Dx12Upscaler;
	std::optional<std::string> VulkanUpscaler;
	std::optional<bool> OutputScalingEnabled;
	std::optional<float> OutputScalingMultiplier;

	// fsr
	std::optional<float> FsrVerticalFov;
	std::optional<float> FsrHorizontalFov;

	// dx11wdx12
	std::optional<int> TextureSyncMethod;
	std::optional<int> CopyBackSyncMethod;
	std::optional<bool> Dx11DelayedInit;
	std::optional<bool> SyncAfterDx12;
	std::optional<bool> DontUseNTShared;

	// nvapi override
	std::optional<bool> OverrideNvapiDll;
	std::optional<std::string> NvapiDllPath;

	// nvngx init parameters
	unsigned long long NVNGX_ApplicationId = 1337;
	std::wstring NVNGX_ApplicationDataPath;
	std::string NVNGX_ProjectId;
	NVSDK_NGX_Version NVNGX_Version{};
	const NVSDK_NGX_FeatureCommonInfo* NVNGX_FeatureInfo = nullptr;
	std::vector<std::wstring> NVNGX_FeatureInfo_Paths;
	NVSDK_NGX_LoggingInfo NVNGX_Logger{ nullptr, NVSDK_NGX_LOGGING_LEVEL_OFF, false };
	NVSDK_NGX_EngineType NVNGX_Engine = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
	std::string NVNGX_EngineVersion;
	bool NVNGX_EngineVersion5 = false;
	NVNGX_Api Api = NVNGX_NOT_SELECTED;
	
	// dlss enabler
	bool DE_Available = false;
	std::optional<int> DE_FramerateLimit;
	std::optional<int> DE_DynamicLimitAvailable;	// DFG.Available
	std::optional<int> DE_DynamicLimitEnabled;		// DFG.Enabled

	// for realtime changes
	bool changeBackend = false;
	std::string newBackend = "";

	// XeSS debug stuff
	bool xessDebug = false;
	int xessDebugFrames = 5;
	float lastMipBias = 0.0f;

	// dlss 
	bool dlssDisableHook = false;

	IFeature* CurrentFeature = nullptr;

	bool Reload();
	bool LoadFromPath(const wchar_t* InPath);
	bool SaveIni();

	static Config* Instance();

private:
	inline static Config* _config;

	CSimpleIniA ini;
	std::filesystem::path absoluteFileName;

	std::optional<std::string> readString(std::string section, std::string key, bool lowercase = false);
	std::optional<float> readFloat(std::string section, std::string key);
	std::optional<int> readInt(std::string section, std::string key);
	std::optional<bool> readBool(std::string section, std::string key);
};
