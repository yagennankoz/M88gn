// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ニーモニック表 (Z80 の命令の一覧) のウインドウ
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#pragma once

#include "winmon.h"
#include <vector>

// ---------------------------------------------------------------------------

namespace PC8801
{

// 命令のバイト (code、4 バイト読める所) が、ニーモニック表 (Zilog の公式の命令) の命令で、長さが size か (M88gn)。
// アセンブラに通せる形で保存するときに、そのまま書いてよい命令かを決める
bool IsOfficialInstruction(const uint8* code, int size);

class MnemonicMonitor : public WinMonitor
{
public:
	MnemonicMonitor();
	~MnemonicMonitor();

	bool Init();

	// 命令のバイト (4 バイト) に当たる行へ移って色を付ける。窓が閉じていれば開く (コードの窓から)
	void ShowInstruction(const uint8* code, HINSTANCE hinst, HWND parent);

private:
	void UpdateText();
	void Start();
	BOOL DlgProc(HWND, UINT, WPARAM, LPARAM);

	void LoadMonitorConfig();
	void SaveMonitorConfig();

	bool CopyToClipboard();		// 全部の行をタブ区切りでコピーする

	void BuildRows();			// 並び順に合わせて行の並びを作る
	void CenterSelected();		// 選んだ行を窓の真ん中に (端では行けるところまで)。選んでいなければ先頭

	bool bycategory;			// false = コード順、true = カテゴリー順。M88.ini に保存する
	// 表示する行。0 以上 = mnemtable の番号、負 = カテゴリーの見出し (-1 - カテゴリーの番号)
	std::vector<int> rows;
	int selected;				// 色を付ける行 (mnemtable の番号、-1 なら無し)
	uint8 selcode[4];			// 最後に探した命令のバイト (表に無かったときの知らせ用)
	bool notfound;
};

}
