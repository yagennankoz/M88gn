// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1999, 2000.
// ---------------------------------------------------------------------------
//	$Id: memmon.cpp,v 1.15 2003/05/19 02:33:56 cisc Exp $

#include "headers.h"
#include "resource.h"
#include "pc88/pc88.h"
#include "memmon.h"
#include "88config.h"
#include "watchbank.h"
#include "codemon.h"
#include "patbp.h"
#include "labeledit.h"
#include "dpi.h"

#ifndef WM_DPICHANGED
#define WM_DPICHANGED	0x02e0
#endif
#ifndef SPI_GETFLATMENU
#define SPI_GETFLATMENU	0x1022
#endif
#ifndef COLOR_MENUBAR
#define COLOR_MENUBAR	30
#endif
#include "misc.h"
#include "device_i.h"
#include "file.h"
#include "winvars.h"
#include "if/ifguid.h"

#define LOGNAME "memmon"
#include "diag.h"

using namespace PC8801;

COLORREF MemoryMonitor::col[0x100] = { 0 };

// ---------------------------------------------------------------------------
//	構築/消滅
//
MemoryMonitor::MemoryMonitor()
{
	mid = -1;
	mm = 0;
	sumtype = sum_none;
	hwndtip = 0;
	tipaddr = -1;
	curaddr = -2;
	tiptracking = false;
	codemon = 0;
	rangestart = 0;
	rangeend = 0xffff;
	rangeload = false;
	foundaddr = -1;
	foundlen = 0;
	searchforward = true;

	if (!col[0xff])
	{
		col[0xff] = RGB(0x1f, 0x1f, 0x9f);
		for (int i=1; i<256; i++)
		{
			int r = 0x40 + static_cast<int>(0xbf * pow((i/256.), 8.0));
			int g = 0x20 + static_cast<int>(0xdf * pow((i/256.), 24.0));
			col[0xff-i] = RGB(Min(r, 0xff), Min(g, 0xff), 0xff);
		}
	}
}

MemoryMonitor::~MemoryMonitor()
{
}

// ---------------------------------------------------------------------------
//	初期化
//
bool MemoryMonitor::Init(WinCore* pc88)
{
	core = pc88;
	buttons.Init(pc88);
	if (!MemViewMonitor::Init(MAKEINTRESOURCE(IDD_MEMORY), pc88, "MonitorMemory", 70, 25))
		return false;
	SetLines(0x1000);
	SetUpdateTimer(50);
	line[0] = 0;
	memset(stat, 1, 0x10000);
	time = 0x1;
	memset(access, 0, sizeof(access)); 

	watchflag = 0;
	prevaddr = -1;
	prevlines = 0;
	return true;
}

void MemoryMonitor::SetWatch(uint addr, uint range)
{
	core->Lock();
	if (mid >= 0)
	{
		Log("SetWatch: %p %.4x - %.4x\n", mm, addr, range);
		range = Min(range, 0x10000 - addr);
		
		mm->ReleaseR(mid, 0, 0x10000);
		if (watchflag & 1)
			mm->AllocR(mid, addr, range, MemRead);

		mm->ReleaseW(mid, 0, 0x10000);
		if (watchflag & 2)
			mm->AllocW(mid, addr, range, MemWrite);
	}
	core->Unlock();
}


void MemoryMonitor::Start()
{
	buttons.Attach(GetHWnd(), GetHInst());
	addritem.Setup(GetHWnd(), IDM_MEM_CURADDR);
	curaddr = -2;
	core->Lock();
	if (GetA0() == MemoryViewer::sub)
	{
		mm = (IMemoryManager*) core->QueryIF(M88IID_MemoryManager2);
	}
	else
	{
		mm = (IMemoryManager*) core->QueryIF(M88IID_MemoryManager1);
	}
	if (mm)
	{
		mid = mm->Connect(this, true);
	}	

	gmb = (IGetMemoryBank*) core->QueryIF(M88IID_GetMemoryBank);
	core->Unlock();
}

void MemoryMonitor::Stop()
{
	core->Lock();
	if (mm)
	{
		mm->ReleaseR(mid, 0, 0x10000);
		mm->ReleaseW(mid, 0, 0x10000);
		mm->Disconnect(mid);
		mm = 0;
		mid = -1;
	}
	core->Unlock();
}

void MemoryMonitor::SetBank()
{
	prevlines = 0;
	Stop();
	Start();
	MemViewMonitor::SetBank();
}

uint MEMCALL MemoryMonitor::MemRead(void* p, uint a)
{
	MemoryMonitor* m = reinterpret_cast<MemoryMonitor*>(p);

	// 領域が見たいメモリを指し示しているなら更新
	int b = m->mv.GetCurrentBank(a);
	if (b == -1 || b == m->gmb->GetRdBank(a))
		m->access[a] = m->time;
	
	return m->mm->Read8P(m->mid, a);	// 本来のメモリ空間へとアクセス
}

void MEMCALL MemoryMonitor::MemWrite(void* p, uint a, uint d)
{
	MemoryMonitor* m = reinterpret_cast<MemoryMonitor*>(p);
	
	// 領域が見たいメモリを指し示しているなら更新
	int b = m->mv.GetCurrentBank(a);
	if (b == -1 || b == m->gmb->GetWrBank(a))
		m->access[a] = m->time;

	m->mm->Write8P(m->mid, a, d);	// 本来のメモリ空間へとアクセス
}

// ---------------------------------------------------------------------------
//	テキスト更新
//
void MemoryMonitor::UpdateText()
{
	buttons.Update();
	AutoBank();
	char buf[4];
	char mem[16];
	int a = GetLine() * 0x10;

	// 0行目は列見出し。毎フレーム書き直すのでスクロールしても固定される
	int rows = GetHeight() - 1;
	if (rows < 0)
		rows = 0;

	if (prevaddr != a || prevlines != rows)
	{
		prevaddr = a; prevlines = rows;
		SetWatch(a, rows * 0x10);
	}

	labelboxes.clear();

	// ウォッチの番地 (表示中の CPU の分)。背景色を付ける (M88gn)
	watchshown.clear();
	{
		Debugger* dbg = core->GetDebugger();
		Debugger::CpuTarget cpu = ShownCpu();
		for (int i = 0; i < dbg->GetWatchCount(); i++)
		{
			Debugger::WatchEntry w;
			if (dbg->GetWatch(i, &w) && w.cpu == cpu)
				watchshown.push_back(w);
		}
		bpshown.clear();
		for (int i = 0; i < dbg->GetBreakPointCount(); i++)
		{
			Debugger::BreakPoint bp;
			if (dbg->GetBreakPoint(i, &bp) && bp.cpu == cpu && !bp.io && (bp.read || bp.write))
				bpshown.push_back(bp);
		}
	}

	SetTxCol(0xc0c0c0);
	SetBkCol(RGB(0, 0, 0x50));
	Puts("      ");
	for (int h = 0; h < 16; h++)
		Putf(" %x ", h);
	if (sumtype != sum_none)
		Puts("sum  ");
	Puts("0123456789abcdef\n");
	SetBkCol(RGB(0, 0, 0x20));

	for (int y=0; y<rows; y++)
	{
		if (a < 0x10000)
		{
			int x;
			uint8 row[16];
			int a0 = a;

			// ウォッチしている番地は、値の前後の空白まで橙にする (M88gn)。
			// そのため 1 バイトを「前の空白 + 値」の順に書く (並びは「番地: xx xx ... xx 」のまま)
			// 色: ウォッチは橙、読み・書きのブレークポイントは赤、両方なら紫 (地は明るく、字は黒)
			// 検索で見つけた所は緑 (ほかの印より優先)
			int mark[16];
			for (x=0; x<16; x++)
				mark[x] = InFound(a + x) ? 4 : ((a + x) < 0x10000 && InJump(a + x)) ? 7 : MarkAt((a + x) & 0xffff);
			// ラベルのある番地 (M88gn): 16 進の欄の値と文字の欄の字を角丸の箱で囲む
			for (x=0; x<16; x++)
			{
				if ((a + x) < 0x10000 && core->GetDebugger()->GetLabel(ShownCpu(), (a + x) & 0xffff, ShownBankAt((a + x) & 0xffff)))
				{
					LabelBox hb = { 6 + 3 * x, y + 1, 2 };	// 値は「番地:」の 5 字と前の空白の後
					LabelBox cb = { 6 + 48 + (sumtype != sum_none ? 5 : 0) + x, y + 1, 1 };
					labelboxes.push_back(hb);
					labelboxes.push_back(cb);
				}
			}
			// 5 = 無効なブレークポイント (灰色)、6 = ウォッチと無効なブレークポイント (くすんだ橙)
			// 7 = ジャンプした先 (水色、次のジャンプまで)
			static const COLORREF markbk[8] =
			{
				RGB(0, 0, 0x20), RGB(0xf0, 0xa8, 0x30), RGB(0xf0, 0x70, 0x70), RGB(0xc8, 0x90, 0xf8),
				RGB(0x70, 0xe0, 0x70), RGB(0xa8, 0xa8, 0xa8), RGB(0xc8, 0xb0, 0x88), RGB(0x60, 0xd0, 0xf0),
			};

			SetTxCol(0xffffff);
			Putf("%.4x:", (a & 0xffff));
			buf[2] = 0;

			for (x=0; x<16; x++)
			{
				int d = GetBus()->Read8(a);

				// 前の空白: 左半分は 1 つ前の値の印、右半分はこの値の印 (隙間の真ん中で切り替える)
				PutSplitSpace(markbk[x > 0 ? mark[x - 1] : 0], markbk[mark[x]]);

				COLORREF tx = 0xffffff;
				if (watchflag)
				{
					uint t = 0xff;
					if (access[a])
					{
						t = time - access[a];
						t = t < 0xfe ? t : 0xfe;
					}
					tx = col[t];
				}
				if (mark[x])
				{
					SetBkCol(markbk[mark[x]]);
					SetTxCol(RGB(0, 0, 0));
				}
				else
				{
					SetBkCol(RGB(StatExec(a), stat[a], 0x20));
					SetTxCol(tx);
				}
				Putf("%.2x", d);
				SetTxCol(tx);

				mem[x] = d >= 0x20 && d < 0xfd && d != 0x7f ? d : '.'; 
				row[x] = d;
				a++;
			}
			// 最後の空白: 左半分だけ 16 バイト目の印の色
			PutSplitSpace(markbk[mark[15]], markbk[0]);
			SetBkCol(RGB(0, 0, 0x20));
			if (sumtype != sum_none)
			{
				SetTxCol(0x60ffff);
				Putf(":%.2x  ", RowSum(sumtype, a0, row));
			}
			// 文字の欄も、見つけた所は緑の地に黒い字
			for (x=0; x<16; x++)
			{
				bool f = mark[x] == 4 || mark[x] == 7;
				SetBkCol(f ? markbk[mark[x]] : RGB(0, 0, 0x20));
				SetTxCol(f ? RGB(0, 0, 0) : 0xffffff);
				Putf("%c", mem[x]);
			}
			SetBkCol(RGB(0, 0, 0x20));
			SetTxCol(0xffffff);
			Puts("\n");
		}
		else
		{
			Puts("\n");
		}
	}
	time++;
}

// ---------------------------------------------------------------------------
//	ダイアログ処理
//
BOOL MemoryMonitor::DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
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
		POINT p;
	
	case WM_INITMENU:
		{
			HMENU hmenu = (HMENU) wp;
			CheckMenuItem(hmenu, IDM_MEM_READ,  (watchflag & 1) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_WRITE, (watchflag & 2) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuRadioItem(hmenu, IDM_MEM_SUM_NONE, IDM_MEM_SUM_DEMPA,
				IDM_MEM_SUM_NONE + sumtype, MF_BYCOMMAND);
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wp))
		{
		case IDM_MEM_SAVEIMAGE:
			SaveImage();
			break;

		case IDM_MEM_SAVERANGE:
			SaveRange();
			break;

		case IDM_MEM_LOADRANGE:
			LoadRange();
			break;

		case IDM_MEM_FIND:
			Find();
			break;

		case IDM_MEM_FINDNEXT:
		case IDM_MEM_FINDPREV:
			if (search.len > 0)
				FindPattern(LOWORD(wp) == IDM_MEM_FINDNEXT);
			else
				Find();
			break;

		case IDM_MEM_STATCLEAR:
			time = 0x1;
			memset(access, 0, sizeof(access)); 
			break;
			
		case IDM_MEM_READ:
			watchflag ^= 1;
			prevlines = -1;
			break;

		case IDM_MEM_WRITE:
			watchflag ^= 2;
			prevlines = -1;
			break;

		case IDM_MEM_SUM_NONE:
		case IDM_MEM_SUM_KOUGAKUSHA:
		case IDM_MEM_SUM_ASCII:
		case IDM_MEM_SUM_DEMPA:
			sumtype = LOWORD(wp) == IDM_MEM_SUM_KOUGAKUSHA ? sum_kougakusha
					: LOWORD(wp) == IDM_MEM_SUM_ASCII ? sum_ascii
					: LOWORD(wp) == IDM_MEM_SUM_DEMPA ? sum_dempa : sum_none;
			SaveMonitorConfig();
			Update();
			break;
		}
		break;

	case WM_MOUSEMOVE:
		// ラベルを付けたウォッチの上ならツールチップ (M88gn)
		p.x = (short) LOWORD(lp);
		p.y = (short) HIWORD(lp);
		if (!tiptracking)
		{
			TRACKMOUSEEVENT tme;
			tme.cbSize = sizeof(tme);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = hdlg;
			tme.dwHoverTime = 0;
			tiptracking = !!TrackMouseEvent(&tme);
		}
		UpdateLabelTip(p, true);
		{
			POINT tp = p;
			int a = GetTextPos(&tp) ? AddrAtPos(tp) : -1;
			ShowCursorAddr(a);
			// 行に下線、桁の右に縦線 (16 進の欄と文字の欄の両方)
			int row = (tp.y >= 1 && tp.y < GetHeight()) ? tp.y : -1;
			if (a >= 0)
			{
				int c = a & 15;
				int asc = 6 + 48 + (sumtype != sum_none ? 5 : 0);
				SetGuide(row, 6 + 3 * c + 2, asc + c + 1);	// 16 進の欄は値のすぐ右 (値は 6 + 3 * c から 2 字)
			}
			else
				SetGuide(row);
		}
		break;

	case WM_MOUSELEAVE:
		tiptracking = false;
		p.x = p.y = 0;
		UpdateLabelTip(p, false);
		ShowCursorAddr(-1);
		SetGuide(-1);
		break;

	case WM_MOUSEWHEEL:
	case WM_VSCROLL:
		p.x = p.y = 0;
		UpdateLabelTip(p, false);		// 表示がずれるので消す (次にマウスを動かせば出し直す)
		ShowCursorAddr(-1);
		break;

	case WM_DESTROY:
		hwndtip = 0;					// 持ち主の窓といっしょに壊れる
		tipaddr = -1;
		tiptracking = false;
		break;

	case WM_RBUTTONUP:
		// データの上の右クリックのメニュー (M88gn)
		p.x = (short) LOWORD(lp);
		p.y = (short) HIWORD(lp);
		if (GetTextPos(&p))
			ShowDataMenu(hdlg, p);
		return TRUE;

	case WM_LBUTTONDBLCLK:
		p.x = LOWORD(lp);
		p.y = HIWORD(lp);
		
		if (GetTextPos(&p))
		{
			// 0行目は列見出しなので編集対象にしない
			if (p.y >= 1 && 6 <= p.x && p.x <= 5+48)
			{
				editaddr = (GetLine() + p.y - 1) * 16 + (p.x - 6) / 3;
				if (editaddr < 0x10000)
				{
					DialogBoxParam(GetHInst(), MAKEINTRESOURCE(IDD_MEMORY_EDIT), 
								hdlg, DLGPROC((void*)EDlgProcGate), (LPARAM)this);
				}
			}
		}
		return 0;
	
	case WM_KEYDOWN:
		if (wp == 'G' && (GetKeyState(VK_CONTROL) & 0x8000))
		{
			GotoAddress();
			return 0;
		}
		// 検索 (M88gn): Ctrl+F、F3 / Shift+F3
		if (wp == 'F' && (GetKeyState(VK_CONTROL) & 0x8000))
		{
			SendMessage(hdlg, WM_COMMAND, IDM_MEM_FIND, 0);
			return 0;
		}
		if (wp == VK_F3)
		{
			SendMessage(hdlg, WM_COMMAND, (GetKeyState(VK_SHIFT) & 0x8000) ? IDM_MEM_FINDPREV : IDM_MEM_FINDNEXT, 0);
			return 0;
		}
		// Ctrl+英字は16進入力として食わない(Ctrl+C を基底へ通すため)
		if (GetKeyState(VK_CONTROL) & 0x8000)
			break;

		EnableStatus(true);
		if (VK_NUMPAD0 <= wp && wp <= VK_NUMPAD9)
			wp -= VK_NUMPAD0 - '0';

		if (wp == VK_RETURN)
		{
			ExecCommand();
		}
		else if (wp == VK_BACK)
		{
			size_t l = strlen(line);
			if (l)
				line[l-1] = 0;
			if (l == 2)
				line[0] = 0;
			PutStatus(line);
		}
		else if (isdigit(wp) | isupper(wp) | (wp == ' '))
		{
			size_t l = strlen(line);
			if (l < sizeof(line) - 1)
			{
				line[l] = wp;
				if (l > 0)
					line[l+1] = 0;
				else
					line[l+1] = ' ', line[l+2] = 0;
			}
			PutStatus(line);
		}
		Update();
		break;
	}
	return MemViewMonitor::DlgProc(hdlg, msg, wp, lp);
}


void MemoryMonitor::ExecCommand()
{
	PutStatus(0);
	switch (line[0])
	{
	case '1': case '2': case '3': case '4':
		Search(strtoul(line+1, 0, 0), line[0] - '0');
		break;

	case 0:
		memset(stat, 1, 0x10000);
		break;

	default:
		break;
	}
	line[0] = 0;
}

void MemoryMonitor::Search(uint key, int bytes)
{
	int match = 0;
	int mask = 0xffffffff >> (8 * (4 - bytes));
	int end = 0x10000 - bytes;

	for (int i=0; i<end; i++)
	{
		int data = 0;
		for (int j=bytes-1; j>=0; j--)
		{
			data = (data << 8) | GetBus()->Read8(i+j);
		}

		if (stat[i])
		{
			if (!((data ^ key) & mask))
			{
				if (!match++)
					SetLine(i / 16);
				for (int j=0; j<bytes; j++)
					stat[i++] = 0xc0;
				i--;
			}
			else
				stat[i] = 0;
		}
	}
	if (match)
	{
		PutStatus("%d hits", match);
	}
	else
	{
		PutStatus("not found");
		memset(stat, 1, 0x10000);
	}
}

// ----------------------------------------------------------------------------
//	検索 (M88gn)。今表示しているバンク (Save Range と同じ) の中を、文字列か 1～16 バイトの並びで探す
//
bool MemoryMonitor::Find()
{
	INT_PTR r = DialogBoxParam(GetHInst(), MAKEINTRESOURCE(IDD_MEM_SEARCH), GetHWnd(),
							   DLGPROC((void*)SDlgProcGate), (LPARAM)this);
	if (r == IDOK)
	{
		FindPattern(searchforward);
		return true;
	}
	if (r == IDC_SEARCH_BREAK)
		SetPatternBreak();
	return false;
}

// 検索のダイアログの Set Breakpoint: 入れたパターンで、パターンのブレークの窓を開く。
// バンクは見つけた所 (無ければ窓の先頭) に出しているもの (ROM なら RAM)
void MemoryMonitor::SetPatternBreak()
{
	Debugger::BreakPoint bp;
	bp.pattern = true;
	bp.enabled = true;
	bp.cpu = uint8(ShownCpu());
	int bank = ShownBank(foundaddr >= 0 ? uint(foundaddr) : uint(GetLine() * 16) & 0xffff);
	if (!IsPatternBank(bank))
		bank = MemoryViewer::mainram;
	bp.patbank = uint8(bank);
	if (bp.cpu == Debugger::cpu_sub)
		bp.addr = 0x4000, bp.length = 0x4000;
	else
	{
		const WatchBank* b = FindWatchBank(bank);
		bp.addr = b->lo;
		bp.length = b->hi - b->lo + 1;
	}
	bp.patlen = uint8(search.len);
	memcpy(bp.pat, search.pat, search.len);
	bp.patflags = uint8((search.hex ? Debugger::pat_hex : 0) | (search.CaseSensitive() ? 0 : Debugger::pat_nocase));
	if (EditPatternBreak(GetHWnd(), GetHInst(), core, -1, &bp) >= 0)
	{
		EnableStatus(true);
		PutStatus("Pattern breakpoint set");
	}
}

void MemoryMonitor::ShowMatch(uint addr, uint len)
{
	if (!IsOpen())
		return;
	foundaddr = int(addr & 0xffff);
	foundlen = int(len);
	int top = GetLine() * 16;
	int rows = Max(GetHeight() - 1, 1);
	if (foundaddr < top || foundaddr + foundlen > top + rows * 16)
		JumpTo(foundaddr);
	Update();
}

BOOL MemoryMonitor::SDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM)
{
	switch (msg)
	{
	case WM_INITDIALOG:
		search.InitDialog(hdlg);
		return FALSE;

	case WM_COMMAND:
		{
			int id = LOWORD(wp);
			int code = HIWORD(wp);
			if (search.Command(hdlg, id, code))
				break;
			if ((id == IDOK || id == IDC_SEARCH_PREV) && code == BN_CLICKED)
			{
				if (search.Read(hdlg, "Search"))
				{
					searchforward = id == IDOK;
					EndDialog(hdlg, IDOK);
				}
				return TRUE;
			}
			if (id == IDC_SEARCH_BREAK && code == BN_CLICKED)
			{
				if (search.Read(hdlg, "Search"))
					EndDialog(hdlg, IDC_SEARCH_BREAK);
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

BOOL CALLBACK MemoryMonitor::SDlgProcGate(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
	MemoryMonitor* mon = 0;
	if (m == WM_INITDIALOG)
	{
		mon = reinterpret_cast<MemoryMonitor*>(l);
		if (mon)
			::SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) mon);
	}
	else
		mon = (MemoryMonitor*) ::GetWindowLongPtr(hwnd, GWLP_USERDATA);
	return mon ? mon->SDlgProc(hwnd, m, w, l) : FALSE;
}

bool MemoryMonitor::PatternAt(const uint8* p)
{
	for (int i = 0; i < search.len; i++)
	{
		uint8 a = p[i], b = search.pat[i];
		if (a != b)
		{
			if (search.CaseSensitive())
				return false;
			if (a >= 'a' && a <= 'z')
				a -= 0x20;
			if (b >= 'a' && b <= 'z')
				b -= 0x20;
			if (a != b)
				return false;
		}
	}
	return true;
}

// 見つけた所 (窓に見えていなければ窓の先頭) の次 (前) から探す。末尾 (先頭) まで行ったら反対の端から続ける
void MemoryMonitor::FindPattern(bool forward)
{
	int patternlen = search.len;
	if (patternlen <= 0)
		return;

	uint8* img = new uint8[0x10000];
	core->Lock();
	core->GetDebugger()->SetSuppressBreak(true);
	for (int a = 0; a < 0x10000; a++)
		img[a] = GetBus()->Read8(a);
	core->GetDebugger()->SetSuppressBreak(false);
	core->Unlock();

	int top = GetLine() * 16;
	int rows = Max(GetHeight() - 1, 1);
	bool visible = foundaddr >= top && foundaddr < top + rows * 16;
	int start = visible ? foundaddr + (forward ? 1 : -1) : (forward ? top : top - 1);

	int n = 0x10000 - patternlen + 1;		// 探し始められる番地の数 (FFFF を越えて 0000 にはつながない)
	int s0 = ((start % n) + n) % n;
	int hit = -1;
	bool wrapped = false;
	for (int i = 0; i < n; i++)
	{
		int a = forward ? s0 + i : s0 - i;
		a = ((a % n) + n) % n;
		if (PatternAt(img + a))
		{
			hit = a;
			wrapped = forward ? a < s0 : a > s0;
			break;
		}
	}
	delete[] img;

	// 探しているものを status に出す字 (16 進は 2 桁ずつ、文字列は "...")
	char what[PatternInput::textmax * 3 + 8];
	search.Describe(what);

	EnableStatus(true);
	if (hit >= 0)
	{
		foundaddr = hit;
		foundlen = patternlen;
		// 見えていなければ移る
		if (hit < top || hit + patternlen > top + rows * 16)
			JumpTo(hit);
		PutStatus("%s: %.4X%s", what, hit, wrapped ? (forward ? " (wrapped to top)" : " (wrapped to end)") : "");
	}
	else
	{
		foundaddr = -1;
		PutStatus("%s: not found", what);
	}
	Update();
}

// ----------------------------------------------------------------------------
//	イメージを書き込む
//	
bool MemoryMonitor::SaveImage()
{
	// ダイアログ
	OFNV5 ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = WINVAR(OFNSIZE);
	ofn.FlagsEx = 0;		// 位置バーを消す指定 (OFN_EX_NOPLACESBAR) を付けると、Windows が古い形のダイアログを出す (DPI 対応の窓ではツールバーのアイコンが小さい)

	char filename[MAX_PATH];
	filename[0] = 0;
	
	ofn.hwndOwner = GetHWnd();
	ofn.lpstrFilter = "binary image (*.bin)\0*.bin\0"
					  "All Files (*.*)\0*.*\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_CREATEPROMPT;
	ofn.lpstrDefExt = "bin";
	ofn.lpstrTitle = "Save Image";
	
	if (!GetSaveFileName(&ofn))
		return false;

	// 書き込み
	FileIO fio(filename, FileIO::create);
	// 括弧が要る。! は & より優先度が高いので、
	// 「!fio.GetFlags() & FileIO::open」だと (!flags) & open の意味になる
	if (!(fio.GetFlags() & FileIO::open))
		return false;
	
	uint8* img = new uint8[0x10000];
	if (!img)
		return false;

	for (int a=0; a<0x10000; a++)
		img[a] = GetBus()->Read8(a);

	fio.Write(img, 0x10000);
	delete[] img;
	return true;
}

// ---------------------------------------------------------------------------
//	選択中のバンクの 0000-ffff 全体をクリップボードへコピーする(仕様変更。
//	docs/spec-phase10a-fix.md F-3)。書式は画面表示(UpdateText)と同じ。
//
//	Lock() は要る。64KB 読む間に VM が走っていると、時点の食い違った
//	内容が混ざるため。
//
//	SetSuppressBreak() の方は念のためで、実際には効いていない。
//	GetBus() は MemoryViewer の私設バス(生ポインタと RdGV*)で、VM の
//	MemoryManager を通らないので Debugger のメモリフックに届かない
//	(docs/spec-phase5-debug-ui.md §5.3 / docs/TODO.md の T-6)。
//	ブレークが出るのは MemoryManager 経由で読む場合だけ。
//
bool MemoryMonitor::CopyToClipboard()
{
	TextBuffer tb;

	tb.Add("      ");
	for (int h = 0; h < 16; h++)
		tb.Addf(" %x ", h);
	if (sumtype != sum_none)
		tb.Add("sum  ");
	tb.Add("0123456789abcdef\r\n");

	core->Lock();
	core->GetDebugger()->SetSuppressBreak(true);
	for (int a = 0; a < 0x10000; )
	{
		char mem[17];
		uint8 row[16];
		int a0 = a;
		tb.Addf("%.4x: ", a);
		for (int x = 0; x < 16; x++)
		{
			int d = GetBus()->Read8(a);
			tb.Addf("%.2x ", d);
			mem[x] = d >= 0x20 && d < 0xfd && d != 0x7f ? d : '.';
			row[x] = d;
			a++;
		}
		if (sumtype != sum_none)
			tb.Addf(":%.2x  ", RowSum(sumtype, a0, row));
		mem[16] = 0;
		tb.Add(mem);
		tb.Add("\r\n");
	}
	core->GetDebugger()->SetSuppressBreak(false);
	core->Unlock();

	return SetClipboardText(tb.Get(), tb.Length());
}

// ---------------------------------------------------------------------------
//	ウォッチ (M88gn)。表示中の CPU は、0000 のバンクが Sub ならサブ (コードの窓と同じ)
//
Debugger::CpuTarget MemoryMonitor::ShownCpu()
{
	return GetA0() == MemoryViewer::sub ? Debugger::cpu_sub : Debugger::cpu_main;
}

// その番地に今出しているバンク。メモリの窓の 0000/6000/c000/f000 のメニューの割り当てと同じ
int MemoryMonitor::ShownBank(uint addr)
{
	if (GetA0() != MemoryViewer::sub)
		return ShownBankAt(addr);
	if (addr < 0x6000)
		return GetA0();
	if (addr < 0x8000)
		return GetShown6();
	if (addr < 0xc000)
		return MemoryViewer::mainram;
	MemoryViewer::Type ac = GetAc();
	if (ac == MemoryViewer::gvram0 || ac == MemoryViewer::gvram1 || ac == MemoryViewer::gvram2)
		return ac;
	if (addr < 0xf000)
		return MemoryViewer::mainram;
	return GetAf();
}

// ウォッチがその番地 (今出しているバンクの) を含むか。サブ CPU のウォッチはバンクを見ない
bool MemoryMonitor::WatchMatches(const Debugger::WatchEntry& w, uint addr)
{
	if (w.cpu != ShownCpu())
		return false;
	if (((addr - w.addr) & 0xffff) >= uint(w.size ? w.size : 1))
		return false;
	return w.cpu == Debugger::cpu_sub || w.bank == ShownBank(addr);
}

int MemoryMonitor::WatchAt(uint addr)
{
	Debugger* dbg = core->GetDebugger();
	for (int i = 0; i < dbg->GetWatchCount(); i++)
	{
		Debugger::WatchEntry w;
		if (dbg->GetWatch(i, &w) && WatchMatches(w, addr))
			return i;
	}
	return -1;
}

// その番地を含むウォッチがあれば消し、無ければ 1 バイトのウォッチを足す
void MemoryMonitor::ToggleWatch(uint addr)
{
	core->Lock();
	Debugger* dbg = core->GetDebugger();
	int i = WatchAt(addr);
	if (i >= 0)
		dbg->DeleteWatch(i);
	else
	{
		Debugger::WatchEntry w;
		memset(&w, 0, sizeof(w));
		w.addr = addr & 0xffff;
		w.size = 1;
		w.cpu = uint8(ShownCpu());
		// 今出しているバンクで足す (サブ CPU はバンクを使わないので RAM にしておく)
		w.bank = uint8(w.cpu == Debugger::cpu_sub ? MemoryViewer::mainram : ShownBank(addr));
		dbg->AddWatch(w);
	}
	core->Unlock();
	Update();
}

// 文字の位置 (GetTextPos の後) のデータの番地。16 進の欄か文字の欄の上でなければ -1
int MemoryMonitor::AddrAtPos(POINT p)
{
	if (p.y < 1)
		return -1;				// 0 行目は列の見出し
	int col = -1;
	if (6 <= p.x && p.x < 6 + 48)
		col = (p.x - 6) / 3;
	else
	{
		int asc = 6 + 48 + (sumtype != sum_none ? 5 : 0);	// 文字の欄の始まり
		if (asc <= p.x && p.x < asc + 16)
			col = p.x - asc;
	}
	if (col < 0)
		return -1;
	int addr = (GetLine() + p.y - 1) * 16 + col;
	return addr < 0x10000 ? addr : -1;
}

// ラベルの付いたデータを角丸の箱で囲む (M88gn)。画面に写した後に WinMonitor::Draw から呼ばれる
void MemoryMonitor::DrawOverlay(HDC hdc)
{
	if (labelboxes.empty())
		return;
	int fw = GetFontWidth(), fh = GetFontHeightPx();
	int th = Max(1, fh / 16);
	HPEN hpen = CreatePen(PS_SOLID, th, RGB(0xff, 0xe0, 0x60));
	HPEN holdpen = (HPEN) SelectObject(hdc, hpen);
	HBRUSH holdbr = (HBRUSH) SelectObject(hdc, GetStockObject(NULL_BRUSH));
	int mx = fw / 4, r = fw * 2 / 3;
	for (size_t i = 0; i < labelboxes.size(); i++)
	{
		const LabelBox& b = labelboxes[i];
		// 字に掛からないよう少し上に広げ (ユーザーの要望)、描いた後に字を書き直して字を上にする
		int l = b.x * fw - mx, t = b.y * fh - fh / 8;
		int rr = (b.x + b.n) * fw + mx, bb = (b.y + 1) * fh - th / 2;
		RoundRect(hdc, l, t, rr, bb, r, r);
		RedrawCellText(hdc, b.x, b.y, b.n);
	}
	SelectObject(hdc, holdbr);
	SelectObject(hdc, holdpen);
	DeleteObject(hpen);
}

// メニューバーの Addr の項目に、マウスの下のデータの番地を出す (M88gn)。変わったときだけ書き直す
void MemoryMonitor::ShowCursorAddr(int addr)
{
	if (addr == curaddr)
		return;
	HWND hwnd = GetHWnd();
	HMENU hmenu = hwnd ? GetMenu(hwnd) : 0;
	if (!hmenu)
		return;
	curaddr = addr;
	char text[64];
	const char* label = addr >= 0 ? core->GetDebugger()->GetLabel(ShownCpu(), addr, ShownBankAt(addr)) : 0;
	if (label)
		wsprintf(text, "Addr: %.4X  %s", addr, label);
	else if (addr >= 0)
		wsprintf(text, "Addr: %.4X", addr);
	else
		lstrcpy(text, "Addr: ----");
	addritem.SetText(text);
}




// ラベルを付けたウォッチの番地の上なら、ラベルのツールチップを出す。p はクライアント座標
void MemoryMonitor::UpdateLabelTip(POINT p, bool inside)
{
	int addr = -1;
	Debugger::WatchEntry w;
	const char* label = 0;		// 番地のラベル (M88gn。ウォッチのラベルより先に出す)
	if (inside && GetTextPos(&p))
	{
		addr = AddrAtPos(p);
		label = addr >= 0 ? core->GetDebugger()->GetLabel(ShownCpu(), addr, ShownBankAt(addr)) : 0;
		int i = addr >= 0 && !label ? WatchAt(addr) : -1;
		if (!label && (i < 0 || !core->GetDebugger()->GetWatch(i, &w) || !w.label[0]))
			addr = -1;
	}

	if (addr < 0)
	{
		if (hwndtip && tipaddr >= 0)
			SendMessage(hwndtip, TTM_TRACKACTIVATE, FALSE, 0);
		tipaddr = -1;
		return;
	}
	if (addr == tipaddr)
		return;					// 同じ番地なら出したまま

	HWND hwnd = GetHWnd();
	if (!hwndtip)
	{
		hwndtip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, 0, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
								 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
								 hwnd, 0, GetHInst(), 0);
		if (!hwndtip)
			return;
		TOOLINFO ti;
		memset(&ti, 0, sizeof(ti));
		ti.cbSize = TTTOOLINFOA_V1_SIZE;
		ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
		ti.hwnd = hwnd;
		ti.uId = 1;
		ti.lpszText = (LPSTR) "";
		SendMessage(hwndtip, TTM_ADDTOOL, 0, (LPARAM) &ti);
	}

	char text[96];
	if (label)
		wsprintf(text, "%s  (%.4X%s)", label, addr, ShownCpu() == Debugger::cpu_sub ? " Sub CPU" : "");
	else if (w.cpu == Debugger::cpu_sub)
		wsprintf(text, "%s  (%.4X Sub CPU)", w.label, w.addr);
	else
		wsprintf(text, "%s  (%.4X %s)", w.label, w.addr, WatchBankName(w.bank));

	TOOLINFO ti;
	memset(&ti, 0, sizeof(ti));
	ti.cbSize = TTTOOLINFOA_V1_SIZE;
	ti.hwnd = hwnd;
	ti.uId = 1;
	ti.lpszText = text;
	SendMessage(hwndtip, TTM_UPDATETIPTEXT, 0, (LPARAM) &ti);

	POINT sp;
	GetCursorPos(&sp);
	SendMessage(hwndtip, TTM_TRACKPOSITION, 0, MAKELPARAM(sp.x + 12, sp.y + 20));
	SendMessage(hwndtip, TTM_TRACKACTIVATE, TRUE, (LPARAM) &ti);
	tipaddr = addr;
}

// データ (16 進の欄か文字の欄) の上で右クリックしたときのメニュー
void MemoryMonitor::ShowDataMenu(HWND hdlg, POINT p)
{
	int a = AddrAtPos(p);
	if (a < 0)
		return;
	uint addr = uint(a);

	// メニューに出す番地 (M88gn): ラベルがあれば「名前(1234H)」、無ければ「1234H」(括弧で囲む項目は「(1234H)」)。
	// ラベルは画面と同じ決め方 (表示中のバンクのものを先に、無ければ Any)
	int lcpu = ShownCpu();
	const char* mlabel = core->GetDebugger()->GetLabel(lcpu, addr, ShownBankAt(addr));
	char at[64], pat[64];
	if (mlabel)
	{
		wsprintf(at, "%s(%.4XH)", mlabel, addr);
		lstrcpy(pat, at);
	}
	else
	{
		wsprintf(at, "%.4XH", addr);
		wsprintf(pat, "(%.4XH)", addr);
	}

	bool watched = WatchAt(addr) >= 0;
	char buf[96];
	wsprintf(buf, watched ? "Remove &Watch %s" : "Add &Watch %s", pat);

	enum { cmd_watch = 1, cmd_bread, cmd_bwrite, cmd_code, cmd_label, cmd_unlabel };
	HMENU hmenu = CreatePopupMenu();
	AppendMenu(hmenu, MF_STRING, cmd_watch, buf);
	AppendMenu(hmenu, MF_SEPARATOR, 0, 0);
	// 読み・書きのブレークポイント (M88gn)。チェックが付いていれば、選ぶと外す
	wsprintf(buf, "Break on &Read %s", pat);
	AppendMenu(hmenu, MF_STRING | (FindMemBreak(addr, false) >= 0 ? MF_CHECKED : 0), cmd_bread, buf);
	wsprintf(buf, "Break on Wri&te %s", pat);
	AppendMenu(hmenu, MF_STRING | (FindMemBreak(addr, true) >= 0 ? MF_CHECKED : 0), cmd_bwrite, buf);
	AppendMenu(hmenu, MF_SEPARATOR, 0, 0);
	wsprintf(buf, "Show %s in &Code", at);
	AppendMenu(hmenu, MF_STRING | (codemon ? 0 : MF_GRAYED), cmd_code, buf);
	// ラベル (M88gn)
	AppendMenu(hmenu, MF_SEPARATOR, 0, 0);
	int lbank = lcpu ? -1 : ShownBank(addr);
	bool haslabel = core->GetDebugger()->GetLabel(lcpu, addr, lbank, &lbank) != 0;
	if (lbank < 0)
		lbank = Debugger::label_anybank;
	wsprintf(buf, haslabel ? "Edit &Label %s..." : "Set &Label %s...", pat);
	AppendMenu(hmenu, MF_STRING, cmd_label, buf);
	AppendMenu(hmenu, MF_STRING | (haslabel ? 0 : MF_GRAYED), cmd_unlabel, "&Remove Label");

	POINT sp;
	GetCursorPos(&sp);
	int cmd = TrackPopupMenu(hmenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, sp.x, sp.y, 0, hdlg, 0);
	DestroyMenu(hmenu);

	if (cmd == cmd_watch)
		ToggleWatch(addr);
	else if (cmd == cmd_bread || cmd == cmd_bwrite)
		ToggleMemBreak(addr, cmd == cmd_bwrite);
	else if (cmd == cmd_code && codemon)
		codemon->ShowAddress(addr, GetHInst(), GetWindow(hdlg, GW_OWNER));
	else if (cmd == cmd_label || cmd == cmd_unlabel)
	{
		if (cmd == cmd_unlabel)
			core->GetDebugger()->SetLabel(lcpu, addr, lbank, 0);
		else
			EditLabel(hdlg, GetHInst(), core->GetDebugger(), lcpu, addr, lbank);
		curaddr = -2;			// メニューバーの Addr の欄を書き直す
		tipaddr = -1;
		Update();
	}
	// ラベルを変えたら、コードの窓も書き直す
	if ((cmd == cmd_label || cmd == cmd_unlabel) && codemon)
		codemon->Update();
}

// ---------------------------------------------------------------------------
//	ダンプからのブレークポイント (M88gn)
//
int MemoryMonitor::MarkAt(uint addr)
{
	// 1 = ウォッチ、2 = 有効なブレークポイント (両方なら 3)、5 = 無効なブレークポイントだけ、
	// 6 = ウォッチと無効なブレークポイント (M88gn。読み・書きのどちらかが有効なら有効の色)
	int m = 0;
	bool disabled = false;
	for (size_t w = 0; w < watchshown.size() && !(m & 1); w++)
		if (WatchMatches(watchshown[w], addr))
			m |= 1;
	for (size_t b = 0; b < bpshown.size() && !(m & 2); b++)
	{
		const Debugger::BreakPoint& bp = bpshown[b];
		if (addr >= bp.addr && addr < bp.addr + bp.length)
		{
			if (bp.enabled)
				m |= 2;
			else
				disabled = true;
		}
	}
	if (!(m & 2) && disabled)
		m = (m & 1) ? 6 : 5;
	return m;
}

int MemoryMonitor::FindMemBreak(uint addr, bool write)
{
	Debugger* dbg = core->GetDebugger();
	Debugger::CpuTarget cpu = ShownCpu();
	for (int i = 0; i < dbg->GetBreakPointCount(); i++)
	{
		Debugger::BreakPoint bp;
		if (dbg->GetBreakPoint(i, &bp) && bp.cpu == cpu && !bp.io && (write ? bp.write : bp.read)
			&& addr >= bp.addr && addr < bp.addr + bp.length)
			return i;
	}
	return -1;
}

// 読み (書き) のブレークポイントを切り替える。addr を含むものがあれば、その読み (書き) を外し
// (実行・読み・書きのどれも無くなれば消す)、無ければ 1 バイトのブレークポイントを足す
void MemoryMonitor::ToggleMemBreak(uint addr, bool write)
{
	core->Lock();
	Debugger* dbg = core->GetDebugger();
	int i = FindMemBreak(addr, write);
	if (i >= 0)
	{
		Debugger::BreakPoint bp;
		dbg->GetBreakPoint(i, &bp);
		if (write)
			bp.write = false;
		else
			bp.read = false;
		if (!bp.exec && !bp.read && !bp.write)
			dbg->DeleteBreakPoint(i);
		else
			dbg->SetBreakPoint(i, bp);
	}
	else
	{
		Debugger::BreakPoint bp;
		memset(&bp, 0, sizeof(bp));
		bp.addr = addr & 0xffff;
		bp.length = 1;
		bp.cpu = uint8(ShownCpu());
		bp.read = !write;
		bp.write = write;
		bp.enabled = true;
		dbg->AddBreakPoint(bp);
	}
	core->Unlock();
	Update();
}

// ---------------------------------------------------------------------------
//	範囲の保存・読み込み (M88gn)。今表示しているバンクのメモリを、番地の範囲で書き出す・読み込む
//
bool MemoryMonitor::AskRange(bool load)
{
	rangeload = load;
	return DialogBoxParam(GetHInst(), MAKEINTRESOURCE(IDD_MEM_RANGE), GetHWnd(),
						  DLGPROC((void*)RDlgProcGate), (LPARAM)this) == IDOK;
}

BOOL MemoryMonitor::RDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM)
{
	char buf[64];
	switch (msg)
	{
	case WM_INITDIALOG:
		SetWindowText(hdlg, rangeload ? "Load Range" : "Save Range");
		wsprintf(buf, "%.4x", rangestart);
		SetDlgItemText(hdlg, IDC_RANGE_START, buf);
		wsprintf(buf, "%.4x", rangeend);
		SetDlgItemText(hdlg, IDC_RANGE_END, buf);
		// 読み込みは、終わりをファイルの大きさで決める
		EnableWindow(GetDlgItem(hdlg, IDC_RANGE_END), !rangeload);
		SetFocus(GetDlgItem(hdlg, IDC_RANGE_START));
		SendDlgItemMessage(hdlg, IDC_RANGE_START, EM_SETSEL, 0, -1);
		return FALSE;

	case WM_COMMAND:
		if (LOWORD(wp) == IDOK)
		{
			// 16 進かラベルの名前 (M88gn)
			uint s, e = rangeend;
			GetDlgItemText(hdlg, IDC_RANGE_START, buf, sizeof(buf));
			if (!ParseAddrInput(buf, &s))
				return TRUE;
			s &= 0xffff;
			if (!rangeload)
			{
				GetDlgItemText(hdlg, IDC_RANGE_END, buf, sizeof(buf));
				if (!ParseAddrInput(buf, &e))
					return TRUE;
				e &= 0xffff;
				if (e < s)
				{
					MessageBox(hdlg, "終わりの番地が始まりより前です。", "Save Range", MB_OK | MB_ICONWARNING);
					return TRUE;
				}
			}
			rangestart = s, rangeend = e;
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

BOOL CALLBACK MemoryMonitor::RDlgProcGate(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
	MemoryMonitor* mon = 0;
	if (m == WM_INITDIALOG)
	{
		mon = reinterpret_cast<MemoryMonitor*>(l);
		if (mon)
			::SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) mon);
	}
	else
		mon = (MemoryMonitor*) ::GetWindowLongPtr(hwnd, GWLP_USERDATA);
	return mon ? mon->RDlgProc(hwnd, m, w, l) : FALSE;
}

bool MemoryMonitor::SaveRange()
{
	if (!AskRange(false))
		return false;

	OFNV5 ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = WINVAR(OFNSIZE);
	char filename[MAX_PATH];
	filename[0] = 0;
	ofn.hwndOwner = GetHWnd();
	ofn.lpstrFilter = "binary image (*.bin)\0*.bin\0All Files (*.*)\0*.*\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_OVERWRITEPROMPT;
	ofn.lpstrDefExt = "bin";
	ofn.lpstrTitle = "Save Range";
	if (!GetSaveFileName(&ofn))
		return false;

	FileIO fio(filename, FileIO::create);
	if (!(fio.GetFlags() & FileIO::open))
		return false;
	uint n = rangeend - rangestart + 1;
	uint8* img = new uint8[n];
	core->Lock();
	core->GetDebugger()->SetSuppressBreak(true);
	for (uint i = 0; i < n; i++)
		img[i] = GetBus()->Read8(rangestart + i);
	core->GetDebugger()->SetSuppressBreak(false);
	core->Unlock();
	fio.Write(img, n);
	delete[] img;
	return true;
}

bool MemoryMonitor::LoadRange()
{
	if (!AskRange(true))
		return false;

	OFNV5 ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = WINVAR(OFNSIZE);
	char filename[MAX_PATH];
	filename[0] = 0;
	ofn.hwndOwner = GetHWnd();
	ofn.lpstrFilter = "binary image (*.bin)\0*.bin\0All Files (*.*)\0*.*\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST;
	ofn.lpstrTitle = "Load Range";
	if (!GetOpenFileName(&ofn))
		return false;

	FileIO fio(filename, FileIO::readonly);
	if (!(fio.GetFlags() & FileIO::open))
		return false;
	fio.Seek(0, FileIO::end);
	uint size = fio.Tellp();
	fio.Seek(0, FileIO::begin);
	uint room = 0x10000 - rangestart;
	uint n = size < room ? size : room;
	if (n == 0)
		return false;
	if (size > room)
	{
		char msg[128];
		wsprintf(msg, "ファイルは %u バイトですが、%.4X から FFFF までの %u バイトだけ読み込みます。", size, rangestart, room);
		if (MessageBox(GetHWnd(), msg, "Load Range", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
			return false;
	}
	uint8* img = new uint8[n];
	fio.Read(img, n);
	core->Lock();
	for (uint i = 0; i < n; i++)
		GetBus()->Write8(rangestart + i, img[i]);
	core->Unlock();
	delete[] img;
	rangeend = rangestart + n - 1;
	Update();
	return true;
}

// ---------------------------------------------------------------------------
//	ダンプリストの 1 行 (16 バイト) のチェックサム (M88gn)。横だけで、縦の合計は出さない
//	工学社 (I/O・PiO)       : 16 バイトの和の下位 1 バイト
//	アスキー (ASCII・LOGiN) : 番地の上位 + 下位 + 16 バイトの和の下位 1 バイト
//	電波新聞社 (ベーマガ)   : 1 バイトごとに S = S + D (桁上がりは捨てる) の後、S を左に 1 ビット回す
//
uint MemoryMonitor::RowSum(int type, uint addr, const uint8* d)
{
	uint s = 0;
	switch (type)
	{
	case sum_kougakusha:
		for (int i = 0; i < 16; i++)
			s += d[i];
		break;

	case sum_ascii:
		s = (addr >> 8) + (addr & 0xff);
		for (int i = 0; i < 16; i++)
			s += d[i];
		break;

	case sum_dempa:
		for (int i = 0; i < 16; i++)
		{
			s = (s + d[i]) & 0xff;
			s = ((s << 1) | (s >> 7)) & 0xff;
		}
		break;
	}
	return s & 0xff;
}

// ---------------------------------------------------------------------------
//	チェックサムの方式を M88.ini に保存/復元する (既定は出さない)
//
void MemoryMonitor::LoadMonitorConfig()
{
	MemViewMonitor::LoadMonitorConfig();
	int t = GetPrivateProfileInt("MonitorMemory", "Checksum", sum_none, PC8801::GetM88IniPath());
	sumtype = (t >= 0 && t < sum_count) ? t : sum_none;
}

void MemoryMonitor::SaveMonitorConfig()
{
	MemViewMonitor::SaveMonitorConfig();
	char buf[8];
	wsprintf(buf, "%d", sumtype);
	WritePrivateProfileString("MonitorMemory", "Checksum", buf, PC8801::GetM88IniPath());
}

// ---------------------------------------------------------------------------
//	内容書き換えダイアログ表示
//
BOOL MemoryMonitor::EDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	char buf[16];
	switch (msg)
	{
	case WM_INITDIALOG:
		wsprintf(buf, "%.4x:", editaddr);
		SetDlgItemText(hdlg, IDC_MEMORY_EDIT_TEXT, buf);
		wsprintf(buf, "%.2x", GetBus()->Read8(editaddr));
		SetDlgItemText(hdlg, IDC_MEMORY_EDITBOX, buf);
		SetFocus(GetDlgItem(hdlg, IDC_MEMORY_EDITBOX));
		SendDlgItemMessage(hdlg, IDC_MEMORY_EDITBOX, EM_SETSEL, 0, -1);
		return 0;

	case WM_COMMAND:
		if (HIWORD(wp) == BN_CLICKED)
		{
			switch (LOWORD(wp))
			{
				char* dum;
			case IDOK:
				GetDlgItemText(hdlg, IDC_MEMORY_EDITBOX, buf, 5);
				if (isxdigit(buf[0]))
					GetBus()->Write8(editaddr, strtoul(buf, &dum, 16));
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

BOOL CALLBACK MemoryMonitor::EDlgProcGate
(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
	MemoryMonitor* memmon = 0;

	if ( m == WM_INITDIALOG ) {
		memmon = reinterpret_cast<MemoryMonitor*>(l);
		if (memmon) {
			::SetWindowLongPtr( hwnd, GWLP_USERDATA, (LONG_PTR)memmon );
		}
	} else {
		memmon = (MemoryMonitor*)::GetWindowLongPtr( hwnd, GWLP_USERDATA );
	}

	if (memmon) {
		return memmon->EDlgProc(hwnd, m, w, l);
	} else {
		return FALSE;
	}
}

