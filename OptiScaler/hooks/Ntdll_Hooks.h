#pragma once

#include <pch.h>

#include <Util.h>
#include <State.h>
#include <Config.h>
#include <DllNames.h>

#include <detours/detours.h>

#include <cwctype>
#include <winternl.h>

#pragma intrinsic(_ReturnAddress)

class NtdllHooks
{
  private:
    typedef struct _UNICODE_STRING
    {
        USHORT Length;
        USHORT MaximumLength;
        PWSTR Buffer;
    } UNICODE_STRING, *PUNICODE_STRING;

    typedef NTSTATUS(NTAPI* PFN_LdrLoadDll)(PWSTR PathToFile OPTIONAL, PULONG Flags OPTIONAL,
                                            PUNICODE_STRING ModuleFileName, PHANDLE ModuleHandle);

    typedef NTSTATUS(NTAPI* PFN_NtLoadDll)(PUNICODE_STRING PathToFile OPTIONAL, PULONG Flags OPTIONAL,
                                           PUNICODE_STRING ModuleFileName, PHANDLE ModuleHandle);

    inline static PFN_LdrLoadDll o_LdrLoadDll = nullptr;
    inline static PFN_NtLoadDll o_NtLoadDll = nullptr;

    inline static bool _overlayMethodsCalled = false;

    inline static HMODULE LoadLibraryCheckW(std::wstring lcaseLibName)
    {
        auto lcaseLibNameA = wstring_to_string(lcaseLibName);

        // If Opti is not loading as nvngx.dll
        if (!State::Instance().enablerAvailable && !State::Instance().isWorkingAsNvngx)
        {
            // exe path
            auto exePath = Util::ExePath().parent_path().wstring();

            for (size_t i = 0; i < exePath.size(); i++)
                exePath[i] = std::tolower(exePath[i]);

            auto pos = lcaseLibName.rfind(exePath);

            if (Config::Instance()->EnableDlssInputs.value_or_default() && CheckDllNameW(&lcaseLibName, &nvngxNamesW) &&
                (!Config::Instance()->HookOriginalNvngxOnly.value_or_default() || pos == std::string::npos))
            {
                LOG_INFO("nvngx call: {0}, returning this dll!", lcaseLibNameA);

                return dllModule;
            }
        }

        return nullptr;
    }

    static std::wstring UnicodeStringToWString(const UNICODE_STRING& us)
    {
        size_t charCount = us.Length / sizeof(wchar_t);
        return std::wstring(us.Buffer, charCount);
    }

    static NTSTATUS NTAPI hkLdrLoadDll(PWSTR PathToFile, PULONG Flags, PUNICODE_STRING ModuleFileName,
                                       PHANDLE ModuleHandle)
    {
        if (ModuleFileName == nullptr || ModuleFileName->Length == 0)
            return o_LdrLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);

        std::wstring libName = UnicodeStringToWString(*ModuleFileName);
        std::wstring lcaseLibName(libName);

        for (size_t i = 0; i < lcaseLibName.size(); i++)
            lcaseLibName[i] = std::towlower(lcaseLibName[i]);

        if (State::SkipDllChecks())
        {
            if (State::SkipDllName() == "")
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(lcaseLibName));
                return o_LdrLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);
            }

            auto dllName = State::SkipDllName();
            auto pos = wstring_to_string(lcaseLibName).rfind(dllName);

            // -4 for extension `.dll`
            if (pos == (lcaseLibName.length() - dllName.length()) ||
                pos == (lcaseLibName.length() - dllName.length() - 4))
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(lcaseLibName));
                return o_LdrLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);
            }
        }

        auto moduleHandle = LoadLibraryCheckW(lcaseLibName);

        if (moduleHandle != nullptr)
        {

            LOG_TRACE("{}, caller: {}", wstring_to_string(lcaseLibName), Util::WhoIsTheCaller(_ReturnAddress()));
            *ModuleHandle = (HANDLE) moduleHandle;
            return (NTSTATUS) 0x00000000L;
        }

        return o_LdrLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);
    }

    static NTSTATUS NTAPI hkNtLoadDll(PUNICODE_STRING PathToFile, PULONG Flags, PUNICODE_STRING ModuleFileName,
                                      PHANDLE ModuleHandle)
    {
        if (ModuleFileName == nullptr || ModuleFileName->Length == 0)
            return o_NtLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);

        std::wstring libName = UnicodeStringToWString(*ModuleFileName);
        std::wstring lcaseLibName(libName);

        for (size_t i = 0; i < lcaseLibName.size(); i++)
            lcaseLibName[i] = std::towlower(lcaseLibName[i]);

        if (State::SkipDllChecks())
        {
            if (State::SkipDllName() == "")
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(lcaseLibName));
                return o_NtLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);
            }

            auto dllName = State::SkipDllName();
            auto pos = wstring_to_string(lcaseLibName).rfind(dllName);

            // -4 for extension `.dll`
            if (pos == (lcaseLibName.length() - dllName.length()) ||
                pos == (lcaseLibName.length() - dllName.length() - 4))
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(lcaseLibName));
                return o_NtLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);
            }
        }

        auto moduleHandle = LoadLibraryCheckW(lcaseLibName);

        if (moduleHandle != nullptr)
        {
            LOG_TRACE("{}, caller: {}", wstring_to_string(lcaseLibName), Util::WhoIsTheCaller(_ReturnAddress()));
            *ModuleHandle = (HANDLE) moduleHandle;
            return (NTSTATUS) 0x00000000L;
        }

        return o_NtLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);
    }

  public:
    static void Hook()
    {
        if (o_LdrLoadDll != nullptr)
            return;

        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");

        if (ntdll == nullptr)
            return;

        o_LdrLoadDll = (PFN_LdrLoadDll) GetProcAddress(ntdll, "LdrLoadDll");
        o_NtLoadDll = (PFN_NtLoadDll) GetProcAddress(ntdll, "NtLoadDll");

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_LdrLoadDll != nullptr)
            DetourAttach(&(PVOID&) o_LdrLoadDll, hkLdrLoadDll);

        if (o_NtLoadDll != nullptr)
            DetourAttach(&(PVOID&) o_NtLoadDll, hkNtLoadDll);

        DetourTransactionCommit();
    }

    static void UnHook()
    {
        if (o_LdrLoadDll == nullptr)
            return;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&) o_LdrLoadDll, hkLdrLoadDll);
        DetourDetach(&(PVOID&) o_NtLoadDll, hkNtLoadDll);
        DetourTransactionCommit();
    }
};
