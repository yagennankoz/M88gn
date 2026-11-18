// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	フロッピーのドライブの音 (シークの 1 ステップごとの音)
// ---------------------------------------------------------------------------

#include "headers.h"
#include "fddsound.h"
#include "misc.h"
#include <math.h>
#include "fddclick.h"
#include "fddmech.h"

using namespace PC8801;

FDDSound::FDDSound()
{
	soundcontrol = 0;
	rate = 0;
	amp = maxamp;
	clicks = 0;
	clicklen = 0;
	mech[0] = mech[1] = 0;
	mechlen[0] = mechlen[1] = 0;
	mechend = 0;
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
	delete[] mech[0];
	delete[] mech[1];
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
	mechend = 0;
	delete[] clicks;
	clicks = 0;
	clicklen = 0;
	for (int k = 0; k < 2; k++)
	{
		delete[] mech[k];
		mech[k] = 0;
		mechlen[k] = 0;
	}
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
	mech[0] = Convert(fddinsert, sizeof(fddinsert) / sizeof(int16), fddmechrate, rate, &mechlen[0]);
	mech[1] = Convert(fddeject, sizeof(fddeject) / sizeof(int16), fddmechrate, rate, &mechlen[1]);
	return true;
}

// ---------------------------------------------------------------------------
//	録音した音を合成のレートに直線で補間して変換する
//
int16* FDDSound::Convert(const int16* src, int srclen, int srcrate, uint rate, int* len)
{
	int n = int(double(srclen - 1) * rate / srcrate);
	int16* dst = new int16[n];
	for (int i = 0; i < n; i++)
	{
		double p = double(i) * srcrate / rate;
		int k = int(p);
		double f = p - k;
		dst[i] = int16(src[k] * (1 - f) + src[k + 1] * f);
	}
	*len = n;
	return dst;
}

// ---------------------------------------------------------------------------
//	ディスクを入れた / 取り出した。前の出し入れの音が鳴っていれば、終わって mechgap 後に鳴らす
//
void FDDSound::Mech(bool insert)
{
	CriticalSection::Lock lock(cs);
	int k = insert ? 0 : 1;
	if (!mech[k] || nvoice >= maxvoice)
		return;
	int at = mechend > 0 ? mechend + int(rate * mechgap / 1000) : 0;
	voice[nvoice].src = mech[k];
	voice[nvoice].len = mechlen[k];
	voice[nvoice].pos = -at;
	nvoice++;
	mechend = at + mechlen[k];
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
		voice[nvoice].src = clicks + NextWave() * clicklen;
		voice[nvoice].len = clicklen;
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
		mechend = 0;
		return;
	}
	int a = amp;
	for (int v = 0; v < nvoice; )
	{
		Voice& vc = voice[v];
		const int16* src = vc.src;
		int len = vc.len;
		int i = 0;
		if (vc.pos < 0)
			i = Min(-vc.pos, nsamples);			// まだ鳴らさない分
		int p = vc.pos + i;
		int32* d = dest + i * 2;
		for (; i < nsamples && p < len; i++, p++, d += 2)
		{
			int s = (src[p] * a) >> 10;
			d[0] += s;
			d[1] += s;
		}
		vc.pos += nsamples;
		if (vc.pos >= len)
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
	mechend = Max(0, mechend - nsamples);
}
