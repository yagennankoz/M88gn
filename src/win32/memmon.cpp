// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1999, 2000.
// ---------------------------------------------------------------------------
//	$Id: memmon.cpp,v 1.15 2003/05/19 02:33:56 cisc Exp $

#include "headers.h"
#include "resource.h"
#include "pc88/pc88.h"
#include "memmon.h"
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

	SetTxCol(0xc0c0c0);
	SetBkCol(RGB(0, 0, 0x50));
	Puts("      ");
	for (int h = 0; h < 16; h++)
		Putf(" %x ", h);
	Puts("0123456789abcdef\n");
	SetBkCol(RGB(0, 0, 0x20));

	for (int y=0; y<rows; y++)
	{
		if (a < 0x10000)
		{
			int x;

			SetTxCol(0xffffff);
			Putf("%.4x: ", (a & 0xffff));
			buf[2] = 0;

			for (x=0; x<16; x++)
			{
				int d = GetBus()->Read8(a);
				
				if (watchflag)
				{
					uint t = 0xff;
					if (access[a])
					{
						t = time - access[a];
						t = t < 0xfe ? t : 0xfe;
					}
					SetTxCol(col[t]);
				}
				SetBkCol(RGB(StatExec(a), stat[a], 0x20));
				Putf("%.2x", d);
				SetBkCol(RGB(0, 0, 0x20));
				Puts(" ");
				
				mem[x] = d >= 0x20 && d < 0xfd && d != 0x7f ? d : '.'; 
				a++;
			}
			SetTxCol(0xffffff);
			Putf("%.16s\n", mem);
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
	switch (msg)
	{
		POINT p;
	
	case WM_INITMENU:
		{
			HMENU hmenu = (HMENU) wp;
			CheckMenuItem(hmenu, IDM_MEM_READ,  (watchflag & 1) ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_MEM_WRITE, (watchflag & 2) ? MF_CHECKED : MF_UNCHECKED);
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wp))
		{
		case IDM_MEM_SAVEIMAGE:
			SaveImage();
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
		}
		break;

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
	tb.Add("0123456789abcdef\r\n");

	core->Lock();
	core->GetDebugger()->SetSuppressBreak(true);
	for (int a = 0; a < 0x10000; )
	{
		char mem[17];
		tb.Addf("%.4x: ", a);
		for (int x = 0; x < 16; x++)
		{
			int d = GetBus()->Read8(a);
			tb.Addf("%.2x ", d);
			mem[x] = d >= 0x20 && d < 0xfd && d != 0x7f ? d : '.';
			a++;
		}
		mem[16] = 0;
		tb.Add(mem);
		tb.Add("\r\n");
	}
	core->GetDebugger()->SetSuppressBreak(false);
	core->Unlock();

	return SetClipboardText(tb.Get(), tb.Length());
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

