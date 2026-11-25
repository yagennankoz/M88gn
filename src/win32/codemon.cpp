// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1999, 2000.
// ---------------------------------------------------------------------------
//	$Id: codemon.cpp,v 1.8 2001/02/21 11:58:53 cisc Exp $

#include "headers.h"
#include "resource.h"
#include "pc88/pc88.h"
#include "codemon.h"
#include "labeledit.h"

static int JumpTarget(uint addr, const uint8* b);
#include "misc.h"
#include "device_i.h"
#include "winvars.h"
#include "88config.h"
#include "dpi.h"
#include "mnemmon.h"
#include "memmon.h"
#include "dbgbuttons.h"

using namespace PC8801;

// ---------------------------------------------------------------------------
//	構築/消滅
//
CodeMonitor::CodeMonitor()
: core(0), pcfollow(true), mnemmon(0), memmon(0), showcycles(true), lasta0(MemoryViewer::mainram),
  dumpfrom(0), dumpto(0xffff)
{
}

CodeMonitor::~CodeMonitor()
{
}

bool CodeMonitor::Init(WinCore* wincore)
{
	core = wincore;
	buttons.Init(wincore);
	// 行頭に PC マーカ/BP マーカ用の1文字を足すため、既定カラム数は
	// 42 ではなく 44 にする
	if (!MemViewMonitor::Init(MAKEINTRESOURCE(IDD_CODEMON), wincore, "MonitorCode", 44, 24))
		return false;
	if (!diag.Init(GetBus()))
		return false;

	SetLines(0x10000);
	SetUpdateTimer(500);
	return true;
}

// ---------------------------------------------------------------------------
//	PCFollow の ON/OFF を M88.ini に保存/復元する(既定 ON)
//
void CodeMonitor::LoadMonitorConfig()
{
	MemViewMonitor::LoadMonitorConfig();
	pcfollow = GetPrivateProfileInt("MonitorCode", "PCFollow", 1, PC8801::GetM88IniPath()) != 0;
	showcycles = GetPrivateProfileInt("MonitorCode", "Cycles", 1, PC8801::GetM88IniPath()) != 0;
}

void CodeMonitor::SaveMonitorConfig()
{
	MemViewMonitor::SaveMonitorConfig();
	WritePrivateProfileString("MonitorCode", "PCFollow", pcfollow ? "1" : "0", PC8801::GetM88IniPath());
	WritePrivateProfileString("MonitorCode", "Cycles", showcycles ? "1" : "0", PC8801::GetM88IniPath());
}

// ---------------------------------------------------------------------------
//	ブレーク発生位置へスクロールする(PC追従モードが有効なときだけ)。
//	PC が既に画面内(行の先頭アドレスとして見えている)なら動かさない。
//	表示中のバンクとブレークした CPU が食い違うときは追従しない
//
void CodeMonitor::FollowPC()
{
	if (!pcfollow || !IsOpen())
		return;

	Debugger* dbg = core->GetDebugger();
	// ブレーク中は、ブレークした CPU を表示しているときだけ追従する
	// (別の CPU を見ているのに勝手にスクロールさせない)
	if (dbg->IsBroken() && dbg->GetBreakCpu() != GetShownCpu())
		return;

	ScrollToPC();
}

// ---------------------------------------------------------------------------
//	表示中の CPU の PC へスクロールする。FollowPC と違い、ブレークした CPU と
//	一致しているかどうかは見ない(バンクを手で切り替えたときに使う。
//	「別の CPU を見ている」ことがそのまま操作の意図なので、ここでは弾かない)
//
void CodeMonitor::FollowShownPC()
{
	if (!pcfollow || !IsOpen())
		return;

	ScrollToPC();
}

// ---------------------------------------------------------------------------
//	表示中の CPU の PC が既に画面内(行の先頭アドレスとして見えている)なら
//	動かさない。それ以外は画面の1/4くらいの位置に置き直す
//
void CodeMonitor::ScrollToPC()
{
	Debugger::CpuTarget shown = GetShownCpu();
	PC88::Z80* cpu = (shown == Debugger::cpu_sub) ? core->GetCPU2() : core->GetCPU1();
	uint pc = cpu->GetPC();

	for (size_t y = 0; y < lineaddr.size(); y++)
	{
		if (lineaddr[y] == pc)
		{
			// 命令境界として既に見えている。スクロールしない
			Update();
			return;
		}
	}

	// 画面外(またはジャンプ/自己書き換えで命令境界がずれた)。
	// PC を画面の1/4くらいの位置に置き直す。InstDec は完全ではないので、
	// InstInc で辿り直して pc にちょうど乗るか必ず検証する
	int rows = GetHeight();
	int k = Max(1, rows / 4);

	uint candidate = pc;
	for (int i = 0; i < k; i++)
	{
		if (candidate == 0)
			break;
		uint prev = diag.InstDec(candidate);
		if (prev >= candidate)
			break;
		candidate = prev;
	}

	bool hit = false;
	uint a = candidate;
	for (int i = 0; i < rows; i++)
	{
		if (a == pc)
		{
			hit = true;
			break;
		}
		if (a >= 0x10000)
			break;
		a = diag.InstInc(a);
	}

	SetLine(hit ? candidate : pc);
	Update();
}

// ---------------------------------------------------------------------------
//	表示中のバンクに対応する CPU
//
Debugger::CpuTarget CodeMonitor::GetShownCpu()
{
	return (GetA0() == MemoryViewer::sub) ? Debugger::cpu_sub : Debugger::cpu_main;
}

// ---------------------------------------------------------------------------
//	表示する CPU を切り替える。main へ戻すときは、sub へ切り替える前に
//	見ていたバンクを復元する
//
void CodeMonitor::ShowCpu(Debugger::CpuTarget cpu)
{
	if (!IsOpen() || GetShownCpu() == cpu)
		return;

	if (cpu == Debugger::cpu_sub)
	{
		lasta0 = GetA0();			// main 側で見ていたバンクを覚えておく
		SetA0(MemoryViewer::sub);
	}
	else
	{
		SetA0(lasta0);
	}
	SetBank();						// 中で Update() まで済む
}

void CodeMonitor::ShowAt(uint addr, bool sub, int bank, HINSTANCE hinst, HWND parent)
{
	ShowAddress(addr, hinst, parent);		// 閉じていれば開く (ShowCpu は開いている窓だけ)
	ShowCpu(sub ? Debugger::cpu_sub : Debugger::cpu_main);
	if (sub)
		ShowAddress(addr, hinst, parent);
	else
		ShowAddressIn(addr, false, bank, 1, hinst, parent);
}

// ---------------------------------------------------------------------------
//	表示中の CPU / バンクをタイトルバーに出す
//	(ブレークに連動して自動で切り替わるので、いま何を見ているのかが
//	 分かるようにしておく)
//
void CodeMonitor::UpdateCaption()
{
	if (!IsOpen())
		return;

	const char* bank;
	switch (GetA0())
	{
	case MemoryViewer::sub:		bank = "SUB"; break;
	case MemoryViewer::n88rom:	bank = "MAIN - N88 ROM"; break;
	case MemoryViewer::nrom:	bank = "MAIN - N ROM"; break;
	case MemoryViewer::n80rom:	bank = "MAIN - N80 ROM"; break;
	case MemoryViewer::n80srrom: bank = GetShown6() == MemoryViewer::n80sr1 ? "MAIN - N80SR ROM (6000=8000)" : "MAIN - N80SR ROM"; break;
	case MemoryViewer::eram0:	bank = "MAIN - ERAM0"; break;
	case MemoryViewer::eram1:	bank = "MAIN - ERAM1"; break;
	case MemoryViewer::eram2:	bank = "MAIN - ERAM2"; break;
	case MemoryViewer::eram3:	bank = "MAIN - ERAM3"; break;
	default:					bank = "MAIN - RAM"; break;
	}

	char buf[64];
	wsprintf(buf, IsAutoBank() && GetA0() != MemoryViewer::sub ? "Code  [%s (Auto)]" : "Code  [%s]", bank);
	SetWindowText(GetHWnd(), buf);
}

void CodeMonitor::SetBank()
{
	MemViewMonitor::SetBank();
	UpdateCaption();
}

// ---------------------------------------------------------------------------
//	窓が開いた直後に呼ばれる。UpdateCaption() は SetBank() からしか
//	呼ばれていなかったので、開いた直後はタイトルが .rc の "Code" のままだった
//
void CodeMonitor::Start()
{
	UpdateCaption();
	buttons.Attach(GetHWnd(), GetHInst());
	addritem.Setup(GetHWnd(), IDM_CODE_CURADDR);
}

// ---------------------------------------------------------------------------
//	ダイアログ処理
//
BOOL CodeMonitor::DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	POINT p;

	// メニューバーの Pause/Play/Step のボタン (M88gn)
	if (buttons.Message(hdlg, msg, wp, lp))
	{
		SetWindowLongPtr(hdlg, DWLP_MSGRESULT, TRUE);
		return TRUE;
	}
	// メニューバーの Addr の欄 (M88gn)
	if (addritem.Message(hdlg, msg, wp, lp))
	{
		SetWindowLongPtr(hdlg, DWLP_MSGRESULT, TRUE);
		return TRUE;
	}

	switch (msg)
	{
	case WM_MOUSEMOVE:
		// マウスの下の行に下線 (M88gn)
		p.x = (short) LOWORD(lp);
		p.y = (short) HIWORD(lp);
		TrackGuideLeave(hdlg);
		{
			int row = GetTextPos(&p) && p.y >= 0 && p.y < GetHeight() ? p.y : -1;
			SetGuide(row);
			// メニューバーの Addr の欄に、その行の番地とラベル (M88gn)
			char t[64] = "Addr: ----";
			if (row >= 0 && size_t(row) < lineaddr.size() && lineaddr[row] < 0x10000)
			{
				uint a = lineaddr[row];
				const char* l = core->GetDebugger()->GetLabel(GetShownCpu(), a, ShownBankAt(a));
				if (l)
					wsprintf(t, "Addr: %.4X  %s", a, l);
				else
					wsprintf(t, "Addr: %.4X", a);
			}
			addritem.SetText(t);
		}
		break;

	case WM_MOUSELEAVE:
		guidetracking = false;
		SetGuide(-1);
		addritem.SetText("Addr: ----");
		break;

	case WM_MOUSEWHEEL:
	case WM_VSCROLL:
		addritem.SetText("Addr: ----");		// 行がずれるので消す (次にマウスを動かせば出し直す)
		break;

	case WM_COMMAND:
		switch (LOWORD(wp))
		{
		case IDM_MEM_SAVEIMAGE:
			DumpImage();
			break;

		case IDM_CODE_SAVELABELS:		// ラベル付きで保存 (M88gn)
			DumpImage(dump_labeled);
			break;

		case IDM_CODE_SAVESOURCE:		// アセンブラに通せる形で保存 (M88gn)
			DumpImage(dump_source);
			break;

		case IDM_CODE_PCFOLLOW:
			pcfollow = !pcfollow;
			break;

		case IDM_CODE_CYCLES:
			showcycles = !showcycles;
			SaveMonitorConfig();
			Update();
			break;

		}
		break;

	case WM_INITMENU:
		CheckMenuItem((HMENU) wp, IDM_CODE_PCFOLLOW, pcfollow ? MF_CHECKED : MF_UNCHECKED);
		CheckMenuItem((HMENU) wp, IDM_CODE_CYCLES, showcycles ? MF_CHECKED : MF_UNCHECKED);
		break;

	case WM_LBUTTONDOWN:
		p.x = LOWORD(lp);
		p.y = HIWORD(lp);
		if (GetTextPos(&p))
			ToggleBreakAt(p);
		break;

	case WM_RBUTTONUP:
		// 右クリックのメニュー (M88gn): ニーモニック表の該当行へ・飛び先へ・ブレークポイント
		p.x = (short) LOWORD(lp);
		p.y = (short) HIWORD(lp);
		if (GetTextPos(&p))
			ShowLineMenu(hdlg, p);
		return TRUE;
	}

	BOOL r = MemViewMonitor::DlgProc(hdlg, msg, wp, lp);

	// $0000 のバンクを手で切り替えたときは、止まっていれば表示中の CPU の
	// PC を見せる。切り替えただけでは表示位置が前のままで、PC の行が
	// 画面外にいると緑の行が見えない
	if (msg == WM_COMMAND && core->IsStopped())
	{
		switch (LOWORD(wp))
		{
		case IDM_MEM_0_RAM:   case IDM_MEM_0_N88:   case IDM_MEM_0_N:
		case IDM_MEM_0_SUB:
		case IDM_MEM_0_ERAM0: case IDM_MEM_0_ERAM1:
		case IDM_MEM_0_ERAM2: case IDM_MEM_0_ERAM3:
			FollowShownPC();
			break;
		}
	}
	return r;
}

// ---------------------------------------------------------------------------
//	命令の飛び先 (JP nn・JP cc,nn・CALL nn・CALL cc,nn・JR・JR cc・DJNZ・RST)。
//	命令に飛び先が書かれていなければ -1 (RET・JP (HL) など)
//
static int JumpTarget(uint addr, const uint8* b)
{
	uint op = b[0];
	if (op == 0xc3 || op == 0xcd || (op & 0xc7) == 0xc2 || (op & 0xc7) == 0xc4)
		return b[1] | (b[2] << 8);
	if (op == 0x18 || op == 0x10 || op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38)
		return (addr + 2 + int8(b[1])) & 0xffff;
	if ((op & 0xc7) == 0xc7)
		return op & 0x38;
	return -1;
}

// ---------------------------------------------------------------------------
//	命令に書かれた 16 ビットの番地 (LD rr,nn・LD (nn),A・LD A,(nn)・LD HL,(nn)・LD (nn),HL・JP・CALL、
//	ED の LD (nn),rr・LD rr,(nn)、DD/FD の LD IX,nn・LD (nn),IX・LD IX,(nn))。無ければ -1
//
static int OperandAddress(const uint8* b)
{
	uint op = b[0];
	if ((op & 0xcf) == 0x01 || op == 0x22 || op == 0x2a || op == 0x32 || op == 0x3a
		|| op == 0xc3 || op == 0xcd || (op & 0xc7) == 0xc2 || (op & 0xc7) == 0xc4)
		return b[1] | (b[2] << 8);
	if (op == 0xed && ((b[1] & 0xc7) == 0x43 || (b[1] & 0xc7) == 0x4b))
		return b[2] | (b[3] << 8);
	if ((op == 0xdd || op == 0xfd) && (b[1] == 0x21 || b[1] == 0x22 || b[1] == 0x2a))
		return b[2] | (b[3] << 8);
	return -1;
}

// ---------------------------------------------------------------------------
//	右クリックのメニュー (M88gn)。p はクリックした文字の位置
//
void CodeMonitor::ShowLineMenu(HWND hdlg, POINT p)
{
	if (p.y < 0 || size_t(p.y) >= lineaddr.size())
		return;
	uint addr = lineaddr[p.y];
	if (addr >= 0x10000)
		return;

	uint8 b[4];
	for (int i = 0; i < 4; i++)
		b[i] = GetBus()->Read8((addr + i) & 0xffff);
	int target = JumpTarget(addr, b);

	enum { cmd_mnemonic = 1, cmd_goto, cmd_break, cmd_memline, cmd_memoperand, cmd_label, cmd_unlabel,
		   cmd_labeltarget, cmd_labeloperand };
	int operand = OperandAddress(b);
	HMENU hmenu = CreatePopupMenu();
	AppendMenu(hmenu, MF_STRING | (mnemmon ? 0 : MF_GRAYED), cmd_mnemonic, "Show in &Mnemonic Table");
	// メニューに出す番地 (M88gn): ラベルがあれば「名前(1234H)」、無ければ「1234H」。
	// ラベルは画面と同じ決め方 (表示中のバンクのものを先に、無ければ Any)
	int lcpu = GetShownCpu();
	auto addrtext = [&](char* out, uint a)
	{
		const char* l = core->GetDebugger()->GetLabel(lcpu, a, ShownBankAt(a));
		if (l)
			wsprintf(out, "%s(%.4XH)", l, a);
		else
			wsprintf(out, "%.4XH", a);
	};
	char at[64];
	char buf[96];
	if (target >= 0)
		addrtext(at, target), wsprintf(buf, "&Go to %s", at);
	else
		lstrcpy(buf, "&Go to Jump Target");
	AppendMenu(hmenu, MF_STRING | (target >= 0 ? 0 : MF_GRAYED), cmd_goto, buf);
	AppendMenu(hmenu, MF_SEPARATOR, 0, 0);
	// メモリの窓へ (M88gn): この行の番地、命令に書かれた番地
	addrtext(at, addr);
	wsprintf(buf, "Show %s in M&emory", at);
	AppendMenu(hmenu, MF_STRING | (memmon ? 0 : MF_GRAYED), cmd_memline, buf);
	if (operand >= 0)
		addrtext(at, operand), wsprintf(buf, "Show &Operand %s in Memory", at);
	else
		lstrcpy(buf, "Show &Operand in Memory");
	AppendMenu(hmenu, MF_STRING | (memmon && operand >= 0 ? 0 : MF_GRAYED), cmd_memoperand, buf);
	AppendMenu(hmenu, MF_SEPARATOR, 0, 0);
	AppendMenu(hmenu, MF_STRING, cmd_break, "Toggle &Breakpoint");
	// ラベル (M88gn)
	AppendMenu(hmenu, MF_SEPARATOR, 0, 0);
	int lbank = ShownBankAt(addr);		// 今出しているバンク (ラベルが無ければ、付けるときに最初に選んでおく)
	bool haslabel = core->GetDebugger()->GetLabel(lcpu, addr, lbank, &lbank) != 0;
	if (lbank < 0)
		lbank = Debugger::label_anybank;
	addrtext(at, addr);
	wsprintf(buf, haslabel ? "Edit &Label %s..." : "Set &Label (%s)...", at);
	AppendMenu(hmenu, MF_STRING, cmd_label, buf);
	AppendMenu(hmenu, MF_STRING | (haslabel ? 0 : MF_GRAYED), cmd_unlabel, "&Remove Label");
	// 飛び先・参照先のラベル (M88gn)。付いていれば直す
	int tbank = -1, obank = -1;
	char lb[96];
	if (target >= 0)
	{
		tbank = ShownBankAt(target);
		bool has = core->GetDebugger()->GetLabel(lcpu, target, tbank, &tbank) != 0;
		addrtext(at, target);
		wsprintf(lb, has ? "Edit Label for &Target %s..." : "Set Label for &Target %s...", at);
		AppendMenu(hmenu, MF_STRING, cmd_labeltarget, lb);
	}
	if (operand >= 0 && operand != target)
	{
		obank = ShownBankAt(operand);
		bool has = core->GetDebugger()->GetLabel(lcpu, operand, obank, &obank) != 0;
		addrtext(at, operand);
		wsprintf(lb, has ? "Edit Label for O&perand %s..." : "Set Label for O&perand %s...", at);
		AppendMenu(hmenu, MF_STRING, cmd_labeloperand, lb);
	}

	POINT sp;
	GetCursorPos(&sp);
	int cmd = TrackPopupMenu(hmenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, sp.x, sp.y, 0, hdlg, 0);
	DestroyMenu(hmenu);

	switch (cmd)
	{
	case cmd_mnemonic:
		mnemmon->ShowInstruction(b, GetHInst(), GetWindow(hdlg, GW_OWNER));
		break;

	case cmd_goto:
		SetJumpMark(target);
		JumpTo(target);
		Update();
		break;

	case cmd_break:
		ToggleBreakAt(p);
		break;

	case cmd_memline:
		memmon->ShowAddress(addr, GetHInst(), GetWindow(hdlg, GW_OWNER));
		break;

	case cmd_memoperand:
		memmon->ShowAddress(operand, GetHInst(), GetWindow(hdlg, GW_OWNER));
		break;

	case cmd_label:
		if (EditLabel(hdlg, GetHInst(), core->GetDebugger(), lcpu, addr, lbank))
			Update();
		break;

	case cmd_labeltarget:
	case cmd_labeloperand:
		{
			uint t = cmd == cmd_labeltarget ? uint(target) : uint(operand);
			int bk = cmd == cmd_labeltarget ? tbank : obank;
			if (EditLabel(hdlg, GetHInst(), core->GetDebugger(), lcpu, t, bk < 0 ? int(Debugger::label_anybank) : bk))
			{
				Update();
				if (memmon)
					memmon->Update();
			}
		}
		break;

	case cmd_unlabel:
		core->GetDebugger()->SetLabel(lcpu, addr, lbank, 0);
		Update();
		break;
	}
}

// ---------------------------------------------------------------------------
//	クリックされた行のアドレスの実行ブレークポイントをトグルする。
//	表示中のバンク(&0000 が Sub かどうか)に応じて、対象 CPU を切り替える
//
void CodeMonitor::ToggleBreakAt(POINT p)
{
	if (p.y < 0 || size_t(p.y) >= lineaddr.size())
		return;

	uint addr = lineaddr[p.y];
	if (addr >= 0x10000)
		return;

	Debugger::CpuTarget cpu = (GetA0() == MemoryViewer::sub) ? Debugger::cpu_sub : Debugger::cpu_main;

	core->Lock();
	Debugger* dbg = core->GetDebugger();
	dbg->SetExecBreak(cpu, addr, !dbg->IsExecBreak(cpu, addr));
	core->Unlock();

	Update();
}

// ---------------------------------------------------------------------------
//	スクロールバー処理
//
int CodeMonitor::VerticalScroll(int msg)
{
	int addr = GetLine();
	
	switch (msg)
	{
		int i;

	case SB_LINEUP:
		addr = diag.InstDec(addr);
		if (addr >= 0)
			ScrollDown();
		break;

	case SB_LINEDOWN:
		addr = diag.InstInc(addr);
		if (addr < 0x10000)
			ScrollUp();
		break;

	case SB_PAGEUP:
		for (i=1; i<GetHeight() && addr > 0; i++)
			addr = diag.InstDec(addr);
		break;

	case SB_PAGEDOWN:
		for (i=1; i<GetHeight() && addr < 0x10000; i++)
			addr = diag.InstInc(addr);
		break;

	case SB_TOP:
		addr = 0;
		break;

	case SB_BOTTOM:
		addr = 0x10000-1;
		break;

	case SB_THUMBTRACK:
	case SB_THUMBPOSITION:
		addr = GetScrPos(msg == SB_THUMBTRACK);
		break;
	}
	return Limit(addr, 0xffff, 0);
}

static inline void ToHex(char** p, uint d)
{
	static const char hex[] = "0123456789abcdef";

	*(*p)++ = hex[d >> 4];
	*(*p)++ = hex[d & 15];
}

// ---------------------------------------------------------------------------
//	命令のサイクル数 (Z80 の T ステート、M88gn)。PC-8801 のメモリのウェイトは含めない。
//	条件で変わる命令は「成立したとき/しないとき」(JR cc・DJNZ・CALL cc・RET cc・LDIR など)。
//	表の値: 下位 8 ビット = 成立したとき (または常に)、上位 8 ビット = 成立しないとき (0 なら同じ)
//
#define C2(t, n)	((t) | ((n) << 8))

static const uint16 cyc_main[256] =
{
	 4,10, 7, 6, 4, 4, 7, 4,  4,11, 7, 6, 4, 4, 7, 4,		// 00
	C2(13,8),10, 7, 6, 4, 4, 7, 4, 12,11, 7, 6, 4, 4, 7, 4,	// 10
	C2(12,7),10,16, 6, 4, 4, 7, 4, C2(12,7),11,16, 6, 4, 4, 7, 4,	// 20
	C2(12,7),10,13, 6,11,11,10, 4, C2(12,7),11,13, 6, 4, 4, 7, 4,	// 30
	 4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,		// 40
	 4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,		// 50
	 4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,		// 60
	 7, 7, 7, 7, 7, 7, 4, 7,  4, 4, 4, 4, 4, 4, 7, 4,		// 70
	 4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,		// 80
	 4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,		// 90
	 4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,		// a0
	 4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,		// b0
	C2(11,5),10,10,10,C2(17,10),11, 7,11, C2(11,5),10,10, 0,C2(17,10),17, 7,11,	// c0
	C2(11,5),10,10,11,C2(17,10),11, 7,11, C2(11,5), 4,10,11,C2(17,10), 0, 7,11,	// d0
	C2(11,5),10,10,19,C2(17,10),11, 7,11, C2(11,5), 4,10, 4,C2(17,10), 0, 7,11,	// e0
	C2(11,5),10,10, 4,C2(17,10),11, 7,11, C2(11,5), 6,10, 4,C2(17,10), 0, 7,11,	// f0
};

static uint CyclesED(uint op)
{
	if (op >= 0x40 && op < 0x80)
	{
		switch (op & 7)
		{
		case 0: case 1: return 12;					// IN r,(C) / OUT (C),r
		case 2: return 15;							// SBC/ADC HL,rr
		case 3: return 20;							// LD (nn),rr / LD rr,(nn)
		case 4: case 6: return 8;					// NEG / IM
		case 5: return 14;							// RETN / RETI
		default:
			if (op == 0x47 || op == 0x4f || op == 0x57 || op == 0x5f) return 9;	// LD I/R,A / LD A,I/R
			if (op == 0x67 || op == 0x6f) return 18;	// RRD / RLD
			return 8;
		}
	}
	if (op >= 0xa0 && op < 0xc0 && (op & 7) < 4)
		return op & 0x10 ? C2(21, 16) : 16;			// LDIR 等 / LDI 等
	return 8;
}

// IX/IY を使う命令 (DD/FD の後の 1 バイト)。0 なら HL を使わない命令 (前置きは 4 足すだけ)
static uint CyclesIndex(uint op)
{
	switch (op)
	{
	case 0x09: case 0x19: case 0x29: case 0x39: return 15;	// ADD IX,rr
	case 0x21: return 14;									// LD IX,nn
	case 0x22: case 0x2a: return 20;						// LD (nn),IX / LD IX,(nn)
	case 0x23: case 0x2b: return 10;						// INC/DEC IX
	case 0x24: case 0x25: case 0x2c: case 0x2d: return 8;	// INC/DEC IXH/IXL
	case 0x26: case 0x2e: return 11;						// LD IXH/IXL,n
	case 0x34: case 0x35: return 23;						// INC/DEC (IX+d)
	case 0x36: return 19;									// LD (IX+d),n
	case 0xe1: return 14;									// POP IX
	case 0xe3: return 23;									// EX (SP),IX
	case 0xe5: return 15;									// PUSH IX
	case 0xe9: return 8;									// JP (IX)
	case 0xf9: return 10;									// LD SP,IX
	}
	if (op >= 0x40 && op < 0xc0 && op != 0x76)
	{
		bool mem = (op & 7) == 6 || (op < 0x80 && (op & 0x38) == 0x30);
		return mem ? 19 : 8;								// (IX+d) を使うか、IXH/IXL など
	}
	return 0;
}

uint CodeMonitor::GetCycles(uint a)
{
	uint op = GetBus()->Read8(a & 0xffff);
	switch (op)
	{
	case 0xcb:
		op = GetBus()->Read8((a + 1) & 0xffff);
		if ((op & 7) != 6)
			return 8;
		return (op & 0xc0) == 0x40 ? 12 : 15;				// BIT n,(HL) / それ以外の (HL)

	case 0xed:
		return CyclesED(GetBus()->Read8((a + 1) & 0xffff));

	case 0xdd: case 0xfd:
		{
			uint op2 = GetBus()->Read8((a + 1) & 0xffff);
			if (op2 == 0xcb)
			{
				uint op4 = GetBus()->Read8((a + 3) & 0xffff);
				return (op4 & 0xc0) == 0x40 ? 20 : 23;		// BIT n,(IX+d) / それ以外
			}
			if (op2 == 0xdd || op2 == 0xfd || op2 == 0xed)
				return 4;									// 前置きが続く: この前置きは NOP 扱い
			uint c = CyclesIndex(op2);
			if (c)
				return c;
			c = cyc_main[op2];
			return (c & 0xff) + 4 + ((c >> 8) ? ((c >> 8) + 4) << 8 : 0);
		}
	}
	return cyc_main[op];
}

// 逆アセンブルの字の中の飛び先 (最後の「16 進 + H」) を、ラベルがあればその名前に置き換える (M88gn)。
// text は 120 バイトの置き場 (buf + 8)。置き換えたら名前の位置 (text の中の何文字目か) を返し、len に長さ。
// 置き換えなければ -1。paren なら「(1234H)」の括弧の中の番地 (メモリの参照先) だけを置き換える
static int ReplaceTargetLabel(char* text, int target, const char* label, int* len, bool paren = false)
{
	if (target < 0 || !label)
		return -1;
	int n = lstrlen(text);
	for (int e = n - 1; e > 0; e--)
	{
		if (text[e] != 'H')
			continue;
		int s = e;
		while (s > 0 && isxdigit((unsigned char) text[s - 1]))
			s--;
		if (s == e)
			continue;
		// 数の前が区切り (空白・カンマ・括弧) でなければ名前の一部
		if (s > 0 && text[s - 1] != ' ' && text[s - 1] != ',' && text[s - 1] != '\t' && text[s - 1] != '(')
			continue;
		if (paren && (s == 0 || text[s - 1] != '(' || text[e + 1] != ')'))
			continue;
		char num[16];
		lstrcpyn(num, text + s, Min(e - s + 1, int(sizeof(num))));
		if (int(strtoul(num, 0, 16)) != target)
			return -1;
		char tail[128];
		lstrcpyn(tail, text + e + 1, sizeof(tail));
		text[s] = 0;
		char out[128];
		_snprintf(out, sizeof(out) - 1, "%s%s%s", text, label, tail);
		out[sizeof(out) - 1] = 0;
		lstrcpyn(text, out, 120);	// buf + 8 なので 120 バイト
		if (s >= lstrlen(text))
			return -1;
		*len = Min(lstrlen(label), lstrlen(text) - s);
		return s;
	}
	return -1;
}

// この命令で名前に置き換える番地 (M88gn)。飛び先 (JumpTarget) か、無ければ命令に書かれた番地 (OperandAddress。
// 置き換えるのは「(1234H)」の形のメモリの参照先だけ)。paren に参照先か。無ければ -1。
// norst なら RST の番号は置き換えない (アセンブラに通す形)
static int LabelRef(uint a, const uint8* b, bool* paren, bool norst = false)
{
	int t = JumpTarget(a, b);
	if (t >= 0)
	{
		*paren = false;
		return (norst && (b[0] & 0xc7) == 0xc7) ? -1 : t;
	}
	*paren = true;
	return OperandAddress(b);
}

void CodeMonitor::UpdateText()
{
	if (AutoBank())
		UpdateCaption();
	char buf[128];
	int a = GetLine();

	// PC マーカ。表示中のバンクに対応する CPU の PC だけをマークする。
	// 別の CPU の PC を重ねると、その行には全く関係ないコードが表示されて
	// いるのに緑になってしまう。マーカはポーズ中だけ出す (実行中に出すと
	// 「止まっている位置」と紛らわしい)
	bool subshown = (GetShownCpu() == Debugger::cpu_sub);
	int pc = -1;
	if (core->IsPaused())
		pc = (subshown ? core->GetCPU2() : core->GetCPU1())->GetPC();

	buttons.Update();

	int rows = GetHeight();
	lineaddr.resize(rows);

	for (int y=0; y<rows; y++)
	{
		if (a < 0x10000)
		{
			lineaddr[y] = uint(a);

			bool hasbp = core->GetDebugger()->IsExecBreak(Debugger::cpu_main, a) ||
						 core->GetDebugger()->IsExecBreak(Debugger::cpu_sub, a);

			int next = diag.Disassemble(a, buf+8);
			// ジャンプした先 (M88gn) を含む命令の行は、次のジャンプまで色を変える
			bool isjump = jumpaddr >= a && jumpaddr < next;

			char mark;
			if (pc == a)
			{
				mark = subshown ? 'S' : 'M';
				SetBkCol(RGB(0, 0x60, 0));
			}
			else if (isjump)
			{
				mark = hasbp ? '*' : ' ';
				SetBkCol(RGB(0, 0x48, 0x80));
			}
			else if (hasbp)
			{
				mark = '*';
				SetBkCol(RGB(0x60, 0, 0));
			}
			else
			{
				mark = ' ';
				// 実行回数に基づいて色をつけてみる
				SetBkCol(RGB(StatExec(a), 0, 0x20));
			}

			// 飛び先にラベルがあれば、番地の代わりに名前を出す (M88gn)。名前の所は字の色を変える
			int lpos = -1, llen = 0;
			{
				uint8 b[4];
				for (int i = 0; i < 4; i++)
					b[i] = GetBus()->Read8((a + i) & 0xffff);
				bool paren;
				int t = LabelRef(a, b, &paren);
				if (t >= 0)
					lpos = ReplaceTargetLabel(buf + 8, t, core->GetDebugger()->GetLabel(GetShownCpu(), t, ShownBankAt(t)), &llen, paren);
			}
			char* ptr = buf;
			int c, d = next-a;
			for (c=0; c<d; c++)
				ToHex(&ptr, GetBus()->Read8((a+c) & 0xffff));
			for (; c<4; c++)
				*ptr++ = ' ', *ptr++ = ' ';
			// ニーモニック (20 桁にそろえる)。置き換えたラベルの名前の所だけ色を変える
			const char* mn = buf + 8;
			auto putmnemonic = [&]()
			{
				int n = lstrlen(mn);
				if (lpos >= 0)
				{
					// Putf (wvsprintf) は %.*s を使えないので、区切って写してから書く
					char part[128];
					lstrcpyn(part, mn, lpos + 1);
					Puts(part);
					SetTxCol(RGB(0xff, 0xe0, 0x60));
					lstrcpyn(part, mn + lpos, llen + 1);
					Puts(part);
					SetTxCol(0xffffff);
					Puts(mn + lpos + llen);
				}
				else
					Puts(mn);
				for (; n < 20; n++)
					Puts(" ");
			};
			// ラベル (M88gn): 行の右端 (サイクル数の後) に出す
			const char* label = core->GetDebugger()->GetLabel(GetShownCpu(), a, ShownBankAt(a));
			if (showcycles)
			{
				// 行の右に「; 12/7」のように出す (ニーモニックの幅をそろえる)
				uint c = GetCycles(a);
				Putf("%c %.4x: %.8s   ", mark, a, buf);
				putmnemonic();
				SetTxCol(0xa0c0a0);
				char cyc[16];
				if (c >> 8)
					wsprintf(cyc, "%d/%d", c & 0xff, c >> 8);
				else
					wsprintf(cyc, "%d", c & 0xff);
				if (label)
					Putf("; %-6s ", cyc);
				else
					Putf("; %s", cyc);
			}
			else
			{
				Putf("%c %.4x: %.8s   ", mark, a, buf);
				putmnemonic();
			}
			if (label)
			{
				SetTxCol(RGB(0xff, 0xe0, 0x60));
				Puts(label);
			}
			Puts("\n");
			SetTxCol(0xffffff);
			a = next;
		}
		else
		{
			lineaddr[y] = uint(-1);
			Puts("\n");
		}
	}
	StatClear();
}

bool CodeMonitor::DumpTo(TextBuffer& tb, int from, int to)
{
	char buf[128];
	for (int a=from; a<to; a)
	{
		int next = diag.Disassemble(a, buf+8);

		char* ptr = buf;
		int c, d = next-a;
		for (c=0; c<d; c++)
			ToHex(&ptr, GetBus()->Read8((a+c) & 0xffff));
		for (; c<4; c++)
			*ptr++ = ' ', *ptr++ = ' ';

		tb.Addf("%.4x %.8s\t\t%s\n", a, buf, buf+8);
		a = next;
	}
	return !tb.Failed();
}

// ラベル付きの逆アセンブル (M88gn)。今表示しているバンクの 0000-FFFF。
// ラベルの選び方は画面と同じ (表示中のバンクのものを先に、無ければ Any)
// 1 バイトの値をアセンブラの 16 進で (A～F で始まれば 0 を付ける)
static void HexByte(char* buf, uint v)
{
	wsprintf(buf, "%s%.2XH", v >= 0xa0 ? "0" : "", v & 0xff);
}

bool CodeMonitor::DumpToLabeled(TextBuffer& tb, bool source, uint from, uint to)
{
	Debugger* dbg = core->GetDebugger();
	int cpu = GetShownCpu();
	from &= 0xffff;
	to &= 0xffff;
	if (to < from)
		to = from;

	// この命令の飛び先を、ラベルの名前で書くか (書くなら飛び先、書かないなら -1)
	// RST の番号はアセンブラによっては名前を書けないので、ソースの形では数のまま
	char buf[128];
	// 飛び先・括弧付きの参照先 (LabelRef)。text の字を実際に置き換えられたときだけ
	auto labeltarget = [&](uint a, const uint8* b, int size, bool asdb, char* text) -> int
	{
		if (asdb)
			return -1;
		bool paren;
		int t = LabelRef(a, b, &paren, source);
		if (t < 0)
			return -1;
		const char* l = dbg->GetLabel(cpu, t, ShownBankAt(t));
		int len;
		if (!l || ReplaceTargetLabel(text, t, l, &len, paren) < 0)
			return -1;
		return t;
	};
	// アセンブラに通せる形: 公式の命令でない (同じバイトに戻らないかもしれない) もの、FFFF を越えるものは DB で
	auto writeasdb = [&](uint a, const uint8* b, int size) -> bool
	{
		return source && (a + size > 0x10000 || !IsOfficialInstruction(b, size));
	};

	// 1 回目: 命令の頭の番地と、名前で書く飛び先を調べる
	std::vector<bool> start(0x10000, false);
	std::vector<bool> target(0x10000, false);
	uint end = from;			// 書き出す最後の命令の次の番地 (to を越えることがある)
	for (uint a = from; a <= to && a < 0x10000; )
	{
		start[a] = true;
		int next = diag.Disassemble(a, buf + 8);
		int size = int(next) > int(a) ? next - a : 1;
		uint8 b[4];
		for (int i = 0; i < 4; i++)
			b[i] = GetBus()->Read8((a + i) & 0xffff);
		int t = labeltarget(a, b, size, writeasdb(a, b, size), buf + 8);
		if (t >= 0)
			target[t] = true;
		a += size;
		end = a;
	}

	char caption[96];
	GetWindowText(GetHWnd(), caption, sizeof(caption));
	if (source)
	{
		tb.Addf("; M88gn disassembly (assembler source)  %s\n", caption);
		tb.Add("; Instructions not in the official Zilog set are written as DB.\n");
		tb.Add("; The comment on each line is the address and the bytes.\n");
	}
	else
		tb.Addf("; M88gn disassembly with labels  %s\n", caption);
	tb.Addf("; Range %.4X-%.4X\n", from, Min(end, 0x10000u) - 1);

	// 行の頭に置けないラベルは先頭に EQU で:
	// 範囲の中で命令の途中に当たるもの、範囲の外で飛び先に名前で書いたもの
	bool anyequ = false;
	for (uint a = 0; a < 0x10000; a++)
	{
		bool inside = a >= from && a < end;
		if (inside ? start[a] : !target[a])
			continue;
		const char* l = dbg->GetLabel(cpu, a, ShownBankAt(a));
		if (!l)
			continue;
		if (!anyequ)
			tb.Add("\n; labels not at the start of an instruction in this range\n");
		anyequ = true;
		tb.Addf("%-15s\tEQU\t%s%.4XH\n", l, a >= 0xa000 ? "0" : "", a);
	}
	tb.Add("\n");
	if (source)
		tb.Addf("\tORG\t%s%.4XH\n\n", from >= 0xa000 ? "0" : "", from);

	// 2 回目: 書き出す
	for (uint a = from; a < end; )
	{
		const char* l = dbg->GetLabel(cpu, a, ShownBankAt(a));
		if (l)
			tb.Addf("%s:\n", l);

		int next = diag.Disassemble(a, buf + 8);
		uint8 b[4];
		for (int i = 0; i < 4; i++)
			b[i] = GetBus()->Read8((a + i) & 0xffff);
		int size = int(next) > int(a) ? next - a : 1;
		bool asdb = writeasdb(a, b, size);
		labeltarget(a, b, size, asdb, buf + 8);		// 置き換える

		char* ptr = buf;
		int c;
		for (c = 0; c < size && c < 4; c++)
			ToHex(&ptr, b[c]);
		for (; c < 4; c++)
			*ptr++ = ' ', *ptr++ = ' ';

		if (!source)
			tb.Addf("%.4x %.8s\t\t%s\n", a, buf, buf + 8);
		else
		{
			char bytes[16];
			lstrcpyn(bytes, buf, 9);
			for (int k = lstrlen(bytes); k > 0 && bytes[k - 1] == ' '; k--)
				bytes[k - 1] = 0;
			if (asdb)
			{
				char db[64] = "DB   ";
				int n = Min(size, int(0x10000 - a));
				for (int k = 0; k < n; k++)
				{
					char h[8];
					HexByte(h, b[k]);
					if (k)
						lstrcat(db, ",");
					lstrcat(db, h);
				}
				tb.Addf("\t%-28s; %.4X  %s  (%s)\n", db, a, bytes, buf + 8);
				size = n;
			}
			else
				tb.Addf("\t%-28s; %.4X  %s\n", buf + 8, a, bytes);
		}
		a += size;
	}
	return !tb.Failed();
}

// ---------------------------------------------------------------------------
//	ラベル付きの保存の範囲を聞く (M88gn。メモリの窓の Save Range と同じ窓)
//
namespace
{
struct DumpRangeCtx
{
	uint from, to;
	const char* title;
};

BOOL CALLBACK DumpRangeDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	char buf[64];
	switch (msg)
	{
	case WM_INITDIALOG:
		{
			SetWindowLongPtr(hdlg, DWLP_USER, lp);
			DumpRangeCtx* ctx = (DumpRangeCtx*) lp;
			SetWindowText(hdlg, ctx->title);
			wsprintf(buf, "%.4x", ctx->from);
			SetDlgItemText(hdlg, IDC_RANGE_START, buf);
			wsprintf(buf, "%.4x", ctx->to);
			SetDlgItemText(hdlg, IDC_RANGE_END, buf);
			SetFocus(GetDlgItem(hdlg, IDC_RANGE_START));
			SendDlgItemMessage(hdlg, IDC_RANGE_START, EM_SETSEL, 0, -1);
		}
		return FALSE;

	case WM_COMMAND:
		if (LOWORD(wp) == IDOK)
		{
			DumpRangeCtx* ctx = (DumpRangeCtx*) GetWindowLongPtr(hdlg, DWLP_USER);
			// 16 進かラベルの名前 (M88gn)
			uint s, e;
			GetDlgItemText(hdlg, IDC_RANGE_START, buf, sizeof(buf));
			if (!ParseAddrInput(buf, &s))
				return TRUE;
			GetDlgItemText(hdlg, IDC_RANGE_END, buf, sizeof(buf));
			if (!ParseAddrInput(buf, &e))
				return TRUE;
			s &= 0xffff, e &= 0xffff;
			if (e < s)
			{
				MessageBox(hdlg, "終わりの番地が始まりより前です。", ctx->title, MB_OK | MB_ICONWARNING);
				return TRUE;
			}
			ctx->from = s, ctx->to = e;
			EndDialog(hdlg, IDOK);
			return TRUE;
		}
		if (LOWORD(wp) == IDCANCEL)
		{
			EndDialog(hdlg, IDCANCEL);
			return TRUE;
		}
		break;

	case WM_CLOSE:
		EndDialog(hdlg, IDCANCEL);
		return TRUE;
	}
	return FALSE;
}
}

bool CodeMonitor::AskDumpRange(const char* title)
{
	DumpRangeCtx ctx = { dumpfrom, dumpto, title };
	if (DialogBoxParam(GetHInst(), MAKEINTRESOURCE(IDD_MEM_RANGE), GetHWnd(),
					   DLGPROC((void*) DumpRangeDlgProc), (LPARAM) &ctx) != IDOK)
		return false;
	dumpfrom = ctx.from, dumpto = ctx.to;
	return true;
}

bool CodeMonitor::Dump(FILE* fp, int from, int to)
{
	TextBuffer tb;
	DumpTo(tb, from, to);
	fwrite(tb.Get(), 1, tb.Length(), fp);
	return true;
}

// ---------------------------------------------------------------------------
//	選択中のバンクの 0000-ffff を逆アセンブルした全体をクリップボードへ
//	コピーする(仕様変更。docs/spec-phase10a-fix.md F-3)。
//
//	Lock() は要る。64KB 読む間に VM が走っていると、時点の食い違った
//	内容が混ざるため。
//
//	SetSuppressBreak() の方は念のためで、実際には効いていない。
//	GetBus() は MemoryViewer の私設バスで、VM の MemoryManager を
//	通らないので Debugger のメモリフックに届かない
//	(docs/spec-phase5-debug-ui.md §5.3 / docs/TODO.md の T-6)。
//
bool CodeMonitor::CopyToClipboard()
{
	TextBuffer tb;

	core->Lock();
	core->GetDebugger()->SetSuppressBreak(true);
	DumpTo(tb, 0, 0x10000);
	core->GetDebugger()->SetSuppressBreak(false);
	core->Unlock();

	return SetClipboardText(tb.Get(), tb.Length());
}

// ----------------------------------------------------------------------------
//	総ダンプを書き込む
//	
bool CodeMonitor::DumpImage(int mode)
{
	// ダイアログ
	OFNV5 ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = WINVAR(OFNSIZE);
	ofn.FlagsEx = 0;		// 位置バーを消す指定 (OFN_EX_NOPLACESBAR) を付けると、Windows が古い形のダイアログを出す (DPI 対応の窓ではツールバーのアイコンが小さい)

	char filename[MAX_PATH];
	filename[0] = 0;
	
	ofn.hwndOwner = GetHWnd();
	ofn.lpstrFilter = "assembly file (*.asm)\0*.asm\0"
					  "All Files (*.*)\0*.*\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_CREATEPROMPT;
	ofn.lpstrDefExt = "asm";
	ofn.lpstrTitle = mode == dump_labeled ? "Save with Labels" : mode == dump_source ? "Save as Source" : "Save Disassembly";
	// 番地の範囲を聞く (M88gn)
	if (!AskDumpRange(ofn.lpstrTitle))
		return false;
	
	if (!GetSaveFileName(&ofn))
		return false;

	// 書き込み
	FILE* fp = fopen(filename, "w");
	if (fp)
	{
		if (mode != dump_plain)
		{
			TextBuffer tb;
			DumpToLabeled(tb, mode == dump_source, dumpfrom, dumpto);
			fwrite(tb.Get(), 1, tb.Length(), fp);
		}
		else
			Dump(fp, dumpfrom, dumpto + 1);	// to は含まない (dumpto で始まる命令までを書く)
		fclose(fp);
		return true;
	}
	return false;
}
