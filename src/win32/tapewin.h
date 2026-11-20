// ---------------------------------------------------------------------------
//	M88gn - テープの窓
//	テープの位置 (カウンター)・長さ・状態と、頭出しできる所 (区切り) の一覧を出す。
//	一覧をクリックするとそこへ頭出しする。ボタンで Open・Rewind・前後の区切り・Record
// ---------------------------------------------------------------------------

#pragma once

#include "types.h"
#include "pc88/tapemgr.h"
#include <string>
#include <vector>

class WinCore;

class TapeWindow
{
public:
	TapeWindow();
	~TapeWindow();

	bool Init(HINSTANCE hinst, HWND owner, WinCore* core);
	void SetTape(TapeManager* t) { tape = t; }
	void Show(bool show);
	bool IsShown() { return visible; }		// 表示している (M88 の終わりで窓が先に閉じられても、表示していたことを覚えている)

	// 位置と大きさ (物理ピクセル。w = 0 なら既定)
	void SetPlacement(int x, int y, int w, int h);
	void GetPlacement(int* x, int* y, int* w, int* h);

private:
	enum
	{
		btn_open = 0, btn_rewind, btn_prev, btn_next, btn_rec, nbuttons,
		// 速さ (Normal/x2/x4/x8) と音量 (Off/25/50/75/100%) のプルダウン。ボタンの後ろに番号を続ける
		dd_speed = nbuttons, dd_vol, nitems,
		timerid = 1, interval = 200,		// 状態を読みに行く間隔 (ms)
		animid = 2, animinterval = 33,		// カセットの絵を動かす間隔 (ms。モーターが回っている間だけ)
	};
	struct State
	{
		bool open, wave, motor, recording, recarm, writeprotect, cancue;
		int speed;
		int volume;
		uint ms, length;
		std::string file;
	};

	static LRESULT CALLBACK WndProcGate(HWND, UINT, WPARAM, LPARAM);
	LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);
	void Create();
	void Refresh(bool force);
	void MakeFonts();
	void Layout();
	void Paint(HDC hdc);
	void PaintButton(HDC hdc, int i);
	void PaintDropdown(HDC hdc, int i);
	void ShowDropdown(int i);
	void DrawIcon(HDC hdc, int i, const RECT& r, bool enabled);
	bool ButtonEnabled(int i);
	int HitButton(int x, int y);
	int HitRow(int x, int y);
	void Press(int i);
	void CueRow(int row);
	int CurrentRow();
	void UpdateScroll();
	void DrawCassette(HDC hdc, const RECT& r);
	void Animate();
	void SetAnimate(bool on);
	void Scroll(int pos);
	int S(int x) { return MulDiv(x, dpi, 96); }

	HINSTANCE hinst;
	HWND owner;
	HWND hwnd;
	WinCore* core;
	TapeManager* tape;

	int posx, posy, width, height;
	bool placed;
	bool visible;
	int dpi;

	HFONT fnormal, fbig, fsmall;
	RECT rcfile, rccas, rccounter, rcbar, rclist;
	RECT rcbtn[nitems];				// ボタンとプルダウン
	RECT rcspeedlabel, rcvollabel;	// プルダウンの左の字 (Speed・Volume)
	int rowh;
	int scroll;						// 一覧の最初に見えている行
	int hoverbtn, pressbtn, hoverrow;
	bool tracking;

	State st;
	uint serial;
	std::vector<TapeManager::CueInfo> cues;
	int shownsec;					// 描いたカウンターの秒

	// カセットの絵 (M88gn)。リールはテープの送りの速さ / 巻きの半径で回る。
	// 位置は 200ms ごとに読むので、その間は読んだ位置の進み方 (rate) から求める
	bool animating;
	double angle[2];				// リールの角度 (ラジアン。左・右)
	double animms;					// 絵の上のテープの位置 (ms)
	double rate;					// テープの位置の進み方 (実時間 1ms あたりのテープの ms)
	double accms, accdt;			// 進み方を求めるための、1 秒分の位置の進みと実時間
	DWORD polltick, frametick;
	int current;					// 描いた今の行
};
