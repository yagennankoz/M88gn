// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	画面の DPI (表示スケール) の扱い
// ---------------------------------------------------------------------------

#include "headers.h"
#include "dpi.h"

// 古い SDK でもビルドできるように、使う値はここで決める
#define M88_DPI_AWARENESS_CONTEXT_UNAWARE				((void*) -1)
#define M88_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2	((void*) -4)

typedef void* (WINAPI* PFSetThreadDpiAwarenessContext)(void*);
typedef UINT (WINAPI* PFGetDpiForWindow)(HWND);
typedef BOOL (WINAPI* PFAdjustWindowRectExForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);

static PFSetThreadDpiAwarenessContext pSetThreadDpiAwarenessContext = 0;
static PFGetDpiForWindow pGetDpiForWindow = 0;
static PFAdjustWindowRectExForDpi pAdjustWindowRectExForDpi = 0;
static bool aware = false;

bool Dpi::Init()
{
	HMODULE user32 = GetModuleHandle("user32.dll");
	if (!user32)
		return false;
	pSetThreadDpiAwarenessContext = (PFSetThreadDpiAwarenessContext) GetProcAddress(user32, "SetThreadDpiAwarenessContext");
	pGetDpiForWindow = (PFGetDpiForWindow) GetProcAddress(user32, "GetDpiForWindow");
	pAdjustWindowRectExForDpi = (PFAdjustWindowRectExForDpi) GetProcAddress(user32, "AdjustWindowRectExForDpi");
	if (!pSetThreadDpiAwarenessContext || !pGetDpiForWindow || !pAdjustWindowRectExForDpi)
		return false;
	// Per Monitor v2 が使えない Windows (1703 より前) では 0 が返る
	aware = pSetThreadDpiAwarenessContext(M88_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != 0;
	return aware;
}

void Dpi::InitThread()
{
	if (aware)
		pSetThreadDpiAwarenessContext(M88_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

bool Dpi::IsAware()
{
	return aware;
}

UINT Dpi::GetForWindow(HWND hwnd)
{
	if (aware && hwnd)
	{
		UINT d = pGetDpiForWindow(hwnd);
		if (d)
			return d;
	}
	return 96;
}

void Dpi::AdjustWindowRect(RECT* rect, DWORD style, bool menu, DWORD exstyle, HWND hwnd)
{
	if (aware && hwnd)
		pAdjustWindowRectExForDpi(rect, style, menu, exstyle, GetForWindow(hwnd));
	else
		AdjustWindowRectEx(rect, style, menu, exstyle);
}

Dpi::UnawareScope::UnawareScope()
{
	prev = aware ? pSetThreadDpiAwarenessContext(M88_DPI_AWARENESS_CONTEXT_UNAWARE) : 0;
}

Dpi::UnawareScope::~UnawareScope()
{
	if (prev)
		pSetThreadDpiAwarenessContext(prev);
}
