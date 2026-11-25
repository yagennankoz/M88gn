// ---------------------------------------------------------------------------
//	M88 - PC-8801 emulator
//	バックトレース表示ウインドウ
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#include "headers.h"
#include "resource.h"
#include "pc88/pc88.h"
#include "tracemon.h"
#include "misc.h"
#include "labeledit.h"
#include "88config.h"
#include "winvars.h"

using namespace PC8801;

// ---------------------------------------------------------------------------
//	構築/消滅
//
TraceMonitor::TraceMonitor()
: core(0), ascending(false)
{
}

TraceMonitor::~TraceMonitor()
{
}

// ---------------------------------------------------------------------------
//	初期化
//
bool TraceMonitor::Init(WinCore* wincore)
{
	core = wincore;
	// レジスタ列を足したぶん1行が長いので、既定の桁数を広げる(60→125程度。
	// 裏レジスタ・I/R/IM/IFF を一覧に足した G-2 でさらに広げた)
	if (!WinMonitor::Init(MAKEINTRESOURCE(IDD_TRACEMON), "MonitorTrace", 125, 16))
		return false;
	if (!diagmain.Init(core->GetMemoryAccess1()) || !diagsub.Init(core->GetMemoryAccess2()))
		return false;
	// 自動更新はしない。実行中はリングバッファが回り続けるだけで意味が無く、
	// 表示は Refresh() 経由でポーズ/ブレーク時にだけ更新する
	return true;
}

// ---------------------------------------------------------------------------
//	並び順・件数を M88.ini から読み込む(既定: 新しいものが上 / 1000件)。
//	件数は Debugger 側が実体を持つので、読み込んだ値をそのまま反映する
//
void TraceMonitor::LoadMonitorConfig()
{
	WinMonitor::LoadMonitorConfig();

	const char* ini = PC8801::GetM88IniPath();
	ascending = GetPrivateProfileInt("MonitorTrace", "Ascending", 0, ini) != 0;

	int cap = GetPrivateProfileInt("MonitorTrace", "Capacity", Debugger::tracecapacity_max, ini);
	core->Lock();
	core->GetDebugger()->SetTraceCapacity(cap);
	core->Unlock();
}

void TraceMonitor::SaveMonitorConfig()
{
	WinMonitor::SaveMonitorConfig();

	const char* ini = PC8801::GetM88IniPath();
	WritePrivateProfileString("MonitorTrace", "Ascending", ascending ? "1" : "0", ini);

	core->Lock();
	int cap = core->GetDebugger()->GetTraceCapacity();
	core->Unlock();

	char buf[8];
	wsprintf(buf, "%d", cap);
	WritePrivateProfileString("MonitorTrace", "Capacity", buf, ini);
}

// ---------------------------------------------------------------------------
//	ウインドウが実際に開かれた直後(hwnd 有効化後)に呼ばれる。
//	D列(呼び出し深さ)は SP による自己修復込みの推定値であり、正確な
//	呼び出し履歴ではないことを常時見えるところに明記しておく(仕様書§7.2)。
//	EnableStatus は内部で GetHWnd() を親にステータスバーを作るため、
//	hwnd がまだ無い Init() 側で呼んでも失敗して何も表示されない
//
void TraceMonitor::Start()
{
	EnableStatus(true);
	PutStatus("D(深さ)は目安です。JPによるテールコール等で実際の呼び出し階層とずれることがあります。");
}

// ---------------------------------------------------------------------------
//	WM_M88_DEBUGBREAK から呼ばれる
//
void TraceMonitor::Refresh()
{
	if (IsOpen())
		Update();
}

// ---------------------------------------------------------------------------
//	テキスト更新。0行目は列見出し。ascending が false(既定)なら新しいものが
//	上(-1 が直近)、true なら古いものが上(いちばん古いものが -count)。
//	記録している内容をすべて表示する(裏レジスタ・I/R/IM/IFF を含む。G-2)。
//	IFF は IFF1/IFF2 をまとめて "1:1" のように出す。R は rreg(0-6bit)と
//	rreg7(7bit)を合成した値(BuildText と同じ計算)。
//	逆アセンブルは「表示時点のメモリ」を使うので、自己書き換えコードでは
//	実行当時の内容と異なって見えることがある(仕様として許容)。
//	VM の MemoryManager を経由するので、ウォッチと同じ理由で
//	SetSuppressBreak(true) で読み出し区間を囲む(§5.3 の落とし穴)
//
void TraceMonitor::UpdateText()
{
	core->Lock();
	Debugger* dbg = core->GetDebugger();
	int count = dbg->GetTraceCount();

	SetLines(Max(1, count));
	int rows = Max(0, GetHeight() - 1);
	int top = GetLine();

	SetTxCol(0xc0c0c0);
	SetBkCol(RGB(0, 0, 0x50));
	// # 列は 100/500/1000 件の -1000 まで欠けないよう %5s/%5d にする(G-1)。
	// 切り詰め(精度指定)は付けない ― 件数が増えても桁が伸びるだけで壊れない
	Putf("%5s  %-4s  %-4s  %-4s  %-2s  %-20s  %-4s %-4s %-4s %-4s %-4s %-4s %-4s %-4s %-4s %-4s  %-2s %-2s %-2s %-3s  %s\n",
		 "#", "CPU", "PC", "SP", "D", "Disassemble",
		 "AF", "BC", "DE", "HL", "IX", "IY",
		 "AF'", "BC'", "DE'", "HL'",
		 "I", "R", "IM", "IFF", "Label");

	dbg->SetSuppressBreak(true);
	for (int y = 0; y < rows; y++)
	{
		int index = top + y;
		if (index < count)
		{
			int bufindex = ascending ? (count - 1 - index) : index;
			Debugger::TraceEntry e;
			if (dbg->GetTraceEntry(bufindex, &e))
			{
				char buf[128];
				DiagFor(e.cpu).Disassemble(e.pc, buf);

				int num = ascending ? -(count - index) : -(index + 1);

				// R レジスタは rreg(0-6bit)と rreg7(7bit)の合成(BuildText と同じ式)
				uint rcombined = (e.reg.rreg & 0x7f) | (e.reg.rreg7 & 0x80);

				SetTxCol(0xffffff);
				SetBkCol(RGB(0, 0, 0x20));
				Putf("%5d  %-4s  %.4x  %.4x  %-2d  %-20.20s  %.4x %.4x %.4x %.4x %.4x %.4x %.4x %.4x %.4x %.4x  %.2x %.2x %-2d %d:%d  ",
					 num,
					 e.cpu == Debugger::cpu_sub ? "SUB" : "MAIN",
					 e.pc, e.sp, e.depth, buf,
					 e.reg.r.w.af & 0xffff, e.reg.r.w.bc & 0xffff, e.reg.r.w.de & 0xffff,
					 e.reg.r.w.hl & 0xffff, e.reg.r.w.ix & 0xffff, e.reg.r.w.iy & 0xffff,
					 e.reg.r_af & 0xffff, e.reg.r_bc & 0xffff, e.reg.r_de & 0xffff, e.reg.r_hl & 0xffff,
					 e.reg.ireg, rcombined, e.reg.intmode,
					 e.reg.iff1 ? 1 : 0, e.reg.iff2 ? 1 : 0);
				// PC のラベル (M88gn)
				const char* tl = LabelNow(e.cpu, e.pc);
				SetTxCol(RGB(0xff, 0xe0, 0x60));
				Putf("%s\n", tl ? tl : "");
				continue;
			}
		}
		SetBkCol(RGB(0, 0, 0x20));
		Puts("\n");
	}
	dbg->SetSuppressBreak(false);
	core->Unlock();
}

// ---------------------------------------------------------------------------
//	全件をテキストにする(並び順は ascending に従う)。Ctrl+C(CopyToClipboard)
//	とファイル出力(SaveToFile)で共有する。1命令1行・タブ区切りで、
//	表計算に直接貼れる形にする(G-3)。桁揃えの空白は入れない。
//	レジスタは裏レジスタ・I/R/割り込みモード・IFF1/IFF2 まで全部出す。
//	改行は \r\n で統一する(クリップボードが \r\n を要求するため。
//	ファイルへは SaveToFile が "wb" で開いて素通しする)。
//	逆アセンブルは VM のメモリを読むので、UpdateText と同じ作法で
//	core->Lock() + SetSuppressBreak(true) の中で行う
//
bool TraceMonitor::BuildText(TextBuffer& tb)
{
	core->Lock();
	Debugger* dbg = core->GetDebugger();
	int count = dbg->GetTraceCount();

	dbg->SetSuppressBreak(true);

	tb.Add("#\tCPU\tPC\tSP\tD\tDisassemble\tAF\tBC\tDE\tHL\tIX\tIY\tAF'\tBC'\tDE'\tHL'\tI\tR\tIM\tIFF1\tIFF2\r\n");

	for (int index = 0; index < count; index++)
	{
		int bufindex = ascending ? (count - 1 - index) : index;
		Debugger::TraceEntry e;
		if (!dbg->GetTraceEntry(bufindex, &e))
			continue;

		int num = ascending ? -(count - index) : -(index + 1);

		char buf[128];
		DiagFor(e.cpu).Disassemble(e.pc, buf);

		// Z80Diag の出力はタブを含まないが空白で桁揃えされているので、
		// 行末の空白だけ落とす
		size_t len = strlen(buf);
		while (len > 0 && buf[len - 1] == ' ')
			buf[--len] = 0;

		// R レジスタは 0-6bit(rreg)と 7bit(rreg7)に分かれて保持されている
		// (Z80_x86.cpp の合成式と同じ形。仕様書 §5.4)
		uint rcombined = (e.reg.rreg & 0x7f) | (e.reg.rreg7 & 0x80);

		tb.Addf("%d\t%s\t%.4x\t%.4x\t%d\t%s\t%.4x\t%.4x\t%.4x\t%.4x\t%.4x\t%.4x\t%.4x\t%.4x\t%.4x\t%.4x\t%.2x\t%.2x\t%d\t%d\t%d\t%s\r\n",
				num, e.cpu == Debugger::cpu_sub ? "SUB" : "MAIN", e.pc, e.sp, e.depth, buf,
				e.reg.r.w.af & 0xffff, e.reg.r.w.bc & 0xffff, e.reg.r.w.de & 0xffff,
				e.reg.r.w.hl & 0xffff, e.reg.r.w.ix & 0xffff, e.reg.r.w.iy & 0xffff,
				e.reg.r_af & 0xffff, e.reg.r_bc & 0xffff, e.reg.r_de & 0xffff, e.reg.r_hl & 0xffff,
				e.reg.ireg, rcombined, e.reg.intmode,
				e.reg.iff1 ? 1 : 0, e.reg.iff2 ? 1 : 0, LabelNow(e.cpu, e.pc) ? LabelNow(e.cpu, e.pc) : "");
	}
	dbg->SetSuppressBreak(false);
	core->Unlock();

	return !tb.Failed();
}

// ---------------------------------------------------------------------------
//	Ctrl+C。10a の結論(docs/spec-phase10a-fix.md F-3)に合わせ、表示中の
//	1画面ぶんではなく全件をコピーする
//
bool TraceMonitor::CopyToClipboard()
{
	TextBuffer tb;
	BuildText(tb);
	return SetClipboardText(tb.Get(), tb.Length());
}

// ---------------------------------------------------------------------------
//	全件をファイルへ書き出す。CodeMonitor::DumpImage() のダイアログ部分を
//	真似た形。BuildText 自体が core->Lock() で読み出しを済ませるので、
//	fopen はロックの外で行う(1000件のロック区間は数十msだが、VM を
//	止める時間を最小限にするため)。
//	BuildText は改行を \r\n で組み立てているので、テキストモード("w")で
//	開くと \n がさらに \r\n に変換されて \r\r\n になってしまう。
//	必ずバイナリモード("wb")で開いて素通しすること
//
bool TraceMonitor::SaveToFile()
{
	OFNV5 ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = WINVAR(OFNSIZE);
	ofn.FlagsEx = OFN_EX_NOPLACESBAR;

	char filename[MAX_PATH];
	filename[0] = 0;

	ofn.hwndOwner = GetHWnd();
	ofn.lpstrFilter = "text file (*.txt)\0*.txt\0"
					  "All Files (*.*)\0*.*\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_CREATEPROMPT;
	ofn.lpstrDefExt = "txt";
	ofn.lpstrTitle = 0;

	if (!GetSaveFileName(&ofn))
		return false;

	TextBuffer tb;
	BuildText(tb);

	FILE* fp = fopen(filename, "wb");
	if (!fp)
		return false;

	fwrite(tb.Get(), 1, tb.Length(), fp);
	fclose(fp);
	return true;
}

// ---------------------------------------------------------------------------
//	メニューから件数を切り替える(100/500/1000)。SetTraceCapacity は
//	記録内容を捨てるので、直後に表示を更新する
//
void TraceMonitor::SetCapacity(int n)
{
	core->Lock();
	core->GetDebugger()->SetTraceCapacity(n);
	core->Unlock();
	Update();
}

// ---------------------------------------------------------------------------
//	ダイアログ処理
//
BOOL TraceMonitor::DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_COMMAND:
		switch (LOWORD(wp))
		{
		case IDM_TRACE_SAVE:
			SaveToFile();
			break;

		case IDM_TRACE_SORT_DESC:
			ascending = false;
			Update();
			break;

		case IDM_TRACE_SORT_ASC:
			ascending = true;
			Update();
			break;

		case IDM_TRACE_CAP_100:
			SetCapacity(100);
			break;

		case IDM_TRACE_CAP_500:
			SetCapacity(500);
			break;

		case IDM_TRACE_CAP_1000:
			SetCapacity(1000);
			break;

		case IDM_CLOSE:
			PostMessage(hdlg, WM_CLOSE, 0, 0);
			break;
		}
		break;

	case WM_INITMENU:
		{
			HMENU hmenu = (HMENU) wp;
			CheckMenuItem(hmenu, IDM_TRACE_SORT_DESC, !ascending ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_TRACE_SORT_ASC, ascending ? MF_CHECKED : MF_UNCHECKED);

			core->Lock();
			int cap = core->GetDebugger()->GetTraceCapacity();
			core->Unlock();
			CheckMenuItem(hmenu, IDM_TRACE_CAP_100, cap == 100 ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_TRACE_CAP_500, cap == 500 ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(hmenu, IDM_TRACE_CAP_1000, cap == 1000 ? MF_CHECKED : MF_UNCHECKED);
		}
		break;
	}
	return WinMonitor::DlgProc(hdlg, msg, wp, lp);
}
