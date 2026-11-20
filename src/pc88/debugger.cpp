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
#include "misc.h"

namespace PC8801
{

// ---------------------------------------------------------------------------
//	構築/消滅
//
Debugger::Debugger()
: vm(0), hasmembreak(false), broken(false), reason(brk_none), breakcpu(cpu_main), breakaddr(0), suppressbreak(false),
  stepmode(step_none), steptarget(cpu_main), stepfirstcheck(false), stepoveraddr(0), stepoversp(0),
  intskipenabled(true), intskip(false), intskipretaddr(0), intskipsp(0), intskipcount(0), active(false),
  tracemode(false), tracecapacity(0), tracebuf(0), tracehead(0), tracecount(0)
{
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

Debugger::~Debugger()
{
	delete[] tracebuf;
}

// ---------------------------------------------------------------------------
//	初期化
//
bool Debugger::Init(PC88* _vm)
{
	vm = _vm;

	// メモリ読み書きブレークポイント用に、MemoryMonitor とは別の
	// Connect ID を取得しておく(MemoryMonitor は開いたときだけ接続するが、
	// こちらは起動直後から常時接続しておく)
	mm[cpu_main] = vm->GetMemoryManager1();
	mm[cpu_sub]  = vm->GetMemoryManager2();
	memmid[cpu_main] = mm[cpu_main] ? mm[cpu_main]->Connect(this, true) : -1;
	memmid[cpu_sub]  = mm[cpu_sub]  ? mm[cpu_sub]->Connect(this, true)  : -1;

	SetTraceCapacity(tracecapacity_max);

	return true;
}

void Debugger::Cleanup()
{
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
			if (bp.cpu == uint8(cpu) && bp.exec && addr >= bp.addr && addr < bp.addr + bp.length)
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
		if (bp.cpu == uint8(cpu) && bp.exec && addr >= bp.addr && addr < bp.addr + bp.length)
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

	// 同一範囲(cpu/addr/length が一致)のエントリがあれば、
	// 新規に増やさず属性を統合する
	for (size_t i = 0; i < bplist.size(); i++)
	{
		BreakPoint& e = bplist[i];
		if (e.cpu == nb.cpu && e.addr == nb.addr && e.length == nb.length)
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
		if (!bp.enabled || !bp.exec)
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
	for (size_t i = 0; i < bplist.size(); i++)
	{
		const BreakPoint& bp = bplist[i];
		if (!bp.enabled || (!bp.read && !bp.write))
			continue;
		uint cpu = bp.cpu & 1;
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

	UpdateActive();
}

// ---------------------------------------------------------------------------
//	CheckExecBreak 冒頭の高速パス用フラグを再計算する
//
void Debugger::UpdateActive()
{
	active = hasexecbreak[cpu_main] || hasexecbreak[cpu_sub] || hasmembreak
		  || (stepmode != step_none) || broken || tracemode;
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
void Debugger::RequestBreak(BreakReason r, CpuTarget cpu, uint addr)
{
	broken = true;
	reason = r;
	breakcpu = cpu;
	breakaddr = addr & 0xffff;
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

	if (execbreak[cpuid][pc >> 3] & (1 << (pc & 7)))
	{
		RequestBreak(brk_exec, CpuTarget(cpuid), pc);
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
		if (!bp.enabled || bp.cpu != uint8(cpu) || !bp.read)
			continue;
		if (addr < bp.addr || addr >= bp.addr + bp.length)
			continue;

		RequestBreak(brk_memread, cpu, addr);
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
	// ブレークするかどうかに関わらず、書き込みは先に実際へ反映する
	mm[cpu]->Write8P(uint(memmid[cpu]), addr, data);

	if (suppressbreak || broken)
		return;

	for (size_t i = 0; i < bplist.size(); i++)
	{
		const BreakPoint& bp = bplist[i];
		if (!bp.enabled || bp.cpu != uint8(cpu) || !bp.write)
			continue;
		if (addr < bp.addr || addr >= bp.addr + bp.length)
			continue;

		RequestBreak(brk_memwrite, cpu, addr);
		stepmode = step_none;

		// DoMemRead と同じく、次の命令の前で止まる (Stop(0) は呼ばない)
		break;
	}
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
