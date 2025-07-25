#pragma once

#include <pch.h>
#include <sl.h>

// camera data
// hudless, depth, color, ui(?)
// 
// init values
// fg feature creation
// fg dispatch
// 

class Sl_Inputs_Dx12
{
  private:
    std::optional<sl::Constants> constants;

  public:
    bool setConstants(const sl::Constants& constants);
    bool evaluateState(ID3D12Device* device);
    bool reportResource(const sl::ResourceTag &tag, ID3D12GraphicsCommandList* cmdBuffer);
    bool dispatchFG(ID3D12GraphicsCommandList* cmdBuffer);
};
