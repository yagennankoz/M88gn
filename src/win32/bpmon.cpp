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
#include "ioports.h"
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
: core(0), iomode(false), cursor(-1), editindex(-1)
{
}

BreakMonitor::~BreakMonitor()
{
}

// ---------------------------------------------------------------------------
//	初期化
//
bool BreakMonitor::Init(WinCore* wincore, bool io)
{
	core = wincore;
	iomode = io;
	if (!WinMonitor::Init(MAKEINTRESOURCE(io ? IDD_IOBPMON : IDD_BPMON), io ? "MonitorIOBP" : "MonitorBP", io ? 80 : 70, 12))
		return false;
	SetUpdateTimer(500);
	return true;
}

// ---------------------------------------------------------------------------
//	テキスト更新。0行目は列見出し(毎フレーム書き直すので固定表示になる)
//
// 表示する行: この窓の種類 (I/O か、メモリ・実行か) のブレークポイントだけ (M88gn)。Lock の中で呼ぶ
void BreakMonitor::BuildView()
{
	Debugger* dbg = core->GetDebugger();
	view.clear();
	for (int i = 0; i < dbg->GetBreakPointCount(); i++)
	{
		Debugger::BreakPoint bp;
		if (dbg->GetBreakPoint(i, &bp) && bp.io == iomode)
			view.push_back(i);
	}
}

void BreakMonitor::UpdateText()
{
	core->Lock();
	Debugger* dbg = core->GetDebugger();
	BuildView();
	int count = int(view.size());

	SetLines(Max(1, count));
	int rows = Max(0, GetHeight() - 1);
	int top = GetLine();

	SetTxCol(0xc0c0c0);
	SetBkCol(RGB(0, 0, 0x50));
	// I/O の窓 (M88gn): Port・Count、R = IN、W = OUT、E の列は使わない。名前の列はポートの用途
	if (iomode)
		Putf("%3s  %-4s  %-4s  %-4s  %c %c %c  %-7s  %-9s  %s\n",
			 "#", "CPU", "Port", "Cnt", ' ', 'I', 'O', "Enabled", "Hits", "Name / Condition / Comment");
	else
		Putf("%3s  %-4s  %-4s  %-4s  %c %c %c  %-7s  %-9s  %s\n",
			 "#", "CPU", "Addr", "Len", 'E', 'R', 'W', "Enabled", "Hits", "Condition / Comment");

	for (int y = 0; y < rows; y++)
	{
		int index = top + y;
		Debugger::BreakPoint bp;
		if (index < count && dbg->GetBreakPoint(view[index], &bp))
		{
			SetBkCol(view[index] == cursor ? RGB(0, 0, 0x70) : RGB(0, 0, 0x20));
			SetTxCol(bp.enabled ? 0xffffff : 0x808080);
			// 回数 (M88gn): 条件が成り立った回数。止める回数を決めていれば「回数/止める回数」
			char hits[16];
			if (bp.hitcount > 1)
				wsprintf(hits, "%u/%u", bp.hits, bp.hitcount);
			else
				wsprintf(hits, "%u", bp.hits);
			// 条件とコメント
			char cc[160];
			wsprintf(cc, "%s%s%s%s", bp.cond[0] ? "if " : "", bp.cond,
					 (bp.cond[0] && bp.comment[0]) ? "  ; " : "", bp.comment);
			if (iomode)
			{
				char port[8], name[96];
				wsprintf(port, "  %.2x", bp.addr);
				const char* pn = IOPortName(bp.cpu == Debugger::cpu_sub, bp.addr);
				if (!pn[0])
					pn = "(not connected)";		// M88 の本体では何もつながっていないポート
				wsprintf(name, "%s%s%s", pn, (pn[0] && cc[0]) ? " / " : "", cc);
				Putf("%3d  %-4s  %-4s  %.4x  %c %c %c  %-7s  %-9s  %s\n",
					 index,
					 bp.cpu == Debugger::cpu_sub ? "SUB" : "MAIN",
					 port, bp.length, ' ',
					 bp.read  ? '*' : '-',
					 bp.write ? '*' : '-',
					 bp.enabled ? "on" : "off",
					 hits, name);
			}
			else
				Putf("%3d  %-4s  %.4x  %.4x  %c %c %c  %-7s  %-9s  %s\n",
					 index,
					 bp.cpu == Debugger::cpu_sub ? "SUB" : "MAIN",
					 bp.addr, bp.length,
					 bp.exec  ? '*' : '-',
					 bp.read  ? '*' : '-',
					 bp.write ? '*' : '-',
					 bp.enabled ? "on" : "off",
					 hits, cc);
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
	BuildView();
	int count = int(view.size());
	core->Unlock();

	int row = GetLine() + p.y - 1;
	if (row < 0 || row >= count)
		return;
	int index = view[row];

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
		case 'E': if (!bp.io) bp.exec = !bp.exec; break;	// I/O は実行を使わない
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
	core->Unlock();

	cursor = -1;
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

		// 全部の削除・有効・無効は、この窓の種類のものだけ (M88gn)
		case IDM_BP_DELETEALL:
			core->Lock();
			BuildView();
			for (int i = int(view.size()) - 1; i >= 0; i--)
				core->GetDebugger()->DeleteBreakPoint(view[i]);
			core->Unlock();
			cursor = -1;
			Update();
			break;

		case IDM_BP_ENABLEALL:
		case IDM_BP_DISABLEALL:
			core->Lock();
			BuildView();
			for (size_t i = 0; i < view.size(); i++)
			{
				Debugger::BreakPoint bp;
				if (core->GetDebugger()->GetBreakPoint(view[i], &bp))
				{
					bp.enabled = LOWORD(wp) == IDM_BP_ENABLEALL;
					core->GetDebugger()->SetBreakPoint(view[i], bp);
				}
			}
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
	DialogBoxParam(GetHInst(), MAKEINTRESOURCE(iomode ? IDD_IOBP_EDIT : IDD_BP_EDIT), GetHWnd(),
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
				SetDlgItemText(hdlg, IDC_BP_COND, bp.cond);
				SetDlgItemInt(hdlg, IDC_BP_HITCOUNT, bp.hitcount, false);
				if (iomode)
				{
					wsprintf(buf, "%.2x", bp.addr);
					SetDlgItemText(hdlg, IDC_BP_ADDR, buf);
				}
			}
			else
			{
				SetDlgItemText(hdlg, IDC_BP_ADDR, iomode ? "00" : "0000");
				SetDlgItemText(hdlg, IDC_BP_LEN, "1");
				SetDlgItemText(hdlg, IDC_BP_COND, "");
				SetDlgItemInt(hdlg, IDC_BP_HITCOUNT, 0, false);
				CheckDlgButton(hdlg, iomode ? IDC_BP_WRITE : IDC_BP_EXEC, BST_CHECKED);
				CheckDlgButton(hdlg, IDC_BP_ENABLED, BST_CHECKED);
				SetDlgItemText(hdlg, IDC_BP_COMMENT, "");
			}
			if (iomode)
			{
				SendDlgItemMessage(hdlg, IDC_BP_ADDR, EM_LIMITTEXT, 2, 0);	// ポートは 2 桁まで
				UpdatePortLabel(hdlg);
			}
			SetFocus(GetDlgItem(hdlg, IDC_BP_ADDR));
			SendDlgItemMessage(hdlg, IDC_BP_ADDR, EM_SETSEL, 0, -1);
		}
		return 0;

	case WM_COMMAND:
		// I/O (M88gn): ポートや CPU が変わったら説明を出し直す
		if (iomode && ((LOWORD(wp) == IDC_BP_ADDR && HIWORD(wp) == EN_CHANGE)
					   || (LOWORD(wp) == IDC_BP_SUBCPU && HIWORD(wp) == BN_CLICKED)))
		{
			UpdatePortLabel(hdlg);
			return 0;
		}
		if (HIWORD(wp) == BN_CLICKED)
		{
			switch (LOWORD(wp))
			{
				char* dum;
				char lenbuf[16];
			case IDC_BP_PORTSEL:
				ShowPortMenu(hdlg);
				return 0;

			case IDC_BP_CONDHELP:
				MessageBox(hdlg, Debugger::CondHelp(), "Condition", MB_OK | MB_ICONINFORMATION);
				return 0;

			case IDOK:
				GetDlgItemText(hdlg, IDC_BP_ADDR, buf, 5);
				if (isxdigit((uint8) buf[0]))
				{
					Debugger::BreakPoint bp;
					bp.addr = strtoul(buf, &dum, 16) & 0xffff;
					bp.io = iomode;
					bp.cpu = uint8(IsDlgButtonChecked(hdlg, IDC_BP_SUBCPU) == BST_CHECKED
								   ? Debugger::cpu_sub : Debugger::cpu_main);
					bp.exec = IsDlgButtonChecked(hdlg, IDC_BP_EXEC) == BST_CHECKED;
					bp.read = IsDlgButtonChecked(hdlg, IDC_BP_READ) == BST_CHECKED;
					bp.write = IsDlgButtonChecked(hdlg, IDC_BP_WRITE) == BST_CHECKED;
					bp.enabled = IsDlgButtonChecked(hdlg, IDC_BP_ENABLED) == BST_CHECKED;
					GetDlgItemText(hdlg, IDC_BP_COMMENT, bp.comment, sizeof(bp.comment));
					// 条件と回数 (M88gn)。式の書き方が違えば閉じない
					GetDlgItemText(hdlg, IDC_BP_COND, bp.cond, sizeof(bp.cond));
					bp.hitcount = GetDlgItemInt(hdlg, IDC_BP_HITCOUNT, 0, false);
					bp.hits = 0;
					{
						char err[128];
						if (!Debugger::CheckCondition(bp.cond, err))
						{
							MessageBox(hdlg, err, "Condition", MB_OK | MB_ICONWARNING);
							SetFocus(GetDlgItem(hdlg, IDC_BP_COND));
							return 0;
						}
					}

					GetDlgItemText(hdlg, IDC_BP_LEN, lenbuf, sizeof(lenbuf));
					bp.length = isxdigit((uint8) lenbuf[0]) ? strtoul(lenbuf, &dum, 16) : 1;
					if (bp.io)
					{
						bp.exec = false;
						if (bp.length < 1)
							bp.length = 1;
						if (bp.addr + bp.length > 0x100)
						{
							MessageBox(hdlg, "I/O ポートは 00～FF です。", "Set Breakpoint", MB_OK | MB_ICONWARNING);
							SetFocus(GetDlgItem(hdlg, IDC_BP_ADDR));
							return 0;
						}
					}

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

// ---------------------------------------------------------------------------
//	I/O: ポートを選ぶメニュー (分類ごと、説明付き)。選んだらポートの欄に入れ、
//	サブ CPU のポートなら Sub CPU にチェックする (M88gn)
//
void BreakMonitor::ShowPortMenu(HWND hdlg)
{
	HMENU hmenu = CreatePopupMenu();
	HMENU hsub = 0;
	const char* group = 0;
	for (int i = 0; i < nioports; i++)
	{
		const IOPortInfo& e = ioports[i];
		if (!group || lstrcmp(group, e.group))
		{
			group = e.group;
			hsub = CreatePopupMenu();
			AppendMenu(hmenu, MF_POPUP | MF_STRING, (UINT_PTR) hsub, group);
		}
		char text[160];
		// タブを使うとショートカットキーの欄として右に寄せられるので、空白でつなぐ
		wsprintf(text, "%.2X   %s", e.port, e.desc);
		AppendMenu(hsub, MF_STRING, 1 + i, text);
	}

	RECT r;
	GetWindowRect(GetDlgItem(hdlg, IDC_BP_PORTSEL), &r);
	int cmd = TrackPopupMenu(hmenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY,
							 r.left, r.bottom, 0, hdlg, 0);
	DestroyMenu(hmenu);			// 中のメニューもいっしょに消える
	if (cmd < 1 || cmd > nioports)
		return;

	const IOPortInfo& e = ioports[cmd - 1];
	char buf[8];
	wsprintf(buf, "%.2x", e.port);
	SetDlgItemText(hdlg, IDC_BP_ADDR, buf);
	CheckDlgButton(hdlg, IDC_BP_SUBCPU, e.sub ? BST_CHECKED : BST_UNCHECKED);
	UpdatePortLabel(hdlg);
}

// I/O: 入っているポートの説明を、追加の窓に出す
void BreakMonitor::UpdatePortLabel(HWND hdlg)
{
	char buf[8];
	char* dum;
	GetDlgItemText(hdlg, IDC_BP_ADDR, buf, 4);
	char text[200] = "";
	if (isxdigit((uint8) buf[0]))
	{
		uint port = strtoul(buf, &dum, 16);
		int sub = IsDlgButtonChecked(hdlg, IDC_BP_SUBCPU) == BST_CHECKED;
		const char* desc = 0;
		const char* other = 0;		// もう一方の CPU のポートの説明
		for (int i = 0; i < nioports; i++)
		{
			if (ioports[i].port != port)
				continue;
			if (ioports[i].sub == sub)
				desc = ioports[i].desc;
			else
				other = ioports[i].desc;
		}
		if (desc)
			lstrcpyn(text, desc, sizeof(text));
		else if (other)
			// CPU の選び間違いに気付けるように、もう一方の CPU での用途も出す
			wsprintf(text, "%s CPU のこのポートには何もつながっていません (%s CPU なら: %s)",
					 sub ? "サブ" : "メイン", sub ? "メイン" : "サブ", other);
		else
			wsprintf(text, "%s CPU のこのポートには何もつながっていません (M88 の本体では)", sub ? "サブ" : "メイン");
	}
	SetDlgItemText(hdlg, IDC_BP_ADDRLABEL, text);
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
