// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ラベルの一覧の窓
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	ラベルは Debugger が持つ (コード・メモリの窓の右クリックで付ける)。この窓は 0.5 秒ごとに読み直す。
//	クリックで選ぶ、ダブルクリックで名前を変える、Delete で消す、右クリックでコード・メモリの窓へ
// ---------------------------------------------------------------------------

#include "headers.h"
#include "resource.h"
#include "lblmon.h"
#include "codemon.h"
#include "memmon.h"
#include "labeledit.h"
#include "misc.h"
#include "88config.h"
#include <algorithm>

using namespace PC8801;

// 並べ方 (番地順はメイン → サブ、番地の順。名前順は大文字・小文字を区別しない)
static int BankOrder(int bank) { return bank == Debugger::label_anybank ? -1 : bank; }
struct LabelLessAddr
{
	template <class T> bool operator()(const T& a, const T& b) const
	{
		if (a.cpu != b.cpu)
			return a.cpu < b.cpu;
		if (a.addr != b.addr)
			return a.addr < b.addr;
		return BankOrder(a.bank) < BankOrder(b.bank);
	}
};
struct LabelLessName
{
	template <class T> bool operator()(const T& a, const T& b) const
	{
		int c = lstrcmpi(a.name.c_str(), b.name.c_str());
		if (c)
			return c < 0;
		return LabelLessAddr()(a, b);
	}
};

// ---------------------------------------------------------------------------
//	構築/消滅
//
LabelMonitor::LabelMonitor()
: core(0), codemon(0), memmon(0), byname(false), selkey(-1)
{
}

LabelMonitor::~LabelMonitor()
{
}

bool LabelMonitor::Init(WinCore* _core)
{
	core = _core;
	if (!WinMonitor::Init(MAKEINTRESOURCE(IDD_LABELMON), "MonitorLabel", 48, 20))
		return false;
	SetUpdateTimer(500);		// ほかの窓で付けた・消したラベルを拾う
	return true;
}

void LabelMonitor::Start()
{
	SetUpdateTimer(500);
	EnableStatus(true);
	PutStatus("Insert: 足す  ダブルクリック: 名前を変える  Delete: 消す  右クリック: コード・メモリの窓へ");
}

void LabelMonitor::LoadMonitorConfig()
{
	WinMonitor::LoadMonitorConfig();
	byname = GetPrivateProfileInt("MonitorLabel", "SortByName", 0, PC8801::GetM88IniPath()) != 0;
}

void LabelMonitor::SaveMonitorConfig()
{
	WinMonitor::SaveMonitorConfig();
	WritePrivateProfileString("MonitorLabel", "SortByName", byname ? "1" : "0", PC8801::GetM88IniPath());
}

// ---------------------------------------------------------------------------
//	一覧
//
void LabelMonitor::BuildList()
{
	Debugger* dbg = core->GetDebugger();
	entries.clear();
	int n = dbg->GetLabelCount();
	for (int i = 0; i < n; i++)
	{
		Entry e;
		const char* name;
		if (dbg->GetLabelAt(i, &e.cpu, &e.addr, &e.bank, &name))
		{
			e.name = name;
			entries.push_back(e);
		}
	}
	if (byname)
		std::sort(entries.begin(), entries.end(), LabelLessName());
	else
		std::sort(entries.begin(), entries.end(), LabelLessAddr());
}

int LabelMonitor::SelectedIndex()
{
	for (size_t i = 0; i < entries.size(); i++)
		if (entries[i].Key() == selkey)
			return int(i);
	return -1;
}

int LabelMonitor::RowAt(POINT p)
{
	if (p.y < 1)
		return -1;				// 0 行目は見出し
	int i = GetLine() + p.y - 1;
	return i < int(entries.size()) ? i : -1;
}

void LabelMonitor::UpdateText()
{
	BuildList();
	SetLines(Max(1, int(entries.size())));
	int rows = Max(0, GetHeight() - 1);
	int top = GetLine();

	SetTxCol(0xc0c0c0);
	SetBkCol(RGB(0, 0, 0x50));
	Putf("%-4s  %-4s  %-6s  %s   (%d, sorted by %s)\n", "CPU", "Addr", "Bank", "Name", int(entries.size()),
		 byname ? "name" : "address");

	for (int y = 0; y < rows; y++)
	{
		int i = top + y;
		if (i >= int(entries.size()))
		{
			SetBkCol(RGB(0, 0, 0x20));
			Puts("\n");
			continue;
		}
		const Entry& e = entries[i];
		bool sel = e.Key() == selkey;
		SetBkCol(sel ? RGB(0, 0, 0x70) : RGB(0, 0, 0x20));
		SetTxCol(0xffffff);
		Putf("%-4s  %.4X  ", e.cpu ? "SUB" : "MAIN", e.addr);
		SetTxCol(e.bank == Debugger::label_anybank || e.cpu ? 0x909090 : 0xa0ffa0);
		Putf("%-6s  ", e.cpu ? "-" : LabelBankName(e.bank));
		SetTxCol(RGB(0xff, 0xe0, 0x60));
		Putf("%s\n", e.name.c_str());
	}
}

// Ctrl+C: 書き出しと同じ形で全部
bool LabelMonitor::CopyToClipboard()
{
	BuildList();
	TextBuffer tb;
	for (size_t i = 0; i < entries.size(); i++)
	{
		const Entry& e = entries[i];
		if (e.cpu)
			tb.Addf("S:%.4X %s\r\n", e.addr, e.name.c_str());
		else if (e.bank != Debugger::label_anybank)
			tb.Addf("%s:%.4X %s\r\n", LabelBankName(e.bank), e.addr, e.name.c_str());
		else
			tb.Addf("%.4X %s\r\n", e.addr, e.name.c_str());
	}
	return SetClipboardText(tb.Get(), tb.Length());
}

// ---------------------------------------------------------------------------
//	操作
//
void LabelMonitor::Changed()
{
	Update();
	if (codemon)
		codemon->Update();
	if (memmon)
		memmon->Update();
}

void LabelMonitor::EditSelected()
{
	int i = SelectedIndex();
	if (i < 0)
		return;
	Entry e = entries[i];
	if (EditLabel(GetHWnd(), GetHInst(), core->GetDebugger(), e.cpu, e.addr, e.bank))
		Changed();
}

// 新しく足す (M88gn)。足したラベルを選ぶ
void LabelMonitor::AddNew()
{
	int cpu, bank;
	uint addr;
	if (!AddLabel(GetHWnd(), GetHInst(), core->GetDebugger(), &cpu, &addr, &bank))
		return;
	Entry e;
	e.cpu = cpu, e.addr = addr, e.bank = cpu ? int(Debugger::label_anybank) : bank;
	selkey = e.Key();
	Changed();
	// 足したラベルが見えるように
	BuildList();
	int i = SelectedIndex();
	if (i >= 0 && (i < GetLine() || i >= GetLine() + Max(1, GetHeight() - 1)))
	{
		SetLine(Max(0, i - (GetHeight() - 1) / 2));
		Update();
	}
}

void LabelMonitor::RemoveSelected()
{
	int i = SelectedIndex();
	if (i < 0)
		return;
	core->GetDebugger()->SetLabel(entries[i].cpu, entries[i].addr, entries[i].bank, 0);
	// 選ぶのは次の行 (無ければ前の行)
	if (i + 1 < int(entries.size()))
		selkey = entries[i + 1].Key();
	else if (i > 0)
		selkey = entries[i - 1].Key();
	else
		selkey = -1;
	Changed();
}

void LabelMonitor::ShowInCode(const Entry& e)
{
	if (!codemon)
		return;
	HWND hdlg = GetHWnd();
	codemon->ShowAt(e.addr, e.cpu != 0, e.bank, GetHInst(), GetWindow(hdlg, GW_OWNER) ? GetWindow(hdlg, GW_OWNER) : GetAncestor(hdlg, GA_ROOT));	// ラベルのバンクで (Any なら今のバンク)
}

void LabelMonitor::ShowInMemory(const Entry& e)
{
	if (!memmon)
		return;
	HWND hdlg = GetHWnd();
	memmon->ShowAddressIn(e.addr, e.cpu != 0, e.bank, 1, GetHInst(), GetWindow(hdlg, GW_OWNER) ? GetWindow(hdlg, GW_OWNER) : GetAncestor(hdlg, GA_ROOT));
}

void LabelMonitor::ShowRowMenu(HWND hdlg, int index)
{
	Entry e = entries[index];
	enum { cmd_code = 1, cmd_mem, cmd_edit, cmd_remove };
	HMENU hmenu = CreatePopupMenu();
	char buf[64];
	wsprintf(buf, "Show %.4XH in &Code", e.addr);
	AppendMenu(hmenu, MF_STRING | (codemon ? 0 : MF_GRAYED), cmd_code, buf);
	wsprintf(buf, "Show %.4XH in M&emory", e.addr);
	AppendMenu(hmenu, MF_STRING | (memmon ? 0 : MF_GRAYED), cmd_mem, buf);
	AppendMenu(hmenu, MF_SEPARATOR, 0, 0);
	AppendMenu(hmenu, MF_STRING, cmd_edit, "&Edit Label...");
	AppendMenu(hmenu, MF_STRING, cmd_remove, "&Remove Label");

	POINT sp;
	GetCursorPos(&sp);
	int cmd = TrackPopupMenu(hmenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, sp.x, sp.y, 0, hdlg, 0);
	DestroyMenu(hmenu);

	switch (cmd)
	{
	case cmd_code:		ShowInCode(e); break;
	case cmd_mem:		ShowInMemory(e); break;
	case cmd_edit:		EditSelected(); break;
	case cmd_remove:	RemoveSelected(); break;
	}
}

// ---------------------------------------------------------------------------
//	ダイアログ処理
//
BOOL LabelMonitor::DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	POINT p;
	switch (msg)
	{
	case WM_COMMAND:
		switch (LOWORD(wp))
		{
		case IDM_LBL_ADD:
			AddNew();
			break;

		case IDM_LBL_EDIT:
			EditSelected();
			break;

		case IDM_LBL_REMOVE:
			RemoveSelected();
			break;

		case IDM_LBL_DELETEALL:
			if (core->GetDebugger()->GetLabelCount() &&
				MessageBox(hdlg, "ラベルを全部消しますか?", "Labels", MB_OKCANCEL | MB_ICONQUESTION) == IDOK)
			{
				core->GetDebugger()->DeleteAllLabels();
				selkey = -1;
				Changed();
			}
			break;

		case IDM_LBL_SORTADDR:
		case IDM_LBL_SORTNAME:
			byname = LOWORD(wp) == IDM_LBL_SORTNAME;
			SaveMonitorConfig();
			Update();
			break;

		case IDM_LABEL_EXPORT:
			{
				int n = ExportLabels(hdlg, core->GetDebugger());
				if (n >= 0)
				{
					EnableStatus(true);
					PutStatus("ラベルを %d 個書き出しました", n);
				}
			}
			break;

		case IDM_LABEL_IMPORT:
			{
				int skipped;
				int n = ImportLabels(hdlg, core->GetDebugger(), &skipped);
				if (n >= 0)
				{
					EnableStatus(true);
					PutStatus("ラベルを %d 個読み込みました (読めない行 %d)", n, skipped);
					Changed();
				}
			}
			break;

		case IDM_CLOSE:
			PostMessage(hdlg, WM_CLOSE, 0, 0);
			break;
		}
		break;

	case WM_INITMENU:
		{
			HMENU hm = (HMENU) wp;
			CheckMenuItem(hm, IDM_LBL_SORTADDR, byname ? MF_UNCHECKED : MF_CHECKED);
			CheckMenuItem(hm, IDM_LBL_SORTNAME, byname ? MF_CHECKED : MF_UNCHECKED);
			bool sel = SelectedIndex() >= 0;
			EnableMenuItem(hm, IDM_LBL_EDIT, MF_BYCOMMAND | (sel ? MF_ENABLED : MF_GRAYED));
			EnableMenuItem(hm, IDM_LBL_REMOVE, MF_BYCOMMAND | (sel ? MF_ENABLED : MF_GRAYED));
		}
		break;

	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
	case WM_RBUTTONUP:
		p.x = (short) LOWORD(lp);
		p.y = (short) HIWORD(lp);
		if (GetTextPos(&p))
		{
			int i = RowAt(p);
			if (i >= 0)
			{
				selkey = entries[i].Key();
				Update();
				if (msg == WM_LBUTTONDBLCLK)
					EditSelected();
				else if (msg == WM_RBUTTONUP)
					ShowRowMenu(hdlg, i);
			}
		}
		if (msg == WM_RBUTTONUP)
			return TRUE;
		break;

	case WM_KEYDOWN:
		if (wp == VK_DELETE)
		{
			RemoveSelected();
			return 0;
		}
		if (wp == VK_RETURN)
		{
			EditSelected();
			return 0;
		}
		if (wp == VK_INSERT)
		{
			AddNew();
			return 0;
		}
		break;
	}
	return WinMonitor::DlgProc(hdlg, msg, wp, lp);
}
