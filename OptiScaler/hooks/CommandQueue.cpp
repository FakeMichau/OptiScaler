#include "CommandQueue.h"
#include <detours/detours.h>
#include <State.h>
#include <upscalers/IFeature_Dx12.h>

PFN_ExecuteCommandLists CommandQueue::o_ExecuteCommandLists = nullptr;
std::unordered_map<ID3D12GraphicsCommandList*, ID3D12CommandQueue*> CommandQueue::commandListToQueueMap{};

ID3D12CommandQueue* CommandQueue::GetLastKnownCommandQueue(ID3D12GraphicsCommandList* commandList) {
    return commandListToQueueMap[commandList];
}

void CommandQueue::hkExecuteCommandLists(ID3D12CommandQueue* This, UINT numCommandLists, ID3D12CommandList* const* ppCommandLists) {
    IFeature_Dx12* dx12Feature = nullptr;

    o_ExecuteCommandLists(This, numCommandLists, ppCommandLists);

    // Track which queue is executing which command list
    for (UINT i = 0; i < numCommandLists; ++i) {
        ID3D12GraphicsCommandList* cmdList = static_cast<ID3D12GraphicsCommandList*>(ppCommandLists[i]);
        commandListToQueueMap[cmdList] = This;

        for (auto& pair : State::Instance().handleIdToCommandList) {
            if (pair.second == cmdList && State::Instance().currentFeature) {
                pair.first->SignalCommandQueue(This);
            }
        }
    }
}

void CommandQueue::HookCommandQueue(ID3D12Device* InDevice) {
    if (!InDevice || o_ExecuteCommandLists)
        return;

    ID3D12CommandQueue* queue = nullptr;

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;

    if (InDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)) == S_OK) {
        void** vtable = *reinterpret_cast<void***>(queue);

        o_ExecuteCommandLists = (PFN_ExecuteCommandLists)vtable[10];

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)o_ExecuteCommandLists, hkExecuteCommandLists);
        DetourTransactionCommit();

        queue->Release();
    }
}
