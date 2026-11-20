// ---------------------------------------------------------------------------
//	M88 - PC-8801 emulator
//	ウォッチウインドウ
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#pragma once

#include "winmon.h"
#include "wincore.h"
#include "pc88/debugger.h"
#include "pc88/memview.h"

// ---------------------------------------------------------------------------

namespace PC8801
{

class WatchMonitor : public WinMonitor
{
public:
	WatchMonitor();
	~WatchMonitor();

	bool Init(WinCore*);

private:
	void UpdateText();
	BOOL DlgProc(HWND, UINT, WPARAM, LPARAM);

	// ウォッチが読み書きするバス。「今CPUが読んだら返ってくる値」ではなく
	// 「指定したアドレスに実際に格納されている値」を見せるのが目的なので、
	// Debugger 経由の生きた MemoryManager(バンク切り替えの影響を受ける)
	// ではなく、MemoryMonitor と同じ MemoryViewer の私設バス(常に RAM を
	// 指すバンク固定)を使う。main/sub でバスが別なので2つ持つ
	MemoryBus* GetWatchBus(uint8 cpu) { return cpu == Debugger::cpu_sub ? mvsub.GetBus() : mvmain.GetBus(); }

	MemoryViewer mvmain;
	MemoryViewer mvsub;

	// p はクライアント座標(GetTextPos 変換前)。dblclick が true かつ Value 列
	// なら値の編集、それ以外の列ならエントリの編集ダイアログを開く
	void OnClick(POINT p, bool dblclick);
	void DeleteSelected();
	// index < 0 なら追加ダイアログ、それ以外は index 番目の編集ダイアログ
	void ShowEditDialog(int index);
	// アドレス/サイズ/CPU/ラベルではなく、値そのものの編集(IDD_MEMORY_EDIT を流用)
	void ShowValueDialog(int index);

	BOOL EDlgProc(HWND, UINT, WPARAM, LPARAM);
	static BOOL CALLBACK EDlgProcGate(HWND, UINT, WPARAM, LPARAM);
	BOOL VDlgProc(HWND, UINT, WPARAM, LPARAM);
	static BOOL CALLBACK VDlgProcGate(HWND, UINT, WPARAM, LPARAM);

	WinCore* core;
	int cursor;			// 選択中の行(watchlist のインデックス。-1 = 未選択)
	int editindex;		// 編集ダイアログの対象(-1 = 追加モード)
};

}
