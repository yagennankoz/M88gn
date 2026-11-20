// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator.
//	Copyright (C) cisc 1998, 2001.
// ---------------------------------------------------------------------------
//	$Id: wincore.cpp,v 1.42 2003/05/15 13:15:35 cisc Exp $

#include "headers.h"
#include "wincore.h"
#include "WinKeyIF.h"
#include "misc.h"
#include "pc88/config.h"
#include "status.h"
#include "device.h"
#include "version.h"
#include "file.h"
#include "pc88/diskmgr.h"
#include "pc88/tapemgr.h"
#include "pc88/opnif.h"
#include "pc88/beep.h"
#include "extdev.h"
#include "if/ifpc88.h"
#include "if/ifguid.h"
#include "pc88/pd8257.h"
#include "pc88/joypad.h"
#include "module.h"
#include "ui.h"
#include "zlib/zlib.h"

#define LOGNAME "wincore"
#include "diag.h"

using namespace PC8801;

//					 0123456789abcdef
#define SNAPSHOT_ID	"M88 SnapshotData"

// ---------------------------------------------------------------------------
//	構築/消滅
//
WinCore::WinCore()
: paused(false)
{
}

WinCore::~WinCore()
{
	PC88::DeInit();
	Cleanup();
}

// ---------------------------------------------------------------------------
//	初期化
//
bool WinCore::Init
(WinUI* _ui, HWND hwnd, Draw* draw, DiskManager* disk, WinKeyIF* keyb, 
 IConfigPropBase* cp, TapeManager* tape)
{
	ui = _ui;
	cfgprop = cp;

	if (!PC88::Init(draw, disk, tape))
		return false;

	if (!sound.Init(this, hwnd, 0, 0))
		return false;

	padif.Init();

	if (!ConnectDevices(keyb))
		return false;

	if (!ConnectExternalDevices())
		return false;

	seq.SetClock(40);	// 4MHz
	seq.SetSpeed(100);	// 100%

	if (!seq.Init(this))
		return false;
	seq.SetDebugBreakNotify(hwnd);
	
	return true;
}

// ---------------------------------------------------------------------------
//	後始末
//
bool WinCore::Cleanup()
{
	seq.Cleanup();
	
	for (ExtendModules::iterator i = extmodules.begin(); i != extmodules.end(); ++i)
		delete *i;
	extmodules.clear();

	for (ExternalDevices::iterator j = extdevices.begin(); j != extdevices.end(); ++j)
		delete *j;
	extdevices.clear();

	return true;
}

// ---------------------------------------------------------------------------
//	リセット
//
void WinCore::Reset()
{
	LockObj lock(this);
	// リセットで PC/SP が別物になるので、一時的なステップの条件とブレークの状態は捨てる
	// (ブレークポイントは残す)
	GetDebugger()->CancelStep();
	GetDebugger()->ClearBreak();
	PC88::Reset();
}

// ---------------------------------------------------------------------------
//	デバッガ: 一時停止/再開 (M88am から)
//
void WinCore::Pause(bool pause)
{
	// ブレークした直後は paused == false のまま止まっているので、その状態からの再開は取りこぼさない
	if (paused == pause && !(!pause && GetDebugger()->IsBroken()))
		return;
	paused = pause;

	// 止めるとき・再開するときとも、進行中のステップの条件は捨てる
	GetDebugger()->CancelStep();
	if (!pause && GetDebugger()->IsBroken())
	{
		// ブレークポイントの番地に乗ったまま再開すると、すぐに同じブレークポイントで止まるので、
		// 先に 1 命令だけ進める (ExecOne がブレークの解除もする)
		seq.ExecOne();
	}

	seq.Activate(!pause);
	sound.Mute(pause);
}

// ---------------------------------------------------------------------------
//	ステップのために少しだけ実行を再開する (条件に当たると Sequencer::CheckDebugBreak がまた止める)
//
void WinCore::ResumeForStep()
{
	// ブレーク中は PC88::Execute が CPU を進めないので、前のブレークを先に解く
	GetDebugger()->ClearBreak();
	// 対象でない CPU がブレークポイントの番地に居座っていると、再開した瞬間に踏み直してステップごと
	// 取り消されるので、1 回だけ見逃す
	GetDebugger()->ArmResumeSkip();
	paused = false;
	seq.Activate(true);
	sound.Mute(false);
}

void WinCore::StepIn(PC8801::Debugger::CpuTarget cpu)
{
	if (!IsStopped())
		return;
	GetDebugger()->RequestStepIn(cpu);
	ResumeForStep();
}

void WinCore::StepOver(PC8801::Debugger::CpuTarget cpu)
{
	if (!IsStopped())
		return;
	PC88::Z80* z = (cpu == PC8801::Debugger::cpu_main) ? GetCPU1() : GetCPU2();
	uint pc = z->GetPC();
	uint retaddr;
	if (GetDebugger()->IsStepOverInstruction(cpu, pc, &retaddr))
		GetDebugger()->RequestStepOver(cpu, retaddr, z->GetReg().r.w.sp & 0xffff);
	else
		GetDebugger()->RequestStepIn(cpu);
	ResumeForStep();
}

bool WinCore::StepOut(PC8801::Debugger::CpuTarget cpu)
{
	if (!IsStopped())
		return false;
	PC88::Z80* z = (cpu == PC8801::Debugger::cpu_main) ? GetCPU1() : GetCPU2();
	uint sp = z->GetReg().r.w.sp & 0xffff;
	IMemoryAccess* mem = (cpu == PC8801::Debugger::cpu_main) ? GetMemoryAccess1() : GetMemoryAccess2();
	uint retaddr = (mem->Read8(sp) | (mem->Read8((sp + 1) & 0xffff) << 8)) & 0xffff;
	if (!GetDebugger()->LooksLikeReturnAddress(cpu, retaddr))
		return false;
	GetDebugger()->RequestStepOver(cpu, retaddr, (sp + 2) & 0xffff);
	ResumeForStep();
	return true;
}

// ---------------------------------------------------------------------------
//	設定を反映する
//
void WinCore::ApplyConfig(PC8801::Config* cfg)
{
	config = *cfg;

	int c = cfg->clock;
	if (cfg->flags & PC8801::Config::fullspeed)
		c = 0;
	if (cfg->flags & PC8801::Config::cpuburst)
		c = -c;
	seq.SetClock(c);
	seq.SetSpeed(cfg->speed / 10);
	seq.SetRefreshTiming(cfg->refreshtiming);

	if (joypad)
		joypad->Connect(&padif);

	PC88::ApplyConfig(cfg);
	sound.ApplyConfig(cfg);
	draw->SetFlipMode(false);
}


// ---------------------------------------------------------------------------
//	Windows 用のデバイスを接続
//
bool WinCore::ConnectDevices(WinKeyIF* keyb)
{
	static const IOBus::Connector c_keyb[] = 
	{
		{ PC88::pres, IOBus::portout, WinKeyIF::reset },
		{ PC88::vrtc, IOBus::portout, WinKeyIF::vsync },
		{ 0x00, IOBus::portin, WinKeyIF::in },
		{ 0x01, IOBus::portin, WinKeyIF::in },
		{ 0x02, IOBus::portin, WinKeyIF::in },
		{ 0x03, IOBus::portin, WinKeyIF::in },
		{ 0x04, IOBus::portin, WinKeyIF::in },
		{ 0x05, IOBus::portin, WinKeyIF::in },
		{ 0x06, IOBus::portin, WinKeyIF::in },
		{ 0x07, IOBus::portin, WinKeyIF::in },
		{ 0x08, IOBus::portin, WinKeyIF::in },
		{ 0x09, IOBus::portin, WinKeyIF::in },
		{ 0x0a, IOBus::portin, WinKeyIF::in },
		{ 0x0b, IOBus::portin, WinKeyIF::in },
		{ 0x0c, IOBus::portin, WinKeyIF::in },
		{ 0x0d, IOBus::portin, WinKeyIF::in },
		{ 0x0e, IOBus::portin, WinKeyIF::in },
		{ 0x0f, IOBus::portin, WinKeyIF::in },
		{ 0, 0, 0 }
	};
	if (!bus1.Connect(keyb, c_keyb)) return false;

	if (FAILED(GetOPN1()->Connect(&sound))) return false;
	if (FAILED(GetOPN2()->Connect(&sound))) return false;
	if (FAILED(GetBEEP()->Connect(&sound))) return false;
	if (FAILED(GetTapeManager()->Connect(&sound))) return false;
	sound.SetNoMaster(GetTapeManager());		// テープの音は全体の音量 (Master) の外
	if (FAILED(GetFDDSound()->Connect(&sound))) return false;
	sound.SetNoMaster(GetFDDSound());			// ディスクの音は全体の音量 (Master) の外

	return true;
}

// ---------------------------------------------------------------------------
//	スナップショット保存
//
bool WinCore::SaveShapshot(const char* filename)
{
	LockObj lock(this);

	bool docomp = !!(config.flag2 & Config::compresssnapshot);

	uint size = devlist.GetStatusSize();
	uint8* buf = new uint8[docomp ? size * 129 / 64 + 20 : size];
	if (!buf)
		return false;
	memset(buf, 0, size);

	if (devlist.SaveStatus(buf))
	{
		ulong esize = size * 129 / 64 + 20-4;
		if (docomp)
		{
			if (Z_OK != compress(buf+size+4, &esize, buf, size))
			{
				delete[] buf;
				return false;
			}
			*(int32*) (buf+size) = -(long)esize;
			esize += 4;
		}

		SnapshotHeader ssh;
		memcpy(ssh.id, SNAPSHOT_ID, 16);

		ssh.major = ssmajor;
		ssh.minor = ssminor;
		ssh.datasize = size;
		ssh.basicmode = config.basicmode;
		ssh.clock = int16(config.clock);
		ssh.erambanks = uint16(config.erambanks);
		ssh.cpumode = int16(config.cpumode);
		ssh.mainsubratio = int16(config.mainsubratio);
		ssh.flags = config.flags | (esize < size ? 0x80000000 : 0);
		ssh.flag2 = config.flag2;
		for (uint i=0; i<2; i++)
			ssh.disk[i] = (int8) diskmgr->GetCurrentDisk(i);

		FileIO file;
		if (file.Open(filename, FileIO::create))
		{
			file.Write(&ssh, sizeof(ssh));
			if (esize < size)
				file.Write(buf+size, esize);
			else
				file.Write(buf, size);
		}
	}
	delete[] buf;
	return true;
}

// ---------------------------------------------------------------------------
//	スナップショット復元
//
bool WinCore::LoadShapshot(const char* filename, const char* diskname)
{
	LockObj lock(this);

	FileIO file;
	if (!file.Open(filename, FileIO::readonly))
		return false;

	SnapshotHeader ssh;
	if (file.Read(&ssh, sizeof(ssh)) != sizeof(ssh))
		return false;
	if (memcmp(ssh.id, SNAPSHOT_ID, 16))
		return false;
	if (ssh.major != ssmajor || ssh.minor > ssminor)
		return false;

	// applyconfig
	const uint fl1a = Config::subcpucontrol | Config::fullspeed 
					| Config::enableopna	| Config::enablepcg
					| Config::fv15k 		| Config::cpuburst 
					| Config::cpuclockmode	| Config::digitalpalette
					| Config::opnona8		| Config::opnaona8
					| Config::enablewait;
	const uint fl2a = Config::disableopn44;

	config.flags = (config.flags & ~fl1a) | (ssh.flags & fl1a);
	config.flag2 = (config.flag2 & ~fl2a) | (ssh.flag2 & fl2a);
	config.basicmode = ssh.basicmode;
	config.clock = ssh.clock;
	config.erambanks = ssh.erambanks;
	config.cpumode = ssh.cpumode;
	config.mainsubratio = ssh.mainsubratio;
	ApplyConfig(&config);

	// Reset
	// スナップショットで PC/SP が別物になるので、一時的なステップの条件とブレークの状態は捨てる
	GetDebugger()->CancelStep();
	GetDebugger()->ClearBreak();
	PC88::Reset();

	// 読み込み

	uint8* buf = new uint8[ssh.datasize];
	bool r = false;

	if (buf)
	{
		bool read = false;
		if (ssh.flags & 0x80000000)
		{
			int32 csize;
			
			file.Read(&csize, 4);
			if (csize < 0)
			{
				csize = -csize;
				uint8* cbuf = new uint8[csize];
				
				if (cbuf)
				{
					ulong bufsize = ssh.datasize;
					file.Read(cbuf, csize);
					read = uncompress(buf, &bufsize, cbuf, csize) == Z_OK; 

					delete[] cbuf;
				}
			}
		}
		else
			read = file.Read(buf, ssh.datasize) == ssh.datasize;

		if (read)
		{
			r = devlist.LoadStatus(buf);
			if (r && diskname)
			{
				for (uint i=0; i<2; i++)
				{
					diskmgr->Unmount(i);
					diskmgr->Mount(i, diskname, false, ssh.disk[i], false);
				}
			}
			if (!r)
			{
				statusdisplay.Show(70, 3000, "バージョンが異なります");
				PC88::Reset();
			}
		}
		delete[] buf;
	}
	return r;
}

// ---------------------------------------------------------------------------
//	外部もじゅーるのためにインターフェースを提供する
//
void* WinCore::QueryIF(REFIID id)
{
	if (id == M88IID_IOBus1)			return static_cast<IIOBus*>(&bus1);
	if (id == M88IID_IOBus2)			return static_cast<IIOBus*>(&bus2);
	if (id == M88IID_IOAccess1)			return static_cast<IIOAccess*> (&bus1);
	if (id == M88IID_IOAccess2)			return static_cast<IIOAccess*> (&bus2);
	if (id == M88IID_MemoryManager1)	return static_cast<IMemoryManager*> (&mm1);
	if (id == M88IID_MemoryManager2)	return static_cast<IMemoryManager*> (&mm2);
	if (id == M88IID_MemoryAccess1)		return static_cast<IMemoryAccess*> (&mm1);
	if (id == M88IID_MemoryAccess2)		return static_cast<IMemoryAccess*> (&mm2);
	if (id == M88IID_SoundControl)		return static_cast<ISoundControl*> (&sound);
	if (id == M88IID_Scheduler)			return static_cast<IScheduler*> (this);
	if (id == M88IID_Time)				return static_cast<ITime*> (this);
	if (id == M88IID_CPUTime)			return static_cast<ICPUTime*> (this);
	if (id == M88IID_DMA)				return static_cast<IDMAAccess*> (GetDMAC());
	if (id == M88IID_ConfigPropBase)	return cfgprop;
	if (id == M88IID_LockCore)			return static_cast<ILockCore*> (this);
	if (id == M88IID_GetMemoryBank)		return static_cast<IGetMemoryBank*> (GetMem1());
	
	return 0;
}


bool WinCore::ConnectExternalDevices()
{
	FileFinder ff;
	extern char m88dir[MAX_PATH];
	char buf[MAX_PATH];
	strncpy_s(buf, MAX_PATH, m88dir, _TRUNCATE);
	strncat_s(buf, MAX_PATH, "*.m88", _TRUNCATE);

	// 読み込めなかったときに Windows のエラーのダイアログを出させない
	UINT olderrmode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

	if (ff.FindFile(buf))
	{
		while (ff.FindNext())
		{
			const char* modname = ff.GetFileName();

			// "*.m88" は 8.3 形式の短い名前にも当たるので、拡張子が .m88 で始まるだけの別のファイル
			// (デバッガの設定の「イメージの名前.m88dbg」など) まで拾う。長い名前の拡張子が本当に .m88 かを確かめる
			// (M88am から)
			size_t modlen = strlen(modname);
			if (modlen < 4 || _stricmp(modname + modlen - 4, ".m88") != 0)
				continue;

			ExtendModule* em = ExtendModule::Create(modname, this);
			if (em)
			{
				extmodules.push_back(em);
			}
			else
			{
				ExternalDevice* extdevice = new ExternalDevice();
				if (extdevice)
				{
					if (extdevice->Init(modname, this, &bus1, GetDMAC(), &sound, &mm1))
					{
						devlist.Add(extdevice);
						extdevices.push_back(extdevice);
					}
					else
					{
						delete extdevice;
					}
				}
			}
		}
	}
	SetErrorMode(olderrmode);
	return true;
}

// ---------------------------------------------------------------------------
//	PAD を接続
//
/*
bool WinCore::EnablePad(bool enable)
{
	if (padenable == enable)
		return true;
	padenable = enable;

	LockObj lock(this);
	if (enable)
	{
		static const IOBus::Connector c_pad[] =
		{
			{ PC88::popnio  , IOBus::portin, WinJoyPad::getdir },
			{ PC88::popnio+1, IOBus::portin, WinJoyPad::getbutton },
			{ PC88::vrtc,     IOBus::portout, WinJoyPad::vsync },
			{ 0, 0, 0 }
		};
		if (!bus1.Connect(&pad, c_pad)) return false;
		pad.Init();
	}
	else
	{
		bus1.Disconnect(&pad);
	}
	return true;
}
*/
// ---------------------------------------------------------------------------
//	マウスを接続
//
/*
bool WinCore::EnableMouse(bool enable)
{
	if (mouseenable == enable)
		return true;
	mouseenable = enable;

	LockObj lock(this);
	if (enable)
	{
		static const IOBus::Connector c_mouse[] =
		{
			{ PC88::popnio  , IOBus::portin, WinMouse::getmove },
			{ PC88::popnio+1, IOBus::portin, WinMouse::getbutton },
			{ 0x40,           IOBus::portout, WinMouse::strobe },
			{ PC88::vrtc,     IOBus::portout, WinMouse::vsync },
			{ 0, 0, 0 }
		};
		if (!bus1.Connect(&mouse, c_mouse)) return false;
		ActivateMouse(true);
	}
	else
	{
		bus1.Disconnect(&mouse);
		ActivateMouse(false);
	}
	return true;
}
*/
