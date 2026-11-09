// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Copyright (C) cisc 1999.
// ---------------------------------------------------------------------------
//	$Id: beep.h,v 1.2 1999/10/10 01:47:04 cisc Exp $

#pragma once

#include "device.h"
#include "critsect.h"
#include <vector>

// ---------------------------------------------------------------------------

class PC88;

namespace PC8801
{
class Sound;
class Config;
class OPNIF;

// ---------------------------------------------------------------------------
//
//
class Beep : public Device, public ISoundSource
{
public:
	enum IDFunc
	{
		out40=0,
	};

public:
	Beep(const ID& id);
	~Beep();
	
	bool Init();
	void Cleanup();	
	void EnableSING(bool s) { p40mask = s ? 0xa0 : 0x20; port40 &= p40mask; }
	
	bool IFCALL Connect(ISoundControl* sc);
	bool IFCALL SetRate(uint rate);
	void IFCALL Mix(int32*, int);
		
	const Descriptor* IFCALL GetDesc() const { return &descriptor; }
	uint IFCALL GetStatusSize();
	bool IFCALL SaveStatus(uint8* status);
	bool IFCALL LoadStatus(const uint8* status);
	
	void IOCALL Out40(uint, uint data);

private:
	enum { ssrev = 1, };
	struct Status
	{
		uint8 rev;
		uint8 port40;
		uint32 prevtime;
	};
	
	ISoundControl* soundcontrol;
	int bcount;
	int bperiod;
	// 高い音を丸めるフィルター: RC の 1 次のローパスを 2 段重ねたもの。
	// 切り替えの時刻ごとに、アナログのフィルターの応答を式で求める
	// (サンプルに区切る前にフィルターを掛けたことになり、高い倍音の折り返しが減る)
	enum { lpfcut = 7000, };	// 全体で -3dB になる周波数 (Hz)
	double lpfa;				// 1 段の係数 (2π x 1 段の遮断周波数、1/2000 サンプルあたり)
	double lpfe;				// exp(-lpfa x 2000) (1 サンプルぶん)
	double lpf1, lpf2;			// 1 段目・2 段目の出力
	// 低い音を削るフィルター (実機のヘッドホン端子の手前のコンデンサーのように、直流を止める)。
	// 実機の録音で、0 に戻る時定数は約 6.3ms (約 25Hz)
	enum { hpfcut = 25, };		// 遮断周波数 (Hz)
	double hpfk;				// 係数
	double hpfl;				// 入力の低い音の部分 (出力 = 入力 - hpfl)
	void Filter(double x, int d);

	// ポート 40h の切り替え。時刻は 1/2000 サンプル単位 (Sound::GetSubsampleTime と同じ)。
	// 合成のときに 1 サンプルごとに鳴っていた時間を積分するので、
	// 1 サンプルより短いパルス (BEEP で音楽を鳴らすソフトのパルス幅の変調など) も音量として残る
	struct Edge
	{
		__int64 pos;
		uint state;
	};
	enum { maxedges = 65536, };
	std::vector<Edge> edges;
	size_t edgehead;			// まだ合成していない最初の切り替え
	__int64 mixpos;				// 合成した位置
	uint mixstate;				// 合成した位置の port40
	CriticalSection cs;

	uint port40;
	uint p40mask;

	static const Descriptor descriptor;
	static const OutFuncPtr outdef[];
};

}

