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
typedef BOOL (WINAPI* PFSystemParametersInfoForDpi)(UINT, UINT, PVOID, UINT, UINT);

static PFSetThreadDpiAwarenessContext pSetThreadDpiAwarenessContext = 0;
static PFGetDpiForWindow pGetDpiForWindow = 0;
static PFAdjustWindowRectExForDpi pAdjustWindowRectExForDpi = 0;
static PFSystemParametersInfoForDpi pSystemParametersInfoForDpi = 0;
static bool aware = false;

bool Dpi::Init()
{
	HMODULE user32 = GetModuleHandle("user32.dll");
	if (!user32)
		return false;
	pSetThreadDpiAwarenessContext = (PFSetThreadDpiAwarenessContext) GetProcAddress(user32, "SetThreadDpiAwarenessContext");
	pGetDpiForWindow = (PFGetDpiForWindow) GetProcAddress(user32, "GetDpiForWindow");
	pAdjustWindowRectExForDpi = (PFAdjustWindowRectExForDpi) GetProcAddress(user32, "AdjustWindowRectExForDpi");
	pSystemParametersInfoForDpi = (PFSystemParametersInfoForDpi) GetProcAddress(user32, "SystemParametersInfoForDpi");
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

void Dpi::AdjustWindowRectForDpi(RECT* rect, DWORD style, bool menu, DWORD exstyle, UINT dpi)
{
	if (aware && dpi)
		pAdjustWindowRectExForDpi(rect, style, menu, exstyle, dpi);
	else
		AdjustWindowRectEx(rect, style, menu, exstyle);
}

void Dpi::GetNonClientMetrics(NONCLIENTMETRICSW* ncm, UINT dpi)
{
	// SystemParametersInfoForDpi は Vista 以降の大きさ (iPaddedBorderWidth を含む) でないと失敗する。
	// headers.h の _WIN32_WINNT (0x500) では構造体にその欄が無いので、足して渡す
	struct
	{
		NONCLIENTMETRICSW m;
		int iPaddedBorderWidth;
	} full;
	memset(&full, 0, sizeof(full));
	full.m.cbSize = sizeof(full);
	if (aware && dpi && pSystemParametersInfoForDpi
		&& pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(full), &full, 0, dpi))
	{
		*ncm = full.m;
		ncm->cbSize = sizeof(NONCLIENTMETRICSW);
		return;
	}
	memset(ncm, 0, sizeof(NONCLIENTMETRICSW));
	ncm->cbSize = sizeof(NONCLIENTMETRICSW);
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSW), ncm, 0);
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
