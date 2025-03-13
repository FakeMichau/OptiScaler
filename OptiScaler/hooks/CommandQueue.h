#pragma once
#include "pch.h"
#include <ankerl/unordered_dense.h>
#include <d3d12.h>

typedef void(*PFN_ExecuteCommandLists)(ID3D12CommandQueue* This, UINT numCommandLists, ID3D12CommandList* const* ppCommandLists);

class CommandQueue
{
private:
    // Elements are never removed, could become an issue
    static ankerl::unordered_dense::map<ID3D12GraphicsCommandList*, ID3D12CommandQueue*> commandListToQueueMap;
    static PFN_ExecuteCommandLists o_ExecuteCommandLists;

    static void hkExecuteCommandLists(ID3D12CommandQueue* This, UINT numCommandLists, ID3D12CommandList* const* ppCommandLists);

public:
    static ID3D12CommandQueue* GetLastKnownCommandQueue(ID3D12GraphicsCommandList* commandList);
    static void HookCommandQueue(ID3D12Device* InDevice);
};

