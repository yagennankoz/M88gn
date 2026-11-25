// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	デバッグの窓のメニューバーの Pause/Play/Step In/Step Over/Step Out のボタン
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	コードの窓にあったものを分けて、コード・メモリ・Video の窓で使う。
//	メニューバーの右に、絵で描いた項目として並べる (押すと本体の Debug メニューと同じ処理)
// ---------------------------------------------------------------------------

#include "headers.h"
#include <commctrl.h>
#include "resource.h"
#include "wincore.h"
#include "dbgbuttons.h"
#include "uitheme.h"
#include "misc.h"
#include "dpi.h"

using namespace PC8801;

#ifndef WM_DPICHANGED
#define WM_DPICHANGED	0x02e0
#endif
#ifndef SPI_GETFLATMENU
#define SPI_GETFLATMENU		0x1022
#endif
#ifndef COLOR_MENUHILIGHT
#define COLOR_MENUHILIGHT	29
#define COLOR_MENUBAR		30
#endif
#ifndef WM_NCMOUSELEAVE
#define WM_NCMOUSELEAVE	0x02a2
#endif

enum { tiptimerid = 0x4d42, tipdelay = 400 };

// ---------------------------------------------------------------------------
//	メニューバーの Pause/Play/Step の絵 (M88gn)
//	Visual Studio のデバッグのツールバーと同じ意味の形を、16x16 の格子の座標で描く。
//	4 倍の大きさで白黒に描いてから縮め (重なった割合を色の濃さにする)、縁を滑らかにする
//
namespace
{
	enum { iconunit = 16, iconss = 4 };

	struct ButtonDef
	{
		uint id;
		const char* name;
		COLORREF color;		// 主な形の色 (part 0)。part 1 (点) はいつも濃い灰色
	};
	const ButtonDef buttondefs[] =
	{
		{ IDM_DBG_PAUSE,    "Pause",     RGB(0x00, 0x53, 0x9c) },
		{ IDM_DBG_PLAY,     "Play",      RGB(0x38, 0x8a, 0x34) },
		{ IDM_DBG_STEPIN,   "Step In",   RGB(0x00, 0x53, 0x9c) },
		{ IDM_DBG_STEPOVER, "Step Over", RGB(0x00, 0x53, 0x9c) },
		{ IDM_DBG_STEPOUT,  "Step Out",  RGB(0x00, 0x53, 0x9c) },
	};
	const COLORREF dotcolor = RGB(0x42, 0x42, 0x42);
	const COLORREF graycolor = RGB(0xa0, 0xa0, 0xa0);

	const ButtonDef* FindButton(uint id)
	{
		for (int i = 0; i < sizeof(buttondefs) / sizeof(buttondefs[0]); i++)
			if (buttondefs[i].id == id)
				return &buttondefs[i];
		return 0;
	}

	// 格子の座標 (16 で 1 辺) を描く DC の座標に
	struct IconPen
	{
		double u;
		int X(double x) const { return int(x * u + 0.5); }
	};

	void IconPolygon(HDC dc, const IconPen& p, const double* xy, int n)
	{
		POINT pt[8];
		for (int i = 0; i < n; i++)
			pt[i].x = p.X(xy[i * 2]), pt[i].y = p.X(xy[i * 2 + 1]);
		Polygon(dc, pt, n);
	}

	void IconDot(HDC dc, const IconPen& p, double cx, double cy, double r)
	{
		Ellipse(dc, p.X(cx - r), p.X(cy - r), p.X(cx + r), p.X(cy + r));
	}

	// part 0 = 色の付いた形、part 1 = 点 (ステップの 3 つだけ)。白で描く
	void DrawIconPart(HDC dc, uint id, int part, const IconPen& p)
	{
		LOGBRUSH lb = { BS_SOLID, RGB(255, 255, 255), 0 };
		HPEN thick = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER,
								  p.X(2.0), &lb, 0, 0);
		HPEN nopen = (HPEN) GetStockObject(NULL_PEN);
		HBRUSH white = (HBRUSH) GetStockObject(WHITE_BRUSH);
		HGDIOBJ oldpen = SelectObject(dc, nopen);
		HGDIOBJ oldbrush = SelectObject(dc, white);

		if (part == 1)
		{
			if (id == IDM_DBG_STEPIN || id == IDM_DBG_STEPOVER || id == IDM_DBG_STEPOUT)
				IconDot(dc, p, 8, 13.5, 2);
		}
		else switch (id)
		{
		case IDM_DBG_PAUSE:
			{
				RECT r1 = { p.X(3.5), p.X(2.5), p.X(6.5), p.X(13.5) };
				RECT r2 = { p.X(9.5), p.X(2.5), p.X(12.5), p.X(13.5) };
				FillRect(dc, &r1, white);
				FillRect(dc, &r2, white);
			}
			break;

		case IDM_DBG_PLAY:
			{
				static const double tri[] = { 4, 2, 14, 8, 4, 14 };
				IconPolygon(dc, p, tri, 3);
			}
			break;

		case IDM_DBG_STEPIN:		// 点に向かって下りる矢印
			{
				SelectObject(dc, thick);
				MoveToEx(dc, p.X(8), p.X(0.5), 0);
				LineTo(dc, p.X(8), p.X(7));
				SelectObject(dc, nopen);
				static const double head[] = { 4.5, 6, 11.5, 6, 8, 10 };
				IconPolygon(dc, p, head, 3);
			}
			break;

		case IDM_DBG_STEPOVER:		// 点をまたぐ弧の矢印
			{
				SelectObject(dc, thick);
				int olddir = SetArcDirection(dc, AD_CLOCKWISE);
				Arc(dc, p.X(2.5), p.X(3), p.X(13.5), p.X(14),
					p.X(2.5), p.X(8.5), p.X(13.5), p.X(8.5));
				SetArcDirection(dc, olddir);
				SelectObject(dc, nopen);
				static const double head[] = { 9.5, 7.5, 16, 7.5, 12.75, 11.5 };
				IconPolygon(dc, p, head, 3);
			}
			break;

		case IDM_DBG_STEPOUT:		// 点から上がる矢印
			{
				SelectObject(dc, thick);
				MoveToEx(dc, p.X(8), p.X(10.5), 0);
				LineTo(dc, p.X(8), p.X(5));
				SelectObject(dc, nopen);
				static const double head[] = { 4.5, 6, 11.5, 6, 8, 1.5 };
				IconPolygon(dc, p, head, 3);
			}
			break;
		}
		SelectObject(dc, oldpen);
		SelectObject(dc, oldbrush);
		DeleteObject(thick);
	}

	// 32bit の DIB (上から下へ) を作る
	HBITMAP CreateDib(HDC dc, int w, int h, void** bits)
	{
		BITMAPINFO bi;
		memset(&bi, 0, sizeof(bi));
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = w;
		bi.bmiHeader.biHeight = -h;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		return CreateDIBSection(dc, &bi, DIB_RGB_COLORS, bits, 0, 0);
	}

	// size x size の絵を、bits (32bit、幅 pitch 画素) の (ox, oy) に重ねる
	void CompositeIcon(HDC dc, uint id, int size, bool gray, uint32* bits, int pitch, int ox, int oy)
	{
		int big = size * iconss;
		void* mbits;
		HBITMAP mbmp = CreateDib(dc, big, big, &mbits);
		if (!mbmp)
			return;
		HDC mdc = CreateCompatibleDC(dc);
		HGDIOBJ old = SelectObject(mdc, mbmp);
		IconPen pen = { double(big) / iconunit };
		const ButtonDef* def = FindButton(id);

		for (int part = 0; part < 2; part++)
		{
			RECT all = { 0, 0, big, big };
			FillRect(mdc, &all, (HBRUSH) GetStockObject(BLACK_BRUSH));
			DrawIconPart(mdc, id, part, pen);
			GdiFlush();

			COLORREF c = gray ? graycolor : part ? dotcolor : def->color;
			int cr = GetRValue(c), cg = GetGValue(c), cb = GetBValue(c);
			const uint32* m = (const uint32*) mbits;
			for (int y = 0; y < size; y++)
			{
				for (int x = 0; x < size; x++)
				{
					int sum = 0;
					for (int sy = 0; sy < iconss; sy++)
						for (int sx = 0; sx < iconss; sx++)
							sum += m[(y * iconss + sy) * big + x * iconss + sx] & 0xff;
					if (!sum)
						continue;
					int a = sum / (iconss * iconss);		// 0～255
					uint32& d = bits[(oy + y) * pitch + ox + x];
					int dr = (d >> 16) & 0xff, dg = (d >> 8) & 0xff, db = d & 0xff;
					dr += (cr - dr) * a / 255;
					dg += (cg - dg) * a / 255;
					db += (cb - db) * a / 255;
					d = (dr << 16) | (dg << 8) | db;
				}
			}
		}
		SelectObject(mdc, old);
		DeleteDC(mdc);
		DeleteObject(mbmp);
	}

	// メニューバーの背景と項目の状態を、テーマ (uxtheme) で描く。テーマが無ければシステムの色
	typedef HANDLE (WINAPI* OpenThemeDataFn)(HWND, LPCWSTR);
	typedef HRESULT (WINAPI* DrawThemeBackgroundFn)(HANDLE, HDC, int, int, const RECT*, const RECT*);
	typedef HRESULT (WINAPI* CloseThemeDataFn)(HANDLE);

	enum
	{
		MENU_BARBACKGROUND_ = 7, MB_ACTIVE_ = 1, MB_INACTIVE_ = 2,
		MENU_BARITEM_ = 8, MBI_HOT_ = 2, MBI_PUSHED_ = 3,
	};

	void DrawMenuBarBack(HWND hwnd, HDC dc, const RECT& r, bool active, bool hot, bool pushed)
	{
		if (Theme::stripdrawing)
		{
			// ペインのメニューの帯 (M88gn): テーマの色
			const Theme::Colors& c = Theme::C();
			HBRUSH b = CreateSolidBrush(pushed ? Theme::Mix(c.tabselbg, c.stripfg, 60) : hot ? Theme::Mix(c.tabselbg, c.stripfg, 30) : c.tabselbg);
			FillRect(dc, &r, b);
			DeleteObject(b);
			return;
		}
		static HMODULE uxtheme = LoadLibrary("uxtheme.dll");
		static OpenThemeDataFn open = uxtheme ? (OpenThemeDataFn) GetProcAddress(uxtheme, "OpenThemeData") : 0;
		static DrawThemeBackgroundFn draw = uxtheme ? (DrawThemeBackgroundFn) GetProcAddress(uxtheme, "DrawThemeBackground") : 0;
		static CloseThemeDataFn close = uxtheme ? (CloseThemeDataFn) GetProcAddress(uxtheme, "CloseThemeData") : 0;

		HANDLE theme = (open && draw && close) ? open(hwnd, L"MENU") : 0;
		if (theme)
		{
			draw(theme, dc, MENU_BARBACKGROUND_, active ? MB_ACTIVE_ : MB_INACTIVE_, &r, 0);
			if (hot || pushed)
				draw(theme, dc, MENU_BARITEM_, pushed ? MBI_PUSHED_ : MBI_HOT_, &r, 0);
			close(theme);
			return;
		}
		BOOL flat = FALSE;
		SystemParametersInfo(SPI_GETFLATMENU, 0, &flat, 0);
		FillRect(dc, &r, GetSysColorBrush((hot || pushed) ? COLOR_MENUHILIGHT : flat ? COLOR_MENUBAR : COLOR_MENU));
	}

	int ButtonIconSize(HWND hwnd)
	{
		return MulDiv(16, Dpi::GetForWindow(hwnd), 96);
	}
}



// ---------------------------------------------------------------------------
//	構築
//
DebugButtons::DebugButtons()
: core(0), hwnd(0), hinst(0), state(-1), inlayout(false), layoutwidth(-1),
  hwndtip(0), tipid(0), tipshown(false), tiptracking(false), menu(0)
{
}

bool DebugButtons::IsButton(uint id)
{
	return FindButton(id) != 0;
}

// ---------------------------------------------------------------------------
//	窓が開いた直後に呼ぶ。メニューバーの右にボタンを並べ、押せる・押せないを合わせる
//
void DebugButtons::Attach(HWND h, HINSTANCE hi)
{
	hwnd = h;
	hinst = hi;
	SetOwnerDraw();
	state = -1;
	Update();
	layoutwidth = -1;
	Layout();
}

// ---------------------------------------------------------------------------
//	窓の処理から呼ぶ。WM_MEASUREITEM・WM_DRAWITEM (ボタンのもの)・ボタンの WM_COMMAND・
//	ツールチップのタイマーを処理したら true (ダイアログなら DWLP_MSGRESULT に TRUE を入れて TRUE を返す)
//
bool DebugButtons::Message(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	if (!hwnd || h != hwnd)
		return false;
	switch (msg)
	{
	case WM_COMMAND:
		// 本体の Debug メニューと同じ処理に回す
		if (FindButton(LOWORD(wp)))
		{
			HWND owner = GetWindow(hwnd, GW_OWNER);
			if (owner)
				PostMessage(owner, WM_COMMAND, LOWORD(wp), 0);
			return true;
		}
		break;

	case WM_SIZE:
		Layout();
		break;

	case WM_DPICHANGED:
		layoutwidth = -1;		// メニューの字の大きさが変わる (この後の WM_SIZE で並べ直す)
		SetOwnerDraw();			// 絵の大きさを測り直させる
		break;

	case WM_MEASUREITEM:
		return Measure((MEASUREITEMSTRUCT*) lp);

	case WM_DRAWITEM:
		return Draw((const DRAWITEMSTRUCT*) lp);

	case WM_NCMOUSEMOVE:
		{
			POINT pt = { (short) LOWORD(lp), (short) HIWORD(lp) };
			if (wp == HTMENU)
				MouseMove(pt);
			else if (tipid)
				ShowTip(false), tipid = 0;
		}
		break;

	case WM_NCMOUSELEAVE:
		tiptracking = false;
		ShowTip(false);
		tipid = 0;
		break;

	case WM_MOUSEMOVE:
	case WM_NCLBUTTONDOWN:
	case WM_ENTERMENULOOP:
		if (tipid)
			ShowTip(false), tipid = 0;
		break;

	case WM_TIMER:
		if (wp == tiptimerid)
		{
			KillTimer(hwnd, tiptimerid);
			if (tipid)
				ShowTip(true);
			return true;
		}
		break;

	case WM_DESTROY:
		hwndtip = 0;			// 持ち主の窓といっしょに壊れる
		tipid = 0;
		tipshown = false;
		tiptracking = false;
		hwnd = 0;
		break;
	}
	return false;
}

// ---------------------------------------------------------------------------
//	メニューバーの Pause/Play/Step を自分で描く項目にし、ツールチップを作る (M88gn)。
//	.rc の字は、ここで外すまでの控え。窓を開くたびにメニューは .rc から作り直されるので、
//	開くたびに呼ぶ。DPI が変わったときも呼ぶ (項目の大きさを測り直させる)
//
void DebugButtons::SetOwnerDraw()
{
		HMENU hmenu = Menu();
	if (!hmenu)
		return;

	// 項目の種類を書き換えるだけでは、Windows は前に測った幅 (字の幅) を使い続ける
	// (WM_MEASUREITEM が最初の項目にしか来ない) ので、いったん外して入れ直す。
	// 最初の項目 (Pause) は右寄せ (MFT_RIGHTJUSTIFY)。折り返し (MFT_MENUBREAK) は LayoutMenuBar が付け直す
	for (int i = 0; i < sizeof(buttondefs) / sizeof(buttondefs[0]); i++)
		RemoveMenu(hmenu, buttondefs[i].id, MF_BYCOMMAND);
	for (int i = 0; i < sizeof(buttondefs) / sizeof(buttondefs[0]); i++)
	{
		MENUITEMINFO mii;
		memset(&mii, 0, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA;
		mii.fType = MFT_OWNERDRAW | (i == 0 ? MFT_RIGHTJUSTIFY : 0);
		mii.wID = buttondefs[i].id;
		InsertMenuItem(hmenu, GetMenuItemCount(hmenu), TRUE, &mii);
	}
	DrawMenuBar(hwnd);
	layoutwidth = -1;
	state = -1;

	if (!hwndtip)
	{
		hwndtip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, 0, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
								 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
								 hwnd, 0, hinst, 0);
		if (hwndtip)
		{
			TOOLINFO ti;
			memset(&ti, 0, sizeof(ti));
			ti.cbSize = TTTOOLINFOA_V1_SIZE;
			ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
			ti.hwnd = hwnd;
			ti.uId = 1;
			ti.lpszText = (LPSTR) "";
			SendMessage(hwndtip, TTM_ADDTOOL, 0, (LPARAM) &ti);
		}
	}
	tipid = 0;
	tipshown = false;
}

// 窓のメニュー。ドックモードでペインに入れると窓からは外れるので、覚えておいたもの (M88gn)
HMENU DebugButtons::Menu()
{
	HMENU m = hwnd ? ::GetMenu(hwnd) : 0;
	if (m)
		menu = m;
	return menu;
}

void DebugButtons::PaintBarBack(HWND hwnd, HDC dc, const RECT& r, bool active)
{
	DrawMenuBarBack(hwnd, dc, r, active, false, false);
}

// ---------------------------------------------------------------------------
//	本体のメニューバーのペインの開閉ボタン (M88gn)。窓の形の四角の中に、
//	そのペインの所を塗る (開いているとき) か、仕切りの線だけ (閉じているとき) を描く
//
void DebugButtons::DrawPaneIcon(HDC dc, const RECT& ir, int side, bool shown, COLORREF c)
{
	int size = ir.right - ir.left;
	int ox = ir.left, oy = ir.top;
	int lw = Max(1, (size + 8) / 16);		// 線の太さ
	HBRUSH br = CreateSolidBrush(c);
	auto X = [&](int v) { return ox + MulDiv(v, size, 16); };
	auto Y = [&](int v) { return oy + MulDiv(v, size, 16); };
	RECT o = { X(1), Y(2), X(15), Y(14) };		// 窓の形
	auto fill = [&](int l, int t, int rr, int b) { RECT f = { l, t, rr, b }; FillRect(dc, &f, br); };
	fill(o.left, o.top, o.right, o.top + lw);
	fill(o.left, o.bottom - lw, o.right, o.bottom);
	fill(o.left, o.top, o.left + lw, o.bottom);
	fill(o.right - lw, o.top, o.right, o.bottom);
	RECT p = o;		// ペインの所
	switch (side)
	{
	case 0:	p.right = X(6); break;
	case 1:	p.left = X(10); break;
	case 2:	p.bottom = Y(6); break;
	default: p.top = Y(10); break;
	}
	if (shown)
		FillRect(dc, &p, br);
	else
	{
		// 仕切りの線
		switch (side)
		{
		case 0:	fill(p.right - lw, o.top, p.right, o.bottom); break;
		case 1:	fill(p.left, o.top, p.left + lw, o.bottom); break;
		case 2:	fill(o.left, p.bottom - lw, o.right, p.bottom); break;
		default: fill(o.left, p.top, o.right, p.top + lw); break;
		}
	}
	DeleteObject(br);
}

bool DebugButtons::Measure(MEASUREITEMSTRUCT* mis)
{
	if (mis->CtlType != ODT_MENU || !FindButton(mis->itemID))
		return false;
	int size = ButtonIconSize(hwnd);
	mis->itemWidth = size;		// 左右の余白は Windows が足す
	mis->itemHeight = size;
	return true;
}

bool DebugButtons::Draw(const DRAWITEMSTRUCT* dis)
{
	if (dis->CtlType != ODT_MENU || !FindButton(dis->itemID))
		return false;

	RECT r = dis->rcItem;
	int w = r.right - r.left, h = r.bottom - r.top;
	if (w <= 0 || h <= 0)
		return true;

	// 背景と絵を作ってから一度に写す (ちらつかないように)
	void* bits;
	HBITMAP bmp = CreateDib(dis->hDC, w, h, &bits);
	if (!bmp)
		return true;
	HDC mdc = CreateCompatibleDC(dis->hDC);
	HGDIOBJ old = SelectObject(mdc, bmp);

	bool gray = (dis->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;
	bool active = !(dis->itemState & ODS_INACTIVE);
	bool pushed = !gray && (dis->itemState & ODS_SELECTED);
	bool hot = !gray && (dis->itemState & ODS_HOTLIGHT);
	RECT lr = { 0, 0, w, h };
	DrawMenuBarBack(hwnd, mdc, lr, active, hot, pushed);
	GdiFlush();

	int size = Min(ButtonIconSize(hwnd), Min(w, h));
	CompositeIcon(mdc, dis->itemID, size, gray, (uint32*) bits, w, (w - size) / 2, (h - size) / 2);

	BitBlt(dis->hDC, r.left, r.top, w, h, mdc, 0, 0, SRCCOPY);
	SelectObject(mdc, old);
	DeleteDC(mdc);
	DeleteObject(bmp);
	return true;
}

// ---------------------------------------------------------------------------
//	メニューバーの上でマウスが動いた (M88gn)。絵の項目に載って少したったらツールチップを出す。
//	字は本体の Debug メニューの字 (ショートカットの表示が Alt+ / App+ に合う) から作る
//
void DebugButtons::MouseMove(POINT screen)
{
		HMENU hmenu = Menu();
	int pos = hmenu ? MenuItemFromPoint(hwnd, hmenu, screen) : -1;
	uint id = pos >= 0 ? GetMenuItemID(hmenu, pos) : 0;
	if (!FindButton(id))
		id = 0;

	if (!tiptracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE | TME_NONCLIENT, hwnd, 0 };
		tiptracking = !!TrackMouseEvent(&tme);
	}
	if (id == tipid)
		return;
	ShowTip(false);
	tipid = id;
	if (id)
		SetTimer(hwnd, tiptimerid, tipdelay, 0);
}

void DebugButtons::ShowTip(bool show)
{
		if (!show)
	{
		KillTimer(hwnd, tiptimerid);
		if (hwndtip && tipshown)
			SendMessage(hwndtip, TTM_TRACKACTIVATE, FALSE, 0);
		tipshown = false;
		return;
	}
	const ButtonDef* def = FindButton(tipid);
	HMENU hmenu = Menu();
	if (!hwndtip || !def || !hmenu)
		return;

	// 本体の Debug メニューの字 ("Step &Over\tF10" など) からショートカットを取り出す
	char text[128];
	lstrcpyn(text, def->name, sizeof(text));
	HWND owner = GetWindow(hwnd, GW_OWNER);
	char label[128];
	HMENU ownermenu = owner ? (HMENU) GetProp(owner, "M88gnMainMenu") : 0;	// 本体はメニューバーを使っていない (M88gn)
	if (!ownermenu && owner)
		ownermenu = GetMenu(owner);
	if (ownermenu && GetMenuString(ownermenu, def->id, label, sizeof(label), MF_BYCOMMAND) > 0)
	{
		char* tab = strchr(label, '\t');
		if (tab && tab[1])
			wsprintf(text, "%s (%s)", def->name, tab + 1);
	}

	TOOLINFO ti;
	memset(&ti, 0, sizeof(ti));
	ti.cbSize = TTTOOLINFOA_V1_SIZE;
	ti.hwnd = hwnd;
	ti.uId = 1;
	ti.lpszText = text;
	SendMessage(hwndtip, TTM_UPDATETIPTEXT, 0, (LPARAM) &ti);

	int n = GetMenuItemCount(hmenu);
	for (int pos = 0; pos < n; pos++)
	{
		if (GetMenuItemID(hmenu, pos) != tipid)
			continue;
		RECT r;
		if (GetMenuItemRect(hwnd, hmenu, pos, &r))
		{
			SendMessage(hwndtip, TTM_TRACKPOSITION, 0, MAKELPARAM(r.left, r.bottom + 2));
			SendMessage(hwndtip, TTM_TRACKACTIVATE, TRUE, (LPARAM) &ti);
			tipshown = true;
		}
		break;
	}
}

// ---------------------------------------------------------------------------
//	メニューバーが 1 段に収まらないとき、Windows に任せると項目の途中で折り返すので、
//	Pause の前 (MFT_MENUBREAK) で折り返す (M88gn)。折り返しを外して並べ直させ、
//	最後の項目が最初の項目と違う段に来たら、Pause に折り返しを付ける
//
void DebugButtons::Layout()
{
		if (!hwnd || inlayout)
		return;
	HMENU hmenu = Menu();
	if (!hmenu)
		return;

	RECT wr;
	GetWindowRect(hwnd, &wr);
	int width = wr.right - wr.left;
	if (width == layoutwidth)
		return;
	layoutwidth = width;

	int n = GetMenuItemCount(hmenu);
	int pos;
	for (pos = 0; pos < n; pos++)
		if (GetMenuItemID(hmenu, pos) == IDM_DBG_PAUSE)
			break;
	if (pos >= n)
		return;

	inlayout = true;

	MENUITEMINFO mii;
	memset(&mii, 0, sizeof(mii));
	mii.cbSize = sizeof(mii);
	mii.fMask = MIIM_FTYPE;
	GetMenuItemInfo(hmenu, pos, TRUE, &mii);
	bool hadbreak = (mii.fType & MFT_MENUBREAK) != 0;

	// 折り返しを外して並べ直す (DrawMenuBar の中で窓の枠を計算し直すので、すぐに位置が取れる)
	if (hadbreak)
	{
		mii.fType &= ~MFT_MENUBREAK;
		SetMenuItemInfo(hmenu, pos, TRUE, &mii);
		DrawMenuBar(hwnd);
	}
	RECT first, last;
	bool wrap = GetMenuItemRect(hwnd, hmenu, 0, &first) && GetMenuItemRect(hwnd, hmenu, n - 1, &last)
		&& last.top != first.top;
	if (wrap)
	{
		mii.fType |= MFT_MENUBREAK;
		SetMenuItemInfo(hmenu, pos, TRUE, &mii);
	}
	if (wrap || hadbreak)
		DrawMenuBar(hwnd);

	inlayout = false;
}

// ---------------------------------------------------------------------------
//	メニューバーの Pause/Play/Step In/Step Over/Step Out (M88gn)
//	本体の Debug メニューと同じ条件で押せる・押せないを決める。メニューバーの項目は
//	開くことが無いので WM_INITMENU では合わせられない。書き直しのたびに調べ、
//	変わったときだけ描き直す
//
void DebugButtons::Update()
{
		if (!hwnd || !core)
		return;
	HMENU hmenu = Menu();
	if (!hmenu)
		return;

	bool stopped = core->IsStopped();
	bool stepping = core->IsStepping();
	int s = (stopped ? 1 : 0) | (stepping ? 2 : 0);
	if (s == state)
		return;
	state = s;

	EnableMenuItem(hmenu, IDM_DBG_PAUSE, !stopped ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DBG_PLAY, (stopped || stepping) ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DBG_STEPIN, stopped ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DBG_STEPOVER, stopped ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DBG_STEPOUT, stopped ? MF_ENABLED : MF_GRAYED);
	DrawMenuBar(hwnd);
}

// ---------------------------------------------------------------------------
//	メニューバーの Addr の欄 (M88gn。コード・メモリの窓で、マウスの下の番地を出す)
//	自分で描く項目にして幅を一定にし (右寄せ、ボタンの左)、変わったらその欄だけ描き直す。
//	メニューバー全体を描き直すと、字の長さが変わったときに並べ直しが起きて窓全体が光ることがあった
//
AddrBarItem::AddrBarItem()
: hwnd(0), id(0), font(0), fontdpi(0)
{
	lstrcpy(text, "Addr: ----");
}

AddrBarItem::~AddrBarItem()
{
	if (font)
		DeleteObject(font);
}

void AddrBarItem::Setup(HWND h, UINT itemid)
{
	hwnd = h, id = itemid;
	HMENU hmenu = ::GetMenu(hwnd);
	if (!hmenu)
		return;
	int n = GetMenuItemCount(hmenu);
	for (int pos = 0; pos < n; pos++)
	{
		if (GetMenuItemID(hmenu, pos) != id)
			continue;
		// 種類を書き換えるだけでは前に測った幅のままなので、外して入れ直す (ボタンと同じ)
		RemoveMenu(hmenu, pos, MF_BYPOSITION);
		MENUITEMINFO mii;
		memset(&mii, 0, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE;
		mii.fType = MFT_OWNERDRAW | MFT_RIGHTJUSTIFY;	// 右寄せ (ボタンの左)
		mii.fState = MFS_DISABLED;
		mii.wID = id;
		InsertMenuItem(hmenu, pos, TRUE, &mii);
		DrawMenuBar(hwnd);
		break;
	}
}

HFONT AddrBarItem::Font()
{
	UINT dpi = Dpi::GetForWindow(hwnd);
	if (font && fontdpi == dpi)
		return font;
	if (font)
		DeleteObject(font);
	NONCLIENTMETRICSW ncm;
	Dpi::GetNonClientMetrics(&ncm, dpi);
	font = CreateFontIndirectW(&ncm.lfMenuFont);
	fontdpi = dpi;
	return font;
}

void AddrBarItem::Paint(HDC dc, const RECT& r, bool active)
{
	int w = r.right - r.left, h = r.bottom - r.top;
	if (w <= 0 || h <= 0)
		return;
	// 地と字を作ってから一度に写す (ちらつかないように)
	HDC mdc = CreateCompatibleDC(dc);
	HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
	HGDIOBJ oldbmp = SelectObject(mdc, bmp);
	RECT lr = { 0, 0, w, h };
	// 地はメニューバーの普通の項目と同じ色 (テーマで描くと右のボタンと同じ白になり、周りの灰色と違って見える)
	BOOL flat = FALSE;
	SystemParametersInfo(SPI_GETFLATMENU, 0, &flat, 0);
	if (Theme::stripdrawing)
	{
		HBRUSH b = CreateSolidBrush(Theme::C().tabselbg);	// ペインのメニューの帯 (M88gn)
		FillRect(mdc, &lr, b);
		DeleteObject(b);
	}
	else
		FillRect(mdc, &lr, GetSysColorBrush(flat ? COLOR_MENUBAR : COLOR_MENU));
	HGDIOBJ oldfont = SelectObject(mdc, Font());
	SetBkMode(mdc, TRANSPARENT);
	if (Theme::stripdrawing)
		SetTextColor(mdc, active ? Theme::C().stripfg : Theme::C().stripgray);
	else
		SetTextColor(mdc, GetSysColor(active ? COLOR_MENUTEXT : COLOR_GRAYTEXT));
	RECT tr = lr;
	tr.left += MulDiv(6, Dpi::GetForWindow(hwnd), 96);
	DrawText(mdc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
	BitBlt(dc, r.left, r.top, w, h, mdc, 0, 0, SRCCOPY);
	SelectObject(mdc, oldfont);
	SelectObject(mdc, oldbmp);
	DeleteObject(bmp);
	DeleteDC(mdc);
}

void AddrBarItem::SetText(const char* t)
{
	if (!lstrcmp(text, t))
		return;
	lstrcpyn(text, t, sizeof(text));
	HMENU hmenu = hwnd ? ::GetMenu(hwnd) : 0;
	if (!hmenu)
	{
		// ドックモードでペインに入っている (メニューはペインの帯に描く。M88gn): ペインに描き直させる
		if (hwnd && (GetWindowLong(hwnd, GWL_STYLE) & WS_CHILD))
			InvalidateRect(GetParent(hwnd), 0, FALSE);
		return;
	}
	// その欄だけ、窓の DC に描き直す
	int n = GetMenuItemCount(hmenu);
	for (int pos = 0; pos < n; pos++)
	{
		if (GetMenuItemID(hmenu, pos) != id)
			continue;
		RECT r, wr;
		if (!GetMenuItemRect(hwnd, hmenu, pos, &r))
			break;
		GetWindowRect(hwnd, &wr);
		OffsetRect(&r, -wr.left, -wr.top);
		HDC dc = GetWindowDC(hwnd);
		if (dc)
		{
			Paint(dc, r, GetForegroundWindow() == hwnd);
			ReleaseDC(hwnd, dc);
		}
		break;
	}
}

bool AddrBarItem::Message(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	if (!hwnd || h != hwnd)
		return false;
	switch (msg)
	{
	case WM_DPICHANGED:
		Setup(hwnd, id);		// 字の大きさが変わるので幅を測り直させる
		return false;			// ほかの処理にも回す

	case WM_MEASUREITEM:
		{
			MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*) lp;
			if (mis->CtlType != ODT_MENU || mis->itemID != id)
				return false;
			// 幅は一定: 「Addr: FFFF  」とラベル 12 字ほど (長いラベルは … で切る)
			HDC dc = GetDC(hwnd);
			HGDIOBJ old = SelectObject(dc, Font());
			SIZE sz;
			GetTextExtentPoint32(dc, "Addr: FFFF  MMMMMMMMMMMM", 24, &sz);
			SelectObject(dc, old);
			ReleaseDC(hwnd, dc);
			mis->itemWidth = sz.cx;
			mis->itemHeight = sz.cy;
			return true;
		}

	case WM_DRAWITEM:
		{
			DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*) lp;
			if (dis->CtlType != ODT_MENU || dis->itemID != id)
				return false;
			Paint(dis->hDC, dis->rcItem, !(dis->itemState & ODS_INACTIVE));
			return true;
		}
	}
	return false;
}
