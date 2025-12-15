#pragma once
#include "FSRDFeature.h"
#include <upscalers/IFeature_Dx12.h>

#include "dx12/ffx_api_dx12.h"
#include "proxies/FfxApi_Proxy.h"

class FSRDFeatureDx12 : public FSRDFeature, public IFeature_Dx12
{
  private:
    NVSDK_NGX_Parameter* SetParameters(NVSDK_NGX_Parameter* InParameters);

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
