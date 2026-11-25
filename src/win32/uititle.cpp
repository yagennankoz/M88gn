// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	本体の窓のタイトルバー: VS Code のように、メニュー・ペインの開閉ボタン・最小化/最大化/閉じるを 1 段に並べる
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	ウィンドウ表示の間は、Windows のタイトルバーを無くし (WM_NCCALCSIZE で窓の上の端まで中にする)、
//	窓の中の上の 1 段 (TitleHeight) を自分で描く。メニューバーは使わず (SetMenu(0))、メニューの名前を
//	押すと本体のメニュー (hmenumain) のサブメニューを TrackPopupMenu で出す。全画面の間は今までどおり
// ---------------------------------------------------------------------------

#include "headers.h"
#include "ui.h"
#include "resource.h"
#include "dpi.h"
#include "misc.h"
#include "dbgbuttons.h"
#include "status.h"
#include "messages.h"
#include "uitheme.h"

using namespace PC8801;

#ifndef SPI_GETFLATMENU
#define SPI_GETFLATMENU		0x1022
#endif
#ifndef WM_NCMOUSELEAVE
#define WM_NCMOUSELEAVE		0x02A2
#endif
#ifndef CLEARTYPE_QUALITY
#define CLEARTYPE_QUALITY	5
#endif
#ifndef SM_CXPADDEDBORDER
#define SM_CXPADDEDBORDER	92
#endif
#ifndef TME_NONCLIENT
#define TME_NONCLIENT		0x00000010
#endif

HHOOK WinUI::titlehook = 0;

// ---------------------------------------------------------------------------
//	大きさ (96 DPI 換算)
//
enum
{
	title_h = 32,			// 段の高さ
	title_iconw = 36,		// アイコンの欄
	title_menupad = 9,		// メニューの名前の左右の余白
	title_panew = 30,		// ペインの開閉ボタン
	title_captionw = 46,	// 最小化・最大化・閉じる
	title_gap = 8,
};

int WinUI::TitleScale(int v)
{
	return MulDiv(v, hwnd ? Dpi::GetForWindow(hwnd) : 96, 96);
}

bool WinUI::TitleActive()
{
	return hwnd && !fullscreen;
}

int WinUI::TitleHeight()
{
	return TitleActive() ? TitleScale(title_h) : 0;
}

// 窓の大きさ: 中の大きさ (タイトルの段を含む) から窓の大きさを求める。dpi が 0 なら今の窓の DPI
void WinUI::AdjustMainRect(RECT* r, UINT dpi)
{
	if (!TitleActive())
	{
		if (dpi)
			Dpi::AdjustWindowRectForDpi(r, wstyle, GetMenu(hwnd) != 0, 0, dpi);
		else
			Dpi::AdjustWindowRect(r, wstyle, GetMenu(hwnd) != 0, 0, hwnd);
		return;
	}
	// タイトルバーは無い (窓の上の端まで中)。左右・下の枠だけ
	int top = r->top;
	if (dpi)
		Dpi::AdjustWindowRectForDpi(r, wstyle, false, 0, dpi);
	else
		Dpi::AdjustWindowRect(r, wstyle, false, 0, hwnd);
	r->top = top;
}

// ---------------------------------------------------------------------------
//	色 (Windows の設定: タイトルバーにアクセントの色・暗いモード)
//
static DWORD RegDword(const char* key, const char* name, DWORD def)
{
	HKEY hk;
	DWORD v = def, size = sizeof(v), type = 0;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, key, 0, KEY_READ, &hk) == ERROR_SUCCESS)
	{
		if (RegQueryValueEx(hk, name, 0, &type, (BYTE*) &v, &size) != ERROR_SUCCESS || type != REG_DWORD)
			v = def;
		RegCloseKey(hk);
	}
	return v;
}

static bool AccentColor(COLORREF* c)
{
	typedef HRESULT (WINAPI* GetColorFn)(DWORD*, BOOL*);
	static HMODULE dwm = LoadLibrary("dwmapi.dll");
	static GetColorFn get = dwm ? (GetColorFn) GetProcAddress(dwm, "DwmGetColorizationColor") : 0;
	DWORD argb;
	BOOL opaque;
	if (!get || FAILED(get(&argb, &opaque)))
		return false;
	*c = RGB((argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff);
	return true;
}

static COLORREF Mix(COLORREF a, COLORREF b, int t)		// a に b を t/256 混ぜる
{
	return RGB(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t / 256,
			   GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t / 256,
			   GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t / 256);
}

void WinUI::TitleColors(COLORREF* bg, COLORREF* fg, COLORREF* hot, COLORREF* pushed)
{
	if (Theme::Get() != Theme::system)
	{
		// テーマの色 (M88gn)
		const Theme::Colors& c = Theme::C();
		*bg = titleactive ? c.titlebg : c.titleibg;
		*fg = titleactive ? c.titlefg : c.titleifg;
		*hot = Mix(*bg, c.titlefg, 30);
		*pushed = Mix(*bg, c.titlefg, 55);
		return;
	}
	bool dark = RegDword("Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", "AppsUseLightTheme", 1) == 0;
	bool accent = RegDword("Software\\Microsoft\\Windows\\DWM", "ColorPrevalence", 0) != 0;
	COLORREF ac;
	if (titleactive && accent && AccentColor(&ac))
	{
		*bg = ac;
		int lum = (GetRValue(ac) * 299 + GetGValue(ac) * 587 + GetBValue(ac) * 114) / 1000;
		*fg = lum > 140 ? RGB(0, 0, 0) : RGB(255, 255, 255);
	}
	else if (dark)
	{
		*bg = titleactive ? RGB(32, 32, 32) : RGB(43, 43, 43);
		*fg = titleactive ? RGB(255, 255, 255) : RGB(150, 150, 150);
	}
	else
	{
		*bg = titleactive ? RGB(243, 243, 243) : RGB(249, 249, 249);
		*fg = titleactive ? RGB(0, 0, 0) : RGB(140, 140, 140);
	}
	int lum = (GetRValue(*bg) * 299 + GetGValue(*bg) * 587 + GetBValue(*bg) * 114) / 1000;
	COLORREF over = lum > 140 ? RGB(0, 0, 0) : RGB(255, 255, 255);
	*hot = Mix(*bg, over, 26);
	*pushed = Mix(*bg, over, 46);
}

// ---------------------------------------------------------------------------
//	並び: アイコン・メニューの名前・(タイトル)・ペインの開閉・最小化/最大化/閉じる
//
void WinUI::TitleLayout()
{
	titleitems.clear();
	RECT c;
	GetClientRect(hwnd, &c);
	int h = TitleHeight();
	int x = 0;
	TitleItem it;
	// アイコン
	it.kind = ti_icon, it.index = 0;
	SetRect(&it.rect, 0, 0, TitleScale(title_iconw), h);
	titleitems.push_back(it);
	x = it.rect.right;
	// メニュー (隠す設定なら出さない)
	if (hmenumain && !HideMenuBar())
	{
		HDC dc = GetDC(hwnd);
		HGDIOBJ old = SelectObject(dc, TitleMenuFont());
		int n = GetMenuItemCount(hmenumain);
		for (int i = 0; i < n; i++)
		{
			char text[64];
			if (!GetSubMenu(hmenumain, i) || !GetMenuString(hmenumain, i, text, sizeof(text), MF_BYPOSITION))
				continue;
			RECT tr = { 0, 0, 0, 0 };
			DrawText(dc, text, -1, &tr, DT_CALCRECT | DT_SINGLELINE | DT_HIDEPREFIX);
			it.kind = ti_menu, it.index = i;
			SetRect(&it.rect, x, 0, x + tr.right + TitleScale(title_menupad) * 2, h);
			titleitems.push_back(it);
			x = it.rect.right;
		}
		SelectObject(dc, old);
		ReleaseDC(hwnd, dc);
	}
	// 右から: 閉じる・最大化・最小化
	int r = c.right;
	LONG_PTR st = GetWindowLongPtr(hwnd, GWL_STYLE);
	int cw = TitleScale(title_captionw);
	it.kind = ti_close, it.index = 0;
	SetRect(&it.rect, r - cw, 0, r, h), r -= cw;
	titleitems.push_back(it);
	if (st & WS_MAXIMIZEBOX)
	{
		it.kind = ti_max;
		SetRect(&it.rect, r - cw, 0, r, h), r -= cw;
		titleitems.push_back(it);
	}
	if (st & WS_MINIMIZEBOX)
	{
		it.kind = ti_min;
		SetRect(&it.rect, r - cw, 0, r, h), r -= cw;
		titleitems.push_back(it);
	}
	// ペインの開閉 (右から 右・下・上・左)
	r -= TitleScale(title_gap);
	static const int order[4] = { DockHost::right, DockHost::bottom, DockHost::top, DockHost::left };
	int pw = TitleScale(title_panew);
	for (int k = 0; k < 4; k++)
	{
		it.kind = ti_pane, it.index = order[k];
		SetRect(&it.rect, r - pw, 0, r, h), r -= pw;
		titleitems.push_back(it);
	}
	titlemenuend = x;
	titlebuttonsleft = r;
}

const WinUI::TitleItem* WinUI::TitleItemAt(POINT pt)
{
	if (pt.y < 0 || pt.y >= TitleHeight())
		return 0;
	if (titleitems.empty())
		TitleLayout();
	for (size_t i = 0; i < titleitems.size(); i++)
		if (PtInRect(&titleitems[i].rect, pt))
			return &titleitems[i];
	return 0;
}

HFONT WinUI::TitleMenuFont()
{
	UINT dpi = Dpi::GetForWindow(hwnd);
	if (!titlefont || titlefontdpi != dpi)
	{
		if (titlefont)
			DeleteObject(titlefont);
		if (titleglyph)
			DeleteObject(titleglyph);
		if (titlebold)
			DeleteObject(titlebold);
		NONCLIENTMETRICSW ncm;
		Dpi::GetNonClientMetrics(&ncm, dpi);
		titlefont = CreateFontIndirectW(&ncm.lfMenuFont);
		ncm.lfMenuFont.lfWeight = FW_BOLD;		// 窓の題は太字
		titlebold = CreateFontIndirectW(&ncm.lfMenuFont);
		// 最小化などの絵の字 (Windows 11 は Segoe Fluent Icons、10 は Segoe MDL2 Assets)
		LOGFONTW lf;
		memset(&lf, 0, sizeof(lf));
		lf.lfHeight = -MulDiv(10, dpi, 96);
		lf.lfCharSet = DEFAULT_CHARSET;
		lf.lfQuality = CLEARTYPE_QUALITY;
		lstrcpyW(lf.lfFaceName, L"Segoe Fluent Icons");
		titleglyph = CreateFontIndirectW(&lf);
		HDC dc = GetDC(hwnd);
		HGDIOBJ old = SelectObject(dc, titleglyph);
		WCHAR face[LF_FACESIZE] = L"";
		GetTextFaceW(dc, LF_FACESIZE, face);
		SelectObject(dc, old);
		ReleaseDC(hwnd, dc);
		if (lstrcmpiW(face, L"Segoe Fluent Icons"))
		{
			DeleteObject(titleglyph);
			lstrcpyW(lf.lfFaceName, L"Segoe MDL2 Assets");
			titleglyph = CreateFontIndirectW(&lf);
		}
		titlefontdpi = dpi;
	}
	return titlefont;
}

void WinUI::InvalidateTitle()
{
	if (!TitleActive())
		return;
	titleitems.clear();
	RECT r;
	GetClientRect(hwnd, &r);
	r.bottom = TitleHeight();
	InvalidateRect(hwnd, &r, FALSE);
}

// ---------------------------------------------------------------------------
//	描く
//
void WinUI::PaintTitle(HDC hdc)
{
	if (!TitleActive())
		return;
	TitleLayout();
	RECT c;
	GetClientRect(hwnd, &c);
	int w = c.right, h = TitleHeight();
	if (w <= 0 || h <= 0)
		return;
	HDC dc = CreateCompatibleDC(hdc);
	HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
	HGDIOBJ oldbmp = SelectObject(dc, bmp);
	COLORREF bg, fg, hot, pushed;
	TitleColors(&bg, &fg, &hot, &pushed);
	RECT all = { 0, 0, w, h };
	HBRUSH b = CreateSolidBrush(bg);
	FillRect(dc, &all, b);
	DeleteObject(b);
	SetBkMode(dc, TRANSPARENT);
	HGDIOBJ oldfont = SelectObject(dc, TitleMenuFont());

	for (size_t i = 0; i < titleitems.size(); i++)
	{
		const TitleItem& it = titleitems[i];
		bool ishot = titlehot == int(i);
		bool isdown = (it.kind == ti_menu && it.index == titlemenuopen) || (titlepressed == int(i) && ishot);
		COLORREF back = isdown ? pushed : ishot ? hot : bg;
		if (it.kind == ti_close && (ishot || titlepressed == int(i)))
			back = isdown ? RGB(0x94, 0x1e, 0x13) : RGB(0xc4, 0x2b, 0x1c);
		if (back != bg)
		{
			HBRUSH hb = CreateSolidBrush(back);
			FillRect(dc, &it.rect, hb);
			DeleteObject(hb);
		}
		RECT r = it.rect;
		switch (it.kind)
		{
		case ti_icon:
			{
				int s = TitleScale(16);
				HICON ic = (HICON) GetClassLongPtr(hwnd, GCLP_HICONSM);
				if (!ic)
					ic = (HICON) GetClassLongPtr(hwnd, GCLP_HICON);
				if (ic)
					DrawIconEx(dc, (r.left + r.right - s) / 2, (r.top + r.bottom - s) / 2, ic, s, s, 0, 0, DI_NORMAL);
			}
			break;
		case ti_menu:
			{
				char text[64];
				GetMenuString(hmenumain, it.index, text, sizeof(text), MF_BYPOSITION);
				SetTextColor(dc, fg);
				DrawText(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_HIDEPREFIX);
			}
			break;
		case ti_pane:
			{
				int s = Min(TitleScale(16), h);
				RECT ir = { (r.left + r.right - s) / 2, (r.top + r.bottom - s) / 2, 0, 0 };
				ir.right = ir.left + s, ir.bottom = ir.top + s;
				bool has = dock.HasWindows(it.index);
				COLORREF ic = has ? fg : Mix(fg, bg, 150);
				DebugButtons::DrawPaneIcon(dc, ir, it.index, has && dock.IsPaneShown(it.index), ic);
			}
			break;
		case ti_min:
		case ti_max:
		case ti_close:
			{
				WCHAR g = it.kind == ti_min ? 0xE921 : it.kind == ti_close ? 0xE8BB : IsZoomed(hwnd) ? 0xE923 : 0xE922;
				HGDIOBJ of = SelectObject(dc, titleglyph);
				SetTextColor(dc, (it.kind == ti_close && (ishot || titlepressed == int(i))) ? RGB(255, 255, 255) : fg);
				DrawTextW(dc, &g, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
				SelectObject(dc, of);
			}
			break;
		}
	}

	// タイトル (窓の題)。真ん中に、入らなければメニューの右に。それでも入らなければ出さない
	char text[128] = "";
	GetWindowText(hwnd, text, sizeof(text));
	if (text[0])
	{
		SelectObject(dc, titlebold);		// 太字
		RECT tr = { 0, 0, 0, 0 };
		DrawText(dc, text, -1, &tr, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
		int tw = tr.right;
		int pad = TitleScale(title_gap);
		int left = titlemenuend + pad, right = titlebuttonsleft - pad;
		int x = (w - tw) / 2;
		if (x < left)
			x = left;
		if (x + tw <= right)
		{
			RECT r = { x, 0, x + tw, h };
			SetTextColor(dc, fg);
			DrawText(dc, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
	}

	SelectObject(dc, oldfont);
	BitBlt(hdc, 0, 0, w, h, dc, 0, 0, SRCCOPY);
	SelectObject(dc, oldbmp);
	DeleteObject(bmp);
	DeleteDC(dc);
}

void WinUI::SetTitleHot(int hot, int pressed)
{
	if (hot == titlehot && pressed == titlepressed)
		return;
	titlehot = hot;
	titlepressed = pressed;
	RECT r;
	GetClientRect(hwnd, &r);
	r.bottom = TitleHeight();
	InvalidateRect(hwnd, &r, FALSE);
}

// ---------------------------------------------------------------------------
//	メニュー: 名前を押すとサブメニューを出す。出している間に別の名前へマウスを動かす・← → で
//	隣のメニューに移る (メニューバーと同じ)
//
LRESULT CALLBACK WinUI::TitleHookProc(int code, WPARAM wp, LPARAM lp)
{
	WinUI* ui = dockui;
	if (code == MSGF_MENU && ui && ui->titlemenuopen >= 0)
	{
		MSG* m = (MSG*) lp;
		if (m->message == WM_MOUSEMOVE || m->message == WM_NCMOUSEMOVE || m->message == WM_LBUTTONDOWN || m->message == WM_NCLBUTTONDOWN)
		{
			// メニューの間は ScreenToClient が DPI 非対応の座標で計算することがあるので、開く前に求めた位置で引く
			POINT pt = { m->pt.x - ui->titleorigin.x, m->pt.y - ui->titleorigin.y };
			const TitleItem* it = ui->TitleItemAt(pt);
			if (it && it->kind == ti_menu)
			{
				if (it->index != ui->titlemenuopen)
				{
					ui->titlenext = it->index;
					ui->titlenextkey = false;
					EndMenu();
					return TRUE;
				}
				if (m->message == WM_LBUTTONDOWN || m->message == WM_NCLBUTTONDOWN)
				{
					// 開いているメニューの名前をもう一度押した: 閉じる
					ui->titlenext = -1;
					EndMenu();
					return TRUE;
				}
			}
		}
		else if (m->message == WM_KEYDOWN && (m->wParam == VK_LEFT || m->wParam == VK_RIGHT))
		{
			HMENU sub = GetSubMenu(ui->hmenumain, ui->titlemenuopen);
			bool intop = ui->titlemenusel == sub;
			bool topopup = (ui->titlemenuselflags & MF_POPUP) != 0;
			if (intop && (m->wParam == VK_LEFT || !topopup))
			{
				int n = GetMenuItemCount(ui->hmenumain);
				int i = ui->titlemenuopen;
				for (int k = 0; k < n; k++)
				{
					i = (i + (m->wParam == VK_LEFT ? n - 1 : 1)) % n;
					if (GetSubMenu(ui->hmenumain, i))
						break;
				}
				ui->titlenext = i;
				ui->titlenextkey = true;
				EndMenu();
				return TRUE;
			}
		}
	}
	return CallNextHookEx(titlehook, code, wp, lp);
}

void WinUI::TrackTitleMenu(int index, bool keyboard)
{
	if (!TitleActive() || !hmenumain || titlemenuopen >= 0)
		return;
	keyif.Activate(false);
	SetGUIFlag(true);
	titlehook = SetWindowsHookEx(WH_MSGFILTER, TitleHookProc, 0, GetCurrentThreadId());
	SetForegroundWindow(hwnd);
	while (index >= 0)
	{
		HMENU sub = GetSubMenu(hmenumain, index);
		if (!sub)
			break;
		titlemenuopen = index;
		titlenext = -1;
		titlemenusel = 0;
		SetTitleHot(-1, -1);
		InvalidateTitle();
		UpdateWindow(hwnd);
		// メニューを開く前の処理 (チェックなど) は WM_INITMENU にある
		SendMessage(hwnd, WM_INITMENU, (WPARAM) hmenumain, 0);
		TitleLayout();
		RECT ir = { 0, 0, 0, 0 };
		for (size_t i = 0; i < titleitems.size(); i++)
			if (titleitems[i].kind == ti_menu && titleitems[i].index == index)
				ir = titleitems[i].rect;
		MapWindowPoints(hwnd, 0, (POINT*) &ir, 2);
		titleorigin.x = titleorigin.y = 0;
		ClientToScreen(hwnd, &titleorigin);
		if (keyboard)
			PostMessage(hwnd, WM_KEYDOWN, VK_DOWN, 0);		// 最初の項目を選んでおく
		TPMPARAMS tpm;
		tpm.cbSize = sizeof(tpm);
		tpm.rcExclude = ir;
		TrackPopupMenuEx(sub, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_VERTICAL, ir.left, ir.bottom, hwnd, &tpm);
		index = titlenext;
		keyboard = titlenextkey;
	}
	if (titlehook)
		UnhookWindowsHookEx(titlehook);
	titlehook = 0;
	titlemenuopen = -1;
	InvalidateTitle();
	PostMessage(hwnd, WM_NULL, 0, 0);
	SetGUIFlag(false);
	keyif.Activate(true);
}

// 下線の字 (&p など) でメニューを探す (-1: 無い)
int WinUI::TitleMenuByMnemonic(char c)
{
	if (!hmenumain)
		return -1;
	int n = GetMenuItemCount(hmenumain);
	for (int i = 0; i < n; i++)
	{
		char text[64];
		if (!GetSubMenu(hmenumain, i) || !GetMenuString(hmenumain, i, text, sizeof(text), MF_BYPOSITION))
			continue;
		const char* a = strchr(text, '&');
		if (a && tolower((unsigned char) a[1]) == tolower((unsigned char) c))
			return i;
	}
	return -1;
}

// ---------------------------------------------------------------------------
//	メッセージ (WinProc の初めに呼ぶ)。処理したら true を返し、*ret に結果
//
bool WinUI::TitleMessage(HWND hw, UINT msg, WPARAM wp, LPARAM lp, LRESULT* ret)
{
	switch (msg)
	{
	case WM_NCCALCSIZE:
		if (wp && TitleActive())
		{
			// Windows のタイトルバーを無くす: 上の端まで中にする (最大化なら画面の外にはみ出す枠の分だけ下げる)
			NCCALCSIZE_PARAMS* p = (NCCALCSIZE_PARAMS*) lp;
			int top = p->rgrc[0].top;
			*ret = DefWindowProc(hw, msg, wp, lp);
			p->rgrc[0].top = top;
			if (IsZoomed(hw))
			{
				UINT dpi = Dpi::GetForWindow(hw);
				p->rgrc[0].top += Dpi::GetSystemMetrics(SM_CYFRAME, dpi) + Dpi::GetSystemMetrics(SM_CXPADDEDBORDER, dpi);
			}
			return true;
		}
		break;

	case WM_NCHITTEST:
		if (TitleActive())
		{
			LRESULT h = DefWindowProc(hw, msg, wp, lp);
			if (h != HTCLIENT)
			{
				*ret = h;
				return true;
			}
			POINT pt = { (short) LOWORD(lp), (short) HIWORD(lp) };
			ScreenToClient(hw, &pt);
			if (pt.y >= TitleHeight())
			{
				*ret = HTCLIENT;
				return true;
			}
			// 上の端: 大きさを変えられる窓なら、枠と同じに
			LONG_PTR st = GetWindowLongPtr(hw, GWL_STYLE);
			if ((st & WS_THICKFRAME) && !IsZoomed(hw))
			{
				UINT dpi = Dpi::GetForWindow(hw);
				int fr = Dpi::GetSystemMetrics(SM_CYFRAME, dpi) + Dpi::GetSystemMetrics(SM_CXPADDEDBORDER, dpi);
				if (pt.y < fr)
				{
					RECT c;
					GetClientRect(hw, &c);
					*ret = pt.x < fr * 2 ? HTTOPLEFT : pt.x >= c.right - fr * 2 ? HTTOPRIGHT : HTTOP;
					return true;
				}
			}
			const TitleItem* it = TitleItemAt(pt);
			*ret = !it ? HTCAPTION
				 : it->kind == ti_icon ? HTSYSMENU
				 : it->kind == ti_min ? HTMINBUTTON
				 : it->kind == ti_max ? HTMAXBUTTON
				 : it->kind == ti_close ? HTCLOSE
				 : HTCLIENT;
			return true;
		}
		break;

	case WM_NCACTIVATE:
		if (TitleActive())
		{
			titleactive = wp != 0;
			titlehot = titlepressed = -1;
			*ret = DefWindowProc(hw, msg, wp, -1);		// 古いタイトルバーを描かせない
			InvalidateTitle();
			return true;
		}
		titleactive = wp != 0;
		break;

	case WM_SETTEXT:
	case WM_SETTINGCHANGE:
	case WM_SYSCOLORCHANGE:
	case 0x0320:		// WM_DWMCOLORIZATIONCOLORCHANGED
		if (TitleActive())
		{
			*ret = DefWindowProc(hw, msg, wp, lp);
			InvalidateTitle();
			return msg == WM_SETTEXT;		// ほかは WinProc でも処理する
		}
		break;

	case WM_PAINT:
		if (TitleActive())
		{
			RECT r;
			if (GetUpdateRect(hw, &r, FALSE) && r.top < TitleHeight())
			{
				HDC dc = GetDC(hw);
				PaintTitle(dc);
				ReleaseDC(hw, dc);
				RECT t = { 0, 0, 0, TitleHeight() };
				RECT c;
				GetClientRect(hw, &c);
				t.right = c.right;
				ValidateRect(hw, &t);
			}
		}
		break;

	case WM_SIZE:
		// 最小化・最大化の後は、ボタンから離れたことが知らされないことがある
		titlehot = titlepressed = -1;
		break;

	case WM_MENUSELECT:
		titlemenusel = (HMENU) lp;
		titlemenuselflags = HIWORD(wp);
		break;

	// 最小化・最大化・閉じる (枠の外の扱い)
	case WM_NCMOUSEMOVE:
	case WM_NCLBUTTONDOWN:
	case WM_NCLBUTTONUP:
	case WM_NCLBUTTONDBLCLK:
		if (TitleActive())
		{
			POINT pt = { (short) LOWORD(lp), (short) HIWORD(lp) };
			ScreenToClient(hw, &pt);
			const TitleItem* it = TitleItemAt(pt);
			int idx = it ? int(it - &titleitems[0]) : -1;
			bool button = it && (it->kind == ti_min || it->kind == ti_max || it->kind == ti_close);
			if (msg == WM_NCMOUSEMOVE)
			{
				TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE | TME_NONCLIENT, hw, 0 };
				TrackMouseEvent(&tme);
				SetTitleHot(button ? idx : -1, titlepressed);
				break;		// 既定の処理も (Windows 11 のスナップのレイアウトなど)
			}
			if (!button)
				break;
			if (msg == WM_NCLBUTTONDOWN || msg == WM_NCLBUTTONDBLCLK)
				SetTitleHot(idx, idx);
			else
			{
				bool hit = titlepressed == idx;
				SetTitleHot(idx, -1);
				if (hit)
				{
					UINT sc = it->kind == ti_min ? SC_MINIMIZE : it->kind == ti_close ? SC_CLOSE
							: IsZoomed(hw) ? SC_RESTORE : SC_MAXIMIZE;
					PostMessage(hw, WM_SYSCOMMAND, sc, 0);
				}
			}
			*ret = 0;
			return true;
		}
		break;

	case WM_NCMOUSELEAVE:
		if (TitleActive())
		{
			// メニュー・ペインのボタン (中の扱い) の上に移ったときも来るので、そちらの印は残す
			if (titlehot >= 0 && titlehot < int(titleitems.size()))
			{
				int k = titleitems[titlehot].kind;
				if (k == ti_min || k == ti_max || k == ti_close)
					SetTitleHot(-1, -1);
			}
		}
		break;

	// メニューの名前・ペインのボタン (中の扱い)
	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
	case WM_LBUTTONUP:
		if (TitleActive())
		{
			POINT pt = { (short) LOWORD(lp), (short) HIWORD(lp) };
			bool intitle = pt.y >= 0 && pt.y < TitleHeight();
			if (msg == WM_LBUTTONUP && titlepressed >= 0 && GetCapture() == hw)
				ReleaseCapture();
			if (!intitle)
			{
				if (titlehot >= 0 || titlepressed >= 0)
					SetTitleHot(-1, -1);
				if (msg == WM_LBUTTONUP && titlepressedwas)
				{
					titlepressedwas = false;
					*ret = 0;
					return true;
				}
				break;
			}
			const TitleItem* it = TitleItemAt(pt);
			int idx = it ? int(it - &titleitems[0]) : -1;
			bool clickable = it && (it->kind == ti_menu || it->kind == ti_pane);
			if (msg == WM_MOUSEMOVE)
			{
				TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
				TrackMouseEvent(&tme);
				SetTitleHot(clickable ? idx : -1, titlepressed);
			}
			else if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK)
			{
				if (it && it->kind == ti_menu)
					TrackTitleMenu(it->index, false);
				else if (it && it->kind == ti_pane)
				{
					SetCapture(hw);
					titlepressedwas = true;
					SetTitleHot(idx, idx);
				}
			}
			else
			{
				titlepressedwas = false;
				bool hit = clickable && titlepressed == idx && it->kind == ti_pane;
				SetTitleHot(clickable ? idx : -1, -1);
				if (hit)
				{
					static const UINT ids[4] = { IDM_DOCKBTN_LEFT, IDM_DOCKBTN_RIGHT, IDM_DOCKBTN_TOP, IDM_DOCKBTN_BOTTOM };
					PostMessage(hw, WM_COMMAND, ids[it->index], 0);
				}
			}
			*ret = 0;
			return true;
		}
		break;

	case WM_MOUSELEAVE:
		if (TitleActive() && titlehot >= 0)
		{
			int k = titlehot < int(titleitems.size()) ? titleitems[titlehot].kind : -1;
			if (k == ti_menu || k == ti_pane)
				SetTitleHot(-1, titlepressed);
		}
		break;

	case WM_M88_TITLEMENU:
		TrackTitleMenu((int) wp, lp != 0);
		*ret = 0;
		return true;
	}
	return false;
}
