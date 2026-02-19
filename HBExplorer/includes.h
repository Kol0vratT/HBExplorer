#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if !defined(UNITY_VERSION_PRE_2022_3_8F1) && !defined(UNITY_VERSION_2022_3_8F1)
#define UNITY_VERSION_2022_3_8F1
#endif

#include "IL2CPP_Resolver.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include "kiero/kiero.h"
#include "kiero/minhook/include/MinHook.h"

typedef HRESULT(__stdcall* Present)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);

namespace HBLog
{
	void Printf(const char* fmt, ...);
	void Snapshot(std::vector<std::string>* outLines);
	void Clear();
}

#include "UExplorer.hpp"
