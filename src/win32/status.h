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
	void SetStopped(bool s);		// デバッガで止まっている間は地の色を変える
	UINT_PTR GetTimerID() { return timerid; }

	HWND GetHWnd() { return hwnd; }
	void Layout();					// 親の窓の下の端に置き直す (親の大きさが変わったとき)

	// 全画面: ステータスバーの代わりに、メニューバーの右の端に知らせとランプを出す
	bool EnableMenu(bool sfs);
	void DisableMenu();
	void MeasureMenuItem(MEASUREITEMSTRUCT* mis);
	void DrawMenuItem(DRAWITEMSTRUCT* dis);
	void RedrawMenu();			// 変わったところを描き直す (UI のスレッドから)

	// 全画面でメニューバーを隠しているとき: 知らせとランプを画面の右下に重ねて出す (M88gn)。
	// いつも出す。知らせが無く、ランプも灯っていない (消えてから 1 秒たった) 間は薄くする
	bool EnableOverlay(bool sfs);
	void DisableOverlay();
	void RedrawOverlay();		// 出す・隠す・描き直す (UI のスレッドから)

private:
	struct List
	{
		List* next;
		int priority;
		int duration;
		char msg[128];
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
	static LRESULT CALLBACK OverlayProc(HWND, UINT, WPARAM, LPARAM);
	void PaintOverlay(HDC hdc, const RECT& r);
	bool LampsLit();
	int LampWidth() { return Scale(showfdstat ? 80 : 64); }
	
	HWND hwnd;
	HWND hwndparent;
	List* list;
	UINT_PTR timerid;
	CriticalSection cs;
	Border border;
	int height;
	int dpi;					// ランプの大きさを合わせる画面の DPI (96 = 100%)
	HFONT statusfont;			// ステータスバーの字 (窓の DPI に合わせて作る)
	int baseheight;				// 作ったときにステータスバーが決めた高さ (basedpi のとき)
	int basedpi;				// システムの DPI (ステータスバーが高さを決めるのに使う DPI)
	int Scale(int x) { return MulDiv(x, dpi, 96); }
	int litstat[3];
	int litcurrent[3];
	bool showfdstat;
	bool stopped;
	bool updatemessage;

	int currentduration;
	int currentpriority;

	char buf[128];
	bool menumode;				// 全画面でメニューバーに出している
	HFONT menufont;
	char menutext[128];			// メニューバーに出す知らせ (重ねて出すときも)

	enum { overlayhold = 1000, overlayalpha = 208, overlayidlealpha = 96 };	// 知らせ・アクセスがあるとき / 無いとき (薄く)
	bool overlaymode;			// 全画面で画面の右下に重ねて出している
	HWND hwndov;
	HFONT ovfont;
	DWORD ovlastlit;			// ランプが最後に灯っていた時刻
};

extern StatusDisplay statusdisplay;

