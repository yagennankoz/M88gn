// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1998, 2000.
// ---------------------------------------------------------------------------
//	$Id: codemon.h,v 1.7 2003/05/19 02:33:56 cisc Exp $

#pragma once

#include "dbgbuttons.h"

#include "mvmon.h"
#include "device.h"
#include "pc88/memview.h"
#include "Z80diag.h"
#include "wincore.h"

// ---------------------------------------------------------------------------

namespace PC8801
{

class CodeMonitor : public MemViewMonitor
{
public:
	CodeMonitor();
	~CodeMonitor();

	bool Init(WinCore*);

	// ブレークが発生した位置へスクロールする(PC追従が無効なら何もしない)。
	// ui.cpp の WM_M88_DEBUGBREAK 通知から呼ばれる
	void FollowPC();

	// 右クリックのメニューの「Show in Mnemonic Table」で使うニーモニック表 (ui が渡す、M88gn)
	void SetMnemonicMonitor(class MnemonicMonitor* m) { mnemmon = m; }
	// 右クリックの Show in Memory で使うメモリの窓 (ui が渡す、M88gn)
	void SetMemoryMonitor(class MemoryMonitor* m) { memmon = m; }
	// 表示中の CPU の PC へスクロールする。FollowPC と違い、ブレークした
	// CPU と一致しているかどうかは見ない (バンクを手で切り替えたとき用)
	void FollowShownPC();

	// 表示中のバンクに対応する CPU (ステップ実行の対象を決めるのに使う)
	Debugger::CpuTarget GetShownCpu();
	// 表示する CPU を切り替える (ブレークした CPU への自動追従)。
	// main へ戻すときは、sub へ切り替える前に見ていたバンクを復元する
	void ShowCpu(Debugger::CpuTarget cpu);
	// CPU とバンクを合わせて番地を見せる (M88gn。ブレークポイント・ウォッチ・ラベルの窓から。bank は ShowAddressIn と同じ)
	void ShowAt(uint addr, bool sub, int bank, HINSTANCE hinst, HWND parent);

private:
	void ScrollToPC();
	void UpdateText();
	uint GetCycles(uint addr);		// 命令のサイクル数 (成立しないときは上位 8 ビット)
	int VerticalScroll(int msg);
	BOOL DlgProc(HWND, UINT, WPARAM, LPARAM);
	void LoadMonitorConfig();
	void SaveMonitorConfig();
	void Start();				// 開いた直後にタイトルを出す
	void SetBank();				// タイトルを更新するためオーバーライドする
	void UpdateCaption();		// タイトルに表示中の CPU / バンクを出す

	bool DumpTo(TextBuffer& tb, int from, int to);	// 中身を組み立てる
	// ラベル付き (M88gn): ラベルの行「名前:」、飛び先を名前に、命令の途中に当たるラベルは先頭に EQU
	// source なら、アセンブラに通せる形 (ORG・ラベル・命令、番地とバイトはコメント。公式の命令以外は DB)
	// from-to (両端を含む) の命令。最後の命令は to を越えることがある
	bool DumpToLabeled(TextBuffer& tb, bool source, uint from, uint to);
	bool AskDumpRange(const char* title);	// 範囲を聞く (dumpfrom・dumpto)
	uint dumpfrom, dumpto;					// 前に指定した範囲 (窓を閉じるまで覚える)
	bool Dump(FILE* fp, int from, int to);			// DumpTo を呼んで書き出すだけ
	enum { dump_plain = 0, dump_labeled, dump_source };
	bool DumpImage(int mode = dump_plain);
	bool CopyToClipboard();	// 0000-ffff の逆アセンブル全体をコピーする(F-3)
	void ToggleBreakAt(POINT p);
	void ShowLineMenu(HWND hdlg, POINT p);		// 右クリックのメニュー (M88gn)

	Z80Diag diag;
	WinCore* core;
	bool pcfollow;
	class MnemonicMonitor* mnemmon;
	class MemoryMonitor* memmon;
	bool showcycles;		// 命令のサイクル数 (T ステート) を行の右に出す (M88gn)
	MemoryViewer::Type lasta0;		// ShowCpu(sub) に切り替える前に見ていたバンク
	AddrBarItem addritem;			// メニューバーの Addr の欄 (M88gn)
	DebugButtons buttons;			// メニューバーの Pause/Play/Step のボタン (M88gn)

	// UpdateText() が描画時に計算した、各行の先頭アドレス。可変長命令
	// なので、クリックされた行のアドレスは行番号から逆算できない
	vector<uint> lineaddr;
};

}

