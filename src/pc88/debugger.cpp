// ---------------------------------------------------------------------------
//	M88 - PC-8801 emulator
//	デバッガコア (ブレークポイント/実行制御の管理)
//	Copyright (C) 2026
//	2-clause BSD license
//
//	M88am (M88 の派生、2026/09-01 版) の src/pc88/debugger.cpp を取り込んだもの。
//	M88gn での変更: Z80_x86 に有効かどうかを知らせる (UpdateActive)、
//	ブレーク中に呼ばれたら何もせずに止める (CheckExecBreak)、メモリのブレークで Stop(0) を呼ばない (DoMemRead・DoMemWrite)
// ---------------------------------------------------------------------------

#include "headers.h"
#include "pc88/debugger.h"
#include "pc88/pc88.h"
#include "pc88/memory.h"
#include "pc88/memview.h"
#include "misc.h"

namespace PC8801
{

// ---------------------------------------------------------------------------
//	構築/消滅
//
Debugger::Debugger()
: vm(0), hasmembreak(false), broken(false), reason(brk_none), breakcpu(cpu_main), breakaddr(0), breaklen(0), breakindex(-1), breakdata(-1), breakpc(0), suppressbreak(false),
  stepmode(step_none), steptarget(cpu_main), stepfirstcheck(false), stepoveraddr(0), stepoversp(0),
  intskipenabled(true), intskip(false), intskipretaddr(0), intskipsp(0), intskipcount(0), active(false),
  tracemode(false), tracecapacity(0), tracebuf(0), tracehead(0), tracecount(0),
  sublog(false), subloghs(false), sublogbuf(0), subloghead(0), sublogcount(0)
{
	for (int c = 0; c < 2; c++)
	{
		iodev[c].dbg = this;
		iodev[c].cpu = CpuTarget(c);
		iobus[c] = 0;
		lastpc[c] = 0;
		haspatbreak[c] = false;
	}
	memset(execbreak, 0, sizeof(execbreak));
	execbreakcount[cpu_main] = execbreakcount[cpu_sub] = 0;
	hasexecbreak[cpu_main] = hasexecbreak[cpu_sub] = false;
	resumeskippc[cpu_main] = resumeskippc[cpu_sub] = 0;
	resumeskip[cpu_main] = resumeskip[cpu_sub] = false;

	for (int c = 0; c < 2; c++)
	{
		mm[c] = 0;
		memmid[c] = -1;
		tracedepth[c] = 0;
		tracesp[c] = 0;
		tracefirst[c] = false;
		lastintack[c] = 0;
	}
}

Debugger* Debugger::current = 0;

Debugger::~Debugger()
{
	if (current == this)
		current = 0;
	delete[] tracebuf;
	delete[] sublogbuf;
}

// ---------------------------------------------------------------------------
//	初期化
//
bool Debugger::Init(PC88* _vm)
{
	vm = _vm;
	current = this;		// 条件式の書き方の確認でラベルを引くため (M88gn)

	// メモリ読み書きブレークポイント用に、MemoryMonitor とは別の
	// Connect ID を取得しておく(MemoryMonitor は開いたときだけ接続するが、
	// こちらは起動直後から常時接続しておく)
	mm[cpu_main] = vm->GetMemoryManager1();
	mm[cpu_sub]  = vm->GetMemoryManager2();
	memmid[cpu_main] = mm[cpu_main] ? mm[cpu_main]->Connect(this, true) : -1;
	memmid[cpu_sub]  = mm[cpu_sub]  ? mm[cpu_sub]->Connect(this, true)  : -1;

	// I/O のブレーク・ログ用 (M88gn)
	iobus[cpu_main] = vm->GetBus1();
	iobus[cpu_sub]  = vm->GetBus2();

	SetTraceCapacity(tracecapacity_max);

	return true;
}

void Debugger::Cleanup()
{
	sublog = false;				// I/O のつなぎも外れる (M88gn)
	DeleteAllBreakPoints();
	DeleteAllWatches();
	ClearBreak();
	CancelStep();

	delete[] tracebuf;
	tracebuf = 0;
	tracecapacity = 0;
	tracehead = 0;
	tracecount = 0;

	for (int c = 0; c < 2; c++)
	{
		if (mm[c] && memmid[c] >= 0)
			mm[c]->Disconnect(uint(memmid[c]));
		memmid[c] = -1;
	}
}

// ---------------------------------------------------------------------------
//	実行ブレークポイントの設定/解除。内部では bplist への Add/Delete に
//	委譲する(CodeMonitor::ToggleBreakAt がそのまま動くように、addr 1バイト
//	ぶんの exec 専用エントリとして扱う)
//
void Debugger::SetExecBreak(CpuTarget cpu, uint addr, bool set)
{
	addr &= 0xffff;

	if (set)
	{
		BreakPoint bp;
		bp.addr = addr;
		bp.length = 1;
		bp.cpu = uint8(cpu);
		bp.exec = true;
		bp.read = false;
		bp.write = false;
		bp.enabled = true;
		bp.comment[0] = 0;
		bp.io = false;
		bp.cond[0] = 0;
		bp.hitcount = 0;
		bp.hits = 0;
		AddBreakPoint(bp);
	}
	else
	{
		// addr を含む exec 属性のエントリを探し、exec フラグだけ落とす。
		// read/write も持っていなければエントリごと削除する
		bool changed = false;
		for (size_t i = 0; i < bplist.size(); )
		{
			BreakPoint& bp = bplist[i];
			if (!bp.io && bp.cpu == uint8(cpu) && bp.exec && addr >= bp.addr && addr < bp.addr + bp.length)
			{
				bp.exec = false;
				changed = true;
				if (!bp.read && !bp.write)
				{
					bplist.erase(bplist.begin() + i);
					continue;
				}
			}
			i++;
		}
		if (changed)
			RebuildBreakTables();
	}
}

bool Debugger::IsExecBreak(CpuTarget cpu, uint addr) const
{
	addr &= 0xffff;
	return !!(execbreak[cpu][addr >> 3] & (1 << (addr & 7)));
}

void Debugger::ClearExecBreaks(CpuTarget cpu)
{
	bool changed = false;
	for (size_t i = 0; i < bplist.size(); )
	{
		BreakPoint& bp = bplist[i];
		if (bp.cpu == uint8(cpu) && bp.exec)
		{
			bp.exec = false;
			changed = true;
			if (!bp.read && !bp.write)
			{
				bplist.erase(bplist.begin() + i);
				continue;
			}
		}
		i++;
	}
	if (changed)
		RebuildBreakTables();
}

// ---------------------------------------------------------------------------
//	ブレークポイント一覧の参照
//
bool Debugger::GetBreakPoint(int index, BreakPoint* bp) const
{
	if (index < 0 || size_t(index) >= bplist.size() || !bp)
		return false;
	*bp = bplist[index];
	return true;
}

int Debugger::FindExecBreakPoint(CpuTarget cpu, uint addr) const
{
	addr &= 0xffff;
	for (size_t i = 0; i < bplist.size(); i++)
	{
		const BreakPoint& bp = bplist[i];
		if (!bp.io && bp.cpu == uint8(cpu) && bp.exec && addr >= bp.addr && addr < bp.addr + bp.length)
			return int(i);
	}
	return -1;
}

// ---------------------------------------------------------------------------
//	ブレークポイントの追加/変更/削除
//	(呼び出し元で core->Lock()/Unlock() すること。bplist は再確保で
//	 ポインタが動くため、VM スレッドと競合してはいけない)
//
int Debugger::AddBreakPoint(const BreakPoint& bp)
{
	BreakPoint nb = bp;
	nb.addr &= 0xffff;
	if (nb.length < 1)
		nb.length = 1;
	nb.length = Min(nb.length, 0x10000 - nb.addr);
	// 受け取った構造体は呼び出し側で comment を埋めていないことがある。
	// 必ず終端させてから保持する
	nb.comment[sizeof(nb.comment)-1] = 0;
	nb.cond[sizeof(nb.cond)-1] = 0;
	nb.hits = 0;

	// 同一範囲(cpu/addr/length が一致)のエントリがあれば、
	// 新規に増やさず属性を統合する
	for (size_t i = 0; i < bplist.size(); i++)
	{
		BreakPoint& e = bplist[i];
		if (e.cpu == nb.cpu && e.addr == nb.addr && e.length == nb.length && e.io == nb.io
			&& !e.pattern && !nb.pattern)		// パターンのブレークはまとめない (M88gn)
		{
			e.exec |= nb.exec;
			e.read |= nb.read;
			e.write |= nb.write;
			e.enabled = e.enabled || nb.enabled;
			RebuildBreakTables();
			return int(i);
		}
	}

	bplist.push_back(nb);
	RebuildBreakTables();
	return int(bplist.size()) - 1;
}

bool Debugger::SetBreakPoint(int index, const BreakPoint& bp)
{
	if (index < 0 || size_t(index) >= bplist.size())
		return false;

	BreakPoint nb = bp;
	nb.addr &= 0xffff;
	if (nb.length < 1)
		nb.length = 1;
	nb.length = Min(nb.length, 0x10000 - nb.addr);
	// 受け取った構造体は呼び出し側で comment を埋めていないことがある。
	// 必ず終端させてから保持する
	nb.comment[sizeof(nb.comment)-1] = 0;
	nb.cond[sizeof(nb.cond)-1] = 0;
	nb.hits = 0;

	bplist[index] = nb;
	RebuildBreakTables();
	return true;
}

bool Debugger::DeleteBreakPoint(int index)
{
	if (index < 0 || size_t(index) >= bplist.size())
		return false;

	bplist.erase(bplist.begin() + index);
	RebuildBreakTables();
	return true;
}

void Debugger::DeleteAllBreakPoints()
{
	bplist.clear();
	RebuildBreakTables();
}

void Debugger::EnableAllBreakPoints(bool enable)
{
	for (size_t i = 0; i < bplist.size(); i++)
		bplist[i].enabled = enable;
	RebuildBreakTables();
}

// ---------------------------------------------------------------------------
//	ウォッチエントリの追加/変更/削除
//	(呼び出し元で core->Lock()/Unlock() すること。判定用の派生データを
//	 持たないので RebuildBreakTables() を呼ぶ必要は無い)
//
bool Debugger::GetWatch(int index, WatchEntry* w) const
{
	if (index < 0 || size_t(index) >= watchlist.size() || !w)
		return false;
	*w = watchlist[index];
	return true;
}

int Debugger::AddWatch(const WatchEntry& w)
{
	WatchEntry nw = w;
	nw.addr &= 0xffff;
	if (nw.size != 2)
		nw.size = 1;
	nw.label[sizeof(nw.label) - 1] = 0;

	watchlist.push_back(nw);
	return int(watchlist.size()) - 1;
}

bool Debugger::SetWatch(int index, const WatchEntry& w)
{
	if (index < 0 || size_t(index) >= watchlist.size())
		return false;

	WatchEntry nw = w;
	nw.addr &= 0xffff;
	if (nw.size != 2)
		nw.size = 1;
	nw.label[sizeof(nw.label) - 1] = 0;

	watchlist[index] = nw;
	return true;
}

bool Debugger::DeleteWatch(int index)
{
	if (index < 0 || size_t(index) >= watchlist.size())
		return false;

	watchlist.erase(watchlist.begin() + index);
	return true;
}

void Debugger::DeleteAllWatches()
{
	watchlist.clear();
}

// ---------------------------------------------------------------------------
//	ラベル (M88gn)
//
const char* Debugger::GetLabelExact(int cpu, uint addr, int bank) const
{
	std::map<uint, std::string>::const_iterator i = labels.find(LabelKey(cpu, addr, bank));
	return i != labels.end() ? i->second.c_str() : 0;
}

const char* Debugger::GetLabel(int cpu, uint addr, int bank, int* foundbank) const
{
	if (!(cpu & 1) && bank >= 0 && bank != label_anybank)
	{
		const char* n = GetLabelExact(cpu, addr, bank);
		if (n)
		{
			if (foundbank)
				*foundbank = bank;
			return n;
		}
	}
	const char* n = GetLabelExact(cpu, addr, label_anybank);
	if (n && foundbank)
		*foundbank = label_anybank;
	return n;
}

void Debugger::SetLabel(int cpu, uint addr, int bank, const char* name)
{
	uint key = LabelKey(cpu, addr, bank);
	if (!name || !name[0])
		labels.erase(key);
	else
		labels[key] = std::string(name).substr(0, labelmax - 1);
}

bool Debugger::FindLabelAddr(const char* name, uint* addr, int* cpu) const
{
	for (std::map<uint, std::string>::const_iterator i = labels.begin(); i != labels.end(); ++i)
	{
		if (!lstrcmpi(i->second.c_str(), name))
		{
			*addr = i->first & 0xffff;
			if (cpu)
				*cpu = int(i->first >> 24);
			return true;
		}
	}
	return false;
}

bool Debugger::GetLabelAt(int index, int* cpu, uint* addr, int* bank, const char** name) const
{
	if (index < 0 || index >= int(labels.size()))
		return false;
	std::map<uint, std::string>::const_iterator i = labels.begin();
	std::advance(i, index);
	*cpu = int(i->first >> 24);
	*bank = int(i->first >> 16) & 0xff;
	*addr = i->first & 0xffff;
	*name = i->second.c_str();
	return true;
}

// ---------------------------------------------------------------------------
//	bplist から実行BPビットマップとメモリフックを作り直す。
//	bplist を変更した箇所は必ず最後にこれを呼ぶこと
//
void Debugger::RebuildBreakTables()
{
	// (1) 実行BPビットマップ
	memset(execbreak, 0, sizeof(execbreak));
	execbreakcount[cpu_main] = execbreakcount[cpu_sub] = 0;

	for (size_t i = 0; i < bplist.size(); i++)
	{
		const BreakPoint& bp = bplist[i];
		if (!bp.enabled || !bp.exec || bp.io)
			continue;
		uint cpu = bp.cpu & 1;
		uint end = Min(bp.addr + bp.length, 0x10000);
		for (uint a = bp.addr; a < end; a++)
		{
			uint8& byte = execbreak[cpu][a >> 3];
			uint8 mask = uint8(1 << (a & 7));
			if (!(byte & mask))
			{
				byte |= mask;
				execbreakcount[cpu]++;
			}
		}
	}
	hasexecbreak[cpu_main] = execbreakcount[cpu_main] != 0;
	hasexecbreak[cpu_sub]  = execbreakcount[cpu_sub] != 0;

	// (2) メモリフックの張り直し。まず両CPUぶん全解除してから、
	//	   有効な read/write エントリごとに個別に AllocR/AllocW する。
	//	   (外接範囲をまとめて1回 Alloc するより、関係ないアドレスで
	//	    フックが呼ばれなくなるぶん精密で速い)
	for (int c = 0; c < 2; c++)
	{
		if (!mm[c] || memmid[c] < 0)
			continue;
		mm[c]->ReleaseR(uint(memmid[c]), 0, 0x10000);
		mm[c]->ReleaseW(uint(memmid[c]), 0, 0x10000);
	}

	hasmembreak = false;
	haspatbreak[cpu_main] = haspatbreak[cpu_sub] = false;
	patold.assign(bplist.size(), -1);
	for (size_t i = 0; i < bplist.size(); i++)
	{
		const BreakPoint& bp = bplist[i];
		uint cpu = bp.cpu & 1;
		// パターンのブレーク (M88gn): 範囲の書き込みを見張る
		if (bp.pattern)
		{
			if (bp.enabled && bp.patlen && bp.length >= bp.patlen && mm[cpu] && memmid[cpu] >= 0)
			{
				mm[cpu]->AllocW(uint(memmid[cpu]), bp.addr, bp.length,
								 cpu == cpu_main ? MemWriteMain : MemWriteSub);
				hasmembreak = true;
				haspatbreak[cpu] = true;
			}
			continue;
		}
		if (!bp.enabled || (!bp.read && !bp.write) || bp.io)
			continue;
		if (!mm[cpu] || memmid[cpu] < 0)
			continue;

		if (bp.read)
		{
			mm[cpu]->AllocR(uint(memmid[cpu]), bp.addr, bp.length,
							 cpu == cpu_main ? MemReadMain : MemReadSub);
			hasmembreak = true;
		}
		if (bp.write)
		{
			mm[cpu]->AllocW(uint(memmid[cpu]), bp.addr, bp.length,
							 cpu == cpu_main ? MemWriteMain : MemWriteSub);
			hasmembreak = true;
		}
	}

	RebuildIOHooks();
	UpdateActive();
}

// ---------------------------------------------------------------------------
//	I/O ポートのブレーク・通信のログ (M88gn)。デバッガの装置 (DebugIO) を、要るポートにだけ
//	本物の装置と並べてつなぐ (IN は 0xff を返すので、本物の装置の値は変わらない)。
//	bplist やログの設定を変えたら呼ぶ (呼び出し元の Lock() 区間の中)
//
void Debugger::RebuildIOHooks()
{
	for (int c = 0; c < 2; c++)
	{
		if (!iobus[c])
			continue;
		iobus[c]->Disconnect(&iodev[c]);		// つないでいなければ何もしない

		bool in[256], out[256];
		memset(in, 0, sizeof(in));
		memset(out, 0, sizeof(out));
		for (size_t i = 0; i < bplist.size(); i++)
		{
			const BreakPoint& bp = bplist[i];
			if (!bp.enabled || !bp.io || (bp.cpu & 1) != uint(c))
				continue;
			for (uint p = bp.addr; p < bp.addr + bp.length && p < 0x100; p++)
			{
				if (bp.read)
					in[p] = true;
				if (bp.write)
					out[p] = true;
			}
		}
		if (sublog)
		{
			out[0xfc] = out[0xfd] = true;
			if (subloghs)
				out[0xfe] = out[0xff] = true;
		}
		for (uint p = 0; p < 0x100; p++)
		{
			if (in[p])
				iobus[c]->ConnectIn(p, &iodev[c], STATIC_CAST(Device::InFuncPtr, &DebugIO::In));
			if (out[p])
				iobus[c]->ConnectOut(p, &iodev[c], STATIC_CAST(Device::OutFuncPtr, &DebugIO::Out));
		}
	}
}

uint IOCALL Debugger::DebugIO::In(uint port)
{
	dbg->DoIOIn(cpu, port & 0xff);
	return 0xff;
}

void IOCALL Debugger::DebugIO::Out(uint port, uint data)
{
	dbg->DoIOOut(cpu, port & 0xff, data & 0xff);
}

void Debugger::DoIOIn(CpuTarget cpu, uint port)
{
	if (suppressbreak || broken)
		return;
	for (size_t i = 0; i < bplist.size(); i++)
	{
		const BreakPoint& bp = bplist[i];
		if (!bp.enabled || !bp.io || !bp.read || bp.cpu != uint8(cpu))
			continue;
		if (port < bp.addr || port >= bp.addr + bp.length)
			continue;
		if (!CondHit(bplist[i], cpu, lastpc[cpu], port, -1))		// IN で読む値はまだ分からない
			continue;
		// メモリのブレークと同じく、この命令は最後まで実行され、次の命令の前で止まる
		RequestBreak(brk_ioread, cpu, port, int(i));
		stepmode = step_none;
		break;
	}
}

void Debugger::DoIOOut(CpuTarget cpu, uint port, uint data)
{
	// 通信のログ
	if (sublog && sublogbuf && port >= 0xfc && (subloghs || port <= 0xfd))
	{
		SubLogEntry& e = sublogbuf[subloghead];
		e.tick = vm ? vm->GetCPUTick() : 0;
		e.pc = lastpc[cpu];
		e.cpu = uint8(cpu);
		e.port = uint8(port);
		e.data = uint8(data);
		subloghead = (subloghead + 1) % subloglimit;
		if (sublogcount < subloglimit)
			sublogcount++;
	}

	if (suppressbreak || broken)
		return;
	for (size_t i = 0; i < bplist.size(); i++)
	{
		const BreakPoint& bp = bplist[i];
		if (!bp.enabled || !bp.io || !bp.write || bp.cpu != uint8(cpu))
			continue;
		if (port < bp.addr || port >= bp.addr + bp.length)
			continue;
		if (!CondHit(bplist[i], cpu, lastpc[cpu], port, int(data)))
			continue;
		RequestBreak(brk_iowrite, cpu, port, int(i), int(data));
		stepmode = step_none;
		break;
	}
}

void Debugger::SetSubLogEnabled(bool e)
{
	if (e && !sublogbuf)
	{
		sublogbuf = new SubLogEntry[subloglimit];
		subloghead = sublogcount = 0;
	}
	sublog = e && sublogbuf;
	RebuildIOHooks();
	UpdateActive();			// ログを取る間は命令ごとの判定で PC を覚える
}

void Debugger::SetSubLogHandshake(bool h)
{
	subloghs = h;
	RebuildIOHooks();
}

bool Debugger::GetSubLogEntry(int index, SubLogEntry* e) const
{
	if (!sublogbuf || index < 0 || index >= sublogcount)
		return false;
	int start = (subloghead - sublogcount + subloglimit) % subloglimit;
	*e = sublogbuf[(start + index) % subloglimit];
	return true;
}

// ---------------------------------------------------------------------------
//	条件付きブレークポイント (M88gn)
//
//	式: 数は 16 進 (先頭は数字。0C・1234・1234H・0x1234・$1234)、#123 で 10 進。
//	レジスター A B C D E H L F I R AF BC DE HL IX IY SP PC IXH IXL IYH IYL AF' BC' DE' HL'、
//	フラグ ZF CF SF PF (= VF) HF NF、DATA (読み書きした値。IN では使えない)、ADDR (番地・ポート)、
//	[式] = その番地の 1 バイト、W[式] = 2 バイト (CPU が今見ているメモリ)。
//	演算子 ( ) ! ~ - * / % + - << >> < <= > >= == != & ^ | && || (C と同じ優先順位)。0 以外が真
//	レジスターは命令の直前の値 (メモリ・I/O のブレークでは、その命令が途中で変えた値のこともある)
//
struct CondEval
{
	Debugger* dbg;				// 0 なら書き方を確かめるだけ (値は読まない)
	Debugger::CpuTarget cpu;
	uint pc, addr;
	int data;
	const char* p;
	const char* err;

	void Skip() { while (*p == ' ' || *p == '\t') p++; }
	bool Eat(const char* op)
	{
		Skip();
		size_t n = strlen(op);
		if (strncmp(p, op, n))
			return false;
		// '&' と '&&'、'|' と '||'、'<' と '<<' / '<=' などを取り違えない
		if (n == 1 && (op[0] == '&' || op[0] == '|') && p[1] == op[0])
			return false;
		if (n == 1 && (op[0] == '<' || op[0] == '>') && (p[1] == '=' || p[1] == op[0]))
			return false;
		if (n == 1 && op[0] == '!' && p[1] == '=')
			return false;
		p += n;
		return true;
	}
	uint Mem(uint a)
	{
		if (!dbg || !dbg->mm[cpu] || dbg->memmid[cpu] < 0)
			return 0;
		return dbg->mm[cpu]->Read8P(uint(dbg->memmid[cpu]), a & 0xffff) & 0xff;
	}
	bool Reg(const char* name, uint* v);
	int Primary();
	int Unary();
	int Mul();
	int Add();
	int Shift();
	int Rel();
	int Eq();
	int BitAnd();
	int BitXor();
	int BitOr();
	int LAnd();
	int LOr();
};

bool CondEval::Reg(const char* n, uint* v)
{
	static const char* const names[] =
	{
		"A", "F", "B", "C", "D", "E", "H", "L", "I", "R",
		"AF", "BC", "DE", "HL", "IX", "IY", "SP", "PC", "IXH", "IXL", "IYH", "IYL",
		"AF'", "BC'", "DE'", "HL'", "ZF", "CF", "SF", "PF", "VF", "HF", "NF", "DATA", "ADDR", 0,
	};
	int k;
	for (k = 0; names[k]; k++)
		if (!_stricmp(n, names[k]))
			break;
	if (!names[k])
		return false;
	if (!dbg)
	{
		*v = 0;
		return true;
	}
	if (!_stricmp(n, "DATA") && data < 0)
	{
		err = "DATA は、このブレークポイント (IN・実行) では使えません";
		*v = 0;
		return true;
	}
	auto* z = cpu == Debugger::cpu_sub ? dbg->vm->GetCPU2() : dbg->vm->GetCPU1();
	const Z80Reg& r = z->GetReg();
	uint af = z->GetAFSync();
	uint f = af & 0xff;
	switch (k)
	{
	case 0: *v = af >> 8; break;
	case 1: *v = f; break;
	case 2: *v = r.r.w.bc >> 8; break;
	case 3: *v = r.r.w.bc & 0xff; break;
	case 4: *v = r.r.w.de >> 8; break;
	case 5: *v = r.r.w.de & 0xff; break;
	case 6: *v = r.r.w.hl >> 8; break;
	case 7: *v = r.r.w.hl & 0xff; break;
	case 8: *v = r.ireg; break;
	case 9: *v = (r.rreg & 0x7f) | (r.rreg7 & 0x80); break;
	case 10: *v = af; break;
	case 11: *v = r.r.w.bc & 0xffff; break;
	case 12: *v = r.r.w.de & 0xffff; break;
	case 13: *v = r.r.w.hl & 0xffff; break;
	case 14: *v = r.r.w.ix & 0xffff; break;
	case 15: *v = r.r.w.iy & 0xffff; break;
	case 16: *v = r.r.w.sp & 0xffff; break;
	case 17: *v = pc & 0xffff; break;
	case 18: *v = (r.r.w.ix >> 8) & 0xff; break;
	case 19: *v = r.r.w.ix & 0xff; break;
	case 20: *v = (r.r.w.iy >> 8) & 0xff; break;
	case 21: *v = r.r.w.iy & 0xff; break;
	case 22: *v = r.r_af & 0xffff; break;
	case 23: *v = r.r_bc & 0xffff; break;
	case 24: *v = r.r_de & 0xffff; break;
	case 25: *v = r.r_hl & 0xffff; break;
	case 26: *v = (f >> 6) & 1; break;		// ZF
	case 27: *v = f & 1; break;				// CF
	case 28: *v = (f >> 7) & 1; break;		// SF
	case 29: case 30: *v = (f >> 2) & 1; break;	// PF / VF
	case 31: *v = (f >> 4) & 1; break;		// HF
	case 32: *v = (f >> 1) & 1; break;		// NF
	case 33: *v = uint(data) & 0xff; break;
	case 34: *v = addr & 0xffff; break;
	}
	return true;
}

int CondEval::Primary()
{
	Skip();
	if (err)
		return 0;
	if (*p == '(')
	{
		p++;
		int v = LOr();
		if (!Eat(")"))
			err = "「)」がありません";
		return v;
	}
	bool word = false;
	if ((p[0] == 'W' || p[0] == 'w') && p[1] == '[')
		word = true, p++;
	if (*p == '[')
	{
		p++;
		uint a = uint(LOr());
		if (!Eat("]"))
			err = "「]」がありません";
		return int(word ? (Mem(a) | (Mem(a + 1) << 8)) : Mem(a));
	}
	if (*p == '#' || *p == '$' || (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) || isdigit(uint8(*p)))
	{
		int base = 16;
		if (*p == '#')
			base = 10, p++;
		else if (*p == '$')
			p++;
		else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
			p += 2;
		char* end;
		uint v = strtoul(p, &end, base);
		if (end == p)
		{
			err = "数の書き方が違います";
			return 0;
		}
		p = end;
		if (base == 16 && (*p == 'H' || *p == 'h'))
			p++;
		return int(v);
	}
	// 名前: レジスター・フラグ、無ければラベル (M88gn。ラベルに使える字 _ . ? @ $ も読む)
	if (isalpha(uint8(*p)) || *p == '_' || *p == '.' || *p == '?' || *p == '@')
	{
		char name[Debugger::labelmax];
		int n = 0;
		while ((isalnum(uint8(*p)) || *p == '\'' || *p == '_' || *p == '.' || *p == '?' || *p == '@' || *p == '$')
			   && n < Debugger::labelmax - 1)
			name[n++] = *p++;
		name[n] = 0;
		uint v;
		const Debugger* ld = dbg ? dbg : Debugger::current;
		if (!Reg(name, &v) && !(ld && ld->FindLabelAddr(name, &v)))
		{
			static char msg[64];
			wsprintf(msg, "「%s」は知らない名前です", name);
			err = msg;
			return 0;
		}
		return int(v);
	}
	err = *p ? "式の書き方が違います" : "式が途中で終わっています";
	return 0;
}

int CondEval::Unary()
{
	Skip();
	if (Eat("!"))	return !Unary();
	if (Eat("~"))	return ~Unary();
	if (Eat("-"))	return -Unary();
	return Primary();
}

int CondEval::Mul()
{
	int v = Unary();
	for (;;)
	{
		if (Eat("*"))		v *= Unary();
		else if (Eat("/"))	{ int d = Unary(); v = d ? v / d : 0; }
		else if (Eat("%"))	{ int d = Unary(); v = d ? v % d : 0; }
		else return v;
	}
}

int CondEval::Add()
{
	int v = Mul();
	for (;;)
	{
		if (Eat("+"))		v += Mul();
		else if (Eat("-"))	v -= Mul();
		else return v;
	}
}

int CondEval::Shift()
{
	int v = Add();
	for (;;)
	{
		if (Eat("<<"))		v <<= Add();
		else if (Eat(">>"))	v = int(uint(v) >> Add());
		else return v;
	}
}

int CondEval::Rel()
{
	int v = Shift();
	for (;;)
	{
		if (Eat("<="))		v = v <= Shift();
		else if (Eat(">="))	v = v >= Shift();
		else if (Eat("<"))	v = v < Shift();
		else if (Eat(">"))	v = v > Shift();
		else return v;
	}
}

int CondEval::Eq()
{
	int v = Rel();
	for (;;)
	{
		if (Eat("=="))		v = v == Rel();
		else if (Eat("!="))	v = v != Rel();
		else return v;
	}
}

int CondEval::BitAnd()	{ int v = Eq();     while (Eat("&")) v &= Eq();     return v; }
int CondEval::BitXor()	{ int v = BitAnd(); while (Eat("^")) v ^= BitAnd(); return v; }
int CondEval::BitOr()	{ int v = BitXor(); while (Eat("|")) v |= BitXor(); return v; }
int CondEval::LAnd()	{ int v = BitOr();  while (Eat("&&")) { int w = BitOr(); v = v && w; } return v; }
int CondEval::LOr()		{ int v = LAnd();   while (Eat("||")) { int w = LAnd(); v = v || w; } return v; }

bool Debugger::CheckCondition(const char* cond, char* err)
{
	err[0] = 0;
	CondEval e;
	e.dbg = 0;
	e.cpu = cpu_main;
	e.pc = e.addr = 0;
	e.data = 0;
	e.p = cond;
	e.err = 0;
	e.Skip();
	if (!*e.p)
		return true;			// 空なら条件無し
	e.LOr();
	e.Skip();
	if (!e.err && *e.p)
		e.err = "式の後ろに余分な文字があります";
	if (e.err)
	{
		wsprintf(err, "%s (%d 文字目)", e.err, int(e.p - cond) + 1);
		return false;
	}
	return true;
}

const char* Debugger::CondHelp()
{
	return
		"条件 (Condition): 式が 0 以外のときだけ止めます。空なら毎回。\r\n"
		"\r\n"
		"数: 16 進で書きます (先頭は数字: 0C, 1234, 0FFH, 0x1234, $1234)。#100 は 10 進。\r\n"
		"レジスター: A B C D E H L F I R  AF BC DE HL IX IY SP PC\r\n"
		"　　IXH IXL IYH IYL  AF' BC' DE' HL'\r\n"
		"フラグ: ZF CF SF PF (VF) HF NF (0 か 1)\r\n"
		"DATA: 読み書きした値 (メモリの読み・書き、OUT。実行と IN では使えません)\r\n"
		"ADDR: アクセスした番地・ポート\r\n"
		"[式]: その番地の 1 バイト、W[式]: 2 バイト (CPU が今見ているメモリ)\r\n"
		"ラベルの名前: その番地 (例: [WORK]==0、PC==START)\r\n"
		"演算子: ( ) ! ~ - * / % + - << >> < <= > >= == != & ^ | && ||\r\n"
		"\r\n"
		"例:  A==3   BC>=1000   ZF && HL==0E000   [0E6CB]==0FF   DATA&80\r\n"
		"\r\n"
		"回数 (Hit count): 条件が成り立った回数がこの数になったときから止めます (0 は毎回)。\r\n"
		"回数はブレークポイントを変更すると 0 に戻ります。\r\n"
		"レジスターは命令の直前の値です (メモリ・I/O のブレークでは、その命令が途中で変えた値のこともあります)。";
}

// 条件と回数を見て、止めるなら true。条件の式は毎回読み直す (誤りがあれば止める)
bool Debugger::CondHit(BreakPoint& bp, CpuTarget cpu, uint pc, uint addr, int data)
{
	if (bp.cond[0])
	{
		CondEval e;
		e.dbg = this;
		e.cpu = cpu;
		e.pc = pc;
		e.addr = addr;
		e.data = data;
		e.p = bp.cond;
		e.err = 0;
		int v = e.LOr();
		if (!e.err && !v)
			return false;
	}
	bp.hits++;
	return bp.hitcount <= 1 || bp.hits >= bp.hitcount;
}

// 実行のブレーク: その番地を含む実行のブレークポイントのどれかの条件が成り立てば止める
int Debugger::ExecCondHit(int cpuid, uint pc)
{
	int hit = -1;
	for (size_t i = 0; i < bplist.size(); i++)
	{
		BreakPoint& bp = bplist[i];
		if (!bp.enabled || !bp.exec || bp.io || bp.cpu != uint8(cpuid))
			continue;
		if (pc < bp.addr || pc >= bp.addr + bp.length)
			continue;
		if (CondHit(bp, CpuTarget(cpuid), pc, pc, -1) && hit < 0)
			hit = int(i);			// 回数を数えるため、残りも見る
	}
	return hit;
}

// ---------------------------------------------------------------------------
//	CheckExecBreak 冒頭の高速パス用フラグを再計算する
//
void Debugger::UpdateActive()
{
	bool hasiobreak = false;
	for (size_t i = 0; i < bplist.size() && !hasiobreak; i++)
		hasiobreak = bplist[i].enabled && bplist[i].io && (bplist[i].read || bplist[i].write);
	active = hasexecbreak[cpu_main] || hasexecbreak[cpu_sub] || hasmembreak
		  || (stepmode != step_none) || broken || tracemode || sublog || hasiobreak;
	// Z80_x86 は命令ごとのループでこの印だけを見て、立っているときだけ CheckExecBreak を呼ぶ
	if (vm)
	{
		vm->GetCPU1()->SetDebugActive(active);
		vm->GetCPU2()->SetDebugActive(active);
	}
}

// ---------------------------------------------------------------------------
//	ブレーク状態の設定/解除
//
void Debugger::RequestBreak(BreakReason r, CpuTarget cpu, uint addr, int index, int data)
{
	broken = true;
	reason = r;
	breakcpu = cpu;
	breakaddr = addr & 0xffff;
	breaklen = 0;
	breakindex = index;
	breakdata = data;
	// 読み書き・I/O は命令の途中で来るので、その命令の番地 (lastpc はデバッガが有効な間は正しい)
	breakpc = (r == brk_exec || r == brk_step || r == brk_user) ? (addr & 0xffff) : lastpc[cpu & 1];
	UpdateActive();
}

void Debugger::ClearBreak()
{
	broken = false;
	reason = brk_none;
	UpdateActive();
}

// ---------------------------------------------------------------------------
//	再開直後の空振り防止。両CPUの現在のPCを覚えておき、CheckExecBreak で
//	1回だけ見逃す(§1.2)
//
void Debugger::ArmResumeSkip()
{
	if (!vm)
		return;
	resumeskippc[cpu_main] = vm->GetCPU1()->GetPC() & 0xffff;
	resumeskippc[cpu_sub]  = vm->GetCPU2()->GetPC() & 0xffff;
	resumeskip[cpu_main] = resumeskip[cpu_sub] = true;
}

// ---------------------------------------------------------------------------
//	ステップイン/ステップオーバーの開始
//
void Debugger::RequestStepIn(CpuTarget cpu)
{
	SyncIntAck();
	stepmode = step_in;
	steptarget = cpu;
	stepfirstcheck = true;
	stepoveraddr = 0;
	UpdateActive();
}

void Debugger::RequestStepOver(CpuTarget cpu, uint retaddr, uint framesp)
{
	SyncIntAck();
	stepmode = step_over;
	steptarget = cpu;
	stepfirstcheck = true;
	stepoveraddr = retaddr & 0xffff;
	stepoversp = framesp & 0xffff;
	UpdateActive();
}

void Debugger::CancelStep()
{
	SyncIntAck();
	stepmode = step_none;
	UpdateActive();
}

// ---------------------------------------------------------------------------
//	割り込みスキップの設定
//
void Debugger::SetIntSkipEnabled(bool e)
{
	intskipenabled = e;
	if (!e)
		intskip = false;
}

// ---------------------------------------------------------------------------
//	受理カウンタを現在値に合わせ、スキップ状態を落とす
//	(ステップ開始時/中止時に呼ぶ。古い受理カウンタが残っていると、
//	 次のステップの1命令目で誤検知する)
//
void Debugger::SyncIntAck()
{
	intskip = false;
	intskipcount = 0;
	if (vm)
	{
		lastintack[cpu_main] = vm->GetCPU1()->GetIntAckCount();
		lastintack[cpu_sub]  = vm->GetCPU2()->GetIntAckCount();
	}
}

// ---------------------------------------------------------------------------
//	ステップインの完了判定を見送るべきなら true。受理の検知と、
//	戻ってきたかどうかの判定を兼ねる(VM スレッドから、CheckExecBreak の
//	中でのみ呼ばれる)
//
bool Debugger::CheckIntSkip(int cpuid, uint pc)
{
	PC88::Z80* z = (cpuid == cpu_main) ? vm->GetCPU1() : vm->GetCPU2();

	if (!intskip)
	{
		// 受理の検知
		uint c = z->GetIntAckCount();
		if (c == lastintack[cpuid])
			return false;
		lastintack[cpuid] = c;

		if (!z->IsIntAckPushed())
			return false;				// 戻り番地が無い (IM0)。従来どおり止める

		// HALT で止まっていたところへの割り込みはスキップしない。
		// スキップしても戻り先が HALT 自身で PC が動かず、
		// 「ステップしたのに何も起きない」ように見えるため
		IMemoryAccess* mem = (cpuid == cpu_main) ? vm->GetMemoryAccess1() : vm->GetMemoryAccess2();
		suppressbreak = true;
		uint op = mem->Read8(z->GetIntAckPC() & 0xffff) & 0xff;
		suppressbreak = false;
		if (op == 0x76)
			return false;

		intskip = true;
		intskipretaddr = z->GetIntAckPC() & 0xffff;
		intskipsp = z->GetIntAckSP() & 0xffff;
		intskipcount = 0;
		return true;
	}

	// 戻ってきたか？ (Step Over と同じ SP ガード)
	uint sp = z->GetReg().r.w.sp & 0xffff;
	if (pc == intskipretaddr && sp >= intskipsp)
	{
		intskip = false;
		return false;					// ここで止める = ステップ完了
	}

	// 保険: ハンドラが戻ってこない場合は諦めてその場で止める
	if (++intskipcount > intskiplimit)
	{
		intskip = false;
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
//	CALL nn / CALL cc,nn / RST n の判定。IsStepOverInstruction と
//	RecordTrace(depth+1 判定)の共通ロジック
//
bool Debugger::IsCallInstruction(uint pc, uint op, uint* retaddr) const
{
	// CALL nn(0xCD) / CALL cc,nn(0xC4,CC,D4,DC,E4,EC,F4,FC)
	if (op == 0xcd || (op & 0xc7) == 0xc4)
	{
		if (retaddr)
			*retaddr = (pc + 3) & 0xffff;
		return true;
	}
	// RST n(0xC7,CF,D7,DF,E7,EF,F7,FF)
	if ((op & 0xc7) == 0xc7)
	{
		if (retaddr)
			*retaddr = (pc + 1) & 0xffff;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
//	RET / RET cc / RETI / RETN の判定(RecordTrace の depth-1 判定用)
//
bool Debugger::IsReturnInstruction(uint op, uint op2) const
{
	// RET(0xC9) / RET cc(0xC0,C8,D0,D8,E0,E8,F0,F8)
	if (op == 0xc9 || (op & 0xc7) == 0xc0)
		return true;
	// RETI(0xED 0x4D) / RETN(0xED 0x45)
	if (op == 0xed && (op2 == 0x4d || op2 == 0x45))
		return true;
	return false;
}

// ---------------------------------------------------------------------------
//	現在の PC の命令が CALL/RST/ブロック転送命令(LDIR等)かどうかを見て、
//	ステップオーバーの対象にすべきか判定する。実行はしない
//
bool Debugger::IsStepOverInstruction(CpuTarget cpu, uint pc, uint* retaddr)
{
	IMemoryAccess* mem = (cpu == cpu_main) ? vm->GetMemoryAccess1() : vm->GetMemoryAccess2();
	pc &= 0xffff;
	uint op = mem->Read8(pc) & 0xff;

	if (IsCallInstruction(pc, op, retaddr))
		return true;

	// ED プレフィクスのブロック繰り返し系
	// (LDIR/CPIR/INIR/OTIR/LDDR/CPDR/INDR/OTDR = 0xB0-B3, 0xB8-BB)
	if (op == 0xed)
	{
		uint op2 = mem->Read8((pc + 1) & 0xffff) & 0xff;
		if ((op2 & 0xf4) == 0xb0)
		{
			*retaddr = (pc + 2) & 0xffff;
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
//	addr が CALL/RST 命令の戻り番地に見えるかどうかを判定する(Step Out用)
//
bool Debugger::LooksLikeReturnAddress(CpuTarget cpu, uint addr)
{
	IMemoryAccess* mem = (cpu == cpu_main) ? vm->GetMemoryAccess1() : vm->GetMemoryAccess2();
	addr &= 0xffff;

	// CALL nn(0xCD) / CALL cc,nn(0xC4,CC,D4,DC,E4,EC,F4,FC) : 3バイト命令
	uint op3 = mem->Read8((addr - 3) & 0xffff) & 0xff;
	if (op3 == 0xcd || (op3 & 0xc7) == 0xc4)
		return true;

	// RST n(0xC7,CF,D7,DF,E7,EF,F7,FF) : 1バイト命令
	uint op1 = mem->Read8((addr - 1) & 0xffff) & 0xff;
	if ((op1 & 0xc7) == 0xc7)
		return true;

	return false;
}

// ---------------------------------------------------------------------------
//	1命令実行の直前に呼ばれる。true を返すと実行を打ち切る
//	(VM スレッドから呼ばれる。BP/ステップ/トレースのいずれも無効なときは
//	 active を見るだけで即座に false を返す。1命令ごとに呼ばれるので
//	 この判定の位置を動かさないこと)
//
bool Debugger::CheckExecBreak(int cpuid, uint pc)
{
	if (!active)
		return false;
	lastpc[cpuid & 1] = uint16(pc);		// 通信のログ用 (M88gn)
	if (suppressbreak)
		return false;
	// 既にブレークしている: 何もせずに止める (メモリのブレークは命令の途中で起きるので、次の命令の前のここで止める。
	// トレースも二重に記録しない)
	if (broken)
		return true;

	pc &= 0xffff;

	if (tracemode)
		RecordTrace(cpuid, pc);

	if (stepmode != step_none && CpuTarget(cpuid) == steptarget)
	{
		if (stepfirstcheck)
		{
			// ステップ開始位置そのものにブレークポイントが張られていても
			// 最初の1命令は必ず実行させる
			stepfirstcheck = false;
			if (stepmode == step_in)
				return false;
			// step_over はここが戻り番地と一致することは無いはずだが、
			// 念のため下のロジックへそのまま続ける
		}

		if (stepmode == step_in)
		{
			// 割り込みで飛んだ先では止めず、戻ってきてから止める。
			// スキップ中も下の通常 BP 判定へは必ず落とすので、
			// ハンドラ内のブレークポイントは従来どおり効く
			if (intskipenabled && CheckIntSkip(cpuid, pc))
			{
				// このステップの完了判定は見送る (フォールスルー)
			}
			else
			{
				RequestBreak(brk_step, steptarget, pc);
				stepmode = step_none;
				return true;
			}
		}
		else // step_over
		{
			// SPガード: 呼び出しより浅いか同じ深さに戻ってきたときだけ止める。
			// これが無いと、再帰呼び出しの内側や、割り込みハンドラ/別ルーチンが
			// たまたま戻り番地を通過しただけで誤って止まってしまう
			PC88::Z80* z = (steptarget == cpu_main) ? vm->GetCPU1() : vm->GetCPU2();
			uint sp = z->GetReg().r.w.sp & 0xffff;
			if (pc == stepoveraddr && sp >= stepoversp)
			{
				RequestBreak(brk_step, steptarget, pc);
				stepmode = step_none;
				return true;
			}
			// 戻り番地でなければ、呼び出し先の中にあるかもしれない
			// 通常のブレークポイントを判定するため下へフォールスルーする
		}
	}

	if (resumeskip[cpuid])
	{
		resumeskip[cpuid] = false;
		if (pc == resumeskippc[cpuid])
			return false;		// 再開直後、居座っていた番地。1回だけ見逃す
	}

	if (!hasexecbreak[cpuid])
		return false;

	int bi;
	if ((execbreak[cpuid][pc >> 3] & (1 << (pc & 7))) && (bi = ExecCondHit(cpuid, pc)) >= 0)
	{
		RequestBreak(brk_exec, CpuTarget(cpuid), pc, bi);
		stepmode = step_none;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
//	IMemoryManager::AllocR/AllocW に登録するコールバック。VM スレッドから
//	(命令の途中で)呼ばれる。main/sub どちらの MemoryManager から来た
//	呼び出しかは、呼ばれた関数(Main/Sub)自体で分かる
//
uint MEMCALL Debugger::MemReadMain(void* inst, uint addr)
{
	return static_cast<Debugger*>(inst)->DoMemRead(cpu_main, addr);
}

uint MEMCALL Debugger::MemReadSub(void* inst, uint addr)
{
	return static_cast<Debugger*>(inst)->DoMemRead(cpu_sub, addr);
}

void MEMCALL Debugger::MemWriteMain(void* inst, uint addr, uint data)
{
	static_cast<Debugger*>(inst)->DoMemWrite(cpu_main, addr, data);
}

void MEMCALL Debugger::MemWriteSub(void* inst, uint addr, uint data)
{
	static_cast<Debugger*>(inst)->DoMemWrite(cpu_sub, addr, data);
}

// ---------------------------------------------------------------------------
//	実際のメモリ読み込みを代行しつつ、ブレーク範囲内なら実行を止める。
//	代行を忘れると、ブレークポイントを張っただけでエミュレーションが
//	壊れる(そのアドレスが読めなくなる)ので注意。
//	suppressbreak が立っているとき(ウォッチウインドウ等が VM の
//	MemoryManager 経由でメモリを覗くとき)は判定自体をスキップする
//
uint Debugger::DoMemRead(CpuTarget cpu, uint addr)
{
	addr &= 0xffff;
	uint data = mm[cpu]->Read8P(uint(memmid[cpu]), addr);

	if (suppressbreak || broken)
		return data;

	for (size_t i = 0; i < bplist.size(); i++)
	{
		const BreakPoint& bp = bplist[i];
		if (!bp.enabled || bp.cpu != uint8(cpu) || !bp.read || bp.io)
			continue;
		if (addr < bp.addr || addr >= bp.addr + bp.length)
			continue;
		if (!CondHit(bplist[i], cpu, lastpc[cpu], addr, int(data & 0xff)))
			continue;

		RequestBreak(brk_memread, cpu, addr, int(i), int(data & 0xff));
		stepmode = step_none;

		// メモリアクセスは命令の途中で起きる。この命令は最後まで実行され、次の命令の前の判定
		// (CheckExecBreak はブレーク中なら止める) で止まる。M88gn: Z80_x86 はメモリの関数を呼ぶときに
		// クロックの値をメモリへ書き戻さないので、ここで Stop(0) を呼ぶと実行の時刻が狂う
		break;
	}
	return data;
}

void Debugger::DoMemWrite(CpuTarget cpu, uint addr, uint data)
{
	addr &= 0xffff;
	// パターンのブレーク (M88gn): 書く前の値を覚える (書き込みがそのバンクを変えたかを見るため)
	bool pat = haspatbreak[cpu] && !suppressbreak && !broken;
	if (pat)
	{
		for (size_t i = 0; i < bplist.size() && i < patold.size(); i++)
		{
			const BreakPoint& bp = bplist[i];
			patold[i] = (bp.pattern && bp.enabled && bp.cpu == uint8(cpu)
						 && addr >= bp.addr && addr < bp.addr + bp.length)
						? PatRead(cpu, bp.patbank, addr) : -1;
		}
	}

	// ブレークするかどうかに関わらず、書き込みは先に実際へ反映する
	mm[cpu]->Write8P(uint(memmid[cpu]), addr, data);

	if (suppressbreak || broken)
		return;

	if (pat)
	{
		for (size_t i = 0; i < bplist.size() && i < patold.size(); i++)
		{
			if (patold[i] < 0)
				continue;
			const BreakPoint& bp = bplist[i];
			uint start;
			if (!PatHit(bp, cpu, addr, patold[i], PatRead(cpu, bp.patbank, addr), &start))
				continue;
			if (!CondHit(bplist[i], cpu, lastpc[cpu], addr, int(data & 0xff)))
				continue;
			RequestBreak(brk_pattern, cpu, start, int(i), int(data & 0xff));
			breaklen = bp.patlen;
			stepmode = step_none;
			return;		// DoMemRead と同じく、次の命令の前で止まる
		}
	}

	for (size_t i = 0; i < bplist.size(); i++)
	{
		const BreakPoint& bp = bplist[i];
		if (!bp.enabled || bp.cpu != uint8(cpu) || !bp.write || bp.io)
			continue;
		if (addr < bp.addr || addr >= bp.addr + bp.length)
			continue;
		if (!CondHit(bplist[i], cpu, lastpc[cpu], addr, int(data & 0xff)))
			continue;

		RequestBreak(brk_memwrite, cpu, addr, int(i), int(data & 0xff));
		stepmode = step_none;

		// DoMemRead と同じく、次の命令の前で止まる (Stop(0) は呼ばない)
		break;
	}
}

// ---------------------------------------------------------------------------
//	パターンのブレーク (M88gn)。バンクの 1 バイトを読む。読めない所 (バンクの範囲の外、
//	つながっていない拡張 RAM) は -1。サブの CPU はバンクが無く、CPU から見えるとおりに読む
//
int Debugger::PatRead(CpuTarget cpu, uint bank, uint addr)
{
	addr &= 0xffff;
	if (cpu == cpu_sub)
		return (mm[cpu] && memmid[cpu] >= 0) ? int(mm[cpu]->Read8P(uint(memmid[cpu]), addr) & 0xff) : -1;

	Memory* m = vm ? vm->GetMem1() : 0;
	if (!m)
		return -1;
	switch (bank)
	{
	case MemoryViewer::mainram:
		return m->GetRAM()[addr];
	case MemoryViewer::eram0: case MemoryViewer::eram1:
	case MemoryViewer::eram2: case MemoryViewer::eram3:
		{
			if (addr >= 0x8000)
				return -1;
			uint8* p = m->GetERAM(bank - MemoryViewer::eram0);
			return p == m->GetRAM() ? -1 : p[addr];		// 無いバンクなら GetERAM は本体の RAM を返す
		}
	case MemoryViewer::gvram0: case MemoryViewer::gvram1: case MemoryViewer::gvram2:
		return addr >= 0xc000 ? m->GetGVRAM()[addr & 0x3fff].byte[bank - MemoryViewer::gvram0] : -1;
	case MemoryViewer::tvram:
		return addr >= 0xf000 ? m->GetTVRAM()[addr & 0xfff] : -1;
	}
	return -1;
}

// addr への書き込みでバンクの値が oldv → newv になったとき、addr を含む位置にパターンが
// 揃ったか (前は揃っていなかったか) を調べる。揃ったら start に先頭の番地
bool Debugger::PatHit(const BreakPoint& bp, CpuTarget cpu, uint addr, int oldv, int newv, uint* start)
{
	if (newv < 0 || oldv == newv)
		return false;			// このバンクは変わっていない
	bool nocase = (bp.patflags & pat_nocase) != 0;
	struct Fold
	{
		static int F(int v, bool nc) { return (nc && v >= 'a' && v <= 'z') ? v - 0x20 : v; }
	};
	int fo = Fold::F(oldv, nocase), fn = Fold::F(newv, nocase);
	if (fo == fn)
		return false;

	uint len = bp.patlen;
	uint lo = bp.addr, hi = bp.addr + bp.length;		// hi は含まない
	for (uint k = 0; k < len; k++)
	{
		if (addr < lo + k)
			break;
		uint s = addr - k;
		if (s + len > hi)
			continue;
		int pk = Fold::F(bp.pat[k], nocase);
		if (fn != pk || fo == pk)
			continue;			// この書き込みで k バイト目が揃ったのではない
		bool ok = true;
		for (uint j = 0; j < len && ok; j++)
		{
			if (j == k)
				continue;
			int v = PatRead(cpu, bp.patbank, s + j);
			ok = v >= 0 && Fold::F(v, nocase) == Fold::F(bp.pat[j], nocase);
		}
		if (ok)
		{
			*start = s;
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
//	バックトレースの ON/OFF。ON にした瞬間は depth 推定用の状態を
//	リセットする(古い状態を引きずらない)。リングバッファの中身自体は
//	消さない(Clear Trace は別操作)
//
void Debugger::SetTraceEnabled(bool enable)
{
	tracemode = enable;
	if (enable)
	{
		tracedepth[cpu_main] = tracedepth[cpu_sub] = 0;
		tracesp[cpu_main] = tracesp[cpu_sub] = 0;
		tracefirst[cpu_main] = tracefirst[cpu_sub] = false;
	}
	UpdateActive();
}

void Debugger::ClearTrace()
{
	tracehead = 0;
	tracecount = 0;
}

// ---------------------------------------------------------------------------
//	トレースの記録件数を切り替える(100/500/1000)。バッファを作り直すため、
//	記録内容は必ず捨てる(途中でサイズを変えるとリングの整合が取れないため)。
//	UI スレッドから、WinCore::Lock() の中で呼ぶこと(RecordTrace は
//	VM スレッドで走る)
//
void Debugger::SetTraceCapacity(int n)
{
	if (n != 100 && n != 500 && n != tracecapacity_max)
		n = tracecapacity_max;

	delete[] tracebuf;
	tracebuf = 0;
	tracecapacity = 0;
	tracehead = 0;
	tracecount = 0;

	// 確保に失敗したら tracebuf は 0 のまま残す。RecordTrace はこれを見て
	// 即 return するので、記録が止まるだけで異常終了はしない
	TraceEntry* nb = new TraceEntry[n];
	if (!nb)
		return;

	tracebuf = nb;
	tracecapacity = n;
}

// ---------------------------------------------------------------------------
//	index: 0 が最新(直近実行)。GetTraceCount()-1 が最古
//
bool Debugger::GetTraceEntry(int index, TraceEntry* e) const
{
	if (index < 0 || index >= tracecount || !e || !tracebuf)
		return false;

	int idx = (tracehead - 1 - index + tracecapacity * 2) % tracecapacity;
	*e = tracebuf[idx];
	return true;
}

// ---------------------------------------------------------------------------
//	トレース記録本体。VM スレッドから、CheckExecBreak の中(1命令ごと)に
//	呼ばれる。オペコードを読むために VM の MemoryManager を経由するが、
//	これは §5.3 のウォッチと同じ理由で suppressbreak を立てて読む
//	(この読み出し自体がメモリ read ブレークポイントを誤爆させないため)
//
void Debugger::RecordTrace(int cpuid, uint pc)
{
	if (!tracebuf)
		return;

	IMemoryAccess* mem = (cpuid == cpu_main) ? vm->GetMemoryAccess1() : vm->GetMemoryAccess2();

	suppressbreak = true;
	uint op = mem->Read8(pc & 0xffff) & 0xff;
	uint op2 = (op == 0xed) ? (mem->Read8((pc + 1) & 0xffff) & 0xff) : 0;
	suppressbreak = false;

	PC88::Z80* z = (cpuid == cpu_main) ? vm->GetCPU1() : vm->GetCPU2();
	uint sp = z->GetReg().r.w.sp & 0xffff;

	int depth = tracedepth[cpuid];
	if (tracefirst[cpuid])
	{
		// SPガードによる自己修復: JP によるテールコールやスタック直接操作、
		// 割り込み等で depth がずれても、SP が前回より浅くなっていれば
		// 1段ずつ引き下げて追従する(数命令のうちに収束する目安の補正)
		if (sp > tracesp[cpuid])
			depth = Max(0, depth - 1);
	}
	else
	{
		tracefirst[cpuid] = true;
	}

	TraceEntry e;
	e.pc = uint16(pc & 0xffff);
	e.sp = uint16(sp);
	e.cpu = uint8(cpuid);
	e.depth = uint8(Limit(depth, 63, 0));

	// レジスタをまるごと記録する。F は遅延評価を確定させてから入れる
	// (仕様書 §5.1)。GetAFSync() は「いずれ計算するもの」を先に計算するだけで
	// 実行結果は変わらないが、1命令ごとの経路なのでここだけで呼ぶこと
	e.reg = z->GetReg();
	e.reg.r.w.af = z->GetAFSync();

	tracebuf[tracehead] = e;
	tracehead = (tracehead + 1) % tracecapacity;
	tracecount = Min(tracecount + 1, tracecapacity);

	if (IsCallInstruction(pc, op, 0))
		depth = Min(63, depth + 1);
	else if (IsReturnInstruction(op, op2))
		depth = Max(0, depth - 1);

	tracedepth[cpuid] = uint8(Limit(depth, 63, 0));
	tracesp[cpuid] = sp;
}

}
