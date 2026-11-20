// ---------------------------------------------------------------------------
//	M88 - PC-8801 emulator
//	バックトレース表示ウインドウ
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#pragma once

#include "winmon.h"
#include "wincore.h"
#include "pc88/debugger.h"
#include "Z80diag.h"

// ---------------------------------------------------------------------------

namespace PC8801
{

class TraceMonitor : public WinMonitor
{
public:
	TraceMonitor();
	~TraceMonitor();

	bool Init(WinCore*);

	// WM_M88_DEBUGBREAK から呼ばれる。ポーズ/ブレーク中だけ表示を更新する
	// (実行中はリングバッファが猛烈に回るので更新しても意味が無い)
	void Refresh();

private:
	void UpdateText();
	void Start();
	BOOL DlgProc(HWND, UINT, WPARAM, LPARAM);

	void LoadMonitorConfig();
	void SaveMonitorConfig();

	// エントリの cpu からその表示に使う逆アセンブラを選ぶ
	Z80Diag& DiagFor(int cpu) { return cpu == Debugger::cpu_sub ? diagsub : diagmain; }

	// 全件をテキストにする(並び順は ascending に従う)。
	// Ctrl+C とファイル出力(SaveToFile)で共有する
	bool BuildText(TextBuffer& tb);

	bool CopyToClipboard();		// override。BuildText して SetClipboardText
	bool SaveToFile();			// BuildText して fwrite

	// メニューから件数を切り替える。core->Lock() の中で SetTraceCapacity する
	void SetCapacity(int n);

	WinCore* core;
	// エントリの cpu ごとに逆アセンブラを分ける(main/sub でメモリ空間が違う)
	Z80Diag diagmain;
	Z80Diag diagsub;

	bool ascending;		// false = 新しいものが上(既定)。M88.ini に保存する
};

}
