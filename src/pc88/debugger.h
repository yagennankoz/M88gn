// ---------------------------------------------------------------------------
//	M88 - PC-8801 emulator
//	デバッガコア (ブレークポイント/実行制御の管理)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#pragma once

#include <vector>
#include "types.h"
#include "z80debughook.h"
#include "if/ifcommon.h"
#include "Z80.h"
#include "device.h"

class PC88;

namespace PC8801
{

// ---------------------------------------------------------------------------
//	デバッガの状態を保持するクラス。IZ80DebugHook を実装し、
//	Z80C::SetDebugHook で main/sub 両 CPU に登録して使う。
//	ブレークポイントのテーブルは UI スレッド(設定/解除)と
//	VM スレッド(CheckExecBreak での参照)の両方から触られる。
//	設定・解除は WinCore::Lock()/Unlock() の中で呼び出すこと。
//
class Debugger : public IZ80DebugHook
{
public:
	enum BreakReason
	{
		brk_none = 0,
		brk_exec,			// 実行ブレークポイント
		brk_memread,		// メモリ読み込み
		brk_memwrite,		// メモリ書き込み
		brk_step,			// ステップ完了
		brk_user,			// 手動ポーズ
		brk_ioread,			// I/O ポートの IN (M88gn)
		brk_iowrite,		// I/O ポートの OUT (M88gn)
	};

	enum CpuTarget { cpu_main = 0, cpu_sub = 1 };
	enum StepMode { step_none = 0, step_in = 1, step_over = 2 };

	// ブレークポイント1件。exec/read/write は排他ではない
	// (1エントリで「実行でも書き込みでも止める」を指定できる)
	struct BreakPoint
	{
		uint  addr;			// 0x0000-0xffff
		uint  length;		// 1 以上。addr+length は 0x10000 でクランプ
		uint8 cpu;			// cpu_main / cpu_sub
		bool  exec;			// 実行
		bool  read;			// 読み込み
		bool  write;		// 書き込み
		bool  enabled;		// 一時的に無効化できる
		char  comment[64];	// 空文字が初期値。改行・前後の空白は保持しない
		bool  io;			// I/O ポート (M88gn)。addr はポート (00-ff)、read = IN、write = OUT、exec は使わない
		char  cond[64];		// 条件の式 (M88gn)。空なら条件無し。書き方は Debugger::CondHelp
		uint  hitcount;		// この回数目から止める (M88gn。0・1 なら毎回)
		uint  hits;			// 条件が成り立った回数 (M88gn。変更・追加で 0 に戻る)
	};

	// サブシステムとの通信のログ 1 件 (M88gn)。メイン・サブの CPU が 8255 のポート (FC-FF) に OUT した値
	struct SubLogEntry
	{
		uint32 tick;		// メインの CPU のクロック数 (PC88::GetCPUTick)
		uint16 pc;			// OUT を実行した命令の番地
		uint8  cpu;			// cpu_main (メイン→サブ) / cpu_sub (サブ→メイン)
		uint8  port;		// fc-ff
		uint8  data;
	};

	// ウォッチエントリ1件。※ MemoryMonitor::SetWatch (R/Wアクセスの色付け表示。
	// 全く別物) とは無関係
	struct WatchEntry
	{
		uint  addr;
		uint8 size;			// 1 = バイト, 2 = ワード(リトルエンディアン)
		uint8 cpu;			// cpu_main / cpu_sub
		uint8 bank;			// 見るメモリ (MemoryViewer::Type の値。メインの CPU のとき。M88gn)
		char  label[32];
	};

	// バックトレース1件。depth は CALL/RET から推定した呼び出し深さの目安
	// (正確ではない。SP による自己修復あり)
	//
	// reg は「その命令を実行する直前」の状態。F は Z80C の遅延評価を
	// 確定させた値が入る(GetAFSync 経由。仕様書 §5.1)
	struct TraceEntry
	{
		uint16 pc;
		uint16 sp;
		uint8  cpu;
		uint8  depth;
		Z80Reg reg;
	};

	Debugger();
	~Debugger();

	bool Init(PC88* vm);
	void Cleanup();

	// --- 実行ブレークポイント ---
	// (内部では bplist への Add/Delete に委譲する。addr 1バイトぶんの
	//  exec 専用エントリとして扱うので、Code ウインドウのクリックトグルは
	//  そのまま動く)
	void SetExecBreak(CpuTarget cpu, uint addr, bool set);
	bool IsExecBreak(CpuTarget cpu, uint addr) const;
	void ClearExecBreaks(CpuTarget cpu);

	// --- ブレークポイント管理(すべて WinCore::Lock() の中で呼ぶこと) ---
	int  GetBreakPointCount() const { return int(bplist.size()); }
	bool GetBreakPoint(int index, BreakPoint* bp) const;
	int  AddBreakPoint(const BreakPoint& bp);			// index を返す。重複(同一範囲)は統合
	bool SetBreakPoint(int index, const BreakPoint& bp);
	bool DeleteBreakPoint(int index);
	void DeleteAllBreakPoints();
	void EnableAllBreakPoints(bool enable);
	int  FindExecBreakPoint(CpuTarget cpu, uint addr) const;	// Code窓のトグル用

	// --- ウォッチ(すべて WinCore::Lock() の中で呼ぶこと) ---
	int  GetWatchCount() const { return int(watchlist.size()); }
	bool GetWatch(int index, WatchEntry* w) const;
	int  AddWatch(const WatchEntry& w);				// index を返す
	bool SetWatch(int index, const WatchEntry& w);
	bool DeleteWatch(int index);
	void DeleteAllWatches();

	// --- バックトレース(すべて WinCore::Lock() の中で呼ぶこと) ---
	void SetTraceEnabled(bool enable);
	bool IsTraceEnabled() const { return tracemode; }
	void ClearTrace();
	int  GetTraceCount() const { return tracecount; }
	// index: 0 が最新(直近実行)。GetTraceCount()-1 が最古
	bool GetTraceEntry(int index, TraceEntry* e) const;

	// トレースの記録件数。メニューで切り替える(100/500/1000。既定1000)。
	// バッファを作り直すため、記録内容は捨てる(ClearTrace と同じ状態になる)
	enum { tracecapacity_min = 100, tracecapacity_max = 1000 };
	void SetTraceCapacity(int n);
	int  GetTraceCapacity() const { return tracecapacity; }

	// --- サブシステムとの通信のログ (M88gn。すべて WinCore::Lock() の中で呼ぶこと) ---
	enum { subloglimit = 4096 };
	void SetSubLogEnabled(bool e);
	bool IsSubLogEnabled() const { return sublog; }
	void SetSubLogHandshake(bool h);		// FE・FF (ハンドシェイク) も記録するか
	bool IsSubLogHandshake() const { return subloghs; }
	void ClearSubLog() { subloghead = sublogcount = 0; }
	int  GetSubLogCount() const { return sublogcount; }
	bool GetSubLogEntry(int index, SubLogEntry* e) const;	// index: 0 が最も古い

	// --- 条件付きブレークポイント (M88gn) ---
	// 条件の式の書き方を確かめる。誤りなら false で、err に知らせを書く (128 バイト)
	static bool CheckCondition(const char* cond, char* err);
	static const char* CondHelp();			// 書き方の説明 (メッセージの窓用)

	// --- ブレーク状態 ---
	void RequestBreak(BreakReason reason, CpuTarget cpu, uint addr);
	bool IsBroken() const { return broken; }
	BreakReason GetBreakReason() const { return reason; }
	CpuTarget GetBreakCpu() const { return breakcpu; }
	uint GetBreakAddr() const { return breakaddr; }
	void ClearBreak();

	// ブレークポイント判定を一時的に止める(Sequencer::ExecOne 専用)。
	// でないと「BPのある番地で止まっている状態から1命令進める」ことが
	// 永久にできなくなる(進めようとした瞬間に同じ番地でまた止まるため)。
	// IsStepping()(ステップイン/ステップオーバーの実行中かどうか)とは
	// 別物なので注意
	void SetSuppressBreak(bool s) { suppressbreak = s; }

	// --- ステップイン/ステップオーバー ---
	// PC が変わったら止まる、ではなく「1命令実行したら必ず止まる」
	// カウンタ方式(HALT や自己ループで PC が変わらない命令でも
	// 確実に止まる)。開始位置自体にブレークポイントが張られていても
	// 最初の1命令は必ず実行させる
	void RequestStepIn(CpuTarget cpu);
	// retaddr: ステップオーバー対象命令(CALL/RST/ブロック転送)の直後の番地。
	// そこに到達するまで通常のブレークポイントも有効なまま実行を続ける。
	// framesp: 対象命令を実行する直前のSP。再帰呼び出しの内側や、
	// 割り込みハンドラ等がたまたま retaddr を通過しただけで誤って
	// 止まらないよう、SP がここまで戻ってきたかどうかも合わせて見る
	// (PC == retaddr && SP >= framesp)
	void RequestStepOver(CpuTarget cpu, uint retaddr, uint framesp);
	void CancelStep();
	bool IsStepping() const { return stepmode != step_none; }

	// --- 割り込みスキップ (ステップイン中だけ働く) ---
	void SetIntSkipEnabled(bool e);
	bool IsIntSkipEnabled() const { return intskipenabled; }

	// 現在の PC の命令が CALL/RST/ブロック転送(LDIR等)なら true を返し、
	// retaddr にその命令の直後のアドレスを書く。ステップオーバーの
	// 対象にすべきか判定するためのもの(実行はしない)
	bool IsStepOverInstruction(CpuTarget cpu, uint pc, uint* retaddr);

	// Z80 にはフレームポインタが無いので、Step Out は「スタックの先頭が
	// 戻り番地である」というヒューリスティックに頼るしかない。addr の
	// 直前(addr-3 or addr-1)が CALL/RST の命令列に見えるかどうかで
	// 妥当性を確認する。100%の判定ではない(将来のバックトレース機能でも
	// 同じ判定を使う想定なので関数として切り出してある)
	bool LooksLikeReturnAddress(CpuTarget cpu, uint addr);

	// --- VM スレッドから呼ばれる ---
	// 1命令実行の直前にフックされる。true を返すと実行を止める
	bool CheckExecBreak(int cpuid, uint pc);

	// 再開直後の空振り防止。ブレークした番地に居座っている CPU が、
	// 再開した瞬間に同じ実行BPをもう一度踏んで、出したばかりのステップ要求を
	// 取り消してしまうのを防ぐ。WinCore::ResumeForStep から呼ぶこと
	// (ステップ対象CPU用の stepfirstcheck の、対象外CPUに対する対応物)
	void ArmResumeSkip();

private:
	// IMemoryManager::AllocR/AllocW に渡すコールバック。cpu ごとに関数を
	// 分けているのは、inst には Debugger 自身しか渡せず(1つの関数を両方の
	// CPU で共用すると、どちらの MemoryManager からの呼び出しか
	// 区別できないため)
	static uint MEMCALL MemReadMain(void* inst, uint addr);
	static uint MEMCALL MemReadSub(void* inst, uint addr);
	static void MEMCALL MemWriteMain(void* inst, uint addr, uint data);
	static void MEMCALL MemWriteSub(void* inst, uint addr, uint data);
	uint DoMemRead(CpuTarget cpu, uint addr);
	void DoMemWrite(CpuTarget cpu, uint addr, uint data);

	// I/O ポートのブレーク・通信のログ (M88gn)。IOBus のポートに、本物の装置と並べてつなぐ
	class DebugIO : public Device
	{
	public:
		DebugIO() : Device(0), dbg(0), cpu(cpu_main) {}
		uint IOCALL In(uint port);
		void IOCALL Out(uint port, uint data);
		Debugger* dbg;
		CpuTarget cpu;
	};
	// 条件と回数を見て、止めるなら true (M88gn)。data は読み書きした値 (無ければ -1)
	bool CondHit(BreakPoint& bp, CpuTarget cpu, uint pc, uint addr, int data);
	bool ExecCondHit(int cpuid, uint pc);
	friend struct CondEval;
	void DoIOIn(CpuTarget cpu, uint port);
	void DoIOOut(CpuTarget cpu, uint port, uint data);
	void RebuildIOHooks();

	// bplist からビットマップ/メモリフックを作り直す。bplist を変更した
	// 呼び出し元は必ず最後にこれを呼ぶこと(呼び出し元の Lock() 区間の中)
	void RebuildBreakTables();
	// active(CheckExecBreak 高速パス用の単一フラグ)を再計算する。
	// hasexecbreak/hasmembreak/stepmode/broken/tracemode のいずれかを
	// 変えたら呼ぶこと
	void UpdateActive();

	// CALL nn / CALL cc,nn / RST n の判定。IsStepOverInstruction(ステップ
	// オーバー用。ブロック転送命令も対象に含む)と RecordTrace(バックトレースの
	// depth+1 判定。CALL/RST だけが対象) の共通ロジックとして切り出してある。
	// retaddr が非0なら、その命令の直後の番地を書く
	bool IsCallInstruction(uint pc, uint op, uint* retaddr) const;
	// RET / RET cc / RETI / RETN の判定(depth-1 判定用)。op が 0xed のときは
	// op2 に2バイト目を渡すこと(それ以外は無視されるので0でよい)
	bool IsReturnInstruction(uint op, uint op2) const;

	// トレース記録本体(VM スレッドから、CheckExecBreak の中で呼ばれる)
	void RecordTrace(int cpuid, uint pc);

	// 受理カウンタを現在値に合わせ、スキップ状態を落とす
	// (ステップ開始時/中止時に呼ぶ)
	void SyncIntAck();
	// ステップインの完了判定を見送るべきなら true。
	// 受理の検知と、戻ってきたかどうかの判定を兼ねる
	bool CheckIntSkip(int cpuid, uint pc);

private:
	PC88* vm;

	// ブレークポイント本体。UI スレッドが core->Lock() の中でのみ書き換え、
	// VM スレッドは RebuildBreakTables() が作る派生データ(execbreak /
	// メモリフック)だけを参照する
	std::vector<BreakPoint> bplist;

	// ウォッチのリスト。bplist と同じ規約(UI スレッドが core->Lock() の中で
	// のみ書き換え、実アクセスは WatchMonitor が Lock() の中で行う)。
	// 判定に使われる派生データが無いので RebuildBreakTables() の対象外
	std::vector<WatchEntry> watchlist;

	// 実行ブレークポイントは 64KB のビットマップで持つ(bplist から
	// RebuildBreakTables() が再構築する派生データ。命令ごとに引くので、
	// 探索コストを O(1) にするためビットマップのまま残す)
	uint8 execbreak[2][0x10000 / 8];
	int   execbreakcount[2];
	bool  hasexecbreak[2];		// 1つでも設定されているか (高速パス用)

	// メモリ読み書きブレークポイント。実アクセスは AllocR/AllocW の
	// 登録先を経由して代行する必要があるため、専用の Connect ID を持つ。
	// 範囲そのものは bplist が持ち、ここでは Connect ID のみ管理する
	IMemoryManager* mm[2];
	int             memmid[2];
	bool            hasmembreak;	// bplist に有効な read/write エントリがあるか

	volatile bool broken;
	BreakReason   reason;
	CpuTarget     breakcpu;
	uint          breakaddr;

	volatile bool suppressbreak;

	volatile int stepmode;
	CpuTarget    steptarget;
	volatile bool stepfirstcheck;	// ステップ開始後、最初の1命令かどうか
	uint         stepoveraddr;
	uint         stepoversp;		// この深さ以上に戻るまでは止めない(SPガード)

	uint         resumeskippc[2];	// 再開時に居座っていた番地
	bool         resumeskip[2];	// 上を1回だけ見逃す

	// --- 割り込みスキップ (ステップイン中だけ働く) ---
	bool          intskipenabled;	// 設定 (既定 ON)
	volatile bool intskip;			// 割り込みから戻るのを待っている最中
	uint          intskipretaddr;	// 戻り番地 (割り込みで積まれた PC)
	uint          intskipsp;		// push する直前の SP。sp >= これ で戻ったと判定
	uint          intskipcount;		// 暴走の保険用。スキップ中に実行した命令数
	uint          lastintack[2];	// cpu ごとに最後に見た受理カウンタ

	enum { intskiplimit = 200000 };	// これを超えたら諦めてその場で止める

	// CheckExecBreak 冒頭の高速パス用フラグ。BP/ステップ/トレースの
	// いずれも無効なら即 false を返せるようにする
	bool active;

	// --- I/O (M88gn) ---
	DebugIO  iodev[2];
	IOBus*   iobus[2];
	uint16   lastpc[2];				// 最後に CheckExecBreak で見た PC (ログ用。active のときだけ正しい)
	bool     sublog;
	bool     subloghs;
	SubLogEntry* sublogbuf;
	int      subloghead;
	int      sublogcount;

	// --- バックトレース ---
	// 件数可変のリングバッファ(既定1000。SetTraceCapacity で作り直す)。
	// 確保に失敗したら tracebuf が 0 のままになり、RecordTrace は即 return する
	bool     tracemode;
	int         tracecapacity;		// 既定 1000
	TraceEntry* tracebuf;			// new[] で確保
	int      tracehead;			// 次に書き込む位置
	int      tracecount;			// 有効なエントリ数(<= tracecapacity)
	uint8    tracedepth[2];		// cpu ごとの現在の depth (0-63)
	uint     tracesp[2];			// cpu ごとの直前エントリの SP (自己修復用)
	bool     tracefirst[2];		// cpu ごとに「最初の1エントリ」かどうか
									// (自己修復はトレース開始直後は行わない)
};

}
