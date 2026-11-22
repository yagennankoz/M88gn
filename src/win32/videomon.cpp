// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	Video の窓 (GVRAM・テキストの VRAM・パレットを見る。デバッガ用)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	タブで Graphic・Text・Palette を切り替える。中身は止まっているとき (Pause・ブレーク・ステップ) に
//	写し取り (Refresh)、動いている間は前に写し取ったものを出したままにする。
//	本体の窓と同じく DPI 対応 (Per Monitor v2)。位置・大きさは M88.ini の [MonitorVideo]
// ---------------------------------------------------------------------------

#include "headers.h"
#include <commctrl.h>
#include "resource.h"
#include "pc88/pc88.h"
#include "pc88/memory.h"
#include "pc88/crtc.h"
#include "pc88/pd8257.h"
#include "wincore.h"
#include "videomon.h"
#include "memmon.h"
#include "88config.h"
#include "dpi.h"
#include "misc.h"

#ifndef WM_DPICHANGED
#define WM_DPICHANGED	0x02e0
#endif
#ifndef CLEARTYPE_QUALITY
#define CLEARTYPE_QUALITY	5
#endif

using namespace PC8801;

static const char classname[] = "M88gn Video";
static const char* const tabnames[] = { "Graphic", "Text", "Palette" };

// ---------------------------------------------------------------------------
//	構築/消滅
//
VideoMonitor::VideoMonitor()
: core(0), memmon(0), hinst(0), hwnd(0), owner(0), htab(0), font(0), dpi(96), tab(0),
  captured(false), textbase(0), textw(0), texth(0), textattrs(0), textbytes(0), textmode(0), textlines(0), capturetick(0),
  usepalette(true), semimode(true), statush(0), textzoom(0), tfont(0), tfontpx(0), tcw(8), tch(16), tscrollx(0), tscrolly(0), zoom(0), scrollx(0), scrolly(0), dragging(false), hovercol(-1), imgw(0), imgh(0), cellw(8), cellh(16), hoverrow(-1),
  posx(0), posy(0), width(0), height(0), placed(false)
{
	planeon[0] = planeon[1] = planeon[2] = true;
	memset(gvram, 0, sizeof(gvram));
	memset(&sinfo, 0, sizeof(sinfo));
	memset(text, 0, sizeof(text));
	memset(btn, 0, sizeof(btn));
	SetRectEmpty(&imgrect);
	SetRectEmpty(&textrect);
	SetRectEmpty(&semibtn);
	memset(tzbtn, 0, sizeof(tzbtn));
	SetRectEmpty(&textview);
	SetRectEmpty(&imgarea);
	memset(zbtn, 0, sizeof(zbtn));
	dragpt.x = dragpt.y = 0;
	hover[0] = 0;
}

VideoMonitor::~VideoMonitor()
{
	if (font)
		DeleteObject(font);
	if (tfont)
		DeleteObject(tfont);
}

bool VideoMonitor::Init(WinCore* c, MemoryMonitor* mem)
{
	core = c;
	memmon = mem;
	buttons.Init(c);
	LoadPlacement();
	return true;
}

// ---------------------------------------------------------------------------
//	位置・大きさ・タブを M88.ini に
//
void VideoMonitor::LoadPlacement()
{
	const char* ini = PC8801::GetM88IniPath();
	posx = GetPrivateProfileInt("MonitorVideo", "PosX", INT_MIN, ini);
	posy = GetPrivateProfileInt("MonitorVideo", "PosY", INT_MIN, ini);
	width = GetPrivateProfileInt("MonitorVideo", "Width", 0, ini);
	height = GetPrivateProfileInt("MonitorVideo", "Height", 0, ini);
	tab = Limit(GetPrivateProfileInt("MonitorVideo", "Tab", 0, ini), ntabs - 1, 0);
	semimode = GetPrivateProfileInt("MonitorVideo", "Semigraphics", 1, ini) != 0;
	textzoom = GetPrivateProfileInt("MonitorVideo", "TextZoom", 0, ini);
	if (textzoom != 14 && textzoom != 18 && textzoom != 24 && textzoom != 32)
		textzoom = 0;
	zoom = GetPrivateProfileInt("MonitorVideo", "Zoom", 0, ini);
	if (zoom != 1 && zoom != 2 && zoom != 4 && zoom != 8)
		zoom = 0;
	placed = posx != INT_MIN && posy != INT_MIN && width > 0 && height > 0;
}

void VideoMonitor::SavePlacement()
{
	if (!hwnd)
		return;
	RECT r;
	GetWindowRect(hwnd, &r);
	posx = r.left, posy = r.top, width = r.right - r.left, height = r.bottom - r.top;
	placed = true;
	const char* ini = PC8801::GetM88IniPath();
	char buf[16];
	wsprintf(buf, "%d", posx);		WritePrivateProfileString("MonitorVideo", "PosX", buf, ini);
	wsprintf(buf, "%d", posy);		WritePrivateProfileString("MonitorVideo", "PosY", buf, ini);
	wsprintf(buf, "%d", width);		WritePrivateProfileString("MonitorVideo", "Width", buf, ini);
	wsprintf(buf, "%d", height);	WritePrivateProfileString("MonitorVideo", "Height", buf, ini);
	wsprintf(buf, "%d", tab);		WritePrivateProfileString("MonitorVideo", "Tab", buf, ini);
	WritePrivateProfileString("MonitorVideo", "Semigraphics", semimode ? "1" : "0", ini);
	wsprintf(buf, "%d", zoom);		WritePrivateProfileString("MonitorVideo", "Zoom", buf, ini);
	wsprintf(buf, "%d", textzoom);	WritePrivateProfileString("MonitorVideo", "TextZoom", buf, ini);
}

// ---------------------------------------------------------------------------
//	開く・閉じる
//
void VideoMonitor::Show(HINSTANCE hi, HWND own, bool show)
{
	hinst = hi;
	owner = own;
	if (show)
	{
		if (!hwnd)
			Create();
		if (hwnd)
		{
			ShowWindow(hwnd, SW_SHOWNOACTIVATE);
			if (core->IsStopped())
				Refresh();
		}
	}
	else if (hwnd)
	{
		SavePlacement();
		DestroyWindow(hwnd);
	}
}

void VideoMonitor::Close()
{
	if (hwnd)
	{
		SavePlacement();
		DestroyWindow(hwnd);
	}
}

void VideoMonitor::Create()
{
	WNDCLASS wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = WndProcGate;
	wc.hInstance = hinst;
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = 0;
	wc.lpszClassName = classname;
	RegisterClass(&wc);			// 2 回目以降は失敗するが、登録済みなので構わない

	dpi = Dpi::GetForWindow(owner);
	int x = posx, y = posy, w = width, h = height;
	if (placed)
	{
		// 窓の真ん中がどの画面にも無ければ既定の位置にする (ほかの窓と同じ)
		POINT c = { x + w / 2, y + h / 2 };
		if (!MonitorFromPoint(c, MONITOR_DEFAULTTONULL))
			placed = false;
	}
	if (!placed)
	{
		RECT ro;
		GetWindowRect(owner, &ro);
		x = ro.left + Scale(40), y = ro.top + Scale(40);
		w = Scale(700), h = Scale(560);
	}
	// メニューバーは Pause/Play/Step のボタンだけ (DebugButtons が並べる)
	HMENU menu = CreateMenu();
	hwnd = CreateWindowEx(WS_EX_TOOLWINDOW, classname, "Video",
						  WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
						  x, y, w, h, owner, menu, hinst, this);
	if (hwnd)
	{
		buttons.Attach(hwnd, hinst);
		SetTimer(hwnd, buttontimer, 200, 0);	// ボタンの押せる・押せないを合わせる
	}
	else if (menu)
		DestroyMenu(menu);
}

// ---------------------------------------------------------------------------
//	中身を写し取る (止まっているとき)
//
void VideoMonitor::Refresh()
{
	if (!hwnd)
		return;
	Capture();
	InvalidateRect(hwnd, 0, false);
}

void VideoMonitor::Capture()
{
	core->Lock();
	Memory* mem = core->GetMem1();
	Memory::quadbyte* gv = mem->GetGVRAM();
	for (int i = 0; i < 0x4000; i++)
	{
		gvram[0][i] = gv[i].byte[0];
		gvram[1][i] = gv[i].byte[1];
		gvram[2][i] = gv[i].byte[2];
	}
	core->GetScreen()->GetDebugInfo(&sinfo);
	core->GetCRTC()->GetDebugInfo(&textw, &texth, &textattrs, &textbytes, &textmode, &textlines);
	PD8257* dmac = core->GetDMAC();
	textbase = dmac->GetTextBase();
	if (!textbytes)
		textbytes = textw + textattrs * 2;
	uint n = Min(textbytes * texth, uint(maxtext));
	for (uint i = 0; i < n; i++)
		text[i] = uint8(dmac->ReadDebug(textbase + i));
	capturetick = core->GetCPUTick();
	core->Unlock();
	captured = true;
}

// ---------------------------------------------------------------------------
//	字
//
void VideoMonitor::MakeFont()
{
	if (font)
		DeleteObject(font);
	font = CreateFont(-Scale(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS,
					  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "\x82\x6c\x82\x72 \x83\x53\x83\x56\x83\x62\x83\x4e");
	HDC hdc = GetDC(hwnd);
	HGDIOBJ old = SelectObject(hdc, font);
	TEXTMETRIC tm;
	GetTextMetrics(hdc, &tm);
	cellw = tm.tmAveCharWidth;
	cellh = tm.tmHeight;
	SelectObject(hdc, old);
	ReleaseDC(hwnd, hdc);
	if (htab)
		SendMessage(htab, WM_SETFONT, (WPARAM) font, TRUE);
}

void VideoMonitor::Layout()
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	if (htab)
	{
		int th = cellh + Scale(10);
		MoveWindow(htab, 0, 0, rc.right, th, TRUE);
	}
	InvalidateRect(hwnd, 0, false);
}

// ---------------------------------------------------------------------------
//	描く (裏の DC に描いてから写す)
//
void VideoMonitor::Paint(HDC hdc, const RECT& rc)
{
	HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0x20));
	FillRect(hdc, &rc, bg);
	DeleteObject(bg);
	SetBkMode(hdc, TRANSPARENT);
	HGDIOBJ oldfont = SelectObject(hdc, font);

	RECT area = rc;
	if (htab)
	{
		RECT tr;
		GetWindowRect(htab, &tr);
		area.top = tr.bottom - tr.top;
	}
	InflateRect(&area, -Scale(6), -Scale(4));

	if (!captured)
	{
		SetTextColor(hdc, RGB(0xc0, 0xc0, 0xc0));
		DrawText(hdc, "止まっているとき (Debug → Pause・ブレーク・ステップ) に表示します。", -1, &area, DT_LEFT | DT_TOP | DT_WORDBREAK);
	}
	else if (tab == tab_graphic)
		PaintGraphic(hdc, area);
	else if (tab == tab_text)
		PaintText(hdc, area);
	else
		PaintPalette(hdc, area);
	SelectObject(hdc, oldfont);
}

void VideoMonitor::PaintStatus(HDC hdc, const RECT& rc, const char* fields, const char* hint)
{
	// 項目の行は桁の位置をそろえた字 (等幅) なので、値が変わっても位置は動かない。案内は一番下に灰色で
	RECT hr = { rc.left, 0, rc.right, 0 };
	DrawText(hdc, hint, -1, &hr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
	int hh = hr.bottom;
	RECT r = rc;
	r.top = rc.bottom - Max(statush, cellh);
	r.bottom = rc.bottom - hh;
	SetTextColor(hdc, RGB(0xff, 0xff, 0xa0));
	DrawText(hdc, fields, -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
	r.top = rc.bottom - hh;
	r.bottom = rc.bottom;
	SetTextColor(hdc, RGB(0x90, 0x90, 0xa0));
	DrawText(hdc, hint, -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}

int VideoMonitor::StatusHeight(HDC hdc, const RECT& rc, const char* longest, const char* hint)
{
	// 項目の行 (今の字と一番長い字の高い方) + 案内の行
	const char* texts[2] = { hover, longest };
	int h = cellh;
	for (int i = 0; i < 2; i++)
	{
		if (!texts[i][0])
			continue;
		RECT r = { rc.left, 0, rc.right, 0 };
		DrawText(hdc, texts[i], -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
		h = Max(h, int(r.bottom));
	}
	RECT r = { rc.left, 0, rc.right, 0 };
	DrawText(hdc, hint, -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
	return h + Max(int(r.bottom), cellh);
}

int VideoMonitor::AttrLines(HDC hdc, int width)
{
	// PaintText の属性の格子と同じ並び: 見出しの 1 行の下に、左の列 (No./val) の右に 1 組 7 桁の欄。
	// 段ごとにインデックスと値の 2 行
	int perline = Max((width - cellw * attrlabelchars) / (cellw * attrcellchars), 1);
	int blocks = Max((int(textattrs) + perline - 1) / perline, 1);
	return 1 + blocks * 2;
}

// ---- Graphic ----
void VideoMonitor::PaintGraphic(HDC hdc, const RECT& rc)
{
	// ボタン: B・R・G (面の出し入れ)、Palette (パレットの色 / 面そのままの色)
	static const char* const names[4] = { "B", "R", "G", "Palette" };
	static const COLORREF oncol[4] = { RGB(0x40, 0x60, 0xff), RGB(0xff, 0x50, 0x50), RGB(0x40, 0xe0, 0x40), RGB(0xe0, 0xe0, 0xe0) };
	int x = rc.left;
	for (int i = 0; i < 4; i++)
	{
		bool on = i < 3 ? planeon[i] : usepalette;
		SIZE sz;
		GetTextExtentPoint32(hdc, names[i], lstrlen(names[i]), &sz);
		SetRect(&btn[i], x, rc.top, x + sz.cx + Scale(16), rc.top + cellh + Scale(6));
		HBRUSH b = CreateSolidBrush(on ? RGB(0x30, 0x30, 0x60) : RGB(0x10, 0x10, 0x20));
		FillRect(hdc, &btn[i], b);
		DeleteObject(b);
		HBRUSH fr = CreateSolidBrush(on ? oncol[i] : RGB(0x50, 0x50, 0x50));
		FrameRect(hdc, &btn[i], fr);
		DeleteObject(fr);
		SetTextColor(hdc, on ? oncol[i] : RGB(0x70, 0x70, 0x70));
		DrawText(hdc, names[i], -1, &btn[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		x = btn[i].right + Scale(6);
	}
	// 拡大: Fit (窓に合わせる)・x1・x2・x4・x8
	static const char* const znames[5] = { "Fit", "x1", "x2", "x4", "x8" };
	static const int zvals[5] = { 0, 1, 2, 4, 8 };
	x += Scale(10);
	for (int i = 0; i < 5; i++)
	{
		bool on = zoom == zvals[i];
		SIZE sz;
		GetTextExtentPoint32(hdc, znames[i], lstrlen(znames[i]), &sz);
		SetRect(&zbtn[i], x, rc.top, x + sz.cx + Scale(12), rc.top + cellh + Scale(6));
		HBRUSH b = CreateSolidBrush(on ? RGB(0x30, 0x30, 0x60) : RGB(0x10, 0x10, 0x20));
		FillRect(hdc, &zbtn[i], b);
		DeleteObject(b);
		HBRUSH fr = CreateSolidBrush(on ? RGB(0xe0, 0xe0, 0xe0) : RGB(0x50, 0x50, 0x50));
		FrameRect(hdc, &zbtn[i], fr);
		DeleteObject(fr);
		SetTextColor(hdc, on ? RGB(0xe0, 0xe0, 0xe0) : RGB(0x80, 0x80, 0x80));
		DrawText(hdc, znames[i], -1, &zbtn[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		x = zbtn[i].right + Scale(3);
	}
	// モード
	char mode[96];
	wsprintf(mode, "%s  %s%s%s", sinfo.color ? "Color" : "Mono", sinfo.line400 ? "640x400" : (sinfo.line320 ? "320x200" : "640x200"),
			 sinfo.n80mode ? "  N80" : "", sinfo.displaygraphics ? "" : "  (graphics off)");
	RECT mr = { x + Scale(8), rc.top, rc.right, btn[0].bottom };
	SetTextColor(hdc, RGB(0xc0, 0xc0, 0xc0));
	DrawText(hdc, mode, -1, &mr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

	// 絵 (640x200 は縦 2 倍で 640x400 の比に)
	imgw = 640;
	imgh = sinfo.line400 ? 400 : 200;
	static uint32 bits[640 * 400];
	uint32 pal[8];
	for (int i = 0; i < 8; i++)
	{
		if (usepalette)
			pal[i] = RGB(sinfo.pal[i][2] * 255 / 7, sinfo.pal[i][1] * 255 / 7, sinfo.pal[i][0] * 255 / 7);	// DIB は BGR
		else
			pal[i] = RGB(i & 1 ? 255 : 0, i & 4 ? 255 : 0, i & 2 ? 255 : 0);
	}
	if (sinfo.line400)
	{
		// 640x400 (白黒): 上の 200 ラインは青の面、下の 200 ラインは赤の面
		for (int y = 0; y < 400; y++)
		{
			int plane = y < 200 ? 0 : 1;
			const uint8* src = &gvram[plane][(y % 200) * 80];
			for (int xb = 0; xb < 80; xb++)
			{
				uint d = planeon[plane] ? src[xb] : 0;
				for (int b = 0; b < 8; b++)
					bits[y * 640 + xb * 8 + b] = (d & (0x80 >> b)) ? 0xffffff : 0;
			}
		}
	}
	else
	{
		for (int y = 0; y < 200; y++)
		{
			for (int xb = 0; xb < 80; xb++)
			{
				int a = y * 80 + xb;
				uint b0 = planeon[0] ? gvram[0][a] : 0;
				uint b1 = planeon[1] ? gvram[1][a] : 0;
				uint b2 = planeon[2] ? gvram[2][a] : 0;
				for (int b = 0; b < 8; b++)
				{
					uint m = 0x80 >> b;
					int idx = ((b0 & m) ? 1 : 0) | ((b1 & m) ? 2 : 0) | ((b2 & m) ? 4 : 0);
					bits[y * 640 + xb * 8 + b] = sinfo.color ? pal[idx] : (idx ? 0xffffff : 0);
				}
			}
		}
	}
	// 下のステータスは折り返しても全部出す。絵はその上の範囲に、
	// Fit なら窓に合わせて縦横の比を保って出す。拡大なら 640x400 (96 DPI 換算) の倍率で、はみ出す分はスクロール
	static const char ghint[] = "クリックでメモリの窓へ  Ctrl+ホイールで拡大  ホイール・右ドラッグで移動";
	static const char gblank[] = "X ---  Y ---  addr ----      B --  R --  G --  color -";
	statush = StatusHeight(hdc, rc, "X 639  Y 399  addr FFFF (B)  B FF  R FF  G FF  color 7", ghint);
	RECT ar = rc;
	ar.top = btn[0].bottom + Scale(6);
	ar.bottom -= statush + Scale(4);
	if (ar.bottom < ar.top + Scale(16))
		ar.bottom = ar.top + Scale(16);
	imgarea = ar;
	int aw = ar.right - ar.left, ah = ar.bottom - ar.top;
	int dw, dh;
	if (zoom)
	{
		dw = Scale(640 * zoom), dh = Scale(400 * zoom);
		scrollx = Limit(scrollx, Max(dw - aw, 0), 0);
		scrolly = Limit(scrolly, Max(dh - ah, 0), 0);
		SetRect(&imgrect, ar.left - scrollx, ar.top - scrolly, ar.left - scrollx + dw, ar.top - scrolly + dh);
	}
	else
	{
		dw = aw, dh = aw * 400 / 640;
		if (dh > ah)
			dh = ah, dw = ah * 640 / 400;
		SetRect(&imgrect, ar.left, ar.top, ar.left + dw, ar.top + dh);
	}
	BITMAPINFO bi;
	memset(&bi, 0, sizeof(bi));
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = 640;
	bi.bmiHeader.biHeight = -imgh;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	SetStretchBltMode(hdc, COLORONCOLOR);
	IntersectClipRect(hdc, ar.left, ar.top, ar.right, ar.bottom);
	if (zoom)
	{
		// 見えている所だけを写す (大きく拡大したときに全体を引き伸ばさない)
		double sx = double(dw) / 640, sy = double(dh) / imgh;
		int x0 = Max(int(scrollx / sx), 0), y0 = Max(int(scrolly / sy), 0);
		int x1 = Min(int((scrollx + aw) / sx) + 1, 640), y1 = Min(int((scrolly + ah) / sy) + 1, imgh);
		int ddx = imgrect.left + int(x0 * sx + 0.5), ddy = imgrect.top + int(y0 * sy + 0.5);
		int ddw = imgrect.left + int(x1 * sx + 0.5) - ddx, ddh = imgrect.top + int(y1 * sy + 0.5) - ddy;
		// 上下を逆にした DIB (biHeight < 0) でも、元の範囲の y は下からの数え方になる
		StretchDIBits(hdc, ddx, ddy, ddw, ddh, x0, imgh - y1, x1 - x0, y1 - y0, bits, &bi, DIB_RGB_COLORS, SRCCOPY);
	}
	else
		StretchDIBits(hdc, imgrect.left, imgrect.top, dw, dh, 0, 0, 640, imgh, bits, &bi, DIB_RGB_COLORS, SRCCOPY);
	SelectClipRgn(hdc, 0);
	HBRUSH fr = CreateSolidBrush(RGB(0x40, 0x40, 0x60));
	RECT frr = imgrect;
	IntersectRect(&frr, &frr, &ar);
	InflateRect(&frr, 1, 1);
	FrameRect(hdc, &frr, fr);
	DeleteObject(fr);

	PaintStatus(hdc, rc, hover[0] ? hover : gblank, ghint);
}

bool VideoMonitor::GraphicPixel(int px, int py, int* gx, int* gy)
{
	if (!PtInRect(&imgrect, POINT{ px, py }) || !PtInRect(&imgarea, POINT{ px, py }) || imgrect.right <= imgrect.left)
		return false;
	*gx = (px - imgrect.left) * 640 / (imgrect.right - imgrect.left);
	*gy = (py - imgrect.top) * imgh / (imgrect.bottom - imgrect.top);
	return *gx >= 0 && *gx < 640 && *gy >= 0 && *gy < imgh;
}

// ---- Text ----
void VideoMonitor::TextAttrAt(int row, int col, uint* colcode, uint* deco)
{
	// M88 の CRTC (ExpandAttributes) と同じ読み方: 1 バイト目 (桁、下位 7 ビット) で印を付けた桁に、
	// 2 バイト目の値を並んだ順に当てる (桁と値は組ではない)。色のモードでは、色の属性 (bit3 = 1) と
	// 装飾の属性は別々に持つ。属性は行をまたいで引き継ぐ (フレームの初めは白・装飾無し)
	*colcode = 0xe8;
	*deco = 0;
	bool colormode = (textmode & 2) != 0;
	for (int y = 0; y <= row; y++)
	{
		const uint8* a = &text[y * textbytes + textw];
		bool mark[128];
		memset(mark, 0, sizeof(mark));
		for (uint i = 0; i < textattrs; i++)
			mark[a[i * 2] & 0x7f] = true;
		uint k = 0;
		int last = y < row ? int(textw) - 1 : col;
		for (int x = 0; x <= last; x++)
		{
			if (mark[x] && k < textattrs)
			{
				uint code = a[k * 2 + 1];
				k++;
				if (colormode && (code & 0x08))
					*colcode = code;
				else
					*deco = code;
			}
		}
	}
}

void VideoMonitor::CellAttrs(uint8* colcodes, uint8* decos, uint16* colids, uint16* decoids)
{
	// TextAttrAt と同じ読み方を、1 回で全部の桁に
	uint cc = 0xe8, dc = 0;
	uint ccid = 0, dcid = 0;
	bool colormode = (textmode & 2) != 0;
	for (uint y = 0; y < texth && y < 64 && (y + 1) * textbytes <= maxtext; y++)
	{
		const uint8* a = &text[y * textbytes + textw];
		bool mark[128];
		memset(mark, 0, sizeof(mark));
		for (uint i = 0; i < textattrs; i++)
			mark[a[i * 2] & 0x7f] = true;
		uint k = 0;
		for (uint x = 0; x < textw && x < 128; x++)
		{
			if (mark[x] && k < textattrs)
			{
				uint code = a[k * 2 + 1];
				k++;
				if (colormode && (code & 0x08))
					cc = code, ccid = y * 64 + k;
				else
					dc = code, dcid = y * 64 + k;		// k は取り出した後なので、組の番号 + 1
			}
			colcodes[y * 128 + x] = uint8(cc);
			decos[y * 128 + x] = uint8(dc);
			if (colids)
				colids[y * 128 + x] = uint16(ccid);
			if (decoids)
				decoids[y * 128 + x] = uint16(dcid);
		}
	}
}

void VideoMonitor::DescribeAttr(uint colcode, uint deco, char* buf)
{
	static const char* const cols[8] = { "black", "blue", "red", "magenta", "green", "cyan", "yellow", "white" };
	char d[64];
	wsprintf(d, "%s%s%s%s%s", deco & 1 ? " secret" : "", deco & 2 ? " blink" : "",
			 deco & 4 ? " reverse" : "", deco & 0x10 ? " upperline" : "", deco & 0x20 ? " underline" : "");
	if (textmode & 2)
		wsprintf(buf, "%.2X %s%s / %.2X%s", colcode, cols[(colcode >> 5) & 7], colcode & 0x10 ? " semigraphic" : "",
				 deco, d[0] ? d : " normal");
	else
		wsprintf(buf, "%.2X%s%s", deco, d[0] ? d : " normal", deco & 0x80 ? " semigraphic" : "");
}

void VideoMonitor::PaintText(HDC hdc, const RECT& rc)
{
	// Semigraphics のボタン (オン: セミグラフィックの属性の桁を 2x4 の点で描く / オフ: 文字で出す)
	{
		static const char name[] = "Semigraphics";
		SIZE sz;
		GetTextExtentPoint32(hdc, name, lstrlen(name), &sz);
		SetRect(&semibtn, rc.left, rc.top, rc.left + sz.cx + Scale(16), rc.top + cellh + Scale(6));
		HBRUSH b = CreateSolidBrush(semimode ? RGB(0x30, 0x30, 0x60) : RGB(0x10, 0x10, 0x20));
		FillRect(hdc, &semibtn, b);
		DeleteObject(b);
		HBRUSH fr = CreateSolidBrush(semimode ? RGB(0xe0, 0xe0, 0xe0) : RGB(0x50, 0x50, 0x50));
		FrameRect(hdc, &semibtn, fr);
		DeleteObject(fr);
		SetTextColor(hdc, semimode ? RGB(0xe0, 0xe0, 0xe0) : RGB(0x70, 0x70, 0x70));
		DrawText(hdc, name, -1, &semibtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	// 文字の大きさ: Fit (窓に合わせる)・14・18・24・32 (96 DPI 換算のピクセル)
	static const char* const tznames[5] = { "Fit", "14", "18", "24", "32" };
	static const int tzvals[5] = { 0, 14, 18, 24, 32 };
	int bx = semibtn.right + Scale(10);
	for (int i = 0; i < 5; i++)
	{
		bool on = textzoom == tzvals[i];
		SIZE sz;
		GetTextExtentPoint32(hdc, tznames[i], lstrlen(tznames[i]), &sz);
		SetRect(&tzbtn[i], bx, rc.top, bx + sz.cx + Scale(12), semibtn.bottom);
		HBRUSH b = CreateSolidBrush(on ? RGB(0x30, 0x30, 0x60) : RGB(0x10, 0x10, 0x20));
		FillRect(hdc, &tzbtn[i], b);
		DeleteObject(b);
		HBRUSH fr = CreateSolidBrush(on ? RGB(0xe0, 0xe0, 0xe0) : RGB(0x50, 0x50, 0x50));
		FrameRect(hdc, &tzbtn[i], fr);
		DeleteObject(fr);
		SetTextColor(hdc, on ? RGB(0xe0, 0xe0, 0xe0) : RGB(0x80, 0x80, 0x80));
		DrawText(hdc, tznames[i], -1, &tzbtn[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		bx = tzbtn[i].right + Scale(3);
	}
	char head[160];
	wsprintf(head, "Text %ux%u  base %.4X  %u bytes/line  %u attributes/line  %u dots/row%s",
			 textw, texth, textbase, textbytes, textattrs, textlines, textmode & 8 ? "  skip line" : "");
	RECT hr = { bx + Scale(10), rc.top, rc.right, semibtn.bottom };
	SetTextColor(hdc, RGB(0xc0, 0xc0, 0xc0));
	DrawText(hdc, head, -1, &hr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

	static uint8 colcodes[64 * 128], decos[64 * 128];
	static uint16 colids[64 * 128], decoids[64 * 128];
	memset(colcodes, 0xe8, sizeof(colcodes));
	memset(decos, 0, sizeof(decos));
	memset(colids, 0, sizeof(colids));
	memset(decoids, 0, sizeof(decoids));
	if (textattrs)
		CellAttrs(colcodes, decos, colids, decoids);

	// 下の情報は拡大・縮小しても全部出す。下から、ステータス (折り返す)、セミグラフィックの拡大
	// (画面にセミグラフィックの桁があるときだけ)、属性の並び (最も長いときの行数)。格子はその上の範囲
	static const char thint[] = "文字にマウスで番地・コード・属性  Ctrl+ホイールで文字の大きさ  ホイール・右ドラッグで移動";
	static const char tblank[] = "col ---  row --  addr ----  code --  attr --";
	statush = StatusHeight(hdc, rc, "col 127  row 63  addr FFFF  code FF  attr FF white semigraphic / FF secret blink reverse upperline underline", thint);
	bool anysemi = false;
	for (uint y = 0; y < texth && y < 64 && !anysemi; y++)
		for (uint x = 0; x < textw && x < 128; x++)
			if (IsSemi(colcodes[y * 128 + x], decos[y * 128 + x]))
			{
				anysemi = true;
				break;
			}
	const int ds = Scale(26);					// セミグラフィックの拡大の 1 点
	int statustop = rc.bottom - statush;
	int semitop = statustop - Scale(4) - (anysemi ? Scale(8) + cellh + Scale(6) + ds * 4 : 0);
	int attrlines = textattrs ? AttrLines(hdc, rc.right - rc.left) : 0;
	int attrtop = semitop - attrlines * (cellh + Scale(2)) - (attrlines ? Scale(6) : 0);

	// 格子の字の大きさ。Fit なら、格子 (行番号の 3 桁を含む) が窓の幅と、下の情報を残した高さに
	// 収まる一番大きい字 (ＭＳ ゴシックは幅が高さの半分)
	int y0 = semibtn.bottom + Scale(6);
	if (textzoom)
		TextFont(hdc, Scale(textzoom));
	else
	{
		int aw = rc.right - rc.left, ah = attrtop - Scale(6) - y0;
		int cols = Max(int(textw) + 3, 1), rows = Max(int(texth), 1);
		int px = Min(ah / rows, aw * 2 / cols);
		px = Max(px, Scale(8));
		TextFont(hdc, px);
		// 字の幅が高さの半分より広いフォントのときは、収まるまで小さくする
		while (px > Scale(8) && (tcw * cols > aw || tch * rows > ah))
			TextFont(hdc, --px);
	}
	// 格子を出す範囲は、下の情報の上まで。はみ出す分はスクロール (ホイール・右ドラッグ)
	SetRect(&textview, rc.left, y0, rc.right, Max(attrtop - Scale(6), y0 + tch));
	{
		int gw = tcw * (int(textw) + 3), gh = tch * int(texth);
		tscrollx = Limit(tscrollx, Max(gw - int(textview.right - textview.left), 0), 0);
		tscrolly = Limit(tscrolly, Max(gh - int(textview.bottom - textview.top), 0), 0);
	}
	HGDIOBJ oldgrid = SelectObject(hdc, tfont);
	IntersectClipRect(hdc, textview.left, textview.top, textview.right, textview.bottom);
	int gx = rc.left - tscrollx;
	y0 -= tscrolly;
	int x0 = gx + tcw * 3;
	SetRect(&textrect, x0, y0, x0 + int(textw) * tcw, y0 + int(texth) * tch);

	// マウスの下の文字の属性を設定した組 (色と装飾) が同じまま続いている範囲。読む順に前後へたどる。
	// 行の頭に印が無く前の行から引き継いでいるときだけ行をまたぐ (同じ値でも、設定し直した所で区切る)。
	// その範囲の文字に背景色を付ける (spanbeg・spanend は 行 * gw2 + 桁 の通し番号)
	int spanbeg = -1, spanend = -1, spanhere = -1;
	uint gw2 = Min(textw, 128u), gh2 = Min(texth, 64u);
	if (textattrs && hoverrow >= 0 && hovercol >= 0 && uint(hoverrow) < gh2 && uint(hovercol) < gw2)
	{
		auto same = [&](int i, int j)
		{
			int ri = i / gw2, ci = i % gw2, rj = j / gw2, cj = j % gw2;
			return colids[ri * 128 + ci] == colids[rj * 128 + cj] && decoids[ri * 128 + ci] == decoids[rj * 128 + cj];
		};
		spanhere = hoverrow * gw2 + hovercol;
		spanbeg = spanend = spanhere;
		while (spanbeg > 0 && same(spanbeg - 1, spanhere))
			spanbeg--;
		while (spanend + 1 < int(gw2 * gh2) && same(spanend + 1, spanhere))
			spanend++;
	}
	HBRUSH spanbr = CreateSolidBrush(RGB(0x28, 0x30, 0x68));
	for (uint y = 0; y < texth && (y + 1) * textbytes <= maxtext; y++)
	{
		char num[8];
		wsprintf(num, "%2u", y);
		SetTextColor(hdc, RGB(0x80, 0x80, 0xa0));
		TextOut(hdc, gx, y0 + y * tch, num, 2);
		for (uint x = 0; x < textw; x++)
		{
			uint c = text[y * textbytes + x];
			if (spanbeg >= 0 && x < gw2 && y < gh2)
			{
				int li = y * gw2 + x;
				if (spanbeg <= li && li <= spanend)
				{
					RECT br = { x0 + int(x) * tcw, y0 + int(y) * tch, x0 + int(x + 1) * tcw, y0 + int(y + 1) * tch };
					FillRect(hdc, &br, spanbr);
				}
			}
			if (semimode && y < 64 && x < 128 && IsSemi(colcodes[y * 128 + x], decos[y * 128 + x]))
			{
				// 2x4 の点。左の列が上から bit0～3、右の列が bit4～7 (CRTC::CreateGFont と同じ)
				// 色のモードなら色の属性の色 (黒は見えないので暗い灰色)、白黒なら白
				COLORREF col = RGB(0xff, 0xff, 0xff);
				if (textmode & 2)
				{
					uint g = (colcodes[y * 128 + x] >> 5) & 7;
					col = g ? RGB(g & 2 ? 0xff : 0, g & 4 ? 0xff : 0, g & 1 ? 0xff : 0) : RGB(0x40, 0x40, 0x40);
				}
				HBRUSH db = CreateSolidBrush(col);
				int cx = x0 + x * tcw, cy = y0 + y * tch;
				for (int b = 0; b < 8; b++)
				{
					if (!(c & (1 << b)))
						continue;
					int dx = b < 4 ? 0 : 1, dy = b & 3;
					RECT dr = { cx + dx * tcw / 2, cy + dy * tch / 4,
								cx + (dx + 1) * tcw / 2, cy + (dy + 1) * tch / 4 };
					FillRect(hdc, &dr, db);
				}
				DeleteObject(db);
				continue;
			}
			char ch = '.';
			bool shown = (c >= 0x20 && c < 0x7f) || (c >= 0xa1 && c <= 0xdf);
			if (shown)
				ch = char(c);
			SetTextColor(hdc, shown ? RGB(0xff, 0xff, 0xff) : RGB(0x50, 0x50, 0x70));
			TextOut(hdc, x0 + x * tcw, y0 + y * tch, &ch, 1);
		}
	}
	DeleteObject(spanbr);
	SelectObject(hdc, oldgrid);
	SelectClipRgn(hdc, 0);
	// マウスの下の行の属性
	if (textattrs)
	{
		bool rowok = hoverrow >= 0 && uint(hoverrow) < texth;
		const uint8* a = &text[(rowok ? hoverrow : 0) * textbytes + textw];
		RECT ar = rc;
		ar.top = Max(attrtop, int(textview.bottom) + Scale(6));
		ar.bottom = semitop;

		// マウスの下の桁に効いている値を探す (M88 の CRTC と同じ読み方: 印の付いた桁に、値を並んだ順に当てる)。
		// 色のモードでは色の属性と装飾の属性を別に持つので、2 つ (この行に無ければ前の行から引き継いだもの)
		int colidx = -1, decoidx = -1;			// 効いている値の番号 (-1: この行より前から)
		int colmark = -1, decomark = -1;		// その値を当てた桁
		if (hovercol >= 0)
		{
			bool mark[128];
			memset(mark, 0, sizeof(mark));
			for (uint i = 0; i < textattrs; i++)
				mark[a[i * 2] & 0x7f] = true;
			bool colormode = (textmode & 2) != 0;
			uint k = 0;
			for (int x = 0; x <= hovercol && x < 128; x++)
			{
				if (mark[x] && k < textattrs)
				{
					uint code = a[k * 2 + 1];
					if (colormode && (code & 0x08))
						colidx = k, colmark = x;
					else
						decoidx = k, decomark = x;
					k++;
				}
			}
		}
		// 桁の印は、その桁を書いた最初の組に付ける
		int colpos = -1, decopos = -1;
		for (uint i = 0; rowok && i < textattrs; i++)
		{
			int pos = a[i * 2] & 0x7f;
			if (colpos < 0 && pos == colmark)
				colpos = i;
			if (decopos < 0 && pos == decomark)
				decopos = i;
		}

		// 格子に書く: 左に見出しの列、右に 1 組 7 桁 ("127:FF" + 空白) の欄を並べる。
		// 各組の上の行にインデックス (何番目の組か。値はこの順に、印の付いた桁へ当てる)。
		// 効いている値と、その値を当てた桁に背景色。幅に収まらなければ次の段へ (AttrLines と同じ並び)
		const COLORREF normal = RGB(0xa0, 0xff, 0xa0);
		const COLORREF idxcol = RGB(0x70, 0x90, 0xa0);
		const COLORREF valbg = RGB(0xff, 0xa0, 0x40);		// 効いている値
		const COLORREF posbg = RGB(0x80, 0x80, 0xc0);		// その値を当てた桁
		const int lh = cellh + Scale(2);
		const int labelw = cellw * attrlabelchars;
		const int cw7 = cellw * attrcellchars;
		int perline = Max((int(ar.right - ar.left) - labelw) / cw7, 1);
		auto put = [&](int x, int y, const char* s, bool bg, COLORREF bgc, COLORREF fg)
		{
			SIZE sz;
			GetTextExtentPoint32(hdc, s, lstrlen(s), &sz);
			if (bg)
			{
				RECT r = { x - 1, y, x + sz.cx + 1, y + cellh };
				HBRUSH b = CreateSolidBrush(bgc);
				FillRect(hdc, &r, b);
				DeleteObject(b);
			}
			SetTextColor(hdc, fg);
			TextOut(hdc, x, y, s, lstrlen(s));
		};
		char t[64];
		if (rowok)
			wsprintf(t, "Row %2d attributes:", hoverrow);
		else
			lstrcpy(t, "Row -- attributes:");
		// 1 行目は見出し (と引き継ぎの注記)。その下に段ごとにインデックスの行と値の行
		put(ar.left, ar.top, t, false, 0, normal);
		int headw;
		{
			SIZE sz;
			GetTextExtentPoint32(hdc, t, lstrlen(t), &sz);
			headw = sz.cx;
		}
		for (uint i = 0; rowok && i < textattrs; i++)
		{
			int col = i % perline;
			int y = ar.top + lh + int(i / perline) * lh * 2;
			if (col == 0)
			{
				put(ar.left, y, "No.", false, 0, idxcol);
				put(ar.left, y + lh, "val", false, 0, idxcol);
			}
			int x = ar.left + labelw + col * cw7;
			char is[8], ps[8], vs[8];
			wsprintf(is, "%3u", i);
			wsprintf(ps, "%3u", a[i * 2] & 0x7f);
			wsprintf(vs, "%.2X", a[i * 2 + 1]);
			bool pb = int(i) == colpos || int(i) == decopos;
			bool vb = int(i) == colidx || int(i) == decoidx;
			put(x, y, is, false, 0, idxcol);
			put(x, y + lh, ps, pb, posbg, pb ? RGB(0xff, 0xff, 0xff) : normal);
			put(x + cellw * 3, y + lh, ":", false, 0, normal);
			put(x + cellw * 4, y + lh, vs, vb, valbg, vb ? RGB(0, 0, 0) : normal);
		}
		// この行の値が効いていない (前の行から引き継いだ) ものを書き添える
		if (hovercol >= 0)
		{
			char note[64] = "";
			if (textmode & 2)
			{
				if (colidx < 0 && decoidx < 0)
					lstrcpy(note, "(color and decoration: from rows above)");
				else if (colidx < 0)
					lstrcpy(note, "(color: from rows above)");
				else if (decoidx < 0)
					lstrcpy(note, "(decoration: from rows above)");
			}
			else if (decoidx < 0)
				lstrcpy(note, "(from rows above)");
			if (note[0])
				put(ar.left + headw + cellw * 2, ar.top, note, false, 0, RGB(0xc0, 0xc0, 0xc0));
		}

		// マウスの下の文字がセミグラフィックなら、その 2x4 の点を大きく出す (各点に bit の番号)
		if (hovercol >= 0 && uint(hovercol) < textw && hoverrow < 64 && hovercol < 128
			&& IsSemi(colcodes[hoverrow * 128 + hovercol], decos[hoverrow * 128 + hovercol]))
		{
			uint c = text[hoverrow * textbytes + hovercol];
			int top = semitop + Scale(8);
			char t[64];
			wsprintf(t, "Semigraphic %.2X  (col %d, row %d)", c, hovercol, hoverrow);
			SetTextColor(hdc, RGB(0xc0, 0xc0, 0xc0));
			TextOut(hdc, rc.left, top, t, lstrlen(t));
			int px = rc.left, py = top + cellh + Scale(6);
			if (py + ds * 4 <= statustop)
			{
				COLORREF col = RGB(0xff, 0xff, 0xff);
				if (textmode & 2)
				{
					uint g = (colcodes[hoverrow * 128 + hovercol] >> 5) & 7;
					col = g ? RGB(g & 2 ? 0xff : 0, g & 4 ? 0xff : 0, g & 1 ? 0xff : 0) : RGB(0x40, 0x40, 0x40);
				}
				HBRUSH on = CreateSolidBrush(col);
				HBRUSH off = CreateSolidBrush(RGB(0x10, 0x10, 0x30));
				HBRUSH grid = CreateSolidBrush(RGB(0x50, 0x50, 0x70));
				for (int b = 0; b < 8; b++)
				{
					int dx = b < 4 ? 0 : 1, dy = b & 3;
					RECT dr = { px + dx * ds, py + dy * ds, px + (dx + 1) * ds, py + (dy + 1) * ds };
					bool set = (c & (1 << b)) != 0;
					FillRect(hdc, &dr, set ? on : off);
					FrameRect(hdc, &dr, grid);
					char n[2] = { char('0' + b), 0 };
					// 点の色が明るければ黒い字
					SetTextColor(hdc, set ? (GetRValue(col) + GetGValue(col) * 2 + GetBValue(col) > 400 ? RGB(0, 0, 0) : RGB(0xff, 0xff, 0xff))
									  : RGB(0x60, 0x60, 0x80));
					DrawText(hdc, n, 1, &dr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				}
				DeleteObject(on);
				DeleteObject(off);
				DeleteObject(grid);
			}
		}
	}
	PaintStatus(hdc, rc, hover[0] ? hover : tblank, thint);
}

// ---- Palette ----
void VideoMonitor::PaintPalette(HDC hdc, const RECT& rc)
{
	int sw = Scale(36), sh = cellh + Scale(6);
	int y = rc.top;
	SetTextColor(hdc, RGB(0xc0, 0xc0, 0xc0));
	TextOut(hdc, rc.left, y, "Palette (R G B: 0-7)", 20);
	y += cellh + Scale(6);
	for (int i = 0; i < 9; i++)
	{
		const uint8* p = i < 8 ? sinfo.pal[i] : sinfo.bg;
		COLORREF c = RGB(p[0] * 255 / 7, p[1] * 255 / 7, p[2] * 255 / 7);
		RECT r = { rc.left, y, rc.left + sw, y + sh };
		HBRUSH b = CreateSolidBrush(c);
		FillRect(hdc, &r, b);
		DeleteObject(b);
		HBRUSH fr = CreateSolidBrush(RGB(0x80, 0x80, 0x80));
		FrameRect(hdc, &r, fr);
		DeleteObject(fr);
		char t[64];
		if (i < 8)
			wsprintf(t, "%d   R%d G%d B%d   #%.2X%.2X%.2X", i, p[0], p[1], p[2], GetRValue(c), GetGValue(c), GetBValue(c));
		else
			wsprintf(t, "BG  R%d G%d B%d   #%.2X%.2X%.2X  (port 52)", p[0], p[1], p[2], GetRValue(c), GetGValue(c), GetBValue(c));
		SetTextColor(hdc, RGB(0xff, 0xff, 0xff));
		TextOut(hdc, rc.left + sw + Scale(10), y + Scale(3), t, lstrlen(t));
		y += sh + Scale(4);
	}
	y += Scale(6);
	char t[160];
	wsprintf(t, "port 30: %.2X   port 31: %.2X   port 32: %.2X   port 53: %.2X", sinfo.port30, sinfo.port31, sinfo.port32, sinfo.port53);
	SetTextColor(hdc, RGB(0xc0, 0xc0, 0xc0));
	TextOut(hdc, rc.left, y, t, lstrlen(t));
	y += cellh + Scale(4);
	wsprintf(t, "%s  %s%s   text %s   planes shown: %s%s%s   graphics %s",
			 sinfo.color ? "Color" : "Mono", sinfo.line400 ? "640x400" : (sinfo.line320 ? "320x200" : "640x200"),
			 sinfo.n80mode ? " (N80)" : "",
			 sinfo.port53 & 1 ? "off" : "on",
			 sinfo.port53 & 2 ? "-" : "B", sinfo.port53 & 4 ? "-" : "R", sinfo.port53 & 8 ? "-" : "G",
			 sinfo.displaygraphics ? "on" : "off");
	TextOut(hdc, rc.left, y, t, lstrlen(t));
}

// ---------------------------------------------------------------------------
//	マウス
//
void VideoMonitor::OnMouseMove(int x, int y)
{
	char old[160];
	lstrcpy(old, hover);
	int oldrow = hoverrow;
	hover[0] = 0;
	if (captured && tab == tab_graphic)
	{
		int gx, gy;
		if (GraphicPixel(x, y, &gx, &gy))
		{
			int line = sinfo.line400 ? gy % 200 : gy;
			int a = line * 80 + gx / 8;
			int m = 0x80 >> (gx & 7);
			int idx = ((gvram[0][a] & m) ? 1 : 0) | ((gvram[1][a] & m) ? 2 : 0) | ((gvram[2][a] & m) ? 4 : 0);
			wsprintf(hover, "X %3d  Y %3d  addr %.4X%s  B %.2X  R %.2X  G %.2X  color %d",
					 gx, gy, 0xc000 + a, sinfo.line400 ? (gy < 200 ? " (B)" : " (R)") : "    ",
					 gvram[0][a], gvram[1][a], gvram[2][a], idx);
		}
	}
	else if (captured && tab == tab_text)
	{
		hoverrow = -1;
		if (PtInRect(&textrect, POINT{ x, y }) && PtInRect(&textview, POINT{ x, y }) && tcw && tch)
		{
			int col = (x - textrect.left) / tcw, row = (y - textrect.top) / tch;
			hoverrow = row;
			hovercol = col;
			uint a = textbase + row * textbytes + col;
			char at[64];
			if (textattrs)
			{
				uint cc, dc;
				TextAttrAt(row, col, &cc, &dc);
				DescribeAttr(cc, dc, at);
			}
			else
				lstrcpy(at, "-");
			wsprintf(hover, "col %3d  row %2d  addr %.4X  code %.2X  attr %s", col, row, a & 0xffff, text[row * textbytes + col], at);
		}
	}
	if (hoverrow < 0)
		hovercol = -1;
	if (lstrcmp(old, hover) || oldrow != hoverrow || tab == tab_text)
		InvalidateRect(hwnd, 0, false);
}

void VideoMonitor::OnClick(int x, int y)
{
	if (tab == tab_graphic)
	{
		static const int zvals[5] = { 0, 1, 2, 4, 8 };
		for (int i = 0; i < 5; i++)
		{
			if (PtInRect(&zbtn[i], POINT{ x, y }))
			{
				SetZoom(zvals[i], (imgarea.left + imgarea.right) / 2, (imgarea.top + imgarea.bottom) / 2);
				return;
			}
		}
		for (int i = 0; i < 4; i++)
		{
			if (PtInRect(&btn[i], POINT{ x, y }))
			{
				if (i < 3)
					planeon[i] = !planeon[i];
				else
					usepalette = !usepalette;
				InvalidateRect(hwnd, 0, false);
				return;
			}
		}
		int gx, gy;
		if (captured && memmon && GraphicPixel(x, y, &gx, &gy))
		{
			// メモリの窓をその番地へ。c000 のバンクは出している面 (400 ラインは上 = 青・下 = 赤)
			int line = sinfo.line400 ? gy % 200 : gy;
			int plane = sinfo.line400 ? (gy < 200 ? 0 : 1) : (planeon[0] ? 0 : planeon[1] ? 1 : 2);
			memmon->ShowAddressBank(0xc000 + line * 80 + gx / 8,
									MemoryViewer::Type(MemoryViewer::gvram0 + plane), hinst, owner);
		}
	}
	else if (tab == tab_text && (PtInRect(&tzbtn[0], POINT{ x, y }) || PtInRect(&tzbtn[1], POINT{ x, y })
			 || PtInRect(&tzbtn[2], POINT{ x, y }) || PtInRect(&tzbtn[3], POINT{ x, y }) || PtInRect(&tzbtn[4], POINT{ x, y })))
	{
		static const int tzvals[5] = { 0, 14, 18, 24, 32 };
		for (int i = 0; i < 5; i++)
			if (PtInRect(&tzbtn[i], POINT{ x, y }))
				SetTextZoom(tzvals[i]);
	}
	else if (tab == tab_text && PtInRect(&semibtn, POINT{ x, y }))
	{
		semimode = !semimode;
		InvalidateRect(hwnd, 0, false);
	}
	else if (tab == tab_text && memmon && PtInRect(&textrect, POINT{ x, y }) && PtInRect(&textview, POINT{ x, y }) && tcw && tch)
	{
		int col = (x - textrect.left) / tcw, row = (y - textrect.top) / tch;
		memmon->ShowAddress((textbase + row * textbytes + col) & 0xffff, hinst, owner);
	}
}

void VideoMonitor::SetZoom(int z, int px, int py)
{
	if (z == zoom)
		return;
	if (z && imgrect.right > imgrect.left && imgrect.bottom > imgrect.top)
	{
		// (px, py) の絵の位置 (割合) が拡大の後も同じ所に来るように
		px = Limit(px, int(imgarea.right) - 1, int(imgarea.left));
		py = Limit(py, int(imgarea.bottom) - 1, int(imgarea.top));
		double fx = double(px - imgrect.left) / (imgrect.right - imgrect.left);
		double fy = double(py - imgrect.top) / (imgrect.bottom - imgrect.top);
		scrollx = int(fx * Scale(640 * z)) - (px - imgarea.left);
		scrolly = int(fy * Scale(400 * z)) - (py - imgarea.top);
	}
	zoom = z;
	InvalidateRect(hwnd, 0, false);
}

void VideoMonitor::SetTextZoom(int z)
{
	if (z == textzoom)
		return;
	textzoom = z;
	hoverrow = hovercol = -1;
	hover[0] = 0;
	InvalidateRect(hwnd, 0, false);
}

void VideoMonitor::TextFont(HDC hdc, int px)
{
	if (tfont && px == tfontpx)
		return;
	if (tfont)
		DeleteObject(tfont);
	tfont = CreateFont(-px, 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS,
					   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "\x82\x6c\x82\x72 \x83\x53\x83\x56\x83\x62\x83\x4e");
	tfontpx = px;
	HGDIOBJ old = SelectObject(hdc, tfont);
	TEXTMETRIC tm;
	GetTextMetrics(hdc, &tm);
	tcw = Max(int(tm.tmAveCharWidth), 1);
	tch = Max(int(tm.tmHeight), 1);
	SelectObject(hdc, old);
}

void VideoMonitor::Scroll(int dx, int dy)
{
	if (tab == tab_text)
	{
		tscrollx += dx, tscrolly += dy;	// 範囲は描くときに合わせる
		InvalidateRect(hwnd, 0, false);
		return;
	}
	if (!zoom)
		return;
	scrollx += dx, scrolly += dy;		// 範囲は描くときに合わせる
	InvalidateRect(hwnd, 0, false);
}

// ---------------------------------------------------------------------------
//	窓の処理
//
LRESULT CALLBACK VideoMonitor::WndProcGate(HWND h, UINT m, WPARAM w, LPARAM l)
{
	VideoMonitor* v;
	if (m == WM_NCCREATE)
	{
		v = (VideoMonitor*) ((CREATESTRUCT*) l)->lpCreateParams;
		SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR) v);
		v->hwnd = h;
	}
	else
		v = (VideoMonitor*) GetWindowLongPtr(h, GWLP_USERDATA);
	return v ? v->WndProc(h, m, w, l) : DefWindowProc(h, m, w, l);
}

LRESULT VideoMonitor::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	// メニューバーの Pause/Play/Step のボタン
	if (buttons.Message(h, msg, wp, lp))
		return TRUE;
	if (msg == WM_TIMER && wp == buttontimer)
	{
		buttons.Update();
		return 0;
	}
	switch (msg)
	{
	case WM_CREATE:
		dpi = Dpi::GetForWindow(h);
		htab = CreateWindowEx(0, WC_TABCONTROL, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FOCUSNEVER,
							  0, 0, 100, 20, h, 0, hinst, 0);
		for (int i = 0; i < ntabs; i++)
		{
			TCITEM ti;
			memset(&ti, 0, sizeof(ti));
			ti.mask = TCIF_TEXT;
			ti.pszText = (LPSTR) tabnames[i];
			TabCtrl_InsertItem(htab, i, &ti);
		}
		TabCtrl_SetCurSel(htab, tab);
		MakeFont();
		Layout();
		return 0;

	case WM_SIZE:
		Layout();
		return 0;

	case WM_DPICHANGED:
		dpi = HIWORD(wp);
		MakeFont();
		{
			RECT* r = (RECT*) lp;
			SetWindowPos(h, 0, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		Layout();
		return 0;

	case WM_NOTIFY:
		if (((NMHDR*) lp)->hwndFrom == htab && ((NMHDR*) lp)->code == TCN_SELCHANGE)
		{
			tab = TabCtrl_GetCurSel(htab);
			hover[0] = 0;
			hoverrow = -1;
			InvalidateRect(h, 0, false);
		}
		return 0;

	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(h, &ps);
			RECT rc;
			GetClientRect(h, &rc);
			HDC mdc = CreateCompatibleDC(hdc);
			HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
			HGDIOBJ old = SelectObject(mdc, bmp);
			Paint(mdc, rc);
			// タブの部品の所は写さない (部品が自分で描く)
			RECT tr;
			GetWindowRect(htab, &tr);
			int top = tr.bottom - tr.top;
			BitBlt(hdc, 0, top, rc.right, rc.bottom - top, mdc, 0, top, SRCCOPY);
			SelectObject(mdc, old);
			DeleteObject(bmp);
			DeleteDC(mdc);
			EndPaint(h, &ps);
		}
		return 0;

	case WM_MOUSEMOVE:
		if (dragging)
		{
			int mx = (short) LOWORD(lp), my = (short) HIWORD(lp);
			Scroll(dragpt.x - mx, dragpt.y - my);
			dragpt.x = mx, dragpt.y = my;
			UpdateWindow(h);
		}
		OnMouseMove((short) LOWORD(lp), (short) HIWORD(lp));
		return 0;

	case WM_RBUTTONDOWN:
		if ((tab == tab_graphic && zoom) || tab == tab_text)
		{
			dragging = true;
			dragpt.x = (short) LOWORD(lp), dragpt.y = (short) HIWORD(lp);
			SetCapture(h);
		}
		return 0;

	case WM_RBUTTONUP:
	case WM_CAPTURECHANGED:
		if (dragging)
		{
			dragging = false;
			if (msg == WM_RBUTTONUP)
				ReleaseCapture();
		}
		return 0;

	case WM_MOUSEWHEEL:
		if (tab == tab_text && (LOWORD(wp) & MK_CONTROL))
		{
			// Ctrl+ホイールで文字の大きさを変える (Fit → 14 → 18 → 24 → 32)
			static const int zs[5] = { 0, 14, 18, 24, 32 };
			int i = 0;
			while (i < 4 && zs[i] != textzoom)
				i++;
			i = Limit(i + ((short) HIWORD(wp) > 0 ? 1 : -1), 4, 0);
			SetTextZoom(zs[i]);
			return 0;
		}
		if (tab == tab_text && captured)
		{
			// ホイールで縦・Shift+ホイールで横にスクロール
			int delta = (short) HIWORD(wp);
			if (LOWORD(wp) & MK_SHIFT)
				Scroll(-delta * Scale(80) / WHEEL_DELTA, 0);
			else
				Scroll(0, -delta * Scale(80) / WHEEL_DELTA);
			POINT pt = { (short) LOWORD(lp), (short) HIWORD(lp) };
			ScreenToClient(h, &pt);
			UpdateWindow(h);			// 描き直してから (絵・格子の位置が変わる) マウスの下を調べる
			OnMouseMove(pt.x, pt.y);
			return 0;
		}
		if (tab == tab_graphic && captured)
		{
			POINT pt = { (short) LOWORD(lp), (short) HIWORD(lp) };
			ScreenToClient(h, &pt);
			int delta = (short) HIWORD(wp);
			if (LOWORD(wp) & MK_CONTROL)
			{
				// Ctrl+ホイールで拡大・縮小 (Fit → x1 → x2 → x4 → x8)
				static const int zs[5] = { 0, 1, 2, 4, 8 };
				int i = 0;
				while (i < 4 && zs[i] != zoom)
					i++;
				i = Limit(i + (delta > 0 ? 1 : -1), 4, 0);
				SetZoom(zs[i], pt.x, pt.y);
			}
			else if (LOWORD(wp) & MK_SHIFT)
				Scroll(-delta * Scale(80) / WHEEL_DELTA, 0);
			else
				Scroll(0, -delta * Scale(80) / WHEEL_DELTA);
			UpdateWindow(h);			// 描き直してから (絵・格子の位置が変わる) マウスの下を調べる
			OnMouseMove(pt.x, pt.y);
		}
		return 0;

	case WM_LBUTTONDOWN:
		OnClick((short) LOWORD(lp), (short) HIWORD(lp));
		return 0;

	case WM_CLOSE:
		SavePlacement();
		DestroyWindow(h);
		return 0;

	case WM_DESTROY:
		SavePlacement();		// 本体を閉じると WM_CLOSE 無しで壊れるので、ここでも残す
		hwnd = 0;
		htab = 0;
		return 0;
	}
	return DefWindowProc(h, msg, wp, lp);
}
