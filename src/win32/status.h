// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1998, 1999.
// ---------------------------------------------------------------------------
//	$Id: status.h,v 1.6 2002/04/07 05:40:10 cisc Exp $

#pragma once

#include "types.h"
#include "critsect.h"

class StatusDisplay
{
public:
	StatusDisplay();
	~StatusDisplay();

	bool Init(HWND hwndparent);
	void Cleanup();

	bool Enable(bool sfs=false);
	bool Disable();
	int GetHeight() { return height; }
	void DrawItem(DRAWITEMSTRUCT* dis);
	void FDAccess(uint dr, bool hd, bool active);
	void UpdateDisplay();
	void WaitSubSys() { litstat[2] = 9; }

	bool Show(int priority, int duration, char* msg, ...);
	void Update();
	UINT_PTR GetTimerID() { return timerid; }

	HWND GetHWnd() { return hwnd; }

	// 全画面: ステータスバーの代わりに、メニューバーの右の端に知らせとランプを出す
	bool EnableMenu(bool sfs);
	void DisableMenu();
	void MeasureMenuItem(MEASUREITEMSTRUCT* mis);
	void DrawMenuItem(DRAWITEMSTRUCT* dis);
	void RedrawMenu();			// 変わったところを描き直す (UI のスレッドから)

private:
	struct List
	{
		List* next;
		int priority;
		int duration;
		char msg[127];
		bool clear;
	};
	struct Border
	{
		int horizontal;
		int vertical;
		int split;
	};

	void Clean();
	void DrawLamps(HDC hdc, int left, int cy);
	void DrawMenuStatus(HDC hdc, const RECT& r);
	int LampWidth() { return Scale(showfdstat ? 80 : 64); }
	
	HWND hwnd;
	HWND hwndparent;
	List* list;
	UINT_PTR timerid;
	CriticalSection cs;
	Border border;
	int height;
	int dpi;					// ランプの大きさを合わせる画面の DPI (96 = 100%)
	int Scale(int x) { return MulDiv(x, dpi, 96); }
	int litstat[3];
	int litcurrent[3];
	bool showfdstat;
	bool updatemessage;

	int currentduration;
	int currentpriority;

	char buf[128];
	bool menumode;				// 全画面でメニューバーに出している
	HFONT menufont;
	char menutext[128];			// メニューバーに出す知らせ
};

extern StatusDisplay statusdisplay;

