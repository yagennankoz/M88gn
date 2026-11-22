// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1998, 2000.
// ---------------------------------------------------------------------------
//	$Id: memmon.h,v 1.9 2003/05/19 02:33:56 cisc Exp $

#pragma once

#include "device.h"
#include "mvmon.h"
#include "pc88/memview.h"
#include "wincore.h"
#include "dbgbuttons.h"

// ---------------------------------------------------------------------------

class PC88;

namespace PC8801
{

class MemoryMonitor : public MemViewMonitor
{
public:
	MemoryMonitor();
	~MemoryMonitor();

	bool Init(WinCore*); 

private:
	// Memory ウインドウは1行=16バイトなので、Code 窓(1行=1アドレス)と
	// 換算が異なる
	// 見せたい番地の行を、上から少し下に出す (M88gn)
	void JumpTo(uint addr) { SetLine(Max(0, int(addr >> 4) - 3)); }

	BOOL DlgProc(HWND, UINT, WPARAM, LPARAM);
	BOOL EDlgProc(HWND, UINT, WPARAM, LPARAM);
	static BOOL CALLBACK EDlgProcGate(HWND, UINT, WPARAM, LPARAM);

	static uint MEMCALL MemRead(void* p, uint a);
	static void MEMCALL MemWrite(void* p, uint a, uint d);

	void Start();
	void Stop();

	void UpdateText();
	bool SaveImage();
	bool CopyToClipboard();		// 0000-ffff 全体をコピーする(F-3)

	// ダンプリストのチェックサム (M88gn)。1 行 16 バイトの横だけ
	enum SumType { sum_none = 0, sum_kougakusha, sum_ascii, sum_dempa, sum_count };
	static uint RowSum(int type, uint addr, const uint8* d);
	void LoadMonitorConfig();
	void SaveMonitorConfig();
	int sumtype;

	// ウォッチ (M88gn): 登録した番地のデータに背景色、右クリックの Toggle Watch
	Debugger::CpuTarget ShownCpu();
	int ShownBank(uint addr);				// その番地に今出しているバンク (MemoryViewer::Type)
	bool WatchMatches(const Debugger::WatchEntry& w, uint addr);
	int WatchAt(uint addr);					// addr を含むウォッチの番号 (無ければ -1)
	void ToggleWatch(uint addr);
	void ShowDataMenu(HWND hdlg, POINT p);	// 右クリックのメニュー。p は文字の位置
	int AddrAtPos(POINT p);					// 文字の位置のデータの番地 (16 進の欄か文字の欄。無ければ -1)
	void UpdateLabelTip(POINT p, bool inside);	// ラベルのあるウォッチの上ならツールチップを出す
	HWND hwndtip;
	int tipaddr;							// ツールチップを出している番地 (-1 なら出していない)
	bool tiptracking;
	std::vector<Debugger::WatchEntry> watchshown;	// 表示中の CPU のウォッチ。UpdateText で作る

	// ダンプからのブレークポイント (M88gn): 読み・書きのブレークポイントの番地に色、右クリックで切り替え
	std::vector<Debugger::BreakPoint> bpshown;		// 表示中の CPU の読み・書きのブレークポイント。UpdateText で作る
	int MarkAt(uint addr);
	class CodeMonitor* codemon;				// 右クリックの Show in Code (ui が渡す)
	DebugButtons buttons;					// メニューバーの Pause/Play/Step のボタン (M88gn)
public:
	void SetCodeMonitor(class CodeMonitor* c) { codemon = c; }
private:					// 1 = ウォッチ、2 = ブレークポイント (両方なら 3)
	int FindMemBreak(uint addr, bool write);	// addr を含む読み (書き) のブレークポイントの番号 (-1 なら無し)
	void ToggleMemBreak(uint addr, bool write);

	// 範囲の保存・読み込み (M88gn)
	bool SaveRange();
	bool LoadRange();
	bool AskRange(bool load);				// 番地の範囲を聞く (load なら始まりだけ)
	BOOL RDlgProc(HWND, UINT, WPARAM, LPARAM);
	static BOOL CALLBACK RDlgProcGate(HWND, UINT, WPARAM, LPARAM);
	uint rangestart, rangeend;
	bool rangeload;

	void SetWatch(uint, uint);

	void ExecCommand();
	void Search(uint key, int bytes);

	void SetBank();

	WinCore* core;
	IMemoryManager* mm;
	IGetMemoryBank* gmb;
	int mid;
	uint time;

	int prevaddr, prevlines;

	int watchflag;

	HWND hwndstatus;

	int editaddr;

	char line[0x100];
	uint8 stat[0x10000];
	uint access[0x10000];

	static COLORREF col[0x100];
};

}

