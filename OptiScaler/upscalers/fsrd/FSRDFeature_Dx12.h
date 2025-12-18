#pragma once
#include "FSRDFeature.h"
#include <upscalers/IFeature_Dx12.h>

#include "dx12/ffx_api_dx12.h"
#include "proxies/FfxApi_Proxy.h"

class FSRDFeatureDx12 : public FSRDFeature, public IFeature_Dx12
{
  private:
    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState,
                              ID3D12Resource** OutResource, bool UAV = false, bool depth = false);
    void ResourceBarrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource,
                         D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState);
    bool CopyResource(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* source,
                                       ID3D12Resource** target, D3D12_RESOURCE_STATES sourceState);
    NVSDK_NGX_Parameter* SetParameters(NVSDK_NGX_Parameter* InParameters);
    ID3D12Resource* _buffer;
    DXGI_FORMAT format;
    FfxApiFloatCoords3D cameraPrevPosition;

  protected:
    bool InitFSRD(const NVSDK_NGX_Parameter* InParameters);

  public:
    FSRDFeatureDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    bool Init(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCommandList,
              NVSDK_NGX_Parameter* InParameters) override;
    bool Evaluate(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) override;

    feature_version Version() override { return FSRDFeature::Version(); }
    std::string Name() const override { return FSRDFeature::Name(); }

    ~FSRDFeatureDx12()
    {
        if (State::Instance().isShuttingDown)
            return;

        if (_upscaleContext != nullptr)
            FfxApiProxy::D3D12_DestroyContext(&_upscaleContext, NULL);
    }
};
