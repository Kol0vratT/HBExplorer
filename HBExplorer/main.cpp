#include "includes.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Present oPresent;
HWND window = NULL;
WNDPROC oWndProc;
ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;
ID3D11RenderTargetView* mainRenderTargetView;
void* g_RenderIl2CppThread = nullptr;
bool g_MenuCursorOverrideLogged = false;
bool init = false;
bool g_ShowExplorer = false;
float g_ExplorerUiAlpha = 0.0f;
bool g_CursorApiHooksReady = false;
POINT g_VirtualCursorPos{ 0, 0 };
bool g_VirtualCursorInitialized = false;
LONG g_RawMouseDeltaX = 0;
LONG g_RawMouseDeltaY = 0;
ULONGLONG g_InjectToastStartMs = 0;
bool g_InjectToastActive = false;

using SetCursorPos_t = BOOL(WINAPI*)(int, int);
using ClipCursor_t = BOOL(WINAPI*)(const RECT*);
SetCursorPos_t oSetCursorPos = nullptr;
ClipCursor_t oClipCursor = nullptr;

BOOL WINAPI hkSetCursorPos(int X, int Y)
{
	if (g_ShowExplorer)
	{
		if (window)
		{
			POINT pt{ X, Y };
			if (ScreenToClient(window, &pt))
				g_VirtualCursorPos = pt;
		}
		return TRUE;
	}

	return oSetCursorPos ? oSetCursorPos(X, Y) : FALSE;
}

BOOL WINAPI hkClipCursor(const RECT* lpRect)
{
	if (g_ShowExplorer)
		return TRUE;

	return oClipCursor ? oClipCursor(lpRect) : FALSE;
}

void InitCursorApiHooks()
{
	if (g_CursorApiHooksReady)
		return;

	MH_STATUS initStatus = MH_Initialize();
	if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED)
	{
		std::printf("[Core] MinHook init failed: %s\n", MH_StatusToString(initStatus));
		return;
	}

	auto createAndEnableApiHook = [](LPCWSTR module, LPCSTR procName, LPVOID detour, LPVOID* original) -> bool
		{
			MH_STATUS createStatus = MH_CreateHookApi(module, procName, detour, original);
			if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
			{
				std::printf("[Core] MH_CreateHookApi failed for %s: %s\n", procName, MH_StatusToString(createStatus));
				return false;
			}

			FARPROC target = GetProcAddress(GetModuleHandleW(module), procName);
			if (!target)
			{
				std::printf("[Core] GetProcAddress failed for %s\n", procName);
				return false;
			}

			MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(target));
			if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
			{
				std::printf("[Core] MH_EnableHook failed for %s: %s\n", procName, MH_StatusToString(enableStatus));
				return false;
			}

			return true;
		};

	bool okSetCursorPos = createAndEnableApiHook(L"user32.dll", "SetCursorPos", reinterpret_cast<LPVOID>(hkSetCursorPos), reinterpret_cast<LPVOID*>(&oSetCursorPos));
	bool okClipCursor = createAndEnableApiHook(L"user32.dll", "ClipCursor", reinterpret_cast<LPVOID>(hkClipCursor), reinterpret_cast<LPVOID*>(&oClipCursor));

	g_CursorApiHooksReady = okSetCursorPos && okClipCursor;
	std::printf("[Core] Cursor API hooks: SetCursorPos=%s ClipCursor=%s\n",
		okSetCursorPos ? "OK" : "FAIL",
		okClipCursor ? "OK" : "FAIL");
}

void ResetVirtualCursorState()
{
	g_VirtualCursorInitialized = false;
	InterlockedExchange(&g_RawMouseDeltaX, 0);
	InterlockedExchange(&g_RawMouseDeltaY, 0);
}

void TickMenuVirtualCursor()
{
	if (!window)
		return;

	RECT rc{};
	if (!GetClientRect(window, &rc))
		return;

	const int width = rc.right - rc.left;
	const int height = rc.bottom - rc.top;
	if (width <= 0 || height <= 0)
		return;

	if (!g_VirtualCursorInitialized)
	{
		POINT pt{};
		if (GetCursorPos(&pt) && ScreenToClient(window, &pt))
		{
			g_VirtualCursorPos = pt;
		}
		else
		{
			g_VirtualCursorPos.x = width / 2;
			g_VirtualCursorPos.y = height / 2;
		}

		g_VirtualCursorInitialized = true;
	}

	const LONG dx = InterlockedExchange(&g_RawMouseDeltaX, 0);
	const LONG dy = InterlockedExchange(&g_RawMouseDeltaY, 0);

	if (dx != 0 || dy != 0)
	{
		g_VirtualCursorPos.x += static_cast<int>(dx);
		g_VirtualCursorPos.y += static_cast<int>(dy);
	}
	else
	{
		POINT pt{};
		if (GetCursorPos(&pt) && ScreenToClient(window, &pt))
			g_VirtualCursorPos = pt;
	}

	if (g_VirtualCursorPos.x < 0) g_VirtualCursorPos.x = 0;
	if (g_VirtualCursorPos.y < 0) g_VirtualCursorPos.y = 0;
	if (g_VirtualCursorPos.x >= width) g_VirtualCursorPos.x = width - 1;
	if (g_VirtualCursorPos.y >= height) g_VirtualCursorPos.y = height - 1;

	ImGuiIO& io = ImGui::GetIO();
	io.MousePos = ImVec2(static_cast<float>(g_VirtualCursorPos.x), static_cast<float>(g_VirtualCursorPos.y));
}

std::wstring GetLogFilePath()
{
	wchar_t modulePath[MAX_PATH]{};
	if (!GetModuleFileNameW(nullptr, modulePath, MAX_PATH))
		return L"HBExplorerLogs.txt";

	std::wstring logPath(modulePath);
	const size_t slashPos = logPath.find_last_of(L"\\/");
	if (slashPos != std::wstring::npos)
		logPath.resize(slashPos + 1);
	else
		logPath.clear();

	logPath += L"HBExplorerLogs.txt";
	return logPath;
}

void InitFileLogging()
{
	static bool s_Initialized = false;
	if (s_Initialized)
		return;

	s_Initialized = true;

	FILE* stream = nullptr;
	const std::wstring logPath = GetLogFilePath();

	if (_wfreopen_s(&stream, logPath.c_str(), L"a", stdout) == 0)
		setvbuf(stdout, nullptr, _IONBF, 0);

	if (_wfreopen_s(&stream, logPath.c_str(), L"a", stderr) == 0)
		setvbuf(stderr, nullptr, _IONBF, 0);

	SYSTEMTIME st{};
	GetLocalTime(&st);
	std::printf("\n========== HBExplorer session %04u-%02u-%02u %02u:%02u:%02u ==========\n",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	std::printf("[Core] File logging initialized: %ls\n", logPath.c_str());
}

bool EnsureRenderIl2CppAttach()
{
	if (g_RenderIl2CppThread)
		return true;

	void* domain = IL2CPP::Domain::Get();
	if (!domain)
		return false;

	g_RenderIl2CppThread = IL2CPP::Thread::Attach(domain);
	std::printf("[Core] Render thread IL2CPP attach: %p\n", g_RenderIl2CppThread);
	return g_RenderIl2CppThread != nullptr;
}

void ForceSystemCursorForMenu()
{
	CURSORINFO cursorInfo{};
	cursorInfo.cbSize = sizeof(cursorInfo);

	// Only lift the internal ShowCursor counter when cursor is actually hidden.
	if (GetCursorInfo(&cursorInfo))
	{
		if ((cursorInfo.flags & CURSOR_SHOWING) == 0)
		{
			while (ShowCursor(TRUE) < 1) {}
		}
	}
	else
	{
		// Fallback if GetCursorInfo failed for any reason.
		while (ShowCursor(TRUE) < 1) {}
	}

	ClipCursor(nullptr);
	SetCapture(nullptr);
	SetCursor(LoadCursor(nullptr, IDC_ARROW));

	// Unity-side cursor lock/visibility often overrides Win32, so force both.
	if (EnsureRenderIl2CppAttach())
	{
		Unity::Cursor::SetLockState(Unity::Cursor::m_eLockMode::None);
		Unity::Cursor::SetVisible(true);
	}

	if (!g_MenuCursorOverrideLogged)
	{
		g_MenuCursorOverrideLogged = true;
		std::printf("[Core] Cursor override enabled for ImGui menu.\n");
	}
}

void InitConsole()
{
	static bool s_Initialized = false;
	if (s_Initialized)
		return;

	s_Initialized = true;

	InitFileLogging();
	/*InitCursorApiHooks();*/
	std::printf("[Core] Logging bootstrap complete.\n");
}

static ImVec4 MakeColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255)
{
	return ImVec4(
		static_cast<float>(r) / 255.0f,
		static_cast<float>(g) / 255.0f,
		static_cast<float>(b) / 255.0f,
		static_cast<float>(a) / 255.0f);
}

static float Clamp01f(float value)
{
	if (value < 0.0f) return 0.0f;
	if (value > 1.0f) return 1.0f;
	return value;
}

static void ApplyGraySmoothImGuiStyle()
{
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();

	style.Alpha = 1.0f;
	style.WindowPadding = ImVec2(12.0f, 10.0f);
	style.FramePadding = ImVec2(10.0f, 6.0f);
	style.CellPadding = ImVec2(8.0f, 5.0f);
	style.ItemSpacing = ImVec2(9.0f, 7.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
	style.IndentSpacing = 18.0f;

	style.WindowRounding = 10.0f;
	style.ChildRounding = 9.0f;
	style.PopupRounding = 9.0f;
	style.FrameRounding = 7.0f;
	style.ScrollbarRounding = 9.0f;
	style.GrabRounding = 7.0f;
	style.TabRounding = 7.0f;

	style.WindowBorderSize = 1.0f;
	style.ChildBorderSize = 1.0f;
	style.PopupBorderSize = 1.0f;
	style.FrameBorderSize = 1.0f;
	style.TabBorderSize = 0.0f;

	style.ScrollbarSize = 12.0f;
	style.GrabMinSize = 9.0f;

	style.AntiAliasedLines = true;
	style.AntiAliasedLinesUseTex = true;
	style.AntiAliasedFill = true;

	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text] = MakeColor(232, 232, 232);
	colors[ImGuiCol_TextDisabled] = MakeColor(150, 150, 150);
	colors[ImGuiCol_WindowBg] = MakeColor(26, 26, 28, 245);
	colors[ImGuiCol_ChildBg] = MakeColor(30, 30, 33, 245);
	colors[ImGuiCol_PopupBg] = MakeColor(34, 34, 38, 250);
	colors[ImGuiCol_Border] = MakeColor(72, 72, 78, 170);
	colors[ImGuiCol_BorderShadow] = MakeColor(0, 0, 0, 0);
	colors[ImGuiCol_FrameBg] = MakeColor(44, 44, 48, 215);
	colors[ImGuiCol_FrameBgHovered] = MakeColor(58, 58, 64, 230);
	colors[ImGuiCol_FrameBgActive] = MakeColor(70, 70, 78, 235);
	colors[ImGuiCol_TitleBg] = MakeColor(21, 21, 24, 245);
	colors[ImGuiCol_TitleBgActive] = MakeColor(35, 35, 40, 245);
	colors[ImGuiCol_TitleBgCollapsed] = MakeColor(16, 16, 18, 180);
	colors[ImGuiCol_MenuBarBg] = MakeColor(33, 33, 37, 245);
	colors[ImGuiCol_ScrollbarBg] = MakeColor(18, 18, 21, 190);
	colors[ImGuiCol_ScrollbarGrab] = MakeColor(72, 72, 80, 210);
	colors[ImGuiCol_ScrollbarGrabHovered] = MakeColor(92, 92, 102, 220);
	colors[ImGuiCol_ScrollbarGrabActive] = MakeColor(112, 112, 124, 230);
	colors[ImGuiCol_CheckMark] = MakeColor(214, 214, 214, 255);
	colors[ImGuiCol_SliderGrab] = MakeColor(145, 145, 155, 230);
	colors[ImGuiCol_SliderGrabActive] = MakeColor(176, 176, 188, 255);
	colors[ImGuiCol_Button] = MakeColor(56, 56, 62, 215);
	colors[ImGuiCol_ButtonHovered] = MakeColor(78, 78, 86, 230);
	colors[ImGuiCol_ButtonActive] = MakeColor(98, 98, 108, 240);
	colors[ImGuiCol_Header] = MakeColor(52, 52, 58, 215);
	colors[ImGuiCol_HeaderHovered] = MakeColor(74, 74, 82, 230);
	colors[ImGuiCol_HeaderActive] = MakeColor(90, 90, 100, 240);
	colors[ImGuiCol_Separator] = MakeColor(74, 74, 80, 170);
	colors[ImGuiCol_SeparatorHovered] = MakeColor(110, 110, 120, 210);
	colors[ImGuiCol_SeparatorActive] = MakeColor(140, 140, 152, 230);
	colors[ImGuiCol_ResizeGrip] = MakeColor(90, 90, 98, 180);
	colors[ImGuiCol_ResizeGripHovered] = MakeColor(115, 115, 126, 220);
	colors[ImGuiCol_ResizeGripActive] = MakeColor(140, 140, 152, 245);
	colors[ImGuiCol_Tab] = MakeColor(43, 43, 48, 235);
	colors[ImGuiCol_TabHovered] = MakeColor(62, 62, 69, 245);
	colors[ImGuiCol_TabActive] = MakeColor(78, 78, 86, 245);
	colors[ImGuiCol_TabUnfocused] = MakeColor(37, 37, 41, 225);
	colors[ImGuiCol_TabUnfocusedActive] = MakeColor(60, 60, 67, 235);
#ifdef IMGUI_HAS_DOCK
	colors[ImGuiCol_DockingPreview] = MakeColor(135, 135, 147, 130);
#endif
	colors[ImGuiCol_PlotLines] = MakeColor(160, 160, 168, 255);
	colors[ImGuiCol_PlotLinesHovered] = MakeColor(220, 220, 228, 255);
	colors[ImGuiCol_PlotHistogram] = MakeColor(172, 172, 180, 255);
	colors[ImGuiCol_PlotHistogramHovered] = MakeColor(226, 226, 234, 255);
	colors[ImGuiCol_TextSelectedBg] = MakeColor(122, 122, 132, 100);
	colors[ImGuiCol_DragDropTarget] = MakeColor(218, 218, 224, 220);
	colors[ImGuiCol_NavHighlight] = MakeColor(158, 158, 170, 200);
	colors[ImGuiCol_NavWindowingHighlight] = MakeColor(255, 255, 255, 100);
	colors[ImGuiCol_ModalWindowDimBg] = MakeColor(4, 4, 6, 180);
}

static void LoadSmoothUiFont(ImGuiIO& io)
{
	io.Fonts->Clear();

	ImFontConfig cfg{};
	cfg.OversampleH = 4;
	cfg.OversampleV = 2;
	cfg.PixelSnapH = false;
	cfg.RasterizerMultiply = 1.10f;

	const ImWchar* glyphRanges = io.Fonts->GetGlyphRangesCyrillic();

	bool loadedFromFile = false;
	ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, &cfg, glyphRanges);
	if (font)
		loadedFromFile = true;
	if (!font)
		font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\tahoma.ttf", 18.0f, &cfg, glyphRanges);
	if (font)
		loadedFromFile = true;
	if (!font)
		font = io.Fonts->AddFontDefault();

	io.FontDefault = font;
	io.FontGlobalScale = 1.0f;

	std::printf("[Core] ImGui font: %s\n", loadedFromFile ? "TTF loaded (smooth)" : "default fallback");
}

static void DrawInjectionToast()
{
	if (!g_InjectToastActive)
		return;

	const ULONGLONG nowMs = GetTickCount64();
	const float elapsed = static_cast<float>(nowMs - g_InjectToastStartMs) / 1000.0f;
	const float totalDuration = 6.0f;
	const float fadeIn = 0.30f;
	const float fadeOut = 0.45f;

	if (elapsed >= totalDuration)
	{
		g_InjectToastActive = false;
		return;
	}

	float alpha = 1.0f;
	if (elapsed < fadeIn)
		alpha = elapsed / fadeIn;
	else if (elapsed > (totalDuration - fadeOut))
		alpha = (totalDuration - elapsed) / fadeOut;
	alpha = Clamp01f(alpha);

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImVec2 toastSize(540.0f, 78.0f);
	const ImVec2 toastPos(
		viewport->WorkPos.x + viewport->WorkSize.x - toastSize.x - 16.0f,
		viewport->WorkPos.y + viewport->WorkSize.y - toastSize.y - 16.0f);

	ImGui::SetNextWindowPos(toastPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(toastSize, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.92f * alpha);

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoInputs;

	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 9.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.42f, 0.42f, 0.46f, 0.85f));

	if (ImGui::Begin("##HBExplorerInjectToast", nullptr, flags))
	{
		ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.95f, alpha), "Explorer has been successfull injected!");
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.78f, 0.78f, 0.84f, alpha), "To open it push button F1");
	}
	ImGui::End();

	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(2);
}

void InitImGui()
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	io.ConfigWindowsResizeFromEdges = true;
	LoadSmoothUiFont(io);
	ApplyGraySmoothImGuiStyle();
	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(pDevice, pContext);
	g_InjectToastStartMs = GetTickCount64();
	g_InjectToastActive = true;
	std::printf("[Core] ImGui initialized.\n");
}

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (g_ShowExplorer && uMsg == WM_INPUT)
	{
		UINT dataSize = 0;
		if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dataSize, sizeof(RAWINPUTHEADER)) == 0
			&& dataSize > 0)
		{
			std::vector<BYTE> rawBuffer(dataSize);
			if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, rawBuffer.data(), &dataSize, sizeof(RAWINPUTHEADER)) == dataSize)
			{
				RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(rawBuffer.data());
				if (raw->header.dwType == RIM_TYPEMOUSE)
				{
					InterlockedAdd(&g_RawMouseDeltaX, raw->data.mouse.lLastX);
					InterlockedAdd(&g_RawMouseDeltaY, raw->data.mouse.lLastY);
				}
			}
		}
	}

	if (g_ShowExplorer && uMsg == WM_SETCURSOR)
	{
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		return TRUE;
	}

	if (true && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
		return true;

	return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void Update()
{
	if (GetAsyncKeyState(VK_F1) & 1)
	{
		g_ShowExplorer = !g_ShowExplorer;
		/*ResetVirtualCursorState();*/
		std::printf("[Core] Explorer visibility: %s\n", g_ShowExplorer ? "ON" : "OFF");
	}
}

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
	if (!init)
	{
		if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice)) && pDevice)
		{
			pDevice->GetImmediateContext(&pContext);

			DXGI_SWAP_CHAIN_DESC sd{};
			if (SUCCEEDED(pSwapChain->GetDesc(&sd)))
				window = sd.OutputWindow;

			ID3D11Texture2D* pBackBuffer = nullptr;
			HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
			if (SUCCEEDED(hr) && pBackBuffer)
			{
				hr = pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &mainRenderTargetView);
				pBackBuffer->Release();
				pBackBuffer = nullptr;

				if (SUCCEEDED(hr) && mainRenderTargetView)
				{
					oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);

					InitImGui();
					init = true;
					std::printf("[Core] Present hook initialization complete.\n");
				}
			}
		}

		else
			return oPresent(pSwapChain, SyncInterval, Flags);
	}

	if (!init || !pContext || !mainRenderTargetView)
		return oPresent(pSwapChain, SyncInterval, Flags);

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	Update();
	ImGuiIO& io = ImGui::GetIO();
	const float dt = (io.DeltaTime > 0.0f && io.DeltaTime < 0.5f) ? io.DeltaTime : (1.0f / 60.0f);
	const float targetAlpha = g_ShowExplorer ? 1.0f : 0.0f;
	const float fadeSpeed = g_ShowExplorer ? 12.0f : 9.0f;
	if (g_ExplorerUiAlpha < targetAlpha)
		g_ExplorerUiAlpha = (g_ExplorerUiAlpha + dt * fadeSpeed > targetAlpha) ? targetAlpha : (g_ExplorerUiAlpha + dt * fadeSpeed);
	else if (g_ExplorerUiAlpha > targetAlpha)
		g_ExplorerUiAlpha = (g_ExplorerUiAlpha - dt * fadeSpeed < targetAlpha) ? targetAlpha : (g_ExplorerUiAlpha - dt * fadeSpeed);

	const bool drawExplorerOverlay = (g_ExplorerUiAlpha > 0.001f);
	io.MouseDrawCursor = drawExplorerOverlay;

	if (drawExplorerOverlay)
	{
		/*ForceSystemCursorForMenu();
		TickMenuVirtualCursor();*/

		if (!EnsureRenderIl2CppAttach())
		{
			bool waitingOpen = true;
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_ExplorerUiAlpha);
			ImGui::Begin("Object Explorer", &waitingOpen, ImGuiWindowFlags_NoCollapse);
			ImGui::TextDisabled("Waiting for IL2CPP domain / render-thread attach...");
			ImGui::End();
			ImGui::PopStyleVar();
			if (!waitingOpen)
				g_ShowExplorer = false;
		}
		else
		{
			bool explorerOpen = true;
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * g_ExplorerUiAlpha);
			UExplorer::Draw(&explorerOpen);
			ImGui::PopStyleVar();

			if (!explorerOpen)
			{
				g_ShowExplorer = false;
				std::printf("[Core] Explorer visibility: OFF (closed by window)\n");
			}
		}
	}
	else
	{
		g_MenuCursorOverrideLogged = false;
	}

	DrawInjectionToast();

	ImGui::Render();

	pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	return oPresent(pSwapChain, SyncInterval, Flags);
}

DWORD WINAPI MainThread(LPVOID lpReserved)
{
	InitConsole();
	std::printf("[Core] Main thread started.\n");

	bool init_hook = false;
	do
	{
		if (kiero::init(kiero::RenderType::D3D11) == kiero::Status::Success)
		{
			std::printf("[Core] kiero initialized.\n");
			kiero::bind(8, (void**)&oPresent, hkPresent);
			std::printf("[Core] Present hook bound.\n");

			const bool il2cppReady = IL2CPP::Initialize();
			std::printf("[Core] IL2CPP::Initialize => %s\n", il2cppReady ? "OK" : "FAILED");
			if (!il2cppReady)
			{
				Sleep(500);
				continue;
			}

			std::printf("[Core] Input update is handled in hkPresent (no IL2CPP callback hook).\n");
			init_hook = true;
		}
		else
		{
			Sleep(100);
		}
	} while (!init_hook);

	std::printf("[Core] Main initialization finished.\n");
	return TRUE;
}

BOOL WINAPI DllMain(HMODULE hMod, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hMod);
		CreateThread(nullptr, 0, MainThread, hMod, 0, nullptr);
		break;
	case DLL_PROCESS_DETACH:
		kiero::shutdown();
		if (g_CursorApiHooksReady)
		{
			MH_Uninitialize();
		}
		std::printf("[Core] Detached, kiero shutdown.\n");
		break;
	}
	return TRUE;
}
