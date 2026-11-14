// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Copyright (C) cisc 1997, 2000.
// ---------------------------------------------------------------------------
//	$Id: tapemgr.cpp,v 1.3 2000/08/06 09:58:51 cisc Exp $

#include "headers.h"
#include "tapemgr.h"
#include "file.h"
#include "status.h"
#include "misc.h"
#include <math.h>
#include "tapeclick.h"
#include <algorithm>


#define LOGNAME	"tape"
#include "diag.h"

#define T88ID	"PC-8801 Tape Image(T88)"


// ---------------------------------------------------------------------------
//	構築
//
TapeManager::TapeManager()
: Device(DEV_ID('T','A','P','E')), tags(0), scheduler(0), event(0)
{
	soundcontrol = 0;
	amp = sndamp;
	clickamp = sndamp;
	clicklen = clicklenon = clicklenoff = 0;
	click = 0;
	clickpos = 0;
	speed = 1;
	lastsend = 0;
	datastart = 0;
	lastin40 = 0;
	gapfast = false;
	databytes = 0;
	sndstep = 0;
	tphase = bphase = bstep = 0;
	sndframe = 0;
	sndbits = 0;
	sndidle = 0;
	sndqr = sndqw = 0;
	datasize = 0;
	datatype = 0;
	pinput = 0;
	pos = 0;
	bus = 0;
	offset = 0;
	mode = T_BLANK;
	motor = false;
	ptxrdy = 0;
	port30 = 0;
	txevent = 0;
	lasttxdone = 0;
	writeprotect = false;
	hasdata = false;
	appendok = false;
	recarm = false;
	recording = false;
	txwarned = false;
	baudwarned = false;
	recseg = 0;
	recsegstart = 0;
	notify = 0;
	notifyctx = 0;
	wave = false;
	wvpos = 0;
	wvstart = 0;
	rxsearch = 0;
	rxstart = ~0u;
	siomodebyte = 0xce;
	wvevent = 0;
	sndwvcur = 0;
	sndwvdt = 0;
	sndwvidx = 0;
	sndwvtarget = 0;
	sndwvreq = 0;
}

// ---------------------------------------------------------------------------
//	破棄
//
TapeManager::~TapeManager()
{
	Connect(0);
	Close();
}

// ---------------------------------------------------------------------------
//	初期化
//
bool TapeManager::Init(Scheduler* s, IOBus* b, int pi, int ptr)
{
	scheduler = s;
	bus = b;
	pinput = pi;
	ptxrdy = ptr;
	txevent = 0;

	motor = false;
	timercount = 0;
	timerremain = 0;
	waitremain = 0;
	tick = 0;
	return true;
}
	
// ---------------------------------------------------------------------------
//	T88 を開く
//
bool TapeManager::Open(const char* file, bool readonly)
{
	Close();

	// 拡張子が .raw なら PicoCmt の RAW (波形)。書き込みはできない
	size_t len = strlen(file);
	if (len >= 4 && !_stricmp(file + len - 4, ".raw"))
	{
		if (!LoadRaw(file))
		{
			Close();
			return false;
		}
		filename = file;
		writeprotect = true;
		return Rewind();
	}

	if (!LoadTags(file))
	{
		Close();
		return false;
	}
	filename = file;

	// 書き込み禁止: 指定されたとき、または書き込みで開けないとき (読み取り専用のファイルなど)
	writeprotect = readonly;
	if (!writeprotect)
	{
		FileIO fio;
		if (!fio.Open(file, 0))
			writeprotect = true;
	}

	if (!Rewind())
		return false;
	return true;
}

// ---------------------------------------------------------------------------
//	T88 のタグを読む
//
bool TapeManager::LoadTags(const char* file)
{
	FileIO fio;
	if (!fio.Open(file, FileIO::readonly))
		return false;

	// ヘッダ確認
	char buf[24];
	fio.Read(buf, 24);
	if (memcmp(buf, T88ID, 24))
		return false;

	// タグのリスト構造を展開
	hasdata = false;
	Tag* prv = 0;
	do
	{
		TagHdr hdr;
		if (fio.Read(&hdr, 4) != 4)
			hdr.id = T_END, hdr.length = 0;		// END が無いまま終わっていたら、END があることにする

		Tag* tag = (Tag*) new uchar[sizeof(Tag)-1+hdr.length];
		if (!tag)
			return false;

		tag->prev = prv;
		tag->next = 0;
		(prv ? prv->next : tags) = tag;
		tag->id = hdr.id;
		tag->length = hdr.length;
		fio.Read(tag->data, tag->length);
		if (tag->id == T_DATA)
			hasdata = true;
		prv = tag;
	} while (prv->id);
	return true;
}

// ---------------------------------------------------------------------------
//	空の T88 (ヘッダー・バージョン・END だけ) を作る
//
bool TapeManager::CreateNew(const char* file)
{
	FileIO fio;
	if (!fio.Open(file, FileIO::create))
		return false;
	static const uint8 head[] = { 0x01, 0x00, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
	fio.Write(T88ID, 24);			// T88ID は 23 文字 + 終わりの 0
	fio.Write(head, sizeof(head));	// バージョンのタグ (1.00) と END
	return true;
}

// ---------------------------------------------------------------------------
//	とじる
//
bool TapeManager::Close()
{
	if (recording || recarm)
	{
		rectags.clear();
		recording = recarm = false;
		recseg = 0;
	}
	filename.clear();
	writeprotect = false;
	hasdata = false;
	appendok = false;
	if (scheduler)
		SetTimer(0);
	while (tags)
	{
		Tag* n = tags->next;
		delete []tags;
		tags = n;
	}
	pos = 0;
	mode = T_BLANK;
	WaveClear();
	SoundState();
	return true;
}

// ---------------------------------------------------------------------------
//	まきもどす
//
bool TapeManager::Rewind(bool timer)
{
	if (wave)
	{
		WaveSeek(0);
		return true;
	}
	pos = tags;
	scheduler->DelEvent(event), event = 0;
	if (pos)
	{
		tick = 0;

		// バージョン確認
		// 最初のタグはバージョンタグになるはず？
		if (   pos->id != T_VERSION
			|| pos->length < 2 
			|| *(uint16*)pos->data != T88VER)
			return false;
		
		pos = pos->next;
		Proceed(timer);
	}
	SoundState();
	return true;
}

// ---------------------------------------------------------------------------
//	頭出し
//
//	区切り: BLANK の続きの始まり、MARK と MARK にはさまれた SPACE の続きの始まり
//	(セーブとセーブの間。データの前のリーダーの SPACE は、前が MARK でないので含まない)
//
bool TapeManager::IsCuePoint(Tag* t)
{
	if (t->id != T_BLANK && t->id != T_SPACE)
		return false;
	if (t->prev && t->prev->id == t->id)
		return false;						// 続きの途中
	if (t->id == T_BLANK)
		return true;
	if (!t->prev || t->prev->id != T_MARK)
		return false;
	Tag* n = t->next;
	while (n && n->id == T_SPACE)
		n = n->next;
	return n && n->id == T_MARK;
}

//	区切りの後の最初の DATA の始まり (次の区切りまでに DATA が無ければ ~0)
uint32 TapeManager::CueDataPos(Tag* t)
{
	for (Tag* n = t->next; n && !IsCuePoint(n); n = n->next)
	{
		if (n->id == T_DATA)
			return TagPos(n);
	}
	return ~0u;
}

//	タグの始まりに移る (BLANK / SPACE)
void TapeManager::GoTag(Tag* t)
{
	scheduler->DelEvent(event), event = 0;
	gapfast = false;
	tick = TagPos(t);
	pos = t;
	Proceed();						// モーターが止まっていれば、残りの時間を持つだけ
	SoundState();
}

bool TapeManager::Cue(int dir)
{
	if (!CanCue())
		return false;

	// 今のタグ (Proceed は、処理中のタグの次を pos に置く。終わりなら pos = 0)
	Tag* cur = tags;
	if (pos)
		cur = pos->prev;
	else
		while (cur->next)
			cur = cur->next;

	if (dir > 0)
	{
		for (Tag* t = cur->next; t; t = t->next)
		{
			if (IsCuePoint(t))
			{
				GoTag(t);
				return true;
			}
		}
		return false;
	}

	uint now = GetPos();
	for (Tag* t = cur; t && t->id != T_VERSION; t = t->prev)
	{
		// データが始まっていればこの区切りへ。区切りの無音やリーダーの途中なら、さらに前へ
		if (IsCuePoint(t) && CueDataPos(t) != ~0u && now > CueDataPos(t))
		{
			GoTag(t);
			return true;
		}
	}
	return Rewind();
}

// ---------------------------------------------------------------------------
//	モータ
//
bool TapeManager::Motor(bool s)
{
	if (motor == s)
		return true;
	SoundFlush();
	click = 0;							// リレーの音
	clicklen = s ? clicklenon : clicklenoff;
	clickpos = 0;
	click = s ? clickon : clickoff;
	if (s)
	{
		motor = true;
		txwarned = false;
		if (recarm && tags)
			RecBegin();
		else
			PlayStart();
	}
	else
	{
		if (recording)
			RecStop();
		else
			PlayStop();
		motor = false;
	}
	SoundState();
	return true;
}

// ---------------------------------------------------------------------------
//	再生を進める / 止める (モーター ON / OFF、録音の始め・終わり)
//
//	止めている間は、実際に待つ残りの時間 (waitremain、Tape speed で縮めた後) を持つ
//
void TapeManager::PlayStart()
{
	statusdisplay.Show(10, 2000, "Motor on: %d %d", timerremain, timercount);
	if (wave)
	{
		wvstart = scheduler->GetTime();
		WaveSchedule();
		return;
	}
	gapfast = false;
	time = scheduler->GetTime();
	if (waitremain > 0)
		event = scheduler->AddEvent(waitremain, this, STATIC_CAST(TimeFunc, &TapeManager::Timer));
	waitremain = 0;
}

void TapeManager::PlayStop()
{
	if (wave)
	{
		wvpos = WaveNow();		// (motor はまだ true)
		scheduler->DelEvent(wvevent), wvevent = 0;
		return;
	}
	waitremain = 0;
	if (event && event->inst == this)
	{
		waitremain = Max(1, event->count - scheduler->GetTime());
		timerremain = Max(1, waitremain * 6 / 125);
		scheduler->DelEvent(event), event = 0;
		statusdisplay.Show(10, 2000, "Motor off: %d %d", timerremain, timercount);
	}
}

// ---------------------------------------------------------------------------

uint TapeManager::GetPos()
{
	if (wave)
		return WaveNow();
	if (motor)
	{
		if (timercount)
			return tick + (scheduler->GetTime() - time) * 6 / 125;
		else
			return tick;
	}
	else
	{
		return tick + timercount - timerremain;
	}
}

// ---------------------------------------------------------------------------
//	タグを処理
//
void TapeManager::Proceed(bool timer)
{
	while (pos)
	{
		LOG1("TAG %d\n", pos->id);
		switch (pos->id)
		{
		case T_END:
			mode = T_BLANK;
			pos = 0;
			statusdisplay.Show(50, 0, "end of tape", tick);
			timercount = 0;
			return;

		case T_BLANK:
		case T_SPACE:
		case T_MARK:
		{
			BlankTag* t = (BlankTag*) pos->data;
			mode = (Mode) pos->id;

			// (符号付きで比べる。DATA が T88 の長さより長く続くと負になる)
			int32 len = int32(t->pos + t->tick - tick);
			if (len <= 0)
				break;

			if (timer)
			{
				// BLANK / SPACE / MARK を Tape speed で縮めるのは、CPU が次のデータを待っている
				// (ポート 40h でキャリアを見た) とわかってから、次のデータが来るまで。
				// それまでは等速で流し、見に来たら残りを縮める (GapFast)。
				// ファイルの後の処理中や、モーターを止める前に、次のファイルの前の間を
				// 流しきってしまわないようにする
				if (!gapfast)
					gapfast = speed > 1 && motor && scheduler->GetTime() - lastin40 < pollrecent;
				SetTimer(len, gapfast ? Wait(len) : len);
			}
			else
				timercount = len;

			pos = pos->next;
			return;
		}

		case T_DATA:
		{
			DataTag* t = (DataTag*) pos->data;
			mode = T_DATA;

			data = t->data;
			datasize = t->length;
			datatype = t->type;
			offset = 0;

			if (!datasize)
				break;

			pos = pos->next;
			datastart = scheduler->GetTime();
			databytes = datasize;
			baudwarned = false;
			if (timer)
				SetTimer(ByteTicks(datatype));
			else
				timercount = t->tick;
			return;
		}
		}
		pos = pos->next;
	}
}

// ---------------------------------------------------------------------------
//
//
void IOCALL TapeManager::Timer(uint)
{
	event = 0;			// 発火したイベントの枠は空きになっている
	SoundFlush();
	tick += timercount;
	statusdisplay.Show(50, 0, "tape: %d", tick);
	
	if (mode == T_DATA)
	{
		lastsend = scheduler->GetTime();
		Send(*data++);
		offset++;
		if (--datasize > 0)
		{
			SetTimer(ByteTicks(datatype));
			SoundState();
			return;
		}
		LOG0("\n");
		gapfast = false;		// データの後は、CPU が次を待ち始めるまで等速
		// かかった時間を表示する (速さの確認用)
		int ms = (scheduler->GetTime() - datastart) / 100;
		statusdisplay.Show(20, 8000, "Tape: %d bytes / %d.%.2d s (x%d)", databytes, ms / 1000, ms % 1000 / 10, speed);
	}
	Proceed();
	SoundState();
}

// ---------------------------------------------------------------------------
//	キャリア確認
//
bool TapeManager::Carrier()
{
	if (wave)
		return motor && WaveCarrier(WaveNow());
	if (mode == T_MARK && !recording)
	{
		LOG0("*");
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
//	タイマー更新
//
//	count: テープの上の長さ (1/4800 秒単位)、wait: 実際に待つ長さ (省略時は count)
//
void TapeManager::SetTimer(int count, int wait)
{
	if (count > 100)
		LOG1("Timer: %d\n", count);
	if (wait < 0)
		wait = count;
	scheduler->DelEvent(event), event = 0;
	timercount = count;
	if (motor)
	{
		time = scheduler->GetTime();
		if (count)			// 100000/4800
			event = scheduler->AddEvent(Max(1, wait*125/6), this, STATIC_CAST(TimeFunc, &TapeManager::Timer));
	}
	else
	{
		timerremain = count;
		waitremain = count ? Max(1, wait*125/6) : 0;
	}
}

// ---------------------------------------------------------------------------
//	BLANK / SPACE / MARK を実際に待つ長さ
//	速くするとき (speed > 1) は 1/speed にする。ただし ROM がキャリアを
//	見つけられるよう、minwait (100ms) より短くはしない (元が短ければ元のまま)
//
int TapeManager::Wait(int count)
{
	if (speed <= 1)
		return count;
	return Max(count / speed, Min(count, int(minwait)));
}

// ---------------------------------------------------------------------------
//	CPU がキャリアを待っている: 今の BLANK / SPACE / MARK の残りを Tape speed で縮める
//
void TapeManager::GapFast()
{
	if (speed <= 1 || !motor || recording || !event || event->inst != this)
		return;
	if (mode != T_BLANK && mode != T_SPACE && mode != T_MARK)
		return;
	gapfast = true;
	int now = scheduler->GetTime();
	int rem = event->count - now;
	int nrem = Max(rem / speed, Min(rem, int(minwait) * 125 / 6));
	if (nrem < rem)
	{
		scheduler->SetEvent(event, Max(1, nrem), this, STATIC_CAST(TimeFunc, &TapeManager::Timer));
	}
}

// ---------------------------------------------------------------------------
//	DATA の型
//
int TapeManager::NormType(int type)
{
	int mode = type & 0xfc;
	if (!(type & 0xff) || !(type & 0xc0))
		mode = 0xcc;
	return (type & 0x100) | mode;
}

int TapeManager::ByteTicks(int type)
{
	type = NormType(type);
	int bit = type & 0x100 ? 4 : 8;
	int databits = 5 + ((type >> 2) & 3);
	int paritybit = (type >> 4) & 1;
	int stophalf = ((type >> 6) & 3) + 1;		// ストップビット長 (半ビット単位): 01 = 1、10 = 1.5、11 = 2
	return (2 * (1 + databits + paritybit) + stophalf) * bit / 2;
}

// ---------------------------------------------------------------------------
//	バイト転送
//
inline void TapeManager::Send(uint byte)
{
	LOG1("%.2x ", byte);

	// テープの速さ (DATA の type) と PC 側の設定 (ポート 30h の bit4) が違うときは渡さない。
	// 実機では正しいバイトにならず、ヘッダーが見つからないまま探し続ける
	bool match = !IsCMT() || (!!(datatype & 0x100) == !!(port30 & 0x10));
	if (match)
		bus->Out(pinput, (byte & 0xff) | (NormType(datatype) << 8) | framed);
	else if (!baudwarned)
	{
		baudwarned = true;
		statusdisplay.Show(20, 5000, "Tape: baud rate mismatch (tape %s, PC %s)",
			datatype & 0x100 ? "1200" : "600", port30 & 0x10 ? "1200" : "600");
	}

	// 再生音: 1 バイト分のフレームを再生待ちに入れる
	// (速くロードしているときは実時間より速く届くので、再生待ちがいっぱいなら捨てる。音の高さは変えない)
	if (soundcontrol && motor)
	{
		int w = (sndqw + 1) % sndqsize;
		if (w != sndqr)
		{
			sndq[sndqw] = (byte & 0xff) | (NormType(datatype) << 8);
			sndqw = w;
		}
	}
}

// ---------------------------------------------------------------------------
//	CPU がデータを読んだとき (SIO から呼ばれる)
//
void TapeManager::RequestData(uint, uint)
{
	if (wave)
		return;			// 波形はいつも実機の速さ
	// 等速 (speed = 1) では、実機どおり 1 バイトごとの間隔 (Timer() の ByteTicks。8N2 なら 44 / 88) で送る。
	// 速くするとき (speed = 2/4/8) は、次の 2 つの遅い方の時刻に次のバイトを送る
	//   - CPU が読んでから readgap (1ms) 後
	//     (PC-8801 の ROM は割り込みで受け取ったバイトを置き場所に入れ、読み込みのループが
	//      取り出していると考えられる。すぐに次を送ると、取り出す前に上書きしてしまう)
	//   - 前のバイトを送ってから、実機の間隔 / speed 後
	if (speed > 1 && mode == T_DATA && motor && event && event->inst == this)
	{
		int now = scheduler->GetTime();
		int interval = ByteTicks(datatype) * 125 / 6 / speed;
		int t = now + readgap;
		if (lastsend + interval - t > 0)
			t = lastsend + interval;
		if (event->count - t > 0)
			scheduler->SetEvent(event, t - now, this, STATIC_CAST(TimeFunc, &TapeManager::Timer));
	}
}

// ---------------------------------------------------------------------------
//	シークする
//
bool TapeManager::Seek(uint newpos, uint off)
{
	if (wave)
	{
		WaveSeek(newpos);
		return true;
	}
	if (!Rewind(false))
		return false;

	while (pos && (tick + timercount) < newpos)
	{
		tick += timercount;
		Proceed(false);
	}
	if (!pos)
		return false;
	
	switch (pos->prev->id)
	{
		int l;

	case T_BLANK:
	case T_SPACE:
	case T_MARK:
		mode = (Mode) pos->prev->id;
		l = tick+timercount-newpos;
		tick = newpos;
		SetTimer(l);
		break;

	case T_DATA:
		mode = T_DATA;
		offset = off;
		newpos = tick + offset * ByteTicks(datatype);
		data += offset;
		datasize -= offset;
		SetTimer(ByteTicks(datatype));
		break;

	default:
		return false;
	}
	SoundState();
	return true;
}

// ---------------------------------------------------------------------------

void IOCALL TapeManager::Out30(uint, uint d)
{
	// bit2: キャリア (0 = 出す)、bit3: モーター、bit5-4: SIO の使い道と速さ
	if (recording && motor && ((port30 ^ d) & 4))
	{
		// 録音中にキャリアが切り替わった: ここまでの間をタグにして、新しい間を始める
		SoundFlush();
		RecSync(scheduler->GetTime());
		port30 = d;
		if (recseg != T_DATA)
			recseg = GapId();
		SoundState();
	}
	port30 = d;
	Motor(!!(d & 8));
}

uint IOCALL TapeManager::In40(uint)
{
	lastin40 = scheduler->GetTime();
	if (!gapfast)
		GapFast();
	return IOBus::Active(Carrier() ? 4 : 0, 4);
}


// ---------------------------------------------------------------------------
//	状態保存
//
uint IFCALL TapeManager::GetStatusSize()
{
	return sizeof(Status);
}

bool IFCALL TapeManager::SaveStatus(uint8* s)
{
	Status* status = (Status*) s;
	status->rev = ssrev;
	status->motor = motor;
	status->pos = GetPos();
	status->offset = offset;
	statusdisplay.Show(0, 1000, "tapesave: %d", status->pos);
	return true;
}

bool IFCALL TapeManager::LoadStatus(const uint8* s)
{
	const Status* status = (const Status*) s;
	if (status->rev != ssrev)
		return false;
	motor = status->motor;
	Seek(status->pos, status->offset);
	statusdisplay.Show(0, 1000, "tapesave: %d", GetPos());
	return true;
}

// ---------------------------------------------------------------------------
//	ロードの速さ (1: 実機どおり、2/4/8: 倍率)
//
void TapeManager::SetSpeed(int s)
{
	speed = Limit(s, 8, 1);
}

// ---------------------------------------------------------------------------
//	再生音の音量 (0～100%)
//
void TapeManager::SetVolume(int percent)
{
	// 体感の大きさに合わせる (25% = -20dB、50% = -10dB、75% = -4dB。10dB で体感が半分)
	percent = Limit(percent, 100, 0);
	amp = percent ? int(sndamp * pow(percent / 100.0, log(10.0) / log(2.0) / 2)) : 0;
	clickamp = sndamp * percent / 100;	// リレーの音は比例にする
}

// ---------------------------------------------------------------------------
//	再生音 (データレコーダーのモニターの音)
//	PC-8801 のテープの信号: 1200ボーは "0" = 1200Hz 1 波・"1" = 2400Hz 2 波、
//	600ボーは "0" = 1200Hz 2 波・"1" = 2400Hz 4 波。
//	1 バイトはスタートビット (0)・データ 8 ビット (下位から)・ストップビット 2 つ (1)。
//	MARK はキャリア (2400Hz)、SPACE は 1200Hz、BLANK は無音。
//	速くロードしているときも音は実時間で鳴らす (間に合わないバイトは鳴らさない)
//
bool IFCALL TapeManager::Connect(ISoundControl* sc)
{
	if (soundcontrol) soundcontrol->Disconnect(this);
	soundcontrol = sc;
	if (soundcontrol) soundcontrol->Connect(this);
	return true;
}

bool IFCALL TapeManager::SetRate(uint rate)
{
	sndstep = rate ? uint32(1200.0 * 4294967296.0 / rate) : 0;
	sndwvdt = rate ? 1000000.0 / rate : 0;
	MakeClick(rate);
	return true;
}

// ---------------------------------------------------------------------------
//	リレーの音を作る
//	実機の録音 (tapeclick.h、44100Hz) を合成のレートに変換する (直線で補間)
//
static int ResampleClick(int16* dest, int max, const int16* src, int srclen, uint rate)
{
	if (!rate)
		return 0;
	int len = Min(int(double(srclen - 1) * rate / tapeclickrate), max);
	for (int i = 0; i < len; i++)
	{
		double p = double(i) * tapeclickrate / rate;
		int k = int(p);
		double f = p - k;
		dest[i] = int16(src[k] * (1 - f) + src[k + 1] * f);
	}
	return len;
}

void TapeManager::MakeClick(uint rate)
{
	click = 0;
	clicklen = 0;
	clicklenon  = ResampleClick(clickon,  clickmax, tapeclick_on,  sizeof(tapeclick_on)  / sizeof(int16), rate);
	clicklenoff = ResampleClick(clickoff, clickmax, tapeclick_off, sizeof(tapeclick_off) / sizeof(int16), rate);
}

// ---------------------------------------------------------------------------
//	状態が変わる前に、今までの分の音を合成しておく
//	(エミュレーションのスレッドから呼ばれる Motor() と Timer() だけで使う。
//	 UI のスレッドから呼ばれる Open・Close・設定の変更では使わない)
//
void TapeManager::SoundFlush()
{
	if (soundcontrol)
		soundcontrol->Update(this);
	if (wave)
		WaveSoundSync();
}

// ---------------------------------------------------------------------------
//	今の状態から、フレームが無いときの音を決める
//
void TapeManager::SoundState()
{
	int idle = 0;
	if (recording)
	{
		// 録音中: 送っていない間は、キャリアなら 2400Hz、そうでなければ 1200Hz
		if (motor && IsCMT())
			idle = GapId() == T_MARK ? 2 : 1;
	}
	else if (motor && pos)
	{
		switch (mode)
		{
		case T_MARK:
		case T_DATA:	idle = 2; break;	// データの間もキャリア
		case T_SPACE:	idle = 1; break;
		default:		idle = 0; break;
		}
	}
	if (!motor)
		sndqr = sndqw;						// 再生待ちを捨てる
	sndidle = idle;
}

// ---------------------------------------------------------------------------
//	音の合成
//
void IFCALL TapeManager::Mix(int32* dest, int nsamples)
{
	if (!sndstep)
		return;
	int a = amp;

	// 波形 (.RAW): 録音した波形をそのまま (矩形波で) 鳴らす
	CriticalSection::Lock lock(wvcs);
	bool wv = wave && motor && !wvhalf.empty();
	if (wv && sndwvreq)
	{
		uint32 tg = sndwvtarget;
		double diff = double(tg) - sndwvcur;
		if (sndwvreq == 2 || diff > wvsync || diff < -wvsync)
		{
			sndwvcur = tg;
			sndwvidx = std::upper_bound(wvhalf.begin(), wvhalf.end(), tg) - wvhalf.begin();
		}
		sndwvreq = 0;
	}

	for (; nsamples > 0; nsamples--, dest += 2)
	{
		// リレーの音
		if (click)
		{
			int c = (click[clickpos] * clickamp) >> 10;
			dest[0] += c;
			dest[1] += c;
			if (++clickpos >= clicklen)
				click = 0;
		}

		if (wv)
		{
			uint32 c = uint32(sndwvcur);
			while (sndwvidx < wvhalf.size() && wvhalf[sndwvidx] <= c)
				sndwvidx++;
			if (sndwvidx < wvhalf.size() && wvlevel[sndwvidx] != 2)
			{
				int s = wvlevel[sndwvidx] ? a : -a;
				dest[0] += s;
				dest[1] += s;
			}
			sndwvcur += sndwvdt;
			continue;
		}

		// 次のフレームを取り出す
		if (sndbits <= 0 && sndqr != sndqw)
		{
			uint q = sndq[sndqr];
			sndqr = (sndqr + 1) % sndqsize;
			// 型どおりのビット列: スタート 0・データ・パリティ・ストップ 1 (1.5 は 2 にする)
			uint b = q & 0xff;
			int type = (q >> 8) & 0x1ff;
			int databits = 5 + ((type >> 2) & 3);
			b &= (1 << databits) - 1;
			sndframe = b << 1;
			sndbits = 1 + databits;
			if (type & 0x10)
			{
				uint ones = 0;
				for (uint x = b; x; x >>= 1)
					ones += x & 1;
				uint par = (ones & 1) ^ (type & 0x20 ? 0 : 1);	// 偶数パリティ: 1 の数を偶数に
				sndframe |= par << sndbits;
				sndbits++;
			}
			int stopbits = ((type >> 6) & 3) == 1 ? 1 : 2;
			sndframe |= ((1 << stopbits) - 1) << sndbits;
			sndbits += stopbits;
			bstep = (type & 0x100) ? sndstep : sndstep / 2;
			bphase = 0;
		}

		int freq;
		if (sndbits > 0)
		{
			freq = (sndframe & 1) ? 2 : 1;
			bphase += bstep;
			if (bphase < bstep)			// 1 ビット分が過ぎた
			{
				sndframe >>= 1;
				sndbits--;
			}
		}
		else
			freq = sndidle;

		if (freq)
		{
			tphase += sndstep * freq;
			int s = (tphase & 0x80000000) ? -a : a;
			dest[0] += s;
			dest[1] += s;
		}
	}
}

// ---------------------------------------------------------------------------
//	CPU が SIO に送信のバイトを書いたとき (SIO から呼ばれる)
//	1 バイトを送る時間 (1200ボー 44、600ボー 88、1/4800 秒単位) が過ぎたら、
//	SIO に送信の終わりを知らせる (TXRDY が立つ)。ロードの速さの倍率で短くする。
//	RS-232C のときはすぐに知らせる
//
void IOCALL TapeManager::TxData(uint, uint d)
{
	int wait = 1;
	if (IsCMT())
	{
		// 型: 速さはポート 30h の bit4、形式は CPU が SIO に設定したモード命令
		int mode = d & framed ? (d >> 8) & 0xfc : 0;
		uint16 type = uint16(NormType((port30 & 0x10 ? 0x100 : 0) | mode));
		wait = Max(1, ByteTicks(type) * 125 / 6 / speed);

		if (recording && motor)
		{
			SoundFlush();
			RecByte(d & 0xff, type);

			// 再生音 (ロードと同じ。再生待ちがいっぱいなら鳴らさない)
			if (soundcontrol)
			{
				int w = (sndqw + 1) % sndqsize;
				if (w != sndqr)
				{
					sndq[sndqw] = (d & 0xff) | (type << 8);
					sndqw = w;
				}
			}
			SoundState();
		}
		else if (motor && !txwarned)
		{
			// Record がオフ (またはテープが無い) のにセーブが行われた
			txwarned = true;
			Notify(IsOpen() ? notify_norecord : notify_notape);
		}
	}
	scheduler->DelEvent(txevent);
	txevent = scheduler->AddEvent(wait, this, STATIC_CAST(TimeFunc, &TapeManager::TxDone));
	if (!txevent)
		TxDone();		// イベントの枠が足りないときは、止まらないようにすぐ知らせる
}

void IOCALL TapeManager::SioMode(uint, uint d)
{
	siomodebyte = d & 0xff;
}

void IOCALL TapeManager::TxDone(uint)
{
	txevent = 0;
	lasttxdone = scheduler->GetTime();
	if (ptxrdy)
		bus->Out(ptxrdy, 0);
}

// ---------------------------------------------------------------------------
//	Record ボタン
//	オンにすると、次にモーターが回ったときに録音する (回っていれば今から録音する)。
//	オフにすると、録音の途中ならそこまでを書いて再生に戻る
//
void TapeManager::SetRecord(bool on)
{
	if (on)
	{
		if (!tags || writeprotect || recarm || NeedAppendConfirm())
			return;
		recarm = true;
		appendok = true;
		if (motor)
		{
			PlayStop();
			RecBegin();
		}
	}
	else
	{
		if (recording)
		{
			RecStop();
			if (motor)
				PlayStart();
		}
		recarm = false;
	}
	SoundState();
}

// ---------------------------------------------------------------------------
//	録音を始める (Record がオンでモーターが回った)
//
void TapeManager::RecBegin()
{
	recording = true;
	RecStart();
	statusdisplay.Show(20, 3000, "Tape: recording");
}

// ---------------------------------------------------------------------------
//	モーターが回り始めた (録音中)
//
void TapeManager::RecStart()
{
	rectags.clear();
	recseg = GapId();
	recsegstart = scheduler->GetTime();
}

// ---------------------------------------------------------------------------
//	モーターが止まった (録音中): 1 回のセーブの終わり。Record はオフに戻る
//	データが無ければ (モーターを回しただけなら) 何も書かない
//
void TapeManager::RecStop()
{
	RecSync(scheduler->GetTime());
	recseg = 0;
	recording = false;
	recarm = false;

	int bytes = 0;
	for (size_t i = 0; i < rectags.size(); i++)
		bytes += int(rectags[i].data.size());
	if (bytes)
	{
		// 最後に 2 秒の SPACE を付ける (X88000 で保存した T88 と同じ形。ROM はモーターを止めるまで
		// キャリアを出しているので、実機のテープではここは止まっている)
		RecAddGap(T_SPACE, 9600);
		RecWriteAndReload(rectags);
	}
	rectags.clear();
}

// ---------------------------------------------------------------------------
//	書いて、テープを読み直す (位置はテープの最後。続けてロードするなら Rewind する)
//
void TapeManager::RecWriteAndReload(const std::vector<RecTag>& t)
{
	int bytes = 0;
	for (size_t i = 0; i < t.size(); i++)
		bytes += int(t[i].data.size());

	bool r = RecWrite(t);
	statusdisplay.Show(20, 5000, r ? "Tape: saved %d bytes" : "Tape: cannot write the file", bytes);

	// 読み直す
	std::string file = filename;
	bool wp = writeprotect;
	while (tags)
	{
		Tag* n = tags->next;
		delete []tags;
		tags = n;
	}
	pos = 0;
	if (!LoadTags(file.c_str()))
	{
		Close();
		return;
	}
	ToEnd();
	filename = file;
	writeprotect = wp;
}

// ---------------------------------------------------------------------------
//	テープの最後へ送る
//
void TapeManager::ToEnd()
{
	scheduler->DelEvent(event), event = 0;
	waitremain = 0;
	tick = 0;
	for (Tag* t = tags; t; t = t->next)
	{
		if (t->id >= T_BLANK && t->id <= T_MARK && t->length >= 8)
		{
			BlankTag* b = (BlankTag*) t->data;
			tick = Max(tick, b->pos + b->tick);
		}
		if (!t->next)
			pos = t;		// END
	}
	Proceed(false);
	SoundState();
}

// ---------------------------------------------------------------------------
//	ここまでの間 (MARK / SPACE) をタグにする
//	データの後に送信が recgapmax より空いていたら、データを区切って間にする
//
void TapeManager::RecSync(int now)
{
	if (recseg == T_DATA)
	{
		if (now - lasttxdone <= recgapmax)
			return;
		recseg = GapId();
		recsegstart = lasttxdone;
	}
	if (recseg == T_MARK || recseg == T_SPACE)
	{
		RecAddGap(recseg, (now - recsegstart) * 6 / 125);
		recsegstart = now;
	}
}

void TapeManager::RecAddGap(uint16 id, int ticks)
{
	if (ticks <= 0)
		return;
	if (!rectags.empty() && rectags.back().id == id)
	{
		rectags.back().tick += ticks;
		return;
	}
	RecTag t;
	t.id = id;
	t.tick = ticks;
	t.type = 0;
	rectags.push_back(t);
}

// ---------------------------------------------------------------------------
//	1 バイトを録音する
//
void TapeManager::RecByte(uint d, uint16 type)
{
	RecSync(scheduler->GetTime());
	if (recseg != T_DATA || rectags.empty() || rectags.back().id != T_DATA
		|| rectags.back().type != type || rectags.back().data.size() >= recdatamax)
	{
		RecTag t;
		t.id = T_DATA;
		t.tick = 0;
		t.type = type;
		rectags.push_back(t);
	}
	rectags.back().data.push_back(uint8(d));
	recseg = T_DATA;
}

// ---------------------------------------------------------------------------
//	タグをファイルの後ろ (END の前) に書く
//	テープの上の位置は、ファイルの最後のタグの終わりから続ける
//
bool TapeManager::RecWrite(const std::vector<RecTag>& tags)
{
	FileIO fio;
	if (!fio.Open(filename.c_str(), 0))
		return false;

	char buf[24];
	if (fio.Read(buf, 24) != 24 || memcmp(buf, T88ID, 24))
		return false;

	// END を探し、最後の位置を求める
	uint32 tapepos = 0;
	int32 endpos = -1;
	for (;;)
	{
		int32 p = fio.Tellp();
		TagHdr hdr;
		if (fio.Read(&hdr, 4) != 4)
			break;
		if (hdr.id == T_END)
		{
			endpos = p;
			break;
		}
		if (hdr.id >= T_BLANK && hdr.id <= T_MARK && hdr.length >= 8)
		{
			uint32 pt[2];
			fio.Read(pt, 8);
			tapepos = Max(tapepos, pt[0] + pt[1]);
			fio.Seek(hdr.length - 8, FileIO::current);
		}
		else
			fio.Seek(hdr.length, FileIO::current);
	}
	if (endpos < 0)
		return false;

	// タグを作る
	std::vector<uint8> out;
	for (size_t i = 0; i < tags.size(); i++)
	{
		const RecTag& t = tags[i];
		uint32 tick = t.tick;
		uint16 len = 8;
		if (t.id == T_DATA)
		{
			tick = uint32(t.data.size()) * ByteTicks(t.type);
			len = uint16(12 + t.data.size());
		}
		uint8 h[16];
		*(uint16*) (h + 0) = t.id;
		*(uint16*) (h + 2) = len;
		*(uint32*) (h + 4) = tapepos;
		*(uint32*) (h + 8) = tick;
		out.insert(out.end(), h, h + 12);
		if (t.id == T_DATA)
		{
			*(uint16*) (h + 0) = uint16(t.data.size());
			*(uint16*) (h + 2) = t.type;
			out.insert(out.end(), h, h + 4);
			out.insert(out.end(), t.data.begin(), t.data.end());
		}
		tapepos += tick;
	}
	static const uint8 endtag[4] = { 0, 0, 0, 0 };
	out.insert(out.end(), endtag, endtag + 4);

	fio.Seek(endpos, FileIO::begin);
	if (fio.Write(&out[0], int32(out.size())) != int32(out.size()))
		return false;
	fio.SetEndOfFile();
	return true;
}

// ---------------------------------------------------------------------------
//	波形 (PicoCmt の .RAW) を読む
//	16bit の値 (リトルエンディアン) の並び。bit15 = レベル、bit14-0 = 半周期の長さ (us。
//	録音の回路の分 2us を足す)。0x0000 は読み飛ばす。0xFFFF の次の値は無音の長さ (ms)
//
bool TapeManager::LoadRaw(const char* file)
{
	FileIO fio;
	if (!fio.Open(file, FileIO::readonly))
		return false;
	fio.Seek(0, FileIO::end);
	int32 size = fio.Tellp();
	fio.Seek(0, FileIO::begin);
	if (size < 2)
		return false;
	std::vector<uint16> w(size / 2);
	if (fio.Read(&w[0], int32(w.size() * 2)) != int32(w.size() * 2))
		return false;

	// 半周期の長さ (同じレベルが続くものはまとめる)
	std::vector<uint32> dur;
	std::vector<uint8> level;
	dur.reserve(w.size());
	level.reserve(w.size());
	for (size_t i = 0; i < w.size(); i++)
	{
		uint32 d;
		uint8 lv;
		if (w[i] == 0x0000)
			continue;
		if (w[i] == 0xffff)
		{
			if (++i >= w.size())
				break;
			d = uint32(w[i]) * 1000;
			lv = 2;
		}
		else
		{
			d = (w[i] & 0x7fff) + 2;
			lv = w[i] >> 15;
		}
		if (!d)
			continue;
		if (!dur.empty() && level.back() == lv)
			dur.back() += d;
		else
		{
			dur.push_back(d);
			level.push_back(lv);
		}
	}
	if (dur.empty())
		return false;

	// テープの速さを測る: 2400Hz の半周期が wvrunmin 以上続く所の平均の長さ / 本来の長さ。
	// 0.8～1.25 倍の外や、ばらつきが大きい所 (雑音、周波数の違う高速変調など) では測らない。
	// 短い所の値は、前の値から 5% 以内のときだけ使う。値は次に測れる所まで続く
	const double nominal = 1000000.0 / 4800;
	std::vector<size_t> spi;		// その値を使い始める半周期
	std::vector<double> spr;		// 遅さ (1 = 正しい速さ)
	for (size_t k = 0; k < dur.size(); )
	{
		if (level[k] == 2 || dur[k] >= wvthresh)
		{
			k++;
			continue;
		}
		size_t e = k;
		while (e < dur.size() && level[e] != 2 && dur[e] < wvthresh)
			e++;
		size_t n = e - k;
		if (n >= wvrunmin)
		{
			// 両端の半周期は、前後の波とつながって長さが崩れていることがあるので使わない
			double sum = 0, sum2 = 0;
			for (size_t q = k + 1; q < e - 1; q++)
				sum += dur[q], sum2 += double(dur[q]) * dur[q];
			double m = sum / (n - 2);
			double v = sum2 / (n - 2) - m * m;
			double sd = sqrt(v > 0 ? v : 0) / m;
			double r = m / nominal;
			bool ok = r >= 0.8 && r <= 1.25 && sd < 0.15;
			if (ok && n < wvrunlong && (spr.empty() || fabs(r / spr.back() - 1) > 0.05))
				ok = false;
			if (ok)
			{
				spi.push_back(k);
				spr.push_back(r);
			}
		}
		k = e;
	}

	// 本来の速さの時刻にする
	std::vector<uint32> half(dur.size());
	{
		double t = 0;
		double r = spr.empty() ? 1.0 : spr[0];
		size_t si = 0;
		for (size_t k = 0; k < dur.size(); k++)
		{
			while (si < spi.size() && spi[si] <= k)
				r = spr[si++];
			t += dur[k] / r;
			if (t > 4.0e9)
			{
				half.resize(k);
				level.resize(k);
				break;
			}
			half[k] = uint32(t);
		}
	}
	if (half.empty())
		return false;

	// 復調
	std::vector<uint32> line, cd;
	bool ln = true, c = false;
	int run = 0;
	uint32 start = 0;
	for (size_t i = 0; i < half.size(); i++)
	{
		uint32 end = half[i];
		uint32 d = end - start;
		int cls;				// 1: 2400Hz 0: 1200Hz -1: 信号なし
		if (level[i] == 2 || d > wvnone)
			cls = -1;
		else
			cls = d < wvthresh ? 1 : 0;

		bool l = cls != 0;
		if (l != ln)
		{
			line.push_back(start);
			ln = l;
		}
		if (cls == 1)
		{
			if (!c && ++run >= wvcdon)
			{
				cd.push_back(end);
				c = true;
			}
		}
		else
		{
			run = 0;
			if (c)
			{
				cd.push_back(start + wvthresh);
				c = false;
			}
		}
		start = end;
	}

	CriticalSection::Lock lock(wvcs);
	wvhalf.swap(half);
	wvlevel.swap(level);
	wvline.swap(line);
	wvcd.swap(cd);
	wave = true;
	mode = T_END;
	return true;
}

void TapeManager::WaveClear()
{
	if (scheduler)
		scheduler->DelEvent(wvevent);
	wvevent = 0;
	CriticalSection::Lock lock(wvcs);
	wave = false;
	wvhalf.clear();
	wvlevel.clear();
	wvline.clear();
	wvcd.clear();
	wvpos = 0;
	rxsearch = 0;
	rxstart = ~0u;
	sndwvidx = 0;
}

// ---------------------------------------------------------------------------
//	波形: テープの上の時刻と、その時刻の信号
//
uint32 TapeManager::WaveNow()
{
	if (motor)
		return wvpos + uint32(scheduler->GetTime() - wvstart) * 10;
	return wvpos;
}

bool TapeManager::WaveLine(uint32 t)
{
	size_t n = std::upper_bound(wvline.begin(), wvline.end(), t) - wvline.begin();
	return !(n & 1);
}

bool TapeManager::WaveCarrier(uint32 t)
{
	size_t n = std::upper_bound(wvcd.begin(), wvcd.end(), t) - wvcd.begin();
	return !!(n & 1);
}

uint32 TapeManager::WaveNextFall(uint32 t)
{
	// wvline の偶数番目が 1 から 0 への切り替わり
	size_t n = std::upper_bound(wvline.begin(), wvline.end(), t) - wvline.begin();
	if (n & 1)
		n++;
	return n < wvline.size() ? wvline[n] : ~0u;
}

// 1 ビットの長さ: ポート 30h の bit4 (1 = 1200ボー、0 = 600ボー)
// (SIO のモード命令のクロックの倍率は見ない。ROM の設定のときの速さとする)
double TapeManager::WaveBit()
{
	return port30 & 0x10 ? 1000000.0 / 1200 : 1000000.0 / 600;
}

int TapeManager::WaveFrameBits()
{
	int databits = 5 + ((siomodebyte >> 2) & 3);
	int parity = (siomodebyte >> 4) & 1;
	return databits + parity + 1;
}

// ---------------------------------------------------------------------------
//	波形: 位置を変える (巻き戻し・状態の読み込み)
//
void TapeManager::WaveSeek(uint32 t)
{
	scheduler->DelEvent(wvevent), wvevent = 0;
	wvpos = t;
	wvstart = scheduler->GetTime();
	rxsearch = t;
	rxstart = ~0u;
	WaveSoundSync(true);
	if (motor)
		WaveSchedule();
	SoundState();
}

// ---------------------------------------------------------------------------
//	波形: 次のバイトを受け取る時刻にイベントを置く
//	8251 のように、1 から 0 に変わったところをスタートビットとし、1 ビットの半分の後も 0 なら
//	そこから 1 ビットごとに読む。最初のストップビットを読んだところで SIO に渡す
//
void TapeManager::WaveSchedule()
{
	scheduler->DelEvent(wvevent), wvevent = 0;
	if (!wave || !motor)
		return;
	if (rxstart == ~0u)
	{
		uint32 t = rxsearch;
		for (;;)
		{
			uint32 f = WaveNextFall(t);
			if (f == ~0u)
				return;					// テープの終わり
			uint32 mid = f + uint32(WaveBit() / 2);
			if (!WaveLine(mid))
			{
				rxstart = f;
				break;
			}
			t = mid;					// 短すぎる 0 は読み飛ばす
		}
	}
	double bit = WaveBit();
	uint32 due = rxstart + uint32(bit / 2 + bit * WaveFrameBits());
	int wait = int(due - WaveNow());
	wvevent = scheduler->AddEvent(Max(1, (wait + 9) / 10), this, STATIC_CAST(TimeFunc, &TapeManager::WaveTimer));
}

void IOCALL TapeManager::WaveTimer(uint)
{
	wvevent = 0;
	SoundFlush();
	if (rxstart == ~0u)
	{
		WaveSchedule();
		return;
	}
	double bit = WaveBit();
	int n = WaveFrameBits();
	uint32 last = rxstart + uint32(bit / 2 + bit * n);
	if (int(last - WaveNow()) > 10)
	{
		WaveSchedule();					// 設定が変わって、まだ読み終わっていない
		return;
	}
	uint bits = 0;
	for (int k = 1; k <= n; k++)
		bits |= uint(WaveLine(rxstart + uint32(bit / 2 + bit * k))) << (k - 1);
	if (IsCMT())
		bus->Out(pinput, bits | wvrawbits);
	rxsearch = last;
	rxstart = ~0u;
	WaveSchedule();
}

// ---------------------------------------------------------------------------
//	波形: 音の位置をテープの位置に合わせる (エミュレーションのスレッドから)
//
void TapeManager::WaveSoundSync(bool force)
{
	sndwvtarget = WaveNow();
	if (force || !sndwvreq)
		sndwvreq = force ? 2 : 1;
}

// ---------------------------------------------------------------------------
//	device description
//
const Device::Descriptor TapeManager::descriptor =
{
	TapeManager::indef, TapeManager::outdef
};

const Device::OutFuncPtr TapeManager::outdef[] =
{
	STATIC_CAST(Device::OutFuncPtr, &RequestData),
	STATIC_CAST(Device::OutFuncPtr, &Out30),
	STATIC_CAST(Device::OutFuncPtr, &TxData),
	STATIC_CAST(Device::OutFuncPtr, &SioMode),
};

const Device::InFuncPtr TapeManager::indef[] =
{
	STATIC_CAST(Device::InFuncPtr, &In40),
};
