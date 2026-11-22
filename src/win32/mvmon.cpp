// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 2000.
// ---------------------------------------------------------------------------
//	$Id: mvmon.cpp,v 1.3 2001/02/21 11:58:55 cisc Exp $

#include "headers.h"
#include "resource.h"
#include "pc88/pc88.h"
#include "memmon.h"
#include "misc.h"
#include "device_i.h"
#include "pc88/memory.h"

using namespace PC8801;

// ---------------------------------------------------------------------------
//	構築/消滅
//
MemViewMonitor::MemViewMonitor()
{
}

MemViewMonitor::~MemViewMonitor()
{
}

// ---------------------------------------------------------------------------
//	初期化
//
bool MemViewMonitor::Init(LPCTSTR tmpl, PC88* pc88, const char* inisection,
						  int defcols, int defrows)
{
	if (!WinMonitor::Init(tmpl, inisection, defcols, defrows))
		return false;
	mv.Init(pc88);
	bus = mv.GetBus();
	mem1 = pc88->GetMem1();
	autobank = true;			// 既定は CPU に合わせる (M88gn)
	
	a0 = MemoryViewer::mainram;
	a6 = MemoryViewer::n88rom;
	ac = MemoryViewer::mainram;
	af = MemoryViewer::tvram;
	
	return true;
}

// ---------------------------------------------------------------------------
//	ダイアログ処理
//
BOOL MemViewMonitor::DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_INITDIALOG:
		AutoBank();
		SetBank();
		break;
		
	case WM_COMMAND:
		// 0000～7FFF・F000～FFFF のバンクを選んだら Auto を外す
		switch (LOWORD(wp))
		{
		case IDM_MEM_0_RAM: case IDM_MEM_0_N88: case IDM_MEM_0_N: case IDM_MEM_0_SUB:
		case IDM_MEM_0_ERAM0: case IDM_MEM_0_ERAM1: case IDM_MEM_0_ERAM2: case IDM_MEM_0_ERAM3:
		case IDM_MEM_6_N88: case IDM_MEM_6_E0: case IDM_MEM_6_E1: case IDM_MEM_6_E2: case IDM_MEM_6_E3:
		case IDM_MEM_F_RAM: case IDM_MEM_F_TVRAM:
		case IDM_MEM_0_N80: case IDM_MEM_0_N80SR: case IDM_MEM_6_N80SR: case IDM_MEM_6_N80SR1:
			autobank = false;
			break;
		}
		switch (LOWORD(wp))
		{
		case IDM_MEM_AUTO:
			autobank = !autobank;
			AutoBank();
			SetBank();
			break;

		case IDM_MEM_0_RAM:	a0 = MemoryViewer::mainram;	SetBank(); break;
		case IDM_MEM_0_N88:	a0 = MemoryViewer::n88rom;	SetBank(); break;
		case IDM_MEM_0_N:	a0 = MemoryViewer::nrom;	SetBank(); break;
		case IDM_MEM_0_N80:	a0 = MemoryViewer::n80rom;	SetBank(); break;	// M88gn
		case IDM_MEM_0_N80SR: a0 = MemoryViewer::n80srrom; a6 = MemoryViewer::n80srrom; SetBank(); break;
		case IDM_MEM_6_N80SR:  a0 = MemoryViewer::n80srrom; a6 = MemoryViewer::n80srrom; SetBank(); break;
		case IDM_MEM_6_N80SR1: a0 = MemoryViewer::n80srrom; a6 = MemoryViewer::n80sr1; SetBank(); break;
		case IDM_MEM_0_SUB: a0 = MemoryViewer::sub;     SetBank(); break;
		case IDM_MEM_0_ERAM0: a0 = MemoryViewer::eram0;     SetBank(); break;
		case IDM_MEM_0_ERAM1: a0 = MemoryViewer::eram1;     SetBank(); break;
		case IDM_MEM_0_ERAM2: a0 = MemoryViewer::eram2;     SetBank(); break;
		case IDM_MEM_0_ERAM3: a0 = MemoryViewer::eram3;     SetBank(); break;
		case IDM_MEM_6_N88:	a6 = MemoryViewer::n88rom;	SetBank(); break;
		case IDM_MEM_6_E0:	a6 = MemoryViewer::n88e0;	SetBank(); break;
		case IDM_MEM_6_E1:	a6 = MemoryViewer::n88e1;	SetBank(); break;
		case IDM_MEM_6_E2:	a6 = MemoryViewer::n88e2;	SetBank(); break;
		case IDM_MEM_6_E3:	a6 = MemoryViewer::n88e3;	SetBank(); break;
		case IDM_MEM_F_RAM:	af = MemoryViewer::mainram;	SetBank(); break;
		case IDM_MEM_F_TVRAM:af= MemoryViewer::tvram;	SetBank(); break;
		case IDM_MEM_C_RAM:	ac = MemoryViewer::mainram;	SetBank(); break;
		case IDM_MEM_C_GV0:	ac = MemoryViewer::gvram0;	SetBank(); break;
		case IDM_MEM_C_GV1:	ac = MemoryViewer::gvram1;	SetBank(); break;
		case IDM_MEM_C_GV2:	ac = MemoryViewer::gvram2;	SetBank(); break;

		case IDM_MEM_STATCLEAR: mv.StatClear(); break;

		case IDM_MEM_GOTO:
			GotoAddress();
			break;
	
		case IDM_CLOSE:
			PostMessage(hdlg, WM_CLOSE, 0, 0);
			break;
		}
		break;

	case WM_KEYDOWN:
		if (wp == 'G' && (GetKeyState(VK_CONTROL) & 0x8000))
		{
			GotoAddress();
			return 0;
		}
		break;

	case WM_INITMENU:
		{
			HMENU hmenu = (HMENU) wp;
			CheckMenuItem(hmenu, IDM_MEM_AUTO, autobank ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_0_RAM, (a0 == MemoryViewer::mainram) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_0_N88, (a0 == MemoryViewer::n88rom ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_0_N,   (a0 == MemoryViewer::nrom   ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_0_N80,   (a0 == MemoryViewer::n80rom  ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_0_N80SR, (a0 == MemoryViewer::n80srrom) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_6_N80SR,  (GetShown6() == MemoryViewer::n80srrom) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_6_N80SR1, (GetShown6() == MemoryViewer::n80sr1  ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_0_ERAM0, (a0 == MemoryViewer::eram0 ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_0_ERAM1, (a0 == MemoryViewer::eram1 ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_0_ERAM2, (a0 == MemoryViewer::eram2 ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_0_ERAM3, (a0 == MemoryViewer::eram3 ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_0_SUB, (a0 == MemoryViewer::sub    ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_6_N88, (a6 == MemoryViewer::n88rom ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_6_E0,  (a6 == MemoryViewer::n88e0  ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_6_E1,  (a6 == MemoryViewer::n88e1  ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_6_E2,  (a6 == MemoryViewer::n88e2  ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_6_E3,  (a6 == MemoryViewer::n88e3  ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_F_RAM, (af == MemoryViewer::mainram) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_F_TVRAM,(af== MemoryViewer::tvram  ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_C_RAM, (ac == MemoryViewer::mainram) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_C_GV0, (ac == MemoryViewer::gvram0 ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_C_GV1, (ac == MemoryViewer::gvram1 ) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_C_GV2, (ac == MemoryViewer::gvram2 ) ? MF_CHECKED : MF_UNCHECKED);
			int x = a0 != MemoryViewer::n88rom ? MF_GRAYED : MF_ENABLED;
			EnableMenuItem(hmenu, IDM_MEM_6_N88, x);
			EnableMenuItem(hmenu, IDM_MEM_6_E0,  x);
			EnableMenuItem(hmenu, IDM_MEM_6_E1,  x);
			EnableMenuItem(hmenu, IDM_MEM_6_E2,  x);
			EnableMenuItem(hmenu, IDM_MEM_6_E3,  x);
			// GVRAM 選択中は $c000-$ffff 全部を占有するので &f000 は無効化する
			int fx = ac != MemoryViewer::mainram ? MF_GRAYED : MF_ENABLED;
			EnableMenuItem(hmenu, IDM_MEM_F_RAM,   fx);
			EnableMenuItem(hmenu, IDM_MEM_F_TVRAM, fx);
			break;
		}
	}
	return WinMonitor::DlgProc(hdlg, msg, wp, lp);
}

// ---------------------------------------------------------------------------
//	Auto: メイン CPU がいま見ているバンクに合わせる (M88gn)
//	Memory::GetRdBank が返す番号は MemoryViewer::Type と同じ並び。
//	窓で表示できないバンク (CD-ROM の BIOS・辞書・N88 の拡張 ROM の E1～ など) は RAM にする。
//	C000～EFFF の GVRAM は、ALU のモードでは読むと値が変わることがあるので合わせない (メニューで選ぶ)。
//	サブ CPU を見ているときは何もしない
//
bool MemViewMonitor::AutoBank()
{
	if (!autobank || !mem1 || a0 == MemoryViewer::sub)
		return false;

	MemoryViewer::Type n0, n6 = a6, nf = af;
	uint b = mem1->GetRdBank(0x0000);
	if (b == Memory::mRAM)
		n0 = MemoryViewer::mainram;
	else if (b == Memory::mN88)
		n0 = MemoryViewer::n88rom;
	else if (b == Memory::mN)
		n0 = MemoryViewer::nrom;
	else if (b == Memory::mN80)						// N80 モード (M88gn)
		n0 = MemoryViewer::n80rom;
	else if (b == Memory::mN80SR)
		n0 = MemoryViewer::n80srrom;
	else if (b >= Memory::mERAM && b < Memory::mERAM + 4)
		n0 = MemoryViewer::Type(MemoryViewer::eram0 + (b - Memory::mERAM));
	else
		n0 = MemoryViewer::mainram;

	if (n0 == MemoryViewer::n88rom)
	{
		uint b6 = mem1->GetRdBank(0x6000);
		if (b6 >= Memory::mN88E0 && b6 <= Memory::mN88E3)
			n6 = MemoryViewer::Type(MemoryViewer::n88e0 + (b6 - Memory::mN88E0));
		else
			n6 = MemoryViewer::n88rom;
	}
	else if (n0 == MemoryViewer::n80srrom)
		n6 = mem1->GetRdBank(0x6000) == Memory::mN80SR1 ? MemoryViewer::n80sr1 : MemoryViewer::n80srrom;

	if (ac == MemoryViewer::mainram)
		nf = mem1->GetRdBank(0xf000) == Memory::mTV ? MemoryViewer::tvram : MemoryViewer::mainram;

	if (n0 == a0 && n6 == a6 && nf == af)
		return false;
	a0 = n0, a6 = n6, af = nf;
	mv.SelectBank(a0, GetShown6(), MemoryViewer::mainram, ac, af);
	return true;
}

// 6000-7FFF に実際に出しているバンク (M88gn)。a6 は 0000 が N88 のとき (N88 / E0-E3) と
// N80SR のとき (N80SR / その 8000-9FFF) だけ効き、それ以外は 0000 と同じ
MemoryViewer::Type MemViewMonitor::GetShown6()
{
	if (a0 == MemoryViewer::n88rom)
		return (a6 == MemoryViewer::n88rom || (a6 >= MemoryViewer::n88e0 && a6 <= MemoryViewer::n88e3)) ? a6 : MemoryViewer::n88rom;
	if (a0 == MemoryViewer::n80srrom)
		return a6 == MemoryViewer::n80sr1 ? MemoryViewer::n80sr1 : MemoryViewer::n80srrom;
	return a0;
}

// ---------------------------------------------------------------------------
//	バンク変更
//
void MemViewMonitor::SetBank()
{
	mv.SelectBank(a0, GetShown6(), MemoryViewer::mainram, ac, af);
	Update();
}


uint MemViewMonitor::StatExec(uint a)
{
	int ex = mv.StatExec(a);
	if (!ex)
		return 0;
	return ex < 0x4 ? (ex + 4) * 0x10 : Min(ex+0x80-0x04, 0xc0);
}

void MemViewMonitor::StatClear()
{
//	mv.StatClear();
	uint* st = mv.StatExecBuf();
	if (st)
	{
		for (int i=0; i<0x10000; i++)
		{
			*st = (*st+1) / 2;
			st++;
		}
	}
}

// ---------------------------------------------------------------------------
//	任意アドレスへ飛ぶ
//
void MemViewMonitor::GotoAddress()
{
	DialogBoxParam(GetHInst(), MAKEINTRESOURCE(IDD_ADDR_GOTO),
				   GetHWnd(), DLGPROC((void*)GotoDlgProcGate), (LPARAM)this);
}

BOOL MemViewMonitor::GotoDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	char buf[16];
	switch (msg)
	{
	case WM_INITDIALOG:
		SetFocus(GetDlgItem(hdlg, IDC_ADDR_GOTO_EDIT));
		return 0;

	case WM_COMMAND:
		if (HIWORD(wp) == BN_CLICKED)
		{
			switch (LOWORD(wp))
			{
				char* dum;
			case IDOK:
				GetDlgItemText(hdlg, IDC_ADDR_GOTO_EDIT, buf, sizeof(buf));
				if (isxdigit((unsigned char) buf[0]))
				{
					uint addr = strtoul(buf, &dum, 16) & 0xffff;
					JumpTo(addr);
					Update();
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

BOOL CALLBACK MemViewMonitor::GotoDlgProcGate
(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
	MemViewMonitor* mon = 0;

	if ( m == WM_INITDIALOG ) {
		mon = reinterpret_cast<MemViewMonitor*>(l);
		if (mon) {
			::SetWindowLongPtr( hwnd, GWLP_USERDATA, (LONG_PTR)mon );
		}
	} else {
		mon = (MemViewMonitor*)::GetWindowLongPtr( hwnd, GWLP_USERDATA );
	}

	if (mon) {
		return mon->GotoDlgProc(hwnd, m, w, l);
	} else {
		return FALSE;
	}
}
