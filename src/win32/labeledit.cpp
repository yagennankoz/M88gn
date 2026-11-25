// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ラベル (番地に付ける名前) を入力する窓
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#include "headers.h"
#include "resource.h"
#include "labeledit.h"
#include "wincore.h"
#include "watchbank.h"
#include "misc.h"
#include "winvars.h"
#include <stdio.h>
#include <string>

using namespace PC8801;

// ---------------------------------------------------------------------------
//	ラベルの名前の決まり (M88gn)
//	・1 字目は英字・_・.・?・@、2 字目からはそれと数字・$
//	・レジスターと条件の名前は使えない (アセンブラが読み違える)
//	・名前は CPU・バンクが違っても重ならない (大文字・小文字は区別しない)
//
static const char* const reservednames[] =
{
	"A", "B", "C", "D", "E", "H", "L", "I", "R", "F",
	"AF", "BC", "DE", "HL", "SP", "IX", "IY", "IXH", "IXL", "IYH", "IYL", "XH", "XL", "YH", "YL",
	"NZ", "Z", "NC", "PO", "PE", "P", "M",
};

static bool IsLabelFirst(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '.' || c == '?' || c == '@';
}

static bool IsLabelChar(char c)
{
	return IsLabelFirst(c) || (c >= '0' && c <= '9') || c == '$';
}

static bool IsReservedName(const char* s)
{
	for (int i = 0; i < int(sizeof(reservednames) / sizeof(reservednames[0])); i++)
		if (!lstrcmpi(s, reservednames[i]))
			return true;
	return false;
}

// 使える名前なら 0、使えなければ理由
static const char* CheckLabelName(const char* s)
{
	if (!s[0])
		return 0;
	if (!IsLabelFirst(s[0]))
		return "名前の 1 字目は英字か _ . ? @ にしてください。";
	for (const char* p = s + 1; *p; p++)
		if (!IsLabelChar(*p))
			return "名前に使えるのは英字・数字と _ . ? @ $ です (空白・日本語は使えません)。";
	if (IsReservedName(s))
		return "レジスターや条件の名前 (A・HL・IX・NZ など) は使えません。";
	return 0;
}

// 使えない名前を直す (読み込み用)。直した理由 (1 = 使えない字、2 = レジスターの名前)。直さなければ 0
static int FixLabelName(char* s, int size)
{
	int changed = 0;
	for (char* p = s; *p; p++)
	{
		if (!IsLabelChar(*p))
			*p = '_', changed |= 1;
	}
	if (s[0] && !IsLabelFirst(s[0]))
	{
		// 数字・$ で始まる名前は頭に _
		int n = lstrlen(s);
		if (n + 1 < size)
		{
			memmove(s + 1, s, n + 1);
			s[0] = '_';
		}
		else
			s[0] = '_';
		changed |= 1;
	}
	if (IsReservedName(s) && lstrlen(s) + 1 < size)
	{
		lstrcat(s, "_");
		changed |= 2;
	}
	return changed;
}

// 同じ名前のラベルを探す (except のラベルは除く。そのラベルを付け直すとき)。あれば true
static bool FindLabelName(Debugger* dbg, const char* name, int excpu, uint exaddr, int exbank,
						  int* cpu, uint* addr, int* bank)
{
	int n = dbg->GetLabelCount();
	for (int i = 0; i < n; i++)
	{
		int c, b;
		uint a;
		const char* nm;
		if (!dbg->GetLabelAt(i, &c, &a, &b, &nm) || lstrcmpi(nm, name))
			continue;
		if (c == excpu && a == exaddr && b == exbank)
			continue;
		*cpu = c, *addr = a, *bank = b;
		return true;
	}
	return false;
}

// 重ならない名前にする (名前_2、名前_3…)。変えたら true
static bool MakeUniqueLabel(Debugger* dbg, char* name, int size, int excpu, uint exaddr, int exbank)
{
	int c, b;
	uint a;
	if (!FindLabelName(dbg, name, excpu, exaddr, exbank, &c, &a, &b))
		return false;
	char base[Debugger::labelmax];
	lstrcpyn(base, name, sizeof(base));
	for (int k = 2; k < 100000; k++)
	{
		char suf[16];
		wsprintf(suf, "_%d", k);
		int room = size - 1 - lstrlen(suf);
		char cand[Debugger::labelmax + 16];
		lstrcpyn(cand, base, Min(room, int(sizeof(base))) + 1);
		lstrcat(cand, suf);
		if (!FindLabelName(dbg, cand, excpu, exaddr, exbank, &c, &a, &b))
		{
			lstrcpyn(name, cand, size);
			return true;
		}
	}
	return true;
}

namespace
{

struct LabelEditCtx
{
	Debugger* dbg;
	int cpu;
	uint addr;
	int bank;			// 直すラベルのバンク (無ければ最初に選んでおくバンク)
	bool add;			// 新しく足す (番地と CPU を入れられる。ラベルの一覧の Add から。M88gn)
};

// バンクのコンボ: Any と、この番地を置けるバンク。サブ CPU はバンクが無い。
// 選んでいたバンクが入れば、それを選んだまま
void FillLabelBank(HWND hdlg, int cpu, uint addr, int want)
{
	HWND cb = GetDlgItem(hdlg, IDC_LABEL_BANK);
	SendMessage(cb, CB_RESETCONTENT, 0, 0);
	if (cpu)
	{
		SendMessage(cb, CB_ADDSTRING, 0, (LPARAM) "- (Sub CPU)");
		SendMessage(cb, CB_SETCURSEL, 0, 0);
		EnableWindow(cb, FALSE);
		return;
	}
	EnableWindow(cb, TRUE);
	int i = (int) SendMessage(cb, CB_ADDSTRING, 0, (LPARAM) "Any (all banks)");
	SendMessage(cb, CB_SETITEMDATA, i, Debugger::label_anybank);
	int sel = 0;
	for (int k = 0; k < nwatchbanks; k++)
	{
		const WatchBank& b = watchbanks[k];
		if (addr < b.lo || addr > b.hi)
			continue;
		i = (int) SendMessage(cb, CB_ADDSTRING, 0, (LPARAM) b.name);
		SendMessage(cb, CB_SETITEMDATA, i, b.type);
		if (b.type == want)
			sel = i;
	}
	SendMessage(cb, CB_SETCURSEL, sel, 0);
}

int SelectedLabelBank(HWND hdlg)
{
	int i = (int) SendDlgItemMessage(hdlg, IDC_LABEL_BANK, CB_GETCURSEL, 0, 0);
	if (i < 0)
		return Debugger::label_anybank;
	return (int) SendDlgItemMessage(hdlg, IDC_LABEL_BANK, CB_GETITEMDATA, i, 0);
}

// 追加のとき: 番地の欄と CPU から番地を読む (16 進かラベルの名前)
bool ReadLabelAddr(HWND hdlg, uint* addr, int* cpu)
{
	char buf[64];
	GetDlgItemText(hdlg, IDC_LABEL_ADDR, buf, sizeof(buf));
	*cpu = IsDlgButtonChecked(hdlg, IDC_LABEL_SUBCPU) == BST_CHECKED ? 1 : 0;
	if (!ParseAddrInput(buf, addr))
		return false;
	*addr &= 0xffff;
	return true;
}

BOOL CALLBACK LabelDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_INITDIALOG:
		{
			SetWindowLongPtr(hdlg, DWLP_USER, lp);
			LabelEditCtx* ctx = (LabelEditCtx*) lp;
			char buf[64];
			if (ctx->add)
				lstrcpy(buf, "New label:");
			else
				wsprintf(buf, "Label for %.4XH (%s CPU):", ctx->addr, ctx->cpu ? "Sub" : "Main");
			SetDlgItemText(hdlg, IDC_LABEL_TITLE, buf);
			// 番地と CPU: 足すときだけ入れられる。直すときは見せるだけ
			wsprintf(buf, "%.4X", ctx->addr);
			SetDlgItemText(hdlg, IDC_LABEL_ADDR, buf);
			CheckDlgButton(hdlg, IDC_LABEL_SUBCPU, ctx->cpu ? BST_CHECKED : BST_UNCHECKED);
			EnableWindow(GetDlgItem(hdlg, IDC_LABEL_ADDR), ctx->add);
			EnableWindow(GetDlgItem(hdlg, IDC_LABEL_SUBCPU), ctx->add);
			const char* name = ctx->add ? 0 : ctx->dbg->GetLabelExact(ctx->cpu, ctx->addr, ctx->bank);
			FillLabelBank(hdlg, ctx->cpu, ctx->addr, ctx->bank);
			SetDlgItemText(hdlg, IDC_LABEL_NAME, name ? name : "");
			SendDlgItemMessage(hdlg, IDC_LABEL_NAME, EM_LIMITTEXT, Debugger::labelmax - 1, 0);
			int focus = ctx->add ? IDC_LABEL_ADDR : IDC_LABEL_NAME;
			SendDlgItemMessage(hdlg, focus, EM_SETSEL, 0, -1);
			SetFocus(GetDlgItem(hdlg, focus));
		}
		return FALSE;			// フォーカスは自分で決めた

	case WM_COMMAND:
		{
			LabelEditCtx* ctx = (LabelEditCtx*) GetWindowLongPtr(hdlg, DWLP_USER);
			// 足すとき: 番地・CPU が変わったら、置けるバンクを並べ直す
			if (ctx && ctx->add && ((LOWORD(wp) == IDC_LABEL_ADDR && HIWORD(wp) == EN_CHANGE)
									 || (LOWORD(wp) == IDC_LABEL_SUBCPU && HIWORD(wp) == BN_CLICKED)))
			{
				uint a;
				int c;
				if (!ReadLabelAddr(hdlg, &a, &c))
					a = 0, c = IsDlgButtonChecked(hdlg, IDC_LABEL_SUBCPU) == BST_CHECKED ? 1 : 0;
				FillLabelBank(hdlg, c, a, SelectedLabelBank(hdlg));
				return TRUE;
			}
		}
		switch (LOWORD(wp))
		{
		case IDOK:
			{
				LabelEditCtx* ctx = (LabelEditCtx*) GetWindowLongPtr(hdlg, DWLP_USER);
				if (ctx->add)
				{
					uint a;
					int c;
					if (!ReadLabelAddr(hdlg, &a, &c))
					{
						MessageBox(hdlg, "番地を 16 進かラベルの名前で入れてください。", "Label", MB_OK | MB_ICONWARNING);
						SetFocus(GetDlgItem(hdlg, IDC_LABEL_ADDR));
						return TRUE;
					}
					ctx->addr = a, ctx->cpu = c;
				}
				char buf[Debugger::labelmax];
				GetDlgItemText(hdlg, IDC_LABEL_NAME, buf, sizeof(buf));
				// 前後の空白は捨てる。空なら消す
				char* s = buf;
				while (*s == ' ' || *s == '\t')
					s++;
				int n = lstrlen(s);
				while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
					s[--n] = 0;
				if (ctx->add && !s[0])
				{
					MessageBox(hdlg, "名前を入れてください。", "Label", MB_OK | MB_ICONWARNING);
					SetFocus(GetDlgItem(hdlg, IDC_LABEL_NAME));
					return TRUE;
				}
				int nb = ctx->cpu ? int(Debugger::label_anybank) : SelectedLabelBank(hdlg);
				// 字と重複を調べる (M88gn)。だめなら知らせて閉じない
				if (s[0])
				{
					const char* why = CheckLabelName(s);
					if (why)
					{
						MessageBox(hdlg, why, "Label", MB_OK | MB_ICONWARNING);
						SetFocus(GetDlgItem(hdlg, IDC_LABEL_NAME));
						return TRUE;
					}
					// 直すときは元のラベル、足すときは同じ所 (上書きになる) を除いて探す
					int exbank = ctx->add ? nb : ctx->bank;
					int dc, db;
					uint da;
					if (FindLabelName(ctx->dbg, s, ctx->cpu, ctx->addr, exbank, &dc, &da, &db))
					{
						char msg[160];
						wsprintf(msg, "同じ名前のラベルが %s%.4X (%s) にあります。\r\n名前は CPU・バンクが違っても重ねられません。",
								 dc ? "Sub " : "", da, dc ? "-" : LabelBankName(db));
						MessageBox(hdlg, msg, "Label", MB_OK | MB_ICONWARNING);
						SetFocus(GetDlgItem(hdlg, IDC_LABEL_NAME));
						return TRUE;
					}
				}
				// 直したラベル (バンクを変えたとき・空にしたとき) は元のバンクから消す
				if (!ctx->add)
					ctx->dbg->SetLabel(ctx->cpu, ctx->addr, ctx->bank, 0);
				ctx->dbg->SetLabel(ctx->cpu, ctx->addr, nb, s);
				ctx->bank = nb;
				EndDialog(hdlg, IDOK);
			}
			return TRUE;

		case IDCANCEL:
			EndDialog(hdlg, IDCANCEL);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

}

int PC8801::FixLabelForSet(Debugger* dbg, int cpu, uint addr, int bank, char* name, int size)
{
	if (cpu & 1)
		bank = Debugger::label_anybank;
	int r = FixLabelName(name, size);
	if (MakeUniqueLabel(dbg, name, size, cpu & 1, addr & 0xffff, bank))
		r |= 4;
	return r;
}

// ---------------------------------------------------------------------------
//	ラベルを窓のあちこちで使うための関数 (M88gn)
//
static WinCore* labelcore = 0;

void PC8801::SetLabelCore(WinCore* core)
{
	labelcore = core;
}

bool PC8801::ParseAddrInput(const char* text, uint* addr, int* cpu)
{
	char buf[64];
	while (*text == ' ' || *text == '\t')
		text++;
	lstrcpyn(buf, text, sizeof(buf));
	int n = lstrlen(buf);
	while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\t'))
		buf[--n] = 0;
	if (!n)
		return false;
	if (cpu)
		*cpu = -1;
	// ラベルの名前
	if (labelcore && labelcore->GetDebugger()->FindLabelAddr(buf, addr, cpu))
		return true;
	// 16 進
	char* p = buf;
	if (*p == '$')
		p++;
	else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	n = lstrlen(p);
	if (n > 0 && (p[n - 1] == 'h' || p[n - 1] == 'H'))
		p[--n] = 0;
	if (n == 0 || n > 5)
		return false;
	for (int i = 0; i < n; i++)
		if (!isxdigit((unsigned char) p[i]))
			return false;
	uint v = strtoul(p, 0, 16);
	if (v > 0xffff)
		return false;
	*addr = v;
	return true;
}

const char* PC8801::LabelNow(int cpu, uint addr)
{
	if (!labelcore)
		return 0;
	addr &= 0xffff;
	int bank = (cpu & 1) ? -1 : int(labelcore->GetMem1()->GetRdBank(addr));
	return labelcore->GetDebugger()->GetLabel(cpu & 1, addr, bank);
}

void PC8801::AddrLabelText(char* out, int cpu, uint addr, bool h)
{
	const char* l = LabelNow(cpu, addr);
	if (l)
		wsprintf(out, h ? "%s(%.4XH)" : "%s(%.4X)", l, addr & 0xffff);
	else
		wsprintf(out, h ? "%.4XH" : "%.4X", addr & 0xffff);
}

const char* PC8801::LabelBankName(int bank)
{
	if (bank < 0 || bank == Debugger::label_anybank)
		return "Any";
	return WatchBankName(bank);
}

bool PC8801::EditLabel(HWND owner, HINSTANCE hinst, Debugger* dbg, int cpu, uint addr, int bank)
{
	if (bank < 0)
		bank = Debugger::label_anybank;
	LabelEditCtx ctx = { dbg, cpu & 1, addr & 0xffff, (cpu & 1) ? int(Debugger::label_anybank) : bank, false };
	return DialogBoxParam(hinst, MAKEINTRESOURCE(IDD_LABEL_EDIT), owner,
						  DLGPROC((void*) LabelDlgProc), (LPARAM) &ctx) == IDOK;
}

bool PC8801::AddLabel(HWND owner, HINSTANCE hinst, Debugger* dbg, int* cpu, uint* addr, int* bank)
{
	LabelEditCtx ctx = { dbg, 0, 0, int(Debugger::label_anybank), true };
	if (DialogBoxParam(hinst, MAKEINTRESOURCE(IDD_LABEL_EDIT), owner,
					   DLGPROC((void*) LabelDlgProc), (LPARAM) &ctx) != IDOK)
		return false;
	*cpu = ctx.cpu, *addr = ctx.addr, *bank = ctx.bank;
	return true;
}

// ---------------------------------------------------------------------------
//	ラベルの書き出し・読み込み (テキストファイル。M88gn)
//
//	書き出し: 1 行に「番地 名前」(16 進 4 桁)。サブ CPU のラベルは「S:番地 名前」。; から後はコメント
//	読み込み: 上の形のほか、番地は 1234 / 1234H / $1234 / 0x1234 / &H1234、
//	アセンブラのシンボルの「名前 EQU 1234H」「名前: equ $1234」「名前 = 1234H」も読む
//

namespace
{

// 番地として読めれば true (0000-FFFF)
bool ParseAddr(const char* s, uint* addr)
{
	char buf[16];
	int n = lstrlen(s);
	if (n == 0 || n >= int(sizeof(buf)))
		return false;
	lstrcpy(buf, s);
	char* p = buf;
	if (*p == '$')
		p++;
	else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	else if (p[0] == '&' && (p[1] == 'h' || p[1] == 'H'))
		p += 2;
	n = lstrlen(p);
	if (n > 0 && (p[n - 1] == 'h' || p[n - 1] == 'H'))
		p[--n] = 0;
	if (n == 0 || n > 5)
		return false;
	for (int i = 0; i < n; i++)
		if (!isxdigit((unsigned char) p[i]))
			return false;
	uint v = strtoul(p, 0, 16);
	if (v > 0xffff)
		return false;
	*addr = v;
	return true;
}

// 番地の前の「S:」「M:」(CPU) か「バンクの名前:」。無ければメインの CPU の Any
bool ParseCpuAddr(const char* s, int* cpu, uint* addr, int* bank)
{
	*cpu = 0;
	*bank = Debugger::label_anybank;
	const char* colon = strchr(s, ':');
	if (colon && colon[1])
	{
		char pre[16];
		int n = int(colon - s);
		if (n <= 0 || n >= int(sizeof(pre)))
			return false;
		lstrcpyn(pre, s, n + 1);
		if (!lstrcmpi(pre, "S"))
			*cpu = 1;
		else if (!lstrcmpi(pre, "M") || !lstrcmpi(pre, "Any"))
			;
		else
		{
			const WatchBank* b = FindWatchBankByName(pre);
			if (!b)
				return false;
			*bank = b->type;
		}
		s = colon + 1;
	}
	return ParseAddr(s, addr);
}

bool IsEqu(const char* s)
{
	return !lstrcmpi(s, "EQU") || !lstrcmpi(s, ".EQU") || !lstrcmpi(s, "=");
}

bool AskLabelFile(HWND owner, char* filename, bool save)
{
	OFNV5 ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = WINVAR(OFNSIZE);
	filename[0] = 0;
	ofn.hwndOwner = owner;
	ofn.lpstrFilter = "Label file (*.txt;*.lbl;*.sym)\0*.txt;*.lbl;*.sym\0All Files (*.*)\0*.*\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = "txt";
	if (save)
	{
		ofn.Flags = OFN_OVERWRITEPROMPT;
		ofn.lpstrTitle = "Export Labels";
		return !!GetSaveFileName(&ofn);
	}
	ofn.Flags = OFN_FILEMUSTEXIST;
	ofn.lpstrTitle = "Import Labels";
	return !!GetOpenFileName(&ofn);
}

}

// 書き出した数 (やめた・書けなければ -1)
int PC8801::ExportLabels(HWND owner, Debugger* dbg)
{
	char filename[MAX_PATH];
	if (!AskLabelFile(owner, filename, true))
		return -1;
	FILE* fp = fopen(filename, "wb");
	if (!fp)
	{
		MessageBox(owner, "ファイルを作れません.", "Export Labels", MB_OK | MB_ICONERROR);
		return -1;
	}
	fputs("; M88gn labels\r\n; [S:|bank:]address name   (S: = sub CPU, bank = RAM/N88/N/E0-E3/ERAM0-3/GVRAM0-2/TVRAM..., none = any bank)\r\n", fp);
	int n = dbg->GetLabelCount();
	for (int i = 0; i < n; i++)
	{
		int cpu, bank;
		uint addr;
		const char* name;
		if (!dbg->GetLabelAt(i, &cpu, &addr, &bank, &name))
			continue;
		if (cpu)
			fprintf(fp, "S:%.4X %s\r\n", addr, name);
		else if (bank != Debugger::label_anybank)
			fprintf(fp, "%s:%.4X %s\r\n", LabelBankName(bank), addr, name);
		else
			fprintf(fp, "%.4X %s\r\n", addr, name);
	}
	fclose(fp);
	return n;
}

// 読み込んだ数 (やめた・読めなければ -1)。skipped に読めなかった行の数
int PC8801::ImportLabels(HWND owner, Debugger* dbg, int* skipped)
{
	*skipped = 0;
	char filename[MAX_PATH];
	if (!AskLabelFile(owner, filename, false))
		return -1;
	FILE* fp = fopen(filename, "rb");
	if (!fp)
	{
		MessageBox(owner, "ファイルを開けません.", "Import Labels", MB_OK | MB_ICONERROR);
		return -1;
	}
	if (dbg->GetLabelCount() > 0)
	{
		int r = MessageBox(owner, "今のラベルを消してから読み込みますか?\r\n"
								  "(いいえ: 今のラベルに足します。同じ番地は読み込んだ名前にします)",
						   "Import Labels", MB_YESNOCANCEL | MB_ICONQUESTION);
		if (r == IDCANCEL)
		{
			fclose(fp);
			return -1;
		}
		if (r == IDYES)
			dbg->DeleteAllLabels();
	}

	int count = 0;
	int lineno = 0;
	int renamed = 0;
	std::string notes;			// 名前を変えた行 (知らせに出す。多ければ先頭だけ)
	char line[512];
	while (fgets(line, sizeof(line), fp))
	{
		lineno++;
		// コメント (; と #) を外す
		for (char* c = line; *c; c++)
		{
			if (*c == ';' || *c == '#')
			{
				*c = 0;
				break;
			}
		}
		// 区切り (空白・タブ・カンマ・=) で分ける。= は EQU と同じに扱うので語として残す
		char* tok[4];
		int nt = 0;
		char* c = line;
		while (*c && nt < 4)
		{
			while (*c == ' ' || *c == '\t' || *c == ',' || *c == '\r' || *c == '\n')
				c++;
			if (!*c)
				break;
			if (*c == '=')
			{
				*c++ = 0;
				tok[nt++] = (char*) "=";
				continue;
			}
			tok[nt++] = c;
			while (*c && *c != ' ' && *c != '\t' && *c != ',' && *c != '=' && *c != '\r' && *c != '\n')
				c++;
			if (*c == '=')
			{
				*c = 0;
				tok[nt++] = (char*) "=";
				c++;
				continue;
			}
			if (*c)
				*c++ = 0;
		}
		if (nt == 0)
			continue;

		int cpu = 0, bank = Debugger::label_anybank;
		uint addr;
		const char* name = 0;
		if (nt >= 2 && ParseCpuAddr(tok[0], &cpu, &addr, &bank) && !IsEqu(tok[1]))
			name = tok[1];						// 番地 名前
		else if (nt >= 3 && IsEqu(tok[1]) && ParseAddr(tok[2], &addr) && (bank = Debugger::label_anybank, cpu = 0, true))
			name = tok[0];						// 名前 EQU 番地
		if (!name)
		{
			(*skipped)++;
			continue;
		}
		// 「名前:」の : は外す
		char nm[Debugger::labelmax];
		lstrcpyn(nm, name, sizeof(nm));
		int l = lstrlen(nm);
		if (l > 0 && nm[l - 1] == ':')
			nm[--l] = 0;
		if (!l)
		{
			(*skipped)++;
			continue;
		}
		// 使えない字は直し、名前が重なれば 名前_2 などにする (M88gn)
		char orig[Debugger::labelmax];
		lstrcpy(orig, nm);
		int fixed = FixLabelName(nm, sizeof(nm));
		if (cpu)
			bank = Debugger::label_anybank;
		bool dup = MakeUniqueLabel(dbg, nm, sizeof(nm), cpu, addr, bank);
		if (fixed || dup)
		{
			renamed++;
			if (renamed <= 20)
			{
				char why[64] = "";
				if (fixed & 1)
					lstrcat(why, "使えない字");
				if (fixed & 2)
					lstrcat(why, why[0] ? "・レジスターの名前" : "レジスターの名前");
				if (dup)
					lstrcat(why, why[0] ? "・名前の重複" : "名前の重複");
				char note[160];
				wsprintf(note, "%d 行目: %s -> %s (%s)\r\n", lineno, orig, nm, why);
				notes += note;
			}
		}
		dbg->SetLabel(cpu, addr, bank, nm);
		count++;
	}
	fclose(fp);
	if (renamed)
	{
		char head[96];
		wsprintf(head, "%d 個のラベルの名前を変えて読み込みました。\r\n\r\n", renamed);
		std::string msg = head + notes;
		if (renamed > 20)
			msg += "…\r\n";
		MessageBox(owner, msg.c_str(), "Import Labels", MB_OK | MB_ICONWARNING);
	}
	return count;
}
