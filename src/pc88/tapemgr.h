// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Copyright (C) cisc 1997, 2000.
// ---------------------------------------------------------------------------
//	$Id: tapemgr.h,v 1.2 2000/06/26 14:05:30 cisc Exp $

#pragma once

#include "device.h"
#include "opna.h"
#include "psg.h"
#include "soundbuf.h"
#include "schedule.h"
#include "critsect.h"
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
//
//
class TapeManager : public Device, public ISoundSource
{
public:
	enum
	{
		requestdata = 0, out30, txdata, siomode,
		in40 = 0,
	};

	TapeManager();
	~TapeManager();

	bool Init(Scheduler* s, IOBus* bus, int pin, int ptxrdy = 0);

	// readonly: 書き込み禁止にする (ファイルが読み取り専用のときも書き込み禁止になる)
	bool Open(const char* file, bool readonly = false);
	static bool CreateNew(const char* file);		// 空の T88 を作る
	bool Close();
	bool Rewind(bool timer = true);

	// 頭出し: 次 / 前の区切りの始まりに移る (dir: 1 = 次、-1 = 前)
	//   区切りは、BLANK の続きの始まりと、MARK と MARK にはさまれた SPACE の続きの始まり。
	//   前は、今いる区切りの後の DATA が始まっていればその区切りの始まりへ、
	//   そうでなければ (区切りの無音やリーダーの途中なら) 1 つ前へ
	//   (データは短いと 1 秒ほどなので、始まってからの時間では決めない)
	//   (無ければテープの先頭)。次が無ければ移らずに false
	bool Cue(int dir);
	bool CanCue() { return tags && !recording && !recarm; }

	bool IsOpen() { return tags || wave; }
	bool IsWave() { return wave; }		// PicoCmt の .RAW (波形) を開いている

	bool Motor(bool on);
	bool Carrier();
	
	bool Seek(uint pos, uint offset);
	uint GetPos();

	uint ReadByte();
	void IOCALL RequestData(uint=0, uint=0);

	void IOCALL Out30(uint, uint en);
	uint IOCALL In40(uint);

	uint IFCALL GetStatusSize();
	bool IFCALL SaveStatus(uint8* status);
	bool IFCALL LoadStatus(const uint8* status);

	// ロードの速さ (1: 実機どおり、2/4/8: 倍率)
	void SetSpeed(int s);

	// 再生音の音量 (0～100%)
	void SetVolume(int percent);

	// 録音 (テープへのセーブ)。データレコーダーの録音ボタンのように使う
	//   Record をオンにしてモーターが回ると録音になり (再生は止める)、CPU が送ったバイトを
	//   開いている T88 の最後に追記する。モーターが止まると書いて、Record はオフに戻る。
	//   T88 に既にデータがあれば、UI が Record をオンにする前に追記してよいか確認し、
	//   AllowAppend() を呼ぶ (一度許可したら、開き直すまで確認しない)
	void SetRecord(bool on);
	bool NeedAppendConfirm() { return tags && hasdata && !appendok; }
	void AllowAppend() { appendok = true; }
	bool IsRecord() { return recarm; }
	bool IsWriteProtected() { return writeprotect; }
	const char* GetFileName() { return filename.c_str(); }

	// UI への知らせ (エミュレーションのスレッドから呼ぶ)
	enum Notify
	{
		notify_norecord = 2,		// Record がオフのときにセーブが行われた
		notify_notape,			// テープを開いていないときにセーブが行われた
	};
	void SetNotify(void (*func)(void*, int), void* ctx) { notify = func, notifyctx = ctx; }

	void IOCALL TxData(uint, uint data);	// CPU が SIO に送信のバイトを書いたとき
	void IOCALL SioMode(uint, uint data);	// CPU が SIO にモード命令を書いたとき

	// 再生音 (ISoundSource)
	bool IFCALL Connect(ISoundControl* sc);
	bool IFCALL SetRate(uint rate);
	void IFCALL Mix(int32* dest, int nsamples);

	const Descriptor* IFCALL GetDesc() const { return &descriptor; }
	
private:
	enum
	{
		T88VER		= 0x100,
		ssrev		= 1,
	};
	enum Mode
	{
		T_END		= 0,
		T_VERSION	= 1,
		T_BLANK		= 0x100,
		T_DATA		= 0x101,
		T_SPACE		= 0x102,
		T_MARK		= 0x103,
	};
	struct TagHdr
	{
		uint16 id;
		uint16 length;
	};
	struct Tag
	{
		Tag* next;
		Tag* prev;
		uint16 id;
		uint16 length;
		uint8 data[1];
	};
	struct BlankTag
	{
		uint32 pos;
		uint32 tick;
	};
	struct DataTag
	{
		uint32 pos;
		uint32 tick;
		uint16 length;
		uint16 type;
		uint8 data[1];
	};
	struct Status
	{
		uint8 rev;
		bool motor;
		uint32 pos;
		uint offset;
	};
	
	void Proceed(bool timer = true);
	void IOCALL Timer(uint=0);
	void Send(uint);
	void SetTimer(int t, int wait = -1);
	int Wait(int count);
	void PlayStart();
	void PlayStop();
	bool IsCuePoint(Tag* t);
	uint32 CueDataPos(Tag* t);
	void GoTag(Tag* t);
	static uint32 TagPos(Tag* t) { return ((BlankTag*) t->data)->pos; }

	Scheduler* scheduler;
	SchedulerEvent* event;
	Tag* tags;
	Tag* pos;
	int offset;
	uint32 tick;
	Mode mode;
	uint time;			// motor on: タイマー開始時間
	uint timercount;
	uint timerremain;	// タイマー残り
	int waitremain;		// モーターが止まっている間の、実際に待つ残りの時間 (10us 単位。倍率で縮めた後)
	bool motor;

	IOBus* bus;
	int pinput;

	uint8* data;
	int datasize;
	int datatype;

	int speed;					// ロードの速さの倍率 (1 = 実機どおり)
	int lastsend;				// 最後にバイトを送った時刻 (Scheduler の時刻、10us 単位)
	int datastart;				// DATA の最初のバイトを送った時刻 (かかった時間の表示用)
	int lastin40;				// CPU が最後にポート 40h (キャリア) を読んだ時刻 (10us 単位)
	bool gapfast;				// CPU が次のデータを待っている: BLANK / SPACE / MARK を Tape speed で縮める
	void GapFast();
	int databytes;				// DATA のバイト数 (かかった時間の表示用)

	// 録音
	struct RecTag
	{
		uint16 id;					// T_MARK / T_SPACE / T_DATA
		uint32 tick;				// MARK / SPACE の長さ (1/4800 秒単位。DATA はバイト数から求める)
		uint16 type;				// DATA の種類 (0x01cc: 1200ボー 0x00cc: 600ボー)
		std::vector<uint8> data;
	};
	enum
	{
		recgapmax = 2000,			// 送信の間がこれより空いたら、DATA を区切って間を MARK / SPACE にする (10us 単位 = 20ms)
		recdatamax = 0x8000,		// 1 つの DATA タグの最大のバイト数
	};
	void IOCALL TxDone(uint=0);
	bool IsCMT() { return !(port30 & 0x20); }				// ポート 30h の bit5-4: 00 = テープ 600ボー、01 = テープ 1200ボー、1x = RS-232C
	// DATA の型 (T88 の実データ種別): bit8 = 1200ボー、bit2-7 = 8251 のモード命令と同じ並び
	// (bit7-6 ストップビット長、bit5 偶数パリティ、bit4 パリティあり、bit3-2 データビット長)
	static int NormType(int type);		// 下位バイトが 0 か、ストップビット長が 00 なら 0xCC (8bit・パリティ無し・ストップ 2) にする
	static int ByteTicks(int type);		// 1 バイトの長さ (1/4800 秒単位。1200ボーの 1 ビット = 4)
	enum { framed = 0x80000000 };		// SIO とのやり取りで、データに型 (bit8-16) を付けたしるし
	// ポート 30h の bit2: 0 = キャリア (MARK) を出す、1 = 出さない (SPACE)
	uint16 GapId() { return port30 & 4 ? T_SPACE : T_MARK; }
	bool LoadTags(const char* file);
	void ToEnd();
	void Notify(int code) { if (notify) notify(notifyctx, code); }
	void RecBegin();
	void RecStart();
	void RecStop();
	void RecWriteAndReload(const std::vector<RecTag>& tags);
	void RecSync(int now);
	void RecAddGap(uint16 id, int ticks);
	void RecByte(uint d, uint16 type);
	bool RecWrite(const std::vector<RecTag>& tags);

	int ptxrdy;
	uint port30;
	SchedulerEvent* txevent;
	int lasttxdone;				// 最後に送信を終えた時刻 (10us 単位)
	std::string filename;		// 開いている T88
	bool writeprotect;			// 書き込み禁止
	bool hasdata;				// 開いている T88 に DATA がある
	bool recarm;				// Record がオン
	bool recording;				// 録音中 (Record がオンでモーターが回っている)
	bool appendok;				// 開いている T88 への追記を許可済み (開き直すまで確認しない)
	bool txwarned;				// Record がオフのセーブを知らせた (モーターが回るたびに 1 回)
	bool baudwarned;			// テープと PC の速さが違うことを知らせた (DATA ごとに 1 回)
	std::vector<RecTag> rectags;	// 録音中のセーブ (モーターが止まるまで)
	uint16 recseg;				// 今の区切り (0: なし T_MARK / T_SPACE: 間 T_DATA: データ)
	int recsegstart;			// 間の始まりの時刻 (10us 単位)
	void (*notify)(void*, int);
	void* notifyctx;

	// 再生音
	enum
	{
		minwait = 480,			// 速くするときの BLANK / SPACE / MARK の最短の長さ (1/4800 秒単位 = 100ms)
		pollrecent = 1000,		// この時間内にポート 40h が読まれていたら、CPU がキャリアを待っているとみなす (10us 単位 = 10ms)
		readgap = 100,			// CPU が読んでから次のバイトを送るまでの最短の時間 (10us 単位 = 1ms)
		sndqsize = 8,			// 再生待ちのバイトの数
		sndamp = 1536,			// 音の大きさ (音量 100% のとき)
		clickms = 125,			// リレーの音の最長の長さ (ms。tapeclick.h の音より長くする)
		clickmax = 55467 * clickms / 1000 + 1,
	};
	void SoundFlush();
	void SoundState();

	ISoundControl* soundcontrol;
	int amp;					// 今の音の大きさ (sndamp x 音量。体感に合わせたカーブ)
	int clickamp;				// リレーの音の大きさ (sndamp x 音量。比例)
	uint32 sndstep;				// 1200Hz の 1 サンプルあたりの位相の進み
	uint32 tphase;				// 音の位相
	uint32 bphase;				// ビットの位相
	uint32 bstep;				// 今のフレームの 1 サンプルあたりのビットの位相の進み
	uint sndframe;				// 再生中のフレーム (下位ビットから送る)
	int sndbits;				// 再生中のフレームの残りビット数
	int sndidle;				// フレームが無いときの音 (0: 無音 1: 1200Hz 2: 2400Hz)
	uint32 sndq[sndqsize];		// 再生待ちのバイト (bit0-7) と型 (bit8-16)
	volatile int sndqr, sndqw;

	// リレーの音 (カチッ)。PC-8001mkII の実機の録音 (tapeclick.h) を合成のレートに変換したもの。値は 1024 = clickamp
	void MakeClick(uint rate);
	int16 clickon[clickmax];
	int16 clickoff[clickmax];
	int clicklenon, clicklenoff;
	int clicklen;				// 鳴らしている音の長さ
	const int16* click;			// 鳴らしている音 (0: 無し)
	int clickpos;

	// 波形 (PicoCmt の .RAW)
	//   録音したままの半周期の並びを読み、PC-8801 のテープの回路のように復調する。
	//   半周期が wvthresh より短ければ 2400Hz ("1")、長ければ 1200Hz ("0")、
	//   無音や wvnone より長いものは信号なし。復調した信号を CPU が設定した形式で
	//   ビットごとに読んで SIO に渡し、2400Hz が続いている間はキャリアを立てる。
	//   録音したデッキの速さのずれは、2400Hz が続く所で測り、テープの時間を本来の速さに直す
	//   (周波数の違う高速変調の部分などでは測らず、前の値のまま)。
	//   時刻は直した後のテープの上の時刻 (us)。Tape speed は効かない (いつも実機の速さ)
	enum
	{
		wvthresh = 312,			// 2400Hz と 1200Hz の半周期の境 (us)
		wvnone = 2000,			// これより長い半周期は信号なし (us)
		wvcdon = 8,				// 2400Hz の半周期がこれだけ続いたらキャリアを立てる (4 波 = 約 1.7ms)
		wvrawbits = 0x40000000,	// SIO とのやり取りで、データが読んだビットの並び (スタートビットの次から) であるしるし
		wvsync = 30000,			// 音の位置がテープの位置とこれ以上ずれたら合わせ直す (us)
		wvrunmin = 32,			// テープの速さは、2400Hz の半周期がこれだけ続く所で測る
		wvrunlong = 256,		// これより短い所の値は、前の値から 5% 以内のときだけ使う
	};
	bool LoadRaw(const char* file);
	void WaveClear();
	uint32 WaveNow();						// テープの上の今の時刻 (us)
	bool WaveLine(uint32 t);				// 復調した信号 (1 / 0)
	bool WaveCarrier(uint32 t);				// キャリア
	uint32 WaveNextFall(uint32 t);			// t より後で、復調した信号が 1 から 0 になる時刻 (無ければ ~0)
	double WaveBit();						// 1 ビットの長さ (us)
	int WaveFrameBits();					// スタートビットの次から最初のストップビットまでのビットの数
	void WaveSeek(uint32 t);
	void WaveSchedule();
	void IOCALL WaveTimer(uint=0);
	void WaveSoundSync(bool force = false);

	bool wave;
	CriticalSection wvcs;					// 波形のデータ (音の合成と開く・閉じるの間)
	std::vector<uint32> wvhalf;				// 半周期の終わりの時刻 (us)
	std::vector<uint8> wvlevel;				// 半周期のレベル (0: Low 1: High 2: 無音)
	std::vector<uint32> wvline;				// 復調した信号が切り替わる時刻 (最初は 1)
	std::vector<uint32> wvcd;				// キャリアが切り替わる時刻 (最初は立っていない)
	uint32 wvpos;							// テープの位置 (us。モーターが回っている間は回り始めた位置)
	int wvstart;							// モーターが回り始めた時刻 (10us 単位)
	uint32 rxsearch;						// 次のスタートビットをこの時刻より後から探す (us)
	uint32 rxstart;							// 受信中のバイトのスタートビットの始まり (~0: 探している)
	uint siomodebyte;						// CPU が SIO に書いたモード命令
	SchedulerEvent* wvevent;
	double sndwvcur;						// 音の位置 (us)
	double sndwvdt;							// 1 サンプルの長さ (us)
	size_t sndwvidx;						// 音の位置の半周期
	volatile uint32 sndwvtarget;			// 合わせる位置 (エミュレーションのスレッドが書く)
	volatile int sndwvreq;					// 0: なし 1: ずれていたら合わせる 2: 必ず合わせる

private:
	static const Descriptor descriptor;
	static const InFuncPtr  indef[];
	static const OutFuncPtr outdef[];
};

