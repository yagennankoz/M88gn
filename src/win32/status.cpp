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
	menumode = false;
	menufont = 0;
	menutext[0] = 0;
	litstat[0] = litstat[1] = litstat[2] = 0;
	litcurrent[0] = litcurrent[1] = litcurrent[2] = 0;
	updatemessage = 0;
	showfdstat = false;
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
	}
	{
		showfdstat = showfd;

		SendMessage(hwnd, SB_GETBORDERS, 0, (LPARAM) &border);
		
		// ランプの欄の幅は画面の DPI に合わせる。位置は窓の中の幅から決める (枠の太さは DPI で変わる)
		dpi = Dpi::GetForWindow(hwndparent);
		RECT rect;
		GetClientRect(hwndparent, &rect);
		int widths[2];
		widths[0] = (rect.right-rect.left-border.vertical) - 1*(Scale(showfd ? 80 : 64)+border.split);
		widths[1] = -1;
		SendMessage(hwnd, SB_SETPARTS, 2, (LPARAM) widths);
		InvalidateRect(hwndparent, 0, false);
		
		GetWindowRect(hwnd, &rect);
		height = rect.bottom - rect.top;
	
		PostMessage(hwnd,  SB_SETTEXT, SBT_OWNERDRAW | 1, 0);
	}
	return true;
}

bool StatusDisplay::Disable()
{
	if (hwnd)
	{
		DestroyWindow(hwnd);
		hwnd = 0;
		height = 0;
	}
	return true;
}

void StatusDisplay::Cleanup()
{
	Disable();
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
void StatusDisplay::DrawItem(DRAWITEMSTRUCT* dis)
{
	switch (dis->itemID)
	{
	case 0:
		{
			SetBkColor(dis->hDC, GetSysColor(COLOR_3DFACE));
			SetTextColor(dis->hDC, GetSysColor(COLOR_MENUTEXT));
			char* text = reinterpret_cast<char*>(dis->itemData);
			TextOut(dis->hDC, dis->rcItem.left, dis->rcItem.top, text, strlen(text));
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
	NONCLIENTMETRICS ncm;
	memset(&ncm, 0, sizeof(ncm));
	ncm.cbSize = sizeof(ncm);
	SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
	menufont = CreateFontIndirect(&ncm.lfMenuFont);
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

	va_list marker;
	va_start(marker, msg);
	int tl = wvsprintf(entry->msg, msg, marker);
	assert(tl < 128);
	va_end(marker);

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
	if (hwnd || menumode)
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
	else if (menumode)
	{
		if (update)
			PostMessage(hwndparent, WM_M88_STATUSMENU, 0, 0);
		if (updatemessage)
			Update();
	}
}

// 
