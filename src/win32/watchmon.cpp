// ---------------------------------------------------------------------------
//	M88 - PC-8801 emulator
//	ウォッチウインドウ
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#include "headers.h"
#include "resource.h"
#include "pc88/pc88.h"
#include "watchmon.h"
#include "misc.h"
#include "winexapi.h"
#include "watchbank.h"

using namespace PC8801;

namespace
{
	// UpdateText() の Putf フォーマット文字列と対応する列の文字位置(0起点)。
	// フォーマットを変えたら、ここも必ず合わせて直すこと。
	//
	//   "%3d  %-4s  %-4s  %-6s  %-2s  %-6s  %-7s  %-18s  %-7s  %-4s  %s\n"
	//   (M88gn: Addr の後に Bank の列 (6 桁 + 空白 2) を足したので、下の図より値の列は 8 桁右)
	//     0-2  3-4 5-8  9-10 11-14 15-16 17-18 19-20 21-26 27-28 29-35 36-37 38-55  56-57 58-64 65-66 67-70 71-72 73-
	//      #        CPU        Addr        Sz          Hex         Dec         Bin                  Raw         Ch          Label
	//
	// Hex/Dec/Bin/Ch は「値」、Raw はアドレス順の実バイト格納(ワードなら
	// 下位バイトが先)。ダブルクリック判定は Hex～Ch をまとめて「値の列」として扱う
	const int col_value_lo = 21 + 8;
	const int col_value_hi = 71 + 8;

	// value の下位 bits ビットぶんを2進数文字列にする(MSBが先。dest は bits+1 バイト必要)
	void ToBinary(char* dest, uint value, int bits)
	{
		for (int i = bits - 1; i >= 0; i--)
			*dest++ = char('0' + ((value >> i) & 1));
		*dest = 0;
	}
}

// ---------------------------------------------------------------------------
//	構築/消滅
//
WatchMonitor::WatchMonitor()
: core(0), cursor(-1), editindex(-1), editbank(MemoryViewer::mainram)
{
}

// ---------------------------------------------------------------------------
//	ウォッチが読み書きするバス (M88gn: メインの CPU はウォッチのバンクに切り替える)
//
MemoryBus* WatchMonitor::GetWatchBus(const Debugger::WatchEntry& w)
{
	if (w.cpu == Debugger::cpu_sub)
		return mvsub.GetBus();
	SelectWatchBank(mvmain, w.bank);
	return mvmain.GetBus();
}

WatchMonitor::~WatchMonitor()
{
}

// ---------------------------------------------------------------------------
//	初期化
//
bool WatchMonitor::Init(WinCore* wincore)
{
	core = wincore;
	if (!WinMonitor::Init(MAKEINTRESOURCE(IDD_WATCHMON), "MonitorWatch", 90, 12))
		return false;

	// main は既定で a0/a6/a8/ac/af 全部 mainram(MemoryViewer::Init が
	// SelectBank(mainram,...) 済み)。sub は $0000-$3fff が ROM 固定なので
	// a0 だけ sub に切り替える(MemoryViewer::SelectBank 参照)
	mvmain.Init(core);
	mvsub.Init(core);
	mvsub.SelectBank(MemoryViewer::sub, MemoryViewer::sub,
					  MemoryViewer::mainram, MemoryViewer::mainram, MemoryViewer::mainram);

	SetUpdateTimer(200);
	return true;
}

// ---------------------------------------------------------------------------
//	テキスト更新。0行目は列見出し(毎フレーム書き直すので固定表示になる)。
//	「今CPUが読んだら返ってくる値」ではなく「指定アドレスに実際に格納
//	されている値」を見せるのが目的なので、MemoryMonitor と同じく
//	MemoryViewer の私設バス(バンク固定)を読む。VM の MemoryManager を
//	経由しないので Debugger の read ブレークポイントも誤爆しない
//
void WatchMonitor::UpdateText()
{
	core->Lock();
	Debugger* dbg = core->GetDebugger();
	int count = dbg->GetWatchCount();

	SetLines(Max(1, count));
	int rows = Max(0, GetHeight() - 1);
	int top = GetLine();

	SetTxCol(0xc0c0c0);
	SetBkCol(RGB(0, 0, 0x50));
	Putf("%3s  %-4s  %-4s  %-6s  %-2s  %-6s  %-7s  %-18s  %-7s  %-4s  %s\n",
		 "#", "CPU", "Addr", "Bank", "Sz", "Hex", "Dec", "Bin", "Raw", "Ch", "Label");

	for (int y = 0; y < rows; y++)
	{
		int index = top + y;
		Debugger::WatchEntry w;
		if (index < count && dbg->GetWatch(index, &w))
		{
			MemoryBus* bus = GetWatchBus(w);

			char hexbuf[8], decbuf[8], binbuf[20], rawbuf[8], chbuf[8];
			if (w.size == 2)
			{
				uint lo = bus->Read8(w.addr) & 0xff;
				uint hi = bus->Read8((w.addr + 1) & 0xffff) & 0xff;
				uint value = (hi << 8) | lo;

				wsprintf(hexbuf, "%.4x", value);
				wsprintf(decbuf, "%u", value);
				ToBinary(binbuf, value, 16);
				// アドレス順の実バイト格納(下位バイトが先)
				wsprintf(rawbuf, "%.2x %.2x", lo, hi);
				chbuf[0] = 0;
			}
			else
			{
				uint d = bus->Read8(w.addr) & 0xff;

				wsprintf(hexbuf, "%.2x", d);
				wsprintf(decbuf, "%u", d);
				ToBinary(binbuf, d, 8);
				wsprintf(rawbuf, "%.2x", d);
				wsprintf(chbuf, "'%c'", (d >= 0x20 && d < 0x7f) ? char(d) : '.');
			}

			SetBkCol(index == cursor ? RGB(0, 0, 0x70) : RGB(0, 0, 0x20));
			SetTxCol(0xffffff);
			Putf("%3d  %-4s  %.4x  %-6s  %-2s  %-6s  %-7s  %-18s  %-7s  %-4s  %s\n",
				 index,
				 w.cpu == Debugger::cpu_sub ? "SUB" : "MAIN",
				 w.addr,
				 w.cpu == Debugger::cpu_sub ? "-" : WatchBankName(w.bank),
				 w.size == 2 ? "W" : "B",
				 hexbuf, decbuf, binbuf, rawbuf, chbuf,
				 w.label);
		}
		else
		{
			SetBkCol(RGB(0, 0, 0x20));
			Puts("\n");
		}
	}
	core->Unlock();
}

// ---------------------------------------------------------------------------
//	クリック処理。p はキャレット座標(GetTextPos 変換済み)
//
void WatchMonitor::OnClick(POINT p, bool dblclick)
{
	if (p.y < 1)
		return;		// 見出し行はクリック対象外

	core->Lock();
	int count = core->GetDebugger()->GetWatchCount();
	core->Unlock();

	int index = GetLine() + p.y - 1;
	if (index < 0 || index >= count)
		return;

	if (!dblclick)
	{
		cursor = index;
		Update();
		return;
	}

	if (col_value_lo <= p.x && p.x < col_value_hi)
	{
		cursor = index;
		ShowValueDialog(index);
	}
	else
	{
		cursor = index;
		ShowEditDialog(index);
	}
	Update();
}

void WatchMonitor::DeleteSelected()
{
	if (cursor < 0)
		return;

	core->Lock();
	Debugger* dbg = core->GetDebugger();
	dbg->DeleteWatch(cursor);
	int count = dbg->GetWatchCount();
	core->Unlock();

	cursor = Min(cursor, count - 1);
	Update();
}

// ---------------------------------------------------------------------------
//	ダイアログ処理
//
BOOL WatchMonitor::DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	POINT p;

	switch (msg)
	{
	case WM_COMMAND:
		switch (LOWORD(wp))
		{
		case IDM_WATCH_ADD:
			ShowEditDialog(-1);
			Update();
			break;

		case IDM_WATCH_EDIT:
			if (cursor >= 0)
				ShowEditDialog(cursor);
			Update();
			break;

		case IDM_WATCH_DELETE:
			DeleteSelected();
			break;

		case IDM_WATCH_DELETEALL:
			core->Lock();
			core->GetDebugger()->DeleteAllWatches();
			core->Unlock();
			cursor = -1;
			Update();
			break;

		case IDM_CLOSE:
			PostMessage(hdlg, WM_CLOSE, 0, 0);
			break;
		}
		break;

	case WM_LBUTTONDOWN:
		p.x = LOWORD(lp);
		p.y = HIWORD(lp);
		if (GetTextPos(&p))
			OnClick(p, false);
		break;

	case WM_LBUTTONDBLCLK:
		p.x = LOWORD(lp);
		p.y = HIWORD(lp);
		if (GetTextPos(&p))
			OnClick(p, true);
		break;

	case WM_KEYDOWN:
		if (wp == VK_DELETE)
		{
			DeleteSelected();
			return 0;
		}
		if (wp == VK_INSERT)
		{
			ShowEditDialog(-1);
			Update();
			return 0;
		}
		break;
	}
	return WinMonitor::DlgProc(hdlg, msg, wp, lp);
}

// ---------------------------------------------------------------------------
//	追加/編集ダイアログ(アドレス/サイズ/CPU/ラベル。IDD_WATCH_EDIT を使う)
//
void WatchMonitor::ShowEditDialog(int index)
{
	editindex = index;
	(*EnableIME)(GetHWnd(), true);
	DialogBoxParam(GetHInst(), MAKEINTRESOURCE(IDD_WATCH_EDIT), GetHWnd(),
				   DLGPROC((void*)EDlgProcGate), (LPARAM)this);
	(*EnableIME)(GetHWnd(), false);
}

BOOL WatchMonitor::EDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	char buf[16];

	switch (msg)
	{
	case WM_INITDIALOG:
		{
			Debugger::WatchEntry w;
			bool has = false;
			if (editindex >= 0)
			{
				core->Lock();
				has = core->GetDebugger()->GetWatch(editindex, &w);
				core->Unlock();
			}
			if (has)
			{
				wsprintf(buf, "%.4x", w.addr);
				SetDlgItemText(hdlg, IDC_WATCH_ADDR, buf);
				CheckDlgButton(hdlg, IDC_WATCH_SUBCPU, w.cpu == Debugger::cpu_sub ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hdlg, IDC_WATCH_WORD, w.size == 2 ? BST_CHECKED : BST_UNCHECKED);
				SetDlgItemText(hdlg, IDC_WATCH_LABEL, w.label);
				editbank = FindWatchBank(w.bank) ? w.bank : MemoryViewer::mainram;
			}
			else
			{
				SetDlgItemText(hdlg, IDC_WATCH_ADDR, "0000");
				SetDlgItemText(hdlg, IDC_WATCH_LABEL, "");
				editbank = MemoryViewer::mainram;
			}
			FillBankCombo(hdlg);
			SetFocus(GetDlgItem(hdlg, IDC_WATCH_ADDR));
			SendDlgItemMessage(hdlg, IDC_WATCH_ADDR, EM_SETSEL, 0, -1);
		}
		return 0;

	case WM_COMMAND:
		// M88gn: バンクの選び。番地・大きさ・Sub CPU が変わったら、選べるバンクを並べ直す
		if (LOWORD(wp) == IDC_WATCH_BANK && HIWORD(wp) == CBN_SELCHANGE)
		{
			int sel = int(SendDlgItemMessage(hdlg, IDC_WATCH_BANK, CB_GETCURSEL, 0, 0));
			if (sel >= 0)
				editbank = int(SendDlgItemMessage(hdlg, IDC_WATCH_BANK, CB_GETITEMDATA, sel, 0));
			return 0;
		}
		if ((LOWORD(wp) == IDC_WATCH_ADDR && HIWORD(wp) == EN_CHANGE)
			|| ((LOWORD(wp) == IDC_WATCH_WORD || LOWORD(wp) == IDC_WATCH_SUBCPU) && HIWORD(wp) == BN_CLICKED))
		{
			FillBankCombo(hdlg);
			return 0;
		}
		if (HIWORD(wp) == BN_CLICKED)
		{
			switch (LOWORD(wp))
			{
				char* dum;
			case IDOK:
				GetDlgItemText(hdlg, IDC_WATCH_ADDR, buf, 5);
				if (isxdigit((uint8) buf[0]))
				{
					Debugger::WatchEntry w;
					w.addr = strtoul(buf, &dum, 16) & 0xffff;
					w.cpu = uint8(IsDlgButtonChecked(hdlg, IDC_WATCH_SUBCPU) == BST_CHECKED
								  ? Debugger::cpu_sub : Debugger::cpu_main);
					w.size = uint8(IsDlgButtonChecked(hdlg, IDC_WATCH_WORD) == BST_CHECKED ? 2 : 1);
					w.bank = uint8(editbank);
					GetDlgItemText(hdlg, IDC_WATCH_LABEL, w.label, sizeof(w.label));

					// 番地がバンクの範囲に入るか (M88gn。先にバンクを選んでから番地を入れたとき)
					uint lo = watchsub_lo, hi = watchsub_hi;
					const char* bname = "Sub CPU";
					if (w.cpu != Debugger::cpu_sub)
					{
						const WatchBank* wb = FindWatchBank(editbank);
						lo = wb ? wb->lo : 0, hi = wb ? wb->hi : 0xffff;
						bname = wb ? wb->name : "?";
					}
					if (w.addr < lo || w.addr + w.size - 1 > hi)
					{
						char msg[128];
						wsprintf(msg, "%s で置ける番地は %.4X～%.4X です%s。", bname, lo, hi,
								 w.size == 2 ? " (Word は 2 バイトとも)" : "");
						MessageBox(hdlg, msg, "Set Watch", MB_OK | MB_ICONWARNING);
						SetFocus(GetDlgItem(hdlg, IDC_WATCH_ADDR));
						return 0;
					}

					core->Lock();
					if (editindex >= 0)
						core->GetDebugger()->SetWatch(editindex, w);
					else
						editindex = core->GetDebugger()->AddWatch(w);
					core->Unlock();

					cursor = editindex;
				}
			case IDCANCEL:
				EndDialog(hdlg, false);
				break;
			}
		}
		return 0;

	case WM_CLOSE:
		EndDialog(hdlg, false);
		return true;

	default:
		return false;
	}
}

BOOL CALLBACK WatchMonitor::EDlgProcGate(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
	WatchMonitor* mon = 0;

	if (m == WM_INITDIALOG)
	{
		mon = reinterpret_cast<WatchMonitor*>(l);
		if (mon)
			::SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)mon);
	}
	else
	{
		mon = (WatchMonitor*)::GetWindowLongPtr(hwnd, GWLP_USERDATA);
	}

	if (mon)
		return mon->EDlgProc(hwnd, m, w, l);
	return FALSE;
}

// ---------------------------------------------------------------------------
//	追加・編集の窓のバンクの選び (M88gn)。番地が入っていれば、その番地 (Word なら 2 バイトとも)
//	が入るバンクだけを並べる。今選んでいるバンクは、範囲に入らなくても残す
//	(先にバンクを選んだときは、OK で番地の範囲を確かめる)。Sub CPU のときは選べない
//
void WatchMonitor::FillBankCombo(HWND hdlg)
{
	HWND hcb = GetDlgItem(hdlg, IDC_WATCH_BANK);
	if (!hcb)
		return;

	char buf[16];
	char* dum;
	GetDlgItemText(hdlg, IDC_WATCH_ADDR, buf, 5);
	bool hasaddr = isxdigit((uint8) buf[0]) != 0;
	uint addr = hasaddr ? (strtoul(buf, &dum, 16) & 0xffff) : 0;
	uint size = IsDlgButtonChecked(hdlg, IDC_WATCH_WORD) == BST_CHECKED ? 2 : 1;
	bool subcpu = IsDlgButtonChecked(hdlg, IDC_WATCH_SUBCPU) == BST_CHECKED;

	SendMessage(hcb, CB_RESETCONTENT, 0, 0);
	if (subcpu)
	{
		int i = int(SendMessage(hcb, CB_ADDSTRING, 0, (LPARAM) "(Sub CPU)"));
		SendMessage(hcb, CB_SETITEMDATA, i, editbank);
		SendMessage(hcb, CB_SETCURSEL, i, 0);
		EnableWindow(hcb, false);
		return;
	}
	EnableWindow(hcb, true);
	for (int k = 0; k < nwatchbanks; k++)
	{
		const WatchBank* wb = &watchbanks[k];
		if (hasaddr && !WatchBankFits(wb, addr, size) && wb->type != editbank)
			continue;
		char name[32];
		wsprintf(name, "%s (%.4X-%.4X)", wb->name, wb->lo, wb->hi);
		int i = int(SendMessage(hcb, CB_ADDSTRING, 0, (LPARAM) name));
		SendMessage(hcb, CB_SETITEMDATA, i, wb->type);
		if (wb->type == editbank)
			SendMessage(hcb, CB_SETCURSEL, i, 0);
	}
}

// ---------------------------------------------------------------------------
//	値そのものの編集(IDD_MEMORY_EDIT を流用。B は2桁、W は4桁の16進)
//
void WatchMonitor::ShowValueDialog(int index)
{
	editindex = index;
	DialogBoxParam(GetHInst(), MAKEINTRESOURCE(IDD_MEMORY_EDIT), GetHWnd(),
				   DLGPROC((void*)VDlgProcGate), (LPARAM)this);
}

BOOL WatchMonitor::VDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	char buf[8];

	switch (msg)
	{
	case WM_INITDIALOG:
		{
			core->Lock();
			Debugger::WatchEntry w;
			bool has = core->GetDebugger()->GetWatch(editindex, &w);
			uint value = 0;
			if (has)
			{
				MemoryBus* bus = GetWatchBus(w);
				value = bus->Read8(w.addr) & 0xff;
				if (w.size == 2)
					value |= (bus->Read8((w.addr + 1) & 0xffff) & 0xff) << 8;
			}
			core->Unlock();

			wsprintf(buf, "%.4x:", has ? w.addr : 0);
			SetDlgItemText(hdlg, IDC_MEMORY_EDIT_TEXT, buf);
			wsprintf(buf, (has && w.size == 2) ? "%.4x" : "%.2x", value);
			SetDlgItemText(hdlg, IDC_MEMORY_EDITBOX, buf);
			SetFocus(GetDlgItem(hdlg, IDC_MEMORY_EDITBOX));
			SendDlgItemMessage(hdlg, IDC_MEMORY_EDITBOX, EM_SETSEL, 0, -1);
		}
		return 0;

	case WM_COMMAND:
		if (HIWORD(wp) == BN_CLICKED)
		{
			switch (LOWORD(wp))
			{
				char* dum;
			case IDOK:
				GetDlgItemText(hdlg, IDC_MEMORY_EDITBOX, buf, sizeof(buf));
				if (isxdigit((uint8) buf[0]))
				{
					uint v = strtoul(buf, &dum, 16);

					core->Lock();
					Debugger::WatchEntry w;
					if (core->GetDebugger()->GetWatch(editindex, &w))
					{
						MemoryBus* bus = GetWatchBus(w);
						bus->Write8(w.addr, v & 0xff);
						if (w.size == 2)
							bus->Write8((w.addr + 1) & 0xffff, (v >> 8) & 0xff);
					}
					core->Unlock();
				}
			case IDCANCEL:
				EndDialog(hdlg, false);
				break;
			}
		}
		return 0;

	case WM_CLOSE:
		EndDialog(hdlg, false);
		return true;

	default:
		return false;
	}
}

BOOL CALLBACK WatchMonitor::VDlgProcGate(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
	WatchMonitor* mon = 0;

	if (m == WM_INITDIALOG)
	{
		mon = reinterpret_cast<WatchMonitor*>(l);
		if (mon)
			::SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)mon);
	}
	else
	{
		mon = (WatchMonitor*)::GetWindowLongPtr(hwnd, GWLP_USERDATA);
	}

	if (mon)
		return mon->VDlgProc(hwnd, m, w, l);
	return FALSE;
}
