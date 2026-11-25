// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 2000.
// ---------------------------------------------------------------------------
//	$Id: winmon.cpp,v 1.4 2002/04/07 05:40:11 cisc Exp $

#include "headers.h"
#include <climits>
#include "resource.h"
#include "winmon.h"
#include "labeledit.h"
#include "misc.h"
#include "88config.h"
#include "dpi.h"
#include "uitheme.h"

#ifndef WM_DPICHANGED
#define WM_DPICHANGED	0x02e0
#endif

// フォントサイズの選択肢（IDM_MON_FONTSIZE_BASE からの連番に対応）
static const int fontsizetable[] = { 10, 12, 14, 16, 18, 20, 24, 32 };
static const int nfontsizes = sizeof(fontsizetable) / sizeof(fontsizetable[0]);
static const int fontsizedefault = 18;

// タイトルバーのメニュー (システムメニュー) に足す項目 (下の 4 ビットは Windows が使うので 0。M88gn)
enum { sc_fontlarger = 0x1010, sc_fontsmaller = 0x1020, sc_fontdefault = 0x1030 };
// デバッグの窓のフォント (すべての窓で共通。M88gn)
static const char fontfacedefault[] = "ＭＳ ゴシック";		// Windows のフォントの名前 (M88am の「ＭＳゴシック」は空白が無く、似たフォントで代わりに描いていた)
char WinMonitor::fontface[LF_FACESIZE] = "";
BYTE WinMonitor::fontcharset = SHIFTJIS_CHARSET;
WinMonitor* WinMonitor::instances[WinMonitor::maxinstances];

// ---------------------------------------------------------------------------
//	ウインドウが画面外に出ていたら、プライマリモニタの左上寄りに戻す
//
static void ClampToWorkArea(RECT* rect)
{
	POINT center;
	center.x = (rect->left + rect->right) / 2;
	center.y = (rect->top + rect->bottom) / 2;

	HMONITOR hmon = MonitorFromPoint(center, MONITOR_DEFAULTTONULL);
	if (hmon)
		return;     // どこかのモニタ上にある。補正不要

	RECT work;
	SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);

	int w = rect->right - rect->left;
	int h = rect->bottom - rect->top;

	rect->left   = work.left + 64;
	rect->top    = work.top + 64;
	rect->right  = rect->left + w;
	rect->bottom = rect->top + h;
}

// ---------------------------------------------------------------------------
//	構築/消滅
//
WinMonitor::WinMonitor()
: guidetracking(false), txtbuf(0), txpbuf(0), timerinterval(0), timer(0), wheeldelta(0), inisection(0),
  guiderow(-1), guidecol1(-1), guidecol2(-1),
  defcols(0), defrows(0), savedpi(0)
{
	hwnd = 0;
	hwndowner = 0;
	hwndstatus = 0;
	line = 0;
	hfont = 0;
	hbitmap = 0;
	fontheight = fontsizedefault;
	fontheightpx = fontsizedefault;
	wndrect.left = wndrect.top = 0;
	wndrect.right = wndrect.bottom = -1;
	for (int i = 0; i < maxinstances; i++)
	{
		if (!instances[i])
		{
			instances[i] = this;
			break;
		}
	}
}

WinMonitor::~WinMonitor()
{
	for (int i = 0; i < maxinstances; i++)
		if (instances[i] == this)
			instances[i] = 0;
	if (hbitmap)
		DeleteObject(hbitmap);
	if (hfont)
		DeleteObject(hfont);
	delete[] txtbuf;
}

// ---------------------------------------------------------------------------
//	初期化
//
bool WinMonitor::Init(LPCTSTR tmpl, const char* section, int _defcols, int _defrows)
{
	lptemplate = tmpl;
	nlines = 0;
	txcol = 0xffffff;
	bkcol = 0x400000;
	inisection = section;
	defcols = _defcols;
	defrows = _defrows;
	LoadMonitorConfig();
	return true;
}

// ---------------------------------------------------------------------------
//	ウインドウ位置・サイズを M88.ini から読み込む
//	(inisection が NULL なら何もしない)
//
//	M88gn: 文字の大きさ (FontSize、96 DPI での高さ) と、位置・大きさを保存したときの DPI (Dpi) も読む。
//	Dpi が無ければ M88gn 1.3.0 まで (DPI 非対応の窓) の値なので、96 DPI 換算として窓を作るときに直す
//
void WinMonitor::LoadMonitorConfig()
{
	if (!inisection)
		return;

	const char* ini = PC8801::GetM88IniPath();

	int x  = GetPrivateProfileInt(inisection, "PosX",   INT_MIN, ini);
	int y  = GetPrivateProfileInt(inisection, "PosY",   INT_MIN, ini);
	int w  = GetPrivateProfileInt(inisection, "Width",  0, ini);
	int h  = GetPrivateProfileInt(inisection, "Height", 0, ini);
	savedpi = GetPrivateProfileInt(inisection, "Dpi", 0, ini);
	int fh = GetPrivateProfileInt(inisection, "FontSize", fontsizedefault, ini);
	fontheight = fontsizedefault;
	for (int i = 0; i < nfontsizes; i++)
		if (fontsizetable[i] == fh)
			fontheight = fh;

	if (x != INT_MIN && y != INT_MIN && w > 0 && h > 0)
	{
		wndrect.left   = x;
		wndrect.top    = y;
		wndrect.right  = x + w;
		wndrect.bottom = y + h;
	}
}

// ---------------------------------------------------------------------------
//	ウインドウ位置・サイズを M88.ini へ書き込む
//	(inisection が NULL なら何もしない。M88gn: 文字の大きさと DPI も書く。位置・大きさは物理ピクセル)
//
void WinMonitor::SaveMonitorConfig()
{
	if (!inisection || !hwnd)
		return;

	RECT rect;
	if (docked)
		rect = wndrect;		// ドックしている間は浮かせたときの位置・大きさ (M88gn)
	else
		GetWindowRect(hwnd, &rect);

	const char* ini = PC8801::GetM88IniPath();
	char buf[32];

	wsprintf(buf, "%d", rect.left);                 WritePrivateProfileString(inisection, "PosX",   buf, ini);
	wsprintf(buf, "%d", rect.top);                  WritePrivateProfileString(inisection, "PosY",   buf, ini);
	wsprintf(buf, "%d", rect.right - rect.left);    WritePrivateProfileString(inisection, "Width",  buf, ini);
	wsprintf(buf, "%d", rect.bottom - rect.top);    WritePrivateProfileString(inisection, "Height", buf, ini);
	wsprintf(buf, "%d", fontheight);                WritePrivateProfileString(inisection, "FontSize", buf, ini);
	savedpi = Dpi::IsAware() ? Dpi::GetForWindow(hwnd) : 96;
	wsprintf(buf, "%d", savedpi);                   WritePrivateProfileString(inisection, "Dpi",    buf, ini);
	wndrect = rect;
}

// ---------------------------------------------------------------------------
//	ダイアログ表示
//
void WinMonitor::Show(HINSTANCE hinstance, HWND hwndparent, bool show)
{
	hwndowner = hwndparent;

	if (show)
	{
		if (!hwnd)
		{
			assert(!hwndstatus);

			RECT rect = wndrect;
			// M88gn: デバッグの窓は DPI 対応 (本体の窓と同じ Per Monitor v2)。M88gn 1.3.0 までの M88.ini の値は
			// 96 DPI 換算なので、本体の窓の DPI で物理ピクセルに直す
			if (savedpi == 0 && Dpi::IsAware() && rect.right > rect.left)
			{
				int d = Dpi::GetForWindow(hwndparent);
				rect.left = MulDiv(rect.left, d, 96), rect.top = MulDiv(rect.top, d, 96);
				rect.right = MulDiv(rect.right, d, 96), rect.bottom = MulDiv(rect.bottom, d, 96);
				savedpi = d;
			}
			hinst = hinstance;
			hwnd = CreateDialogParam(hinst, lptemplate, hwndparent, DLGPROC((void*)DlgProcGate), (LPARAM)this);
			hwndstatus = 0;

			if (rect.right > rect.left && rect.bottom > rect.top)
			{
				// 画面外に飛んでいたら補正する
				ClampToWorkArea(&rect);

				SetWindowPos(hwnd, 0,
						rect.left, rect.top,
						rect.right - rect.left, rect.bottom - rect.top,
						SWP_NOZORDER | SWP_NOACTIVATE);
			}
			else if (defcols > 0 && defrows > 0 && fontwidth > 0 && fontheightpx > 0)
			{
				// INI に保存された大きさが無いので、
				// 「必要な文字数×行数」が入るサイズにしてやる。
				// フォントサイズを変えてもこの計算が追従する
				ApplyDefaultSize();
			}
			SetLines(nlines);
			Start();
			DockShown();		// 前にペインにあったなら、そのペインへ (M88gn)
		}
	}
	else
	{
		if (hwnd)
		{
			Stop();
			SendMessage(hwnd, WM_CLOSE, 0, 0);
		}
	}
}

// ---------------------------------------------------------------------------
//	defcols × defrows の文字が収まるウインドウサイズにする
//	(INI に保存されたサイズが無いときだけ呼ばれる)
//
void WinMonitor::ApplyDefaultSize()
{
	RECT r;
	r.left = 0;
	r.top  = 0;
	r.right  = defcols * fontwidth;
	r.bottom = defrows * fontheightpx;

	DWORD style   = GetWindowLong(hwnd, GWL_STYLE);
	DWORD exstyle = GetWindowLong(hwnd, GWL_EXSTYLE);
	Dpi::AdjustWindowRect(&r, style, GetMenu(hwnd) != 0, exstyle, hwnd);	// 窓の DPI の枠 (M88gn)

	// AdjustWindowRectEx はスクロールバーを勘定に入れない
	if (style & WS_VSCROLL)
		r.right += GetSystemMetrics(SM_CXVSCROLL);

	SetWindowPos(hwnd, 0, 0, 0, r.right - r.left, r.bottom - r.top,
				 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
}

// ---------------------------------------------------------------------------
//	フォント高さの変更
//
bool WinMonitor::SetFont(HWND hwnd, int fh)
{
	if (hfont)
		DeleteObject(hfont);

	fontheight = fh;

	// DPI スケールを反映(fontheight 自体はメニュー・INI 用の論理値のまま保つ)
	// M88gn: 窓のある画面の DPI (Per Monitor v2。GetDeviceCaps の LOGPIXELSY はシステムの DPI しか返さない)
	int dpi = Dpi::GetForWindow(hwnd);
	int fhScaled = MulDiv(fh, dpi, 96);

	LoadFontFace();
	hfont = CreateFont(fhScaled, 0, 0, 0, 0, 0, 0, 0,
					fontcharset, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					DEFAULT_QUALITY, FIXED_PITCH, fontface);
	if (!hfont)
		return false;

	HDC hdc = GetDC(hwnd);
	HFONT hof = (HFONT) SelectObject(hdc, hfont);
	GetCharWidth(hdc, '0', '0', &fontwidth);
	TEXTMETRIC tm;
	GetTextMetrics(hdc, &tm);
	fontheightpx = tm.tmHeight;          // 実測値。ResizeWindow はこちらを使う
	SelectObject(hdc, hof);
	ReleaseDC(hwnd, hdc);
	return true;
}

// ---------------------------------------------------------------------------
//	窓の大きさに従いテキストバッファのサイズを変更
//
void WinMonitor::ResizeWindow(HWND hwnd)
{
	RECT rect;

	if (!docked)
		GetWindowRect(hwnd, &wndrect);	// ドックしている間は浮かせたときの位置を残す (M88gn)
	GetClientRect(hwnd, &rect);

	clientwidth = rect.right;
	clientheight = rect.bottom;
	if (hwndstatus)
	{
		RECT rectstatus;
		GetWindowRect(hwndstatus, &rectstatus);
		clientheight -= rectstatus.bottom - rectstatus.top;
	}
	width = (clientwidth + fontwidth - 1) / fontwidth;
	height = (clientheight + fontheightpx - 1) / fontheightpx;

	// 仮想 TVRAM のセットアップ
	delete[] txtbuf;
	txtbuf = new TXCHAR[width * height * 2];
	txpbuf = txtbuf + width * height;
	ClearText();

	// DDB の入れ替え
	HDC hdc = GetDC(hwnd);
	HDC hmemdc = CreateCompatibleDC(hdc);

	if (hbitmap)
		DeleteObject(hbitmap);
	hbitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);

	// 背景色で塗りつぶし
	HBITMAP holdbmp = (HBITMAP) SelectObject(hmemdc, hbitmap);
	HBRUSH hbrush = (HBRUSH) SelectObject(hmemdc, CreateSolidBrush(bkcol));
	PatBlt(hmemdc, 0, 0, rect.right, rect.bottom, PATCOPY);
	DeleteObject(SelectObject(hmemdc, hbrush));
	SelectObject(hmemdc, holdbmp);
	DeleteDC(hmemdc);

	ReleaseDC(hwnd, hdc);

	// スクロールバーの設定
	SetLines(nlines);
}

// ---------------------------------------------------------------------------
//	画面の中身を消す
//
void WinMonitor::ClearText()
{
	int nch = width * height;
	for (int i=0; i<nch; i++)
	{
		txtbuf[i].ch = ' ';
		txtbuf[i].txcol = txcol;
		txtbuf[i].bkcol = bkcol;
		txtbuf[i].bkcol2 = bkcol;
	}
}

static inline bool IsKan(int c)
{
	return (0x81 <= c && c <= 0x9f) || (0xe0 < c && c < 0xfd);
}

// ---------------------------------------------------------------------------
//	画面を書く
//
void WinMonitor::DrawMain(HDC _hdc, bool update)
{
	hdc = _hdc;

	Locate(0, 0);

	assert(height>=0);
	memcpy(txpbuf, txtbuf, sizeof(TXCHAR) * width * height);
	UpdateText();

	char* buf = new char[width];
	int c=0;
	int y=0;
	for (int l=0; l<height; l++)
	{
		if (memcmp(txpbuf + c, txtbuf + c, width * sizeof(TXCHAR)) || update)
		{
			// 1行描画
			for (int x=0; x<width; )
			{
				// 同じ属性で何文字書けるか…？
				int n;

				for (n=x; n<width; n++)
				{
					if (   txtbuf[c+x].txcol != txtbuf[c+n].txcol
						|| txtbuf[c+x].bkcol != txtbuf[c+n].bkcol
						|| txtbuf[c+n].bkcol2 != txtbuf[c+n].bkcol)
						break;
					buf[n-x] = txtbuf[c+n].ch;
				}

				// 左右で塗り分けた空白 (M88gn)
				if (n == x && txtbuf[c+x].bkcol2 != txtbuf[c+x].bkcol)
				{
					RECT r;
					SetRect(&r, x * fontwidth, y, x * fontwidth + fontwidth / 2, y + fontheightpx);
					HBRUSH hb = CreateSolidBrush(txtbuf[c+x].bkcol);
					FillRect(hdc, &r, hb);
					DeleteObject(hb);
					SetRect(&r, x * fontwidth + fontwidth / 2, y, (x + 1) * fontwidth, y + fontheightpx);
					hb = CreateSolidBrush(txtbuf[c+x].bkcol2);
					FillRect(hdc, &r, hb);
					DeleteObject(hb);
					x++;
					continue;
				}

				// 書く
				SetTextColor(hdc, txtbuf[c+x].txcol);
				SetBkColor(hdc, txtbuf[c+x].bkcol);
				TextOut(hdc, x * fontwidth, y, buf, n - x);
				x = n;
			}
		}
		c += width, y += fontheightpx;
	}
	delete[] buf;
}

// ---------------------------------------------------------------------------
//	1行上にスクロール
//
void WinMonitor::ScrollUp()
{
	if (height > 2)
	{
		memmove(txtbuf, txtbuf + width, width * (height - 2) * sizeof(TXCHAR));
		memset(txtbuf+width*(height-1), 0, width * sizeof(TXCHAR));

		HDC hdc = GetDC(hwnd);
		HDC hmemdc = CreateCompatibleDC(hdc);

		HBITMAP holdbmp = (HBITMAP) SelectObject(hmemdc, hbitmap);

		RECT rect;
		GetClientRect(hwnd, &rect);
		BitBlt(hmemdc, 0, 0, rect.right, rect.bottom-fontheightpx,
			   hmemdc, 0, fontheightpx, SRCCOPY);

		SelectObject(hmemdc, holdbmp);
		DeleteDC(hmemdc);

		ReleaseDC(hwnd, hdc);
	}
}

// ---------------------------------------------------------------------------
//	1行下にスクロール
//
void WinMonitor::ScrollDown()
{
	if (height > 2)
	{
		memmove(txtbuf + width, txtbuf, width * (height - 1) * sizeof(TXCHAR));

		HDC hdc = GetDC(hwnd);
		HDC hmemdc = CreateCompatibleDC(hdc);

		HBITMAP holdbmp = (HBITMAP) SelectObject(hmemdc, hbitmap);

		RECT rect;
		GetClientRect(hwnd, &rect);
		BitBlt(hmemdc, 0, fontheightpx, rect.right, rect.bottom-fontheightpx,
			   hmemdc, 0, 0, SRCCOPY);

		SelectObject(hmemdc, holdbmp);
		DeleteDC(hmemdc);

		ReleaseDC(hwnd, hdc);
	}
}

// ---------------------------------------------------------------------------
//	テスト用
//
void WinMonitor::UpdateText()
{
	Puts("???\n");
}

// ---------------------------------------------------------------------------
//	書き込み位置の変更
//
void WinMonitor::Locate(int x, int y)
{
	txp.x = Min(x, width);
	txp.y = Min(y, height);
	txtbufptr = txtbuf + txp.x + txp.y * width;
}

// ---------------------------------------------------------------------------
//	文字列書き込み
//
void WinMonitor::Puts(const char* text)
{
	if (txp.y >= height)
		return;

	int c;
	char* txx;
	while (c = *text++)
	{
		if (c == '\n')
		{
			while (txp.x++ < width)
			{
				txtbufptr->ch = ' ';
				txtbufptr->txcol = txcol;
				txtbufptr->bkcol = bkcol;
				txtbufptr->bkcol2 = bkcol;
				txtbufptr++;
			}
			txp.x = 0, txp.y++;
			if (txp.y >= height)
				return;
		}
		else if (c == '\a')
		{
			// set fgcolor
			if (*text != ';')
			{
				txcolprev = txcol;
				txcol = strtoul(text, &txx, 0);
				text = txx + (*txx == ';');
			}
			else
			{
				txcol = txcolprev;
				text++;
			}
		}
		else if (c == '\b')
		{
			// set bkcolor
			if (*text != ';')
			{
				bkcolprev = bkcol;
				bkcol = strtoul(text, &txx, 0);
				text = txx + (*txx == ';');
			}
			else
			{
				bkcol = bkcolprev;
				text++;
			}
		}
		else
		{
			if (txp.x < width)
			{
				txtbufptr->ch = c;
				txtbufptr->txcol = txcol;
				txtbufptr->bkcol = bkcol;
				txtbufptr->bkcol2 = bkcol;
				txtbufptr++;
				txp.x++;
			}
		}
	}
}

// ---------------------------------------------------------------------------
//	左半分と右半分で背景色の違う空白を 1 文字書く (M88gn)
//
void WinMonitor::PutSplitSpace(COLORREF left, COLORREF right)
{
	if (txp.y >= height || txp.x >= width)
		return;
	txtbufptr->ch = ' ';
	txtbufptr->txcol = txcol;
	txtbufptr->bkcol = left;
	txtbufptr->bkcol2 = right;
	txtbufptr++;
	txp.x++;
}

// ---------------------------------------------------------------------------
//	文字列書き込み(書式付)
//
void WinMonitor::Putf(const char* msg, ...)
{
	char buf[512];
	va_list marker;
	va_start(marker, msg);
	wvsprintf(buf, msg, marker);
	va_end(marker);
	Puts(buf);
}

// ---------------------------------------------------------------------------
//	窓書き換え
//
// マスの字を地を塗らずに書き直す (M88gn)
void WinMonitor::RedrawCellText(HDC dc, int x, int y, int n)
{
	if (!txtbuf || y < 0 || y >= height)
		return;
	HGDIOBJ oldfont = SelectObject(dc, hfont);
	int oldmode = SetBkMode(dc, TRANSPARENT);
	for (int i = 0; i < n; i++)
	{
		int cx = x + i;
		if (cx < 0 || cx >= width)
			continue;
		const TXCHAR& c = txtbuf[y * width + cx];
		SetTextColor(dc, c.txcol);
		TextOut(dc, cx * fontwidth, y * fontheightpx, &c.ch, 1);
	}
	SetBkMode(dc, oldmode);
	SelectObject(dc, oldfont);
}

// ラベルを色付きで書く (M88gn)
void WinMonitor::PutLabel(const char* label)
{
	COLORREF c = txcol;
	txcol = labelcolor;
	Puts(label);
	txcol = c;
}

void WinMonitor::PutAddrLabel(int cpu, uint addr, int width)
{
	const char* l = PC8801::LabelNow(cpu, addr);
	char buf[16];
	int n;
	if (l)
	{
		PutLabel(l);
		wsprintf(buf, "(%.4X)", addr & 0xffff);
		n = lstrlen(l) + lstrlen(buf);
	}
	else
	{
		wsprintf(buf, "%.4X", addr & 0xffff);
		n = lstrlen(buf);
	}
	Puts(buf);
	for (; n < width; n++)
		Puts(" ");
}

// マウスの下の行の下線と桁の縦線 (M88gn)
void WinMonitor::SetGuide(int row, int col1, int col2)
{
	if (row == guiderow && col1 == guidecol1 && col2 == guidecol2)
		return;
	guiderow = row, guidecol1 = col1, guidecol2 = col2;
	if (hwnd)
		InvalidateRect(hwnd, 0, FALSE);
}

void WinMonitor::TrackGuideLeave(HWND hdlg)
{
	if (guidetracking)
		return;
	TRACKMOUSEEVENT tme;
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = hdlg;
	tme.dwHoverTime = 0;
	guidetracking = !!TrackMouseEvent(&tme);
}

void WinMonitor::DrawGuide(HDC dc)
{
	if (guiderow < 0 && guidecol1 < 0 && guidecol2 < 0)
		return;
	int th = Max(1, fontheightpx / 16);		// 線の太さ (字の大きさに合わせる)
	int bottom = Min(clientheight, height * fontheightpx);
	HBRUSH hb = CreateSolidBrush(RGB(0x60, 0xd0, 0xff));
	RECT r;
	if (guiderow >= 0 && guiderow < height)
	{
		SetRect(&r, 0, (guiderow + 1) * fontheightpx - th, clientwidth, (guiderow + 1) * fontheightpx);
		FillRect(dc, &r, hb);
	}
	int cols[2] = { guidecol1, guidecol2 };
	for (int i = 0; i < 2; i++)
	{
		if (cols[i] < 0 || cols[i] > width)
			continue;
		int x = cols[i] * fontwidth;
		SetRect(&r, x - th, 0, x, bottom);
		FillRect(dc, &r, hb);
	}
	DeleteObject(hb);
}

void WinMonitor::Draw(HWND hwnd, HDC hdc)
{
	if (!hbitmap)
		return;

	HDC hmemdc = CreateCompatibleDC(hdc);

	HBITMAP holdbmp = (HBITMAP) SelectObject(hmemdc, hbitmap);

	SetTextColor(hmemdc, 0xffffff);
	SetBkColor(hmemdc, bkcol);
	HFONT holdfont = (HFONT) SelectObject(hmemdc, hfont);

	DrawMain(hmemdc);

	SelectObject(hmemdc, holdfont);

	BitBlt(hdc, 0, 0, clientwidth, clientheight, hmemdc, 0, 0, SRCCOPY);
	// 線は画面の側だけに描く (裏の絵は文字の差分の描き直しに使うので汚さない)
	DrawOverlay(hdc);
	DrawGuide(hdc);

	SelectObject(hmemdc, holdbmp);
	DeleteDC(hmemdc);
}


void WinMonitor::Update()
{
	if (hwnd)
	{
		InvalidateRect(hwnd, 0, false);
		RedrawWindow(hwnd, 0, 0, RDW_UPDATENOW);
	}
}

// ---------------------------------------------------------------------------
//	ライン数を設定
//
void WinMonitor::SetLines(int nl)
{
	nlines = nl;
	line = Limit(line, nlines, 0);
	if (nlines)
	{
		SCROLLINFO si;
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE | SIF_DISABLENOSCROLL;
		si.nMin = 0;
		si.nMax = Max(1, nlines+height-2);
		si.nPage = height;
		si.nPos = line;
		SetScrollInfo(hwnd, SB_VERT, &si, true);
	}
}

// ---------------------------------------------------------------------------
//	ライン数を設定
//
void WinMonitor::SetLine(int nl)
{
	line = nl;
	if (nlines)
	{
		SCROLLINFO si;
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_POS | SIF_DISABLENOSCROLL;
		si.nMin = 0;
		si.nMax = Max(1, nlines+height-2);
		si.nPage = height;
		si.nPos = line;
		SetScrollInfo(hwnd, SB_VERT, &si, true);
	}
}



// ---------------------------------------------------------------------------
//	スクロールバー処理
//
int WinMonitor::VerticalScroll(int msg)
{
	switch (msg)
	{
	case SB_LINEUP:
		if (--line < 0)
			line = 0;
		else
			ScrollDown();
		break;

	case SB_LINEDOWN:
		if (++line >= nlines)
			line = Max(0, nlines - 1);
		else
			ScrollUp();
		break;

	case SB_PAGEUP:
		line = Max(line - Max(1, height - 1), 0);
		break;

	case SB_PAGEDOWN:
		line = Min(line + Max(1, height - 1), nlines - 1);
		break;

	case SB_TOP:
		line = 0;
		break;

	case SB_BOTTOM:
		line = Max(0, nlines - 1);
		break;

	case SB_THUMBTRACK:
	case SB_THUMBPOSITION:
		line = Limit(GetScrPos(msg == SB_THUMBTRACK), nlines-1, 0);
		break;
	}
	return line;
}

// ---------------------------------------------------------------------------
//	現在のスクロールタブの位置を得る
//
int WinMonitor::GetScrPos(bool track)
{
	SCROLLINFO si;
	memset(&si, 0, sizeof(si));
	si.cbSize = sizeof(si);
	si.fMask = track ? SIF_TRACKPOS : SIF_POS;
	GetScrollInfo(hwnd, SB_VERT, &si);
	return track ? si.nTrackPos : si.nPos;
}

// ---------------------------------------------------------------------------
//	クライエント領域座標を文字座標に変換
//
bool WinMonitor::GetTextPos(POINT* p)
{
	if (fontwidth && fontheightpx)
	{
		p->x /= fontwidth;
		p->y /= fontheightpx;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
//	マウスホイール処理
//	・delta を累積して WHEEL_DELTA 単位で消化する（高精細ホイール対応）
//	・OS のホイールスクロール行数設定を反映する
//
bool WinMonitor::ProcMouseWheel(WPARAM wp)
{
	if (LOWORD(wp) & MK_CONTROL)
	{
		int notch = static_cast<short>(HIWORD(wp)) / WHEEL_DELTA;
		if (notch)
		{
			ChangeFontSize(notch > 0 ? +1 : -1);
			return true;
		}
	}

	if (!nlines)
		return false;

	// OS の設定行数を取得（0 = 設定なし、WHEEL_PAGESCROLL = ページ単位）
	UINT lines = 3;
	SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);

	wheeldelta += static_cast<short>(HIWORD(wp));

	int notches = wheeldelta / WHEEL_DELTA;
	if (!notches)
		return true;                    // 端数を貯めただけ。処理はした扱い
	wheeldelta -= notches * WHEEL_DELTA;

	if (lines == WHEEL_PAGESCROLL)
	{
		int msg = notches > 0 ? SB_PAGEUP : SB_PAGEDOWN;
		int n = notches > 0 ? notches : -notches;
		for (int i = 0; i < n; i++)
			SendMessage(hwnd, WM_VSCROLL, MAKELONG(msg, 0), 0L);
	}
	else
	{
		if (lines < 1)
			lines = 1;
		int msg = notches > 0 ? SB_LINEUP : SB_LINEDOWN;
		int n = (notches > 0 ? notches : -notches) * static_cast<int>(lines);

		// 暴走防止（1回のホイールで動かす上限）
		if (n > 256)
			n = 256;

		for (int i = 0; i < n; i++)
			SendMessage(hwnd, WM_VSCROLL, MAKELONG(msg, 0), 0L);
	}
	return true;
}

// ---------------------------------------------------------------------------
//	Ctrl+ホイールでのフォントサイズ変更
//
void WinMonitor::ChangeFontSize(int dir)
{
	// 現在値に最も近いインデックスを求める
	int idx = 0;
	for (int i = 0; i < nfontsizes; i++)
	{
		if (fontsizetable[i] == fontheight) { idx = i; break; }
		if (fontsizetable[i] < fontheight)  idx = i;
	}
	idx += dir;
	if (idx < 0) idx = 0;
	if (idx >= nfontsizes) idx = nfontsizes - 1;

	SetFontSize(fontsizetable[idx]);
}

// ---------------------------------------------------------------------------
//	デバッグの窓のフォント (M88gn)
//	M88.ini の [Monitor] の FontFace・FontCharset。無ければ ＭＳゴシック
//
void WinMonitor::LoadFontFace()
{
	if (fontface[0])
		return;
	const char* ini = PC8801::GetM88IniPath();
	GetPrivateProfileString("Monitor", "FontFace", fontfacedefault, fontface, sizeof(fontface), ini);
	if (!fontface[0])
		strncpy_s(fontface, sizeof(fontface), fontfacedefault, _TRUNCATE);
	fontcharset = (BYTE) GetPrivateProfileInt("Monitor", "FontCharset", SHIFTJIS_CHARSET, ini);
}

bool WinMonitor::ChooseMonitorFont(HWND owner)
{
	LoadFontFace();

	LOGFONT lf;
	memset(&lf, 0, sizeof(lf));
	lf.lfHeight = -MulDiv(12, Dpi::GetForWindow(owner), 72);
	lf.lfCharSet = fontcharset;
	lf.lfPitchAndFamily = FIXED_PITCH;
	strncpy_s(lf.lfFaceName, sizeof(lf.lfFaceName), fontface, _TRUNCATE);

	CHOOSEFONT cf;
	memset(&cf, 0, sizeof(cf));
	cf.lStructSize = sizeof(cf);
	cf.hwndOwner = owner;
	cf.lpLogFont = &lf;
	// 等幅のフォントだけ。大きさは窓ごとに拡大縮小で変えるので、ここで選んだ大きさは使わない
	cf.Flags = CF_SCREENFONTS | CF_FIXEDPITCHONLY | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;
	if (!ChooseFont(&cf))
		return false;

	strncpy_s(fontface, sizeof(fontface), lf.lfFaceName, _TRUNCATE);
	fontcharset = lf.lfCharSet;

	const char* ini = PC8801::GetM88IniPath();
	char buf[16];
	WritePrivateProfileString("Monitor", "FontFace", fontface, ini);
	wsprintf(buf, "%d", fontcharset);
	WritePrivateProfileString("Monitor", "FontCharset", buf, ini);

	// 開いている窓を新しいフォントで書き直す
	for (int i = 0; i < maxinstances; i++)
	{
		WinMonitor* m = instances[i];
		if (m && m->hwnd)
		{
			m->SetFont(m->hwnd, m->fontheight);
			m->ResizeWindow(m->hwnd);
			m->Update();
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
//	文字の大きさを変えて、窓を書き直す (M88gn)
//
void WinMonitor::SetFontSize(int fh)
{
	if (!hwnd || fh == fontheight)
		return;
	fontheight = fh;
	SetFont(hwnd, fontheight);
	ResizeWindow(hwnd);
	Update();
}

// ---------------------------------------------------------------------------
//	自動更新頻度を設定
//
void WinMonitor::SetUpdateTimer(int t)
{
	timerinterval = t;
	if (hwnd)
	{
		if (timer)
			KillTimer(hwnd, timer), timer = 0;
		if (t)
			timer = SetTimer(hwnd, 1, timerinterval, 0);
	}
}

// ---------------------------------------------------------------------------
//
//
bool WinMonitor::EnableStatus(bool s)
{
	if (s)
	{
		if (!hwndstatus)
		{
			hwndstatus = CreateStatusWindow(WS_CHILD | WS_VISIBLE, 0, GetHWnd(), 1);
			if (!hwndstatus)
				return false;
		}
	}
	else
	{
		if (hwndstatus)
		{
			DestroyWindow(hwndstatus);
			hwndstatus = 0;
		}
	}
	ResizeWindow(hwnd);
	return true;
}

bool WinMonitor::PutStatus(const char* text, ...)
{
	if (!hwndstatus)
		return false;

	if (!text)
	{
		SendMessage(hwndstatus, SB_SETTEXT, SBT_OWNERDRAW, 0);
		return true;
	}

	va_list a;
	va_start(a, text);
	vsprintf_s(statusbuf, sizeof(statusbuf), text, a);
	va_end(a);

	SendMessage(hwndstatus, SB_SETTEXT, SBT_OWNERDRAW, (LPARAM) statusbuf);
	return true;
}

// ---------------------------------------------------------------------------
//	文字列をクリップボードへ置く
//
//	★ SetClipboardData が成功したらメモリの所有権はクリップボードに移る。
//	   GlobalFree してはいけない(失敗したときだけ解放する)
//
bool WinMonitor::SetClipboardText(const char* text, size_t len)
{
	if (!hwnd || !text)
		return false;

	HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
	if (!hmem)
		return false;

	char* p = (char*) GlobalLock(hmem);
	if (!p)
	{
		GlobalFree(hmem);
		return false;
	}

	memcpy(p, text, len);
	p[len] = 0;
	GlobalUnlock(hmem);

	if (!OpenClipboard(hwnd))
	{
		GlobalFree(hmem);
		return false;
	}
	EmptyClipboard();
	if (!SetClipboardData(CF_TEXT, hmem))
	{
		CloseClipboard();
		GlobalFree(hmem);
		return false;
	}
	CloseClipboard();
	return true;
}

// ---------------------------------------------------------------------------
//	表示中の内容をクリップボードへコピーする(既定の実装)
//
//	txtbuf は「いま見えている1画面ぶん」なので、スクロールして見えていない
//	部分は含まれない。行末の空白は落とす。改行は CRLF。
//	全体をコピーしたい派生クラス(Memory/Code/BASIC)はこの関数を override する
//
bool WinMonitor::CopyToClipboard()
{
	if (!hwnd || !txtbuf)
		return false;

	// 1行あたり最大 width 文字 + CRLF、最後に終端
	size_t size = size_t(width + 2) * height + 1;
	char* buf = new char[size];
	if (!buf)
		return false;

	char* d = buf;
	for (int y = 0; y < height; y++)
	{
		const TXCHAR* src = txtbuf + y * width;

		// 行末の空白を落とす
		int len = width;
		while (len > 0 && (src[len-1].ch == ' ' || src[len-1].ch == 0))
			len--;

		for (int x = 0; x < len; x++)
			*d++ = src[x].ch ? src[x].ch : ' ';

		*d++ = '\r';
		*d++ = '\n';
	}
	*d = 0;

	bool ok = SetClipboardText(buf, d - buf);
	delete[] buf;
	return ok;
}

// ---------------------------------------------------------------------------
//	TextBuffer ― 伸びるテキストバッファ
//
namespace PC8801
{

TextBuffer::TextBuffer()
: buf(0), len(0), cap(0), failed(false)
{
}

TextBuffer::~TextBuffer()
{
	delete[] buf;
}

bool TextBuffer::Reserve(size_t need)
{
	if (failed)
		return false;
	if (need <= cap)
		return true;

	size_t newcap = cap ? cap : 0x10000;	// 初期容量 64KB
	while (newcap < need)
		newcap *= 2;

	char* newbuf = new char[newcap];
	if (!newbuf)
	{
		failed = true;
		return false;
	}
	if (buf)
		memcpy(newbuf, buf, len);
	delete[] buf;
	buf = newbuf;
	cap = newcap;
	return true;
}

bool TextBuffer::Add(const char* text)
{
	if (failed || !text)
		return false;

	size_t n = strlen(text);
	if (!Reserve(len + n + 1))
		return false;

	memcpy(buf + len, text, n);
	len += n;
	buf[len] = 0;
	return true;
}

bool TextBuffer::Addf(const char* fmt, ...)
{
	if (failed)
		return false;

	va_list a;
	va_start(a, fmt);
	int n = _vscprintf(fmt, a);
	va_end(a);
	if (n < 0)
		return false;

	if (!Reserve(len + size_t(n) + 1))
		return false;

	va_start(a, fmt);
	vsprintf_s(buf + len, size_t(n) + 1, fmt, a);
	va_end(a);
	len += size_t(n);
	return true;
}

}

// ---------------------------------------------------------------------------
//	ダイアログ処理
//
BOOL WinMonitor::DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	bool r = false;
	int sn;
	SCROLLINFO si;

	switch (msg)
	{
	case WM_INITDIALOG:
		SetFont(hdlg, fontheight);
		ResizeWindow(hdlg);
		Theme::ApplyScrollBars(hdlg);		// スクロールバーをテーマに合わせる (M88gn)
		{
			// タイトルバーのメニューに、文字の大きさを変える項目を足す (M88gn)
			HMENU hsys = GetSystemMenu(hdlg, FALSE);
			if (hsys)
			{
				AppendMenu(hsys, MF_SEPARATOR, 0, 0);
				AppendMenu(hsys, MF_STRING, sc_fontlarger, "文字を大きく(&L)\tCtrl++");
				AppendMenu(hsys, MF_STRING, sc_fontsmaller, "文字を小さく(&S)\tCtrl+-");
				AppendMenu(hsys, MF_STRING, sc_fontdefault, "文字の大きさを元に戻す(&D)\tCtrl+0");
				if (inisection)		// ペインにドック (M88gn)
					AddDockMenu(hsys);
			}
		}

		if (timerinterval && !timer)
			timer = SetTimer(hdlg, 1, timerinterval, 0);
		break;

	case WM_ACTIVATE:
		if (wp == WA_INACTIVE)
			EnableStatus(false);
		break;

	case WM_INITMENU:
		{
			HMENU hmenu = (HMENU) wp;
			for (int i = 0; i < nfontsizes; i++)
			{
				CheckMenuItem(hmenu, IDM_MON_FONTSIZE_BASE + i,
					(fontheight == fontsizetable[i]) ? MF_CHECKED : MF_UNCHECKED);
			}
		}
		break;

	case WM_COMMAND:
		{
			UINT id = LOWORD(wp);
			if (IDM_MON_FONTSIZE_BASE <= id && id <= IDM_MON_FONTSIZE_LAST)
			{
				int idx = id - IDM_MON_FONTSIZE_BASE;
				if (idx < nfontsizes)
				{
					fontheight = fontsizetable[idx];
					SetFont(hdlg, fontheight);
					ResizeWindow(hdlg);
					Update();
					r = true;
				}
			}
		}
		break;

	case WM_SYSCOMMAND:
		if (DockSysCommand(wp))		// ペインにドック (M88gn)
			return TRUE;
		switch (wp & 0xfff0)
		{
		case sc_fontlarger:		ChangeFontSize(+1); return TRUE;
		case sc_fontsmaller:	ChangeFontSize(-1); return TRUE;
		case sc_fontdefault:	SetFontSize(fontsizedefault); return TRUE;
		}
		break;

	case WM_DPICHANGED_AFTERPARENT:
		// ペインに入っていて、本体の窓が DPI の違う画面へ移った: 文字をその DPI で作り直す (M88gn)
		SetFont(hdlg, fontheight);
		ResizeWindow(hdlg);
		Update();
		return TRUE;

	case WM_DPICHANGED:
		// DPI の違う画面へ移った: 文字をその DPI で作り直し、Windows の勧める位置・大きさにする (M88gn)
		{
			RECT* rc = (RECT*) lp;
			SetFont(hdlg, fontheight);
			SetWindowPos(hdlg, 0, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
						 SWP_NOZORDER | SWP_NOACTIVATE);
			ResizeWindow(hdlg);
			Update();
		}
		return TRUE;

	case WM_DESTROY:
		// 本体の窓が閉じると、開いたままのモニターの窓は WM_CLOSE 無しで壊れるので、ここでも保存する (M88gn)
		if (hwnd)
			SaveMonitorConfig();
		break;

	case WM_CLOSE:
		SaveMonitorConfig();
		EnableStatus(false);
		if (timer)
			KillTimer(hdlg, timer), timer = 0;

		DestroyWindow(hdlg);
		hwnd = 0;
		r = true;
		break;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hdlg, &ps);
			Draw(hdlg, hdc);
			EndPaint(hdlg, &ps);
		}
		r = true;
		break;

	case WM_TIMER:
		Update();
		return true;

	case WM_SIZE:
		ResizeWindow(hdlg);
		if (hwndstatus)
			PostMessage(hwndstatus, WM_SIZE, wp, lp);
		Update();

		r = true;
		break;

	case WM_VSCROLL:
		line = VerticalScroll(LOWORD(wp));

		memset(&si, 0, sizeof(si));
		si.cbSize = sizeof(si);
		si.fMask = SIF_POS;
		si.nPos = line;
		SetScrollInfo(hdlg, SB_VERT, &si, true);

		Update();
		return true;

	case WM_MOUSEWHEEL:
		if (ProcMouseWheel(wp))
			r = true;
		break;

	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		// デバッグ用ファンクションキーは、どのモニタ窓にフォーカスがあっても
		// 効くようにする。アクセラレータ(TranslateAccelerator)経由だと
		// メニュー項目の状態やアクティブウインドウの状態に左右されて
		// 取りこぼすため、ここで拾ってメインウインドウへ直接投げる
		//
		// ★ F10 は Windows では「メニューバーを開くキー」なので、単独で押すと
		//    WM_KEYDOWN ではなく WM_SYSKEYDOWN で飛んでくる。WM_KEYDOWN しか
		//    見ていないと DefDlgProc へ流れてメニューモードに入ってしまい
		//    (メニュー項目に下線が出る。F10 のたびに ON/OFF する)、
		//    以降のキー入力がメニュー側に食われて効かなくなる。
		//    ここで受け取って TRUE を返し、メニューモードに入れないこと
		if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000))
		{
			if (CopyToClipboard())
				PutStatus("クリップボードにコピーしました.");
			return TRUE;
		}
		// Ctrl++ / Ctrl+- / Ctrl+0 で文字の大きさ (M88gn。日本語のキーボードの + は ; のキー = VK_OEM_PLUS)
		if (GetKeyState(VK_CONTROL) & 0x8000)
		{
			if (wp == VK_OEM_PLUS || wp == VK_ADD)
				return ChangeFontSize(+1), TRUE;
			if (wp == VK_OEM_MINUS || wp == VK_SUBTRACT)
				return ChangeFontSize(-1), TRUE;
			if (wp == '0' || wp == VK_NUMPAD0)
				return SetFontSize(fontsizedefault), TRUE;
		}

		if (hwndowner && !(GetKeyState(VK_MENU) & 0x8000))
		{
			uint id = 0;
			switch (wp)
			{
			case VK_F5:  id = IDM_DBG_PLAY; break;
			case VK_F10: id = IDM_DBG_STEPOVER; break;
			case VK_F11:
				id = (GetKeyState(VK_SHIFT) & 0x8000) ? IDM_DBG_STEPOUT : IDM_DBG_STEPIN;
				break;
			}
			if (id)
			{
				PostMessage(hwndowner, WM_COMMAND, id, 0);
				return TRUE;
			}
		}
		// スクロールは通常のキー入力のときだけ (Alt 併用は対象外)
		if (msg == WM_KEYDOWN && nlines)
		{
			sn = -1;
			switch (wp)
			{
			case VK_UP:		sn = SB_LINEUP; break;
			case VK_PRIOR:	sn = SB_PAGEUP;	break;
			case VK_NEXT:	sn = SB_PAGEDOWN; break;
			case VK_DOWN:	sn = SB_LINEDOWN; break;
			case VK_HOME:	sn = SB_TOP; break;
			case VK_END:	sn = SB_BOTTOM; break;
			}
			if (sn != -1)
				SendMessage(hwnd, WM_VSCROLL, MAKELONG(sn, 0), 0L);
		}
		break;

	case WM_SYSKEYUP:
		// F10 の離した側も捨てる。DefDlgProc に渡すとここでメニューモードへ
		// 入ることがある (WM_SYSKEYDOWN を握り潰しただけでは足りない)
		if (wp == VK_F10 && !(GetKeyState(VK_MENU) & 0x8000))
			return TRUE;
		break;

	case WM_DRAWITEM:
		if ((UINT) wp == 1)
		{
			DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
			SetBkColor(dis->hDC, GetSysColor(COLOR_3DFACE));
//			SetTextColor(dis->hDC, RGB(255, 0, 0));
			char* text = reinterpret_cast<char*>(dis->itemData);
			if (text)
				TextOut(dis->hDC, dis->rcItem.left, dis->rcItem.top, text, strlen(text));
		}
		break;
	}
	return r;
}

BOOL CALLBACK WinMonitor::DlgProcGate
(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
	WinMonitor* winmon = 0;

	if ( m == WM_INITDIALOG ) {
		winmon = reinterpret_cast<WinMonitor*>(l);
		if (winmon) {
			::SetWindowLongPtr( hwnd, GWLP_USERDATA, (LONG_PTR)winmon );
		}
	} else {
		winmon = (WinMonitor*)::GetWindowLongPtr( hwnd, GWLP_USERDATA );
	}

	if (winmon) {
		return winmon->DlgProc(hwnd, m, w, l);
	} else {
		return FALSE;
	}
}
