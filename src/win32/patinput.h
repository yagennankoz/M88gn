// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	文字列か 1～16 バイトの並びの入力 (メモリの窓の検索と、パターンのブレークの窓で使う)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	ダイアログの部品は IDC_SEARCH_TEXT・STR・CASE・HEX・BYTES・B0～B15 を使う
// ---------------------------------------------------------------------------

#pragma once

#include "types.h"

namespace PC8801
{

struct PatternInput
{
	enum { textmax = 64, hexmax = 16 };

	bool hex;					// 16 進で入れる (false なら文字列)
	bool mcase;					// 文字列の大文字・小文字を区別する
	int bytes;					// 16 進のバイト数 (1～16)
	char text[textmax + 1];
	char box[hexmax][3];		// 16 進の各欄の字
	uint8 pat[textmax];			// 入れたもの (Read の後)
	int len;					// 0 ならまだ無い

	PatternInput();

	void InitDialog(HWND hdlg, bool focus = true);		// 欄に今の値を入れる
	bool Command(HWND hdlg, int id, int code);	// 欄の操作 (WM_COMMAND)。扱ったら true
	bool Read(HWND hdlg, const char* title);	// 欄を読んで pat を作る。足りなければ知らせて false
	bool CaseSensitive() const { return hex || mcase; }
	void Describe(char* buf) const;				// "文字列" か XX XX ... (textmax * 3 + 8 バイト)
	// バイトの並びから欄の値を作る (ブレークポイントを編集するとき)
	void Set(const uint8* p, int n, bool hex, bool mcase);

private:
	void EnableBoxes(HWND hdlg);
};

}
