// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Copyright (C) cisc 1999.
// ---------------------------------------------------------------------------
//	$Id: beep.cpp,v 1.2 1999/10/10 01:47:04 cisc Exp $

#include "headers.h"
#include "pc88/beep.h"
#include "misc.h"
#include <math.h>

using namespace PC8801;

// ---------------------------------------------------------------------------
//	生成・破棄
//
Beep::Beep(const ID& id)
: Device(id), soundcontrol(0)
{
}

Beep::~Beep()
{
	Cleanup();
}

// ---------------------------------------------------------------------------
//	初期化とか
//
bool Beep::Init()
{
	port40 = 0;
	p40mask = 0xa0;
	bcount = 0;
	bperiod = 0;
	lpfa = 0;
	lpfe = 1;
	lpf1 = lpf2 = 0;
	hpfk = 0;
	hpfl = 0;
	edgehead = 0;
	mixpos = 0;
	mixstate = 0;
	return true;
}

// ---------------------------------------------------------------------------
//	後片付け
//
void Beep::Cleanup()
{
	Connect(0);
}


bool IFCALL Beep::Connect(ISoundControl* control)
{
	if (soundcontrol) soundcontrol->Disconnect(this);
	soundcontrol = control;
	if (soundcontrol) soundcontrol->Connect(this);
	return true;
}

// ---------------------------------------------------------------------------
//	レート設定
//
bool Beep::SetRate(uint rate)
{
	CriticalSection::Lock lock(cs);
	bcount = 0;
	bperiod = int(2400.0 / rate * (1 << 14));
	// 実機のスピーカーまわりのように、矩形波の角を丸める。
	// 同じ RC を 2 段重ねると、1 段の遮断周波数の 0.6436 倍で -3dB になる
	lpfa = 2 * 3.14159265358979 * (lpfcut / 0.6436) / (rate * 2000.0);
	lpfe = exp(-lpfa * 2000);
	lpf1 = lpf2 = 0;
	hpfk = 1 - exp(-2 * 3.14159265358979 * hpfcut / rate);
	hpfl = 0;
	edges.clear();
	edgehead = 0;
	mixpos = 0;
	mixstate = port40;
	return true;
}

// ---------------------------------------------------------------------------
//	ビープ音合成
//	1 サンプル (2000) の中を、ポート 40h が切り替わった時刻で区切り、
//	区間ごとの入力 (鳴っていれば 8192) でフィルター (Filter) を進め、サンプルの終わりの値を出す。
//	SING (bit7) はそのまま、BEEP (bit5) は 2400Hz の矩形波の
//	このサンプルでの平均 (1 サンプルを 8 つに分けて求める) を入力にする。
//	SING と BEEP は OR (どちらかが 1 なら 1)
//
void IFCALL Beep::Mix(int32* dest, int nsamples)
{
	CriticalSection::Lock lock(cs);

	// 合成した位置より前の切り替えは、すぐに反映する
	while (edgehead < edges.size() && edges[edgehead].pos <= mixpos)
		mixstate = edges[edgehead++].state;

	for (int i = 0; i < nsamples; i++)
	{
		__int64 end = mixpos + 2000;
		bool more = edgehead < edges.size() && edges[edgehead].pos < end;
		if (!more && !(mixstate & 0xa0) && fabs(lpf1) < 0.5 && fabs(lpf2) < 0.5 && fabs(hpfl) < 0.5)
		{
			// 鳴っていない (フィルターの出力も消えた) 間は、次の切り替えまで飛ばす
			lpf1 = lpf2 = 0;
			hpfl = 0;
			int skip = nsamples - i;
			if (edgehead < edges.size())
				skip = Min(skip, int((edges[edgehead].pos - mixpos) / 2000));
			dest += skip * 2;
			mixpos += __int64(skip) * 2000;
			bcount += bperiod * 8 * skip;
			i += skip - 1;
			continue;
		}

		// 2400Hz の矩形波の、このサンプルでの平均 (0～8)
		int osc = 0;
		for (int j = 0; j < 8; j++)
		{
			osc += (bcount >> 16) & 1;
			bcount += bperiod;
		}
		double beepin = 8192.0 * osc / 8;

		// 区間ごとにフィルターを進める
		__int64 t = mixpos;
		for (;;)
		{
			__int64 next = end;
			if (edgehead < edges.size() && edges[edgehead].pos < end)
				next = edges[edgehead].pos;
			int d = int(next - t);
			if (d > 0)
				Filter(mixstate & 0x80 ? 8192.0 : mixstate & 0x20 ? beepin : 0.0, d);
			if (next >= end)
				break;
			mixstate = edges[edgehead++].state;
			t = next;
		}
		hpfl += (lpf2 - hpfl) * hpfk;
		int sample = int(lpf2 - hpfl);
		*dest++ += sample; *dest++ += sample;
		mixpos = end;
	}

	// 使い終わった切り替えを捨てる
	if (edgehead > 0 && (edgehead == edges.size() || edgehead > 4096))
	{
		edges.erase(edges.begin(), edges.begin() + edgehead);
		edgehead = 0;
	}
}

// ---------------------------------------------------------------------------
//	フィルターを、入力 x のまま d (1/2000 サンプル単位) だけ進める
//	同じ RC (時定数 1/a) を 2 段: 入力が一定なら、t 後の値は
//	  1 段目 x + u1 e^(-at)、2 段目 x + (u2 + u1 at) e^(-at)  (u1, u2 は始めの値 - x)
//
void Beep::Filter(double x, int d)
{
	double ad = lpfa * d;
	double e = d == 2000 ? lpfe : exp(-ad);
	double u1 = lpf1 - x, u2 = lpf2 - x;
	lpf2 = x + (u2 + u1 * ad) * e;
	lpf1 = x + u1 * e;
}

// ---------------------------------------------------------------------------
//	BEEP Port への Out
//
void IOCALL Beep::Out40(uint, uint data)
{
	data &= p40mask;
	int i = data ^ port40;
	if (i & 0xa0)
	{
		if (soundcontrol)
		{
			// 合成を今の時刻の近くまで進めてから、合成した位置からの時刻を求める
			soundcontrol->Update(this);
			int tdiff = soundcontrol->GetSubsampleTime(this);

			CriticalSection::Lock lock(cs);
			if (edges.size() - edgehead >= maxedges)
			{
				// 合成が止まっている (音を出していない) ときは、溜めずに捨てる
				edges.clear();
				edgehead = 0;
				mixstate = data;
			}
			else
			{
				Edge e;
				e.pos = mixpos + tdiff;
				if (!edges.empty() && edges.back().pos > e.pos)
					e.pos = edges.back().pos;
				e.state = data;
				edges.push_back(e);
			}
		}
		port40 = data;
	}
}

// ---------------------------------------------------------------------------
//	状態保存
//
uint IFCALL Beep::GetStatusSize()
{
	return sizeof(Status);
}

bool IFCALL Beep::SaveStatus(uint8* s)
{
	Status* st = (Status*) s;
	st->rev = ssrev;
	st->port40 = port40;
	return true;
}

bool IFCALL Beep::LoadStatus(const uint8* s)
{
	const Status* st = (const Status*) s;
	if (st->rev != ssrev)
		return false;
	port40 = st->port40;
	CriticalSection::Lock lock(cs);
	edges.clear();
	edgehead = 0;
	mixstate = port40;
	return true;
}

// ---------------------------------------------------------------------------
//	device description
//
const Device::Descriptor Beep::descriptor = { 0, outdef };

const Device::OutFuncPtr Beep::outdef[] = 
{
	STATIC_CAST(Device::OutFuncPtr, &Out40),
};

