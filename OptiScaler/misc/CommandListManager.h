#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <string>

#define COMMAND_BUFFER_COUNT 4 // BUFFER_COUNT

class CommandListManager
{
  public:
    CommandListManager();
    ~CommandListManager();

    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* cmdQueue);
    void Shutdown();

    ID3D12GraphicsCommandList* GetCommandList(int index);
    void CloseAndExecute(int index);

  private:
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocators[COMMAND_BUFFER_COUNT];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandLists[COMMAND_BUFFER_COUNT];
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
};
