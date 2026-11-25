// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ステータスの窓 (ステータスバーと同じ情報と、止まった理由の詳しい説明)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#pragma once

#include "winmon.h"
#include "wincore.h"
#include "pc88/debugger.h"

// ---------------------------------------------------------------------------

namespace PC8801
{

class StatusMonitor : public WinMonitor
{
public:
	StatusMonitor();
	~StatusMonitor();

	bool Init(WinCore* core);

	// 止まっている間に WinUI::ShowPauseStatus から呼ぶ。止まった理由を覚え、
	// ステータスバーに出す短い説明 (brief) を返す。lap は前に止まってからのクロック数の説明
	void CaptureStop(char* brief, int size, const char* lap);

private:
	void UpdateText();
	void Start();
	void PutBreakPoint();		// 止めたブレークポイントの中身

	WinCore* core;

	// 最後に止まったときの情報
	bool hasstop;
	int reason;				// Debugger::BreakReason (止まっているがブレークでない = 手動の Pause は brk_user)
	int cpu;				// Debugger::CpuTarget
	uint addr;
	uint len;				// パターンのとき、揃ったバイト数
	int data;				// 読み書きした値 (-1: 無い)
	uint pc;				// 読み書きした命令の番地
	int index;				// ブレークポイントの番号 (-1: 無い)
	int viewno;				// ブレークポイントの窓 (メモリか I/O) の一覧での番号
	bool hasbp;
	Debugger::BreakPoint bp;	// 止まったときのブレークポイントの中身
	uint pc1, pc2;			// 止まったときのメイン・サブの PC
	char lap[48];
	char brief[160];
};

}
