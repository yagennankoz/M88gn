// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator.
//	Copyright (C) cisc 1997, 2001.
// ---------------------------------------------------------------------------
//	$Id: wincore.h,v 1.34 2003/05/15 13:15:36 cisc Exp $

#pragma once

// ---------------------------------------------------------------------------

#include "types.h"
#include "critsect.h"
#include "pc88/pc88.h"
#include "pc88/config.h"
#include "winsound.h"
#include "sequence.h"
#include "winjoy.h"

namespace PC8801
{
	class WinKeyIF;
	class ExternalDevice;
	class ExtendModule;
}


class WinUI;

// ---------------------------------------------------------------------------

class WinCore : public PC88, public ISystem, public ILockCore
{
public:
	WinCore();
	~WinCore();
	bool Init(WinUI* ui, HWND hwnd, Draw* draw, DiskManager* diskmgr, 
			  PC8801::WinKeyIF* keyb, IConfigPropBase* cpb, TapeManager* tapemgr); 
	bool Cleanup();

	void Reset();
	void ApplyConfig(PC8801::Config* config);
	
	
	bool SaveShapshot(const char* filename);
	bool LoadShapshot(const char* filename, const char* diskname = 0);

	PC8801::WinSound* GetSound() { return &sound; }

	long GetExecCount() { return seq.GetExecCount(); }
	void Wait(bool dowait) { seq.Activate(!dowait); }
	void ExecOne() { seq.ExecOne(); }
	void* IFCALL QueryIF(REFIID iid);
	void IFCALL Lock() { seq.Lock(); }
	void IFCALL Unlock() { seq.Unlock(); }

	// デバッガの実行の制御 (M88am から)
	void Pause(bool pause);
	bool IsPaused() const { return paused; }
	// UI から見た「止まっている」状態。paused は WM_M88_DEBUGBREAK を UI のスレッドが処理して初めて立つので、
	// VM のスレッドですぐに立つデバッガの broken と OR を取る
	bool IsStopped() { return paused || GetDebugger()->IsBroken(); }
	// ステップイン/ステップオーバーの実行中か (戻ってこないステップオーバーの間だけ true)
	bool IsStepping() { return GetDebugger()->IsStepping(); }
	void CancelStep() { GetDebugger()->CancelStep(); }
	// 止めている間だけ。1 命令だけ実行してまた止める
	void StepIn(PC8801::Debugger::CpuTarget cpu);
	// CALL/RST/ブロック転送の命令なら戻り番地まで、それ以外は StepIn と同じ
	void StepOver(PC8801::Debugger::CpuTarget cpu);
	// 呼び出し元に戻ったところで止める。[SP] が戻り番地に見えなければ何もせず false
	bool StepOut(PC8801::Debugger::CpuTarget cpu);

private:
	void ResumeForStep();
public:

private:
//	Snapshot ヘッダー
	enum
	{
		ssmajor = 1, ssminor = 1,
	};

	struct SnapshotHeader
	{
		char id[16];
		uint8 major, minor;

		int8 disk[2];
		int datasize;
		PC8801::Config::BASICMode basicmode;
		int16 clock;
		uint16 erambanks;
		uint16 cpumode;
		uint16 mainsubratio;
		uint flags;
		uint flag2;
	};

	class LockObj
	{
		WinCore* b;
	public:
		LockObj(WinCore* _b) : b(_b) { b->Lock(); }
		~LockObj() { b->Unlock(); }
	};

private:
	bool ConnectDevices(PC8801::WinKeyIF* keyb);
	bool ConnectExternalDevices();

	WinUI* ui;
	IConfigPropBase* cfgprop;

	Sequencer seq;
	WinPadIF padif;
	bool paused;

	typedef vector<PC8801::ExtendModule*> ExtendModules;
	ExtendModules extmodules;

	PC8801::WinSound sound;
	PC8801::Config config;

	typedef vector<PC8801::ExternalDevice*> ExternalDevices;
	ExternalDevices extdevices;

};

