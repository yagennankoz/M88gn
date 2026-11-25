// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	デバッグの窓のメニューバーの Pause/Play/Step In/Step Over/Step Out のボタン
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#pragma once

#include "types.h"

class WinCore;

namespace PC8801
{

class DebugButtons
{
public:
	DebugButtons();
	void Init(WinCore* c) { core = c; }

	void Attach(HWND hwnd, HINSTANCE hinst);	// 窓が開いた直後に呼ぶ (メニューは開くたびに作り直されるため)
	void Update();								// 押せる・押せないを合わせる (変わったときだけ描き直す)
	void Layout();								// 1 段に収まらなければ Pause の前で折り返す
	bool Message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);	// 窓の処理から。処理したら true
	static bool IsButton(uint id);
	HMENU Menu();				// 窓のメニュー (ドックしても覚えておいたもの。M88gn)
	// メニューバーの地を描く (M88gn。自分で描くほかの項目のため。テーマが無ければシステムの色)
	static void PaintBarBack(HWND hwnd, HDC dc, const RECT& r, bool active);
	// ペインの開閉ボタンの絵 (M88gn。本体のタイトルの段)。r は絵の四角 (16x16 の格子で描く)、
	// side は DockHost::Side、shown はペインを開いているか
	static void DrawPaneIcon(HDC dc, const RECT& r, int side, bool shown, COLORREF color);

private:
	void SetOwnerDraw();
	bool Measure(MEASUREITEMSTRUCT* mis);
	bool Draw(const DRAWITEMSTRUCT* dis);
	void MouseMove(POINT screen);
	void ShowTip(bool show);

	WinCore* core;
	HWND hwnd;
	HINSTANCE hinst;
	int state;					// 今の状態 (-1: まだ合わせていない)
	bool inlayout;				// Layout の中 (メニューバーの高さが変わると WM_SIZE がまた来るため)
	int layoutwidth;			// Layout で合わせたときの窓の幅 (-1: まだ)
	HWND hwndtip;				// 絵のツールチップ
	uint tipid;					// マウスが載っている項目 (0: 無し)
	bool tipshown;
	bool tiptracking;			// TrackMouseEvent で WM_NCMOUSELEAVE を待っている
	HMENU menu;					// 窓のメニュー (M88gn)
};

// メニューバーの Addr の欄 (M88gn。コード・メモリの窓で、マウスの下の番地を出す)。
// 自分で描く項目にして幅を一定にし (右寄せ、ボタンの左)、変わったらその欄だけ描き直す
class AddrBarItem
{
public:
	AddrBarItem();
	~AddrBarItem();
	void Setup(HWND hwnd, UINT id);		// 窓を開くたびに (ボタンの Attach の後)
	void SetText(const char* text);		// 変わったときだけ、その欄を描き直す
	bool Message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);	// 測る・描く。処理したら true (DWLP_MSGRESULT に TRUE を)

private:
	HFONT Font();
	void Paint(HDC dc, const RECT& r, bool active);
	HWND hwnd;
	UINT id;
	char text[64];
	HFONT font;
	UINT fontdpi;
};

}
