// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	サブシステムとの通信のログのウインドウ
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	メイン・サブの CPU が 8255 のポート (FC・FD、選べば FE・FF も) に OUT した値を、新しい順に並べる。
//	記録はデバッガ (Debugger::SetSubLogEnabled) が行い、最大 4096 件 (古いものから消える)。
//	時刻はメインの CPU のクロック数から求めた、いちばん古い記録からの ms
// ---------------------------------------------------------------------------

#include "headers.h"
#include "resource.h"
#include "pc88/pc88.h"
#include "sublogmon.h"
#include "misc.h"
#include "labeledit.h"
#include "88config.h"
#include "winvars.h"

using namespace PC8801;

// ---------------------------------------------------------------------------
//	構築/消滅
//
SubLogMonitor::SubLogMonitor()
: core(0), started(false), lastcount(-1)
{
}

SubLogMonitor::~SubLogMonitor()
{
}

bool SubLogMonitor::Init(WinCore* wincore)
{
	core = wincore;
	if (!WinMonitor::Init(MAKEINTRESOURCE(IDD_SUBLOGMON), "MonitorSubLog", 70, 20))
		return false;
	SetUpdateTimer(200);
	return true;
}

// ---------------------------------------------------------------------------
//	ハンドシェイクを記録するかを M88.ini に保存/復元する
//
void SubLogMonitor::LoadMonitorConfig()
{
	WinMonitor::LoadMonitorConfig();
	bool hs = GetPrivateProfileInt("MonitorSubLog", "Handshake", 0, PC8801::GetM88IniPath()) != 0;
	core->Lock();
	core->GetDebugger()->SetSubLogHandshake(hs);
	core->Unlock();
}

void SubLogMonitor::SaveMonitorConfig()
{
	WinMonitor::SaveMonitorConfig();
	core->Lock();
	bool hs = core->GetDebugger()->IsSubLogHandshake();
	core->Unlock();
	WritePrivateProfileString("MonitorSubLog", "Handshake", hs ? "1" : "0", PC8801::GetM88IniPath());
}

// ---------------------------------------------------------------------------
//	窓を開いた直後。初めて開いたときはログを取り始める
//
void SubLogMonitor::Start()
{
	if (!started)
	{
		started = true;
		core->Lock();
		core->GetDebugger()->SetSubLogEnabled(true);
		core->Unlock();
	}
	EnableStatus(true);
	PutStatus("M>S: メイン→サブ  S>M: サブ→メイン  FC/FD: データ  FE/FF: ハンドシェイク");
	lastcount = -1;
}

// ---------------------------------------------------------------------------
//	1 件の説明。FF への OUT は、bit7 = 0 ならポート C のビットのセット/リセット
//
void SubLogMonitor::Describe(const Debugger::SubLogEntry& e, char* buf)
{
	buf[0] = 0;
	if (e.port == 0xff)
	{
		if (!(e.data & 0x80))
			wsprintf(buf, "PC%d=%d", (e.data >> 1) & 7, e.data & 1);
		else
			wsprintf(buf, "mode %.2X", e.data);
	}
	else if (e.data >= 0x20 && e.data < 0x7f)
		wsprintf(buf, "'%c'", e.data);
}

// ---------------------------------------------------------------------------
//	表示。新しいものが上
//
void SubLogMonitor::UpdateText()
{
	core->Lock();
	Debugger* dbg = core->GetDebugger();
	int count = dbg->GetSubLogCount();
	bool on = dbg->IsSubLogEnabled();
	uint clock = Max(1u, core->GetCPUSpeed());		// 100kHz 単位 (40 = 4MHz)

	SetLines(Max(1, count));
	int rows = Max(0, GetHeight() - 1);
	int top = GetLine();

	SetTxCol(0xc0c0c0);
	SetBkCol(RGB(0, 0, 0x50));
	Putf("%5s  %10s  %8s  %-3s  %-4s  %-4s  %-4s  %s\n", "#", "Time(ms)", "+clk", "Dir", "Port", "Data", "PC",
		 on ? "" : "(logging off)");

	Debugger::SubLogEntry first;
	bool hasfirst = dbg->GetSubLogEntry(0, &first);
	for (int y = 0; y < rows; y++)
	{
		int index = top + y;
		int i = count - 1 - index;			// 新しいものが上
		Debugger::SubLogEntry e, prev;
		if (i >= 0 && dbg->GetSubLogEntry(i, &e))
		{
			uint32 t = hasfirst ? e.tick - first.tick : 0;
			uint us = uint((unsigned __int64)(t) * 10 / clock);
			uint32 d = (i > 0 && dbg->GetSubLogEntry(i - 1, &prev)) ? e.tick - prev.tick : 0;
			char note[16];
			Describe(e, note);
			const char* sl = LabelNow(e.cpu, e.pc);		// PC のラベル (M88gn。説明の後ろに黄色で)

			SetBkCol(RGB(0, 0, 0x20));
			SetTxCol(e.cpu == Debugger::cpu_main ? 0xffffff : 0xa0ffa0);
			Putf("%5d  %6u.%.3u  %8u  %-3s  %.2X    %.2X    %.4X  %s",
				 i, us / 1000, us % 1000, d,
				 e.cpu == Debugger::cpu_main ? "M>S" : "S>M",
				 e.port, e.data, e.pc, note);
			if (sl)
			{
				if (note[0])
					Puts("  ");
				PutLabel(sl);
			}
			Puts("\n");
			continue;
		}
		SetBkCol(RGB(0, 0, 0x20));
		Puts("\n");
	}
	core->Unlock();
}

// ---------------------------------------------------------------------------
//	全件をタブ区切りで (古い順)
//
bool SubLogMonitor::BuildText(TextBuffer& tb)
{
	core->Lock();
	Debugger* dbg = core->GetDebugger();
	int count = dbg->GetSubLogCount();
	uint clock = Max(1u, core->GetCPUSpeed());
	tb.Add("#\tTime(ms)\t+clk\tDir\tPort\tData\tPC\tNote\r\n");
	Debugger::SubLogEntry first, e, prev;
	bool hasfirst = dbg->GetSubLogEntry(0, &first);
	for (int i = 0; i < count; i++)
	{
		if (!dbg->GetSubLogEntry(i, &e))
			continue;
		uint32 t = hasfirst ? e.tick - first.tick : 0;
		uint us = uint((unsigned __int64)(t) * 10 / clock);
		uint32 d = (i > 0 && dbg->GetSubLogEntry(i - 1, &prev)) ? e.tick - prev.tick : 0;
		char note[16 + Debugger::labelmax + 4];
		Describe(e, note);
		if (const char* sl = LabelNow(e.cpu, e.pc))
			lstrcat(note, note[0] ? "  " : ""), lstrcat(note, sl);
		tb.Addf("%d\t%u.%.3u\t%u\t%s\t%.2X\t%.2X\t%.4X\t%s\r\n", i, us / 1000, us % 1000, d,
				e.cpu == Debugger::cpu_main ? "M>S" : "S>M", e.port, e.data, e.pc, note);
	}
	core->Unlock();
	return !tb.Failed();
}

bool SubLogMonitor::CopyToClipboard()
{
	TextBuffer tb;
	BuildText(tb);
	return SetClipboardText(tb.Get(), tb.Length());
}

bool SubLogMonitor::SaveToFile()
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
	ofn.lpstrTitle = "Save Sub System Log";
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
BOOL SubLogMonitor::DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_COMMAND:
		switch (LOWORD(wp))
		{
		case IDM_SUBLOG_ENABLE:
			core->Lock();
			core->GetDebugger()->SetSubLogEnabled(!core->GetDebugger()->IsSubLogEnabled());
			core->Unlock();
			Update();
			break;

		case IDM_SUBLOG_HANDSHAKE:
			core->Lock();
			core->GetDebugger()->SetSubLogHandshake(!core->GetDebugger()->IsSubLogHandshake());
			core->Unlock();
			SaveMonitorConfig();
			break;

		case IDM_SUBLOG_CLEAR:
			core->Lock();
			core->GetDebugger()->ClearSubLog();
			core->Unlock();
			SetLine(0);
			Update();
			break;

		case IDM_SUBLOG_SAVE:
			SaveToFile();
			break;

		case IDM_CLOSE:
			PostMessage(hdlg, WM_CLOSE, 0, 0);
			break;
		}
		break;

	case WM_INITMENU:
		{
			core->Lock();
			bool on = core->GetDebugger()->IsSubLogEnabled();
			bool hs = core->GetDebugger()->IsSubLogHandshake();
			core->Unlock();
			CheckMenuItem((HMENU) wp, IDM_SUBLOG_ENABLE, on ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem((HMENU) wp, IDM_SUBLOG_HANDSHAKE, hs ? MF_CHECKED : MF_UNCHECKED);
		}
		break;
	}
	return WinMonitor::DlgProc(hdlg, msg, wp, lp);
}
