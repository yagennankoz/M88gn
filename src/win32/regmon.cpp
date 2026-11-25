// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1999.
// ---------------------------------------------------------------------------
//	$Id: regmon.cpp,v 1.1 2000/11/02 12:43:51 cisc Exp $

#include "headers.h"
#include "resource.h"
#include "regmon.h"
#include "misc.h"
#include "pc88/pc88.h"
#include "wincore.h"
#include "codemon.h"
#include "memmon.h"

using namespace PC8801;

// ---------------------------------------------------------------------------
//	表示上の行/列とレジスタの対応表
//	(メインCPU側の座標。サブCPU側は列に subcpuoffset を足したもの)
//
const Z80RegMonitor::RegField Z80RegMonitor::fields[reg_max] =
{
//	 row  col  width  valcol  vallen  name
	{  0,   0,   7,      3,      4,   "PC"  },	// reg_pc
	{  0,   8,   7,     11,      4,   "SP"  },	// reg_sp
	{  1,   0,   7,      3,      4,   "AF"  },	// reg_af
	{  1,   8,   7,     11,      4,   "BC"  },	// reg_bc
	{  2,   0,   7,      3,      4,   "HL"  },	// reg_hl
	{  2,   8,   7,     11,      4,   "DE"  },	// reg_de
	{  3,   0,   7,      3,      4,   "IX"  },	// reg_ix
	{  3,   8,   7,     11,      4,   "IY"  },	// reg_iy
	{  4,   0,   7,      3,      4,   "AF'" },	// reg_af2
	{  4,   8,   7,     11,      4,   "BC'" },	// reg_bc2
	{  5,   0,   7,      3,      4,   "HL'" },	// reg_hl2
	{  5,   8,   7,     11,      4,   "DE'" },	// reg_de2
	{  6,   0,   5,      3,      2,   "I"   },	// reg_i
	{  6,   8,   7,     14,      1,   "IM"  },	// reg_im
};

// ---------------------------------------------------------------------------
//	構築/消滅
//
Z80RegMonitor::Z80RegMonitor()
: core(0), codemon(0), memmon(0), editcpu(0), editid(0)
{
}

Z80RegMonitor::~Z80RegMonitor()
{
}

bool Z80RegMonitor::Init(WinCore* _core)
{
	if (!WinMonitor::Init(MAKEINTRESOURCE(IDD_REGMON), "MonitorRegister", 34, 8))
		return false;

	core = _core;
	SetUpdateTimer(50);

	return true;
}

void Z80RegMonitor::DrawMain(HDC hdc, bool)
{
	RECT rect;
	GetClientRect(GetHWnd(), &rect);

	HBRUSH hbr = CreateSolidBrush(0x113300);
	hbr = (HBRUSH) SelectObject(hdc, hbr);
	PatBlt(hdc, rect.left, rect.top, rect.right, rect.bottom, PATCOPY);
	DeleteObject(SelectObject(hdc, hbr));

	SetTextColor(hdc, 0xffffff);
	WinMonitor::DrawMain(hdc, true);
}

// ---------------------------------------------------------------------------
//	クリック位置からレジスタを特定する
//
bool Z80RegMonitor::FindField(POINT p, int* cpu, int* id)
{
	for (int c = 0; c < 2; c++)
	{
		int coloffset = c * subcpuoffset;
		for (int i = 0; i < reg_max; i++)
		{
			const RegField& f = fields[i];
			if (p.y == f.row &&
				p.x >= f.col + coloffset && p.x < f.col + coloffset + f.width)
			{
				*cpu = c;
				*id = i;
				return true;
			}
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
//	レジスタの値を取得/設定する
//	PC は Z80C 内部でポインタとして持たれているため GetPC/SetPC 経由、
//	それ以外は GetRegRef() で直接触る
//
uint Z80RegMonitor::GetRegValue(int cpu, int id)
{
	PC88::Z80* z = cpu ? core->GetCPU2() : core->GetCPU1();
	if (id == reg_pc)
		return z->GetPC();

	Z80Reg& r = z->GetRegRef();
	switch (id)
	{
	case reg_sp:	return r.r.w.sp;
	// F は Z80C の遅延評価により未確定のことがあるため、GetAFSync() で
	// 確定させてから返す(仕様書 §5.1/§5.2。10b で確認・修正)
	case reg_af:	return z->GetAFSync();
	case reg_bc:	return r.r.w.bc;
	case reg_hl:	return r.r.w.hl;
	case reg_de:	return r.r.w.de;
	case reg_ix:	return r.r.w.ix;
	case reg_iy:	return r.r.w.iy;
	case reg_af2:	return r.r_af;
	case reg_bc2:	return r.r_bc;
	case reg_hl2:	return r.r_hl;
	case reg_de2:	return r.r_de;
	case reg_i:		return r.ireg;
	case reg_im:	return r.intmode;
	}
	return 0;
}

void Z80RegMonitor::SetRegValue(int cpu, int id, uint value)
{
	PC88::Z80* z = cpu ? core->GetCPU2() : core->GetCPU1();
	if (id == reg_pc)
	{
		z->SetPC(value);
		return;
	}

	Z80Reg& r = z->GetRegRef();
	switch (id)
	{
	case reg_sp:	r.r.w.sp = value; break;
	case reg_af:	z->SetAFSync(value); break;		// Z80_x86 はメモリの上の AF の並びが違う (M88gn)
	case reg_bc:	r.r.w.bc = value; break;
	case reg_hl:	r.r.w.hl = value; break;
	case reg_de:	r.r.w.de = value; break;
	case reg_ix:	r.r.w.ix = value; break;
	case reg_iy:	r.r.w.iy = value; break;
	case reg_af2:	r.r_af = value; break;
	case reg_bc2:	r.r_bc = value; break;
	case reg_hl2:	r.r_hl = value; break;
	case reg_de2:	r.r_de = value; break;
	case reg_i:		r.ireg = (uint8) value; break;
	case reg_im:	r.intmode = (uint8) value; break;
	}
}

// ---------------------------------------------------------------------------
//	ダイアログ処理
//
BOOL Z80RegMonitor::DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_LBUTTONDBLCLK:
		{
			if (!core->IsPaused())
			{
				EnableStatus(true);
				PutStatus("ポーズ中のみ編集できます");
				break;
			}

			POINT p;
			p.x = LOWORD(lp);
			p.y = HIWORD(lp);
			if (GetTextPos(&p) && FindField(p, &editcpu, &editid))
			{
				DialogBoxParam(GetHInst(), MAKEINTRESOURCE(IDD_REG_EDIT),
							hdlg, DLGPROC((void*)EDlgProcGate), (LPARAM)this);
				Update();
			}
		}
		return 0;

	case WM_RBUTTONUP:
		// 右クリック (M88gn): 番地のレジスターなら、メモリの窓・コードの窓のその番地へ
		{
			POINT p;
			p.x = (short) LOWORD(lp);
			p.y = (short) HIWORD(lp);
			if (GetTextPos(&p))
				ShowFieldMenu(hdlg, p);
		}
		return TRUE;
	}
	return WinMonitor::DlgProc(hdlg, msg, wp, lp);
}

// ---------------------------------------------------------------------------
//	右クリックのメニュー (M88gn)。16 ビットのレジスター (AF・AF' を除く) の値を番地として、
//	メモリの窓・コードの窓で見せる。サブ CPU の欄ならサブの CPU のメモリ
//
void Z80RegMonitor::ShowFieldMenu(HWND hdlg, POINT p)
{
	int cpu, id;
	if (!FindField(p, &cpu, &id))
		return;
	const RegField& f = fields[id];
	if (f.vallen != 4 || id == reg_af || id == reg_af2)
		return;
	uint addr = GetRegValue(cpu, id) & 0xffff;

	enum { cmd_mem = 1, cmd_code };
	HMENU hmenu = CreatePopupMenu();
	// 番地にラベルがあれば「名前(1234H)」(M88gn。コード・メモリの窓のメニューと同じ)。
	// バンクは CPU がその番地で今見ているもの (サブ CPU はバンクが無い)
	int bank = cpu ? -1 : int(core->GetMem1()->GetRdBank(addr));
	const char* label = core->GetDebugger()->GetLabel(cpu, addr, bank);
	char at[64];
	if (label)
		wsprintf(at, "%s(%.4XH)", label, addr);
	else
		wsprintf(at, "%.4XH", addr);
	char buf[96];
	wsprintf(buf, "Show %s %s in M&emory", f.name, at);
	AppendMenu(hmenu, MF_STRING | (memmon ? 0 : MF_GRAYED), cmd_mem, buf);
	wsprintf(buf, "Show %s %s in &Code", f.name, at);
	AppendMenu(hmenu, MF_STRING | (codemon ? 0 : MF_GRAYED), cmd_code, buf);

	POINT sp;
	GetCursorPos(&sp);
	int cmd = TrackPopupMenu(hmenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, sp.x, sp.y, 0, hdlg, 0);
	DestroyMenu(hmenu);

	HWND owner = GetWindow(hdlg, GW_OWNER);
	switch (cmd)
	{
	case cmd_mem:
		memmon->ShowAddressCpu(addr, cpu == 1, GetHInst(), owner);
		break;

	case cmd_code:
		// 開いてから CPU を合わせ (ShowCpu は開いている窓だけ)、もう一度番地へ
		codemon->ShowAddress(addr, GetHInst(), owner);
		codemon->ShowCpu(cpu == 1 ? Debugger::cpu_sub : Debugger::cpu_main);
		codemon->ShowAddress(addr, GetHInst(), owner);
		break;
	}
}

// ---------------------------------------------------------------------------
//	値編集ダイアログ
//
BOOL Z80RegMonitor::EDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	char buf[16];
	const RegField& f = fields[editid];

	switch (msg)
	{
	case WM_INITDIALOG:
		wsprintf(buf, "%s (%s):", f.name, editcpu ? "sub" : "main");
		SetDlgItemText(hdlg, IDC_REG_EDIT_TEXT, buf);
		{
			// GetRegValue は reg_af で GetAFSync() を呼ぶので、ここもロックが要る
			// (IDOK 側(222)と同じ形。仕様書 G-4)
			core->Lock();
			uint value = GetRegValue(editcpu, editid);
			core->Unlock();
			wsprintf(buf, f.vallen == 4 ? "%.4x" : (f.vallen == 2 ? "%.2x" : "%x"), value);
		}
		SetDlgItemText(hdlg, IDC_REG_EDITBOX, buf);
		SetFocus(GetDlgItem(hdlg, IDC_REG_EDITBOX));
		SendDlgItemMessage(hdlg, IDC_REG_EDITBOX, EM_SETSEL, 0, -1);
		return 0;

	case WM_COMMAND:
		if (HIWORD(wp) == BN_CLICKED)
		{
			switch (LOWORD(wp))
			{
				char* dum;
			case IDOK:
				GetDlgItemText(hdlg, IDC_REG_EDITBOX, buf, 5);
				if (isxdigit((uint8) buf[0]))
				{
					uint value = strtoul(buf, &dum, 16);
					core->Lock();
					SetRegValue(editcpu, editid, value);
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

BOOL CALLBACK Z80RegMonitor::EDlgProcGate
(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
	Z80RegMonitor* regmon = 0;

	if (m == WM_INITDIALOG) {
		regmon = reinterpret_cast<Z80RegMonitor*>(l);
		if (regmon) {
			::SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)regmon);
		}
	} else {
		regmon = (Z80RegMonitor*)::GetWindowLongPtr(hwnd, GWLP_USERDATA);
	}

	if (regmon) {
		return regmon->EDlgProc(hwnd, m, w, l);
	} else {
		return FALSE;
	}
}

namespace
{
	// UpdateText 用の表示スナップショット。core->Lock() の中で埋め、
	// Unlock() した後で Putf に渡す(下の注意を参照)
	struct RegSnapshot
	{
		uint pc, sp;
		uint af, bc, hl, de, ix, iy;
		uint af2, bc2, hl2, de2;
		uint ireg, intmode;
	};

	// GetAFSync() は「遅延評価中のフラグを確定させる」処理であり、
	// RegF を書き換えて uf を落とす。読み出しではなく書き込みなので、
	// core->Lock() の中でだけ呼ぶこと(呼び出し元で保護する。仕様書 G-4)
	void FillRegSnapshot(RegSnapshot& s, PC88::Z80* z, uint m)
	{
		s.pc = z->GetPC() & m;
		s.af = z->GetAFSync() & m;

		const Z80Reg& r = z->GetReg();
		s.sp  = r.r.w.sp & m;
		s.bc  = r.r.w.bc & m;
		s.hl  = r.r.w.hl & m;
		s.de  = r.r.w.de & m;
		s.ix  = r.r.w.ix & m;
		s.iy  = r.r.w.iy & m;
		s.af2 = r.r_af & m;
		s.bc2 = r.r_bc & m;
		s.hl2 = r.r_hl & m;
		s.de2 = r.r_de & m;
		s.ireg = r.ireg;
		s.intmode = r.intmode;
	}
}

// ---------------------------------------------------------------------------
//	状態を表示
//
void Z80RegMonitor::UpdateText()
{
	const uint m = 0xffff;

	// この窓は SetUpdateTimer(50) で VM が走ったまま 50ms ごとに呼ばれる。
	// GetAFSync() を含むレジスタの読み出しは core->Lock() の中で行い、
	// 結果をローカル(RegSnapshot)へ写してから Unlock() する。
	// Z80Reg を参照のまま持ち回って Unlock() 後に読むと、ロックの意味が
	// 無くなるので注意(仕様書 G-4)。Putf での表示はロックの外で行う
	RegSnapshot r1, r2;
	core->Lock();
	FillRegSnapshot(r1, core->GetCPU1(), m);
	FillRegSnapshot(r2, core->GetCPU2(), m);
	core->Unlock();

	Putf("PC:%.4x SP:%.4x   PC:%.4x SP:%.4x\n",	r1.pc,  r1.sp,  r2.pc,  r2.sp);
	Putf("AF:%.4x BC:%.4x   AF:%.4x BC:%.4x\n",	r1.af,  r1.bc,  r2.af,  r2.bc);
	Putf("HL:%.4x DE:%.4x   HL:%.4x DE:%.4x\n",	r1.hl,  r1.de,  r2.hl,  r2.de);
	Putf("IX:%.4x IY:%.4x   IX:%.4x IY:%.4x\n",	r1.ix,  r1.iy,  r2.ix,  r2.iy);
	Putf("AF'%.4x BC'%.4x   AF'%.4x BC'%.4x\n",	r1.af2, r1.bc2, r2.af2, r2.bc2);
	Putf("HL'%.4x DE'%.4x   HL'%.4x DE'%.4x\n",	r1.hl2, r1.de2, r2.hl2, r2.de2);
	Putf("I :%.2x   IM:   %x   I :%.2x   IM:   %x\n",	r1.ireg, r1.intmode, r2.ireg, r2.intmode);
}
