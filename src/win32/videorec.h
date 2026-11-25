// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	録画: 画面と音を MP4 (H.264 / AAC) に書く (Windows の Media Foundation)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	時刻はエミュレーターの中の音の時間で付ける (音を受け取るたびに、1/60 秒の区切りを越えたら、
//	そのときの最新の絵を 1 フレームとして書く)。Burst mode などでも、できる動画は実機と同じ速さ。
//	絵 (SetFrame) と音 (AddAudio) はエミュレーションのスレッドから渡し、圧縮は別のスレッドで行う
// ---------------------------------------------------------------------------

#pragma once

class VideoRecorder
{
public:
	struct Frame;						// 絵 (参照を数える)
	enum Mode { filtered = 0, raw = 1 };	// 画面と同じ (CRT 風のフィルター込み) / フィルター無しの 2 倍

	VideoRecorder();
	~VideoRecorder();

	// 録画の用意 (ファイル名と形式)。書き始めるのは、描く部品が最初の絵の大きさを決めたとき (Open)
	bool Prepare(const char* path, int mode, int fps = 60);	// fps: 60・30・20・15 (48000 を割り切れる数)
	void Stop();							// 書き終える (UI のスレッドから)
	bool IsActive() const { return state != idle; }
	bool IsWaitingFirstFrame() const { return state == prepared; }
	int GetMode() const { return mode; }
	const char* GetPath() const { return path; }
	const char* GetError() const { return error; }

	// 描く部品から (エミュレーションのスレッド)
	bool Open(int width, int height);		// 絵の大きさが決まったら書き始める
	void SetFrame(const void* bgra, int pitch);		// 最新の絵 (幅 x 高さ、BGRA)
	int Width() const { return width; }
	int Height() const { return height; }

	// 音 (エミュレーションのスレッド)。合成のレートの 32bit ステレオ
	void AddAudio(const int* stereo, int samples, int rate);

private:
	struct Item;
	bool OpenWriter();
	void CloseWriter();
	static unsigned __stdcall ThreadEntry(void* arg);
	void ThreadMain();
	void Push(Item* it);
	void EmitFrames();

	enum State { idle, prepared, recording };
	volatile State state;
	int mode;
	char path[260];
	char error[256];
	int width, height;

	void* writer;				// IMFSinkWriter
	unsigned long vstream, astream;
	bool mfstarted;

	// 絵: 最新の絵
	Frame* latest;

	// 音: 48kHz にした音を溜めて、まとめて送る
	enum { arate = 48000, achunk = 4800 };
	int fps;					// フレームレート
	int samplesperframe;		// 1 フレームの音のサンプル (48kHz)
	short* abuf;
	int alen;
	long long abase;			// abuf の先頭の時刻 (48kHz のサンプル)
	long long atotal;			// 受け取った音の時間 (48kHz のサンプル)
	long long nextframe;		// 次に書くフレームの番号
	double rphase;				// 合成のレートから 48kHz への位置 (元のサンプル単位、0～1)
	int rprevl, rprevr;
	bool rhasprev;

	// 圧縮のスレッドへの受け渡し
	struct Item
	{
		Item* next;
		bool video;
		long long time;			// 100ns 単位
		long long duration;
		Frame* frame;
		short* audio;
		int audiosamples;
	};
	CRITICAL_SECTION cs;
	HANDLE thread;
	HANDLE wake;				// 項目が来た・終わる
	HANDLE drained;				// 項目が減った
	Item* head;
	Item* tail;
	int queued;					// 待っているフレームの数
	volatile bool quit;
	long long framesdone;		// 書いたフレームの数 (経過時間の表示)
	int lastshownsec;
};

extern VideoRecorder videorecorder;
