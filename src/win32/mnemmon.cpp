// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ニーモニック表 (Z80 の命令の一覧) のウインドウ
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	Zilog の資料に載っている命令を、コード順かカテゴリー順で並べる。
//	表は mnemtab.h (gen_mnemtab.py で作ったもの)。サイクル数はコードの窓と同じ値
//	(Z80 の T ステート。PC-8801 のメモリのウェイトは含めない)
// ---------------------------------------------------------------------------

#include "headers.h"
#include "resource.h"
#include "mnemmon.h"
#include "misc.h"
#include "88config.h"
#include "mnemtab.h"

using namespace PC8801;

static const int nmnem = sizeof(mnemtable) / sizeof(mnemtable[0]);

// 凡例 (Help → Legend...)。メッセージの窓はシステムのフォントで描くので日本語で書ける
static const char legend[] =
	"【列】\r\n"
	"・Code: 命令のバイト (16 進)\r\n"
	"　　n = 1 バイトの値 (n n は 2 バイトの値。下位、上位の順)\r\n"
	"　　d = (IX+d)・(IY+d) の変位 (-128～+127)\r\n"
	"　　e = JR・DJNZ の相対の飛び先 (次の命令からの -128～+127)\r\n"
	"・Mnemonic: ニーモニック (nn = 2 バイトの値)\r\n"
	"・Size: 命令のバイト数\r\n"
	"・Cycles: Z80 の T ステート (クロック数)\r\n"
	"　　a/b は条件が成立したとき / しないとき\r\n"
	"　　(LDIR などのくり返しは、くり返すとき / 終わるとき)\r\n"
	"　　PC-8801 のメモリのウェイト (M1 ウェイトなど) は含まない\r\n"
	"・S Z H P N C: フラグ (サイン、ゼロ、ハーフキャリー、\r\n"
	"　　パリティ/オーバーフロー、減算、キャリー)\r\n"
	"・Category: 命令の分類 (Zilog の資料の分類)\r\n"
	"\r\n"
	"【フラグの記号】\r\n"
	"* : 結果によって変わる\r\n"
	"0 / 1 : いつもその値にする\r\n"
	"- : 変わらない\r\n"
	"? : 不定\r\n"
	"P : パリティ (結果の 1 のビットが偶数なら 1)\r\n"
	"V : オーバーフロー\r\n"
	"\r\n"
	"【分類】\r\n"
	"・8-Bit Load: 8 ビット転送\r\n"
	"・16-Bit Load: 16 ビット転送 (PUSH・POP を含む)\r\n"
	"・Exchange, Block Transfer, Search: 交換・ブロック転送・サーチ\r\n"
	"・8-Bit Arithmetic and Logical: 8 ビット演算・論理\r\n"
	"・General-Purpose Arithmetic, CPU Control: 汎用演算・CPU 制御\r\n"
	"・16-Bit Arithmetic: 16 ビット演算\r\n"
	"・Rotate and Shift: ローテート・シフト\r\n"
	"・Bit Set, Reset, Test: ビット操作\r\n"
	"・Jump: ジャンプ\r\n"
	"・Call and Return: コール・リターン\r\n"
	"・Input and Output: 入出力\r\n"
	"\r\n"
	"載せているのは Zilog の資料にある公式の命令だけです (非公式の命令は載せていません)。\r\n"
	"行をクリックすると選べます。選んだ行は並び順を変えても真ん中に出ます。\r\n"
	"Ctrl+C で、今の並び順の全部の行をタブ区切りでコピーします。\r\n";
static const int ncategory = sizeof(mnemcategory) / sizeof(mnemcategory[0]);

// ---------------------------------------------------------------------------
//	構築/消滅
//
MnemonicMonitor::MnemonicMonitor()
: bycategory(false), selected(-1), notfound(false)
{
	memset(selcode, 0, sizeof(selcode));
}

MnemonicMonitor::~MnemonicMonitor()
{
}

// ---------------------------------------------------------------------------
//	初期化
//
bool MnemonicMonitor::Init()
{
	if (!WinMonitor::Init(MAKEINTRESOURCE(IDD_MNEMMON), "MonitorMnemonic", 80, 30))
		return false;
	BuildRows();
	return true;
}

// ---------------------------------------------------------------------------
//	並び順を M88.ini に保存/復元する (既定: コード順)
//
void MnemonicMonitor::LoadMonitorConfig()
{
	WinMonitor::LoadMonitorConfig();
	bycategory = GetPrivateProfileInt("MonitorMnemonic", "SortByCategory", 0, PC8801::GetM88IniPath()) != 0;
	BuildRows();
}

void MnemonicMonitor::SaveMonitorConfig()
{
	WinMonitor::SaveMonitorConfig();
	WritePrivateProfileString("MonitorMnemonic", "SortByCategory", bycategory ? "1" : "0", PC8801::GetM88IniPath());
}

// ---------------------------------------------------------------------------
//	窓を開いた直後: ステータスバーにフラグの記号の意味を出す
//
void MnemonicMonitor::Start()
{
	EnableStatus(true);
	// statusbuf は 128 バイトなので短く
	if (notfound)
		PutStatus("表に無い命令です (%.2X %.2X %.2X %.2X)", selcode[0], selcode[1], selcode[2], selcode[3]);
	else
		PutStatus("フラグ: *変わる 0/1その値 -変わらない ?不定 Pパリティ Vオーバーフロー");
}

// ---------------------------------------------------------------------------
//	行の並びを作る。コード順は表の並びのまま。カテゴリー順はカテゴリーごとに見出しの行を入れ、
//	その中はコード順
//
void MnemonicMonitor::BuildRows()
{
	rows.clear();
	if (!bycategory)
	{
		for (int i = 0; i < nmnem; i++)
			rows.push_back(i);
	}
	else
	{
		for (int c = 0; c < ncategory; c++)
		{
			rows.push_back(-1 - c);
			for (int i = 0; i < nmnem; i++)
				if (mnemtable[i].category == c)
					rows.push_back(i);
		}
	}
	SetLines(int(rows.size()));
}

// ---------------------------------------------------------------------------
//	テキスト更新。0 行目は列の見出し
//
void MnemonicMonitor::UpdateText()
{
	int nrows = Max(0, GetHeight() - 1);
	int top = GetLine();

	SetTxCol(0xc0c0c0);
	SetBkCol(RGB(0, 0, 0x50));
	Putf("%-12s %-14s %-4s %-6s %-11s %s\n", "Code", "Mnemonic", "Size", "Cycles", "S Z H P N C",
		 bycategory ? "" : "Category");

	for (int y = 0; y < nrows; y++)
	{
		int index = top + y;
		if (index < int(rows.size()))
		{
			int r = rows[index];
			if (r < 0)
			{
				SetTxCol(0x60ffff);
				SetBkCol(RGB(0, 0, 0x40));
				Putf("-- %s --\n", mnemcategory[-1 - r]);
				continue;
			}
			const MnemEntry& e = mnemtable[r];
			SetTxCol(0xffffff);
			SetBkCol(r == selected ? RGB(0, 0x60, 0) : RGB(0, 0, 0x20));
			Putf("%-12s %-14s ", e.code, e.mnem);
			Putf("%-4d ", e.size);
			SetTxCol(0xa0c0a0);
			Putf("%-6s ", e.cycles);
			SetTxCol(0xffffff);
			for (int f = 0; f < 6; f++)
				Putf("%c%s", e.flags[f], f < 5 ? " " : "");
			if (!bycategory)
			{
				SetTxCol(0xc0c0c0);
				Putf(" %s", mnemcategory[e.category]);
			}
			Puts("\n");
			continue;
		}
		SetBkCol(RGB(0, 0, 0x20));
		Puts("\n");
	}
}

// ---------------------------------------------------------------------------
//	命令のバイトに当たる表の番号。表のコードの 16 進の所が全部合うもの (n・d・e は何でもよい)。
//	無ければ -1
//
static int FindEntry(const uint8* code)
{
	for (int i = 0; i < nmnem; i++)
	{
		const char* p = mnemtable[i].code;
		bool match = true;
		for (int pos = 0; *p && match; pos++)
		{
			while (*p == ' ')
				p++;
			if (!*p)
				break;
			if (pos >= 4)
			{
				match = false;
				break;
			}
			char tok[4];
			int n = 0;
			while (*p && *p != ' ' && n < 3)
				tok[n++] = *p++;
			tok[n] = 0;
			if (n == 2 && isxdigit(BYTE(tok[0])) && isxdigit(BYTE(tok[1])))
			{
				if (uint(strtoul(tok, 0, 16)) != code[pos])
					match = false;
			}
		}
		if (match)
			return i;
	}
	return -1;
}

// ---------------------------------------------------------------------------
//	コードの窓の右クリックのメニューから: 命令に当たる行へ移って色を付ける。
//	窓が閉じていれば開く。表に無い命令 (非公式の命令など) ならステータスバーで知らせる
//
bool PC8801::IsOfficialInstruction(const uint8* code, int size)
{
	int i = FindEntry(code);
	return i >= 0 && mnemtable[i].size == size;
}

void MnemonicMonitor::ShowInstruction(const uint8* code, HINSTANCE hinst, HWND parent)
{
	memcpy(selcode, code, sizeof(selcode));
	selected = FindEntry(code);
	notfound = selected < 0;

	if (!IsOpen())
		Show(hinst, parent, true);		// Start() で知らせも出る
	else
		Start();

	CenterSelected();
	Update();
}

// ---------------------------------------------------------------------------
//	選んだ行を窓の真ん中に出す。表の先頭・最後の近くでは、表の外が見えない所まで。
//	選んでいなければ (または今の並びに無ければ) 先頭
//
void MnemonicMonitor::CenterSelected()
{
	int visible = Max(1, GetHeight() - 1);		// 0 行目は列の見出し
	int count = int(rows.size());
	int top = 0;
	for (int i = 0; i < count; i++)
	{
		if (rows[i] == selected)
		{
			top = i - visible / 2;
			break;
		}
	}
	top = Limit(top, Max(0, count - visible), 0);
	SetLine(top);
}

// ---------------------------------------------------------------------------
//	Ctrl+C: 今の並びで全部の行をタブ区切りでコピーする (表計算に貼れる形)
//
bool MnemonicMonitor::CopyToClipboard()
{
	TextBuffer tb;
	tb.Add("Code\tMnemonic\tSize\tCycles\tS\tZ\tH\tP/V\tN\tC\tCategory\r\n");
	for (size_t i = 0; i < rows.size(); i++)
	{
		int r = rows[i];
		if (r < 0)
			continue;
		const MnemEntry& e = mnemtable[r];
		tb.Addf("%s\t%s\t%d\t%s", e.code, e.mnem, e.size, e.cycles);
		for (int f = 0; f < 6; f++)
			tb.Addf("\t%c", e.flags[f]);
		tb.Addf("\t%s\r\n", mnemcategory[e.category]);
	}
	return SetClipboardText(tb.Get(), tb.Length());
}

// ---------------------------------------------------------------------------
//	ダイアログ処理
//
BOOL MnemonicMonitor::DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_COMMAND:
		switch (LOWORD(wp))
		{
		case IDM_MNEM_SORT_CODE:
		case IDM_MNEM_SORT_CATEGORY:
			bycategory = LOWORD(wp) == IDM_MNEM_SORT_CATEGORY;
			BuildRows();
			CenterSelected();			// 選んだ行があれば、並べ替えた後もそれを真ん中に
			SaveMonitorConfig();
			Update();
			break;

		case IDM_MNEM_LEGEND:
			MessageBox(hdlg, legend, "Mnemonic Table - Legend", MB_OK | MB_ICONINFORMATION);
			break;

		case IDM_CLOSE:
			PostMessage(hdlg, WM_CLOSE, 0, 0);
			break;
		}
		break;

	case WM_LBUTTONDOWN:
		// クリックした行を選ぶ (見出しの行・列の見出しは選ばない)
		{
			POINT p;
			p.x = (short) LOWORD(lp);
			p.y = (short) HIWORD(lp);
			if (GetTextPos(&p) && p.y >= 1)
			{
				int index = GetLine() + p.y - 1;
				if (index < int(rows.size()) && rows[index] >= 0)
				{
					selected = rows[index];
					if (notfound)
					{
						notfound = false;
						Start();			// 「表に無い命令です」を消す
					}
					Update();
				}
			}
		}
		break;

	case WM_INITMENU:
		CheckMenuRadioItem((HMENU) wp, IDM_MNEM_SORT_CODE, IDM_MNEM_SORT_CATEGORY,
						   bycategory ? IDM_MNEM_SORT_CATEGORY : IDM_MNEM_SORT_CODE, MF_BYCOMMAND);
		break;
	}
	return WinMonitor::DlgProc(hdlg, msg, wp, lp);
}
