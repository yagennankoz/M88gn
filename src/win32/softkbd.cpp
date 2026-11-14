// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	ソフトウェアキーボード
// ---------------------------------------------------------------------------

#include "headers.h"
#include "softkbd.h"
#include "WinKeyIF.h"
#include "misc.h"

// ---------------------------------------------------------------------------
//	キーの並び
//	位置と幅はキー 1 つ分を 1 とした値。実機 (FA 以降の型) の写真から読んだ。
//	bits: 押すビット (ポート x 16 + ビット)。M88 の 106 キーの表 (WinKeyIF.cpp) に合わせる
//	(後の型で足したキーは、古いソフトのために前からあるキーのビットも押す。例: f6 = SHIFT + f1)
//
#define P(port, bit)	((port) * 16 + (bit))
#define END				0xff

enum { c_light = 0, c_dark };
enum { k_normal = 0, k_mod, k_lock, k_return };

struct SoftKeyboard::KeyDef
{
	float x, y, w;
	uint8 color, kind;
	const char* label;			// キーの文字 (文字キーは中段の左、ほかは真ん中。\n で 2 行)
	const char* shift;			// 文字キーの上段の真ん中の記号 (SHIFT で入る文字)
	const char* kana;			// 文字キーの下段の真ん中のかな
	uint8 bits[4];
	const char* kana2;			// 文字キーの中段の右の文字 (SHIFT のかな: 小さいかな・を・句読点)
};

static const float RF = 0.0f, RN = 1.25f, RQ = 2.25f, RA = 3.25f, RZ = 4.25f, RB = 5.25f;
static const float FW = 1.225f;					// ファンクションキーの幅
static const float TX = 19.05f;					// テンキーの左
static const float RQX = 13.8f, RAX = 14.1f;	// RETURN の左 (上の段 / 下の段)
static const float MW = 15.25f;					// 本体の部分の右
static const double totalw = 23.55, totalh = 6.75, margin = 0.25;

static const SoftKeyboard::KeyDef keys[] =
{
	// ファンクションキーの段
	{ 0.0f,  RF, 1.0f, c_dark, k_normal, "停止",		0, 0, { P(9,0), END } },
	{ 1.2f,  RF, 1.0f, c_dark, k_normal, "画面\nコピー", 0, 0, { P(10,4), END } },
	{ 2.6f + FW*0, RF, FW, c_dark, k_normal, "f･1", 0, 0, { P(9,1), END } },
	{ 2.6f + FW*1, RF, FW, c_dark, k_normal, "f･2", 0, 0, { P(9,2), END } },
	{ 2.6f + FW*2, RF, FW, c_dark, k_normal, "f･3", 0, 0, { P(9,3), END } },
	{ 2.6f + FW*3, RF, FW, c_dark, k_normal, "f･4", 0, 0, { P(9,4), END } },
	{ 2.6f + FW*4, RF, FW, c_dark, k_normal, "f･5", 0, 0, { P(9,5), END } },
	{ MW - FW*5, RF, FW, c_dark, k_normal, "f･6",  0, 0, { P(8,6), P(9,1), P(12,0), END } },
	{ MW - FW*4, RF, FW, c_dark, k_normal, "f･7",  0, 0, { P(8,6), P(9,2), P(12,1), END } },
	{ MW - FW*3, RF, FW, c_dark, k_normal, "f･8",  0, 0, { P(8,6), P(9,3), P(12,2), END } },
	{ MW - FW*2, RF, FW, c_dark, k_normal, "f･9",  0, 0, { P(8,6), P(9,4), P(12,3), END } },
	{ MW - FW*1, RF, FW, c_dark, k_normal, "f･10", 0, 0, { P(8,6), P(9,5), P(12,4), END } },
	{ 16.2f, RF, 1.0f, c_dark, k_normal, "ROLL\nUP",   0, 0, { P(11,0), END } },
	{ 17.2f, RF, 1.0f, c_dark, k_normal, "ROLL\nDOWN", 0, 0, { P(11,1), END } },

	// 数字の段
	{ 0.0f,   RN, 1.25f, c_dark,  k_normal, "ESC", 0, 0, { P(9,7), END } },
	{ 1.25f,  RN, 1.0f, c_light, k_normal, "1", "!",  "ぬ", { P(6,1), END } },
	{ 2.25f,  RN, 1.0f, c_light, k_normal, "2", "\"", "ふ", { P(6,2), END } },
	{ 3.25f,  RN, 1.0f, c_light, k_normal, "3", "#", "あ", { P(6,3), END }, "ぁ" },
	{ 4.25f,  RN, 1.0f, c_light, k_normal, "4", "$", "う", { P(6,4), END }, "ぅ" },
	{ 5.25f,  RN, 1.0f, c_light, k_normal, "5", "%", "え", { P(6,5), END }, "ぇ" },
	{ 6.25f,  RN, 1.0f, c_light, k_normal, "6", "&", "お", { P(6,6), END }, "ぉ" },
	{ 7.25f,  RN, 1.0f, c_light, k_normal, "7", "'", "や", { P(6,7), END }, "ゃ" },
	{ 8.25f,  RN, 1.0f, c_light, k_normal, "8", "(", "ゆ", { P(7,0), END }, "ゅ" },
	{ 9.25f,  RN, 1.0f, c_light, k_normal, "9", ")", "よ", { P(7,1), END }, "ょ" },
	{ 10.25f, RN, 1.0f, c_light, k_normal, "0", 0, "わ", { P(6,0), END }, "を" },
	{ 11.25f, RN, 1.0f, c_light, k_normal, "-", "=",  "ほ", { P(5,7), END } },
	{ 12.25f, RN, 1.0f, c_light, k_normal, "^", 0, "へ", { P(5,6), END }, 0 },
	{ 13.25f, RN, 1.0f, c_light, k_normal, "\\", "|", "ー", { P(5,4), END } },	// \ は円記号と表示される。| は 途中が切れた縦棒 (Shift_JIS に無いので描くときに置き換える)
	{ 14.25f, RN, 1.0f, c_dark,  k_normal, "後退", 0, 0, { P(8,3), P(12,5), END } },
	{ 16.2f,  RN, 1.0f, c_dark,  k_normal, "挿入", 0, 0, { P(8,3), P(8,6), P(12,6), END } },
	{ 17.2f,  RN, 1.0f, c_dark,  k_normal, "削除", 0, 0, { P(8,3), P(12,7), END } },

	// Q の段
	{ 0.0f,   RQ, 1.8f, c_dark,  k_normal, "タブ", 0, 0, { P(10,0), END } },
	{ 1.8f,   RQ, 1.0f, c_light, k_normal, "Q", 0, "た", { P(4,1), END } },
	{ 2.8f,   RQ, 1.0f, c_light, k_normal, "W", 0, "て", { P(4,7), END } },
	{ 3.8f,   RQ, 1.0f, c_light, k_normal, "E", 0, "い", { P(2,5), END }, "ぃ" },
	{ 4.8f,   RQ, 1.0f, c_light, k_normal, "R", 0, "す", { P(4,2), END } },
	{ 5.8f,   RQ, 1.0f, c_light, k_normal, "T", 0, "か", { P(4,4), END } },
	{ 6.8f,   RQ, 1.0f, c_light, k_normal, "Y", 0, "ん", { P(5,1), END } },
	{ 7.8f,   RQ, 1.0f, c_light, k_normal, "U", 0, "な", { P(4,5), END } },
	{ 8.8f,   RQ, 1.0f, c_light, k_normal, "I", 0, "に", { P(3,1), END } },
	{ 9.8f,   RQ, 1.0f, c_light, k_normal, "O", 0, "ら", { P(3,7), END } },
	{ 10.8f,  RQ, 1.0f, c_light, k_normal, "P", 0, "せ", { P(4,0), END } },
	{ 11.8f,  RQ, 1.0f, c_light, k_normal, "@", "~", "゛", { P(2,0), END }, 0 },
	{ 12.8f,  RQ, 1.0f, c_light, k_normal, "[", "{", "゜", { P(5,3), END }, "「" },
	{ RQX,    RQ, MW - RQX, c_dark, k_return, 0, 0, 0, { P(1,7), P(14,0), END } },	// RETURN (L 字)

	// A の段
	{ 0.0f,   RA, 1.05f, c_dark,  k_mod,  "コント\nロール", 0, 0, { P(8,7), END } },
	{ 1.05f,  RA, 1.05f, c_dark,  k_lock, "大文字", 0, 0, { P(10,7), END } },
	{ 2.1f,   RA, 1.0f, c_light, k_normal, "A", 0, "ち", { P(2,1), END } },
	{ 3.1f,   RA, 1.0f, c_light, k_normal, "S", 0, "と", { P(4,3), END } },
	{ 4.1f,   RA, 1.0f, c_light, k_normal, "D", 0, "し", { P(2,4), END } },
	{ 5.1f,   RA, 1.0f, c_light, k_normal, "F", 0, "は", { P(2,6), END } },
	{ 6.1f,   RA, 1.0f, c_light, k_normal, "G", 0, "き", { P(2,7), END } },
	{ 7.1f,   RA, 1.0f, c_light, k_normal, "H", 0, "く", { P(3,0), END } },
	{ 8.1f,   RA, 1.0f, c_light, k_normal, "J", 0, "ま", { P(3,2), END } },
	{ 9.1f,   RA, 1.0f, c_light, k_normal, "K", 0, "の", { P(3,3), END } },
	{ 10.1f,  RA, 1.0f, c_light, k_normal, "L", 0, "り", { P(3,4), END } },
	{ 11.1f,  RA, 1.0f, c_light, k_normal, ";", "+", "れ", { P(7,3), END } },
	{ 12.1f,  RA, 1.0f, c_light, k_normal, ":", "*", "け", { P(7,2), END } },
	{ 13.1f,  RA, 1.0f, c_light, k_normal, "]", "}", "む", { P(5,5), END }, "」" },

	// Z の段
	{ 0.0f,   RZ, 2.55f, c_dark,  k_mod, "シフト", 0, 0, { P(8,6), P(14,2), END } },
	{ 2.55f,  RZ, 1.0f, c_light, k_normal, "Z", 0, "つ", { P(5,2), END }, "っ" },
	{ 3.55f,  RZ, 1.0f, c_light, k_normal, "X", 0, "さ", { P(5,0), END } },
	{ 4.55f,  RZ, 1.0f, c_light, k_normal, "C", 0, "そ", { P(2,3), END } },
	{ 5.55f,  RZ, 1.0f, c_light, k_normal, "V", 0, "ひ", { P(4,6), END } },
	{ 6.55f,  RZ, 1.0f, c_light, k_normal, "B", 0, "こ", { P(2,2), END } },
	{ 7.55f,  RZ, 1.0f, c_light, k_normal, "N", 0, "み", { P(3,6), END } },
	{ 8.55f,  RZ, 1.0f, c_light, k_normal, "M", 0, "も", { P(3,5), END } },
	{ 9.55f,  RZ, 1.0f, c_light, k_normal, ",", "<", "ね", { P(7,4), END }, "、" },
	{ 10.55f, RZ, 1.0f, c_light, k_normal, ".", ">", "る", { P(7,5), END }, "。" },
	{ 11.55f, RZ, 1.0f, c_light, k_normal, "/", "?", "め", { P(7,6), END }, "・" },
	{ 12.55f, RZ, 1.0f, c_light, k_normal, "", "_", "ろ", { P(7,7), END } },	// _ は SHIFT で入る
	{ 13.55f, RZ, MW - 13.55f, c_dark, k_mod, "シフト", 0, 0, { P(8,6), P(14,3), END } },

	// いちばん下の段 (スペースバーは 3 つに分かれている: 決定・スペース・変換)
	{ 1.5f,   RB, 1.0f, c_dark,  k_lock, "かな", 0, 0, { P(8,5), END } },
	{ 2.5f,   RB, 1.0f, c_dark,  k_mod,  "GRPH", 0, 0, { P(8,4), END } },
	{ 3.6f,   RB, 2.0f, c_light, k_normal, "決定", 0, 0, { P(9,6), P(13,1), END } },
	{ 5.7f,   RB, 2.5f, c_light, k_normal, "", 0, 0, { P(9,6), END } },
	{ 8.3f,   RB, 2.5f, c_light, k_normal, "変換", 0, 0, { P(9,6), P(13,0), END } },
	{ 10.9f,  RB, 1.0f, c_dark,  k_normal, "PC",   0, 0, { P(13,2), END } },
	{ 11.9f,  RB, 1.0f, c_dark,  k_normal, "全角", 0, 0, { P(13,3), END } },

	// カーソルキー
	{ 16.68f, RA, 1.0f, c_dark, k_normal, "↑", 0, 0, { P(8,1), END } },
	{ 15.68f, RZ, 1.0f, c_dark, k_normal, "←", 0, 0, { P(10,2), END } },
	{ 16.68f, RZ, 1.0f, c_dark, k_normal, "↓", 0, 0, { P(10,1), END } },
	{ 17.68f, RZ, 1.0f, c_dark, k_normal, "→", 0, 0, { P(8,2), END } },

	// テンキー
	{ TX + 0, RN, 1.0f, c_dark,  k_normal, "画面\n消去", 0, 0, { P(8,0), END } },
	{ TX + 1, RN, 1.0f, c_dark,  k_normal, "説明", 0, 0, { P(10,3), END } },
	{ TX + 2, RN, 1.0f, c_dark,  k_normal, "-", 0, 0, { P(10,5), END } },
	{ TX + 3, RN, 1.0f, c_dark,  k_normal, "/", 0, 0, { P(10,6), END } },
	{ TX + 0, RQ, 1.0f, c_light, k_normal, "7", 0, 0, { P(0,7), END } },
	{ TX + 1, RQ, 1.0f, c_light, k_normal, "8", 0, 0, { P(1,0), END } },
	{ TX + 2, RQ, 1.0f, c_light, k_normal, "9", 0, 0, { P(1,1), END } },
	{ TX + 3, RQ, 1.0f, c_dark,  k_normal, "*", 0, 0, { P(1,2), END } },
	{ TX + 0, RA, 1.0f, c_light, k_normal, "4", 0, 0, { P(0,4), END } },
	{ TX + 1, RA, 1.0f, c_light, k_normal, "5", 0, 0, { P(0,5), END } },
	{ TX + 2, RA, 1.0f, c_light, k_normal, "6", 0, 0, { P(0,6), END } },
	{ TX + 3, RA, 1.0f, c_dark,  k_normal, "+", 0, 0, { P(1,3), END } },
	{ TX + 0, RZ, 1.0f, c_light, k_normal, "1", 0, 0, { P(0,1), END } },
	{ TX + 1, RZ, 1.0f, c_light, k_normal, "2", 0, 0, { P(0,2), END } },
	{ TX + 2, RZ, 1.0f, c_light, k_normal, "3", 0, 0, { P(0,3), END } },
	{ TX + 3, RZ, 1.0f, c_dark,  k_normal, "=", 0, 0, { P(1,4), END } },
	{ TX + 0, RB, 1.0f, c_light, k_normal, "0", 0, 0, { P(0,0), END } },
	{ TX + 1, RB, 1.0f, c_light, k_normal, ",", 0, 0, { P(1,5), END } },
	{ TX + 2, RB, 1.0f, c_light, k_normal, ".", 0, 0, { P(1,6), END } },
	{ TX + 3, RB, 1.0f, c_dark,  k_return, 0, 0, 0, { P(1,7), P(14,1), END } },	// テンキーの RETURN
};
static const int nkeys = sizeof(keys) / sizeof(keys[0]);

// 色 (実機の写真から。照明の黄ばみを少し除いた)
static const COLORREF col_case   = RGB(214, 207, 192);
static const COLORREF col_key[2] = { RGB(234, 229, 216), RGB(160, 151, 138) };
static const COLORREF col_edge   = RGB(112, 104, 94);
static const COLORREF col_text   = RGB(48, 46, 44);
static const COLORREF col_press  = RGB(120, 180, 240);		// 押されている
static const COLORREF col_once   = RGB(245, 200, 80);		// 次のキーまで保持
static const COLORREF col_lock   = RGB(240, 140, 70);		// 固定

static const char* classname = "M88gn SoftKeyboard";

// ---------------------------------------------------------------------------

SoftKeyboard::SoftKeyboard()
{
	hinst = 0;
	owner = 0;
	hwnd = 0;
	keyif = 0;
	posx = posy = width = 0;
	placed = false;
	visible = false;
	unit = 0;
	ox = oy = 0;
	fmain = fsmall = flabel = 0;
	backbmp = 0;
	backw = backh = 0;
	memset(matrix, 0, sizeof(matrix));
	pressed = -1;
	presstime = 0;
	releasewait = false;
	lastmod = -1;
	lastmodtime = 0;
	modstate = new uint8[nkeys];
	memset(modstate, 0, nkeys);
}

SoftKeyboard::~SoftKeyboard()
{
	if (hwnd)
		DestroyWindow(hwnd);
	if (fmain) DeleteObject(fmain);
	if (fsmall) DeleteObject(fsmall);
	if (flabel) DeleteObject(flabel);
	if (backbmp) DeleteObject(backbmp);
	delete[] modstate;
}

bool SoftKeyboard::Init(HINSTANCE hi, HWND own, PC8801::WinKeyIF* ki)
{
	hinst = hi;
	owner = own;
	keyif = ki;

	WNDCLASS wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = WndProcGate;
	wc.hInstance = hinst;
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = 0;
	wc.lpszClassName = classname;
	RegisterClass(&wc);			// 2 回目以降は失敗するが、登録済みなので構わない
	return true;
}

void SoftKeyboard::SetPlacement(int x, int y, int w)
{
	posx = x, posy = y, width = w;
	placed = w > 0;
	if (hwnd && placed)
	{
		RECT r;
		GetWindowRect(hwnd, &r);
		SetWindowPos(hwnd, 0, posx, posy, width, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

void SoftKeyboard::GetPlacement(int* x, int* y, int* w)
{
	if (hwnd)
	{
		RECT r;
		GetWindowRect(hwnd, &r);
		posx = r.left, posy = r.top, width = r.right - r.left;
	}
	*x = posx, *y = posy, *w = width;
}

// ---------------------------------------------------------------------------
//	窓を作る (初めて表示するとき)
//
void SoftKeyboard::Create()
{
	const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
	const DWORD exstyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

	int w = width > 0 ? width : 800;
	RECT r = { 0, 0, w, 0 };
	AdjustWindowRectEx(&r, style, false, exstyle);
	int ncw = (r.right - r.left) - w;
	int nch = r.bottom - r.top;
	int cw = w - ncw;
	int h = int(cw * totalh / totalw + 0.5) + nch;

	int x = posx, y = posy;
	if (!placed)
	{
		// 既定: M88 の窓のすぐ下
		RECT ro;
		GetWindowRect(owner, &ro);
		x = ro.left, y = ro.bottom;
	}
	hwnd = CreateWindowEx(exstyle, classname, "M88gn - Software keyboard", style,
						  x, y, w, h, owner, 0, hinst, this);
}

void SoftKeyboard::Show(bool show)
{
	visible = show;
	if (show && !hwnd)
		Create();
	if (!hwnd)
		return;
	ShowWindow(hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
	if (!show)
	{
		// 押したままのキーを戻す
		Release();
		memset(modstate, 0, nkeys);
		SendSoft();
	}
}

// ---------------------------------------------------------------------------

LRESULT CALLBACK SoftKeyboard::WndProcGate(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	SoftKeyboard* sk;
	if (msg == WM_NCCREATE)
	{
		sk = (SoftKeyboard*) ((LPCREATESTRUCT) lp)->lpCreateParams;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) sk);
		sk->hwnd = hwnd;
	}
	else
		sk = (SoftKeyboard*) GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (sk)
		return sk->WndProc(hwnd, msg, wp, lp);
	return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT SoftKeyboard::WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_CREATE:
		SetTimer(hw, 1, 30, 0);
		Layout();
		return 0;

	case WM_DESTROY:
		Release();					// 押したままのキーを残さない
		KillTimer(hw, 1);
		GetPlacement(&posx, &posy, &width);
		placed = true;
		hwnd = 0;
		return 0;

	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;			// クリックしてもフォーカスを奪わない

	case WM_CLOSE:
		Show(false);
		return 0;

	case WM_TIMER:
		if (wp == 2)
		{
			if (releasewait)
				Release();
			return 0;
		}
		{
			uint8 m[16];
			if (keyif)
				keyif->GetMatrix(m);
			else
				memset(m, 0, 16);
			if (memcmp(m, matrix, 16))
			{
				memcpy(matrix, m, 16);
				InvalidateRect(hw, 0, false);
			}
		}
		return 0;

	case WM_SIZING:
		Sizing(wp, (RECT*) lp);
		return TRUE;

	case WM_GETMINMAXINFO:
		((MINMAXINFO*) lp)->ptMinTrackSize.x = 320;
		return 0;

	case WM_SIZE:
		Layout();
		InvalidateRect(hw, 0, false);
		return 0;

	case WM_EXITSIZEMOVE:
		GetPlacement(&posx, &posy, &width);
		placed = true;
		return 0;

	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hw, &ps);
			Paint(hdc);
			EndPaint(hw, &ps);
		}
		return 0;

	case WM_LBUTTONDOWN:
		{
			if (releasewait)			// 前のキーを離すのを待っていたら、先に離す
				Release();
			int i = HitTest((short) LOWORD(lp), (short) HIWORD(lp));
			if (i >= 0)
				Press(i);
		}
		return 0;

	case WM_LBUTTONUP:
		// 短いクリックでも minhold は押したことにする (PC-8801 がキーボードを読みに来る前に
		// 離すと、キーが入らない)。残りの時間はマウスを捕まえたまま、タイマーで離す
		if (pressed >= 0 && !releasewait)
		{
			DWORD held = GetTickCount() - presstime;
			if (held < minhold)
			{
				releasewait = true;
				SetTimer(hw, 2, minhold - held, 0);
				return 0;
			}
		}
		Release();
		return 0;

	case WM_CAPTURECHANGED:
		if (pressed >= 0)
			Release();
		return 0;
	}
	return DefWindowProc(hw, msg, wp, lp);
}

// ---------------------------------------------------------------------------
//	大きさを変えるとき、縦横の比を保つ
//
void SoftKeyboard::Sizing(WPARAM edge, RECT* r)
{
	RECT rw, rc;
	GetWindowRect(hwnd, &rw);
	GetClientRect(hwnd, &rc);
	int ncw = (rw.right - rw.left) - rc.right;
	int nch = (rw.bottom - rw.top) - rc.bottom;

	int cw = (r->right - r->left) - ncw;
	int ch = (r->bottom - r->top) - nch;
	if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM)
		cw = int(ch * totalw / totalh + 0.5);
	else
		ch = int(cw * totalh / totalw + 0.5);

	int w = cw + ncw, h = ch + nch;
	if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT)
		r->left = r->right - w;
	else
		r->right = r->left + w;
	if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT)
		r->top = r->bottom - h;
	else
		r->bottom = r->top + h;
}

// ---------------------------------------------------------------------------
//	大きさに合わせて、キー 1 つ分の大きさと文字を決める
//
void SoftKeyboard::Layout()
{
	if (!hwnd)
		return;
	RECT rc;
	GetClientRect(hwnd, &rc);
	if (rc.right <= 0 || rc.bottom <= 0)
		return;
	unit = rc.right / totalw;					// (misc.h の Min は整数用なので使わない)
	if (rc.bottom / totalh < unit)
		unit = rc.bottom / totalh;
	ox = (rc.right - unit * totalw) / 2 + unit * margin;
	oy = (rc.bottom - unit * totalh) / 2 + unit * margin;

	if (fmain) DeleteObject(fmain);
	if (fsmall) DeleteObject(fsmall);
	if (flabel) DeleteObject(flabel);
	fmain  = CreateFont(-Max(6, int(unit * 0.30)), 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET,
						OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "MS UI Gothic");
	fsmall = CreateFont(-Max(5, int(unit * 0.24)), 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET,
						OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "MS UI Gothic");
	flabel = CreateFont(-Max(5, int(unit * 0.25)), 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET,
						OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "MS UI Gothic");

	if (backbmp && (backw != rc.right || backh != rc.bottom))
		DeleteObject(backbmp), backbmp = 0;
	backw = rc.right, backh = rc.bottom;
}

// ---------------------------------------------------------------------------
//	キーの場所 (ピクセル)。RETURN は L 字なので 2 つ (r2 は下の段。無ければ空)
//
void SoftKeyboard::KeyRect(int i, RECT* r, RECT* r2)
{
	const KeyDef& k = keys[i];
	r->left   = LONG(ox + unit * k.x);
	r->top    = LONG(oy + unit * k.y);
	r->right  = LONG(ox + unit * (k.x + k.w));
	r->bottom = LONG(oy + unit * (k.y + 1));
	SetRectEmpty(r2);
	if (k.kind == k_return)
	{
		if (k.y == RQ)		// 本体: 上の段 (Q) と下の段 (A)
		{
			r2->left   = LONG(ox + unit * RAX);
			r2->top    = r->bottom;
			r2->right  = r->right;
			r2->bottom = LONG(oy + unit * (RA + 1));
		}
		else				// テンキー: 1 段
			;
	}
}

int SoftKeyboard::HitTest(int x, int y)
{
	POINT pt = { x, y };
	for (int i = 0; i < nkeys; i++)
	{
		RECT r, r2;
		KeyRect(i, &r, &r2);
		if (PtInRect(&r, pt) || PtInRect(&r2, pt))
			return i;
	}
	return -1;
}

// ---------------------------------------------------------------------------
//	マウスで押す / 離す
//
void SoftKeyboard::Press(int i)
{
	switch (keys[i].kind)
	{
	case k_mod:			// 押す → 次のキーまで保持 (もう一度押すと戻る)。素早く 2 回押すと固定
		{
			DWORD now = GetTickCount();
			bool dbl = i == lastmod && now - lastmodtime <= GetDoubleClickTime();
			// 素早く 2 回押したら、前の状態によらず固定 (保持中からでも固定できる)。
			// 固定した次のクリックは 2 回目と数えない (3 回目で戻る)
			if (dbl)
				modstate[i] = mod_lock, lastmod = -1;
			else
			{
				modstate[i] = modstate[i] == mod_off ? mod_once : mod_off;
				lastmod = i;
				lastmodtime = now;
			}
		}
		break;

	case k_lock:		// 押すたびに固定 / 戻る
		modstate[i] = modstate[i] ? mod_off : mod_lock;
		break;

	default:
		pressed = i;
		presstime = GetTickCount();
		SetCapture(hwnd);
		break;
	}
	SendSoft();
	InvalidateRect(hwnd, 0, false);
}

void SoftKeyboard::Release()
{
	if (releasewait)
	{
		releasewait = false;
		KillTimer(hwnd, 2);
	}
	if (pressed < 0)
		return;
	pressed = -1;
	for (int i = 0; i < nkeys; i++)		// 次のキーまで保持していた SHIFT などを戻す
		if (modstate[i] == mod_once)
			modstate[i] = mod_off;
	SendSoft();
	if (GetCapture() == hwnd)
		ReleaseCapture();
	if (hwnd)
		InvalidateRect(hwnd, 0, false);
}

void SoftKeyboard::SendSoft()
{
	uint8 b[16];
	memset(b, 0, 16);
	for (int i = 0; i < nkeys; i++)
	{
		if (i == pressed || modstate[i])
		{
			for (int j = 0; j < 4 && keys[i].bits[j] != END; j++)
				b[keys[i].bits[j] >> 4] |= 1 << (keys[i].bits[j] & 7);
		}
	}
	if (keyif)
		keyif->SetSoftKeys(b);
}

// ---------------------------------------------------------------------------
//	描く
//
static void DrawTextIn(HDC hdc, const char* s, RECT* r, UINT align)
{
	// 複数行は縦の真ん中にそろえる
	RECT rc = *r;
	DrawText(hdc, s, -1, &rc, align | DT_CALCRECT | DT_NOPREFIX);
	int h = rc.bottom - rc.top;
	RECT rd = *r;
	rd.top = r->top + (r->bottom - r->top - h) / 2;
	rd.bottom = rd.top + h;
	DrawText(hdc, s, -1, &rd, align | DT_NOPREFIX);
}

static COLORREF Mix(COLORREF a, COLORREF b, int t)		// t / 256 だけ b に寄せる
{
	return RGB((GetRValue(a) * (256 - t) + GetRValue(b) * t) >> 8,
			   (GetGValue(a) * (256 - t) + GetGValue(b) * t) >> 8,
			   (GetBValue(a) * (256 - t) + GetBValue(b) * t) >> 8);
}

void SoftKeyboard::Paint(HDC hdcw)
{
	if (unit <= 0)
		return;
	HDC hdc = CreateCompatibleDC(hdcw);
	if (!backbmp)
		backbmp = CreateCompatibleBitmap(hdcw, backw, backh);
	HGDIOBJ oldbmp = SelectObject(hdc, backbmp);

	RECT all = { 0, 0, backw, backh };
	HBRUSH bcase = CreateSolidBrush(col_case);
	FillRect(hdc, &all, bcase);
	DeleteObject(bcase);

	HPEN pen = CreatePen(PS_SOLID, 1, col_edge);
	HGDIOBJ oldpen = SelectObject(hdc, pen);
	HGDIOBJ oldfont = SelectObject(hdc, fmain);
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, col_text);

	int gap = Max(1, int(unit * 0.06));
	int round = Max(2, int(unit * 0.18));
	int pad = Max(1, int(unit * 0.11));

	for (int i = 0; i < nkeys; i++)
	{
		const KeyDef& k = keys[i];

		// 押されているか (そのキーのビットがすべて立っている)
		bool on = true;
		for (int j = 0; j < 4 && k.bits[j] != END; j++)
			if (!(matrix[k.bits[j] >> 4] & (1 << (k.bits[j] & 7))))
				on = false;
		// 左右の シフト: パソコンのキーボードからは左右の区別 (ポート 0Eh の bit2/3) が来ないので、
		// どちらも立っていなければ SHIFT (08h の bit6) だけで光らせる
		if (!on && k.bits[0] == P(8,6) && (k.bits[1] == P(14,2) || k.bits[1] == P(14,3))
			&& (matrix[8] & 0x40) && !(matrix[14] & 0x0c))
			on = true;
		if (i == pressed)
			on = true;

		COLORREF c = col_key[k.color];
		if (modstate[i] == mod_lock)
			c = Mix(c, col_lock, 200);
		else if (modstate[i] == mod_once)
			c = Mix(c, col_once, 200);
		else if (on)
			c = Mix(c, col_press, 200);
		HBRUSH br = CreateSolidBrush(c);
		HGDIOBJ oldbr = SelectObject(hdc, br);
		HPEN thick = 0;
		if (modstate[i] == mod_lock)		// 固定: 枠を太くする
		{
			thick = CreatePen(PS_INSIDEFRAME, Max(2, int(unit * 0.09)), col_edge);
			SelectObject(hdc, thick);
		}

		RECT r, r2;
		KeyRect(i, &r, &r2);
		if (k.kind == k_return && !IsRectEmpty(&r2))
		{
			// L 字
			POINT pt[6] =
			{
				{ r.left + gap,  r.top + gap },
				{ r.right - gap, r.top + gap },
				{ r.right - gap, r2.bottom - gap },
				{ r2.left + gap, r2.bottom - gap },
				{ r2.left + gap, r.bottom - gap },
				{ r.left + gap,  r.bottom - gap },
			};
			Polygon(hdc, pt, 6);
			r.bottom = r2.bottom;
			r.left = r2.left;
		}
		else
			RoundRect(hdc, r.left + gap, r.top + gap, r.right - gap, r.bottom - gap, round, round);
		SelectObject(hdc, oldbr);
		DeleteObject(br);
		if (thick)
		{
			SelectObject(hdc, pen);
			DeleteObject(thick);
		}

		InflateRect(&r, -pad, -pad);
		if (k.kind == k_return)
		{
			// RETURN の矢印 (下に降りて左に曲がる形)
			int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
			int s = int(unit * 0.22);
			POINT a[3] = { { cx + s, cy - s }, { cx + s, cy + s / 2 }, { cx - s, cy + s / 2 } };
			Polyline(hdc, a, 3);
			POINT h1[2] = { { cx - s, cy + s / 2 }, { cx - s / 2, cy } };
			POINT h2[2] = { { cx - s, cy + s / 2 }, { cx - s / 2, cy + s } };
			Polyline(hdc, h1, 2);
			Polyline(hdc, h2, 2);
		}
		else if (k.kana)
		{
			// 文字キー: 上段の真ん中に SHIFT の記号、中段の左に英字・数字、中段の右に SHIFT のかな
			// (小さいかな・を・句読点)、下段の真ん中にかな。
			// 枠で切らない (DT_NOCLIP。_ などが段の下にはみ出す)
			const UINT f = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_NOCLIP;
			int rh = (r.bottom - r.top) / 3;
			RECT r0 = { r.left, r.top, r.right, r.top + rh };
			RECT r1 = { r.left, r.top + rh, r.right, r.top + rh * 2 };
			RECT rk = { r.left, r.top + rh * 2, r.right, r.bottom };
			SelectObject(hdc, fsmall);
			if (k.shift)
			{
				if (!strcmp(k.shift, "|"))
					DrawTextW(hdc, L"\x00a6", 1, &r0, DT_CENTER | f);	// 途中が切れた縦棒 (Shift_JIS に無い)
				else
					DrawText(hdc, k.shift, -1, &r0, DT_CENTER | f);
			}
			SelectObject(hdc, fmain);
			DrawText(hdc, k.label, -1, &r1, DT_LEFT | f);
			SelectObject(hdc, fsmall);
			DrawText(hdc, k.kana, -1, &rk, DT_CENTER | f);
			if (k.kana2)
				DrawText(hdc, k.kana2, -1, &r1, DT_RIGHT | f);
		}
		else if (k.label)
		{
			SelectObject(hdc, strlen(k.label) <= 2 ? fmain : flabel);
			DrawTextIn(hdc, k.label, &r, DT_CENTER);
		}
	}

	SelectObject(hdc, oldfont);
	SelectObject(hdc, oldpen);
	DeleteObject(pen);

	BitBlt(hdcw, 0, 0, backw, backh, hdc, 0, 0, SRCCOPY);
	SelectObject(hdc, oldbmp);
	DeleteDC(hdc);
}
