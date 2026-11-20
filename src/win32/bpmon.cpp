// ---------------------------------------------------------------------------
//	M88 - PC-8801 emulator
//	ブレークポイント管理ウインドウ
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#include "headers.h"
#include "resource.h"
#include "pc88/pc88.h"
#include "bpmon.h"
#include "misc.h"
#include "winexapi.h"

using namespace PC8801;

namespace
{
	// UpdateText() の Putf フォーマット文字列と対応する列の文字位置(0起点)。
	// フォーマットを変えたら、ここも必ず合わせて直すこと。
	// Comment 列は末尾に足しただけなので、クリック判定(col_enabled_hi より
	// 右)には影響しない。ダブルクリックで編集ダイアログが開くのは意図どおり
	//
	// ★ %-3s は「最小幅3」であって「幅3で切る」ではない。7文字の "Enabled"
	//   がそのまま出るので %-7s にすること(F-6)。on/off は 30-32 に収まるので
	//   col_enabled_hi = 33 のままでよい
	//
	//   "%3d  %-4s  %.4x  %.4x  %c %c %c  %-7s  %s\n"
	//     0-2  3-4 5-8  9-10 11-14 15-16 17-20 21-22 23 24 25 26 27 28-29 30-36 38-
	//      #        CPU        Addr        Len         E     R     W      Enabled  Comment
	const int col_exec       = 23;
	const int col_read       = 25;
	const int col_write      = 27;
	const int col_enabled_lo = 30;
	const int col_enabled_hi = 33;
}

// ---------------------------------------------------------------------------
//	構築/消滅
//
BreakMonitor::BreakMonitor()
: core(0), cursor(-1), editindex(-1)
{
}

BreakMonitor::~BreakMonitor()
{
}

// ---------------------------------------------------------------------------
//	初期化
//
bool BreakMonitor::Init(WinCore* wincore)
{
	core = wincore;
	if (!WinMonitor::Init(MAKEINTRESOURCE(IDD_BPMON), "MonitorBP", 70, 12))
		return false;
	SetUpdateTimer(500);
	return true;
}

// ---------------------------------------------------------------------------
//	テキスト更新。0行目は列見出し(毎フレーム書き直すので固定表示になる)
//
void BreakMonitor::UpdateText()
{
	core->Lock();
	Debugger* dbg = core->GetDebugger();
	int count = dbg->GetBreakPointCount();

	SetLines(Max(1, count));
	int rows = Max(0, GetHeight() - 1);
	int top = GetLine();

	SetTxCol(0xc0c0c0);
	SetBkCol(RGB(0, 0, 0x50));
	Putf("%3s  %-4s  %-4s  %-4s  %c %c %c  %-7s  %s\n",
		 "#", "CPU", "Addr", "Len", 'E', 'R', 'W', "Enabled", "Comment");

	for (int y = 0; y < rows; y++)
	{
		int index = top + y;
		Debugger::BreakPoint bp;
		if (index < count && dbg->GetBreakPoint(index, &bp))
		{
			SetBkCol(index == cursor ? RGB(0, 0, 0x70) : RGB(0, 0, 0x20));
			SetTxCol(bp.enabled ? 0xffffff : 0x808080);
			Putf("%3d  %-4s  %.4x  %.4x  %c %c %c  %-7s  %s\n",
				 index,
				 bp.cpu == Debugger::cpu_sub ? "SUB" : "MAIN",
				 bp.addr, bp.length,
				 bp.exec  ? '*' : '-',
				 bp.read  ? '*' : '-',
				 bp.write ? '*' : '-',
				 bp.enabled ? "on" : "off",
				 bp.comment);
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
void BreakMonitor::OnClick(POINT p, bool dblclick)
{
	if (p.y < 1)
		return;		// 見出し行はクリック対象外

	core->Lock();
	int count = core->GetDebugger()->GetBreakPointCount();
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

	if (p.x == col_exec)
		ToggleAttr(index, 'E');
	else if (p.x == col_read)
		ToggleAttr(index, 'R');
	else if (p.x == col_write)
		ToggleAttr(index, 'W');
	else if (col_enabled_lo <= p.x && p.x < col_enabled_hi)
		ToggleAttr(index, 'e');
	else
	{
		cursor = index;
		ShowEditDialog(index);
	}
	Update();
}

void BreakMonitor::ToggleAttr(int index, int col)
{
	core->Lock();
	Debugger* dbg = core->GetDebugger();
	Debugger::BreakPoint bp;
	if (dbg->GetBreakPoint(index, &bp))
	{
		switch (col)
		{
		case 'E': bp.exec    = !bp.exec;    break;
		case 'R': bp.read    = !bp.read;    break;
		case 'W': bp.write   = !bp.write;   break;
		case 'e': bp.enabled = !bp.enabled; break;
		}
		dbg->SetBreakPoint(index, bp);
	}
	core->Unlock();
}

void BreakMonitor::DeleteSelected()
{
	if (cursor < 0)
		return;

	core->Lock();
	Debugger* dbg = core->GetDebugger();
	dbg->DeleteBreakPoint(cursor);
	int count = dbg->GetBreakPointCount();
	core->Unlock();

	cursor = Min(cursor, count - 1);
	Update();
}

// ---------------------------------------------------------------------------
//	ダイアログ処理
//
BOOL BreakMonitor::DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	POINT p;

	switch (msg)
	{
	case WM_COMMAND:
		switch (LOWORD(wp))
		{
		case IDM_BP_ADD:
			ShowEditDialog(-1);
			Update();
			break;

		case IDM_BP_EDIT:
			if (cursor >= 0)
				ShowEditDialog(cursor);
			Update();
			break;

		case IDM_BP_DELETE:
			DeleteSelected();
			break;

		case IDM_BP_DELETEALL:
			core->Lock();
			core->GetDebugger()->DeleteAllBreakPoints();
			core->Unlock();
			cursor = -1;
			Update();
			break;

		case IDM_BP_ENABLEALL:
			core->Lock();
			core->GetDebugger()->EnableAllBreakPoints(true);
			core->Unlock();
			Update();
			break;

		case IDM_BP_DISABLEALL:
			core->Lock();
			core->GetDebugger()->EnableAllBreakPoints(false);
			core->Unlock();
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
//	追加/編集ダイアログ(IDD_BP_EDIT を流用)
//
void BreakMonitor::ShowEditDialog(int index)
{
	editindex = index;
	// M88 は起動時に IME を切っている(ui.cpp:1092)ので、
	// 日本語を入れるダイアログでは戻してから開く。
	// WatchMonitor::ShowEditDialog と同じ
	(*EnableIME)(GetHWnd(), true);
	DialogBoxParam(GetHInst(), MAKEINTRESOURCE(IDD_BP_EDIT), GetHWnd(),
				   DLGPROC((void*)EDlgProcGate), (LPARAM)this);
	(*EnableIME)(GetHWnd(), false);
}

BOOL BreakMonitor::EDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	char buf[16];

	switch (msg)
	{
	case WM_INITDIALOG:
		{
			Debugger::BreakPoint bp;
			bool has = false;
			if (editindex >= 0)
			{
				core->Lock();
				has = core->GetDebugger()->GetBreakPoint(editindex, &bp);
				core->Unlock();
			}
			if (has)
			{
				wsprintf(buf, "%.4x", bp.addr);
				SetDlgItemText(hdlg, IDC_BP_ADDR, buf);
				wsprintf(buf, "%x", bp.length);
				SetDlgItemText(hdlg, IDC_BP_LEN, buf);
				CheckDlgButton(hdlg, IDC_BP_SUBCPU, bp.cpu == Debugger::cpu_sub ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hdlg, IDC_BP_EXEC, bp.exec ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hdlg, IDC_BP_READ, bp.read ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hdlg, IDC_BP_WRITE, bp.write ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hdlg, IDC_BP_ENABLED, bp.enabled ? BST_CHECKED : BST_UNCHECKED);
				SetDlgItemText(hdlg, IDC_BP_COMMENT, bp.comment);
			}
			else
			{
				SetDlgItemText(hdlg, IDC_BP_ADDR, "0000");
				SetDlgItemText(hdlg, IDC_BP_LEN, "1");
				CheckDlgButton(hdlg, IDC_BP_EXEC, BST_CHECKED);
				CheckDlgButton(hdlg, IDC_BP_ENABLED, BST_CHECKED);
				SetDlgItemText(hdlg, IDC_BP_COMMENT, "");
			}
			SetFocus(GetDlgItem(hdlg, IDC_BP_ADDR));
			SendDlgItemMessage(hdlg, IDC_BP_ADDR, EM_SETSEL, 0, -1);
		}
		return 0;

	case WM_COMMAND:
		if (HIWORD(wp) == BN_CLICKED)
		{
			switch (LOWORD(wp))
			{
				char* dum;
				char lenbuf[16];
			case IDOK:
				GetDlgItemText(hdlg, IDC_BP_ADDR, buf, 5);
				if (isxdigit((uint8) buf[0]))
				{
					Debugger::BreakPoint bp;
					bp.addr = strtoul(buf, &dum, 16) & 0xffff;
					bp.cpu = uint8(IsDlgButtonChecked(hdlg, IDC_BP_SUBCPU) == BST_CHECKED
								   ? Debugger::cpu_sub : Debugger::cpu_main);
					bp.exec = IsDlgButtonChecked(hdlg, IDC_BP_EXEC) == BST_CHECKED;
					bp.read = IsDlgButtonChecked(hdlg, IDC_BP_READ) == BST_CHECKED;
					bp.write = IsDlgButtonChecked(hdlg, IDC_BP_WRITE) == BST_CHECKED;
					bp.enabled = IsDlgButtonChecked(hdlg, IDC_BP_ENABLED) == BST_CHECKED;
					GetDlgItemText(hdlg, IDC_BP_COMMENT, bp.comment, sizeof(bp.comment));

					GetDlgItemText(hdlg, IDC_BP_LEN, lenbuf, sizeof(lenbuf));
					bp.length = isxdigit((uint8) lenbuf[0]) ? strtoul(lenbuf, &dum, 16) : 1;

					core->Lock();
					if (editindex >= 0)
						core->GetDebugger()->SetBreakPoint(editindex, bp);
					else
						editindex = core->GetDebugger()->AddBreakPoint(bp);
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

BOOL CALLBACK BreakMonitor::EDlgProcGate(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
	BreakMonitor* mon = 0;

	if (m == WM_INITDIALOG)
	{
		mon = reinterpret_cast<BreakMonitor*>(l);
		if (mon)
			::SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)mon);
	}
	else
	{
		mon = (BreakMonitor*)::GetWindowLongPtr(hwnd, GWLP_USERDATA);
	}

	if (mon)
		return mon->EDlgProc(hwnd, m, w, l);
	return FALSE;
}
