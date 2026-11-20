// ---------------------------------------------------------------------------
//	Z80 emulator for x86/VC5.
//	Copyright (C) cisc 1997, 1999.
// ---------------------------------------------------------------------------
//	$Id: Z80_x86.h,v 1.15 2000/11/02 12:43:44 cisc Exp $

#ifndef z80_x86_h
#define z80_x86_h

#include "device.h"
#include "memmgr.h"
#include "Z80.h"
#include "z80debughook.h"

#ifndef PTR_IDBIT
#error [Z80_x86] PTR_IDBIT definition is required
#endif

// ---------------------------------------------------------------------------

class Z80_x86 : public Device
{
public:
	enum
	{
		reset = 0, irq, nmi,
	};

public:
	Z80_x86(const ID& id);
	~Z80_x86();

	bool Init(MemoryManager* mm, IOBus* bus, int iack);
	
	int Exec(int clocks);
	int ExecOne();
	static int __stdcall ExecSingle(Z80_x86* first, Z80_x86* second, int clocks);
	static int __stdcall ExecDual  (Z80_x86* first, Z80_x86* second, int clocks);
	static int __stdcall ExecDual2 (Z80_x86* first, Z80_x86* second, int clocks);
	void Stop(int clocks);
	static void StopDual(int clocks);
	static int GetCCount();
	int GetCount();
	
	void IOCALL Reset(uint=0, uint=0);
	void IOCALL IRQ(uint, uint d);
	void IOCALL NMI(uint=0, uint=0);
	void Wait(bool);

	uint GetPC() { return (uint) inst + (uint) instbase; }

	bool GetPages(MemoryPage** rd, MemoryPage** wr) { *rd = rdpages, *wr = wrpages; return true; }
	int* GetWaits() { return waittable; }
	const Descriptor* IFCALL GetDesc() const { return &descriptor; }

	uint IFCALL GetStatusSize() { return sizeof(CPUState); }
	bool IFCALL LoadStatus(const uint8* status);
	bool IFCALL SaveStatus(uint8* status);
	
	bool EnableDump(bool) { return false; }
	int GetDumpState() { return -1; }

public:
	// Debug Service Functions
	void TestIntr();
	void SetPC(uint n) { inst = (uint8*)n; instbase = 0; instpage = (uint8*)-1; instlim = 0; }
	const Z80Reg& GetReg() { return reg; }
	bool IsIntr() { return !!intr; }

	// デバッガ (M88am のデバッガを使うため。Z80C と同じ形)
	//	SetDebugHook: 1 命令ごとに呼ぶフックを登録する。SetDebugActive が立っている間だけ、命令ごとのループと
	//	ExecSingle などの先頭の 1 命令の前で CheckExecBreak を呼ぶ (true なら実行を止める)
	void SetDebugHook(IZ80DebugHook* hook, int cpuid) { debughook = hook, debugcpuid = cpuid; }
	void SetDebugActive(bool a) { dbgactive = (a && debughook) ? 1 : 0; }
	// AF (Z80 の並び: A が上位、F が下位)。Z80_x86 はメモリの上で A が下位・F が上位 (R が最上位) で、
	// F の未定義のビット (3・5) は flagn に持つ。裏の AF' (r_af) は Z80 の並びのまま
	uint GetAFSync() { return ((reg.r.w.af & 0xff) << 8) | ((reg.r.w.af >> 8) & 0xd7) | (flagn & 0x28); }
	void SetAFSync(uint v) { reg.r.w.af = (reg.r.w.af & 0xff000000) | ((v >> 8) & 0xff) | ((v & 0xff) << 8); flagn = uint8(v); }
	Z80Reg& GetRegRef() { return reg; }			// レジスターの窓の書き換え用 (止めている間だけ。AF は SetAFSync)
	// 割り込みを受け付けた回数・そのときの戻り番地・push する直前の SP・push したか (IM0 はしない)
	uint GetIntAckCount() const { return intackcount; }
	uint GetIntAckPC() const { return intackpc; }
	uint GetIntAckSP() const { return intacksp; }
	bool IsIntAckPushed() const { return !!intackpushed; }
	friend int __cdecl Z80x86DebugEntry(Z80_x86* cpu);

private:
	enum
	{
		pagebits   = MemoryManager::pagebits,
		idbit      = MemoryManager::idbit,
	};
	enum
	{
		ssrev = 1,
	};
	struct CPUState
	{
		Z80Reg reg;
		uint8 intr;
		uint8 waitstate;
		uint8 flagn;
		uint8 rev;
		uint execcount;
	};

private:
	Z80Reg reg;
	uint8* inst;
	uint8* instbase;
	uint8* instlim;
	uint8* instpage;
	uint   instwait;
	const IOBus::InBank* ins;
	const IOBus::OutBank* outs;
	const uint8* ioflags;
	
	uint8 intr;
	uint8 waitstate;
	uint8 flagn;
	uint8 eshift;

	uint execcount;
	int stopcount;
	int delaycount;
	int clockcount;
	int intack;
	int startcount;

	IZ80DebugHook* debughook;
	int debugcpuid;
	uint8 dbgactive;			// 1: 命令ごとにフックを呼ぶ
	uint8 intackpushed;
	uint intackcount;
	uint intackpc;
	uint intacksp;

	MemoryPage rdpages[0x10000 >> MemoryManager::pagebits];
	MemoryPage wrpages[0x10000 >> MemoryManager::pagebits];
	int waittable[0x10000 >> MemoryManager::pagebits];
	
	static const Descriptor descriptor;
	static const OutFuncPtr outdef[];
	static Z80_x86* currentcpu;
};

// ---------------------------------------------------------------------------

inline int Z80_x86::GetCount()
{
	return execcount + (clockcount << eshift); 
}

#endif // z80_x86_h
