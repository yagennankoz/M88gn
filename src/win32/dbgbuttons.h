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
};

}
