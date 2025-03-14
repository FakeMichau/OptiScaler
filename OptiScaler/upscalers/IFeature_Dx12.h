#pragma once
#include <d3d12.h>
#include "IFeature.h"

#include <pch.h>
#include <Util.h>
#include <menu/menu_dx12.h>
#include <shaders/output_scaling/OS_Dx12.h>
#include <shaders/rcas/RCAS_Dx12.h>
#include <shaders/bias/Bias_Dx12.h>

class IFeature_Dx12 : public virtual IFeature
{
protected:
	ID3D12Device* Device = nullptr;
	static inline std::unique_ptr<Menu_Dx12> Imgui = nullptr;
	std::unique_ptr<OS_Dx12> OutputScaler = nullptr;
	std::unique_ptr<RCAS_Dx12> RCAS = nullptr;
	std::unique_ptr<Bias_Dx12> Bias = nullptr;

	ID3D12Fence* uninitFence = nullptr;
	HANDLE uninitFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	UINT64 uninitFenceValue = 1;

	void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource, D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState) const;

public:
	// Could be moved to IFeature as a generic IUnknown to avoid downcasting
	ID3D12GraphicsCommandList* lastKnownCommandList = nullptr;

	virtual bool Init(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) = 0;
	virtual bool Evaluate(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) = 0;

	HRESULT SignalCommandQueue(ID3D12CommandQueue* commandQueue);
	void WaitForCommandQueue(unsigned int timeoutMs);

	IFeature_Dx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

	void Shutdown() final;

	~IFeature_Dx12();
};
