// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ウォッチのバンク (ウォッチの窓とメモリの窓で使う)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	ウォッチは番地とバンクで、どのメモリを見るかを決める。バンクは MemoryViewer::Type の値。
//	バンクごとに置ける番地の範囲が決まっている (メモリの窓の 0000/6000/c000/f000 のメニューと同じ割り当て)。
//	サブ CPU のウォッチはバンクを持たず、0000-7fff (0000-3fff は ROM、4000-7fff は RAM)
// ---------------------------------------------------------------------------

#pragma once

#include "pc88/memview.h"

namespace PC8801
{

struct WatchBank
{
	MemoryViewer::Type type;
	const char* name;		// ウォッチの窓・追加の窓に出す名前
	uint lo, hi;			// 置ける番地の範囲 (両端を含む)
};

static const WatchBank watchbanks[] =
{
	{ MemoryViewer::mainram, "RAM",    0x0000, 0xffff },
	{ MemoryViewer::n88rom,  "N88",    0x0000, 0x7fff },
	{ MemoryViewer::nrom,    "N",      0x0000, 0x7fff },
	{ MemoryViewer::n80rom,  "N80",    0x0000, 0x7fff },	// N80 モード
	{ MemoryViewer::n80srrom,"N80SR",  0x0000, 0x7fff },
	{ MemoryViewer::n80sr1,  "N80SR1", 0x6000, 0x7fff },	// N80SR の ROM の 8000-9FFF (6000-7FFF に出す)
	{ MemoryViewer::n88e0,   "E0",     0x6000, 0x7fff },
	{ MemoryViewer::n88e1,   "E1",     0x6000, 0x7fff },
	{ MemoryViewer::n88e2,   "E2",     0x6000, 0x7fff },
	{ MemoryViewer::n88e3,   "E3",     0x6000, 0x7fff },
	{ MemoryViewer::eram0,   "ERAM0",  0x0000, 0x7fff },
	{ MemoryViewer::eram1,   "ERAM1",  0x0000, 0x7fff },
	{ MemoryViewer::eram2,   "ERAM2",  0x0000, 0x7fff },
	{ MemoryViewer::eram3,   "ERAM3",  0x0000, 0x7fff },
	{ MemoryViewer::gvram0,  "GVRAM0", 0xc000, 0xffff },
	{ MemoryViewer::gvram1,  "GVRAM1", 0xc000, 0xffff },
	{ MemoryViewer::gvram2,  "GVRAM2", 0xc000, 0xffff },
	{ MemoryViewer::tvram,   "TVRAM",  0xf000, 0xffff },
};
static const int nwatchbanks = sizeof(watchbanks) / sizeof(watchbanks[0]);

// サブ CPU のウォッチが置ける範囲
static const uint watchsub_lo = 0x0000, watchsub_hi = 0x7fff;

inline const WatchBank* FindWatchBank(int type)
{
	for (int i = 0; i < nwatchbanks; i++)
		if (watchbanks[i].type == type)
			return &watchbanks[i];
	return 0;
}

inline const WatchBank* FindWatchBankByName(const char* name)
{
	for (int i = 0; i < nwatchbanks; i++)
		if (!lstrcmpi(watchbanks[i].name, name))
			return &watchbanks[i];
	return 0;
}

inline const char* WatchBankName(int type)
{
	const WatchBank* b = FindWatchBank(type);
	return b ? b->name : "?";
}

// addr から size バイトが、そのバンクの範囲に入るか
inline bool WatchBankFits(const WatchBank* b, uint addr, uint size)
{
	return b && addr >= b->lo && addr + size - 1 <= b->hi;
}

// MemoryViewer の見え方を、そのバンクの番地が見えるように切り替える
inline void SelectWatchBank(MemoryViewer& mv, int type)
{
	MemoryViewer::Type t = MemoryViewer::Type(type);
	MemoryViewer::Type ram = MemoryViewer::mainram;
	switch (t)
	{
	case MemoryViewer::n88rom: case MemoryViewer::nrom:
	case MemoryViewer::n80rom: case MemoryViewer::n80srrom:
	case MemoryViewer::eram0: case MemoryViewer::eram1:
	case MemoryViewer::eram2: case MemoryViewer::eram3:
		mv.SelectBank(t, t, ram, ram, ram);
		break;
	case MemoryViewer::n88e0: case MemoryViewer::n88e1:
	case MemoryViewer::n88e2: case MemoryViewer::n88e3:
		mv.SelectBank(ram, t, ram, ram, ram);
		break;
	case MemoryViewer::n80sr1:
		mv.SelectBank(MemoryViewer::n80srrom, t, ram, ram, ram);
		break;
	case MemoryViewer::gvram0: case MemoryViewer::gvram1: case MemoryViewer::gvram2:
		mv.SelectBank(ram, ram, ram, t, ram);
		break;
	case MemoryViewer::tvram:
		mv.SelectBank(ram, ram, ram, ram, t);
		break;
	default:
		mv.SelectBank(ram, ram, ram, ram, ram);
		break;
	}
}

}
