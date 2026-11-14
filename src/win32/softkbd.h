// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	ソフトウェアキーボード
// ---------------------------------------------------------------------------
//	PC-8801 のキーボード (FA 以降の型。PC・全角・f6～f10 などがあるもの) を描いた窓。
//	マウスで押すと、WinKeyIF を通して PC-8801 にキーを渡す。パソコンのキーボードで
//	押したキーも、PC-8801 に見えているとおりに光らせる。
//	M88 の窓に属するツールウィンドウで、M88 の外にも置ける。大きさを変えても縦横の比は保つ。
//	クリックしてもフォーカスを奪わない (パソコンのキーボードの入力は M88 に届いたまま)。
//	SHIFT・CTRL・GRPH は押すと次のキーまで保持し (もう一度押すと戻る)、素早く 2 回押すと固定
//	(iOS のシフトキーと同じ。固定したキーは枠を太くする)。
//	かな・大文字は押すたびに固定 / 戻る。

#pragma once

#include "types.h"

namespace PC8801
{
class WinKeyIF;
}

class SoftKeyboard
{
public:
	SoftKeyboard();
	~SoftKeyboard();

	bool Init(HINSTANCE hinst, HWND owner, PC8801::WinKeyIF* keyif);
	void Show(bool show);
	bool IsShown() { return visible; }		// 表示している (M88 の終わりで窓が先に閉じられても、表示していたことを覚えている)

	// 位置と大きさ (M88.ini に残す。w = 0 なら既定の大きさ)
	void SetPlacement(int x, int y, int w);
	void GetPlacement(int* x, int* y, int* w);

	struct KeyDef;

private:
	enum
	{
		mod_off = 0, mod_once, mod_lock,
		minhold = 60,				// マウスで押したキーを押したままにする最短の時間 (ms)
	};

	static LRESULT CALLBACK WndProcGate(HWND, UINT, WPARAM, LPARAM);
	LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);
	void Create();
	void Paint(HDC hdc);
	void Layout();
	void Sizing(WPARAM edge, RECT* rect);
	int HitTest(int x, int y);
	void KeyRect(int i, RECT* r, RECT* r2);
	void Press(int i);
	void Release();
	void SendSoft();

	HINSTANCE hinst;
	HWND owner;
	HWND hwnd;
	PC8801::WinKeyIF* keyif;

	int posx, posy, width;			// 窓の位置と幅 (width = 0 なら既定)
	bool placed;
	bool visible;

	double unit;					// キー 1 つ分の大きさ (ピクセル)
	double ox, oy;					// 描く範囲の左上 (ピクセル)
	HFONT fmain, fsmall, flabel;
	HBITMAP backbmp;
	int backw, backh;

	uint8 matrix[16];				// PC-8801 に見えているキー (表示用)
	int pressed;					// マウスで押しているキー (-1: 無し)
	DWORD presstime;				// 押した時刻 (GetTickCount)
	bool releasewait;				// マウスは離したが、minhold まで押したままにしている
	uint8* modstate;				// 修飾キーの状態 (キーごと)
	int lastmod;					// 最後に押した修飾キー (素早く 2 回押したかを見る)
	DWORD lastmodtime;
};
