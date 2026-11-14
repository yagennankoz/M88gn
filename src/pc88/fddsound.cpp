// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	フロッピーのドライブの音 (シークの 1 ステップごとの音)
// ---------------------------------------------------------------------------

#include "headers.h"
#include "fddsound.h"
#include "misc.h"
#include <math.h>
#include "fddclick.h"

using namespace PC8801;

FDDSound::FDDSound()
{
	soundcontrol = 0;
	rate = 0;
	amp = maxamp;
	clicks = 0;
	clicklen = 0;
	stepsamples = 0;
	nvoice = 0;
	pending = -1;
	rnd = 0x12345678;
	lastwave = -1;
}

FDDSound::~FDDSound()
{
	// つなぎは外さない (FDC を消すのは PC88 の後始末で、そのときには WinCore の音はもう無い)
	delete[] clicks;
}

bool IFCALL FDDSound::Connect(ISoundControl* sc)
{
	if (soundcontrol) soundcontrol->Disconnect(this);
	soundcontrol = sc;
	if (soundcontrol) soundcontrol->Connect(this);
	return true;
}

// ---------------------------------------------------------------------------
//	合成のレートに合わせて、録音した音 (44100Hz) を直線で補間して変換する
//
bool IFCALL FDDSound::SetRate(uint r)
{
	CriticalSection::Lock lock(cs);
	nvoice = 0;
	pending = -1;
	delete[] clicks;
	clicks = 0;
	clicklen = 0;
	rate = r;
	if (!rate)
		return true;
	clicklen = int(double(fddclicklen - 1) * rate / fddclickrate);
	clicks = new int16[clicklen * fddclicknum];
	for (int n = 0; n < fddclicknum; n++)
	{
		for (int i = 0; i < clicklen; i++)
		{
			double p = double(i) * fddclickrate / rate;
			int k = int(p);
			double f = p - k;
			clicks[n * clicklen + i] = int16(fddclick[n][k] * (1 - f) + fddclick[n][k + 1] * f);
		}
	}
	stepsamples = Max(1, int(rate * stepms / 1000));
	return true;
}

// ---------------------------------------------------------------------------
//	音量 (テープの再生音と同じく、体感に合わせる。25% = -20dB、50% = -10dB)
//
void FDDSound::SetVolume(int percent)
{
	percent = Limit(percent, 100, 0);
	amp = percent ? int(maxamp * pow(percent / 100.0, log(10.0) / log(2.0) / 2)) : 0;
}

// ---------------------------------------------------------------------------
//	8 つの音から 1 つ選ぶ (続けて同じ音にはしない)
//
int FDDSound::NextWave()
{
	int w;
	do
	{
		rnd = rnd * 1103515245 + 12345;
		w = (rnd >> 16) % fddclicknum;
	} while (w == lastwave && fddclicknum > 1);
	lastwave = w;
	return w;
}

// ---------------------------------------------------------------------------
//	シーク: 今の位置から stepms ごとに音を並べる。前のシークの音がまだ残っていれば、その後に続ける
//
void FDDSound::Step(int steps)
{
	if (!clicks || steps <= 0)
		return;
	if (soundcontrol)
		soundcontrol->Update(this);			// 今までの分を合成してから並べる
	CriticalSection::Lock lock(cs);
	int start = pending >= 0 ? pending + stepsamples : 0;
	for (int i = 0; i < steps && nvoice < maxvoice; i++)
	{
		int at = start + i * stepsamples;
		if (at > maxpending * stepsamples)
			break;
		voice[nvoice].wave = NextWave();
		voice[nvoice].pos = -at;
		nvoice++;
		pending = at;
	}
}

// ---------------------------------------------------------------------------
//	合成
//
void IFCALL FDDSound::Mix(int32* dest, int nsamples)
{
	CriticalSection::Lock lock(cs);
	if (!nvoice || !clicks)
	{
		pending = -1;
		return;
	}
	int a = amp;
	for (int v = 0; v < nvoice; )
	{
		Voice& vc = voice[v];
		const int16* src = clicks + vc.wave * clicklen;
		int i = 0;
		if (vc.pos < 0)
			i = Min(-vc.pos, nsamples);			// まだ鳴らさない分
		int p = vc.pos + i;
		int32* d = dest + i * 2;
		for (; i < nsamples && p < clicklen; i++, p++, d += 2)
		{
			int s = (src[p] * a) >> 10;
			d[0] += s;
			d[1] += s;
		}
		vc.pos += nsamples;
		if (vc.pos >= clicklen)
			voice[v] = voice[--nvoice];			// 鳴り終わった
		else
			v++;
	}
	if (pending >= 0)
	{
		pending -= nsamples;
		if (pending < 0)
			pending = -1;
	}
}
