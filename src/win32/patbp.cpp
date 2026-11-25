// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	パターンのブレークポイントの追加・編集の窓
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#include "headers.h"
#include "resource.h"
#include "patbp.h"
#include "labeledit.h"
#include "patinput.h"
#include "watchbank.h"
#include "winexapi.h"

using namespace PC8801;

namespace
{
	// 書き込みで変わりうるバンク (ROM は除く)
	const MemoryViewer::Type patbanks[] =
	{
		MemoryViewer::mainram,
		MemoryViewer::eram0, MemoryViewer::eram1, MemoryViewer::eram2, MemoryViewer::eram3,
		MemoryViewer::gvram0, MemoryViewer::gvram1, MemoryViewer::gvram2,
		MemoryViewer::tvram,
	};
	const int npatbanks = sizeof(patbanks) / sizeof(patbanks[0]);
	const uint sublo = 0x4000, subhi = 0x7fff;		// サブの CPU の RAM

	struct EditState
	{
		WinCore* core;
		int index;
		Debugger::BreakPoint bp;
		PatternInput in;
	};

	void SetRange(HWND hdlg, uint lo, uint hi)
	{
		char buf[8];
		wsprintf(buf, "%.4X", lo);
		SetDlgItemText(hdlg, IDC_BP_ADDR, buf);
		wsprintf(buf, "%.4X", hi);
		SetDlgItemText(hdlg, IDC_PATBP_END, buf);
	}

	// 選んでいるバンクの番地の範囲
	void BankRange(HWND hdlg, uint* lo, uint* hi, const char** name)
	{
		if (IsDlgButtonChecked(hdlg, IDC_BP_SUBCPU) == BST_CHECKED)
		{
			*lo = sublo, *hi = subhi, *name = "Sub CPU RAM";
			return;
		}
		int sel = int(SendDlgItemMessage(hdlg, IDC_PATBP_BANK, CB_GETCURSEL, 0, 0));
		const WatchBank* b = FindWatchBank(patbanks[sel < 0 || sel >= npatbanks ? 0 : sel]);
		*lo = b->lo, *hi = b->hi, *name = b->name;
	}

	BOOL CALLBACK DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
	{
		EditState* st = (EditState*) GetWindowLongPtr(hdlg, GWLP_USERDATA);
		switch (msg)
		{
		case WM_INITDIALOG:
			{
				st = (EditState*) lp;
				SetWindowLongPtr(hdlg, GWLP_USERDATA, (LONG_PTR) st);
				const Debugger::BreakPoint& bp = st->bp;
				st->in.InitDialog(hdlg);
				int sel = 0;
				for (int i = 0; i < npatbanks; i++)
				{
					SendDlgItemMessage(hdlg, IDC_PATBP_BANK, CB_ADDSTRING, 0, (LPARAM) FindWatchBank(patbanks[i])->name);
					if (patbanks[i] == bp.patbank)
						sel = i;
				}
				SendDlgItemMessage(hdlg, IDC_PATBP_BANK, CB_SETCURSEL, sel, 0);
				bool sub = bp.cpu == Debugger::cpu_sub;
				CheckDlgButton(hdlg, IDC_BP_SUBCPU, sub ? BST_CHECKED : BST_UNCHECKED);
				EnableWindow(GetDlgItem(hdlg, IDC_PATBP_BANK), !sub);
				SetRange(hdlg, bp.addr, bp.addr + bp.length - 1);
				CheckDlgButton(hdlg, IDC_BP_ENABLED, bp.enabled ? BST_CHECKED : BST_UNCHECKED);
				SetDlgItemText(hdlg, IDC_BP_COMMENT, bp.comment);
				SetDlgItemText(hdlg, IDC_BP_COND, bp.cond);
				SetDlgItemInt(hdlg, IDC_BP_HITCOUNT, bp.hitcount, false);
			}
			return FALSE;

		case WM_COMMAND:
			{
				if (!st)
					break;
				int id = LOWORD(wp), code = HIWORD(wp);
				if (st->in.Command(hdlg, id, code))
					return TRUE;
				// バンク・CPU を変えたら、範囲をそのバンクいっぱいにする
				if ((id == IDC_PATBP_BANK && code == CBN_SELCHANGE) || (id == IDC_BP_SUBCPU && code == BN_CLICKED))
				{
					EnableWindow(GetDlgItem(hdlg, IDC_PATBP_BANK), IsDlgButtonChecked(hdlg, IDC_BP_SUBCPU) != BST_CHECKED);
					uint lo, hi;
					const char* name;
					BankRange(hdlg, &lo, &hi, &name);
					SetRange(hdlg, lo, hi);
					return TRUE;
				}
				if (id == IDC_BP_CONDHELP && code == BN_CLICKED)
				{
					MessageBox(hdlg, Debugger::CondHelp(), "Condition", MB_OK | MB_ICONINFORMATION);
					return TRUE;
				}
				if (id == IDOK && code == BN_CLICKED)
				{
					const char* title = "Pattern Breakpoint";
					if (!st->in.Read(hdlg, title))
						return TRUE;
					Debugger::BreakPoint bp = st->bp;
					// 16 進かラベルの名前 (M88gn)
					char buf[64];
					uint lo, hi;
					GetDlgItemText(hdlg, IDC_BP_ADDR, buf, sizeof(buf));
					if (!ParseAddrInput(buf, &lo))
						lo = 0x10000;
					GetDlgItemText(hdlg, IDC_PATBP_END, buf, sizeof(buf));
					if (!ParseAddrInput(buf, &hi))
						hi = 0x10000;
					uint blo, bhi;
					const char* bname;
					BankRange(hdlg, &blo, &bhi, &bname);
					if (lo > 0xffff || hi > 0xffff || lo > hi || lo < blo || hi > bhi)
					{
						char msg[160];
						wsprintf(msg, "範囲は %s の %.4X～%.4X の中で、始めと終わりを 16 進で入れてください。", bname, blo, bhi);
						MessageBox(hdlg, msg, title, MB_OK | MB_ICONWARNING);
						SetFocus(GetDlgItem(hdlg, IDC_BP_ADDR));
						return TRUE;
					}
					if (hi - lo + 1 < uint(st->in.len))
					{
						MessageBox(hdlg, "範囲がパターンより短くなっています。", title, MB_OK | MB_ICONWARNING);
						SetFocus(GetDlgItem(hdlg, IDC_PATBP_END));
						return TRUE;
					}
					GetDlgItemText(hdlg, IDC_BP_COND, bp.cond, sizeof(bp.cond));
					char err[128];
					if (!Debugger::CheckCondition(bp.cond, err))
					{
						MessageBox(hdlg, err, "Condition", MB_OK | MB_ICONWARNING);
						SetFocus(GetDlgItem(hdlg, IDC_BP_COND));
						return TRUE;
					}
					bp.pattern = true;
					bp.exec = bp.read = bp.write = bp.io = false;
					bp.addr = lo;
					bp.length = hi - lo + 1;
					bp.cpu = uint8(IsDlgButtonChecked(hdlg, IDC_BP_SUBCPU) == BST_CHECKED ? Debugger::cpu_sub : Debugger::cpu_main);
					int sel = int(SendDlgItemMessage(hdlg, IDC_PATBP_BANK, CB_GETCURSEL, 0, 0));
					bp.patbank = uint8(patbanks[sel < 0 || sel >= npatbanks ? 0 : sel]);
					bp.patlen = uint8(st->in.len);
					memcpy(bp.pat, st->in.pat, st->in.len);
					bp.patflags = uint8((st->in.hex ? Debugger::pat_hex : 0) | (st->in.CaseSensitive() ? 0 : Debugger::pat_nocase));
					bp.enabled = IsDlgButtonChecked(hdlg, IDC_BP_ENABLED) == BST_CHECKED;
					GetDlgItemText(hdlg, IDC_BP_COMMENT, bp.comment, sizeof(bp.comment));
					bp.hitcount = GetDlgItemInt(hdlg, IDC_BP_HITCOUNT, 0, false);
					bp.hits = 0;

					st->core->Lock();
					Debugger* dbg = st->core->GetDebugger();
					if (st->index >= 0)
						dbg->SetBreakPoint(st->index, bp);
					else
						st->index = dbg->AddBreakPoint(bp);
					st->core->Unlock();
					EndDialog(hdlg, IDOK);
					return TRUE;
				}
				if (id == IDCANCEL)
				{
					EndDialog(hdlg, IDCANCEL);
					return TRUE;
				}
			}
			break;

		case WM_CLOSE:
			EndDialog(hdlg, IDCANCEL);
			return TRUE;
		}
		return FALSE;
	}
}

bool PC8801::IsPatternBank(int bank)
{
	for (int i = 0; i < npatbanks; i++)
		if (patbanks[i] == bank)
			return true;
	return false;
}

int PC8801::EditPatternBreak(HWND owner, HINSTANCE hinst, WinCore* core, int index, const Debugger::BreakPoint* init)
{
	EditState st;
	st.core = core;
	st.index = index;
	bool has = false;
	if (index >= 0)
	{
		core->Lock();
		has = core->GetDebugger()->GetBreakPoint(index, &st.bp) && st.bp.pattern;
		core->Unlock();
		if (!has)
			return -1;
	}
	else if (init)
	{
		st.bp = *init;
		has = true;
	}
	if (!has)
	{
		st.bp.patbank = MemoryViewer::mainram;
		st.bp.addr = 0;
		st.bp.length = 0x10000;
		st.bp.enabled = true;
	}
	if (st.bp.length < 1)
		st.bp.length = 1;
	if (st.bp.patlen > 0)
		st.in.Set(st.bp.pat, st.bp.patlen, (st.bp.patflags & Debugger::pat_hex) != 0,
				  !(st.bp.patflags & Debugger::pat_nocase));

	(*EnableIME)(owner, true);		// コメントに日本語を入れられるように (BreakMonitor と同じ)
	INT_PTR r = DialogBoxParam(hinst, MAKEINTRESOURCE(IDD_PATBP_EDIT), owner, DLGPROC((void*) DlgProc), (LPARAM) &st);
	(*EnableIME)(owner, false);
	return r == IDOK ? st.index : -1;
}

void PC8801::DescribePatternBreak(const Debugger::BreakPoint& bp, char* buf)
{
	char what[Debugger::patmax * 3 + 8];
	char* w = what;
	if (bp.patflags & Debugger::pat_hex)
	{
		*w = 0;
		for (int i = 0; i < bp.patlen; i++)
			w += wsprintf(w, i ? " %.2X" : "%.2X", bp.pat[i]);
	}
	else
	{
		*w++ = '"';
		for (int i = 0; i < bp.patlen; i++)
			*w++ = (bp.pat[i] < 0x20 || bp.pat[i] == 0x7f) ? '.' : char(bp.pat[i]);
		*w++ = '"';
		*w = 0;
	}
	const char* bank = bp.cpu == Debugger::cpu_sub ? "SUB" : WatchBankName(bp.patbank);
	wsprintf(buf, "pattern %s%s in %s", what, (bp.patflags & Debugger::pat_nocase) ? " (any case)" : "", bank);
}
