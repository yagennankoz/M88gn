// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1999, 2000.
// ---------------------------------------------------------------------------
//	$Id: codemon.cpp,v 1.8 2001/02/21 11:58:53 cisc Exp $

#include "headers.h"
#include "resource.h"
#include "pc88/pc88.h"
#include "codemon.h"
#include "misc.h"
#include "device_i.h"
#include "winvars.h"
#include "88config.h"
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
//	構築/消滅
//
CodeMonitor::CodeMonitor()
: core(0), pcfollow(true), lasta0(MemoryViewer::mainram), dbgbuttons(-1),
  inlayoutmenu(false), menulayoutwidth(-1),
  hwndtip(0), tipid(0), tipshown(false), tiptracking(false)
{
}

CodeMonitor::~CodeMonitor()
{
}

bool CodeMonitor::Init(WinCore* wincore)
{
	core = wincore;
	// 行頭に PC マーカ/BP マーカ用の1文字を足すため、既定カラム数は
	// 42 ではなく 44 にする
	if (!MemViewMonitor::Init(MAKEINTRESOURCE(IDD_CODEMON), wincore, "MonitorCode", 44, 24))
		return false;
	if (!diag.Init(GetBus()))
		return false;

	SetLines(0x10000);
	SetUpdateTimer(500);
	return true;
}

// ---------------------------------------------------------------------------
//	PCFollow の ON/OFF を M88.ini に保存/復元する(既定 ON)
//
void CodeMonitor::LoadMonitorConfig()
{
	MemViewMonitor::LoadMonitorConfig();
	pcfollow = GetPrivateProfileInt("MonitorCode", "PCFollow", 1, PC8801::GetM88IniPath()) != 0;
}

void CodeMonitor::SaveMonitorConfig()
{
	MemViewMonitor::SaveMonitorConfig();
	WritePrivateProfileString("MonitorCode", "PCFollow", pcfollow ? "1" : "0", PC8801::GetM88IniPath());
}

// ---------------------------------------------------------------------------
//	ブレーク発生位置へスクロールする(PC追従モードが有効なときだけ)。
//	PC が既に画面内(行の先頭アドレスとして見えている)なら動かさない。
//	表示中のバンクとブレークした CPU が食い違うときは追従しない
//
void CodeMonitor::FollowPC()
{
	if (!pcfollow || !IsOpen())
		return;

	Debugger* dbg = core->GetDebugger();
	// ブレーク中は、ブレークした CPU を表示しているときだけ追従する
	// (別の CPU を見ているのに勝手にスクロールさせない)
	if (dbg->IsBroken() && dbg->GetBreakCpu() != GetShownCpu())
		return;

	ScrollToPC();
}

// ---------------------------------------------------------------------------
//	表示中の CPU の PC へスクロールする。FollowPC と違い、ブレークした CPU と
//	一致しているかどうかは見ない(バンクを手で切り替えたときに使う。
//	「別の CPU を見ている」ことがそのまま操作の意図なので、ここでは弾かない)
//
void CodeMonitor::FollowShownPC()
{
	if (!pcfollow || !IsOpen())
		return;

	ScrollToPC();
}

// ---------------------------------------------------------------------------
//	表示中の CPU の PC が既に画面内(行の先頭アドレスとして見えている)なら
//	動かさない。それ以外は画面の1/4くらいの位置に置き直す
//
void CodeMonitor::ScrollToPC()
{
	Debugger::CpuTarget shown = GetShownCpu();
	PC88::Z80* cpu = (shown == Debugger::cpu_sub) ? core->GetCPU2() : core->GetCPU1();
	uint pc = cpu->GetPC();

	for (size_t y = 0; y < lineaddr.size(); y++)
	{
		if (lineaddr[y] == pc)
		{
			// 命令境界として既に見えている。スクロールしない
			Update();
			return;
		}
	}

	// 画面外(またはジャンプ/自己書き換えで命令境界がずれた)。
	// PC を画面の1/4くらいの位置に置き直す。InstDec は完全ではないので、
	// InstInc で辿り直して pc にちょうど乗るか必ず検証する
	int rows = GetHeight();
	int k = Max(1, rows / 4);

	uint candidate = pc;
	for (int i = 0; i < k; i++)
	{
		if (candidate == 0)
			break;
		uint prev = diag.InstDec(candidate);
		if (prev >= candidate)
			break;
		candidate = prev;
	}

	bool hit = false;
	uint a = candidate;
	for (int i = 0; i < rows; i++)
	{
		if (a == pc)
		{
			hit = true;
			break;
		}
		if (a >= 0x10000)
			break;
		a = diag.InstInc(a);
	}

	SetLine(hit ? candidate : pc);
	Update();
}

// ---------------------------------------------------------------------------
//	表示中のバンクに対応する CPU
//
Debugger::CpuTarget CodeMonitor::GetShownCpu()
{
	return (GetA0() == MemoryViewer::sub) ? Debugger::cpu_sub : Debugger::cpu_main;
}

// ---------------------------------------------------------------------------
//	表示する CPU を切り替える。main へ戻すときは、sub へ切り替える前に
//	見ていたバンクを復元する
//
void CodeMonitor::ShowCpu(Debugger::CpuTarget cpu)
{
	if (!IsOpen() || GetShownCpu() == cpu)
		return;

	if (cpu == Debugger::cpu_sub)
	{
		lasta0 = GetA0();			// main 側で見ていたバンクを覚えておく
		SetA0(MemoryViewer::sub);
	}
	else
	{
		SetA0(lasta0);
	}
	SetBank();						// 中で Update() まで済む
}

// ---------------------------------------------------------------------------
//	表示中の CPU / バンクをタイトルバーに出す
//	(ブレークに連動して自動で切り替わるので、いま何を見ているのかが
//	 分かるようにしておく)
//
void CodeMonitor::UpdateCaption()
{
	if (!IsOpen())
		return;

	const char* bank;
	switch (GetA0())
	{
	case MemoryViewer::sub:		bank = "SUB"; break;
	case MemoryViewer::n88rom:	bank = "MAIN - N88 ROM"; break;
	case MemoryViewer::nrom:	bank = "MAIN - N ROM"; break;
	case MemoryViewer::eram0:	bank = "MAIN - ERAM0"; break;
	case MemoryViewer::eram1:	bank = "MAIN - ERAM1"; break;
	case MemoryViewer::eram2:	bank = "MAIN - ERAM2"; break;
	case MemoryViewer::eram3:	bank = "MAIN - ERAM3"; break;
	default:					bank = "MAIN - RAM"; break;
	}

	char buf[64];
	wsprintf(buf, IsAutoBank() && GetA0() != MemoryViewer::sub ? "Code  [%s (Auto)]" : "Code  [%s]", bank);
	SetWindowText(GetHWnd(), buf);
}

void CodeMonitor::SetBank()
{
	MemViewMonitor::SetBank();
	UpdateCaption();
}

// ---------------------------------------------------------------------------
//	窓が開いた直後に呼ばれる。UpdateCaption() は SetBank() からしか
//	呼ばれていなかったので、開いた直後はタイトルが .rc の "Code" のままだった
//
void CodeMonitor::Start()
{
	UpdateCaption();
	SetOwnerDrawButtons();
	dbgbuttons = -1;
	UpdateDebugButtons();
	menulayoutwidth = -1;
	LayoutMenuBar();
}

// ---------------------------------------------------------------------------
//	メニューバーの Pause/Play/Step を自分で描く項目にし、ツールチップを作る (M88gn)。
//	.rc の字は、ここで外すまでの控え。窓を開くたびにメニューは .rc から作り直されるので、
//	開くたびに呼ぶ。DPI が変わったときも呼ぶ (項目の大きさを測り直させる)
//
void CodeMonitor::SetOwnerDrawButtons()
{
	HWND hwnd = GetHWnd();
	HMENU hmenu = hwnd ? GetMenu(hwnd) : 0;
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
	menulayoutwidth = -1;
	dbgbuttons = -1;

	if (!hwndtip)
	{
		hwndtip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, 0, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
								 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
								 hwnd, 0, GetHInst(), 0);
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

bool CodeMonitor::MeasureButton(MEASUREITEMSTRUCT* mis)
{
	if (mis->CtlType != ODT_MENU || !FindButton(mis->itemID))
		return false;
	int size = ButtonIconSize(GetHWnd());
	mis->itemWidth = size;		// 左右の余白は Windows が足す
	mis->itemHeight = size;
	return true;
}

bool CodeMonitor::DrawButton(const DRAWITEMSTRUCT* dis)
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
	DrawMenuBarBack(GetHWnd(), mdc, lr, active, hot, pushed);
	GdiFlush();

	int size = Min(ButtonIconSize(GetHWnd()), Min(w, h));
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
void CodeMonitor::ButtonMouseMove(POINT screen)
{
	HWND hwnd = GetHWnd();
	HMENU hmenu = GetMenu(hwnd);
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
	ShowButtonTip(false);
	tipid = id;
	if (id)
		SetTimer(hwnd, tiptimerid, tipdelay, 0);
}

void CodeMonitor::ShowButtonTip(bool show)
{
	HWND hwnd = GetHWnd();
	if (!show)
	{
		KillTimer(hwnd, tiptimerid);
		if (hwndtip && tipshown)
			SendMessage(hwndtip, TTM_TRACKACTIVATE, FALSE, 0);
		tipshown = false;
		return;
	}
	const ButtonDef* def = FindButton(tipid);
	HMENU hmenu = GetMenu(hwnd);
	if (!hwndtip || !def || !hmenu)
		return;

	// 本体の Debug メニューの字 ("Step &Over\tF10" など) からショートカットを取り出す
	char text[128];
	lstrcpyn(text, def->name, sizeof(text));
	HWND owner = GetWindow(hwnd, GW_OWNER);
	char label[128];
	if (owner && GetMenuString(GetMenu(owner), def->id, label, sizeof(label), MF_BYCOMMAND) > 0)
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
void CodeMonitor::LayoutMenuBar()
{
	HWND hwnd = GetHWnd();
	if (!hwnd || inlayoutmenu)
		return;
	HMENU hmenu = GetMenu(hwnd);
	if (!hmenu)
		return;

	RECT wr;
	GetWindowRect(hwnd, &wr);
	int width = wr.right - wr.left;
	if (width == menulayoutwidth)
		return;
	menulayoutwidth = width;

	int n = GetMenuItemCount(hmenu);
	int pos;
	for (pos = 0; pos < n; pos++)
		if (GetMenuItemID(hmenu, pos) == IDM_DBG_PAUSE)
			break;
	if (pos >= n)
		return;

	inlayoutmenu = true;

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

	inlayoutmenu = false;
}

// ---------------------------------------------------------------------------
//	メニューバーの Pause/Play/Step In/Step Over/Step Out (M88gn)
//	本体の Debug メニューと同じ条件で押せる・押せないを決める。メニューバーの項目は
//	開くことが無いので WM_INITMENU では合わせられない。書き直しのたびに調べ、
//	変わったときだけ描き直す
//
void CodeMonitor::UpdateDebugButtons()
{
	HWND hwnd = GetHWnd();
	if (!hwnd || !core)
		return;
	HMENU hmenu = GetMenu(hwnd);
	if (!hmenu)
		return;

	bool stopped = core->IsStopped();
	bool stepping = core->IsStepping();
	int state = (stopped ? 1 : 0) | (stepping ? 2 : 0);
	if (state == dbgbuttons)
		return;
	dbgbuttons = state;

	EnableMenuItem(hmenu, IDM_DBG_PAUSE, !stopped ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DBG_PLAY, (stopped || stepping) ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DBG_STEPIN, stopped ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DBG_STEPOVER, stopped ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DBG_STEPOUT, stopped ? MF_ENABLED : MF_GRAYED);
	DrawMenuBar(hwnd);
}

// ---------------------------------------------------------------------------
//	ダイアログ処理
//
BOOL CodeMonitor::DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	POINT p;

	switch (msg)
	{
	case WM_COMMAND:
		switch (LOWORD(wp))
		{
		case IDM_MEM_SAVEIMAGE:
			DumpImage();
			break;

		case IDM_CODE_PCFOLLOW:
			pcfollow = !pcfollow;
			break;

		// メニューバーの Pause/Play/Step は本体の Debug メニューと同じ処理に回す (M88gn)
		case IDM_DBG_PAUSE:
		case IDM_DBG_PLAY:
		case IDM_DBG_STEPIN:
		case IDM_DBG_STEPOVER:
		case IDM_DBG_STEPOUT:
			{
				HWND owner = GetWindow(hdlg, GW_OWNER);
				if (owner)
					PostMessage(owner, WM_COMMAND, LOWORD(wp), 0);
			}
			return TRUE;
		}
		break;

	case WM_INITMENU:
		CheckMenuItem((HMENU) wp, IDM_CODE_PCFOLLOW, pcfollow ? MF_CHECKED : MF_UNCHECKED);
		break;

	case WM_SIZE:
		LayoutMenuBar();
		break;

	case WM_DPICHANGED:
		menulayoutwidth = -1;		// メニューの字の大きさが変わる (この後の WM_SIZE で並べ直す)
		SetOwnerDrawButtons();		// 絵の大きさを測り直させる
		break;

	// メニューバーの Pause/Play/Step の絵とツールチップ (M88gn)
	case WM_MEASUREITEM:
		if (MeasureButton((MEASUREITEMSTRUCT*) lp))
		{
			SetWindowLongPtr(hdlg, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}
		break;

	case WM_DRAWITEM:
		if (DrawButton((const DRAWITEMSTRUCT*) lp))
		{
			SetWindowLongPtr(hdlg, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}
		break;

	case WM_NCMOUSEMOVE:
		{
			POINT pt = { (short) LOWORD(lp), (short) HIWORD(lp) };
			if (wp == HTMENU)
				ButtonMouseMove(pt);
			else if (tipid)
				ShowButtonTip(false), tipid = 0;
		}
		break;

	case WM_NCMOUSELEAVE:
		tiptracking = false;
		ShowButtonTip(false);
		tipid = 0;
		break;

	case WM_MOUSEMOVE:
	case WM_NCLBUTTONDOWN:
	case WM_ENTERMENULOOP:
		if (tipid)
			ShowButtonTip(false), tipid = 0;
		break;

	case WM_TIMER:
		if (wp == tiptimerid)
		{
			KillTimer(hdlg, tiptimerid);
			if (tipid)
				ShowButtonTip(true);
			return TRUE;
		}
		break;

	case WM_DESTROY:
		hwndtip = 0;			// 持ち主の窓といっしょに壊れる
		tipid = 0;
		tipshown = false;
		tiptracking = false;
		break;

	case WM_LBUTTONDOWN:
		p.x = LOWORD(lp);
		p.y = HIWORD(lp);
		if (GetTextPos(&p))
			ToggleBreakAt(p);
		break;
	}

	BOOL r = MemViewMonitor::DlgProc(hdlg, msg, wp, lp);

	// $0000 のバンクを手で切り替えたときは、止まっていれば表示中の CPU の
	// PC を見せる。切り替えただけでは表示位置が前のままで、PC の行が
	// 画面外にいると緑の行が見えない
	if (msg == WM_COMMAND && core->IsStopped())
	{
		switch (LOWORD(wp))
		{
		case IDM_MEM_0_RAM:   case IDM_MEM_0_N88:   case IDM_MEM_0_N:
		case IDM_MEM_0_SUB:
		case IDM_MEM_0_ERAM0: case IDM_MEM_0_ERAM1:
		case IDM_MEM_0_ERAM2: case IDM_MEM_0_ERAM3:
			FollowShownPC();
			break;
		}
	}
	return r;
}

// ---------------------------------------------------------------------------
//	クリックされた行のアドレスの実行ブレークポイントをトグルする。
//	表示中のバンク(&0000 が Sub かどうか)に応じて、対象 CPU を切り替える
//
void CodeMonitor::ToggleBreakAt(POINT p)
{
	if (p.y < 0 || size_t(p.y) >= lineaddr.size())
		return;

	uint addr = lineaddr[p.y];
	if (addr >= 0x10000)
		return;

	Debugger::CpuTarget cpu = (GetA0() == MemoryViewer::sub) ? Debugger::cpu_sub : Debugger::cpu_main;

	core->Lock();
	Debugger* dbg = core->GetDebugger();
	dbg->SetExecBreak(cpu, addr, !dbg->IsExecBreak(cpu, addr));
	core->Unlock();

	Update();
}

// ---------------------------------------------------------------------------
//	スクロールバー処理
//
int CodeMonitor::VerticalScroll(int msg)
{
	int addr = GetLine();
	
	switch (msg)
	{
		int i;

	case SB_LINEUP:
		addr = diag.InstDec(addr);
		if (addr >= 0)
			ScrollDown();
		break;

	case SB_LINEDOWN:
		addr = diag.InstInc(addr);
		if (addr < 0x10000)
			ScrollUp();
		break;

	case SB_PAGEUP:
		for (i=1; i<GetHeight() && addr > 0; i++)
			addr = diag.InstDec(addr);
		break;

	case SB_PAGEDOWN:
		for (i=1; i<GetHeight() && addr < 0x10000; i++)
			addr = diag.InstInc(addr);
		break;

	case SB_TOP:
		addr = 0;
		break;

	case SB_BOTTOM:
		addr = 0x10000-1;
		break;

	case SB_THUMBTRACK:
	case SB_THUMBPOSITION:
		addr = GetScrPos(msg == SB_THUMBTRACK);
		break;
	}
	return Limit(addr, 0xffff, 0);
}

static inline void ToHex(char** p, uint d)
{
	static const char hex[] = "0123456789abcdef";

	*(*p)++ = hex[d >> 4];
	*(*p)++ = hex[d & 15];
}

void CodeMonitor::UpdateText()
{
	if (AutoBank())
		UpdateCaption();
	char buf[128];
	int a = GetLine();

	// PC マーカ。表示中のバンクに対応する CPU の PC だけをマークする。
	// 別の CPU の PC を重ねると、その行には全く関係ないコードが表示されて
	// いるのに緑になってしまう。マーカはポーズ中だけ出す (実行中に出すと
	// 「止まっている位置」と紛らわしい)
	bool subshown = (GetShownCpu() == Debugger::cpu_sub);
	int pc = -1;
	if (core->IsPaused())
		pc = (subshown ? core->GetCPU2() : core->GetCPU1())->GetPC();

	UpdateDebugButtons();

	int rows = GetHeight();
	lineaddr.resize(rows);

	for (int y=0; y<rows; y++)
	{
		if (a < 0x10000)
		{
			lineaddr[y] = uint(a);

			bool hasbp = core->GetDebugger()->IsExecBreak(Debugger::cpu_main, a) ||
						 core->GetDebugger()->IsExecBreak(Debugger::cpu_sub, a);

			char mark;
			if (pc == a)
			{
				mark = subshown ? 'S' : 'M';
				SetBkCol(RGB(0, 0x60, 0));
			}
			else if (hasbp)
			{
				mark = '*';
				SetBkCol(RGB(0x60, 0, 0));
			}
			else
			{
				mark = ' ';
				// 実行回数に基づいて色をつけてみる
				SetBkCol(RGB(StatExec(a), 0, 0x20));
			}

			int next = diag.Disassemble(a, buf+8);
			char* ptr = buf;
			int c, d = next-a;
			for (c=0; c<d; c++)
				ToHex(&ptr, GetBus()->Read8((a+c) & 0xffff));
			for (; c<4; c++)
				*ptr++ = ' ', *ptr++ = ' ';
			Putf("%c %.4x: %.8s   %s\n", mark, a, buf, buf+8);
			a = next;
		}
		else
		{
			lineaddr[y] = uint(-1);
			Puts("\n");
		}
	}
	StatClear();
}

bool CodeMonitor::DumpTo(TextBuffer& tb, int from, int to)
{
	char buf[128];
	for (int a=from; a<to; a)
	{
		int next = diag.Disassemble(a, buf+8);

		char* ptr = buf;
		int c, d = next-a;
		for (c=0; c<d; c++)
			ToHex(&ptr, GetBus()->Read8((a+c) & 0xffff));
		for (; c<4; c++)
			*ptr++ = ' ', *ptr++ = ' ';

		tb.Addf("%.4x %.8s\t\t%s\n", a, buf, buf+8);
		a = next;
	}
	return !tb.Failed();
}

bool CodeMonitor::Dump(FILE* fp, int from, int to)
{
	TextBuffer tb;
	DumpTo(tb, from, to);
	fwrite(tb.Get(), 1, tb.Length(), fp);
	return true;
}

// ---------------------------------------------------------------------------
//	選択中のバンクの 0000-ffff を逆アセンブルした全体をクリップボードへ
//	コピーする(仕様変更。docs/spec-phase10a-fix.md F-3)。
//
//	Lock() は要る。64KB 読む間に VM が走っていると、時点の食い違った
//	内容が混ざるため。
//
//	SetSuppressBreak() の方は念のためで、実際には効いていない。
//	GetBus() は MemoryViewer の私設バスで、VM の MemoryManager を
//	通らないので Debugger のメモリフックに届かない
//	(docs/spec-phase5-debug-ui.md §5.3 / docs/TODO.md の T-6)。
//
bool CodeMonitor::CopyToClipboard()
{
	TextBuffer tb;

	core->Lock();
	core->GetDebugger()->SetSuppressBreak(true);
	DumpTo(tb, 0, 0x10000);
	core->GetDebugger()->SetSuppressBreak(false);
	core->Unlock();

	return SetClipboardText(tb.Get(), tb.Length());
}

// ----------------------------------------------------------------------------
//	総ダンプを書き込む
//	
bool CodeMonitor::DumpImage()
{
	// ダイアログ
	OFNV5 ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = WINVAR(OFNSIZE);
	ofn.FlagsEx = 0;		// 位置バーを消す指定 (OFN_EX_NOPLACESBAR) を付けると、Windows が古い形のダイアログを出す (DPI 対応の窓ではツールバーのアイコンが小さい)

	char filename[MAX_PATH];
	filename[0] = 0;
	
	ofn.hwndOwner = GetHWnd();
	ofn.lpstrFilter = "assembly file (*.asm)\0*.asm\0"
					  "All Files (*.*)\0*.*\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_CREATEPROMPT;
	ofn.lpstrDefExt = "asm";
	ofn.lpstrTitle = 0;
	
	if (!GetSaveFileName(&ofn))
		return false;

	// 書き込み
	FILE* fp = fopen(filename, "w");
	if (fp)
	{
		Dump(fp, 0, 0x10000);
		fclose(fp);
		return true;
	}
	return false;
}
