// ---------------------------------------------------------------------------
//	PC-8801 emulator
//	Copyright (C) cisc 1999.
// ---------------------------------------------------------------------------
//	$Id: base.cpp,v 1.19 2003/09/28 14:35:35 cisc Exp $

#include "headers.h"
#include "draw.h"
#include "pc88/base.h"
#include "pc88/pc88.h"
#include "pc88/config.h"
#include "pc88/tapemgr.h"
#include "status.h"

#define LOGNAME "base"
#include "diag.h"

using namespace PC8801;

// ---------------------------------------------------------------------------
//	構築・破壊
//
Base::Base(const ID& id)
: Device(id)
{
	port40 = 0;
	autoboot = true;
}

Base::~Base()
{
}

// ---------------------------------------------------------------------------
//	初期化
//
bool Base::Init(PC88* pc88)
{
	pc = pc88;
	rtcfracq = 0;
	rtcnext = pc->GetTime();
	sw30 = 0xcb;
	sw31 = 0x79;
	sw6e = 0xff;
	// RTC() が割り込みを1回出し、次回のイベントも自分で積む。
	// 繰り返しイベント (repeat = true) では間隔を一定にしかできず、
	// 端数を散らせないため、毎回積み直す形にしている
	RTC();
	return true;
}

// ---------------------------------------------------------------------------
//	スイッチ更新
//
void Base::SetSwitch(const Config* cfg)
{
	bmode = cfg->basicmode;
	clock = cfg->clock;
	dipsw = cfg->dipsw;
	flags = cfg->flags;
	fv15k = cfg->IsFV15k();
}

// ---------------------------------------------------------------------------
//	りせっと
//
void IOCALL Base::Reset(uint, uint)
{
	port40 = 0xc0 + (fv15k ? 2 : 0) + ((dipsw & (1 << 11)) || !autoboot ? 8 : 0);
	sw6e = (sw6e & 0x7f) | ((!clock || Abs(clock) >= 60) ? 0 : 0x80);
	sw31 = ((dipsw >> 5) & 0x3f) | (bmode & 1 ? 0x40 : 0) | (bmode & 0x10 ? 0 : 0x80);
	
	if (bmode & 2)
	{
		//	N80モードのときもDipSWを返すようにする(Xanadu対策)
		sw30 = ~((bmode & 0x10)>>3);
	}
	else
	{
		sw30 = 0xc0 | ((dipsw << 1) & 0x3e) | (bmode & 0x22 ? 1 : 0);
	}	

	const char* mode;
	switch (bmode)
	{
	case Config::N80:    mode = "N"; break;
	case Config::N802:   mode = "N80"; break;
	case Config::N80V2:	 mode = "N80-V2"; break;
	case Config::N88V1:  mode = "N88-V1(S)"; break;
	case Config::N88V1H: mode = "N88-V1(H)"; break;
	case Config::N88V2:  mode = "N88-V2"; break;
	case Config::N88V2CD:mode = "N88-V2 with CD"; break;
	default: mode = "Unknown"; break;
	};
	statusdisplay.Show(100, 2000, "%s mode", mode);
}

// ---------------------------------------------------------------------------
//	Real Time Clock Interrupt (600Hz)
//	
void IOCALL Base::RTC(uint)
{
	pc->bus1.Out(PC88::pint2, 1);
//	LOG0("RTC\n");

	// (M88gn: M88am から取り込んだ修正)
	// 次回を端数込みで積み直す。600Hz = 166.6667 tick なので、
	// 167 に丸め続けると 598.802Hz (-0.2%) になる。
	// 端数を繰り越すと 167,167,166,167,167,166,... と並び、平均が正しくなる
	rtcfracq += rtcperiodq;
	int t = rtcfracq >> 10;
	rtcfracq -= t << 10;

	// 発火の遅れを繰り越さない。CRTC::AddLineEvent と同じ理由で、
	// AddEvent は「今」からの相対で積むため、Execute の行き過ぎぶんが
	// 積み上がって 600Hz が遅くなる。M88am の docs/spec-vrtc-timing.md §11
	rtcnext += t;
	int count = rtcnext - pc->GetTime();
	if (count < 1 || count > t)
	{
		// 補正しきれないほどずれている。現在時刻で引き直す
		count = t;
		rtcnext = pc->GetTime() + count;
	}
	pc->AddEvent(count, this, STATIC_CAST(TimeFunc, &Base::RTC), 0, false);
}

// ---------------------------------------------------------------------------
//	Vertical Retrace Interrupt
//
void IOCALL Base::VRTC(uint, uint en)
{
	if (en)
	{
		pc->VSync();
		pc->bus1.Out(PC88::pint1, 1);
		port40 |= 0x20;
//		LOG0("CRTC: Retrace\n");
	}
	else
	{
		port40 &= ~0x20;
//		LOG0("CRTC: Display\n");
	}
}

// ---------------------------------------------------------------------------
//	In
//
uint IOCALL Base::In30(uint) { return sw30; }

uint IOCALL Base::In31(uint) { return sw31; }

uint IOCALL Base::In40(uint) { return IOBus::Active(port40, 0x2a); }

uint IOCALL Base::In6e(uint) { return sw6e | 0x7f; }

// ---------------------------------------------------------------------------
//	device description
//
const Device::Descriptor Base::descriptor = { indef, outdef };

const Device::OutFuncPtr Base::outdef[] = 
{
	STATIC_CAST(Device::OutFuncPtr, &Reset),
	STATIC_CAST(Device::OutFuncPtr, &VRTC),
};

const Device::InFuncPtr Base::indef[] = 
{
	STATIC_CAST(Device::InFuncPtr, &In30),
	STATIC_CAST(Device::InFuncPtr, &In31),
	STATIC_CAST(Device::InFuncPtr, &In40),
	STATIC_CAST(Device::InFuncPtr, &In6e),
};
