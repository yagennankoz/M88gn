// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ラップタイムの窓 (止まるたびに、前に止まった所・今回止まった所と、その間のクロック数・時間)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#pragma once

#include "winmon.h"
#include "wincore.h"
#include <vector>

// ---------------------------------------------------------------------------

namespace PC8801
{

class LapMonitor : public WinMonitor
{
public:
	LapMonitor();
	~LapMonitor();

	bool Init(WinCore*);

	// 止まったときに WinUI::ShowPauseStatus から呼ぶ (同じ停止で 2 回呼ばないこと)。
	// tick はメインの CPU のクロック数、cpu・pc は止まった CPU とその PC、why は止まった理由の短い説明
	void Record(uint32 tick, int cpu, uint pc, const char* why);

private:
	struct Lap
	{
		int no;				// 通し番号 (Clear で 0 から)
		bool first;			// 記録を始めてから最初の停止 (前が無い)
		uint8 fromcpu, tocpu;	// 0 = メイン、1 = サブ
		uint16 frompc, topc;
		uint32 clocks;		// 前に止まってからのメインの CPU のクロック数
		unsigned __int64 total;	// 記録を始めてからのクロック数
		char why[64];
	};
	enum { maxlaps = 10000 };

	void UpdateText();
	void Start();
	BOOL DlgProc(HWND, UINT, WPARAM, LPARAM);

	static void FormatMs(unsigned __int64 clocks, uint clock, char* buf);
	bool BuildText(TextBuffer& tb);
	bool CopyToClipboard();
	bool SaveToFile();
	void Clear();

	WinCore* core;
	std::vector<Lap> laps;
	int nextno;
	bool hasprev;			// 前の停止を覚えているか
	uint32 prevtick;
	int prevcpu;
	uint prevpc;
	unsigned __int64 total;
};

}
