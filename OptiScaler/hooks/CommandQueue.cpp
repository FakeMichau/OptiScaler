#include "CommandQueue.h"
#include <detours/detours.h>
#include <State.h>
#include <upscalers/IFeature_Dx12.h>

PFN_ExecuteCommandLists CommandQueue::o_ExecuteCommandLists = nullptr;

void CommandQueue::hkExecuteCommandLists(ID3D12CommandQueue* This, UINT numCommandLists, ID3D12CommandList* const* ppCommandLists) {
    IFeature_Dx12* dx12Feature = nullptr;

    o_ExecuteCommandLists(This, numCommandLists, ppCommandLists);

    for (auto& pair : State::Instance().currentFeatures) {
        IFeature_Dx12* dx12Feature = dynamic_cast<IFeature_Dx12*>(pair.second);
        if (!dx12Feature) {
            LOG_WARN("Downcast to IFeature_Dx12 has failed");
            continue;
        }

        for (UINT i = 0; i < numCommandLists; ++i) {
            ID3D12GraphicsCommandList* cmdList = static_cast<ID3D12GraphicsCommandList*>(ppCommandLists[i]);

            if (cmdList == dx12Feature->lastKnownCommandList)
                dx12Feature->SignalCommandQueue(This);
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
