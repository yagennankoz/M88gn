// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	フロッピーのドライブの音 (シークの 1 ステップごとの音)
// ---------------------------------------------------------------------------
//	FDC がシークしたときに Step() を呼ぶ。1 ステップごとに、実機から録音した音
//	(fddclick.h の 8 つから選ぶ。毎回少しずつ違う音にする) を stepms ごとに並べて鳴らす。
//	長いシークは音が重なって続く。エミュレーションが待たない設定 (FDD のウェイト無し) でも、
//	音は実機の間隔で鳴らす。シークの無い読み書きは、実機 (PC-8801FH) でも聞こえる音が無いので鳴らさない

#pragma once

#include "types.h"
#include "if/ifcommon.h"
#include "critsect.h"

namespace PC8801
{

class FDDSound : public ISoundSource
{
public:
	FDDSound();
	~FDDSound();

	void Step(int steps);				// steps ステップ動かす (エミュレーションのスレッドから呼ぶ)
	void SetVolume(int percent);		// 音量 (0～100%)

	// ISoundSource
	bool IFCALL Connect(ISoundControl* sc);
	bool IFCALL SetRate(uint rate);
	void IFCALL Mix(int32* dest, int nsamples);

private:
	enum
	{
		maxamp = 102,				// 音量 100% の大きさ (1024 = fddclick.h のまま。初めの 1024 では大きすぎ、その 25% (-20dB) で十分だった)
		stepms = 4,					// 1 ステップの間隔 (ms。FDC のシークの待ちと同じ)
		maxvoice = 32,				// 同時に鳴らす音の数 (50ms の音を 4ms ごと = 13 前後)
		maxpending = 160,			// 鳴らす前のステップの数の上限 (0 → 79 の往復ぶん)
	};
	struct Voice
	{
		int wave;					// 鳴らす音 (fddclick の番号)
		int pos;					// 音の中の位置 (負ならまだ鳴らしていない。サンプル単位)
	};

	ISoundControl* soundcontrol;
	uint rate;
	int amp;						// 音の大きさ (1024 = fddclick.h のまま)
	int16* clicks;					// 合成のレートに変換した音 (fddclicknum 個)
	int clicklen;					// 変換した音の長さ (サンプル)
	int stepsamples;				// 1 ステップの間隔 (サンプル)
	Voice voice[maxvoice];
	int nvoice;
	int pending;					// 最後に並べたステップの、今からの位置 (サンプル。-1: 鳴らし始めた / 無し)
	CriticalSection cs;				// voice (エミュレーションのスレッドの Step と、合成の Mix の間)
	uint32 rnd;
	int lastwave;
	int NextWave();
};

}
