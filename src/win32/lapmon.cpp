// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ラップタイムの窓 (止まるたびに、前に止まった所・今回止まった所と、その間のクロック数・時間)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	クロック数はメインの CPU の通しのクロック数 (PC88::GetCPUTick、ウェイト込み) の差。
//	時間は今の CPU のクロック (4/8MHz) で割ったもの。止まった所は止まった CPU の PC
//	(サブの CPU で止まったときは S)。新しいものが上。Ctrl+C で全部をタブ区切りで (古い順)
// ---------------------------------------------------------------------------

#include "headers.h"
#include "resource.h"
#include "lapmon.h"
#include "misc.h"
#include "labeledit.h"
#include "winvars.h"

using namespace PC8801;

// ---------------------------------------------------------------------------
//	構築/消滅
//
LapMonitor::LapMonitor()
: core(0), nextno(0), hasprev(false), prevtick(0), prevcpu(0), prevpc(0), total(0)
{
}

LapMonitor::~LapMonitor()
{
}

bool LapMonitor::Init(WinCore* _core)
{
	core = _core;
	return WinMonitor::Init(MAKEINTRESOURCE(IDD_LAPMON), "MonitorLap", 90, 20);
}

void LapMonitor::Start()
{
	// クロック数はメモリのウェイト込み (コードの窓のサイクル数とは違う) ことを示す
	EnableStatus(true);
	PutStatus("Clocks はメモリのウェイト込み (コードの窓のサイクル数はウェイトを含まない)");
	SetLines(Max(1, int(laps.size())));
}

// ---------------------------------------------------------------------------
//	止まったときの記録。窓が閉じていても記録する
//
void LapMonitor::Record(uint32 tick, int cpu, uint pc, const char* why)
{
	Lap l;
	memset(&l, 0, sizeof(l));
	l.no = nextno++;
	l.first = !hasprev;
	l.fromcpu = uint8(prevcpu);
	l.frompc = uint16(prevpc);
	l.tocpu = uint8(cpu);
	l.topc = uint16(pc);
	l.clocks = hasprev ? tick - prevtick : 0;
	total += l.clocks;
	l.total = total;
	lstrcpyn(l.why, why, sizeof(l.why));

	if (laps.size() >= maxlaps)
		laps.erase(laps.begin());
	laps.push_back(l);

	hasprev = true;
	prevtick = tick;
	prevcpu = cpu;
	prevpc = pc;

	if (IsOpen())
	{
		SetLines(Max(1, int(laps.size())));
		SetLine(0);					// 新しいものが上なので、いちばん上を見せる
		Update();
	}
}

// 記録を消す。前に止まった所は覚えたまま (次の停止はそこからの差)
void LapMonitor::Clear()
{
	laps.clear();
	nextno = 0;
	total = 0;
	SetLines(1);
	SetLine(0);
	Update();
}

// ---------------------------------------------------------------------------
//	クロック数を ms に (clock は 100kHz 単位、40 = 4MHz)
//
void LapMonitor::FormatMs(unsigned __int64 clocks, uint clock, char* buf)
{
	unsigned __int64 us = clocks * 10 / Max(1u, clock);
	wsprintf(buf, "%u.%.3u", uint(us / 1000), uint(us % 1000));
}

// ---------------------------------------------------------------------------
//	表示。新しいものが上
//
void LapMonitor::UpdateText()
{
	uint clock = core->GetCPUSpeed();
	int count = int(laps.size());
	int rows = Max(0, GetHeight() - 1);
	int top = GetLine();

	SetTxCol(0xc0c0c0);
	SetBkCol(RGB(0, 0, 0x50));
	Putf("%5s  %-20s    %-20s  %13s  %11s  %12s  %s\n", "#", "From", "To", "Clocks(+wait)", "ms", "Total ms", "Stopped by");

	for (int y = 0; y < rows; y++)
	{
		int index = top + y;
		int i = count - 1 - index;
		SetBkCol(RGB(0, 0, 0x20));
		if (i < 0)
		{
			Puts("\n");
			continue;
		}
		const Lap& l = laps[i];
		char ms[24], tms[24];
		FormatMs(l.clocks, clock, ms);
		FormatMs(l.total, clock, tms);
		// 番地はラベルがあれば「名前(30EE)」、名前は黄色 (M88gn)
		SetTxCol(0xffffff);
		Putf("%5d  ", l.no);
		if (l.first)
			Puts("-                   ");
		else
		{
			Putf("%c ", l.fromcpu ? 'S' : 'M');
			PutAddrLabel(l.fromcpu, l.frompc, 18);
		}
		Putf(" -> %c ", l.tocpu ? 'S' : 'M');
		PutAddrLabel(l.tocpu, l.topc, 18);
		if (l.first)
			Putf("  %13s  %11s  %12s  ", "-", "-", tms);
		else
			Putf("  %13u  %11s  %12s  ", l.clocks, ms, tms);
		SetTxCol(0xa0ffa0);
		Putf("%s\n", l.why);
	}
}

// ---------------------------------------------------------------------------
//	全件をタブ区切りで (古い順)
//
bool LapMonitor::BuildText(TextBuffer& tb)
{
	uint clock = core->GetCPUSpeed();
	tb.Add("#\tFrom\tTo\tClocks (incl. memory wait)\tms\tTotal ms\tStopped by\r\n");
	for (size_t i = 0; i < laps.size(); i++)
	{
		const Lap& l = laps[i];
		char ms[24], tms[24], from[48], to[48], a[48];
		FormatMs(l.clocks, clock, ms);
		FormatMs(l.total, clock, tms);
		if (l.first)
			lstrcpy(from, "-");
		else
		{
			AddrLabelText(a, l.fromcpu, l.frompc);
			wsprintf(from, "%c %s", l.fromcpu ? 'S' : 'M', a);
		}
		AddrLabelText(a, l.tocpu, l.topc);
		wsprintf(to, "%c %s", l.tocpu ? 'S' : 'M', a);
		if (l.first)
			tb.Addf("%d\t%s\t%s\t-\t-\t%s\t%s\r\n", l.no, from, to, tms, l.why);
		else
			tb.Addf("%d\t%s\t%s\t%u\t%s\t%s\t%s\r\n", l.no, from, to, l.clocks, ms, tms, l.why);
	}
	return !tb.Failed();
}

bool LapMonitor::CopyToClipboard()
{
	TextBuffer tb;
	BuildText(tb);
	return SetClipboardText(tb.Get(), tb.Length());
}

bool LapMonitor::SaveToFile()
{
	OFNV5 ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = WINVAR(OFNSIZE);
	char filename[MAX_PATH];
	filename[0] = 0;
	ofn.hwndOwner = GetHWnd();
	ofn.lpstrFilter = "Text (tab separated) (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_OVERWRITEPROMPT;
	ofn.lpstrDefExt = "txt";
	ofn.lpstrTitle = "Save Lap Times";
	if (!GetSaveFileName(&ofn))
		return false;

	TextBuffer tb;
	BuildText(tb);
	FILE* fp = fopen(filename, "wb");
	if (!fp)
		return false;
	fwrite(tb.Get(), 1, tb.Length(), fp);
	fclose(fp);
	return true;
}

// ---------------------------------------------------------------------------
//	ダイアログ処理
//
BOOL LapMonitor::DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_COMMAND)
	{
		switch (LOWORD(wp))
		{
		case IDM_LAP_CLEAR:
			Clear();
			break;

		case IDM_LAP_SAVE:
			SaveToFile();
			break;

		case IDM_LAP_COPY:
			CopyToClipboard();
			break;

		case IDM_CLOSE:
			PostMessage(hdlg, WM_CLOSE, 0, 0);
			break;
		}
	}
	return WinMonitor::DlgProc(hdlg, msg, wp, lp);
}
