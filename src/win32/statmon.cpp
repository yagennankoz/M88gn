// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ステータスの窓 (ステータスバーと同じ情報と、止まった理由の詳しい説明)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	ステータスバーは字数が限られ、隠していることもあるので、同じ知らせとランプを
//	この窓にも出す。デバッガで止まったときは、止めたブレークポイント・読み書きした
//	番地と値・その命令の番地・前に止まってからのクロック数を出す
// ---------------------------------------------------------------------------

#include "headers.h"
#include "resource.h"
#include "statmon.h"
#include "status.h"
#include "patbp.h"
#include "misc.h"
#include "labeledit.h"

using namespace PC8801;

// ---------------------------------------------------------------------------
//	構築/消滅
//
StatusMonitor::StatusMonitor()
: core(0), hasstop(false), reason(0), cpu(0), addr(0), len(0), data(-1), pc(0),
  index(-1), viewno(-1), hasbp(false), pc1(0), pc2(0)
{
	lap[0] = 0;
	brief[0] = 0;
}

StatusMonitor::~StatusMonitor()
{
}

bool StatusMonitor::Init(WinCore* _core)
{
	core = _core;
	if (!WinMonitor::Init(MAKEINTRESOURCE(IDD_STATMON), "MonitorStatus", 64, 14))
		return false;
	SetUpdateTimer(200);		// 知らせ・ランプ・動いているかを追う
	return true;
}

void StatusMonitor::Start()
{
	SetUpdateTimer(200);
}

// ---------------------------------------------------------------------------
//	止まった理由を覚える。ステータスバーの短い説明も作る
//
void StatusMonitor::CaptureStop(char* out, int size, const char* laptext)
{
	Debugger* dbg = core->GetDebugger();
	core->Lock();
	bool broken = dbg->IsBroken();
	reason = broken ? dbg->GetBreakReason() : Debugger::brk_user;
	cpu = broken ? dbg->GetBreakCpu() : Debugger::cpu_main;
	addr = dbg->GetBreakAddr();
	len = dbg->GetBreakLength();
	data = dbg->GetBreakData();
	pc = dbg->GetBreakPC();
	index = broken ? dbg->GetBreakIndex() : -1;
	hasbp = index >= 0 && dbg->GetBreakPoint(index, &bp);
	viewno = -1;
	if (hasbp)
	{
		// ブレークポイントの窓は、メモリ (パターンを含む) と I/O を別の一覧で、0 から数える
		viewno = 0;
		for (int i = 0; i < index; i++)
		{
			Debugger::BreakPoint b;
			if (dbg->GetBreakPoint(i, &b) && b.io == bp.io)
				viewno++;
		}
	}
	pc1 = core->GetCPU1()->GetPC();
	pc2 = core->GetCPU2()->GetPC();
	core->Unlock();
	lstrcpyn(lap, laptext, sizeof(lap));
	hasstop = true;

	// ステータスバーの短い説明
	char no[16] = "";
	if (hasbp)
		wsprintf(no, "%s#%d ", bp.io ? "IO BP" : "BP", viewno);
	const char* c = cpu == Debugger::cpu_sub ? "SUB " : "";
	char d[8] = "";
	if (data >= 0)
		wsprintf(d, "=%.2X ", data);
	// 番地はラベルがあれば「名前(1234)」(M88gn)
	char at[48], pt[48];
	AddrLabelText(at, cpu, addr);
	AddrLabelText(pt, cpu, pc);
	switch (reason)
	{
	case Debugger::brk_exec:	wsprintf(brief, "%s%sexec %s", c, no, at); break;
	case Debugger::brk_memread:	wsprintf(brief, "%s%sread %s %sby %s", c, no, at, d, pt); break;
	case Debugger::brk_memwrite:wsprintf(brief, "%s%swrite %s %sby %s", c, no, at, d, pt); break;
	case Debugger::brk_ioread:	wsprintf(brief, "%s%sIN %.2X by %s", c, no, addr, pt); break;
	case Debugger::brk_iowrite:	wsprintf(brief, "%s%sOUT %.2X %sby %s", c, no, addr, d, pt); break;
	case Debugger::brk_pattern:	wsprintf(brief, "%s%spattern %s-%.4X by %s", c, no, at, (addr + Max(int(len), 1) - 1) & 0xffff, pt); break;
	case Debugger::brk_step:	wsprintf(brief, "%sStep", c); break;
	default:					lstrcpy(brief, "Pause"); break;
	}
	lstrcpyn(out, brief, size);
}

// ---------------------------------------------------------------------------
//	止めたブレークポイントの中身 (ブレークポイントの窓の一覧と同じ書き方)
//
void StatusMonitor::PutBreakPoint()
{
	char what[300];
	if (bp.pattern)
	{
		char pd[256];
		DescribePatternBreak(bp, pd);
		wsprintf(what, "%.4X-%.4X  %s", bp.addr, (bp.addr + bp.length - 1) & 0xffff, pd);
	}
	else if (bp.io)
	{
		const char* dir = bp.read && bp.write ? "IN OUT" : bp.read ? "IN" : "OUT";
		if (bp.length > 1)
			wsprintf(what, "port %.2X-%.2X  %s", bp.addr, (bp.addr + bp.length - 1) & 0xff, dir);
		else
			wsprintf(what, "port %.2X  %s", bp.addr, dir);
	}
	else
		wsprintf(what, "%.4X-%.4X  %s%s%s", bp.addr, (bp.addr + bp.length - 1) & 0xffff,
				 bp.exec ? "E" : "-", bp.read ? "R" : "-", bp.write ? "W" : "-");
	Putf("  Breakpoint : %s\n", what);
	if (bp.cond[0])
		Putf("  Condition  : %s\n", bp.cond);
	if (bp.hitcount > 1)
		Putf("  Hit count  : %u (stop from the %u%s hit)\n", bp.hits, bp.hitcount,
			 bp.hitcount % 10 == 1 && bp.hitcount % 100 != 11 ? "st" :
			 bp.hitcount % 10 == 2 && bp.hitcount % 100 != 12 ? "nd" :
			 bp.hitcount % 10 == 3 && bp.hitcount % 100 != 13 ? "rd" : "th");
	if (bp.comment[0])
		Putf("  Comment    : %s\n", bp.comment);
}

// ---------------------------------------------------------------------------
//	表示
//
void StatusMonitor::UpdateText()
{
	const COLORREF lab = 0xa0a0a0, val = 0xffffff, bk = RGB(0, 0, 0x20);
	SetBkCol(bk);
	SetTxCol(lab);
	ClearText();			// 行の数が変わるので、前の表示の残りを消す

	bool stopped = core->IsStopped();
	bool stepping = !stopped && core->IsStepping();

	SetTxCol(lab); Puts("State        : ");
	SetTxCol(stopped ? RGB(0xff, 0x90, 0xc0) : val);
	Puts(stopped ? "PAUSED" : stepping ? "STEPPING" : "RUNNING");
	SetTxCol(lab);
	if (stopped)
	{
		// 番地のラベルは黄色 (M88gn)
		Puts("    PC  MAIN ");
		PutAddrLabel(0, pc1);
		Puts("  SUB ");
		PutAddrLabel(1, pc2);
	}
	Puts("\n");

	// 止まった理由 (動いているときは最後に止まったとき)
	SetTxCol(lab);
	Puts(stopped ? "Stopped by   : " : "Last stop    : ");
	SetTxCol(val);
	if (!hasstop)
		Puts("-\n");
	else
	{
		const char* c = cpu == Debugger::cpu_sub ? "SUB CPU" : "MAIN CPU";
		char no[40] = "";
		if (hasbp)
			wsprintf(no, "%s #%d, ", bp.io ? "I/O breakpoint" : "breakpoint", viewno);
		switch (reason)
		{
		case Debugger::brk_exec:	Putf("%sexecute (%s)\n", no, c); break;
		case Debugger::brk_memread:	Putf("%smemory read (%s)\n", no, c); break;
		case Debugger::brk_memwrite:Putf("%smemory write (%s)\n", no, c); break;
		case Debugger::brk_ioread:	Putf("%sIN (%s)\n", no, c); break;
		case Debugger::brk_iowrite:	Putf("%sOUT (%s)\n", no, c); break;
		case Debugger::brk_pattern:	Putf("%spattern completed by a write (%s)\n", no, c); break;
		case Debugger::brk_step:	Putf("step (%s)\n", c); break;
		default:					Puts("Pause\n"); break;
		}

		SetTxCol(lab);
		switch (reason)
		{
		case Debugger::brk_exec:
		case Debugger::brk_step:
			Putf("  Address    : "); SetTxCol(val); PutAddrLabel(cpu, addr); Puts("\n");
			break;
		case Debugger::brk_memread:
		case Debugger::brk_memwrite:
			Putf("  Address    : "); SetTxCol(val); PutAddrLabel(cpu, addr);
			if (data >= 0)
				Putf("  data %.2X", data);
			SetTxCol(lab); Putf("\n  Instruction: "); SetTxCol(val); PutAddrLabel(cpu, pc); Puts("\n");
			break;
		case Debugger::brk_ioread:
		case Debugger::brk_iowrite:
			Putf("  Port       : "); SetTxCol(val); Putf("%.2X", addr);
			if (data >= 0)
				Putf("  data %.2X", data);
			SetTxCol(lab); Putf("\n  Instruction: "); SetTxCol(val); PutAddrLabel(cpu, pc); Puts("\n");
			break;
		case Debugger::brk_pattern:
			Putf("  Found at   : "); SetTxCol(val);
			PutAddrLabel(cpu, addr);
			Putf("-%.4X", (addr + Max(int(len), 1) - 1) & 0xffff);
			if (data >= 0)
				Putf("  (last byte written %.2X)", data);
			SetTxCol(lab); Putf("\n  Instruction: "); SetTxCol(val); PutAddrLabel(cpu, pc); Puts("\n");
			break;
		}
		if (hasbp)
		{
			SetTxCol(lab);
			PutBreakPoint();
		}
		SetTxCol(lab); Puts("  Since prev : "); SetTxCol(val);
		Putf("%s%s\n", lap[0] ? lap : "-", lap[0] ? "  incl. memory wait" : "");
	}

	// ステータスバーと同じ知らせとランプ
	char msg[128];
	statusdisplay.GetText(msg, sizeof(msg));
	SetTxCol(lab); Puts("Status bar   : "); SetTxCol(val); Putf("%s\n", msg);
	char perf[64];
	statusdisplay.GetPerf(perf, sizeof(perf));
	SetTxCol(lab); Puts("Speed        : "); SetTxCol(val); Putf("%s\n", perf[0] ? perf : "-");

	int l[3];
	statusdisplay.GetLamps(l);
	SetTxCol(lab); Puts("Lamps        : ");
	for (int i = 0; i < 2; i++)
	{
		SetTxCol(lab);
		Putf("Drive %d ", i + 1);
		SetTxCol((l[i] & 1) ? ((l[i] & 2) ? RGB(0, 0xff, 0) : RGB(0xff, 0x40, 0x40)) : RGB(0x70, 0x70, 0x70));
		Putf("%-8s", (l[i] & 1) ? ((l[i] & 2) ? "ON(2HD)" : "ON") : "off");
	}
	if (statusdisplay.IsShowFDStat())
	{
		SetTxCol(lab); Puts("Sub system ");
		SetTxCol((l[2] & 1) ? RGB(0, 0x80, 0xff) : RGB(0x70, 0x70, 0x70));
		Puts((l[2] & 1) ? "busy" : "-");
	}
	Puts("\n");
}
