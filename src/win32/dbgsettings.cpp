// ---------------------------------------------------------------------------
//	M88 - PC-8801 emulator
//	デバッグ設定(ブレークポイント/ウォッチ)のディスクイメージ単位の保存
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#include "headers.h"
#include "dbgsettings.h"
#include "watchbank.h"
#include "labeledit.h"

namespace
{
	// <diskimage> + ".m88dbg" を組み立てる
	void MakePath(char* path, size_t pathsize, const char* diskimage)
	{
		strncpy_s(path, pathsize, diskimage, _TRUNCATE);
		strncat_s(path, pathsize, ".m88dbg", _TRUNCATE);
	}
}

namespace PC8801
{

// ---------------------------------------------------------------------------
//	保存。書き込みに1つでも失敗したら false を返すが、
//	書けるところまでは書く(全滅させない)
//
bool SaveDebugSettings(Debugger* dbg, const char* diskimage)
{
	if (!diskimage || !diskimage[0])
		return false;

	char path[MAX_PATH + 16];
	MakePath(path, sizeof(path), diskimage);

	bool ok = true;
	char buf[16];

	if (!WritePrivateProfileString("M88Debug", "Version", "1", path))
		ok = false;

	// 古いエントリが残らないよう、書き直す前にセクションごと消す
	WritePrivateProfileString("Breakpoints", 0, 0, path);
	int bpcount = dbg->GetBreakPointCount();
	wsprintf(buf, "%d", bpcount);
	if (!WritePrivateProfileString("Breakpoints", "Count", buf, path))
		ok = false;
	for (int i = 0; i < bpcount; i++)
	{
		Debugger::BreakPoint bp;
		if (!dbg->GetBreakPoint(i, &bp))
			continue;

		char key[16], val[128];
		wsprintf(key, "BP%d", i);
		// addr,length,cpu,exec,read,write,enabled,comment (アドレスと長さは16進、他は0/1)
		wsprintf(val, "%x,%x,%x,%d,%d,%d,%d,%s",
				 bp.addr, bp.length, bp.cpu, bp.exec, bp.read, bp.write, bp.enabled,
				 bp.comment);
		if (!WritePrivateProfileString("Breakpoints", key, val, path))
			ok = false;
		// I/O ポート (M88gn)。前の版でも読めるように別のキーにする
		wsprintf(key, "BPIO%d", i);
		if (!WritePrivateProfileString("Breakpoints", key, bp.io ? "1" : 0, path))
			ok = false;
		// 条件と回数 (M88gn)
		wsprintf(key, "BPC%d", i);
		if (!WritePrivateProfileString("Breakpoints", key, bp.cond[0] ? bp.cond : 0, path))
			ok = false;
		char num[16];
		wsprintf(num, "%u", bp.hitcount);
		wsprintf(key, "BPN%d", i);
		if (!WritePrivateProfileString("Breakpoints", key, bp.hitcount ? num : 0, path))
			ok = false;
		// パターン (M88gn): バンク,印,バイトの並び (16 進)
		wsprintf(key, "BPP%d", i);
		char pv[16 + Debugger::patmax * 2];
		if (bp.pattern)
		{
			char* w = pv + wsprintf(pv, "%x,%x,", bp.patbank, bp.patflags);
			for (int j = 0; j < bp.patlen; j++)
				w += wsprintf(w, "%.2x", bp.pat[j]);
		}
		if (!WritePrivateProfileString("Breakpoints", key, bp.pattern ? pv : 0, path))
			ok = false;
	}

	WritePrivateProfileString("Watch", 0, 0, path);
	int wcount = dbg->GetWatchCount();
	wsprintf(buf, "%d", wcount);
	if (!WritePrivateProfileString("Watch", "Count", buf, path))
		ok = false;
	for (int i = 0; i < wcount; i++)
	{
		Debugger::WatchEntry w;
		if (!dbg->GetWatch(i, &w))
			continue;

		char key[16], val[80];
		wsprintf(key, "W%d", i);
		// addr,size,cpu,label
		wsprintf(val, "%x,%x,%x,%s", w.addr, w.size, w.cpu, w.label);
		if (!WritePrivateProfileString("Watch", key, val, path))
			ok = false;
		// バンク (M88gn)。前の版でも読めるように別のキーにする。名前で書く
		wsprintf(key, "WB%d", i);
		if (!WritePrivateProfileString("Watch", key, WatchBankName(w.bank), path))
			ok = false;
	}

	// ラベル (M88gn): L%d = cpu,addr,名前
	WritePrivateProfileString("Labels", 0, 0, path);
	int lcount = dbg->GetLabelCount();
	wsprintf(buf, "%d", lcount);
	if (!WritePrivateProfileString("Labels", "Count", buf, path))
		ok = false;
	for (int i = 0; i < lcount; i++)
	{
		int cpu, bank;
		uint addr;
		const char* name;
		if (!dbg->GetLabelAt(i, &cpu, &addr, &bank, &name))
			continue;
		char key[16], val[16 + Debugger::labelmax];
		wsprintf(key, "L%d", i);
		wsprintf(val, "%x,%x,%s", cpu, addr, name);
		if (!WritePrivateProfileString("Labels", key, val, path))
			ok = false;
		// バンク (Any でなければ名前で)
		wsprintf(key, "LB%d", i);
		if (!WritePrivateProfileString("Labels", key, (!cpu && bank != Debugger::label_anybank) ? WatchBankName(bank) : 0, path))
			ok = false;
	}

	return ok;
}

// ---------------------------------------------------------------------------
//	読み込み。既存の一覧は読み込み前に丸ごと消す
//
bool LoadDebugSettings(Debugger* dbg, const char* diskimage, int* renamed)
{
	if (!diskimage || !diskimage[0])
		return false;

	char path[MAX_PATH + 16];
	MakePath(path, sizeof(path), diskimage);

	int version = GetPrivateProfileInt("M88Debug", "Version", 0, path);
	if (version <= 0 || version > 1)
		return false;	// 保存されていない、または将来のバージョン

	dbg->DeleteAllBreakPoints();
	dbg->DeleteAllWatches();
	dbg->DeleteAllLabels();

	int bpcount = GetPrivateProfileInt("Breakpoints", "Count", 0, path);
	for (int i = 0; i < bpcount; i++)
	{
		char key[16], val[256];
		wsprintf(key, "BP%d", i);
		if (!GetPrivateProfileString("Breakpoints", key, "", val, sizeof(val), path) || !val[0])
			continue;

		unsigned int addr = 0, length = 1, cpu = 0;
		int exec = 0, read = 0, write = 0, enabled = 1;
		char comment[64] = "";		// 旧形式(コメント無し)を読んだときは空欄にする
		sscanf_s(val, "%x,%x,%x,%d,%d,%d,%d,%63[^\r\n]",
				 &addr, &length, &cpu, &exec, &read, &write, &enabled,
				 comment, (unsigned)sizeof(comment));

		Debugger::BreakPoint bp;
		bp.addr = addr;
		bp.length = length;
		bp.cpu = uint8(cpu);
		wsprintf(key, "BPIO%d", i);
		bp.io = GetPrivateProfileInt("Breakpoints", key, 0, path) != 0;	// M88gn
		wsprintf(key, "BPC%d", i);
		GetPrivateProfileString("Breakpoints", key, "", bp.cond, sizeof(bp.cond), path);
		wsprintf(key, "BPN%d", i);
		bp.hitcount = GetPrivateProfileInt("Breakpoints", key, 0, path);
		bp.hits = 0;
		bp.exec = exec != 0;
		bp.read = read != 0;
		bp.write = write != 0;
		// パターン (M88gn)
		{
			char pv[16 + Debugger::patmax * 2];
			wsprintf(key, "BPP%d", i);
			unsigned int bank = 0, flags = 0;
			char hex[Debugger::patmax * 2 + 1] = "";
			if (GetPrivateProfileString("Breakpoints", key, "", pv, sizeof(pv), path)
				&& sscanf_s(pv, "%x,%x,%128[0-9a-fA-F]", &bank, &flags, hex, (unsigned) sizeof(hex)) == 3)
			{
				int n = int(strlen(hex)) / 2;
				for (int j = 0; j < n; j++)
				{
					char b[3] = { hex[j * 2], hex[j * 2 + 1], 0 };
					bp.pat[j] = uint8(strtoul(b, 0, 16));
				}
				if (n > 0)
				{
					bp.pattern = true;
					bp.patlen = uint8(n);
					bp.patbank = uint8(bank);
					bp.patflags = uint8(flags);
					bp.exec = bp.read = bp.write = false;
				}
			}
		}
		// 復元した BP は必ず無効な状態から始める。有効なまま復元すると
		// ROM のブート中に即ヒットして「起動が固まった」ように見える
		// (特にメモリ BP)。有効化は BP 管理ウインドウで行う
		bp.enabled = false;
		strncpy_s(bp.comment, comment, _TRUNCATE);
		dbg->AddBreakPoint(bp);
	}

	int wcount = GetPrivateProfileInt("Watch", "Count", 0, path);
	for (int i = 0; i < wcount; i++)
	{
		char key[16], val[128];
		wsprintf(key, "W%d", i);
		if (!GetPrivateProfileString("Watch", key, "", val, sizeof(val), path) || !val[0])
			continue;

		unsigned int addr = 0, size = 1, cpu = 0;
		char label[32] = "";
		sscanf_s(val, "%x,%x,%x,%31[^\r\n]", &addr, &size, &cpu, label, (unsigned)sizeof(label));

		Debugger::WatchEntry w;
		w.addr = addr;
		w.size = uint8(size);
		w.cpu = uint8(cpu);
		strncpy_s(w.label, sizeof(w.label), label, _TRUNCATE);
		// バンク (M88gn)。無ければ (前の版で保存したもの) RAM
		char bname[16];
		wsprintf(key, "WB%d", i);
		GetPrivateProfileString("Watch", key, "RAM", bname, sizeof(bname), path);
		const WatchBank* wb = FindWatchBankByName(bname);
		w.bank = uint8(wb ? wb->type : MemoryViewer::mainram);
		dbg->AddWatch(w);
	}

	// ラベル (M88gn)
	int lcount = GetPrivateProfileInt("Labels", "Count", 0, path);
	if (renamed)
		*renamed = 0;
	for (int i = 0; i < lcount; i++)
	{
		char key[16], val[16 + Debugger::labelmax];
		wsprintf(key, "L%d", i);
		if (!GetPrivateProfileString("Labels", key, "", val, sizeof(val), path) || !val[0])
			continue;
		unsigned int cpu = 0, addr = 0;
		char name[Debugger::labelmax] = "";
		if (sscanf_s(val, "%x,%x,%31[^\r\n]", &cpu, &addr, name, (unsigned) sizeof(name)) == 3)
		{
			char bname[16];
			wsprintf(key, "LB%d", i);
			GetPrivateProfileString("Labels", key, "", bname, sizeof(bname), path);
			const WatchBank* wb = bname[0] ? FindWatchBankByName(bname) : 0;
			int bank = wb ? int(wb->type) : int(Debugger::label_anybank);
			// 名前の決まり (重複・使えない字) に合わせる。前の版で付けたものは合っていないことがある
			if (FixLabelForSet(dbg, int(cpu), addr, bank, name, sizeof(name)) && renamed)
				(*renamed)++;
			dbg->SetLabel(int(cpu), addr, bank, name);
		}
	}

	return true;
}

}
