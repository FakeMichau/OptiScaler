#pragma once
#include "../IFeature_Dx11.h"
#include "DLSSFeature.h"
#include <string>

typedef NVSDK_NGX_Result(*PFN_NVSDK_NGX_D3D11_Init_Ext)(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath, ID3D11Device* InDevice, NVSDK_NGX_Version InSDKVersion, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);
typedef NVSDK_NGX_Result(*PFN_NVSDK_NGX_D3D11_Init_ProjectID)(const char* InProjectId, NVSDK_NGX_EngineType InEngineType, const char* InEngineVersion, const wchar_t* InApplicationDataPath, ID3D11Device* InDevice, NVSDK_NGX_Version InSDKVersion, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);
typedef NVSDK_NGX_Result(*PFN_NVSDK_NGX_D3D11_Shutdown)(void);
typedef NVSDK_NGX_Result(*PFN_NVSDK_NGX_D3D11_Shutdown1)(ID3D11Device* InDevice);
typedef NVSDK_NGX_Result(*PFN_NVSDK_NGX_D3D11_GetParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result(*PFN_NVSDK_NGX_D3D11_AllocateParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result(*PFN_NVSDK_NGX_D3D11_DestroyParameters)(NVSDK_NGX_Parameter* InParameters);
typedef NVSDK_NGX_Result(*PFN_NVSDK_NGX_D3D11_CreateFeature)(ID3D11DeviceContext * InDevCtx, NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle);
typedef NVSDK_NGX_Result(*PFN_NVSDK_NGX_D3D11_ReleaseFeature)(NVSDK_NGX_Handle* InHandle);
typedef NVSDK_NGX_Result(*PFN_NVSDK_NGX_D3D11_EvaluateFeature)(ID3D11DeviceContext* InDevCtx, const NVSDK_NGX_Handle* InFeatureHandle, const NVSDK_NGX_Parameter* InParameters, PFN_NVSDK_NGX_ProgressCallback InCallback);

class DLSSFeatureDx11 : public DLSSFeature, public IFeature_Dx11
{
private:
	inline static PFN_NVSDK_NGX_D3D11_Shutdown _Shutdown = nullptr;
	inline static PFN_NVSDK_NGX_D3D11_Shutdown1 _Shutdown1 = nullptr;
	inline static PFN_NVSDK_NGX_D3D11_Init_Ext _Init_Ext = nullptr;
	inline static PFN_NVSDK_NGX_D3D11_Init_ProjectID _Init_with_ProjectID = nullptr;
	inline static PFN_NVSDK_NGX_D3D11_GetParameters _GetParameters = nullptr;
	inline static PFN_NVSDK_NGX_D3D11_AllocateParameters _AllocateParameters = nullptr;
	inline static PFN_NVSDK_NGX_D3D11_DestroyParameters _DestroyParameters = nullptr;
	inline static PFN_NVSDK_NGX_D3D11_CreateFeature _CreateFeature = nullptr;
	inline static PFN_NVSDK_NGX_D3D11_ReleaseFeature _ReleaseFeature = nullptr;
	inline static PFN_NVSDK_NGX_D3D11_EvaluateFeature _EvaluateFeature = nullptr;

protected:


public:
	bool Init(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, const NVSDK_NGX_Parameter* InParameters) override;
	bool Evaluate(ID3D11DeviceContext* InDeviceContext, const NVSDK_NGX_Parameter* InParameters) override;

	static void Shutdown(ID3D11Device* InDevice);

	DLSSFeatureDx11(unsigned int InHandleId, const NVSDK_NGX_Parameter* InParameters);
	~DLSSFeatureDx11();
};