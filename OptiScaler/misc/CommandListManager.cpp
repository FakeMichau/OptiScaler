#include "CommandListManager.h"
#include <stdexcept>

CommandListManager::CommandListManager() {}

CommandListManager::~CommandListManager() { Shutdown(); }

bool CommandListManager::Initialize(ID3D12Device* device, ID3D12CommandQueue* cmdQueue)
{
    if (!device || !cmdQueue)
        return false;

    device = device;
    commandQueue = cmdQueue;

    for (int i = 0; i < COMMAND_BUFFER_COUNT; ++i)
    {
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i]))))
            return false;

        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[i].Get(), nullptr,
                                             IID_PPV_ARGS(&commandLists[i]))))
            return false;

        commandLists[i]->Close();
    }

    return true;
}

void CommandListManager::Shutdown()
{
    for (int i = 0; i < COMMAND_BUFFER_COUNT; ++i)
    {
        commandLists[i].Reset();
        allocators[i].Reset();
    }

    commandQueue.Reset();
    device.Reset();
}

ID3D12GraphicsCommandList* CommandListManager::GetCommandList(int index) 
{
    allocators[index]->Reset();
    commandLists[index]->Reset(allocators[index].Get(), nullptr);

    return commandLists[index].Get(); 
}

void CommandListManager::CloseAndExecute(int index)
{
    if (!commandLists[index])
        return;

    commandLists[index]->Close();

    ID3D12CommandList* lists[] = { commandLists[index].Get() };
    commandQueue->ExecuteCommandLists(1, lists);
}
