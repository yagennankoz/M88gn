// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1998, 2000.
// ---------------------------------------------------------------------------
//	$Id: winmon.h,v 1.4 2002/04/07 05:40:11 cisc Exp $

#pragma once

//	概要：
//	デバッグ表示のために使用するテキストウィンドウの管理用基本クラス
//	機能：
//	テキストバッファ，差分更新，スクロールバーサポート

#include "types.h"

// ---------------------------------------------------------------------------

class WinMonitor
{
public:
	WinMonitor();
	~WinMonitor();

	// inisection を渡すと、ウインドウ位置・サイズ・フォントサイズを
	// M88.ini の [inisection] に保存/復元するようになる(NULL なら保存しない)
	// defcols/defrows は INI に保存位置が無いときの初期サイズを
	// 「文字数×行数」で指定する(0 ならダイアログテンプレートの寸法のまま)
	bool Init(LPCTSTR tmpl, const char* inisection = 0,
			  int defcols = 0, int defrows = 0);
	void Show(HINSTANCE, HWND, bool show);

	bool IsOpen() { return !!hwnd; }

	void Update();

	// デバッグの窓のフォント (すべての窓で共通。M88gn)。フォントを選ぶダイアログ (等幅のフォントだけ) を出し、
	// 選んだら M88.ini の [Monitor] に保存して、開いている窓を書き直す
	static bool ChooseMonitorFont(HWND owner);

protected:
	// 派生クラスでオーバーライドして独自の保存項目を足せるように virtual にしてある
	// (例: CodeMonitor が PCFollow の ON/OFF を足す)
	virtual void LoadMonitorConfig();
	virtual void SaveMonitorConfig();

	void Locate(int x, int y);
	void Puts(const char*);
	void Putf(const char*, ...);

	void SetTxCol(COLORREF col) { txcol = col; }
	void SetBkCol(COLORREF col) { bkcol = col; }
	// 左半分と右半分で背景色の違う空白を 1 文字書く (M88gn。メモリの窓の印を隙間の真ん中で切り替える)
	void PutSplitSpace(COLORREF left, COLORREF right);

	int  GetWidth() { return width; }
	int  GetHeight() { return height; }
	int  GetLine() { return line; }
	void SetLine(int l);
	void SetLines(int nlines);
	bool GetTextPos(POINT*);
	bool EnableStatus(bool);
	bool PutStatus(const char* text, ...);

	// 文字列をクリップボードへ置く。成功したら true
	bool SetClipboardText(const char* text, size_t len);

	// Ctrl+C で呼ばれる。既定は「表示中の1画面」。全体をコピーしたい
	// 派生クラス(Memory/Code/BASIC)はこれを override する
	virtual bool CopyToClipboard();

	bool SetFont(HWND hwnd, int fh);

	void ClearText();

	int  GetScrPos(bool tracking);
	void ScrollUp();
	void ScrollDown();

	void SetUpdateTimer(int t);

	// ホイール入力の処理。処理したら true
	bool ProcMouseWheel(WPARAM wp);

	virtual void DrawMain(HDC, bool = false);
	virtual BOOL DlgProc(HWND, UINT, WPARAM, LPARAM);

	HWND GetHWnd() { return hwnd; }
	HWND GetHWndStatus() { return hwndstatus; }
	HINSTANCE GetHInst() { return hinst; }

private:
	virtual void UpdateText();
	virtual int VerticalScroll(int msg);
	virtual void Start() {}
	virtual void Stop() {}

	static BOOL CALLBACK DlgProcGate(HWND, UINT, WPARAM, LPARAM);

	void Draw(HWND, HDC);
	void ResizeWindow(HWND);
	void ApplyDefaultSize();

	// Ctrl+ホイールでのフォントサイズ変更(fontsizetable 上で dir 段階動かす)
	void ChangeFontSize(int dir);

	HWND hwnd;
	HWND hwndowner;			// Show() で受け取った親(メインウインドウ)
	HINSTANCE hinst;
	LPCTSTR lptemplate;

	HWND hwndstatus;
	char statusbuf[128];

	int clientwidth;
	int clientheight;

	HDC hdc;
	HFONT hfont;
	int fontwidth;
	int fontheight;		// メニュー・INI 用の論理フォント高さ(DPIスケール前)
	int fontheightpx;	// 描画に使う実測ピクセル高さ(DPIスケール後)

	HBITMAP hbitmap;

	struct TXCHAR
	{
		char ch;
		COLORREF txcol;
		COLORREF bkcol;
		COLORREF bkcol2;	// 右半分の背景色 (M88gn。ふつうは bkcol と同じ。違えば左右で塗り分けた空白)
	};

	TXCHAR* txtbuf;
	TXCHAR* txpbuf;
	TXCHAR* txtbufptr;
	POINT txp;
	int width;
	int height;

	COLORREF txcol, txcolprev;
	COLORREF bkcol, bkcolprev;

	RECT wndrect;

	int line;
	int nlines;

	UINT_PTR timer;
	int timerinterval;

	int wheeldelta;      // ホイールの端数累積（120未満の delta を貯める）

	const char* inisection;	// 位置・サイズ・フォントの保存先セクション名(NULLなら保存しない)

	int defcols;		// 初期サイズ(文字数)。0 ならテンプレート任せ
	static void LoadFontFace();
	static char fontface[LF_FACESIZE];	// フォントの名前 (空: まだ M88.ini から読んでいない)
	static BYTE fontcharset;
	enum { maxinstances = 32 };
	static WinMonitor* instances[maxinstances];	// 開いている窓を書き直すため
	int savedpi;		// M88.ini の位置・大きさを保存したときの DPI (0: M88gn 1.3.0 までの値 = 96 DPI 換算。M88gn)
	void SetFontSize(int fh);	// 文字の大きさを変えて、窓を書き直す (M88gn)
	int defrows;		// 初期サイズ(行数)
};

namespace PC8801
{

// ---------------------------------------------------------------------------
//	クリップボード/ファイル出力用の、伸びるテキストバッファ
//
//	コードの逆アセンブルは行数が可変で、出力サイズを先に読めないため。
//	確保に失敗したら以後の追加を捨て、Failed() が true になる
//	(Phase 10b のトレースのファイル出力でも使う)
//
class TextBuffer
{
public:
	TextBuffer();
	~TextBuffer();

	bool Add(const char* text);
	bool Addf(const char* fmt, ...);

	const char* Get() const { return buf ? buf : ""; }
	size_t Length() const { return len; }
	bool Failed() const { return failed; }

private:
	bool Reserve(size_t need);

	// コピー・代入は使わない
	TextBuffer(const TextBuffer&);
	TextBuffer& operator=(const TextBuffer&);

	char*  buf;
	size_t len;
	size_t cap;
	bool   failed;
};

}

