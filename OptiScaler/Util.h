#pragma once
#include <filesystem>

namespace Util
{
#pragma pack(push, 8)
    typedef struct _version_t
    {
        uint16_t major;
        uint16_t minor;
        uint16_t patch;
        uint16_t reserved;
    } version_t;
#pragma pack(pop)

	std::filesystem::path ExePath();
	std::filesystem::path DllPath();
	std::optional<std::filesystem::path> NvngxPath();
	double MillisecondsNow();
	HWND GetProcessWindow();
	void GetDLLVersion(std::wstring dllPath, version_t* versionOut);
};

inline void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		// Set a breakpoint on this line to catch DirectX API errors
		throw std::exception();
	}
}