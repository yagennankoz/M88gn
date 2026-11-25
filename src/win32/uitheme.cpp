// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	テーマ: 本体の窓の枠まわりの色
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#include "headers.h"
#include "uitheme.h"
#include "88config.h"

using namespace PC8801;

namespace Theme
{
	bool stripdrawing = false;
	static int current = system;
	static Colors colors;

	static const char* const inisection = "M88p2 for Windows";

	int Get()
	{
		return current;
	}

	void Set(int t)
	{
		if (t < 0 || t >= count)
			t = system;
		current = t;
		char buf[8];
		wsprintf(buf, "%d", t);
		WritePrivateProfileString(inisection, "Theme", buf, GetM88IniPath());
	}

	void Load()
	{
		int t = GetPrivateProfileInt(inisection, "Theme", system, GetM88IniPath());
		current = (t >= 0 && t < count) ? t : system;
	}

	void ApplyScrollBars(HWND hwnd)
	{
		typedef HRESULT (WINAPI* SetWindowThemeFn)(HWND, LPCWSTR, LPCWSTR);
		static HMODULE uxtheme = LoadLibrary("uxtheme.dll");
		static SetWindowThemeFn setwt = uxtheme ? (SetWindowThemeFn) GetProcAddress(uxtheme, "SetWindowTheme") : 0;
		if (!setwt || !hwnd || !IsWindow(hwnd))
			return;
		setwt(hwnd, current == dark ? L"DarkMode_Explorer" : 0, 0);
		// 枠 (スクロールバー) を描き直す
		SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}

	COLORREF Mix(COLORREF a, COLORREF b, int t)
	{
		return RGB(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t / 256,
				   GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t / 256,
				   GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t / 256);
	}

	const Colors& C()
	{
		Colors& c = colors;
		switch (current)
		{
		case dark:		// VS Code の Dark Modern に近い色
			c.titlebg = RGB(0x1f, 0x1f, 0x1f), c.titlefg = RGB(0xcc, 0xcc, 0xcc);
			c.titleibg = RGB(0x1f, 0x1f, 0x1f), c.titleifg = RGB(0x80, 0x80, 0x80);
			c.panebg = RGB(0x18, 0x18, 0x18);
			c.tabfg = RGB(0x9d, 0x9d, 0x9d);
			c.tabselbg = RGB(0x26, 0x26, 0x26), c.tabselfg = RGB(0xff, 0xff, 0xff);
			c.tabaccent = RGB(0x00, 0x78, 0xd4);
			c.tabsep = RGB(0x3c, 0x3c, 0x3c);
			c.stripfg = RGB(0xcc, 0xcc, 0xcc), c.stripgray = RGB(0x6e, 0x6e, 0x6e);
			c.splitter = RGB(0x2b, 0x2b, 0x2b);
			c.emptybg = RGB(0x18, 0x18, 0x18);
			c.statusbg = RGB(0x18, 0x18, 0x18), c.statusfg = RGB(0xcc, 0xcc, 0xcc);
			c.statussep = RGB(0x3c, 0x3c, 0x3c);
			break;

		case beige:		// PC-8801 本体のようなアイボリー・ベージュに、こげ茶の字
			c.titlebg = RGB(0xcd, 0xbe, 0x9c), c.titlefg = RGB(0x3a, 0x2a, 0x18);
			c.titleibg = RGB(0xdd, 0xd2, 0xb8), c.titleifg = RGB(0x8a, 0x7a, 0x60);
			c.panebg = RGB(0xd8, 0xcc, 0xb0);
			c.tabfg = RGB(0x6a, 0x56, 0x3c);
			c.tabselbg = RGB(0xef, 0xe7, 0xd4), c.tabselfg = RGB(0x2e, 0x1f, 0x0e);
			c.tabaccent = RGB(0x9a, 0x5b, 0x2a);
			c.tabsep = RGB(0xb0, 0x9e, 0x78);
			c.stripfg = RGB(0x3a, 0x2a, 0x18), c.stripgray = RGB(0xa0, 0x90, 0x70);
			c.splitter = RGB(0xc4, 0xb4, 0x8e);
			c.emptybg = RGB(0xcf, 0xc2, 0xa4);
			c.statusbg = RGB(0xcd, 0xbe, 0x9c), c.statusfg = RGB(0x3a, 0x2a, 0x18);
			c.statussep = RGB(0xb0, 0x9e, 0x78);
			break;

		default:		// Windows の色
			c.titlebg = c.titleibg = GetSysColor(COLOR_3DFACE);		// タイトルの段は uititle.cpp が Windows の設定から決める
			c.titlefg = c.titleifg = GetSysColor(COLOR_BTNTEXT);
			c.panebg = GetSysColor(COLOR_3DFACE);
			c.tabfg = Mix(GetSysColor(COLOR_BTNTEXT), c.panebg, 100);
			c.tabselbg = GetSysColor(COLOR_WINDOW), c.tabselfg = GetSysColor(COLOR_WINDOWTEXT);
			c.tabaccent = GetSysColor(COLOR_HIGHLIGHT);
			c.tabsep = GetSysColor(COLOR_3DSHADOW);
			c.stripfg = GetSysColor(COLOR_MENUTEXT), c.stripgray = GetSysColor(COLOR_GRAYTEXT);
			c.splitter = GetSysColor(COLOR_3DFACE);
			c.emptybg = GetSysColor(COLOR_APPWORKSPACE);
			c.statusbg = GetSysColor(COLOR_3DFACE), c.statusfg = GetSysColor(COLOR_BTNTEXT);
			c.statussep = GetSysColor(COLOR_3DSHADOW);
			break;
		}
		return c;
	}
}
