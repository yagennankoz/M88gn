// ---------------------------------------------------------------------------
//	M88gn - テープの窓
// ---------------------------------------------------------------------------

#include "headers.h"
#include "tapewin.h"
#include "wincore.h"
#include "misc.h"
#include "dpi.h"
#include "status.h"
#include "resource.h"
#include "uitheme.h"
#include <math.h>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED	0x02e0
#endif
#ifndef CLEARTYPE_QUALITY
#define CLEARTYPE_QUALITY	5
#endif
#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL	0x020a
#endif

static const char classname[] = "M88gn TapeWindow";

// 色 (M88gn): テーマが Windows default なら Windows の色、Dark・Beige ならテーマの色 (ペインのメニューの帯と同じ地)
static COLORREF TC(int index)
{
	if (Theme::Get() == Theme::system)
		return GetSysColor(index);
	const Theme::Colors& c = Theme::C();
	switch (index)
	{
	case COLOR_WINDOW:			return c.tabselbg;
	case COLOR_WINDOWTEXT:
	case COLOR_BTNTEXT:			return c.stripfg;
	case COLOR_GRAYTEXT:		return c.stripgray;
	case COLOR_3DSHADOW:		return Theme::Mix(c.tabselbg, c.stripfg, 70);
	case COLOR_BTNFACE:			return Theme::Mix(c.tabselbg, c.stripfg, 20);
	case COLOR_HIGHLIGHT:		return c.tabaccent;
	case COLOR_HIGHLIGHTTEXT:	return RGB(0xff, 0xff, 0xff);
	}
	return GetSysColor(index);
}

static const char* const buttonlabel[] =
{
	"Open", "Rewind", "Prev", "Next", "Rec",
};

// ---------------------------------------------------------------------------

TapeWindow::TapeWindow()
: hinst(0), owner(0), hwnd(0), core(0), tape(0),
  posx(0), posy(0), width(0), height(0), placed(false), visible(false), dpi(96),
  fnormal(0), fbig(0), fsmall(0), rowh(20), scroll(0),
  hoverbtn(-1), pressbtn(-1), hoverrow(-1), tracking(false),
  serial(~0u), shownsec(-1), current(-1),
  animating(false), animms(0), rate(0), accms(0), accdt(0), polltick(0), frametick(0)
{
	angle[0] = angle[1] = 0;
	memset(&rcfile, 0, sizeof(rcfile));
	memset(&rccas, 0, sizeof(rccas));
	memset(&rccounter, 0, sizeof(rccounter));
	memset(&rcbar, 0, sizeof(rcbar));
	memset(&rclist, 0, sizeof(rclist));
	memset(rcbtn, 0, sizeof(rcbtn));
	memset(&rcspeedlabel, 0, sizeof(rcspeedlabel));
	memset(&rcvollabel, 0, sizeof(rcvollabel));
	st.open = st.wave = st.motor = st.recording = st.recarm = st.writeprotect = st.cancue = false;
	st.speed = 1;
	st.volume = 100;
	st.ms = st.length = 0;
}

TapeWindow::~TapeWindow()
{
	if (hwnd)
		DestroyWindow(hwnd);
}

bool TapeWindow::Init(HINSTANCE hi, HWND own, WinCore* c)
{
	hinst = hi;
	owner = own;
	core = c;

	WNDCLASS wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = WndProcGate;
	wc.hInstance = hinst;
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = 0;
	wc.lpszClassName = classname;
	RegisterClass(&wc);			// 2 回目以降は失敗するが、登録済みなので構わない
	return true;
}

void TapeWindow::SetPlacement(int x, int y, int w, int h)
{
	posx = x, posy = y, width = w, height = h;
	placed = w > 0 && h > 0;
}

void TapeWindow::GetPlacement(int* x, int* y, int* w, int* h)
{
	if (hwnd && !docked)		// ペインに入っている間は浮かせたときの位置・大きさのまま
	{
		RECT r;
		GetWindowRect(hwnd, &r);
		posx = r.left, posy = r.top, width = r.right - r.left, height = r.bottom - r.top;
	}
	*x = posx, *y = posy, *w = width, *h = height;
}

// ---------------------------------------------------------------------------
//	窓を作る (初めて表示するとき)
//
void TapeWindow::Create()
{
	const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VSCROLL;
	const DWORD exstyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

	int x = posx, y = posy, w = width, h = height;
	if (placed)
	{
		// 保存した位置で窓の真ん中がどの画面にも無ければ (画面の構成が変わった)、既定の位置にする
		// (デバッグの窓・ソフトウェアキーボードと同じ決め方)
		POINT c = { x + w / 2, y + h / 2 };
		if (!MonitorFromPoint(c, MONITOR_DEFAULTTONULL))
			placed = false;
		// 前の版で保存した大きさが、今の最小より小さければ広げる (速さ・音量の段を足した)
		int d = Dpi::GetForWindow(owner);
		w = Max(w, MulDiv(360, d, 96));
		h = Max(h, MulDiv(410, d, 96));
	}
	if (!placed)
	{
		// 既定: M88 の窓の右。画面からはみ出すなら、画面の右の端に寄せる
		int d = Dpi::GetForWindow(owner);
		w = MulDiv(420, d, 96), h = MulDiv(540, d, 96);
		RECT ro;
		GetWindowRect(owner, &ro);
		x = ro.right, y = ro.top;
		MONITORINFO mi;
		memset(&mi, 0, sizeof(mi));
		mi.cbSize = sizeof(mi);
		GetMonitorInfo(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
		if (x + w > mi.rcWork.right)
			x = mi.rcWork.right - w;
		if (y + h > mi.rcWork.bottom)
			y = mi.rcWork.bottom - h;
	}
	hwnd = CreateWindowEx(exstyle, classname, "Tape", style,
						  x, y, w, h, owner, 0, hinst, this);
}

// 浮かせたときの位置・大きさ (ペインから出すとき。M88gn)
void TapeWindow::DockFloatRect(RECT* r)
{
	if (placed)
		SetRect(r, posx, posy, posx + width, posy + height);
	else
		SetRectEmpty(r);
}

// 閉じるときは窓を壊す (ペインに入っていても、DockHost が閉じたと分かるように。M88gn)
void TapeWindow::Show(bool show)
{
	visible = show;
	if (!show)
	{
		if (hwnd)
			DestroyWindow(hwnd);
		return;
	}
	bool created = false;
	if (!hwnd)
		Create(), created = true;
	if (!hwnd)
		return;
	Refresh(true);
	SetTimer(hwnd, timerid, interval, 0);
	ShowWindow(hwnd, SW_SHOWNOACTIVATE);
	if (created)
		DockShown();		// 前にペインにあったなら、そのペインへ
}

// ---------------------------------------------------------------------------
//	テープの状態を読む (エミュレーションを止めて)。変わっていたら描き直す
//
void TapeWindow::Refresh(bool force)
{
	if (!hwnd || !tape || !core)
		return;
	State s;
	bool listchanged = false;
	core->Lock();
	s.open = tape->IsOpen();
	s.wave = tape->IsWave();
	s.motor = tape->IsMotor();
	s.recording = tape->IsRecording();
	s.recarm = tape->IsRecord();
	s.writeprotect = tape->IsWriteProtected();
	s.cancue = tape->CanCue();
	s.speed = tape->GetSpeed();
	s.volume = tape->GetVolume();
	s.ms = tape->GetTimeMs();
	s.file = tape->GetFileName();
	if (serial != tape->GetSerial())
	{
		serial = tape->GetSerial();
		tape->GetCueList(cues);
		listchanged = true;
	}
	s.length = tape->GetLengthMs();		// 録音中は伸びる
	core->Unlock();

	// テープの位置の進み方 (カセットの絵のリールを回す)。頭出しなどで飛んだときは前の値のまま
	DWORD now = GetTickCount();
	// 位置はまとめて進むことがある (読むたびには進まない) ので、1 秒分を平均する。
	// モーターが回っている間は、少なくとも等速で回す (実物のデッキと同じ)
	if (s.motor && st.motor && !listchanged)
	{
		double d = double(s.ms) - double(st.ms);
		if (d >= 0 && d < 60000)
			accms += d;
		accdt += double(now - polltick);
		if (accdt >= 1000)
		{
			rate = accms / accdt;
			rate = rate < 1 ? 1 : rate > 12 ? 12 : rate;
			accms = accdt = 0;
		}
	}
	else
	{
		rate = s.motor ? 1.0 : 0.0;
		accms = accdt = 0;
	}
	polltick = now;
	animms = s.ms;

	bool changed = force || listchanged
		|| s.open != st.open || s.motor != st.motor || s.recording != st.recording || s.recarm != st.recarm
		|| s.writeprotect != st.writeprotect || s.cancue != st.cancue || s.speed != st.speed || s.volume != st.volume || s.file != st.file
		|| s.length / 1000 != st.length / 1000;
	int barw = rcbar.right - rcbar.left;
	int oldpx = st.length ? int((unsigned __int64)(Min(st.ms, st.length)) * barw / st.length) : 0;
	int newpx = s.length ? int((unsigned __int64)(Min(s.ms, s.length)) * barw / s.length) : 0;
	if (int(s.ms / 1000) != shownsec || oldpx != newpx)
		changed = true;
	st = s;

	int cur = CurrentRow();
	if (cur != current)
	{
		changed = true;
		// 今の行が見えるようにする
		int rows = Max(1, int((rclist.bottom - rclist.top) / rowh));
		if (cur >= 0 && (cur < scroll || cur >= scroll + rows))
			scroll = Max(0, cur - rows / 2);
	}
	if (listchanged)
	{
		if (scroll >= int(cues.size()))
			scroll = 0;
		UpdateScroll();
	}
	if (changed)
		InvalidateRect(hwnd, 0, false);
	SetAnimate(st.open && st.motor);
}

void TapeWindow::SetAnimate(bool on)
{
	if (!hwnd || on == animating)
		return;
	animating = on;
	if (on)
	{
		frametick = GetTickCount();
		SetTimer(hwnd, animid, animinterval, 0);
	}
	else
		KillTimer(hwnd, animid);
}

// ---------------------------------------------------------------------------
//	カセットの絵 (M88gn)
//	寸法は実物 (100.4 x 63.8mm) の mm で決め、4 倍の大きさで描いてから縮める (縁を滑らかにする)。
//	テープの巻きとリールの回り方も実物の寸法で求める:
//	  巻きの半径 = √(ハブの半径^2 + 巻いた長さ × テープの厚さ / π)
//	  リールの回る速さ = テープの送りの速さ (47.6mm/秒) / 巻きの半径
//	テープの片面の長さは、T88 の長さが収まる規格の長さ (C-30 など) の半分とする
//
static const double caswidth = 100.4, casheight = 63.8;
static const double reelx[2] = { 50.2 - 21.25, 50.2 + 21.25 }, reely = 28.6;	// ハブの中心 (間は 42.5mm)
static const double hubreal = 11.0;				// ハブの半径 (テープを巻く所。直径約 22mm)
static const double tapemmps = 47.6;			// テープの送りの速さ (mm/秒)

// misc.h の Min / Max は int 用 (小数を渡すと切り捨てる) なので、小数はこちら
static inline double DMin(double a, double b) { return a < b ? a : b; }
static inline double DMax(double a, double b) { return a > b ? a : b; }

//	規格の長さの呼び方 (C-46 など。両面の分数)。片面の長さが収まる、いちばん短いもの
static int CassetteC(uint lengthms)
{
	static const int c[] = { 10, 15, 20, 46, 60, 90, 120 };	// (C-30 は入れない。ユーザーの選んだ並び)
	int side = int((lengthms + 59999) / 60000);
	for (int i = 0; i < int(sizeof(c) / sizeof(c[0])); i++)
	{
		if (c[i] / 2 >= side)
			return c[i];
	}
	return (side + 4) / 5 * 10;
}

//	テープの厚さ (mm。長いテープほど薄い)
static double TapeThick(int c)
{
	return c >= 120 ? 0.009 : c >= 90 ? 0.012 : 0.018;
}

//	左右の巻きの半径 (mm)。ms: テープの位置、lengthms: T88 の長さ
static void PackRadii(double ms, uint lengthms, double r[2])
{
	int c = CassetteC(lengthms);
	double side = c / 2 * 60.0 * tapemmps;			// 片面の長さ (mm)
	double right = DMin(DMax(ms / 1000 * tapemmps, 0.0), side);
	double t = TapeThick(c);
	r[0] = sqrt(hubreal * hubreal + (side - right) * t / 3.14159265358979);
	r[1] = sqrt(hubreal * hubreal + right * t / 3.14159265358979);
}

void TapeWindow::Animate()
{
	DWORD now = GetTickCount();
	double dt = double(now - frametick);
	frametick = now;
	if (dt > 200)
		dt = 200;
	double dms = rate * dt;
	if (dms <= 0)
		return;
	animms += dms;
	double r[2];
	PackRadii(animms, st.length, r);
	double dmm = dms / 1000 * tapemmps;
	// 再生では、テープは左から右へ送られ、どちらのリールも左回り。
	// 1 コマで回る角度が大きいと (歯は 60 度ごと) 逆に回って見えるので、0.45 (約 26 度) までにする
	// (等速なら、巻きの無いリールでも 1 コマ約 0.14)
	for (int i = 0; i < 2; i++)
		angle[i] -= DMin(dmm / r[i], 0.45);
	InvalidateRect(hwnd, &rccas, false);
}

//	ラベルのファイル名の折り返し: 字の幅と、単語の区切り (e の前の字が空白・_・-・. などの後で切れる)
static int TextWidth(HDC hdc, const std::string& s)
{
	SIZE sz = { 0, 0 };
	GetTextExtentPoint32(hdc, s.c_str(), int(s.size()), &sz);
	return sz.cx;
}

static bool IsWordBreak(const std::string& s, size_t e)
{
	if (e == 0 || e >= s.size())
		return false;
	char c = s[e - 1];			// (呼ぶ側で、1 バイトの字の後だけにする)
	return c == ' ' || c == '_' || c == '-' || c == '.' || c == ',' || c == ')' || c == ']'
		|| (s[e] == '(' || s[e] == '[');
}

void TapeWindow::DrawCassette(HDC hdc, const RECT& r)
{
	int w = r.right - r.left, h = r.bottom - r.top;
	if (w <= 0 || h <= 0)
		return;
	const int ss = 4;
	int W = w * ss, H = h * ss;
	BITMAPINFO bi;
	memset(&bi, 0, sizeof(bi));
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = W;
	bi.bmiHeader.biHeight = -H;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	void* bits = 0;
	HBITMAP dib = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, 0, 0);
	if (!dib)
		return;
	HDC dc = CreateCompatibleDC(hdc);
	HGDIOBJ olddib = SelectObject(dc, dib);
	HGDIOBJ oldpen = SelectObject(dc, GetStockObject(NULL_PEN));

	RECT all = { 0, 0, W, H };
	HBRUSH bgb = CreateSolidBrush(TC(COLOR_WINDOW));
	FillRect(dc, &all, bgb);
	DeleteObject(bgb);

	double k = W / caswidth;			// mm あたりの (4 倍の) ピクセル
	#define CX(v)	int((v) * k + 0.5)

	struct Fill
	{
		HDC dc;
		HBRUSH b;
		Fill(HDC d, COLORREF c) : dc(d) { b = CreateSolidBrush(c); SelectObject(dc, b); }
		~Fill() { SelectObject(dc, GetStockObject(NULL_BRUSH)); DeleteObject(b); }
	};

	const COLORREF tapec = RGB(70, 48, 36);
	bool rec = st.recording || st.recarm;
	// 窓
	const double wl = 21.0, wt = 21.4, wr = caswidth - 21.0, wb = 35.8;
	const double holer = 5.7;											// ハブの見える丸い穴
	const double cwl = 40.4, cwt = 23.2, cwr = caswidth - 40.4, cwb = 34.0;	// 真ん中の透明な窓
	if (!st.open)
	{
		// テープが無い: 差し込み口の枠だけ
		HPEN pen = CreatePen(PS_SOLID, ss * Max(1, S(1)), TC(COLOR_3DSHADOW));
		SelectObject(dc, pen);
		SelectObject(dc, GetStockObject(NULL_BRUSH));
		RoundRect(dc, CX(1), CX(1), CX(caswidth - 1), CX(casheight - 1), CX(5), CX(5));
		SelectObject(dc, GetStockObject(NULL_PEN));
		DeleteObject(pen);
	}
	else
	{
		// 黒いシェル (縁を少し明るくして立体に見せる)
		{ Fill f(dc, RGB(72, 72, 78)); RoundRect(dc, 0, 0, CX(caswidth), CX(casheight), CX(6), CX(6)); }
		{ Fill f(dc, RGB(26, 26, 29)); RoundRect(dc, CX(0.7), CX(0.7), CX(caswidth - 0.7), CX(casheight - 0.7), CX(5), CX(5)); }

		// 下の滑り止めの筋 (左右)
		{
			Fill f(dc, RGB(44, 44, 48));
			for (double x = 6.0; x < 15.5; x += 1.3)
			{
				Rectangle(dc, CX(x), CX(47.5), CX(x + 0.55), CX(59.5));
				Rectangle(dc, CX(caswidth - x - 0.55), CX(47.5), CX(caswidth - x), CX(59.5));
			}
		}

		// ラベル
		{ Fill f(dc, RGB(245, 245, 247)); RoundRect(dc, CX(5.4), CX(3.0), CX(caswidth - 5.4), CX(44.6), CX(1.6), CX(1.6)); }
		// A 面の印 (短い上向きの矢印と、黒く塗った四角。A は白抜きで後で描く)
		{
			Fill f(dc, RGB(20, 20, 24));
			POINT tri[3] = { { CX(12.6), CX(8.3) }, { CX(10.9), CX(10.2) }, { CX(14.3), CX(10.2) } };
			Polygon(dc, tri, 3);
			Rectangle(dc, CX(12.3), CX(10.0), CX(12.9), CX(11.4));
			RoundRect(dc, CX(10.0), CX(11.4), CX(15.2), CX(17.1), CX(1.0), CX(1.0));
		}
		// 書き込みの線
		{
			Fill f(dc, RGB(160, 162, 172));
			Rectangle(dc, CX(17.5), CX(14.8), CX(caswidth - 7.5), CX(15.05));
			Rectangle(dc, CX(17.5), CX(19.4), CX(caswidth - 7.5), CX(19.65));
		}
		// 窓 (黒い枠に、ハブの丸い穴と真ん中の透明な窓)
		{ Fill f(dc, RGB(12, 12, 14)); RoundRect(dc, CX(wl), CX(wt), CX(wr), CX(wb), CX(6), CX(6)); }
		// 真ん中の窓: 奥は明るい灰色、テープの巻きの縁が見える
		{
			HRGN clip = CreateRectRgn(CX(cwl), CX(cwt), CX(cwr), CX(cwb));
			{ Fill f(dc, RGB(150, 152, 158)); Rectangle(dc, CX(cwl), CX(cwt), CX(cwr), CX(cwb)); }
			SelectClipRgn(dc, clip);
			double pr[2];
			PackRadii(animms, st.length, pr);
			for (int i = 0; i < 2; i++)
			{
				Fill f(dc, tapec);
				Ellipse(dc, CX(reelx[i] - pr[i]), CX(reely - pr[i]), CX(reelx[i] + pr[i]), CX(reely + pr[i]));
			}
			// 目盛り
			{
				Fill f(dc, RGB(235, 235, 235));
				for (int n = 0; n < 3; n++)
				{
					double x = (cwl + cwr) / 2 + (n - 1) * 4.0;
					Rectangle(dc, CX(x), CX(cwt), CX(x + 0.25), CX(cwt + 1.4));
				}
			}
			SelectClipRgn(dc, 0);
			DeleteObject(clip);
		}
		// ハブ (穴いっぱいに見える。内側に 6 つの歯)
		for (int i = 0; i < 2; i++)
		{
			double cx = reelx[i], cy = reely;
			{ Fill f(dc, RGB(150, 152, 158)); Ellipse(dc, CX(cx - holer), CX(cy - holer), CX(cx + holer), CX(cy + holer)); }
			{ Fill f(dc, RGB(208, 210, 214)); Ellipse(dc, CX(cx - holer + 0.5), CX(cy - holer + 0.5), CX(cx + holer - 0.5), CX(cy + holer - 0.5)); }
			{ Fill f(dc, RGB(230, 232, 235)); Ellipse(dc, CX(cx - 3.9), CX(cy - 3.9), CX(cx + 3.9), CX(cy + 3.9)); }
			const double hole = 3.2;
			{ Fill f(dc, RGB(38, 38, 44)); Ellipse(dc, CX(cx - hole), CX(cy - hole), CX(cx + hole), CX(cy + hole)); }
			Fill f(dc, RGB(230, 232, 235));
			for (int t = 0; t < 6; t++)
			{
				double a = angle[i] + t * 3.14159265358979 / 3;
				double ux = cos(a), uy = sin(a), vx = -uy, vy = ux;
				double r1 = hole + 0.3, r2 = 2.1, hw = 0.5;
				POINT pt[4] =
				{
					{ CX(cx + ux * r1 + vx * hw), CX(cy + uy * r1 + vy * hw) },
					{ CX(cx + ux * r2 + vx * hw), CX(cy + uy * r2 + vy * hw) },
					{ CX(cx + ux * r2 - vx * hw), CX(cy + uy * r2 - vy * hw) },
					{ CX(cx + ux * r1 - vx * hw), CX(cy + uy * r1 - vy * hw) },
				};
				Polygon(dc, pt, 4);
			}
		}

		// 下の台形 (ヘッド・ピンチローラーの入る穴と、そこに見えるテープ)
		{
			Fill f(dc, RGB(36, 36, 40));
			POINT pt[4] = { { CX(17), CX(casheight) }, { CX(21.5), CX(48.4) }, { CX(caswidth - 21.5), CX(48.4) }, { CX(caswidth - 17), CX(casheight) } };
			Polygon(dc, pt, 4);
		}
		{
			Fill f(dc, RGB(6, 6, 8));
			Rectangle(dc, CX(25), CX(59), CX(30), CX(casheight));
			Rectangle(dc, CX(44.5), CX(57.5), CX(caswidth - 44.5), CX(casheight));
			Rectangle(dc, CX(caswidth - 30), CX(59), CX(caswidth - 25), CX(casheight));
			Ellipse(dc, CX(33.5 - 1.5), CX(54.6 - 1.5), CX(33.5 + 1.5), CX(54.6 + 1.5));
			Ellipse(dc, CX(caswidth - 33.5 - 1.5), CX(54.6 - 1.5), CX(caswidth - 33.5 + 1.5), CX(54.6 + 1.5));
			Rectangle(dc, CX(19.0), CX(52.0), CX(21.4), CX(54.4));
			Rectangle(dc, CX(caswidth - 21.4), CX(52.0), CX(caswidth - 19.0), CX(54.4));
		}
		{
			HRGN o = CreateRectRgn(CX(25), CX(59), CX(30), CX(casheight));
			HRGN c = CreateRectRgn(CX(44.5), CX(57.5), CX(caswidth - 44.5), CX(casheight));
			CombineRgn(o, o, c, RGN_OR);
			DeleteObject(c);
			c = CreateRectRgn(CX(caswidth - 30), CX(59), CX(caswidth - 25), CX(casheight));
			CombineRgn(o, o, c, RGN_OR);
			DeleteObject(c);
			SelectClipRgn(dc, o);
			Fill f(dc, tapec);
			Rectangle(dc, CX(22), CX(60.4), CX(caswidth - 22), CX(61.8));
			SelectClipRgn(dc, 0);
			DeleteObject(o);
		}
		// ねじ
		{
			const double sx[5] = { 3.2, caswidth - 3.2, 3.2, caswidth - 3.2, caswidth / 2 };
			const double sy[5] = { 3.2, 3.2, casheight - 3.2, casheight - 3.2, 52.6 };
			for (int i = 0; i < 5; i++)
			{
				{ Fill f(dc, RGB(90, 90, 96)); Ellipse(dc, CX(sx[i] - 1.7), CX(sy[i] - 1.7), CX(sx[i] + 1.7), CX(sy[i] + 1.7)); }
				{ Fill f(dc, RGB(14, 14, 16)); Ellipse(dc, CX(sx[i] - 1.2), CX(sy[i] - 1.2), CX(sx[i] + 1.2), CX(sy[i] + 1.2)); }
			}
		}
	}
	#undef CX
	SelectObject(dc, oldpen);

	SetStretchBltMode(hdc, HALFTONE);
	SetBrushOrgEx(hdc, 0, 0, 0);
	StretchBlt(hdc, r.left, r.top, w, h, dc, 0, 0, W, H, SRCCOPY);
	SelectObject(dc, olddib);
	DeleteDC(dc);
	DeleteObject(dib);

	if (!st.open)
		return;

	// ラベルの字 (縮めた後に描く)。mm の位置をこの窓のピクセルに直す。小さすぎて読めない字は描かない
	double k1 = w / caswidth;
	#define PX(v)	int((v) * k1 + 0.5)
	struct Text
	{
		static void Draw(HDC hdc, const wchar_t* s, const RECT& rc, double mm, double k1, int weight, COLORREF c, UINT fmt, const wchar_t* face = L"Arial")
		{
			int ht = int(mm * k1 + 0.5);
			if (ht < 6)
				return;
			HFONT f = CreateFontW(-ht, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);
			HGDIOBJ of = SelectObject(hdc, f);
			SetTextColor(hdc, c);
			DrawTextW(hdc, s, -1, (RECT*) &rc, fmt | DT_SINGLELINE | DT_NOPREFIX);
			SelectObject(hdc, of);
			DeleteObject(f);
		}
	};
	int x0 = r.left, y0 = r.top;
	RECT t;
	const COLORREF ink = RGB(30, 30, 38);

	// A 面 (黒い四角に白抜きの太字。角の丸い書体があればそれで)
	{
		SetRect(&t, x0 + PX(10.0), y0 + PX(11.4), x0 + PX(15.2), y0 + PX(17.1));
		// 角の丸い書体: Arial Rounded MT Bold (Office に付く)、HG 丸ゴシック M-PRO (同じく)、無ければ Arial
		static const wchar_t* const faces[] = { L"Arial Rounded MT Bold", L"HG\x4e38\xff7a\xff9e\xff7c\xff6f\xff78M-PRO", L"Arial" };
		static int face = -1;
		if (face < 0)
		{
			for (face = 0; face < 2; face++)
			{
				HFONT f = CreateFontW(-20, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, faces[face]);
				HGDIOBJ of = SelectObject(hdc, f);
				wchar_t name[LF_FACESIZE];
				GetTextFaceW(hdc, LF_FACESIZE, name);
				SelectObject(hdc, of);
				DeleteObject(f);
				if (!lstrcmpiW(name, faces[face]))
					break;
			}
		}
		Text::Draw(hdc, L"A", t, 4.4, k1, FW_BOLD, RGB(255, 255, 255), DT_CENTER | DT_VCENTER, faces[face]);
	}
	// EQ (A の下)
	SetRect(&t, x0 + PX(9.6), y0 + PX(19.2), x0 + PX(21), y0 + PX(22.0));
	Text::Draw(hdc, L"EQ 120\x03bcs", t, 2.2, k1, FW_NORMAL, ink, DT_LEFT | DT_VCENTER);
	// ファイル名 (1 本目の書き込みの線の上。ラベルの右の端まで)
	{
		std::string name = st.file;
		size_t sl = name.find_last_of("\\/");
		if (sl != std::string::npos)
			name = name.substr(sl + 1);
		size_t dot = name.find_last_of('.');
		if (dot != std::string::npos && dot > 0)
			name = name.substr(0, dot);
		// 1 行で入らなければ、単語の区切りで 2 本目の線に折り返す。2 行でも入らなければ字を小さくする。
		// 一番小さい字でも単語の区切りでは入らなければ字の途中で折り返し、それでも入らなければ末尾を省く
		RECT l1, l2;			// 1 行目・2 行目 (字の下をそれぞれ 1 本目・2 本目の線に合わせる)
		SetRect(&l1, x0 + PX(17.5), y0 + PX(8.0), x0 + PX(caswidth - 8.0), y0 + PX(15.1));
		SetRect(&l2, x0 + PX(17.5), y0 + PX(12.8), x0 + PX(caswidth - 8.0), y0 + PX(19.7));		// (0.5mm 下げた。175% で約 2 ピクセル)
		int avail = l1.right - l1.left;
		const int maxht = Max(PX(5.2), 8), minht = Max(PX(3.0), 7);
		// 2 行にするときの字の高さの上限。日本語の字は背が高いので、2 本の線の間 (4.6mm) に収まる 4.0mm まで
		bool dbcs = false;
		for (size_t k = 0; k < name.size(); k++)
			if (IsDBCSLeadByte(uint8(name[k])))
				dbcs = true;
		const int twoht = dbcs ? Max(PX(4.0), minht) : maxht;
		std::string line1 = name, line2;
		HFONT f = 0;
		HGDIOBJ of = 0;
		for (int ht = maxht; ; ht = Max(minht, ht * 9 / 10))
		{
			if (f)
			{
				SelectObject(hdc, of);
				DeleteObject(f);
			}
			f = CreateFont(-ht, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
			of = SelectObject(hdc, f);
			if (TextWidth(hdc, name) <= avail)
			{
				line1 = name, line2.clear();
				break;
			}
			if (ht > twoht)
				continue;			// 2 行にするには字が大きすぎる
			// 単語の区切り (一番小さい字なら、字の途中も) で、1 行目を一番長くとれる所
			bool last = ht <= minht;
			bool found = false;
			for (int pass = 0; pass < (last ? 2 : 1) && !found; pass++)
			{
				size_t best = 0;
				for (size_t k = 0; k < name.size(); )
				{
					size_t n = IsDBCSLeadByte(uint8(name[k])) && k + 1 < name.size() ? 2 : 1;
					size_t e = k + n;			// ここで切ると、1 行目は name[0, e)
					bool brk = pass ? true : (n == 1 && IsWordBreak(name, e));		// 2 バイト文字の 2 バイト目は区切りの記号と同じ値のことがある
					if (e < name.size() && brk && TextWidth(hdc, name.substr(0, e)) <= avail)
						best = e;
					k = e;
				}
				if (best)
				{
					line1 = name.substr(0, best);
					line2 = name.substr(best);
					while (!line2.empty() && line2[0] == ' ')
						line2.erase(0, 1);
					found = TextWidth(hdc, line2) <= avail || last;
				}
			}
			if (found || last)
			{
				if (!found)
					line1 = name, line2.clear();		// 区切れない: 1 行で末尾を省く
				break;
			}
		}
		SetTextColor(hdc, RGB(16, 26, 96));
		DrawText(hdc, line1.c_str(), -1, &l1, DT_SINGLELINE | DT_BOTTOM | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
		if (!line2.empty())
			DrawText(hdc, line2.c_str(), -1, &l2, DT_SINGLELINE | DT_BOTTOM | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
		SelectObject(hdc, of);
		DeleteObject(f);
	}

	// 長さ (C-30 など。帯の右)
	{
		wchar_t num[16];
		wsprintfW(num, L"%d", CassetteC(st.length));
		int ht = PX(5.6);
		if (ht >= 6)
		{
			HFONT f = CreateFontW(-ht, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Arial");
			HGDIOBJ of = SelectObject(hdc, f);
			SIZE s1, s2;
			GetTextExtentPoint32W(hdc, L"C-", 2, &s1);
			GetTextExtentPoint32W(hdc, num, lstrlenW(num), &s2);
			TEXTMETRICW tm;
			GetTextMetricsW(hdc, &tm);
			int x = x0 + PX(34.0);
			int y = y0 + PX(40.6) - tm.tmAscent + PX(1.2);	// 帯の下の辺のあたりに字の下の線
			// 帯 (青黒、録音中は赤。字の左右)
			HBRUSH bb = CreateSolidBrush(rec ? RGB(160, 24, 24) : RGB(18, 26, 58));
			int gapx = PX(1.4);
			RECT b1 = { x0 + PX(5.4), y0 + PX(39.4), x - gapx, y0 + PX(41.8) };
			RECT b2 = { x + s1.cx + s2.cx + gapx, y0 + PX(39.4), x0 + PX(caswidth - 5.4), y0 + PX(41.8) };
			FillRect(hdc, &b1, bb);
			FillRect(hdc, &b2, bb);
			DeleteObject(bb);
			SetTextColor(hdc, ink);
			TextOutW(hdc, x, y, L"C-", 2);
			SetTextColor(hdc, rec ? RGB(215, 40, 40) : RGB(40, 160, 90));
			TextOutW(hdc, x + s1.cx, y, num, lstrlenW(num));
			SelectObject(hdc, of);
			DeleteObject(f);
		}
	}
	#undef PX
}

int TapeWindow::CurrentRow()
{
	if (!st.open)
		return -1;
	int r = -1;
	for (size_t i = 0; i < cues.size(); i++)
	{
		if (cues[i].ms <= st.ms)
			r = int(i);
	}
	return r;
}

// ---------------------------------------------------------------------------
//	字と並び (DPI が変わったら作り直す)
//
void TapeWindow::MakeFonts()
{
	if (fnormal) DeleteObject(fnormal);
	if (fbig) DeleteObject(fbig);
	if (fsmall) DeleteObject(fsmall);
	NONCLIENTMETRICSW ncm;
	Dpi::GetNonClientMetrics(&ncm, dpi);
	LOGFONTW lf = ncm.lfMessageFont;
	fnormal = CreateFontIndirectW(&lf);
	lf.lfHeight = lf.lfHeight * 9 / 10;
	fsmall = CreateFontIndirectW(&lf);
	lf = ncm.lfMessageFont;
	lf.lfHeight = -S(30);
	lf.lfWeight = FW_SEMIBOLD;
	lstrcpyW(lf.lfFaceName, L"Consolas");
	fbig = CreateFontIndirectW(&lf);

	HDC hdc = GetDC(hwnd);
	HGDIOBJ old = SelectObject(hdc, fnormal);
	TEXTMETRIC tm;
	GetTextMetrics(hdc, &tm);
	SelectObject(hdc, old);
	ReleaseDC(hwnd, hdc);
	rowh = tm.tmHeight + S(6);
}

void TapeWindow::Layout()
{
	RECT cr;
	GetClientRect(hwnd, &cr);
	int m = S(8);
	int cw = cr.right;
	SetRect(&rcfile, m, m, cw - m, m + rowh - S(4));
	// カセットの絵 (左)。右に速さと音量のプルダウン (右上) とカウンター
	int casw = Min(S(210), int((cw - 2 * m) * 50 / 100));
	int cash = int(casw * casheight / caswidth + 0.5);
	SetRect(&rccas, m, rcfile.bottom + S(6), m + casw, rcfile.bottom + S(6) + cash);
	int rx = rccas.right + S(14), dh = S(26);
	// 左に字 (Speed・Volume)、右にプルダウン (値だけ)。まとめて右に寄せる
	int lw = S(58);
	// プルダウンの幅は、一番長い値と三角が入るだけ (字の幅を測る)
	int ddw = 0;
	{
		static const char* const values[] = { "x1", "x2", "x4", "x8", "Off", "25%", "50%", "75%", "100%" };
		HDC hdc = GetDC(hwnd);
		HGDIOBJ of = SelectObject(hdc, fnormal);
		for (int i = 0; i < int(sizeof(values) / sizeof(values[0])); i++)
		{
			SIZE sz;
			GetTextExtentPoint32(hdc, values[i], strlen(values[i]), &sz);
			ddw = Max(ddw, int(sz.cx));
		}
		SelectObject(hdc, of);
		ReleaseDC(hwnd, hdc);
		ddw += S(8) + S(22);			// 左の余白と、右の三角の分 (PaintDropdown と合わせる)
	}
	int dx = Max(rx, int(cw - m - ddw - lw));
	SetRect(&rcspeedlabel, dx, rccas.top, dx + lw, rccas.top + dh);
	SetRect(&rcvollabel, dx, rccas.top + dh + S(4), dx + lw, rccas.top + 2 * dh + S(4));
	SetRect(&rcbtn[dd_speed], dx + lw, rccas.top, cw - m, rccas.top + dh);
	SetRect(&rcbtn[dd_vol], dx + lw, rccas.top + dh + S(4), cw - m, rccas.top + 2 * dh + S(4));
	SetRect(&rccounter, rx, rcbtn[dd_vol].bottom + S(2), cw - m, Max(int(rccas.bottom), int(rcbtn[dd_vol].bottom + S(56))));
	int bary = Max(int(rccas.bottom), int(rccounter.bottom));
	SetRect(&rcbar, m, bary + S(8), cw - m, bary + S(18));
	int gap = S(4);
	int by = rcbar.bottom + S(10), bh = S(30);
	int bw = Max(1, int((cw - 2 * m - (nbuttons - 1) * gap) / nbuttons));
	for (int i = 0; i < nbuttons; i++)
		SetRect(&rcbtn[i], m + i * (bw + gap), by, m + i * (bw + gap) + bw, by + bh);
	rcbtn[nbuttons - 1].right = cw - m;
	int ly = by + bh + S(10);
	SetRect(&rclist, m, ly, cw - m, Max(int(ly + rowh), int(cr.bottom - m)));
	UpdateScroll();
}

// ---------------------------------------------------------------------------
//	一覧の送り (縦のスクロールバー)
//
void TapeWindow::UpdateScroll()
{
	if (!hwnd)
		return;
	int rows = Max(1, int((rclist.bottom - rclist.top) / rowh));
	int n = int(cues.size());
	if (scroll > Max(0, n - rows))
		scroll = Max(0, n - rows);
	SCROLLINFO si;
	memset(&si, 0, sizeof(si));
	si.cbSize = sizeof(si);
	si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
	si.nMin = 0;
	si.nMax = Max(0, n - 1);
	si.nPage = rows;
	si.nPos = scroll;
	SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

void TapeWindow::Scroll(int p)
{
	int rows = Max(1, int((rclist.bottom - rclist.top) / rowh));
	p = Limit(p, Max(0, int(cues.size()) - rows), 0);
	if (p != scroll)
	{
		scroll = p;
		UpdateScroll();
		InvalidateRect(hwnd, 0, false);
	}
}

// ---------------------------------------------------------------------------
//	ボタン・一覧
//
bool TapeWindow::ButtonEnabled(int i)
{
	switch (i)
	{
	case btn_open:		return true;
	case btn_rewind:	return st.open && !st.recording;
	case btn_prev:
	case btn_next:		return st.cancue;
	case btn_rec:		return st.open && !st.writeprotect && !st.wave;
	}
	if (i == dd_speed)
		return !st.wave;			// RAW は Tape speed が効かない (いつも等速)
	return i == dd_vol;
}

int TapeWindow::HitButton(int x, int y)
{
	POINT pt = { x, y };
	for (int i = 0; i < nitems; i++)
	{
		if (PtInRect(&rcbtn[i], pt))
			return i;
	}
	return -1;
}

int TapeWindow::HitRow(int x, int y)
{
	POINT pt = { x, y };
	if (!PtInRect(&rclist, pt))
		return -1;
	int r = (y - rclist.top - 1) / rowh + scroll;
	return r < int(cues.size()) ? r : -1;
}

void TapeWindow::Press(int i)
{
	static const int ids[nbuttons] =
	{
		IDM_TAPE, IDM_TAPE_REWIND, IDM_TAPE_PREV, IDM_TAPE_NEXT, IDM_TAPE_RECORD,
	};
	if (!ButtonEnabled(i))
		return;
	if (i >= nbuttons)
	{
		ShowDropdown(i);
		return;
	}
	// lparam にこの窓を入れる (メニューから選んだときだけの動き (Tape メニューを開き直す) をさせない)
	SendMessage(owner, WM_COMMAND, MAKEWPARAM(ids[i], 0), (LPARAM) hwnd);
	Refresh(true);
}

void TapeWindow::CueRow(int row)
{
	if (!tape || !core || row < 0 || row >= int(cues.size()))
		return;
	if (!st.cancue && !(st.wave && row == 0))
		return;
	core->Lock();
	bool ok = tape->CueTo(row);
	uint t = tape->GetTimeMs() / 1000;
	core->Unlock();
	if (ok)
		statusdisplay.Show(20, 3000, "Tape: %d:%.2d", t / 60, t % 60);
	Refresh(true);
}

// ---------------------------------------------------------------------------
//	描く
//
static void FormatTime(char* buf, uint ms)
{
	uint s = ms / 1000;
	if (s >= 3600)
		wsprintf(buf, "%d:%.2d:%.2d", s / 3600, (s / 60) % 60, s % 60);
	else
		wsprintf(buf, "%d:%.2d", s / 60, s % 60);
}

static COLORREF Blend(COLORREF a, COLORREF b, int t)		// t: 0～256 (b の割合)
{
	return RGB((GetRValue(a) * (256 - t) + GetRValue(b) * t) >> 8,
			   (GetGValue(a) * (256 - t) + GetGValue(b) * t) >> 8,
			   (GetBValue(a) * (256 - t) + GetBValue(b) * t) >> 8);
}

void TapeWindow::Paint(HDC hdcw)
{
	RECT cr;
	GetClientRect(hwnd, &cr);
	HDC hdc = CreateCompatibleDC(hdcw);
	HBITMAP bmp = CreateCompatibleBitmap(hdcw, Max(1, int(cr.right)), Max(1, int(cr.bottom)));
	HGDIOBJ oldbmp = SelectObject(hdc, bmp);
	HGDIOBJ oldfont = SelectObject(hdc, fnormal);
	SetBkMode(hdc, TRANSPARENT);

	COLORREF bg = TC(COLOR_WINDOW);
	COLORREF fg = TC(COLOR_WINDOWTEXT);
	COLORREF gray = TC(COLOR_GRAYTEXT);
	HBRUSH bgbrush = CreateSolidBrush(bg);
	FillRect(hdc, &cr, bgbrush);
	DeleteObject(bgbrush);

	// ファイル名と状態
	{
		char state[32] = "";
		COLORREF sc = fg;
		if (st.open)
		{
			if (st.recording)
				strcpy(state, "Recording"), sc = RGB(220, 0, 0);
			else if (st.recarm)
				strcpy(state, "Record (standby)"), sc = RGB(220, 0, 0);
			else if (st.motor)
			{
				if (st.speed > 1 && !st.wave)
					wsprintf(state, "Playing x%d", st.speed);
				else
					strcpy(state, "Playing");
				sc = RGB(0, 150, 0);
			}
			else
				strcpy(state, "Stopped");
		}
		RECT r = rcfile;
		SetTextColor(hdc, sc);
		SIZE sz = { 0, 0 };
		GetTextExtentPoint32(hdc, state, strlen(state), &sz);
		DrawText(hdc, state, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
		r.right -= sz.cx + S(12);
		std::string name;
		if (st.open)
		{
			size_t p = st.file.find_last_of("\\/");
			name = p == std::string::npos ? st.file : st.file.substr(p + 1);
			if (st.writeprotect)
				name += "  (read only)";
		}
		else
			name = "No tape";
		SetTextColor(hdc, st.open ? fg : gray);
		DrawText(hdc, name.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
	}

	// カウンター (今の位置 / 長さ)
	{
		char now[16], len[24], tmp[16];
		FormatTime(now, st.open ? st.ms : 0);
		FormatTime(tmp, st.open ? st.length : 0);
		wsprintf(len, "/ %s", tmp);
		shownsec = int(st.ms / 1000);
		SelectObject(hdc, fbig);
		TEXTMETRIC tmb;
		GetTextMetrics(hdc, &tmb);
		SelectObject(hdc, fnormal);
		TEXTMETRIC tmn;
		GetTextMetrics(hdc, &tmn);
		int top = (rccounter.top + rccounter.bottom - tmb.tmHeight - tmn.tmHeight) / 2;
		RECT r = { rccounter.left, top, rccounter.right, top + tmb.tmHeight };
		SelectObject(hdc, fbig);
		SetTextColor(hdc, st.open ? fg : gray);
		DrawText(hdc, now, -1, &r, DT_SINGLELINE | DT_TOP | DT_LEFT | DT_NOPREFIX);
		SelectObject(hdc, fnormal);
		SetRect(&r, rccounter.left + S(2), top + tmb.tmHeight, rccounter.right, top + tmb.tmHeight + tmn.tmHeight);
		SetTextColor(hdc, gray);
		DrawText(hdc, len, -1, &r, DT_SINGLELINE | DT_TOP | DT_LEFT | DT_NOPREFIX);
	}

	DrawCassette(hdc, rccas);

	// 進み具合のバー (区切りに印)
	{
		RECT r = rcbar;
		HBRUSH frame = CreateSolidBrush(TC(COLOR_3DSHADOW));
		HBRUSH back = CreateSolidBrush(Blend(bg, TC(COLOR_3DSHADOW), 48));
		HBRUSH fill = CreateSolidBrush(st.recording || st.recarm ? RGB(220, 60, 60) : TC(COLOR_HIGHLIGHT));
		FillRect(hdc, &r, back);
		int w = r.right - r.left;
		if (st.open && st.length)
		{
			RECT f = r;
			f.right = f.left + int((unsigned __int64)(Min(st.ms, st.length)) * w / st.length);
			FillRect(hdc, &f, fill);
			for (size_t i = 0; i < cues.size(); i++)
			{
				if (!cues[i].ms)
					continue;
				int x = r.left + int((unsigned __int64)(Min(cues[i].ms, st.length)) * w / st.length);
				RECT t = { x, r.top, x + Max(1, S(1)), r.bottom };
				FillRect(hdc, &t, frame);
			}
		}
		FrameRect(hdc, &r, frame);
		DeleteObject(frame);
		DeleteObject(back);
		DeleteObject(fill);
	}

	// ボタン
	for (int i = 0; i < nitems; i++)
		PaintButton(hdc, i);
	SetTextColor(hdc, fg);
	SelectObject(hdc, fnormal);
	DrawText(hdc, "Speed", -1, &rcspeedlabel, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
	DrawText(hdc, "Volume", -1, &rcvollabel, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

	// 区切りの一覧
	{
		RECT lr = rclist;
		HBRUSH frame = CreateSolidBrush(TC(COLOR_3DSHADOW));
		FrameRect(hdc, &lr, frame);
		DeleteObject(frame);
		InflateRect(&lr, -1, -1);
		HRGN clip = CreateRectRgnIndirect(&lr);
		SelectClipRgn(hdc, clip);

		int cur = CurrentRow();
		current = cur;
		if (!st.open)
		{
			SetTextColor(hdc, gray);
			DrawText(hdc, "Open a tape image (Tape - Open...)", -1, &lr, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
		}
		int numw = S(28), timew = S(64), pad = S(6);
		for (int i = scroll; i < int(cues.size()); i++)
		{
			int y = lr.top + (i - scroll) * rowh;
			if (y >= lr.bottom)
				break;
			RECT rr = { lr.left, y, lr.right, y + rowh };
			bool clickable = st.cancue || (st.wave && i == 0);
			COLORREF tc = fg, dc = gray;
			if (i == cur)
			{
				HBRUSH b = CreateSolidBrush(TC(COLOR_HIGHLIGHT));
				FillRect(hdc, &rr, b);
				DeleteObject(b);
				tc = dc = TC(COLOR_HIGHLIGHTTEXT);
			}
			else if (i == hoverrow && clickable)
			{
				HBRUSH b = CreateSolidBrush(Blend(bg, TC(COLOR_HIGHLIGHT), 48));
				FillRect(hdc, &rr, b);
				DeleteObject(b);
			}
			if (!clickable && i != cur)
				tc = gray;

			char buf[32];
			SelectObject(hdc, fnormal);
			SetTextColor(hdc, i == cur ? tc : gray);
			wsprintf(buf, "%d", i + 1);
			RECT c = { rr.left, y, rr.left + numw, y + rowh };
			DrawText(hdc, buf, -1, &c, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
			SetTextColor(hdc, tc);
			FormatTime(buf, cues[i].ms);
			SetRect(&c, rr.left + numw + pad, y, rr.left + numw + pad + timew, y + rowh);
			DrawText(hdc, buf, -1, &c, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

			// 速さとバイト数 (右に寄せる) と、中身の見当
			SelectObject(hdc, fsmall);
			SIZE dsz = { 0, 0 };
			const std::string& d = cues[i].detail;
			GetTextExtentPoint32(hdc, d.c_str(), d.size(), &dsz);
			SetRect(&c, rr.left, y, rr.right - pad, y + rowh);
			SetTextColor(hdc, dc);
			DrawText(hdc, d.c_str(), -1, &c, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
			SelectObject(hdc, fnormal);
			std::string label = cues[i].label;
			if (label.empty())
				label = st.wave ? "Start (RAW: gaps are not listed)" : "(no data)";
			SetRect(&c, rr.left + numw + pad + timew, y, rr.right - pad - (dsz.cx ? dsz.cx + S(12) : 0), y + rowh);
			SetTextColor(hdc, label == cues[i].label ? tc : (i == cur ? tc : gray));
			DrawText(hdc, label.c_str(), -1, &c, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
		}
		SelectClipRgn(hdc, 0);
		DeleteObject(clip);
	}

	BitBlt(hdcw, 0, 0, cr.right, cr.bottom, hdc, 0, 0, SRCCOPY);
	SelectObject(hdc, oldfont);
	SelectObject(hdc, oldbmp);
	DeleteObject(bmp);
	DeleteDC(hdc);
}

//	速さ・音量のプルダウン (押すと選ぶメニューを出す)
void TapeWindow::PaintDropdown(HDC hdc, int i)
{
	char text[32];
	if (i == dd_speed)
		wsprintf(text, "x%d", st.speed);
	else
		wsprintf(text, st.volume ? "%d%%" : "Off", st.volume);
	bool en = ButtonEnabled(i);
	if (!en)
		lstrcpy(text, "x1");			// RAW はいつも等速
	RECT r = rcbtn[i];
	COLORREF face = TC(COLOR_BTNFACE);
	COLORREF hl = TC(COLOR_HIGHLIGHT);
	bool hot = en && (hoverbtn == i || pressbtn == i);
	HBRUSH b = CreateSolidBrush(pressbtn == i ? Blend(face, hl, 96) : hot ? Blend(face, hl, 40) : face);
	HPEN pen = CreatePen(PS_SOLID, 1, hot ? hl : TC(COLOR_3DSHADOW));
	HGDIOBJ ob = SelectObject(hdc, b);
	HGDIOBJ op = SelectObject(hdc, pen);
	RoundRect(hdc, r.left, r.top, r.right, r.bottom, S(4), S(4));
	SelectObject(hdc, ob);
	SelectObject(hdc, op);
	DeleteObject(b);
	DeleteObject(pen);

	// 下向きの三角 (右の端)
	int cy = (r.top + r.bottom) / 2;
	POINT tri[3] = { { r.right - S(17), cy - S(2) }, { r.right - S(8), cy - S(2) }, { r.right - S(12) - 1, cy + S(3) } };
	COLORREF tc = en ? TC(COLOR_BTNTEXT) : TC(COLOR_GRAYTEXT);
	HBRUSH tb = CreateSolidBrush(tc);
	HPEN tp = CreatePen(PS_SOLID, 1, tc);
	ob = SelectObject(hdc, tb);
	op = SelectObject(hdc, tp);
	Polygon(hdc, tri, 3);
	SelectObject(hdc, ob);
	SelectObject(hdc, op);
	DeleteObject(tb);
	DeleteObject(tp);

	RECT tr = { r.left + S(8), r.top, r.right - S(20), r.bottom };
	SelectObject(hdc, fnormal);
	SetTextColor(hdc, tc);
	DrawText(hdc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void TapeWindow::ShowDropdown(int i)
{
	static const int speeds[4] = { 1, 2, 4, 8 };
	static const int speedids[4] = { IDM_TAPESPEED_1, IDM_TAPESPEED_2, IDM_TAPESPEED_4, IDM_TAPESPEED_8 };
	static const char* const speedlabels[4] = { "x1", "x2", "x4", "x8" };
	static const int volids[5] = { IDM_TAPEVOL_0, IDM_TAPEVOL_25, IDM_TAPEVOL_50, IDM_TAPEVOL_75, IDM_TAPEVOL_100 };
	static const char* const vollabels[5] = { "Off", "25%", "50%", "75%", "100%" };

	HMENU hm = CreatePopupMenu();
	if (!hm)
		return;
	if (i == dd_speed)
	{
		for (int k = 0; k < 4; k++)
			AppendMenu(hm, MF_STRING | (st.speed == speeds[k] ? MF_CHECKED : 0), speedids[k], speedlabels[k]);
	}
	else
	{
		for (int k = 0; k < 5; k++)
			AppendMenu(hm, MF_STRING | (st.volume == k * 25 ? MF_CHECKED : 0), volids[k], vollabels[k]);
	}
	POINT pt = { rcbtn[i].left, rcbtn[i].bottom };
	ClientToScreen(hwnd, &pt);
	pressbtn = i;
	InvalidateRect(hwnd, 0, false);
	UpdateWindow(hwnd);

	// この窓は入力を取らないので、メニューの持ち主は本体の窓にする
	// (持ち主がフォアグラウンドでないと、ほかをクリックしてもメニューが閉じない)
	SetForegroundWindow(owner);
	UINT id = TrackPopupMenu(hm, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON,
							 pt.x, pt.y, 0, owner, 0);
	PostMessage(owner, WM_NULL, 0, 0);
	DestroyMenu(hm);
	pressbtn = -1;
	if (id)
		SendMessage(owner, WM_COMMAND, MAKEWPARAM(id, 0), (LPARAM) hwnd);
	Refresh(true);
}

void TapeWindow::PaintButton(HDC hdc, int i)
{
	if (i >= nbuttons)
	{
		PaintDropdown(hdc, i);
		return;
	}
	RECT r = rcbtn[i];
	bool en = ButtonEnabled(i);
	bool checked = i == btn_rec && st.recarm;
	bool down = (pressbtn == i && hoverbtn == i) || checked;
	COLORREF face = TC(COLOR_BTNFACE);
	COLORREF hl = TC(COLOR_HIGHLIGHT);
	COLORREF c = face;
	if (en && down)
		c = Blend(face, hl, 96);
	else if (en && hoverbtn == i)
		c = Blend(face, hl, 40);
	HBRUSH b = CreateSolidBrush(c);
	HPEN pen = CreatePen(PS_SOLID, 1, en && (hoverbtn == i || down) ? hl : TC(COLOR_3DSHADOW));
	HGDIOBJ ob = SelectObject(hdc, b);
	HGDIOBJ op = SelectObject(hdc, pen);
	RoundRect(hdc, r.left, r.top, r.right, r.bottom, S(6), S(6));
	SelectObject(hdc, ob);
	SelectObject(hdc, op);
	DeleteObject(b);
	DeleteObject(pen);

	// 絵と字。狭ければ絵だけ
	SelectObject(hdc, fnormal);
	const char* label = buttonlabel[i];
	SIZE sz;
	GetTextExtentPoint32(hdc, label, strlen(label), &sz);
	int is = S(14), gap = S(5);
	int total = is + gap + sz.cx;
	bool text = total + S(8) <= r.right - r.left;
	if (!text)
		total = is;
	int x = (r.left + r.right - total) / 2;
	int cy = (r.top + r.bottom) / 2;
	RECT ir = { x, cy - is / 2, x + is, cy - is / 2 + is };
	DrawIcon(hdc, i, ir, en);
	if (text)
	{
		RECT tr = { x + is + gap, r.top, r.right, r.bottom };
		SetTextColor(hdc, en ? TC(COLOR_BTNTEXT) : TC(COLOR_GRAYTEXT));
		DrawText(hdc, label, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
	}
}

//	ボタンの絵 (r: 正方形)。Open = 取り出し (三角と棒)、Rewind = 棒と左の三角、Prev / Next = 2 つの三角、Rec = 赤い丸
void TapeWindow::DrawIcon(HDC hdc, int i, const RECT& r, bool en)
{
	COLORREF c = en ? TC(COLOR_BTNTEXT) : TC(COLOR_GRAYTEXT);
	if (i == btn_rec && en)
		c = RGB(220, 0, 0);
	HBRUSH b = CreateSolidBrush(c);
	HPEN pen = CreatePen(PS_SOLID, 1, c);
	HGDIOBJ ob = SelectObject(hdc, b);
	HGDIOBJ op = SelectObject(hdc, pen);
	int w = r.right - r.left, h = r.bottom - r.top;
	int l = r.left, t = r.top;
	switch (i)
	{
	case btn_open:
		{
			POINT p[3] = { { l + w / 2, t + h / 8 }, { r.right - 1, t + h * 5 / 8 }, { l, t + h * 5 / 8 } };
			Polygon(hdc, p, 3);
			RECT bar = { l, t + h * 6 / 8, r.right, t + h * 15 / 16 };
			FillRect(hdc, &bar, b);
		}
		break;
	case btn_rewind:
		{
			RECT bar = { l + w / 8, t + h / 8, l + w / 8 + Max(2, w / 7), r.bottom - h / 8 };
			FillRect(hdc, &bar, b);
			POINT p[3] = { { bar.right, t + h / 2 }, { r.right - w / 8, t + h / 8 }, { r.right - w / 8, r.bottom - h / 8 } };
			Polygon(hdc, p, 3);
		}
		break;
	case btn_prev:
	case btn_next:
		{
			for (int k = 0; k < 2; k++)
			{
				int x0 = l + k * w / 2, x1 = x0 + w / 2;
				POINT p[3];
				if (i == btn_prev)
				{
					p[0].x = x0, p[0].y = t + h / 2;
					p[1].x = x1, p[1].y = t + h / 8;
					p[2].x = x1, p[2].y = r.bottom - h / 8;
				}
				else
				{
					p[0].x = x1, p[0].y = t + h / 2;
					p[1].x = x0, p[1].y = t + h / 8;
					p[2].x = x0, p[2].y = r.bottom - h / 8;
				}
				Polygon(hdc, p, 3);
			}
		}
		break;
	case btn_rec:
		Ellipse(hdc, l + w / 8, t + h / 8, r.right - w / 8, r.bottom - h / 8);
		break;
	}
	SelectObject(hdc, ob);
	SelectObject(hdc, op);
	DeleteObject(b);
	DeleteObject(pen);
}

// ---------------------------------------------------------------------------

LRESULT CALLBACK TapeWindow::WndProcGate(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	TapeWindow* tw;
	if (msg == WM_NCCREATE)
	{
		tw = (TapeWindow*) ((LPCREATESTRUCT) lp)->lpCreateParams;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) tw);
		tw->hwnd = hwnd;
	}
	else
		tw = (TapeWindow*) GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (tw)
		return tw->WndProc(hwnd, msg, wp, lp);
	return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT TapeWindow::WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_CREATE:
		dpi = Dpi::GetForWindow(hw);
		MakeFonts();
		Layout();
		AddDockMenu(GetSystemMenu(hw, FALSE));	// タイトルバーのメニューに「ペインにドック」(M88gn)
		Theme::ApplyScrollBars(hw);				// スクロールバーをテーマに合わせる
		return 0;

	case WM_SYSCOMMAND:
		if (DockSysCommand(wp))
			return 0;
		break;

	case WM_DPICHANGED_AFTERPARENT:		// ペインに入っていて、本体の窓が DPI の違う画面へ移った (M88gn)
		dpi = Dpi::GetForWindow(hw);
		MakeFonts();
		Layout();
		InvalidateRect(hw, 0, false);
		return 0;

	case WM_DESTROY:
		KillTimer(hw, timerid);
		KillTimer(hw, animid);
		animating = false;
		GetPlacement(&posx, &posy, &width, &height);
		placed = true;
		if (fnormal) DeleteObject(fnormal), fnormal = 0;
		if (fbig) DeleteObject(fbig), fbig = 0;
		if (fsmall) DeleteObject(fsmall), fsmall = 0;
		hwnd = 0;
		return 0;

	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;			// クリックしてもフォーカスを奪わない (M88 のキー入力を続けられる)

	case WM_CLOSE:
		Show(false);
		return 0;

	case WM_TIMER:
		if (wp == timerid)
			Refresh(false);
		else if (wp == animid)
			Animate();
		return 0;

	case WM_GETMINMAXINFO:
		{
			int d = Dpi::GetForWindow(hw);
			((MINMAXINFO*) lp)->ptMinTrackSize.x = MulDiv(360, d, 96);
			((MINMAXINFO*) lp)->ptMinTrackSize.y = MulDiv(410, d, 96);
		}
		return 0;

	case WM_DPICHANGED:					// DPI の違う画面に移った。字を作り直し、Windows の勧める位置・大きさにする
		{
			dpi = HIWORD(wp);
			MakeFonts();
			const RECT* r = (const RECT*) lp;
			SetWindowPos(hw, 0, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
			Layout();
			InvalidateRect(hw, 0, false);
		}
		return 0;

	case WM_SIZE:
		Layout();
		InvalidateRect(hw, 0, false);
		return 0;

	case WM_EXITSIZEMOVE:
		GetPlacement(&posx, &posy, &width, &height);
		placed = true;
		return 0;

	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hw, &ps);
			Paint(hdc);
			EndPaint(hw, &ps);
		}
		return 0;

	case WM_VSCROLL:
		{
			int rows = Max(1, int((rclist.bottom - rclist.top) / rowh));
			int p = scroll;
			switch (LOWORD(wp))
			{
			case SB_LINEUP:		p--; break;
			case SB_LINEDOWN:	p++; break;
			case SB_PAGEUP:		p -= rows; break;
			case SB_PAGEDOWN:	p += rows; break;
			case SB_TOP:		p = 0; break;
			case SB_BOTTOM:		p = int(cues.size()); break;
			case SB_THUMBTRACK:
			case SB_THUMBPOSITION:
				{
					SCROLLINFO si;
					memset(&si, 0, sizeof(si));
					si.cbSize = sizeof(si);
					si.fMask = SIF_TRACKPOS;
					GetScrollInfo(hw, SB_VERT, &si);
					p = si.nTrackPos;
				}
				break;
			}
			Scroll(p);
		}
		return 0;

	case WM_MOUSEWHEEL:
		Scroll(scroll - (short) HIWORD(wp) / WHEEL_DELTA * 3);
		return 0;

	case WM_MOUSEMOVE:
		{
			int x = (short) LOWORD(lp), y = (short) HIWORD(lp);
			int hb = HitButton(x, y), hr = HitRow(x, y);
			if (hb != hoverbtn || hr != hoverrow)
			{
				hoverbtn = hb, hoverrow = hr;
				InvalidateRect(hw, 0, false);
			}
			if (!tracking)
			{
				TRACKMOUSEEVENT tme;
				memset(&tme, 0, sizeof(tme));
				tme.cbSize = sizeof(tme);
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = hw;
				tracking = !!TrackMouseEvent(&tme);
			}
		}
		return 0;

	case WM_MOUSELEAVE:
		tracking = false;
		if (hoverbtn >= 0 || hoverrow >= 0)
		{
			hoverbtn = hoverrow = -1;
			InvalidateRect(hw, 0, false);
		}
		return 0;

	case WM_LBUTTONDOWN:
		{
			int x = (short) LOWORD(lp), y = (short) HIWORD(lp);
			int b = HitButton(x, y);
			if (b == dd_speed || b == dd_vol)
			{
				if (ButtonEnabled(b))
					ShowDropdown(b);		// プルダウンは押したときに開く
				return 0;
			}
			if (b >= 0 && ButtonEnabled(b))
			{
				pressbtn = b;
				SetCapture(hw);
				InvalidateRect(hw, 0, false);
				return 0;
			}
			int r = HitRow(x, y);
			if (r >= 0)
				CueRow(r);
		}
		return 0;

	case WM_LBUTTONUP:
		if (pressbtn >= 0)
		{
			int b = pressbtn;
			pressbtn = -1;
			ReleaseCapture();
			if (HitButton((short) LOWORD(lp), (short) HIWORD(lp)) == b)
				Press(b);
			InvalidateRect(hw, 0, false);
		}
		return 0;

	case WM_CAPTURECHANGED:
		if (pressbtn >= 0)
		{
			pressbtn = -1;
			InvalidateRect(hw, 0, false);
		}
		return 0;
	}
	return DefWindowProc(hw, msg, wp, lp);
}
