// ----------------------------------------------------------------------------
//	M88 - PC-8801 series emulator
//	Copyright (C) cisc 1999.
// ----------------------------------------------------------------------------
//	$Id: memview.cpp,v 1.5 2001/02/21 11:57:57 cisc Exp $

#include "headers.h"
#include "device.h"
#include "device_i.h"
#include "pc88/memory.h"
#include "memview.h"

using namespace PC8801;

// ----------------------------------------------------------------------------
//
//
MemoryViewer::MemoryViewer()
{
	mem1 = 0;
}

MemoryViewer::~MemoryViewer()
{
}

// ----------------------------------------------------------------------------
//
//
bool MemoryViewer::Init(PC88* _pc)
{
	if (!bus.Init(0x10000 >> MemoryBus::pagebits))
		return false;
	pc = _pc;
	mem1 = pc->GetMem1();
	mem2 = pc->GetMem2();
	z80  = 0;
#ifdef Z80C_STATISTICSS
	stat = 0;
#endif

	SelectBank(mainram, mainram, mainram, mainram, mainram); 
	return true;
}

// ----------------------------------------------------------------------------
//
//
void MemoryViewer::SelectBank(Type a0, Type a6, Type a8, Type ac, Type af)
{
	uint8* p;
	bank[0] = a0;
	bank[2] = a8;
	bank[3] = ac;
	bank[4] = af;

	if (a0 != sub)
	{
		z80 = pc->GetCPU1();
		// a0
		switch (a0)
		{
		case n88rom: p = mem1->GetROM()+Memory::n88; break;
		case nrom:   p = mem1->GetROM()+Memory::n80; break;
		case eram0:  p = mem1->GetERAM(0); break;
		case eram1:  p = mem1->GetERAM(1); break;
		case eram2:  p = mem1->GetERAM(2); break;
		case eram3:  p = mem1->GetERAM(3); break;
		default:	 p = mem1->GetRAM(); break;
		}
		bus.SetMemorys(0x0000, 0x6000, p);
		// a6
		bank[1] = a6;
		switch (a6)
		{
		case n88rom: p = mem1->GetROM()+Memory::n88+0x6000; break;
		case nrom:   p = mem1->GetROM()+Memory::n80+0x6000; break;
		case n88e0: case n88e1: case n88e2: case n88e3:
			p = mem1->GetROM()+Memory::n88e+(a6-n88e0)*0x2000; break;
		case eram0:  p = mem1->GetERAM(0)+0x6000; break;
		case eram1:  p = mem1->GetERAM(1)+0x6000; break;
		case eram2:  p = mem1->GetERAM(2)+0x6000; break;
		case eram3:  p = mem1->GetERAM(3)+0x6000; break;
		default:	 p = mem1->GetRAM()+0x6000; break;
		}
		bus.SetMemorys(0x6000, 0x2000, p);
	}
	else
	{
		bank[1] = sub;
		z80 = pc->GetCPU2();
		bus.SetMemorys(0x0000, 0x2000, mem2->GetROM());
		bus.SetMemorys(0x2000, 0x2000, mem2->GetROM());
		bus.SetMemorys(0x4000, 0x4000, mem2->GetRAM());
	}
#ifdef Z80C_STATISTICS
	stat = z80->GetStatistics();
#endif
	bus.SetMemorys(0x8000, 0x4000, mem1->GetRAM()+0x8000);	// $8000-$bfff は常に RAM

	if (ac == gvram0 || ac == gvram1 || ac == gvram2)
	{
		// GVRAM はプレーン・インターリーブなので SetMemorys では貼れない。
		// $c000-$ffff の 16KB 全部を関数マッピングで見せる
		static const MemoryBus::ReadFuncPtr rdfunc[3]  = { RdGV0, RdGV1, RdGV2 };
		static const MemoryBus::WriteFuncPtr wrfunc[3] = { WrGV0, WrGV1, WrGV2 };
		int n = ac - gvram0;
		bus.SetFuncs(0xc000, 0x4000, this, rdfunc[n], wrfunc[n]);
		bank[4] = ac;	// $f000 台の R/W ウォッチ色付けもプレーンに合わせる
	}
	else
	{
		bus.SetMemorys(0xc000, 0x3000, mem1->GetRAM()+0xc000);
		// af
		switch (af)
		{
		case tvram: p = mem1->GetTVRAM(); break;
		default:	p = mem1->GetRAM() + 0xf000; break;
		}
		bus.SetMemorys(0xf000, 0x1000, p);
	}
}

// ----------------------------------------------------------------------------
//	GVRAM プレーンの読み書き ($c000-$ffff に貼る関数マッピング用)
//
uint MEMCALL MemoryViewer::RdGV0(void* inst, uint addr)
{
	MemoryViewer* m = STATIC_CAST(MemoryViewer*, inst);
	return m->mem1->GetGVRAM()[addr & 0x3fff].byte[0];
}

uint MEMCALL MemoryViewer::RdGV1(void* inst, uint addr)
{
	MemoryViewer* m = STATIC_CAST(MemoryViewer*, inst);
	return m->mem1->GetGVRAM()[addr & 0x3fff].byte[1];
}

uint MEMCALL MemoryViewer::RdGV2(void* inst, uint addr)
{
	MemoryViewer* m = STATIC_CAST(MemoryViewer*, inst);
	return m->mem1->GetGVRAM()[addr & 0x3fff].byte[2];
}

void MEMCALL MemoryViewer::WrGV0(void* inst, uint addr, uint data)
{
	MemoryViewer* m = STATIC_CAST(MemoryViewer*, inst);
	m->mem1->GetGVRAM()[addr & 0x3fff].byte[0] = data;
	m->mem1->GetDirtyFlag()[(addr & 0x3fff) >> 4] = 1;
}

void MEMCALL MemoryViewer::WrGV1(void* inst, uint addr, uint data)
{
	MemoryViewer* m = STATIC_CAST(MemoryViewer*, inst);
	m->mem1->GetGVRAM()[addr & 0x3fff].byte[1] = data;
	m->mem1->GetDirtyFlag()[(addr & 0x3fff) >> 4] = 1;
}

void MEMCALL MemoryViewer::WrGV2(void* inst, uint addr, uint data)
{
	MemoryViewer* m = STATIC_CAST(MemoryViewer*, inst);
	m->mem1->GetGVRAM()[addr & 0x3fff].byte[2] = data;
	m->mem1->GetDirtyFlag()[(addr & 0x3fff) >> 4] = 1;
}


