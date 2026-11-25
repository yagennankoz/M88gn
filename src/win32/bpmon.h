// ---------------------------------------------------------------------------
//	M88 - PC-8801 emulator
//	ブレークポイント管理ウインドウ
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#pragma once

#include <vector>

#include "winmon.h"
#include "wincore.h"
#include "pc88/debugger.h"

// ---------------------------------------------------------------------------

namespace PC8801
{

class BreakMonitor : public WinMonitor
{
public:
	BreakMonitor();
	~BreakMonitor();

	// io: I/O ポートのブレークポイントの窓にする (M88gn)。そうでなければメモリ・実行の窓
	bool Init(WinCore*, bool io = false);
	// 右クリックの Show in Code / Memory で使う窓 (M88gn)
	void SetMonitors(class CodeMonitor* c, class MemoryMonitor* m) { codemon = c; memmon = m; }

private:
	void UpdateText();
	BOOL DlgProc(HWND, UINT, WPARAM, LPARAM);

	// p はクライアント座標(GetTextPos 変換前)。dblclick が true なら
	// E/R/W/Enabled 列のトグル、それ以外の列なら編集ダイアログを開く
	void OnClick(POINT p, bool dblclick);
	// col は 'E'/'R'/'W'/'e'(Enabled) のいずれか
	void ShowRowMenu(HWND hdlg, POINT p);	// 右クリックのメニュー (M88gn)
	void ToggleAttr(int index, int col);
	void DeleteSelected();
	// index < 0 なら追加ダイアログ、それ以外は index 番目の編集ダイアログ
	void ShowEditDialog(int index);

	BOOL EDlgProc(HWND, UINT, WPARAM, LPARAM);
	static BOOL CALLBACK EDlgProcGate(HWND, UINT, WPARAM, LPARAM);

	void BuildView();				// 表示する行 (この窓の種類のブレークポイントの番号) を作る
	void ShowPortMenu(HWND hdlg);	// I/O: ポートを選ぶメニュー
	void UpdatePortLabel(HWND hdlg);	// I/O: 入っているポートの説明を出す

	WinCore* core;
	bool iomode;
	class CodeMonitor* codemon;
	class MemoryMonitor* memmon;
	std::vector<int> view;		// 行 → bplist の番号
	int cursor;			// 選択中の行(bplist のインデックス。-1 = 未選択)
	int editindex;		// 編集ダイアログの対象(-1 = 追加モード)
};

}
