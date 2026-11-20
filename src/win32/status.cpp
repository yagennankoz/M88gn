// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1998, 1999.
// ---------------------------------------------------------------------------
//	$Id: status.cpp,v 1.8 2002/04/07 05:40:10 cisc Exp $

#include "headers.h"
#include "status.h"

//#define LOGNAME "status"
#include "diag.h"
#include "dpi.h"
#include "resource.h"
#include "messages.h"

#ifndef COLOR_MENUBAR
#define COLOR_MENUBAR	30		// メニューバーの地の色 (Windows XP 以降)
#endif

StatusDisplay statusdisplay;

// ---------------------------------------------------------------------------
//	Constructor/Destructor
//
StatusDisplay::StatusDisplay()
{
	hwnd = 0;
	hwndparent = 0;
	list = 0;
	timerid = 0;
	height = 0;
	dpi = 96;
	statusfont = 0;
	baseheight = 0;
	basedpi = 96;
	menumode = false;
	menufont = 0;
	menutext[0] = 0;
	overlaymode = false;
	hwndov = 0;
	ovfont = 0;
	ovlastlit = 0;
	litstat[0] = litstat[1] = litstat[2] = 0;
	litcurrent[0] = litcurrent[1] = litcurrent[2] = 0;
	updatemessage = 0;
	showfdstat = false;
	stopped = false;
	currentduration = 0;
	currentpriority = 10000;
}
	
StatusDisplay::~StatusDisplay()
{
	Cleanup();
	while (list)
	{
		List* next = list->next;
		delete list;
		list = next;
	}
}

bool StatusDisplay::Init(HWND hwndp)
{
	hwndparent = hwndp;
	return true;
}

bool StatusDisplay::Enable(bool showfd)
{
	if (!hwnd)
	{
		hwnd = CreateStatusWindow(WS_CHILD | WS_VISIBLE, 0, hwndparent, 1);
		
		if (!hwnd)
			return false;

		// ステータスバーは高さをシステムの DPI (主モニターの DPI) で決め、窓のある画面の DPI では
		// 決め直さない。作ったときの高さを覚えておき、高さと位置はこちらで決める
		RECT rect;
		GetWindowRect(hwnd, &rect);
		baseheight = rect.bottom - rect.top;
		HDC hdc = GetDC(0);
		basedpi = GetDeviceCaps(hdc, LOGPIXELSY);
		ReleaseDC(0, hdc);
		if (basedpi <= 0)
			basedpi = 96;
		SetWindowLong(hwnd, GWL_STYLE, GetWindowLong(hwnd, GWL_STYLE) | CCS_NORESIZE | CCS_NOPARENTALIGN);
	}
	{
		showfdstat = showfd;

		// 字は窓のある画面の DPI で作る (ステータスバーが自分で選ぶ字は主モニターの DPI のもの)
		dpi = Dpi::GetForWindow(hwndparent);
		NONCLIENTMETRICSW ncm;
		Dpi::GetNonClientMetrics(&ncm, dpi);
		HFONT font = CreateFontIndirectW(&ncm.lfStatusFont);
		if (font)
		{
			SendMessage(hwnd, WM_SETFONT, (WPARAM) font, FALSE);
			if (statusfont)
				DeleteObject(statusfont);
			statusfont = font;
		}

		SendMessage(hwnd, SB_GETBORDERS, 0, (LPARAM) &border);
		
		height = MulDiv(baseheight, dpi, basedpi);
		Layout();
		InvalidateRect(hwndparent, 0, false);
	
		PostMessage(hwnd,  SB_SETTEXT, SBT_OWNERDRAW | 1, 0);
	}
	return true;
}

void StatusDisplay::Layout()
{
	if (hwnd)
	{
		RECT rect;
		GetClientRect(hwndparent, &rect);
		SetWindowPos(hwnd, 0, 0, rect.bottom - height, rect.right, height, SWP_NOZORDER | SWP_NOACTIVATE);

		// ランプの欄の幅は画面の DPI に合わせ、位置は窓の中の幅から決める (枠の太さは DPI で変わる)。
		// 窓の幅は画面の回転・倍率で変わるので、大きさが変わるたびに決め直す
		int widths[2];
		widths[0] = (rect.right-rect.left-border.vertical) - 1*(LampWidth()+border.split);
		widths[1] = -1;
		SendMessage(hwnd, SB_SETPARTS, 2, (LPARAM) widths);
	}
}

bool StatusDisplay::Disable()
{
	if (hwnd)
	{
		DestroyWindow(hwnd);
		hwnd = 0;
		height = 0;
	}
	if (statusfont)
	{
		DeleteObject(statusfont);
		statusfont = 0;
	}
	return true;
}

void StatusDisplay::Cleanup()
{
	Disable();
	DisableOverlay();
	if (menufont)
	{
		DeleteObject(menufont);
		menufont = 0;
	}
	if (timerid) {
		KillTimer(hwndparent, timerid);
		timerid = 0;
	}
}

// ---------------------------------------------------------------------------
//	DrawItem
//
void StatusDisplay::SetStopped(bool s)
{
	if (stopped == s)
		return;
	stopped = s;
	if (hwnd)
		InvalidateRect(hwnd, 0, true);
	if (overlaymode)
		PostMessage(hwndparent, WM_M88_STATUSMENU, 0, 0);
}

void StatusDisplay::DrawItem(DRAWITEMSTRUCT* dis)
{
	switch (dis->itemID)
	{
	case 0:
		{
			// デバッガで止まっている間は地の色を変える (M88am と同じ色)
			COLORREF bk = stopped ? RGB(0xe6, 0x82, 0xc8) : GetSysColor(COLOR_3DFACE);
			SetBkColor(dis->hDC, bk);
			SetTextColor(dis->hDC, stopped ? RGB(0, 0, 0) : GetSysColor(COLOR_MENUTEXT));
			char* text = reinterpret_cast<char*>(dis->itemData);
			// 自分で描く欄の DC にはステータスバーの字が選ばれていないので、窓の DPI の字を選ぶ
			HGDIOBJ oldfont = statusfont ? SelectObject(dis->hDC, statusfont) : 0;
			if (stopped)
				ExtTextOut(dis->hDC, dis->rcItem.left, dis->rcItem.top, ETO_OPAQUE, &dis->rcItem, text, strlen(text), 0);
			else
				TextOut(dis->hDC, dis->rcItem.left, dis->rcItem.top, text, strlen(text));
			if (oldfont)
				SelectObject(dis->hDC, oldfont);
			break;
		}
	case 1:
		{
			DrawLamps(dis->hDC, dis->rcItem.left, height/2);
			break;
		}
	}
}

// ---------------------------------------------------------------------------
//	ランプ (左から FDD 2・FDD 1・FDC) を描く。left: 欄の左、cy: 縦の真ん中
//
void StatusDisplay::DrawLamps(HDC hdc, int left, int cy)
{
	const DWORD color[] =
	{
		RGB(64, 0, 0), RGB(255, 0, 0), RGB(0, 64, 0), RGB(0, 255, 0),
		RGB(0, 32, 64), RGB(0, 128, 255),
	};

	HBRUSH hbrush1, hbrush2, hbrush3, hbrushold;
	hbrush1 = CreateSolidBrush(color[litcurrent[1] & 3]);
	hbrush2 = CreateSolidBrush(color[litcurrent[0] & 3]);
	hbrush3 = CreateSolidBrush(color[4 | (litcurrent[2] & 1)]);
	hbrushold = (HBRUSH) SelectObject(hdc, hbrush1);
	int y0 = cy - Scale(4), y1 = cy + Scale(5);
	Rectangle(hdc, left+Scale(6), y0, left + Scale(24), y1);
	SelectObject(hdc, hbrush2);
	Rectangle(hdc, left+Scale(32), y0, left + Scale(50), y1);
	if (showfdstat)
	{
		SelectObject(hdc, hbrush3);
		Rectangle(hdc, left+Scale(58), y0, left + Scale(68), y1);
	}
	SelectObject(hdc, hbrushold);
	DeleteObject(hbrush1);
	DeleteObject(hbrush2);
	DeleteObject(hbrush3);
}

// ---------------------------------------------------------------------------
//	全画面: メニューバーの右の端に、知らせとランプの項目を足す
//	(右に寄せた、自分で描く項目。押せないようにしておく)
//
bool StatusDisplay::EnableMenu(bool showfd)
{
	HMENU hmenu = GetMenu(hwndparent);
	if (!hmenu)
		return false;
	showfdstat = showfd;
	dpi = Dpi::GetForWindow(hwndparent);
	if (menufont)
		DeleteObject(menufont);
	NONCLIENTMETRICSW ncm;
	Dpi::GetNonClientMetrics(&ncm, dpi);
	menufont = CreateFontIndirectW(&ncm.lfMenuFont);
	if (!menumode)
	{
		AppendMenu(hmenu, MF_OWNERDRAW | MF_RIGHTJUSTIFY | MF_DISABLED, IDM_STATUS_MENU, 0);
		menumode = true;
	}
	DrawMenuBar(hwndparent);
	return true;
}

void StatusDisplay::DisableMenu()
{
	if (!menumode)
		return;
	menumode = false;
	HMENU hmenu = GetMenu(hwndparent);
	if (hmenu)
	{
		RemoveMenu(hmenu, IDM_STATUS_MENU, MF_BYCOMMAND);
		DrawMenuBar(hwndparent);
	}
}

void StatusDisplay::MeasureMenuItem(MEASUREITEMSTRUCT* mis)
{
	// 知らせの欄は、平均の字の幅で 48 字ぶん
	HDC hdc = GetDC(hwndparent);
	HGDIOBJ old = SelectObject(hdc, menufont ? (HGDIOBJ) menufont : GetStockObject(DEFAULT_GUI_FONT));
	TEXTMETRIC tm;
	GetTextMetrics(hdc, &tm);
	SelectObject(hdc, old);
	ReleaseDC(hwndparent, hdc);
	mis->itemWidth = tm.tmAveCharWidth * 48 + LampWidth();
	mis->itemHeight = tm.tmHeight;
}

void StatusDisplay::DrawMenuItem(DRAWITEMSTRUCT* dis)
{
	DrawMenuStatus(dis->hDC, dis->rcItem);
}

void StatusDisplay::DrawMenuStatus(HDC hdc, const RECT& r)
{
	char text[128];
	{
		CriticalSection::Lock lock(cs);
		memcpy(text, menutext, sizeof(text));
	}
	FillRect(hdc, &r, GetSysColorBrush(COLOR_MENUBAR));
	int lw = LampWidth();
	RECT tr = r;
	tr.right -= lw;
	HGDIOBJ old = SelectObject(hdc, menufont ? (HGDIOBJ) menufont : GetStockObject(DEFAULT_GUI_FONT));
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, GetSysColor(COLOR_MENUTEXT));
	DrawText(hdc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS | DT_NOPREFIX);
	SelectObject(hdc, old);
	DrawLamps(hdc, r.right - lw, (r.top + r.bottom) / 2);
}

// 知らせ・ランプが変わったとき (UI のスレッドから。項目のところだけ描く)
void StatusDisplay::RedrawMenu()
{
	if (!menumode)
		return;
	HMENU hmenu = GetMenu(hwndparent);
	int n = hmenu ? GetMenuItemCount(hmenu) : 0;
	if (n <= 0 || GetMenuItemID(hmenu, n - 1) != IDM_STATUS_MENU)
		return;
	RECT r, wr;
	if (!GetMenuItemRect(hwndparent, hmenu, n - 1, &r))
		return;
	GetWindowRect(hwndparent, &wr);
	OffsetRect(&r, -wr.left, -wr.top);
	HDC hdc = GetWindowDC(hwndparent);
	DrawMenuStatus(hdc, r);
	ReleaseDC(hwndparent, hdc);
}

// ---------------------------------------------------------------------------
//	全画面でメニューバーを隠しているとき: 知らせとランプを画面の右下に重ねて出す (M88gn)。
//	半透明の窓 (レイヤードウィンドウ) で、マウスは下の窓に通す。フォーカスは取らない
//
bool StatusDisplay::EnableOverlay(bool showfd)
{
	showfdstat = showfd;
	dpi = Dpi::GetForWindow(hwndparent);
	NONCLIENTMETRICSW ncm;
	Dpi::GetNonClientMetrics(&ncm, dpi);
	HFONT font = CreateFontIndirectW(&ncm.lfStatusFont);
	if (font)
	{
		if (ovfont)
			DeleteObject(ovfont);
		ovfont = font;
	}
	if (!hwndov)
	{
		static bool registered = false;
		HINSTANCE hinst = (HINSTANCE) GetWindowLongPtr(hwndparent, GWLP_HINSTANCE);
		if (!registered)
		{
			WNDCLASS wc;
			memset(&wc, 0, sizeof(wc));
			wc.lpfnWndProc = OverlayProc;
			wc.hInstance = hinst;
			wc.hCursor = LoadCursor(0, IDC_ARROW);
			wc.lpszClassName = "M88gn StatusOverlay";
			registered = RegisterClass(&wc) != 0;
			if (!registered)
				return false;
		}
		hwndov = CreateWindowEx(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
								"M88gn StatusOverlay", "", WS_POPUP, 0, 0, 1, 1, hwndparent, 0, hinst, 0);
		if (!hwndov)
			return false;
		SetLayeredWindowAttributes(hwndov, 0, overlayalpha, LWA_ALPHA);
	}
	overlaymode = true;
	ovlastlit = 0;
	RedrawOverlay();
	return true;
}

void StatusDisplay::DisableOverlay()
{
	overlaymode = false;
	if (hwndov)
	{
		DestroyWindow(hwndov);
		hwndov = 0;
	}
	if (ovfont)
	{
		DeleteObject(ovfont);
		ovfont = 0;
	}
}

bool StatusDisplay::LampsLit()
{
	return (litcurrent[0] & 1) || (litcurrent[1] & 1) || (showfdstat && (litcurrent[2] & 1));
}

void StatusDisplay::RedrawOverlay()
{
	if (!overlaymode || !hwndov)
		return;
	char text[128];
	{
		CriticalSection::Lock lock(cs);
		memcpy(text, menutext, sizeof(text));
	}
	// 知らせが空白だけなら、無いものとする
	bool hastext = false;
	for (const char* p = text; *p; p++)
		if (*p != ' ')
			hastext = true;

	DWORD now = GetTickCount();
	bool lit = LampsLit();
	if (lit)
		ovlastlit = now;
	// ランプが消えてもすぐには隠さない (読み書きのたびに出たり消えたりしないように)
	DWORD since = now - ovlastlit;
	bool holding = ovlastlit && since < overlayhold;
	// いつも出す。知らせもアクセスも無い間は薄くする (ユーザーの要望)
	bool active = hastext || lit || holding || stopped;
	SetLayeredWindowAttributes(hwndov, 0, active ? overlayalpha : overlayidlealpha, LWA_ALPHA);
	if (!hastext && !lit && holding)
		SetTimer(hwndov, 1, overlayhold - since, 0);

	// 大きさ: 字の幅 + ランプの欄。位置: 本体の窓の中の右下
	HDC hdc = GetDC(hwndov);
	HGDIOBJ old = SelectObject(hdc, ovfont ? (HGDIOBJ) ovfont : GetStockObject(DEFAULT_GUI_FONT));
	TEXTMETRIC tm;
	GetTextMetrics(hdc, &tm);
	SIZE ts = { 0, 0 };
	if (hastext)
		GetTextExtentPoint32(hdc, text, strlen(text), &ts);
	SelectObject(hdc, old);
	ReleaseDC(hwndov, hdc);

	int pad = Scale(8);
	int w = (hastext ? ts.cx + pad * 2 : pad) + LampWidth();
	int h = tm.tmHeight + Scale(8);
	RECT cr;
	GetClientRect(hwndparent, &cr);
	POINT br = { cr.right, cr.bottom };
	ClientToScreen(hwndparent, &br);
	int margin = Scale(12);
	SetWindowPos(hwndov, HWND_TOPMOST, br.x - margin - w, br.y - margin - h, w, h,
				 SWP_NOACTIVATE | SWP_SHOWWINDOW);
	InvalidateRect(hwndov, 0, FALSE);
	UpdateWindow(hwndov);
}

void StatusDisplay::PaintOverlay(HDC hdc, const RECT& r)
{
	char text[128];
	{
		CriticalSection::Lock lock(cs);
		memcpy(text, menutext, sizeof(text));
	}
	// 地は濃い灰色 (デバッガで止まっている間はステータスバーと同じ色)
	HBRUSH bk = CreateSolidBrush(stopped ? RGB(0xe6, 0x82, 0xc8) : RGB(32, 32, 32));
	FillRect(hdc, &r, bk);
	DeleteObject(bk);

	int lw = LampWidth();
	RECT tr = r;
	tr.left += Scale(8);
	tr.right -= lw;
	HGDIOBJ old = SelectObject(hdc, ovfont ? (HGDIOBJ) ovfont : GetStockObject(DEFAULT_GUI_FONT));
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, stopped ? RGB(0, 0, 0) : RGB(240, 240, 240));
	DrawText(hdc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
	SelectObject(hdc, old);
	DrawLamps(hdc, r.right - lw, (r.top + r.bottom) / 2);
}

LRESULT CALLBACK StatusDisplay::OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);
			RECT r;
			GetClientRect(hwnd, &r);
			statusdisplay.PaintOverlay(hdc, r);
			EndPaint(hwnd, &ps);
			return 0;
		}
	case WM_ERASEBKGND:
		return 1;
	case WM_TIMER:
		KillTimer(hwnd, wp);
		statusdisplay.RedrawOverlay();
		return 0;
	case WM_NCHITTEST:
		return HTTRANSPARENT;
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;
	}
	return DefWindowProc(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
//	メッセージ追加
//
bool StatusDisplay::Show(int priority, int duration, char* msg, ...)
{
	CriticalSection::Lock lock(cs);
	
	if (currentpriority < priority)
		if (!duration || (GetTickCount() + duration - currentduration) < 0)
			return true;

	List* entry = new List;
	if (!entry)
		return false;
	memset(entry, 0, sizeof(List));

	// 知らせの置き場 (128 バイト) に収まるように切る。録音中のファイル名などが長いと、
	// 前は置き場の外まで書いてヒープを壊していた (M88 の元から)。2 バイト文字の途中では切らない
	char text[1024];
	va_list marker;
	va_start(marker, msg);
	_vsnprintf(text, sizeof(text) - 1, msg, marker);
	va_end(marker);
	text[sizeof(text) - 1] = 0;
	int tl = 0;
	while (text[tl])
	{
		int n = (IsDBCSLeadByte(BYTE(text[tl])) && text[tl + 1]) ? 2 : 1;
		if (tl + n > int(sizeof(entry->msg)) - 1)
			break;
		tl += n;
	}
	memcpy(entry->msg, text, tl);
	entry->msg[tl] = 0;

	entry->duration = GetTickCount() + duration;
	entry->priority = priority;
	entry->next = list;
	entry->clear = duration != 0;
	list = entry;

	LOG3("reg : [%s] p:%5d d:%8d\n", entry->msg, entry->priority, entry->duration);
	updatemessage = true;
	return true;
}

// ---------------------------------------------------------------------------
//	更新
//
void StatusDisplay::Update()
{
	updatemessage = false;
	if (hwnd || menumode || overlaymode)
	{
		CriticalSection::Lock lock(cs);
		// find highest priority (0 == highest)
		int pc = 10000;
		List* entry = 0;
		int c = GetTickCount();
		for (List* l = list; l; l=l->next)
		{
//			LOG3("\t\t[%s] p:%5d d:%8d\n", l->msg, l->priority, l->duration);
			if ((l->priority < pc) && ((!l->clear) || (l->duration - c) > 0))
				entry = l, pc = l->priority;
		}
		if (entry)
		{
			LOG3("show: [%s] p:%5d d:%8d\n", entry->msg, entry->priority, entry->duration);
			memcpy(buf, entry->msg, 128);
			if (hwnd)
				PostMessage(hwnd,  SB_SETTEXT, SBT_OWNERDRAW | 0, (LPARAM) buf);
			else
			{
				memcpy(menutext, entry->msg, 128);
				PostMessage(hwndparent, WM_M88_STATUSMENU, 0, 0);
			}
			
			if (entry->clear)
			{
				timerid = ::SetTimer(hwndparent, 8, entry->duration - c, 0);
				currentpriority = entry->priority;
				currentduration = entry->duration;
			}
			else
			{
				currentpriority = 10000;
				if (timerid)
				{
					KillTimer(hwndparent, timerid);
					timerid = 0;
				}
			}
			Clean();
		}
		else
		{
			LOG0("clear\n");
			if (hwnd)
				PostMessage(hwnd, SB_SETTEXT, 0, (LPARAM) " ");
			else
			{
				menutext[0] = 0;
				PostMessage(hwndparent, WM_M88_STATUSMENU, 0, 0);
			}
			currentpriority = 10000;
			if (timerid)
			{
				KillTimer(hwndparent, timerid);
				timerid = 0;
			}
		}
	}
}

// ---------------------------------------------------------------------------
//	必要ないエントリの削除
//
void StatusDisplay::Clean()
{
	List** prev = &list;
	int c = GetTickCount();
	while (*prev)
	{
		if ((((*prev)->duration - c) < 0) || !(*prev)->clear)
		{
			List* de = *prev;
			*prev = de->next;
			delete de;
		}
		else
			prev = &(*prev)->next;
	}
}

// ---------------------------------------------------------------------------
//
//
void StatusDisplay::FDAccess(uint dr, bool hd, bool active)
{
	dr &= 1;
	if (!(litstat[dr] & 4))
	{
		litstat[dr] = (hd ? 0x22 : 0) | (active ? 0x11 : 0) | 4;
	}
	else
	{
		litstat[dr] = (litstat[dr] & 0x0f) | (hd ? 0x20 : 0) | (active ? 0x10 : 0) | 9;
	}
}

void StatusDisplay::UpdateDisplay()
{
	bool update = false;
	for (int d=0; d<3; d++)
	{
		if ((litstat[d] ^ litcurrent[d]) & 3)
		{
			litcurrent[d] = litstat[d];
			update = true;
		}
		if (litstat[d] & 8)
			litstat[d] >>= 4;
	}
	if (hwnd)
	{
		if (update)
			PostMessage(hwnd,  SB_SETTEXT, SBT_OWNERDRAW | 1, 0);
		if (updatemessage)
			Update();
	}
	else if (menumode || overlaymode)
	{
		if (update)
			PostMessage(hwndparent, WM_M88_STATUSMENU, 0, 0);
		if (updatemessage)
			Update();
	}
}

// 
