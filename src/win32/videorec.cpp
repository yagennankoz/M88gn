// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	録画: 画面と音を MP4 (H.264 / AAC) に書く (Windows の Media Foundation)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	Media Foundation の関数は動的に読む (Windows の N エディションなどで無くても M88 は動くように)。
//	GUID は mfuuid.lib から。このファイルはプリコンパイル済みヘッダーを使わない (Media Foundation の
//	ヘッダーは Windows 7 以降の定義が要るため)
// ---------------------------------------------------------------------------

#define STRICT
#define WIN32_LEAN_AND_MEAN
#define WINVER			0x0601
#define _WIN32_WINNT	0x0601

#include <windows.h>
#include <process.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <string.h>
#include <stdio.h>
#include "videorec.h"
#include "status.h"

#pragma comment(lib, "mfuuid.lib")

VideoRecorder videorecorder;

// ---------------------------------------------------------------------------
//	Media Foundation の関数
//
typedef HRESULT (WINAPI* PFMFStartup)(ULONG, DWORD);
typedef HRESULT (WINAPI* PFMFShutdown)();
typedef HRESULT (WINAPI* PFMFCreateMediaType)(IMFMediaType**);
typedef HRESULT (WINAPI* PFMFCreateSample)(IMFSample**);
typedef HRESULT (WINAPI* PFMFCreateMemoryBuffer)(DWORD, IMFMediaBuffer**);
typedef HRESULT (WINAPI* PFMFCreateAttributes)(IMFAttributes**, UINT32);
typedef HRESULT (WINAPI* PFMFCreateSinkWriterFromURL)(LPCWSTR, IMFByteStream*, IMFAttributes*, IMFSinkWriter**);

static PFMFStartup pMFStartup;
static PFMFShutdown pMFShutdown;
static PFMFCreateMediaType pMFCreateMediaType;
static PFMFCreateSample pMFCreateSample;
static PFMFCreateMemoryBuffer pMFCreateMemoryBuffer;
static PFMFCreateAttributes pMFCreateAttributes;
static PFMFCreateSinkWriterFromURL pMFCreateSinkWriterFromURL;

static bool LoadMF()
{
	static int loaded = 0;		// 0: まだ 1: 使える -1: 使えない
	if (loaded)
		return loaded > 0;
	HMODULE plat = LoadLibraryA("mfplat.dll");
	HMODULE rw = LoadLibraryA("mfreadwrite.dll");
	if (plat && rw)
	{
		pMFStartup = (PFMFStartup) GetProcAddress(plat, "MFStartup");
		pMFShutdown = (PFMFShutdown) GetProcAddress(plat, "MFShutdown");
		pMFCreateMediaType = (PFMFCreateMediaType) GetProcAddress(plat, "MFCreateMediaType");
		pMFCreateSample = (PFMFCreateSample) GetProcAddress(plat, "MFCreateSample");
		pMFCreateMemoryBuffer = (PFMFCreateMemoryBuffer) GetProcAddress(plat, "MFCreateMemoryBuffer");
		pMFCreateAttributes = (PFMFCreateAttributes) GetProcAddress(plat, "MFCreateAttributes");
		pMFCreateSinkWriterFromURL = (PFMFCreateSinkWriterFromURL) GetProcAddress(rw, "MFCreateSinkWriterFromURL");
	}
	loaded = (pMFStartup && pMFShutdown && pMFCreateMediaType && pMFCreateSample && pMFCreateMemoryBuffer
			  && pMFCreateAttributes && pMFCreateSinkWriterFromURL) ? 1 : -1;
	return loaded > 0;
}

template <class T> static void SafeRelease(T** p)
{
	if (*p)
	{
		(*p)->Release();
		*p = 0;
	}
}

static HRESULT SetSize(IMFMediaType* t, const GUID& key, UINT32 a, UINT32 b)
{
	return t->SetUINT64(key, ((UINT64) a << 32) | b);
}

// ---------------------------------------------------------------------------
//	絵 (参照を数える)
//
struct VideoRecorder::Frame
{
	LONG refs;
	int size;
	unsigned char* data;
};

static VideoRecorder::Frame* NewFrame(int size);
static void AddRef(void* f);
static void Release(void* f);

// ---------------------------------------------------------------------------
//	構築・消滅
//
VideoRecorder::VideoRecorder()
: state(idle), mode(filtered), fps(60), samplesperframe(800), width(0), height(0), writer(0), vstream(0), astream(0), mfstarted(false),
  latest(0), abuf(0), alen(0), abase(0), atotal(0), nextframe(0), rphase(0), rprevl(0), rprevr(0), rhasprev(false),
  thread(0), wake(0), drained(0), head(0), tail(0), queued(0), quit(false), framesdone(0), lastshownsec(-1)
{
	path[0] = 0;
	error[0] = 0;
	InitializeCriticalSection(&cs);
}

VideoRecorder::~VideoRecorder()
{
	Stop();
	DeleteCriticalSection(&cs);
}

bool VideoRecorder::Prepare(const char* p, int m, int f)
{
	if (state != idle)
		return false;
	error[0] = 0;
	if (!LoadMF())
	{
		strcpy_s(error, sizeof(error), "Media Foundation が使えないため、録画できません");
		return false;
	}
	strncpy_s(path, sizeof(path), p, _TRUNCATE);
	mode = m;
	fps = (f == 30 || f == 20 || f == 15) ? f : 60;
	samplesperframe = arate / fps;
	width = height = 0;
	state = prepared;		// 描く部品が次に描いたときに Open を呼ぶ
	return true;
}

// ---------------------------------------------------------------------------
//	書き始める (エミュレーションのスレッド。描く部品が最初の絵の大きさで呼ぶ)
//
bool VideoRecorder::Open(int w, int h)
{
	if (state != prepared)
		return state == recording;
	width = w & ~1, height = h & ~1;		// H.264 は偶数の大きさ
	if (width < 16 || height < 16 || !OpenWriter())
	{
		CloseWriter();
		state = idle;
		statusdisplay.Show(100, 5000, "録画を始められません: %s", error[0] ? error : "形式の設定に失敗");
		return false;
	}
	abuf = new short[achunk * 2];
	alen = 0;
	abase = atotal = 0;
	nextframe = 0;
	rphase = 0;
	rhasprev = false;
	framesdone = 0;
	lastshownsec = -1;
	quit = false;
	head = tail = 0;
	queued = 0;
	wake = CreateEvent(0, FALSE, FALSE, 0);
	drained = CreateEvent(0, FALSE, FALSE, 0);
	unsigned id;
	thread = (HANDLE) _beginthreadex(0, 0, ThreadEntry, this, 0, &id);
	state = recording;
	statusdisplay.Show(101, 0, "録画中 [00:00] %s", path);
	return true;
}

bool VideoRecorder::OpenWriter()
{
	HRESULT hr = pMFStartup(MF_VERSION, MFSTARTUP_LITE);
	if (FAILED(hr))
	{
		strcpy_s(error, sizeof(error), "Media Foundation を初期化できません");
		return false;
	}
	mfstarted = true;

	WCHAR wpath[260];
	MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, 260);
	IMFAttributes* attr = 0;
	pMFCreateAttributes(&attr, 2);
	if (attr)
	{
		attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);	// GPU の圧縮を使えるなら使う
		attr->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
	}
	IMFSinkWriter* w = 0;
	hr = pMFCreateSinkWriterFromURL(wpath, 0, attr, &w);
	SafeRelease(&attr);
	if (FAILED(hr))
	{
		sprintf_s(error, sizeof(error), "ファイルを作れません (%08lx)", hr);
		return false;
	}
	writer = w;

	// 映像: H.264。ビットレートは 1 画素あたり (フィルター込みは細かい模様が多いので多め)
	int bps = int(double(width) * height * fps * (mode == filtered ? 0.16 : 0.08));
	if (bps > 60000000)
		bps = 60000000;
	IMFMediaType* t = 0;
	pMFCreateMediaType(&t);
	t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	t->SetUINT32(MF_MT_AVG_BITRATE, bps);
	t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	SetSize(t, MF_MT_FRAME_SIZE, width, height);
	SetSize(t, MF_MT_FRAME_RATE, fps, 1);
	SetSize(t, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	hr = w->AddStream(t, &vstream);
	SafeRelease(&t);
	if (FAILED(hr))
	{
		sprintf_s(error, sizeof(error), "H.264 の圧縮を使えません (%08lx)", hr);
		return false;
	}
	pMFCreateMediaType(&t);
	t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	t->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(width * 4));		// 上から下へ
	SetSize(t, MF_MT_FRAME_SIZE, width, height);
	SetSize(t, MF_MT_FRAME_RATE, fps, 1);
	SetSize(t, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	hr = w->SetInputMediaType(vstream, t, 0);
	SafeRelease(&t);
	if (FAILED(hr))
	{
		sprintf_s(error, sizeof(error), "絵の形式を設定できません (%08lx)", hr);
		return false;
	}

	// 音: AAC 48kHz ステレオ 192kbps
	pMFCreateMediaType(&t);
	t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	t->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
	t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	t->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, arate);
	t->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
	t->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
	hr = w->AddStream(t, &astream);
	SafeRelease(&t);
	if (FAILED(hr))
	{
		sprintf_s(error, sizeof(error), "AAC の圧縮を使えません (%08lx)", hr);
		return false;
	}
	pMFCreateMediaType(&t);
	t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	t->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	t->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, arate);
	t->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
	t->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
	t->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, arate * 4);
	hr = w->SetInputMediaType(astream, t, 0);
	SafeRelease(&t);
	if (FAILED(hr))
	{
		sprintf_s(error, sizeof(error), "音の形式を設定できません (%08lx)", hr);
		return false;
	}

	hr = w->BeginWriting();
	if (FAILED(hr))
	{
		sprintf_s(error, sizeof(error), "書き始められません (%08lx)", hr);
		return false;
	}
	return true;
}

void VideoRecorder::CloseWriter()
{
	IMFSinkWriter* w = (IMFSinkWriter*) writer;
	if (w)
	{
		w->Finalize();
		w->Release();
		writer = 0;
	}
	if (mfstarted)
	{
		pMFShutdown();
		mfstarted = false;
	}
}

// ---------------------------------------------------------------------------
//	書き終える
//
void VideoRecorder::Stop()
{
	if (state == prepared)
	{
		state = idle;
		return;
	}
	if (state != recording)
		return;
	state = idle;		// これ以降、絵と音を受け取らない
	EnterCriticalSection(&cs);
	// 残りの音を送る
	if (alen > 0)
	{
		Item* it = new Item;
		memset(it, 0, sizeof(*it));
		it->video = false;
		it->time = abase * 10000000LL / arate;
		it->duration = (long long) alen * 10000000LL / arate;
		it->audio = abuf;
		it->audiosamples = alen;
		abuf = 0;
		alen = 0;
		Push(it);
	}
	quit = true;
	LeaveCriticalSection(&cs);
	SetEvent(wake);
	WaitForSingleObject(thread, INFINITE);
	CloseHandle(thread), thread = 0;
	CloseHandle(wake), wake = 0;
	CloseHandle(drained), drained = 0;
	CloseWriter();
	delete[] abuf, abuf = 0;
	if (latest)
		Release(latest), latest = 0;
	int sec = int(framesdone / fps);
	statusdisplay.Show(101, 5000, "録画終了 [%.2d:%.2d] %s", sec / 60, sec % 60, path);
}

// ---------------------------------------------------------------------------
//	最新の絵
//
void VideoRecorder::SetFrame(const void* bgra, int pitch)
{
	if (state != recording)
		return;
	Frame* f = NewFrame(width * height * 4);
	const unsigned char* s = (const unsigned char*) bgra;
	for (int y = 0; y < height; y++)
		memcpy(f->data + y * width * 4, s + y * pitch, width * 4);
	EnterCriticalSection(&cs);
	Frame* old = latest;
	latest = f;
	LeaveCriticalSection(&cs);
	if (old)
		Release(old);
}

// ---------------------------------------------------------------------------
//	音: 合成のレートから 48kHz に (直線で補う)。1/60 秒の区切りを越えたら、その時の最新の絵を書く
//
void VideoRecorder::AddAudio(const int* stereo, int n, int rate)
{
	if (state != recording || !abuf || rate <= 0)
		return;
	double step = double(rate) / arate;		// 48kHz の 1 サンプルで進む元のサンプル
	EnterCriticalSection(&cs);
	if (state != recording || !abuf)
	{
		LeaveCriticalSection(&cs);
		return;
	}
	for (int i = 0; i < n; i++)
	{
		int l = stereo[i * 2], r = stereo[i * 2 + 1];
		if (!rhasprev)
		{
			rprevl = l, rprevr = r;
			rhasprev = true;
			continue;
		}
		// rprev と今のサンプルの間で、rphase の位置の値を作る
		while (rphase < 1.0)
		{
			int ol = int(rprevl + (l - rprevl) * rphase);
			int orr = int(rprevr + (r - rprevr) * rphase);
			abuf[alen * 2] = short(ol > 32767 ? 32767 : ol < -32768 ? -32768 : ol);
			abuf[alen * 2 + 1] = short(orr > 32767 ? 32767 : orr < -32768 ? -32768 : orr);
			alen++;
			atotal++;
			rphase += step;
			if (alen >= achunk)
			{
				Item* it = new Item;
				memset(it, 0, sizeof(*it));
				it->video = false;
				it->time = abase * 10000000LL / arate;
				it->duration = (long long) alen * 10000000LL / arate;
				it->audio = abuf;
				it->audiosamples = alen;
				Push(it);
				abase += alen;
				abuf = new short[achunk * 2];
				alen = 0;
			}
			EmitFrames();
		}
		rphase -= 1.0;
		rprevl = l, rprevr = r;
	}
	LeaveCriticalSection(&cs);

	// 圧縮が追いつかないときは待つ (メモリを使い切らないように)
	while (state == recording)
	{
		EnterCriticalSection(&cs);
		int q = queued;
		LeaveCriticalSection(&cs);
		if (q < 30)
			break;
		WaitForSingleObject(drained, 100);
	}
}

// 音の時間が 1/60 秒の区切りを越えたら、最新の絵をそのフレームとして送る (cs を取ってから呼ぶ)
void VideoRecorder::EmitFrames()
{
	while ((nextframe + 1) * samplesperframe <= atotal)
	{
		if (latest)
		{
			Item* it = new Item;
			memset(it, 0, sizeof(*it));
			it->video = true;
			it->time = nextframe * 10000000LL / fps;
			it->duration = 10000000LL / fps;
			it->frame = latest;
			AddRef(latest);
			Push(it);
			queued++;
		}
		nextframe++;
	}
}

void VideoRecorder::Push(Item* it)
{
	it->next = 0;
	if (tail)
		tail->next = it;
	else
		head = it;
	tail = it;
	SetEvent(wake);
}

// ---------------------------------------------------------------------------
//	圧縮のスレッド
//
unsigned __stdcall VideoRecorder::ThreadEntry(void* arg)
{
	CoInitializeEx(0, COINIT_MULTITHREADED);
	((VideoRecorder*) arg)->ThreadMain();
	CoUninitialize();
	return 0;
}

void VideoRecorder::ThreadMain()
{
	IMFSinkWriter* w = (IMFSinkWriter*) writer;
	for (;;)
	{
		EnterCriticalSection(&cs);
		Item* it = head;
		if (it)
		{
			head = it->next;
			if (!head)
				tail = 0;
		}
		bool done = quit && !it;
		LeaveCriticalSection(&cs);
		if (done)
			break;
		if (!it)
		{
			WaitForSingleObject(wake, 200);
			continue;
		}

		DWORD bytes = it->video ? DWORD(width * height * 4) : DWORD(it->audiosamples * 4);
		IMFMediaBuffer* buf = 0;
		IMFSample* smp = 0;
		if (SUCCEEDED(pMFCreateMemoryBuffer(bytes, &buf)))
		{
			BYTE* p = 0;
			if (SUCCEEDED(buf->Lock(&p, 0, 0)))
			{
				memcpy(p, it->video ? (const void*) it->frame->data : (const void*) it->audio, bytes);
				buf->Unlock();
				buf->SetCurrentLength(bytes);
				if (SUCCEEDED(pMFCreateSample(&smp)))
				{
					smp->AddBuffer(buf);
					smp->SetSampleTime(it->time);
					smp->SetSampleDuration(it->duration);
					w->WriteSample(it->video ? vstream : astream, smp);
				}
			}
		}
		SafeRelease(&smp);
		SafeRelease(&buf);

		if (it->video)
		{
			Release(it->frame);
			EnterCriticalSection(&cs);
			queued--;
			LeaveCriticalSection(&cs);
			SetEvent(drained);
			framesdone++;
			int sec = int(framesdone / fps);
			if (sec != lastshownsec)
			{
				lastshownsec = sec;
				statusdisplay.Show(101, 0, "録画中 [%.2d:%.2d] %s", sec / 60, sec % 60, path);
			}
		}
		else
			delete[] it->audio;
		delete it;
	}
}

// ---------------------------------------------------------------------------
//	絵の参照
//
static VideoRecorder::Frame* NewFrame(int size)
{
	VideoRecorder::Frame* f = new VideoRecorder::Frame;
	f->refs = 1;
	f->size = size;
	f->data = new unsigned char[size];
	return f;
}

static void AddRef(void* p)
{
	InterlockedIncrement(&((VideoRecorder::Frame*) p)->refs);
}

static void Release(void* p)
{
	VideoRecorder::Frame* f = (VideoRecorder::Frame*) p;
	if (InterlockedDecrement(&f->refs) == 0)
	{
		delete[] f->data;
		delete f;
	}
}
