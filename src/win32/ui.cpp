// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1998, 1999.
// ---------------------------------------------------------------------------
//	User Interface for Win32
// ---------------------------------------------------------------------------
//	$Id: ui.cpp,v 1.62 2003/09/28 14:35:35 cisc Exp $

#include "headers.h"
#include <shellapi.h>
#include <mbstring.h>
#include "resource.h"
#include "ui.h"
#include "dbgbuttons.h"
#include "uitheme.h"
#include "videorec.h"
#include "dpi.h"

#ifndef WM_DPICHANGED
#define WM_DPICHANGED	0x02e0
#endif
#ifndef WM_GETDPISCALEDSIZE
#define WM_GETDPISCALEDSIZE	0x02e4
#endif
#include "about.h"
#include "misc.h"
#include "file.h"
#include "messages.h"
#include "error.h"
#include "88config.h"
#include "status.h"
#include "pc88/opnif.h"
#include "pc88/diskmgr.h"
#include "pc88/tapemgr.h"
#include "filetest.h"
#include "winvars.h"
#include "winexapi.h"

#define LOGNAME "ui"
#include "diag.h"

extern char m88dir[MAX_PATH];
extern char m88ini[MAX_PATH];

using namespace PC8801;


// ---------------------------------------------------------------------------
//	WinUI 
//	生成・破棄
//
WinUI::WinUI(HINSTANCE hinstance)
: hinst(hinstance), hwnd(0), diskmgr(0), tapemgr(0),
  fullscreen(false)
{
	timerid = 0;
	point.x = point.y = 0;
//	resizewindow = 0;
	displaychangedtime = GetTickCount();
	report = true;

	diskinfo[0].hmenu = 0;
	diskinfo[0].filename[0] = 0;
	diskinfo[0].idchgdisk = IDM_CHANGEDISK_1;
	diskinfo[1].hmenu = 0;
	diskinfo[1].filename[0] = 0;
	diskinfo[1].idchgdisk = IDM_CHANGEDISK_2;
	capturemouse = true;
	hmenumain = 0;
	resetwindowsize = 0;
	titlehot = titlepressed = -1;
	titlepressedwas = false;
	titlemenuopen = titlenext = -1;
	titlenextkey = false;
	titlemenusel = 0;
	titlemenuselflags = 0;
	titleactive = true;
	titlemenuend = titlebuttonsleft = 0;
	titlefont = titleglyph = titlebold = 0;
	titlefontdpi = 0;
	hmenuss[0] = 0;
	hmenuss[1] = 0;
	currentsnapshot = 0;
	snapshotchanged = true;

	clipmode = 0;
	hoverlay = 0;
	overlayfont = 0;
	overlaydpi = 0;
	overlaybrush = 0;
	background = false;
	mousebutton = 0;
}

WinUI::~WinUI()
{
	if (overlayfont)
		DeleteObject(overlayfont);
	if (overlaybrush)
		DeleteObject(overlaybrush);
	delete diskmgr;
	delete tapemgr;
}

// ---------------------------------------------------------------------------
//	WinUI::InitM88
//	M88 の初期化
//
bool WinUI::InitM88(const char* cmdline)
{
	active = false;
	tapetitle[0] = 0;
	appliedrot = 0;
	dpimoved = false;
	
	//	設定読み込み
	LOG1("%d\tLoadConfig\n", timeGetTime());
	PC8801::LoadConfig(&config, m88ini, true);
	// 表示の倍率が決まっていなければ、画面の DPI から決める (表示スケール 150% → 2 倍)
	if (config.screenscale <= 0)
		config.screenscale = Limit(int(Dpi::GetForWindow(hwnd) + 48) / 96, 4, 1);
	scalefree = GetPrivateProfileInt("M88p2 for Windows", "ScreenScaleFree", 0, PC8801::GetM88IniPath()) != 0;
	SetScaleFlag();

	// ステータスバー初期化
	statusdisplay.Init(hwnd);

	// Window位置復元
	ResizeWindow(640, 400);
	LoadWindowPosition();
	{
		RECT rect;
		GetWindowRect(hwnd, &rect);
		point.x = rect.left; point.y = rect.top;
	}

	// 画面初期化
	M88ChangeDisplay(hwnd, 0, 0);

	//	現在の path 保存
	char path[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, path);

	//	デバイスの初期化
	PC8801::LoadConfigDirectory(&config, m88ini, "BIOSPath", true);
	
	LOG1("%d\tdiskmanager\n", timeGetTime());
	if (!diskmgr)
		diskmgr = new DiskManager;
	if (!diskmgr || !diskmgr->Init(&core))
		return false;
	if (!tapemgr)
		tapemgr = new TapeManager;
	if (!tapemgr)
		return false;

	LOG1("%d\tkeyboard if\n", timeGetTime());
	if (!keyif.Init(hwnd))
		return false;
	softkbd.Init(hinst, hwnd, &keyif);
	LOG1("%d\tcore\n", timeGetTime());
	if (!core.Init(this, hwnd, &draw, diskmgr, &keyif, &winconfig, tapemgr))
		return false;
	tapemgr->SetNotify(TapeNotify, this);
	tapewin.Init(hinst, hwnd, &core);
	tapewin.SetTape(tapemgr);

	
	//	debug 用クラス初期化
	LOG1("%d\tmonitors\n", timeGetTime());
	opnmon.Init(core.GetOPN1(), core.GetSound());
	memmon.Init(&core);
	codemon.Init(&core);
	basmon.Init(&core);
	regmon.Init(&core);
	loadmon.Init();
	iomon.Init(&core);
	bpmon.Init(&core);
	iobpmon.Init(&core, true);
	watchmon.Init(&core);
	tracemon.Init(&core);
	mnemmon.Init();
	statmon.Init(&core);
	lapmon.Init(&core);
	lblmon.Init(&core);
	PC8801::SetLabelCore(&core);		// ラベルを窓のあちこちで使う (M88gn)
	lblmon.SetMonitors(&codemon, &memmon);
	bpmon.SetMonitors(&codemon, &memmon);	// 右クリックで番地へ (M88gn)
	watchmon.SetMonitors(&codemon, &memmon);
	sublogmon.Init(&core);
	videomon.Init(&core, &memmon);
	stoptick = 0;
	stoptickvalid = false;
	laptext[0] = 0;
	codemon.SetMnemonicMonitor(&mnemmon);
	codemon.SetMemoryMonitor(&memmon);
	memmon.SetCodeMonitor(&codemon);
	regmon.SetMonitors(&codemon, &memmon);	// 右クリックで番地へ (M88gn)
	debugnodiskwarned = false;
	debugkey[0] = 0;
	tapefile[0] = 0;
	core.GetSound()->SetSoundMonitor(&opnmon);
	core.GetSound()->SetCapture(VideoAudioCb, 0);		// 録画に音を渡す (M88gn)

	//	実行ファイル改変チェック
	LOG1("%d\tself test\n", timeGetTime());
	if (!SanityCheck())
		return false;

	//	エミュレーション開始
	LOG1("%d\temulation begin\n", timeGetTime());
	core.Wait(false);
	active = true;
	fullscreen = false;
	exec = true;

	//	設定反映
	LOG1("%d\tapply cmdline\n", timeGetTime());
	SetCurrentDirectory(path);
	ApplyCommandLine(cmdline);
	LOG1("%d\tapply config\n", timeGetTime());
	ApplyConfig();
	// 1.1.0 までの M88.ini の値は、DPI 非対応の窓の値 (96 DPI 換算)。今の画面の DPI に直す
	if (config.softkbdw > 0 && config.softkbddpi == 0 && Dpi::IsAware())
	{
		int d = Dpi::GetForWindow(hwnd);
		config.softkbdx = MulDiv(config.softkbdx, d, 96);
		config.softkbdy = MulDiv(config.softkbdy, d, 96);
		config.softkbdw = MulDiv(config.softkbdw, d, 96);
	}
	softkbd.SetPlacement(config.softkbdx, config.softkbdy, config.softkbdw);
	if (config.softkbdshow)
		softkbd.Show(true);
	tapewin.SetPlacement(config.tapewinx, config.tapewiny, config.tapewinw, config.tapewinh);
	if (config.tapewinshow)
		tapewin.Show(true);
	
	//	リセット
	LOG1("%d\treset\n", timeGetTime());
	core.Reset();

	// あとごちゃごちゃしたもの
	LOG1("%d\tetc\n", timeGetTime());
	if (!diskinfo[0].filename[0])
		PC8801::LoadConfigDirectory(&config, m88ini, "Directory", false);
	
	LOG1("%d\tend initm88\n", timeGetTime());
	return true;
}

// ---------------------------------------------------------------------------
//	WinUI::CleanupM88
//	M88 の後片付け
//
void WinUI::CleanupM88()
{
	// 今のイメージ (ドライブ 1 のディスクかテープ) のデバッガの設定を保存する (Save Settings per Disk/Tape のとき)
	SyncDebugSettings(debugkey, "");

	softkbd.GetPlacement(&config.softkbdx, &config.softkbdy, &config.softkbdw);
	config.softkbddpi = Dpi::IsAware() ? Dpi::GetForWindow(hwnd) : 0;	// 値は物理ピクセル
	config.softkbdshow = softkbd.IsShown();
	tapewin.GetPlacement(&config.tapewinx, &config.tapewiny, &config.tapewinw, &config.tapewinh);
	config.tapewinshow = tapewin.IsShown();
	PC8801::Config cfg = config;
	PC8801::SaveConfig(&cfg, m88ini, true);
	core.Cleanup();
	delete diskmgr; diskmgr = 0;
	delete tapemgr; tapemgr = 0;
}

// ---------------------------------------------------------------------------
//	ドックモード (M88gn)
//
WinUI* WinUI::dockui = 0;

// 画面を置く四角が変わった (r が 0 なら窓いっぱい)
void WinUI::DockViewCb(void* ctx, const RECT* r)
{
	WinUI* ui = (WinUI*) ctx;
	if (!ui->dockactive && !ui->fullscreen)
		return;
	// ペインを出している間は、画面を枠の分だけ内側に置き、周りに枠を描く (M88gn)
	bool had = ui->viewframe;
	ui->viewframe = false;
	RECT in;
	if (r && !ui->fullscreen && ui->dock.VisibleBits())
	{
		int b = Max(2, int(Dpi::GetForWindow(ui->hwnd)) * 4 / 96);
		in = *r;
		InflateRect(&in, -b, -b);
		if (in.right > in.left && in.bottom > in.top)
		{
			ui->viewframe = true;
			ui->viewframerect = *r;
			ui->viewframewidth = b;
			r = &in;
		}
	}
	ui->draw.SetViewRect(r);
	if (ui->viewframe)
		ui->PaintViewFrame(GetFocus() == ui->hwnd);
	else if (had)
		InvalidateRect(ui->hwnd, 0, FALSE);
	if (r || ui->fullscreen)
		ui->draw.Resize(640, 400);
	ui->UpdateScaleFree();		// 窓の大きさ・ペイン・仕切りを動かした (M88gn)
}

// 窓のタイトルバーのメニューの「ドック」
void WinUI::DockHook(DockClient* mon, int side)
{
	if (!dockui || dockui->fullscreen)
		return;
	if (side >= 4)
		dockui->dock.Dock(mon, side - 4, DockHost::newgroup);	// 新しい区画に
	else
		dockui->dock.Dock(mon, side);
}

// 窓を開いた: 前にペインにあったなら、そのペインへ
void WinUI::DockShowHook(DockClient* mon)
{
	if (dockui)
		dockui->dock.OnShown(mon);
}

// 開いているペインの組が変わった (M88gn)。窓の大きさは変えず、画面が残りの所に合わせて広がる・縮む
void WinUI::DockChangeCb(void* ctx, int oldbits, int newbits)
{
	((WinUI*) ctx)->DockChanged(oldbits, newbits);
}

void WinUI::DockChanged(int, int)
{
	if (fullscreen || !hwnd)
		return;
	if (!dockactive)
		EnterLayoutMode();
}

// 窓の大きさを自由に変えられる形にする (起動してペインを作った後。M88gn)
void WinUI::EnterLayoutMode()
{
	if (dockactive || fullscreen || !hwnd)
		return;
	layoutguard++;
	dockactive = true;
	// 今の形を読み直す (wstyle には WS_VISIBLE が無いので、そのまま書くと窓が見えなくなる)
	wstyle = (DWORD) GetWindowLongPtr(hwnd, GWL_STYLE) | WS_THICKFRAME | WS_MAXIMIZEBOX;
	SetWindowLongPtr(hwnd, GWL_STYLE, wstyle);
	draw.ChangeDisplayMode(true, (config.flags & PC8801::Config::force480) != 0, Limit(config.screenscale, 4, 1));
	SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	dock.Layout();
	layoutguard--;
}

// 画面が表示の倍率どおりの大きさでなくなっていたら (窓の大きさを変えた・ペインを開閉した・仕切りを動かした)、Free にする
void WinUI::UpdateScaleFree()
{
	if (scalefree || layoutguard || !dockactive || fullscreen || !hwnd || IsZoomed(hwnd) || IsIconic(hwnd))
		return;
	uint w, h;
	GetClientSize(w, h);
	RECT v;
	dock.GetView(&v);
	if (v.right - v.left != int(w) || v.bottom - v.top != int(h))
		scalefree = true;
}

// ---------------------------------------------------------------------------
//	メニューバーの右の端のペインの開閉ボタン (M88gn)。ドックモードで、全画面でない間だけ出す。
//	並びは左・上・下・右。ペインの開閉・窓の有無が変わったら描き直す
//
void WinUI::UpdateDockButtons()
{
	int state = 0;
	for (int s = 0; s < 4; s++)
		state |= ((dock.IsPaneShown(s) ? 1 : 0) | (dock.HasWindows(s) ? 2 : 0)) << (s * 2);
	if (state == dockbtnstate)
		return;
	dockbtnstate = state;
	InvalidateTitle();
}

// 子の窓のクリック: ドックモードで PC-8801 の画面の上なら、キー入力を本体の窓に戻す
LRESULT WinUI::WmParentNotify(HWND hwnd, WPARAM wp, LPARAM lp)
{
	UINT ev = LOWORD(wp);
	if (dockactive && (ev == WM_LBUTTONDOWN || ev == WM_RBUTTONDOWN || ev == WM_MBUTTONDOWN))
	{
		POINT pt = { (short) LOWORD(lp), (short) HIWORD(lp) };
		if (dock.InView(pt))
			SetFocus(hwnd);
		else
		{
			// ペインに入れた窓の上なら、その窓にキー入力を移す (M88gn)。テープの窓は入力を取らない作りなので除く
			POINT sp = pt;
			ClientToScreen(hwnd, &sp);
			for (HWND w = WindowFromPoint(sp); w && w != hwnd; w = GetParent(w))
			{
				DockClient* c = DockClient::FindByWindow(w);
				if (c)
				{
					const char* sec = c->DockSection();
					if (c->IsDocked() && !(sec && !lstrcmp(sec, "TapeWindow")) && GetFocus() != w && !IsChild(w, GetFocus()))
						SetFocus(w);
					break;
				}
			}
		}
	}
	return DefWindowProc(hwnd, WM_PARENTNOTIFY, wp, lp);
}

// ---------------------------------------------------------------------------
//	WinUI::InitWindow
//	M88 の窓作成
//
bool WinUI::InitWindow(int nwinmode)
{
	WNDCLASS wcl;
	static const char* szwinname = "M88p2 WinUI";
 
	wcl.hInstance = hinst;
	wcl.lpszClassName = szwinname;
	wcl.lpfnWndProc = WNDPROC((void*)(WinProcGate));
	wcl.style = 0;
	wcl.hIcon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_ICON_M88));
	wcl.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcl.cbClsExtra = 0;
	wcl.cbWndExtra = 0;
//	wcl.hbrBackground = (HBRUSH) GetStockObject(BLACK_BRUSH);
	wcl.hbrBackground = (HBRUSH) GetStockObject(NULL_BRUSH);
	wcl.lpszMenuName = MAKEINTRESOURCE(IDR_MENU_M88);

	accel = LoadAccelerators(hinst, MAKEINTRESOURCE(IDR_ACC_M88UI));
	accelnoalt = LoadAccelerators(hinst, MAKEINTRESOURCE(IDR_ACC_M88UI_NOALT));
	appswallow = 0;
	debugswallow = 0;

	if (!RegisterClass(&wcl)) 
		return false;

	wstyle = WS_CAPTION | WS_OVERLAPPED | WS_SYSMENU | WS_MINIMIZEBOX;

	hwnd = CreateWindowEx(
		WS_EX_ACCEPTFILES,// | WS_EX_LAYERED,
		szwinname,
		"M88",
		wstyle,
		CW_USEDEFAULT,		// x
		CW_USEDEFAULT,		// y
		640,				// w
		400,				// h
		NULL,
		NULL,
		hinst,
		(LPVOID)this
		);
	hmenumain = hwnd ? GetMenu(hwnd) : 0;
	if (hwnd)
		SetProp(hwnd, "M88gnMainMenu", (HANDLE) hmenumain);	// デバッグの窓が本体のメニューの字を読むため (M88gn)
	if (hwnd)
	{
		// タイトルの段 (M88gn): メニューバーは使わず、Windows のタイトルバーを無くす
		SetMenu(hwnd, 0);
		SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}
	// ドックモード (M88gn)
	dockui = this;
	SetRectEmpty(&dockrect);
	dockbtnstate = -1;
	Theme::Load();		// テーマ (M88gn)
	dock.Init(hwnd, hinst);
	dock.SetViewCallback(DockViewCb, this);
	dock.SetChangeCallback(DockChangeCb, this);
	dockactive = false;
	viewframe = false;
	viewframewidth = 0;
	layoutguard = 0;
	scalefree = false;
	DockClient::dockhook = DockHook;
	DockClient::showhook = DockShowHook;

//	SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0xc0, LWA_ALPHA);
	
	if (!draw.Init0(hwnd))
		return false;

	clipmode = 0;
	guimodebymouse = false;
	cursorhidden = false;
	cursorpos.x = cursorpos.y = -1;

	return true;
}

// ---------------------------------------------------------------------------
// ウインドウ位置の保存/復帰
//
void WinUI::SaveWindowPosition()
{
	WINDOWPLACEMENT wp; 
	wp.length = sizeof(WINDOWPLACEMENT);
	::GetWindowPlacement( hwnd, &wp );
	config.winposx = wp.rcNormalPosition.left;
	config.winposy = wp.rcNormalPosition.top;
	// Window scale の Free と、そのときの窓の大きさ (M88gn。全画面の間は全画面の前の大きさ)
	const char* ini = PC8801::GetM88IniPath();
	WritePrivateProfileString("M88p2 for Windows", "ScreenScaleFree", scalefree ? "1" : "0", ini);
	RECT nr = wp.rcNormalPosition;
	if (fullscreen && dockrect.right > dockrect.left)
		nr = dockrect;
	if (nr.right > nr.left && nr.bottom > nr.top)
	{
		char buf[16];
		wsprintf(buf, "%d", int(nr.right - nr.left));	WritePrivateProfileString("M88p2 for Windows", "WindowWidth", buf, ini);
		wsprintf(buf, "%d", int(nr.bottom - nr.top));	WritePrivateProfileString("M88p2 for Windows", "WindowHeight", buf, ini);
	}
}

void WinUI::LoadWindowPosition()
{
	if (config.flag2 & Config::saveposition) {
		WINDOWPLACEMENT wp;
	    wp.length = sizeof(WINDOWPLACEMENT);
		::GetWindowPlacement( hwnd, &wp );

		LONG winw, winh;
		winw = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
		winh = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;

		wp.rcNormalPosition.top = config.winposy;
		wp.rcNormalPosition.bottom = config.winposy + winh;
		wp.rcNormalPosition.left = config.winposx;
		wp.rcNormalPosition.right = config.winposx + winw;
		SetWindowPlacement( hwnd, &wp );
	}
}

// ---------------------------------------------------------------------------
//	WinUI::Main
//	メッセージループ
//
int WinUI::Main(const char* cmdline)
{
	hmenudbg = 0;
	if (InitM88(cmdline))
	{
		timerid = ::SetTimer(hwnd, 1, 1000, 0);
		::SetTimer(hwnd, 2, 100, 0);

		ShowWindow(hwnd, SW_SHOWDEFAULT);
		UpdateWindow(hwnd);
		::SetTimer(hwnd, 13, 200, 0);		// ドックモードのタブ・メニューの帯 (M88gn)
		layoutguard++;
		dock.SetEnabled(true);			// ペイン (M88gn)。前にペインにあった窓を開いて入れる
		EnterLayoutMode();
		dock.LoadLayout();
		// 窓の大きさ: Free なら前の大きさ、そうでなければ画面が倍率どおりになる大きさ (ペインの分も足す)
		{
			const char* ini = PC8801::GetM88IniPath();
			int ww = GetPrivateProfileInt("M88p2 for Windows", "WindowWidth", 0, ini);
			int wh = GetPrivateProfileInt("M88p2 for Windows", "WindowHeight", 0, ini);
			if (scalefree && ww > 0 && wh > 0)
				SetWindowPos(hwnd, 0, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			else
				ResizeWindow(640, 400);
		}
		layoutguard--;
		UpdateDockButtons();
//		core.ActivateMouse(true);
	}
	else
	{
		ReportError();
		PostQuitMessage(1);
	}

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		if (winconfig.ProcMsg(msg))
		{
			if (!winconfig.IsOpen())
				SetGUIFlag(false);
			continue;
		}

		// ショートカット: Alt (Windows のアクセラレーター) か、アプリケーションキー (AppShortcut)
		if (AppShortcut(msg) || DebugKey(msg))
			continue;
		if (!TranslateAccelerator(msg.hwnd, (config.flag2 & Config::appshortcut) ? accelnoalt : accel, &msg))
		{
			if ((msg.message == WM_SYSKEYDOWN || msg.message == WM_SYSKEYUP)
				&& !(config.flags & Config::suppressmenu))
				TranslateMessage(&msg);
		}
		DispatchMessage(&msg);
	}

	OPNIF* opn = core.GetOPN1();
	if (opn)
		opn->Reset();
	CleanupM88();
	statusdisplay.Cleanup();
	return (int)msg.wParam;
}

// ---------------------------------------------------------------------------
//	WinUI::WinProc
//	メッセージハンドラ
//
#define PROC_MSG(msg, func)		case (msg): ret = (func)(hwnd, wp, lp); break

LRESULT WinUI::WinProc(HWND hwnd, UINT umsg, WPARAM wp, LPARAM lp)
{
	LRESULT ret;
	if (TitleMessage(hwnd, umsg, wp, lp, &ret))		// タイトルの段 (M88gn)
		return ret;
	keyif.Disable(true);

	switch (umsg)
	{
	PROC_MSG(WM_COMMAND,			WmCommand);
	PROC_MSG(WM_PAINT,				WmPaint);
	PROC_MSG(WM_CREATE,				WmCreate);
	PROC_MSG(WM_DESTROY,			WmDestroy);
	PROC_MSG(WM_CLOSE,				WmClose);
	PROC_MSG(WM_TIMER,				WmTimer);
	PROC_MSG(WM_ACTIVATE,			WmActivate);
	PROC_MSG(WM_PALETTECHANGED,		WmPaletteChanged);
	PROC_MSG(WM_QUERYNEWPALETTE,	WmQueryNewPalette);
	PROC_MSG(WM_INITMENU,			WmInitMenu);
	PROC_MSG(WM_KEYUP,				WmKeyUp);
	PROC_MSG(WM_KEYDOWN,			WmKeyDown);
	PROC_MSG(WM_SYSKEYUP,			WmSysKeyUp);
	PROC_MSG(WM_SYSKEYDOWN,			WmSysKeyDown);
	PROC_MSG(WM_SIZE,				WmSize);
	PROC_MSG(WM_DPICHANGED,			WmDpiChanged);
	PROC_MSG(WM_GETDPISCALEDSIZE,	WmGetDpiScaledSize);
	PROC_MSG(WM_MOVE,				WmMove);
	PROC_MSG(WM_DRAWITEM,			WmDrawItem);
	PROC_MSG(WM_MEASUREITEM,		WmMeasureItem);
	PROC_MSG(WM_ENTERMENULOOP,		WmEnterMenuLoop);
	PROC_MSG(WM_EXITMENULOOP,		WmExitMenuLoop);
	PROC_MSG(WM_DISPLAYCHANGE,		WmDisplayChange);
	PROC_MSG(WM_DROPFILES,			WmDropFiles);
	PROC_MSG(WM_LBUTTONDOWN,		WmLButtonDown);
	PROC_MSG(WM_LBUTTONUP,			WmLButtonUp);
	PROC_MSG(WM_RBUTTONDOWN,		WmRButtonDown);
	PROC_MSG(WM_RBUTTONUP,			WmRButtonUp);
	PROC_MSG(WM_CONTEXTMENU,		WmContextMenu);
	PROC_MSG(WM_ENTERSIZEMOVE,		WmEnterSizeMove);
	PROC_MSG(WM_EXITSIZEMOVE,		WmExitSizeMove);
	PROC_MSG(WM_M88_SENDKEYSTATE,	M88SendKeyState);
	PROC_MSG(WM_M88_APPLYCONFIG,	M88ApplyConfig);
	PROC_MSG(WM_M88_CHANGEDISPLAY,	M88ChangeDisplay);
	PROC_MSG(WM_M88_CHANGEVOLUME,	M88ChangeVolume);
	PROC_MSG(WM_M88_CLIPCURSOR,		M88ClipCursor);
	PROC_MSG(WM_M88_TAPERECORD,		M88TapeNotify);
	PROC_MSG(WM_M88_DEBUGBREAK,		M88DebugBreak);
	PROC_MSG(WM_M88_STATUSMENU,		M88StatusMenu);
	case WM_CTLCOLORSTATIC:
		// 知らせの色 (黄色の地に黒い字)
		if (hoverlay && (HWND) lp == hoverlay)
		{
			SetTextColor((HDC) wp, RGB(0, 0, 0));
			SetBkColor((HDC) wp, RGB(255, 236, 140));
			ret = (LRESULT) overlaybrush;
		}
		else
			ret = DefWindowProc(hwnd, umsg, wp, lp);
		break;
	PROC_MSG(WM_MOUSEMOVE,			WmMouseMove);
	PROC_MSG(WM_SETCURSOR,			WmSetCursor);
	PROC_MSG(WM_PARENTNOTIFY,		WmParentNotify);
	PROC_MSG(WM_SETFOCUS,			WmSetFocus);
	PROC_MSG(WM_KILLFOCUS,			WmKillFocus);
	
	default:
		ret = DefWindowProc(hwnd, umsg, wp, lp);
	}
	keyif.Disable(false);
	return ret;
}

LRESULT CALLBACK WinUI::WinProcGate(HWND hwnd, UINT umsg, WPARAM wparam, LPARAM lparam)
{
	WinUI *ui;

	if( umsg == WM_CREATE ) {
		ui = (WinUI*)((LPCREATESTRUCT)lparam)->lpCreateParams;
		if (ui) {
			::SetWindowLongPtr( hwnd, GWLP_USERDATA, (LONG_PTR)ui );
		}
	} else {
		ui = (WinUI*)::GetWindowLongPtr( hwnd, GWLP_USERDATA );
	}

	if (ui) {
		return ui->WinProc(hwnd, umsg, wparam, lparam);
	} else {
		return ::DefWindowProc( hwnd, umsg, wparam, lparam );
	}
}

// ---------------------------------------------------------------------------
//	WinUI::M88SendKeyState
//
inline LRESULT WinUI::M88SendKeyState(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	uint8* dest = reinterpret_cast<uint8*>(wparam);
	GetKeyboardState(dest);
	SetEvent((HANDLE) lparam);
	return 0;
}

inline LRESULT WinUI::WmKeyDown(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	if ((uint) wparam == VK_F12 && !(config.flags & Config::disablef12reset))
		;
	else
		keyif.KeyDown((uint) wparam, (uint32) lparam);
	
	return 0;
}

inline LRESULT WinUI::WmKeyUp(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	if ((uint) wparam == VK_F12 && !(config.flags & Config::disablef12reset))
		Reset();
	else
		keyif.KeyUp((uint) wparam, (uint32) lparam);
	
	return 0;
}

inline LRESULT WinUI::WmSysKeyDown(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	if (config.flags & Config::suppressmenu)
	{
		keyif.KeyDown((uint) wparam, (uint32) lparam);
		return 0;
	}
	return DefWindowProc(hwnd, WM_SYSKEYDOWN, wparam, lparam);
}


inline LRESULT WinUI::WmSysKeyUp(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	if (config.flags & Config::suppressmenu)
	{
		keyif.KeyUp((uint) wparam, (uint32) lparam);
		return 0;
	}
	return DefWindowProc(hwnd, WM_SYSKEYUP, wparam, lparam);
}

// ---------------------------------------------------------------------------
//	WinUI::WmActivate
//	WM_ACTIVATE ハンドラ
//
LRESULT WinUI::WmActivate(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	bool prevbg = background;
	background = LOWORD(wparam) == WA_INACTIVE;
	
	if (!HIWORD(wparam))
	{
		draw.RequestPaint();
	}
	
	keyif.Activate(!background);
	draw.QueryNewPalette(background);
	if (prevbg != background)
	{
//		core.ActivateMouse(!background);
		M88ClipCursor(hwnd, background ? CLIPCURSOR_RELEASE : -CLIPCURSOR_RELEASE, 0);
		draw.SetGUIFlag(background);
	}
	snapshotchanged = true;
	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::WmQueryNewPalette
//	WM_QUERYNEWPALETTE ハンドラ
//
LRESULT WinUI::WmQueryNewPalette(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	draw.QueryNewPalette(background);
	return 1;
}

// ---------------------------------------------------------------------------
//	WinUI::WmPaletteChanged
//	WM_PALETTECHANGED ハンドラ
//
LRESULT WinUI::WmPaletteChanged(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	if ((HWND) wparam != hwnd)
	{
		draw.QueryNewPalette(background);
		return 1;
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::WmCommand
//	WM_COMMAND ハンドラ
//
LRESULT WinUI::WmCommand(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	uint wid = LOWORD(wparam);
	switch (wid)
	{
	case IDM_EXIT:
		PostMessage(hwnd, WM_CLOSE, 0, 0);
		break;

	case IDM_RESET:
		Reset();
		break;

	case IDM_CPU_BURST:
		config.flags ^= Config::cpuburst;
		if (config.flags & Config::cpuburst)
			config.flags &= ~Config::fullspeed;
		ApplyConfig();
		break;

	case IDM_4MHZ:
		this->config.clock = 40;
		Reset();
		break;

	case IDM_8MHZ:
		this->config.clock = 80;
		Reset();
		break;

	case IDM_N88V1:
		config.basicmode = Config::N88V1;
		Reset();
		break;

	case IDM_N88V1H:
		config.basicmode = Config::N88V1H;
		Reset();
		break;

	case IDM_N88V2:
		config.basicmode = Config::N88V2;
		Reset();
		break;

	case IDM_N88V2CD:
		config.basicmode = Config::N88V2CD;
		Reset();
		break;

	case IDM_NMODE:
		config.basicmode = Config::N80;
		Reset();
		break;

	case IDM_N80MODE:
		config.basicmode = Config::N802;
		Reset();
		break;

	case IDM_N80V2MODE:
		config.basicmode = Config::N80V2;
		Reset();
		break;

	case IDM_DRIVE_1:
	case IDM_CHANGEIMAGE_1:
		ChangeDiskImage(hwnd, 0);
		break;

	case IDM_DRIVE_2:
	case IDM_CHANGEIMAGE_2:
		ChangeDiskImage(hwnd, 1);
		break;

	case IDM_NEWDISK_1:
		NewDiskImage(0);
		break;

	case IDM_NEWDISK_2:
		NewDiskImage(1);
		break;

	case IDM_NEWDISK_NONE:
		NewDiskImage(-1);
		break;

	case IDM_TAPE_NEW:
		NewTapeImage();
		break;

	case IDM_BOTHDRIVE:
		if (DiskMounted(1))
			core.FDDMechSound(false);
		diskmgr->Unmount(1);
		OpenDiskImage(1, 0, 0, 0, false);
		ChangeDiskImage(hwnd, 0);
		break;

	case IDM_ABOUTM88:
		SetGUIFlag(true);
		M88About().Show(hinst, hwnd);
		SetGUIFlag(false);
		break;

	case IDM_CONFIG:
		SetGUIFlag(true);
		winconfig.Show(hinst, hwnd, &config);
		break;

	case IDM_TOGGLEDISPLAY:
		ToggleDisplayMode();
		break;

	case IDM_SHOWMENU:
		// キーボードでメニューを出す (メニューバーを隠していると、ほかにはマウスの右クリックしかない)。
		// 窓の中の左上 1/3 の所に出す (サブメニューが右下に開けるように)。項目は ↓ や下線の字で選ぶ
		if (TitleActive() && !HideMenuBar())
		{
			int first = TitleMenuByMnemonic('c');
			PostMessage(hwnd, WM_M88_TITLEMENU, first >= 0 ? first : 0, 1);
		}
		else
		{
			RECT r;
			GetClientRect(hwnd, &r);
			POINT pt = { r.right / 3, r.bottom / 3 };
			ClientToScreen(hwnd, &pt);
			ShowFullScreenMenu(pt.x, pt.y);
		}
		break;

	case IDM_FULLHIDEMENU:
		config.fullhidemenu = !config.fullhidemenu;
		if (fullscreen)
			ApplyFullScreenMenu();
		break;

	case IDM_WINHIDEMENU:
		config.winhidemenu = !config.winhidemenu;
		if (!fullscreen)
			ShowStatusWindow();			// メニューバーを出す・隠して、窓の大きさを直す
		break;

	case IDM_SCALE_1:
	case IDM_SCALE_2:
	case IDM_SCALE_3:
	case IDM_SCALE_4:
		config.screenscale = (wid == IDM_SCALE_4) ? 4 : wid - IDM_SCALE_1 + 1;	// x4 は番号が続いていない
		scalefree = false;
		SetScaleFlag();
		if (!fullscreen)
		{
			if (dockactive)
			{
				if (IsZoomed(hwnd))
					ShowWindow(hwnd, SW_RESTORE);
				ResizeWindow(640, 400);		// 画面が倍率どおりになる大きさに (ペインの分も足す。M88gn)
			}
			else
				ChangeDisplayType(true);	// 全画面のときは、ウィンドウに戻ったときに効く
		}
		break;

	case IDM_SCALE_FREE:		// 窓の大きさを自由に (M88gn)。今の大きさのまま
		scalefree = true;
		break;

	case IDM_ROTATE_0:
	case IDM_ROTATE_90:
	case IDM_ROTATE_180:
	case IDM_ROTATE_270:
		config.screenrotate = wid - IDM_ROTATE_0;
		SetCrtFilter();
		break;

	case IDM_CRTSCAN_0:
	case IDM_CRTSCAN_1:
	case IDM_CRTSCAN_2:
	case IDM_CRTSCAN_3:
		config.crtscan = wid - IDM_CRTSCAN_0;
		SetCrtFilter();
		break;

	case IDM_CRTSHARP_0:
	case IDM_CRTSHARP_1:
	case IDM_CRTSHARP_2:
		config.crtsharp = wid - IDM_CRTSHARP_0;
		SetCrtFilter();
		break;

	case IDM_CRTBREATH_0:
	case IDM_CRTBREATH_1:
	case IDM_CRTBREATH_2:
	case IDM_CRTBREATH_3:
		config.crtbreath = wid - IDM_CRTBREATH_0;
		SetCrtFilter();
		break;

	case IDM_CRTBREATH_EXPAND:
		config.crtbreathexp = !config.crtbreathexp;
		SetCrtFilter();
		break;

	case IDM_CRTVIGNETTE_0:
	case IDM_CRTVIGNETTE_1:
	case IDM_CRTVIGNETTE_2:
	case IDM_CRTVIGNETTE_3:
		config.crtvignette = wid - IDM_CRTVIGNETTE_0;
		SetCrtFilter();
		break;

	case IDM_CRTCBLUR_0:
	case IDM_CRTCBLUR_1:
	case IDM_CRTCBLUR_2:
	case IDM_CRTCBLUR_3:
		config.crtcornerblur = wid - IDM_CRTCBLUR_0;
		SetCrtFilter();
		break;

	case IDM_CRTGLOW_0:
	case IDM_CRTGLOW_1:
	case IDM_CRTGLOW_2:
	case IDM_CRTGLOW_3:
		config.crtglow = wid - IDM_CRTGLOW_0;
		SetCrtFilter();
		break;

	case IDM_CRTBRIGHT_0:
	case IDM_CRTBRIGHT_1:
	case IDM_CRTBRIGHT_2:
		config.crtbright = wid - IDM_CRTBRIGHT_0;
		SetCrtFilter();
		break;

	case IDM_CRTMASK_0:
	case IDM_CRTMASK_1:
	case IDM_CRTMASK_2:
	case IDM_CRTMASK_3:
	case IDM_CRTMASK_4:
		config.crtmask = (wid == IDM_CRTMASK_4) ? 4 : wid - IDM_CRTMASK_0;
		SetCrtFilter();
		break;

	case IDM_CRTDIRECT:
	case IDM_DISP_CRT:
	case IDM_DISP_PLASMA:
	case IDM_DISP_GREEN:
	case IDM_DISP_WHITE:
	case IDM_DISP_AMBER:
	case IDM_DISP_DIGITAL:
		// 表示の型 (Screen → Display)。CrtDirect は前の版のために合わせて書く
		config.screendisplay = (wid == IDM_CRTDIRECT) ? Config::disp_direct : (wid == IDM_DISP_PLASMA) ? Config::disp_plasma
							 : (wid == IDM_DISP_GREEN) ? Config::disp_green : (wid == IDM_DISP_WHITE) ? Config::disp_white
							 : (wid == IDM_DISP_AMBER) ? Config::disp_amber : (wid == IDM_DISP_DIGITAL) ? Config::disp_digital
							 : Config::disp_crt;
		config.crtdirect = config.screendisplay == Config::disp_direct;
		SetCrtFilter();
		break;

	case IDM_PLASMALV_0:
	case IDM_PLASMALV_1:
	case IDM_PLASMALV_2:
	case IDM_PLASMALV_3:
		config.plasmalevels = wid - IDM_PLASMALV_0;
		SetCrtFilter();
		break;

	case IDM_PLASMABG:
		config.plasmabg = !config.plasmabg;
		SetCrtFilter();
		break;

	case IDM_CRTPITCH_0:
	case IDM_CRTPITCH_1:
	case IDM_CRTPITCH_2:
	case IDM_CRTPITCH_3:
		config.crtpitch = wid - IDM_CRTPITCH_0;
		SetCrtFilter();
		break;

	case IDM_CAPTURE:
		CaptureScreen();
		break;

	
	case IDM_DEBUG_TEXT:
		config.flags ^= PC8801::Config::specialpalette;
		ApplyConfig();
		break;

	case IDM_DEBUG_GVRAM0:
		config.flag2 ^= PC8801::Config::mask0;
		ApplyConfig();
		break;

	case IDM_DEBUG_GVRAM1:
		config.flag2 ^= PC8801::Config::mask1;
		ApplyConfig();
		break;

	case IDM_DEBUG_GVRAM2:
		config.flag2 ^= PC8801::Config::mask2;
		ApplyConfig();
		break;
		
	case IDM_STATUSBAR:
		config.flags ^= PC8801::Config::showstatusbar;
		ShowStatusWindow();
		break;

	case IDM_THEME_SYSTEM:		// テーマ (M88gn)
	case IDM_THEME_DARK:
	case IDM_THEME_BEIGE:
		Theme::Set(wid == IDM_THEME_DARK ? Theme::dark : wid == IDM_THEME_BEIGE ? Theme::beige : Theme::system);
		InvalidateTitle();
		dock.Repaint();
		if (statusdisplay.GetHWnd())
			InvalidateRect(statusdisplay.GetHWnd(), 0, FALSE);
		if (tapewin.DockWindow())		// テープの窓もテーマの色
			InvalidateRect(tapewin.DockWindow(), 0, FALSE);
		DockClient::ApplyThemeAll();	// テープ・デバッグの窓のスクロールバー
		break;

	case IDM_SOFTKBD:
		softkbd.Show(!softkbd.IsShown());
		break;

	case IDM_TAPEWIN:
		tapewin.Show(!tapewin.IsShown());
		break;

	case IDM_FDC_STATUS:
		config.flags ^= PC8801::Config::showfdcstatus;
		ApplyConfig();
		break;

	case IDM_SOUNDMON:
		opnmon.Show(hinst, hwnd, !opnmon.IsOpen());
		break;

	case IDM_MEMMON:
		memmon.Show(hinst, hwnd, !memmon.IsOpen());
		break;

	case IDM_CODEMON:
		{
			bool open = !codemon.IsOpen();
			codemon.Show(hinst, hwnd, open);
			// 止まっている間に開いたら、止まっている所に合わせる
			if (open && core.IsStopped())
			{
				codemon.ShowCpu(GetStepTarget());
				codemon.FollowPC();
			}
		}
		break;

	case IDM_BPMON:
		bpmon.Show(hinst, hwnd, !bpmon.IsOpen());
		break;

	case IDM_IOBPMON:
		iobpmon.Show(hinst, hwnd, !iobpmon.IsOpen());
		break;

	case IDM_WATCHMON:
		watchmon.Show(hinst, hwnd, !watchmon.IsOpen());
		break;

	case IDM_TRACEMON:
		tracemon.Show(hinst, hwnd, !tracemon.IsOpen());
		break;

	case IDM_DOCKPANE_LEFT:
	case IDM_DOCKPANE_RIGHT:
	case IDM_DOCKPANE_TOP:
	case IDM_DOCKPANE_BOTTOM:
	case IDM_DOCKBTN_LEFT:		// メニューバーの右のボタン
	case IDM_DOCKBTN_RIGHT:
	case IDM_DOCKBTN_TOP:
	case IDM_DOCKBTN_BOTTOM:
		{
			int side = (wid == IDM_DOCKPANE_LEFT || wid == IDM_DOCKBTN_LEFT) ? 0
					 : (wid == IDM_DOCKPANE_RIGHT || wid == IDM_DOCKBTN_RIGHT) ? 1
					 : (wid == IDM_DOCKPANE_TOP || wid == IDM_DOCKBTN_TOP) ? 2 : 3;
			dock.ShowPane(side, !dock.IsPaneShown(side));
			UpdateDockButtons();
		}
		break;

	case IDM_LABELMON:
		lblmon.Show(hinst, hwnd, !lblmon.IsOpen());
		break;

	case IDM_LAPMON:
		lapmon.Show(hinst, hwnd, !lapmon.IsOpen());
		break;

	case IDM_STATMON:
		statmon.Show(hinst, hwnd, !statmon.IsOpen());
		break;

	case IDM_MNEMMON:
		mnemmon.Show(hinst, hwnd, !mnemmon.IsOpen());
		break;

	case IDM_SUBLOGMON:
		sublogmon.Show(hinst, hwnd, !sublogmon.IsOpen());
		break;

	case IDM_VIDEOMON:
		videomon.Show(hinst, hwnd, !videomon.IsOpen());
		break;

	case IDM_DBG_TRACE:
		core.Lock();
		core.GetDebugger()->SetTraceEnabled(!core.GetDebugger()->IsTraceEnabled());
		core.Unlock();
		break;

	case IDM_DBG_TRACECLEAR:
		core.Lock();
		core.GetDebugger()->ClearTrace();
		core.Unlock();
		tracemon.Refresh();
		break;

	// ラベルの書き出し・読み込み (M88gn)
	case IDM_LABEL_EXPORT:
		{
			int n = PC8801::ExportLabels(hwnd, core.GetDebugger());
			if (n >= 0)
				statusdisplay.Show(80, 3000, "ラベルを %d 個書き出しました.", n);
		}
		break;

	case IDM_LABEL_IMPORT:
		{
			int skipped;
			int n = PC8801::ImportLabels(hwnd, core.GetDebugger(), &skipped);
			if (n >= 0)
			{
				if (skipped)
					statusdisplay.Show(80, 4000, "ラベルを %d 個読み込みました (読めない行 %d).", n, skipped);
				else
					statusdisplay.Show(80, 3000, "ラベルを %d 個読み込みました.", n);
				codemon.Update();
				memmon.Update();
				lblmon.Update();
			}
		}
		break;

	case IDM_DBG_SAVESETTINGS:
		config.flag2 ^= PC8801::Config::savedebugperdisk;
		break;

	case IDM_MON_FONT:
		WinMonitor::ChooseMonitorFont(hwnd);
		break;

	case IDM_BASMON:
		basmon.Show(hinst, hwnd, !basmon.IsOpen());
		break;

	case IDM_LOADMON:
		loadmon.Show(hinst, hwnd, !loadmon.IsOpen());
		break;

	case IDM_IOMON:
		iomon.Show(hinst, hwnd, !iomon.IsOpen());
		break;

	case IDM_WATCHREGISTER:
		config.flags &= ~PC8801::Config::watchregister;
		regmon.Show(hinst, hwnd, !regmon.IsOpen());
		break;

	case IDM_TAPE:
		ChangeTapeImage();
		break;

	case IDM_TAPE_REWIND:
		core.Lock();
		tapemgr->Rewind();
		core.Unlock();
		statusdisplay.Show(20, 2000, "Tape: rewound");
		break;

	case IDM_TAPE_NEXT:
	case IDM_TAPE_PREV:
		{
			core.Lock();
			bool ok = tapemgr->Cue(wid == IDM_TAPE_NEXT ? 1 : -1);
			uint t = tapemgr->GetPos() / 4800;
			core.Unlock();
			if (ok)
				statusdisplay.Show(20, 3000, "Tape: %d:%.2d", t / 60, t % 60);
			else
				statusdisplay.Show(20, 3000, "Tape: no next gap");
			// メニューから選んだときは、続けて選べるように Tape メニューを開き直す
			// (Windows のメニューは選ぶと閉じる。マウスは同じ所にあるので、もう一度クリックすれば続けて動く)
			if (HIWORD(wparam) == 0 && lparam == 0)
			{
				int tm = TitleActive() ? TitleMenuByMnemonic('p') : -1;
				if (tm >= 0)
					PostMessage(hwnd, WM_M88_TITLEMENU, tm, 1);
				else
					PostMessage(hwnd, WM_SYSCOMMAND, SC_KEYMENU, 'p');
			}
		}
		break;

	case IDM_TAPE_RECORD:
		{
			core.Lock();
			bool on = !tapemgr->IsRecord();
			bool ask = on && tapemgr->NeedAppendConfirm();
			core.Unlock();
			if (ask)
			{
				// 既にデータのあるテープに初めて追記する (一度許可したら、開き直すまで聞かない)
				char msg[MAX_PATH + 256];
				wsprintf(msg,
					"テープにはデータがあります。\n\n%s\n\n"
					"セーブを、テープの最後に追記しますか？\n"
					"(はい を選ぶと、このテープを開き直すまでは確認しません)",
					tapemgr->GetFileName());
				if (MessageBox(hwnd, msg, "M88 - Record", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
					break;
			}
			core.Lock();
			if (ask)
				tapemgr->AllowAppend();
			tapemgr->SetRecord(on);
			core.Unlock();
		}
		break;

	case IDM_TAPESPEED_1:	config.tapespeed = 1; ApplyConfig(); break;
	case IDM_TAPESPEED_2:	config.tapespeed = 2; ApplyConfig(); break;
	case IDM_TAPESPEED_4:	config.tapespeed = 4; ApplyConfig(); break;
	case IDM_TAPESPEED_8:	config.tapespeed = 8; ApplyConfig(); break;

	case IDM_TAPEVOL_0:
	case IDM_TAPEVOL_25:
	case IDM_TAPEVOL_50:
	case IDM_TAPEVOL_75:
	case IDM_TAPEVOL_100:
		config.tapevolume = (wid - IDM_TAPEVOL_0) * 25;
		ApplyConfig();
		break;

	case IDM_DISKVOL_0:
	case IDM_DISKVOL_25:
	case IDM_DISKVOL_50:
	case IDM_DISKVOL_75:
	case IDM_DISKVOL_100:
		config.fddvolume = (wid - IDM_DISKVOL_0) * 25;
		ApplyConfig();
		break;

	case IDM_RECORDPCM:
		if (!core.GetSound()->IsDumping())
		{
			char buf[16];
			SYSTEMTIME t;

			GetLocalTime(&t);
			wsprintf(buf, "%.2d%.2d%.2d%.2d.wav", t.wDay, t.wHour, t.wMinute, t.wSecond);
			core.GetSound()->DumpBegin(buf);
		}
		else
		{
			core.GetSound()->DumpEnd();
		}
		break;

	case IDM_VIDEO_RECORD:		// 録画 (M88gn)
		ToggleVideoRecord();
		break;

	case IDM_VIDEO_FPS60:		// 録画のフレームレート (M88gn)
	case IDM_VIDEO_FPS30:
	case IDM_VIDEO_FPS20:
	case IDM_VIDEO_FPS15:
		WritePrivateProfileString("M88p2 for Windows", "VideoFps",
			wid == IDM_VIDEO_FPS30 ? "30" : wid == IDM_VIDEO_FPS20 ? "20" : wid == IDM_VIDEO_FPS15 ? "15" : "60", PC8801::GetM88IniPath());
		break;

	case IDM_VIDEO_FILTERED:
	case IDM_VIDEO_RAW:
		WritePrivateProfileString("M88p2 for Windows", "VideoMode", wid == IDM_VIDEO_RAW ? "1" : "0", PC8801::GetM88IniPath());
		break;

	case IDM_SNAPSHOT_SAVE:
		SaveSnapshot(currentsnapshot);
		break;
	case IDM_SNAPSHOT_SAVE_0:	case IDM_SNAPSHOT_SAVE_1:	case IDM_SNAPSHOT_SAVE_2:
	case IDM_SNAPSHOT_SAVE_3:	case IDM_SNAPSHOT_SAVE_4:	case IDM_SNAPSHOT_SAVE_5:
	case IDM_SNAPSHOT_SAVE_6:	case IDM_SNAPSHOT_SAVE_7:	case IDM_SNAPSHOT_SAVE_8:
	case IDM_SNAPSHOT_SAVE_9:
		SaveSnapshot(wid - IDM_SNAPSHOT_SAVE_0);
		break;
		
	case IDM_SNAPSHOT_LOAD:
		LoadSnapshot(currentsnapshot);
		break;
	case IDM_SNAPSHOT_LOAD_0:	case IDM_SNAPSHOT_LOAD_1:	case IDM_SNAPSHOT_LOAD_2:
	case IDM_SNAPSHOT_LOAD_3:	case IDM_SNAPSHOT_LOAD_4:	case IDM_SNAPSHOT_LOAD_5:
	case IDM_SNAPSHOT_LOAD_6:	case IDM_SNAPSHOT_LOAD_7:	case IDM_SNAPSHOT_LOAD_8:
	case IDM_SNAPSHOT_LOAD_9:
		LoadSnapshot(wid - IDM_SNAPSHOT_LOAD_0);
		break;

	// デバッガ (M88am から)
	case IDM_DBG_PAUSE:
		core.Pause(true);
		statusdisplay.SetStopped(true);
		ShowPauseStatus();
		codemon.ShowCpu(GetStepTarget());
		codemon.FollowPC();
		videomon.Refresh();
		tracemon.Refresh();			// トレースの窓はブレークのときしか書き直さないので、Pause でも書き直す (M88gn)
		break;

	case IDM_DBG_PLAY:
		if (core.IsStopped())
			core.Pause(false);				// 止まっている: 実行に戻す
		else if (core.IsStepping())
			core.CancelStep();				// 戻ってこないステップオーバーを打ち切って、そのまま走らせる
		statusdisplay.SetStopped(false);
		statusdisplay.Update();
		break;

	case IDM_DBG_STEPIN:
	case IDM_DBG_STEPOVER:
	case IDM_DBG_STEPOUT:
		// ステップが終わった後の表示は WM_M88_DEBUGBREAK (M88DebugBreak) で直す
		{
			PC8801::Debugger::CpuTarget t = GetStepTarget();
			if (t == PC8801::Debugger::cpu_sub && !core.IsSubCPUActive())
			{
				// サブCPU は駆動を制御する設定のとき、FDC などが動いていないと 1 命令も実行しないので、
				// ステップが終わらずに走り続けてしまう
				statusdisplay.Show(70, 3000, "サブCPUは停止中です.");
				break;
			}
			if (wid == IDM_DBG_STEPIN)
				core.StepIn(t);
			else if (wid == IDM_DBG_STEPOVER)
				core.StepOver(t);
			else if (!core.StepOut(t))
				statusdisplay.Show(70, 3000, "呼び出し元を特定できません.");
		}
		break;

	case IDM_DBG_SKIPINT:
		config.flag2 ^= PC8801::Config::disableintskip;
		ApplyConfig();
		break;

	default:
		if (IDM_CHANGEDISK_1 <= wid && wid < IDM_CHANGEDISK_1 + 64)
		{
			SelectDisk(0, wid - IDM_CHANGEDISK_1, false);
			break;
		}
		if (IDM_CHANGEDISK_2 <= wid && wid < IDM_CHANGEDISK_2 + 64)
		{
			SelectDisk(1, wid - IDM_CHANGEDISK_2, false);
			break;
		}
		break;
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::WmPaint
//	WM_PAINT ハンドラ
//
LRESULT WinUI::WmPaint(HWND hwnd, WPARAM wp, LPARAM lp)
{
	draw.RequestPaint();
	PaintViewFrame(GetFocus() == hwnd);		// ペインの間の画面の枠 (M88gn)
	return DefWindowProc(hwnd, WM_PAINT, wp, lp);
}

// ---------------------------------------------------------------------------
//	ペインを出している間の、PC-8801 の画面の周りの枠 (M88gn)
//	キー入力が PC-8801 に行く (本体の窓にフォーカスがある) ときだけ強調の色で描く
//
void WinUI::PaintViewFrame(bool focused)
{
	if (!viewframe || fullscreen || !hwnd)
		return;
	// フォーカスがあれば青 (テーマによらず同じ色)、無ければペインの仕切りと同じ色 (目立たない)
	HDC dc = GetDC(hwnd);
	int b = viewframewidth;
	int edge = 0;
	HBRUSH outer = 0;
	HBRUSH inner = CreateSolidBrush(focused ? RGB(0x00, 0x78, 0xd4) : Theme::C().splitter);
	for (int k = 0; k < 2; k++)
	{
		RECT o = viewframerect;
		int w = k == 0 ? b : b - edge;
		if (k == 0 && !edge)
			continue;
		if (k == 1)
			InflateRect(&o, -edge, -edge);
		RECT s4[4] =
		{
			{ o.left, o.top, o.right, o.top + w },
			{ o.left, o.bottom - w, o.right, o.bottom },
			{ o.left, o.top + w, o.left + w, o.bottom - w },
			{ o.right - w, o.top + w, o.right, o.bottom - w },
		};
		for (int i = 0; i < 4; i++)
			FillRect(dc, &s4[i], k == 0 ? outer : inner);
	}
	DeleteObject(inner);
	ReleaseDC(hwnd, dc);
}

LRESULT WinUI::WmSetFocus(HWND hwnd, WPARAM wp, LPARAM lp)
{
	PaintViewFrame(true);
	return DefWindowProc(hwnd, WM_SETFOCUS, wp, lp);
}

LRESULT WinUI::WmKillFocus(HWND hwnd, WPARAM wp, LPARAM lp)
{
	PaintViewFrame(false);
	return DefWindowProc(hwnd, WM_KILLFOCUS, wp, lp);
}

// ---------------------------------------------------------------------------
//	WinUI::WmCreate
//	WM_CREATE ハンドラ
//
LRESULT WinUI::WmCreate(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	CREATESTRUCT* cs = (CREATESTRUCT*) wparam;

	RECT rect;
	rect.left = 0;	rect.right =  640;
	rect.top  = 0;  rect.bottom = 400;
	
	Dpi::AdjustWindowRect(&rect, wstyle, true, 0, hwnd);
	SetWindowPos(hwnd, 0, 0, 0, rect.right-rect.left, rect.bottom-rect.top,
				 SWP_NOMOVE | SWP_NOZORDER);
	
	(*EnableIME)(hwnd, false);
//	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

	GetWindowRect(hwnd, &rect);
	point.x = rect.left; point.y = rect.top;

	LOG0("WmCreate\n");
	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::WmDestroy
//	WM_DESTROY ハンドラ
//
LRESULT WinUI::WmDestroy(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	SaveWindowPosition();
	statusdisplay.DisableOverlay();
	// 全画面でメニューバーを隠していると、窓と一緒には消えないので消す
	if (hmenumain && GetMenu(hwnd) != hmenumain)
		DestroyMenu(hmenumain);
	hmenumain = 0;
	PostQuitMessage(0);
	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::WmClose
//	WM_CLOSE ハンドラ
//
LRESULT WinUI::WmClose(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	// 確認
	if (config.flags & Config::askbeforereset)
	{
		SetGUIFlag(true);
		int res = MessageBox(hwnd, "M88 を終了します", "M88", MB_ICONEXCLAMATION | MB_OKCANCEL | MB_DEFBUTTON2);
		SetGUIFlag(false);
		if (res != IDOK)
			return 0;
	}
	
	if (core.IsStopped())
		core.Pause(false);			// デバッガで止めていたら動かしてから終わる
	// ペインの並びを保存する (M88gn。窓が壊れる前に)
	dock.SaveLayout();
	if (videorecorder.IsActive())		// 録画を書き終える (M88gn)
	{
		core.Lock();
		videorecorder.Stop();
		core.Unlock();
	}

	// タイマー破棄
	KillTimer(hwnd, timerid);
	timerid = 0;

	// 拡張メニューを破壊する
	MENUITEMINFO mii;
	memset(&mii, 0, sizeof(mii));
	mii.cbSize = WINVAR(MIISIZE);
	mii.fMask = MIIM_SUBMENU;
	mii.hSubMenu = 0;
	
	SetMenuItemInfo(hmenumain, IDM_DRIVE_1, false, &mii);
	SetMenuItemInfo(hmenumain, IDM_DRIVE_2, false, &mii);
	SetMenuItemInfo(hmenumain, IDM_SNAPSHOT_LOAD, false, &mii);
	SetMenuItemInfo(hmenumain, IDM_SNAPSHOT_SAVE, false, &mii);
	
	int i;
	for (i=0; i<2; i++)
	{
		if (IsMenu(diskinfo[i].hmenu))
			DestroyMenu(diskinfo[i].hmenu), diskinfo[i].hmenu = 0;
	}
	for (i=0; i<2; i++)
	{
		if (IsMenu(hmenuss[i]))
			DestroyMenu(hmenuss[i]), hmenuss[i] = 0;
	}

	// 窓を閉じる
	DestroyWindow(hwnd);
	active = false;
	
	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::WmTimer
//	WM_TIMER ハンドラ
//
LRESULT WinUI::WmTimer(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	LOG2("WmTimer:%d(%d)\n", wparam, timerid);
	if (wparam == 13)
	{
		dock.Tick();
		UpdateDockButtons();	// ドックモード (M88gn)
		return 0;
	}
	if (wparam == timerid)
	{
		// 実効周波数,表示フレーム数を取得
		int	fcount = draw.GetDrawCount();
		int	icount = core.GetExecCount();

		// デバッガ: 止まっている間は PC を出し続け、ステータスバーの色を今の状態に合わせる
		if (core.IsPaused())
			ShowPauseStatus();
		statusdisplay.SetStopped(core.IsStopped());
		
		// レポートする場合はタイトルバーを更新
		if (report)
		{
			// fps と動作周波数はステータスバー (とステータスの窓) に出す (M88gn)
			char buf[64];
			uint freq = icount / 10000;
			wsprintf(buf, "%d fps  %d.%.2d MHz", fcount, freq / 100, freq % 100);
			statusdisplay.SetPerf(buf);
		}

		if (resetwindowsize > 0)
		{
			resetwindowsize--;
			ResizeWindow(640, 400);
		}
		return 0;
	}
	if (wparam == 2)
	{
		KillTimer(hwnd, 2);
		InvalidateRect(hwnd, 0, false);
		return 0;
	}
	if (wparam == 9)
	{
		KillTimer(hwnd, 9);
		if (hoverlay)
			ShowWindow(hoverlay, SW_HIDE);
		return 0;
	}
	if (wparam == cursortimer)
	{
		// 全画面でマウスが止まっていたらカーソルを消す (画面の上にあるときだけ。メニューを開いている間は消さない)
		KillTimer(hwnd, cursortimer);
		GUITHREADINFO gi;
		memset(&gi, 0, sizeof(gi));
		gi.cbSize = sizeof(gi);
		bool inmenu = GetGUIThreadInfo(GetCurrentThreadId(), &gi) && (gi.flags & GUI_INMENUMODE);
		if (fullscreen && !inmenu && CursorOnScreen())
		{
			cursorhidden = true;
			SetCursor(0);
		}
		return 0;
	}
	if (wparam == statusdisplay.GetTimerID())
	{
		statusdisplay.Update();
		return 0;
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::WmInitMenu
//	WM_INITMENU ハンドラ
//
LRESULT WinUI::WmInitMenu(HWND hwnd, WPARAM wp, LPARAM lp)
{
	HMENU hmenu = (HMENU) wp;
	SetShortcutLabels(hmenu);
#ifndef DEBUG_MONITOR
	EnableMenuItem(hmenu, IDM_LOGSTART, MF_GRAYED);
	EnableMenuItem(hmenu, IDM_LOGEND, MF_GRAYED);
#endif
	CheckMenuItem(hmenu, IDM_4MHZ, (config.clock == 40) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_8MHZ, (config.clock == 80) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_N88V1, (config.basicmode == Config::N88V1) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_N88V1H,(config.basicmode == Config::N88V1H)? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_N88V2, (config.basicmode == Config::N88V2) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_NMODE, (config.basicmode == Config::N80)   ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_N80MODE, (config.basicmode == Config::N802)? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(hmenu, IDM_N80MODE, core.IsN80Supported() ? MF_ENABLED : MF_GRAYED);
	CheckMenuItem(hmenu, IDM_N80V2MODE, (config.basicmode == Config::N80V2)? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(hmenu, IDM_N80V2MODE, core.IsN80V2Supported() ? MF_ENABLED : MF_GRAYED);
	
	CheckMenuItem(hmenu, IDM_N88V2CD, (config.basicmode == Config::N88V2CD) ? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(hmenu, IDM_N88V2CD, core.IsCDSupported() ? MF_ENABLED : MF_GRAYED);

	CheckMenuItem(hmenu, IDM_CPU_BURST, (config.flags & Config::cpuburst) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_TOGGLEDISPLAY, fullscreen ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_FULLHIDEMENU, config.fullhidemenu ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_WINHIDEMENU, config.winhidemenu ? MF_CHECKED : MF_UNCHECKED);
	static const UINT scaleid[4] = { IDM_SCALE_1, IDM_SCALE_2, IDM_SCALE_3, IDM_SCALE_4 };
	for (int s = 0; s < 4; s++)
		CheckMenuItem(hmenu, scaleid[s], (!scalefree && config.screenscale == s + 1) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_SCALE_FREE, scalefree ? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(hmenu, IDM_SCALE_FREE, dockactive ? MF_ENABLED : MF_GRAYED);
	// 画面の回転は Direct3D 11 で描くときだけ
	for (int r = 0; r < 4; r++)
	{
		CheckMenuItem(hmenu, IDM_ROTATE_0 + r, (config.screenrotate == r) ? MF_CHECKED : MF_UNCHECKED);
		EnableMenuItem(hmenu, IDM_ROTATE_0 + r, draw.CanCrtFilter() ? MF_ENABLED : MF_GRAYED);
	}
	// 表示の型 (Screen → Display) は Direct3D 11 で描くときだけ選べる。CRT のフィルターの項目はブラウン管 (Analogue RGB CRT・
	// Digital RGB・白黒の管) のときだけ (Mask・Mask pitch はカラーの管だけ)、Glow は Plasma でも (ハレーションに使う)、
	// Plasma levels は Plasma のときだけ。ほかは灰色にする (値は残す)
	int disp = Limit(config.screendisplay, int(Config::disp_max), 0);
	bool iscrt = disp == Config::disp_crt || disp == Config::disp_green || disp == Config::disp_white || disp == Config::disp_amber
			  || disp == Config::disp_digital;
	UINT enmask = (draw.CanCrtFilter() && (disp == Config::disp_crt || disp == Config::disp_digital)) ? MF_ENABLED : MF_GRAYED;
	CheckMenuItem(hmenu, IDM_DISP_DIGITAL, (disp == Config::disp_digital) ? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(hmenu, IDM_DISP_DIGITAL, draw.CanCrtFilter() ? MF_ENABLED : MF_GRAYED);
	CheckMenuItem(hmenu, IDM_DISP_GREEN, (disp == Config::disp_green) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_DISP_WHITE, (disp == Config::disp_white) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_DISP_AMBER, (disp == Config::disp_amber) ? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(hmenu, IDM_DISP_GREEN, draw.CanCrtFilter() ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DISP_WHITE, draw.CanCrtFilter() ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DISP_AMBER, draw.CanCrtFilter() ? MF_ENABLED : MF_GRAYED);
	CheckMenuItem(hmenu, IDM_CRTDIRECT, (disp == Config::disp_direct) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_DISP_CRT, (disp == Config::disp_crt) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_DISP_PLASMA, (disp == Config::disp_plasma) ? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(hmenu, IDM_CRTDIRECT, draw.CanCrtFilter() ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DISP_CRT, draw.CanCrtFilter() ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DISP_PLASMA, draw.CanCrtFilter() ? MF_ENABLED : MF_GRAYED);
	UINT enplasma = (draw.CanCrtFilter() && disp == Config::disp_plasma) ? MF_ENABLED : MF_GRAYED;
	for (int c = 0; c < 4; c++)
	{
		CheckMenuItem(hmenu, IDM_PLASMALV_0 + c, (config.plasmalevels == c) ? MF_CHECKED : MF_UNCHECKED);
		EnableMenuItem(hmenu, IDM_PLASMALV_0 + c, enplasma);
	}
	CheckMenuItem(hmenu, IDM_PLASMABG, config.plasmabg ? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(hmenu, IDM_PLASMABG, enplasma);
	{
		// Scanlines・Mask などのサブメニューそのものを灰色にする
		// (メニューバーの各メニューの中で、最初の項目が CRT・プラズマの項目のサブメニューを探す)
		static const UINT crtfirst[10] = { IDM_CRTSCAN_0, IDM_CRTMASK_0, IDM_CRTPITCH_0, IDM_CRTSHARP_0, IDM_CRTBRIGHT_0, IDM_CRTGLOW_0, IDM_CRTBREATH_0,
										   IDM_CRTVIGNETTE_0, IDM_CRTCBLUR_0, IDM_PLASMALV_0 };
		UINT englow = (draw.CanCrtFilter() && disp != Config::disp_direct) ? MF_ENABLED : MF_GRAYED;
		UINT en = (draw.CanCrtFilter() && iscrt) ? MF_ENABLED : MF_GRAYED;
		int nbar = GetMenuItemCount(hmenu);
		for (int b = 0; b < nbar; b++)
		{
			HMENU pop = GetSubMenu(hmenu, b);
			if (!pop)
				continue;
			int nitem = GetMenuItemCount(pop);
			for (int i = 0; i < nitem; i++)
			{
				HMENU sub = GetSubMenu(pop, i);
				if (!sub)
					continue;
				UINT first = GetMenuItemID(sub, 0);
				for (int k = 0; k < 10; k++)
					if (first == crtfirst[k])
						EnableMenuItem(pop, i, MF_BYPOSITION | (first == IDM_CRTGLOW_0 ? englow : first == IDM_PLASMALV_0 ? enplasma
															 : (first == IDM_CRTMASK_0 || first == IDM_CRTPITCH_0) ? enmask : en));
			}
		}
	}
	for (int c = 0; c < 4; c++)
	{
		UINT englow = (draw.CanCrtFilter() && disp != Config::disp_direct) ? MF_ENABLED : MF_GRAYED;
		UINT en = (draw.CanCrtFilter() && iscrt) ? MF_ENABLED : MF_GRAYED;
		CheckMenuItem(hmenu, IDM_CRTSCAN_0 + c, (config.crtscan == c) ? MF_CHECKED : MF_UNCHECKED);
		CheckMenuItem(hmenu, IDM_CRTMASK_0 + c, (config.crtmask == c) ? MF_CHECKED : MF_UNCHECKED);
		EnableMenuItem(hmenu, IDM_CRTSCAN_0 + c, en);
		EnableMenuItem(hmenu, IDM_CRTMASK_0 + c, enmask);
		if (c == 0)
		{
			CheckMenuItem(hmenu, IDM_CRTMASK_4, (config.crtmask == 4) ? MF_CHECKED : MF_UNCHECKED);
			EnableMenuItem(hmenu, IDM_CRTMASK_4, enmask);
		}
		CheckMenuItem(hmenu, IDM_CRTPITCH_0 + c, (config.crtpitch == c) ? MF_CHECKED : MF_UNCHECKED);
		EnableMenuItem(hmenu, IDM_CRTPITCH_0 + c, enmask);
		CheckMenuItem(hmenu, IDM_CRTGLOW_0 + c, (config.crtglow == c) ? MF_CHECKED : MF_UNCHECKED);
		EnableMenuItem(hmenu, IDM_CRTGLOW_0 + c, englow);
		CheckMenuItem(hmenu, IDM_CRTBREATH_0 + c, (config.crtbreath == c) ? MF_CHECKED : MF_UNCHECKED);
		EnableMenuItem(hmenu, IDM_CRTBREATH_0 + c, en);
		CheckMenuItem(hmenu, IDM_CRTVIGNETTE_0 + c, (config.crtvignette == c) ? MF_CHECKED : MF_UNCHECKED);
		EnableMenuItem(hmenu, IDM_CRTVIGNETTE_0 + c, en);
		CheckMenuItem(hmenu, IDM_CRTCBLUR_0 + c, (config.crtcornerblur == c) ? MF_CHECKED : MF_UNCHECKED);
		EnableMenuItem(hmenu, IDM_CRTCBLUR_0 + c, en);
		if (c == 0)
		{
			CheckMenuItem(hmenu, IDM_CRTBREATH_EXPAND, config.crtbreathexp ? MF_CHECKED : MF_UNCHECKED);
			EnableMenuItem(hmenu, IDM_CRTBREATH_EXPAND, en);
		}
		if (c < 3)
		{
			CheckMenuItem(hmenu, IDM_CRTSHARP_0 + c, (config.crtsharp == c) ? MF_CHECKED : MF_UNCHECKED);
			EnableMenuItem(hmenu, IDM_CRTSHARP_0 + c, en);
			CheckMenuItem(hmenu, IDM_CRTBRIGHT_0 + c, (config.crtbright == c) ? MF_CHECKED : MF_UNCHECKED);
			EnableMenuItem(hmenu, IDM_CRTBRIGHT_0 + c, en);
		}
	}
	CheckMenuItem(hmenu, IDM_TAPESPEED_1, (config.tapespeed == 1) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_TAPESPEED_2, (config.tapespeed == 2) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_TAPESPEED_4, (config.tapespeed == 4) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_TAPESPEED_8, (config.tapespeed == 8) ? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(hmenu, IDM_TAPE_REWIND, tapemgr->IsOpen() ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_TAPE_NEXT, tapemgr->CanCue() ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_TAPE_PREV, tapemgr->CanCue() ? MF_ENABLED : MF_GRAYED);
	CheckMenuItem(hmenu, IDM_TAPE_RECORD, tapemgr->IsRecord() ? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(hmenu, IDM_TAPE_RECORD, (tapemgr->IsOpen() && !tapemgr->IsWriteProtected()) ? MF_ENABLED : MF_GRAYED);
	for (int v = 0; v <= 4; v++)
		CheckMenuItem(hmenu, IDM_TAPEVOL_0 + v, (config.tapevolume == v * 25) ? MF_CHECKED : MF_UNCHECKED);
	for (int v = 0; v <= 4; v++)
		CheckMenuItem(hmenu, IDM_DISKVOL_0 + v, (config.fddvolume == v * 25) ? MF_CHECKED : MF_UNCHECKED);

	CheckMenuItem(hmenu, IDM_WATCHREGISTER, (config.dipsw != 1 && regmon.IsOpen()) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_STATUSBAR, (config.flags & Config::showstatusbar) ? MF_CHECKED : MF_UNCHECKED);
	// デバッガ
	EnableMenuItem(hmenu, IDM_DBG_PAUSE, !core.IsStopped() ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DBG_PLAY, (core.IsStopped() || core.IsStepping()) ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DBG_STEPIN, core.IsStopped() ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DBG_STEPOVER, core.IsStopped() ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hmenu, IDM_DBG_STEPOUT, core.IsStopped() ? MF_ENABLED : MF_GRAYED);
	CheckMenuItem(hmenu, IDM_DBG_SKIPINT, (config.flag2 & PC8801::Config::disableintskip) ? MF_UNCHECKED : MF_CHECKED);
	CheckMenuItem(hmenu, IDM_SOFTKBD, softkbd.IsShown() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuRadioItem(hmenu, IDM_THEME_SYSTEM, IDM_THEME_BEIGE, IDM_THEME_SYSTEM + Theme::Get(), MF_BYCOMMAND);
	CheckMenuItem(hmenu, IDM_TAPEWIN, tapewin.IsShown() ? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(hmenu, IDM_STATUSBAR, MF_ENABLED);		// 全画面でも切り替えられる (メニューバーの右の端か、画面の右下に出す。M88gn)
	CheckMenuItem(hmenu, IDM_FDC_STATUS, (config.flags & Config::showfdcstatus) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_SOUNDMON, opnmon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_MEMMON, memmon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_CODEMON, codemon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_BPMON, bpmon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_IOBPMON, iobpmon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_WATCHMON, watchmon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_TRACEMON, tracemon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_MNEMMON, mnemmon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_STATMON, statmon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_LAPMON, lapmon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_LABELMON, lblmon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	{
		static const UINT ids[4] = { IDM_DOCKPANE_LEFT, IDM_DOCKPANE_RIGHT, IDM_DOCKPANE_TOP, IDM_DOCKPANE_BOTTOM };
		for (int i = 0; i < 4; i++)
		{
			CheckMenuItem(hmenu, ids[i], dock.IsPaneShown(i) ? MF_CHECKED : MF_UNCHECKED);
			EnableMenuItem(hmenu, ids[i], fullscreen ? MF_GRAYED : MF_ENABLED);
		}
	}
	CheckMenuItem(hmenu, IDM_SUBLOGMON, sublogmon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_VIDEOMON, videomon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_DBG_TRACE, core.GetDebugger()->IsTraceEnabled() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_DBG_SAVESETTINGS, (config.flag2 & PC8801::Config::savedebugperdisk) ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_BASMON, basmon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_LOADMON, loadmon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_IOMON, iomon.IsOpen() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_RECORDPCM, core.GetSound()->IsDumping() ? MF_CHECKED : MF_UNCHECKED);
	// 録画 (M88gn): Direct3D 11 で描いているときだけ。録っている間はチェック、形式は変えられない
	CheckMenuItem(hmenu, IDM_VIDEO_RECORD, videorecorder.IsActive() ? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(hmenu, IDM_VIDEO_RECORD, (draw.CanCrtFilter() || videorecorder.IsActive()) ? MF_ENABLED : MF_GRAYED);
	{
		int vm = GetPrivateProfileInt("M88p2 for Windows", "VideoMode", 0, PC8801::GetM88IniPath());
		CheckMenuRadioItem(hmenu, IDM_VIDEO_FILTERED, IDM_VIDEO_RAW, vm ? IDM_VIDEO_RAW : IDM_VIDEO_FILTERED, MF_BYCOMMAND);
		EnableMenuItem(hmenu, IDM_VIDEO_FILTERED, videorecorder.IsActive() ? MF_GRAYED : MF_ENABLED);
		EnableMenuItem(hmenu, IDM_VIDEO_RAW, videorecorder.IsActive() ? MF_GRAYED : MF_ENABLED);
		int fps = GetPrivateProfileInt("M88p2 for Windows", "VideoFps", 60, PC8801::GetM88IniPath());
		CheckMenuRadioItem(hmenu, IDM_VIDEO_FPS60, IDM_VIDEO_FPS15,
			fps == 30 ? IDM_VIDEO_FPS30 : fps == 20 ? IDM_VIDEO_FPS20 : fps == 15 ? IDM_VIDEO_FPS15 : IDM_VIDEO_FPS60, MF_BYCOMMAND);
		for (UINT id = IDM_VIDEO_FPS60; id <= IDM_VIDEO_FPS15; id++)
			EnableMenuItem(hmenu, id, videorecorder.IsActive() ? MF_GRAYED : MF_ENABLED);
	}
	
	EnableMenuItem(hmenu, IDM_DUMPCPU1, core.GetCPU1()->GetDumpState() == -1 ? MF_GRAYED : MF_ENABLED);
	CheckMenuItem(hmenu, IDM_DUMPCPU1, core.GetCPU1()->GetDumpState() == 1 ? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(hmenu, IDM_DUMPCPU2, core.GetCPU2()->GetDumpState() == -1 ? MF_GRAYED : MF_ENABLED);
	CheckMenuItem(hmenu, IDM_DUMPCPU2, core.GetCPU2()->GetDumpState() == 1 ? MF_CHECKED : MF_UNCHECKED);
	
	if (hmenudbg)
	{
		CheckMenuItem(hmenudbg, IDM_DEBUG_TEXT, (config.flags & Config::specialpalette) ? MF_CHECKED : MF_UNCHECKED);
		int mask = (config.flag2 / Config::mask0) & 7;
		CheckMenuItem(hmenudbg, IDM_DEBUG_GVRAM0, (mask & 1) ? MF_CHECKED : MF_UNCHECKED);
		CheckMenuItem(hmenudbg, IDM_DEBUG_GVRAM1, (mask & 2) ? MF_CHECKED : MF_UNCHECKED);
		CheckMenuItem(hmenudbg, IDM_DEBUG_GVRAM2, (mask & 4) ? MF_CHECKED : MF_UNCHECKED);
	}

	MakeSnapshotMenu();
	return 0;
}


// ---------------------------------------------------------------------------
//	WinUI::WmSize
//	WM_SIZE
//
LRESULT WinUI::WmSize(HWND hwnd, WPARAM wp, LPARAM lp)
{
	if (wp != SIZE_MINIMIZED)
		statusdisplay.Layout();
	dock.SetTopInset(TitleHeight());		// タイトルの段 (M88gn)
	InvalidateTitle();
	active = wp != SIZE_MINIMIZED;
	draw.Activate(active);
	if (fullscreen && active)
		draw.Resize(640, 400);		// 全画面: 描く範囲をウィンドウいっぱいに合わせ直す
	else if (dockactive && active)
		dock.Layout();				// ドックモード: ペインと画面を並べ直す (M88gn)
	return DefWindowProc(hwnd, WM_SIZE, wp, lp);
}

// ---------------------------------------------------------------------------
//	WinUI::ReportError
//	エラー表示
//
void WinUI::ReportError()
{
	const char* errtext = Error::GetErrorText();
	if (errtext)
	{
		::MessageBox(hwnd, errtext, "M88", MB_ICONERROR | MB_OK);
	}
}

// ---------------------------------------------------------------------------
//	WinUI::ReportError
//
LRESULT WinUI::M88ApplyConfig(HWND, WPARAM newconfig, LPARAM)
{
	if (newconfig)
	{
		// 乱暴ですな。
		if (memcmp(&config, (PC8801::Config*)newconfig, sizeof(PC8801::Config)))
		{
			// ウィンドウモードの二倍拡大が変更された時だけ
			// ディスプレイモードの変更を呼びたい
			// 「画面2倍」は表示の倍率 (Control → Window scale) の 1 / 2 と同じ。変えなければ今の倍率のまま
			bool stretchchanged = ((config.flag2 ^ ((PC8801::Config*)newconfig)->flag2) & PC8801::Config::stretchx2) != 0;
			bool changedisplay = stretchchanged && !fullscreen;
			int scale = config.screenscale;

			config = *((PC8801::Config*)newconfig);
			config.screenscale = stretchchanged ? ((config.flag2 & PC8801::Config::stretchx2) ? 2 : 1) : scale;
			ApplyConfig();

			// ディスプレイモード変更要求
			if (changedisplay) ChangeDisplayType(false);
		}
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	CRT 風のフィルターの設定を描画に渡す
//
void WinUI::SetCrtFilter()
{
	// 画面の回転。窓の大きさに使った向きと変われば、窓の大きさを直す (全画面なら描く範囲だけ)
	draw.SetRotation(Limit(config.screenrotate, 3, 0));
	int rot = draw.CanCrtFilter() ? Limit(config.screenrotate, 3, 0) : 0;
	if ((rot & 1) != (appliedrot & 1))
	{
		if (fullscreen)
			appliedrot = rot;
		else
			ResizeWindow(640, 400);
	}
	int disp = Limit(config.screendisplay, int(Config::disp_max), 0);
	// ブラウン管の周辺減光 (bit14-13) と周辺のぼけ (bit16-15)
	int edge = (Limit(config.crtvignette, 3, 0) << 13) | (Limit(config.crtcornerblur, 3, 0) << 15);
	if (disp == Config::disp_direct)
		draw.SetCrtFilter(0, 0, config.crtsharp, config.crtbright, 0, config.crtpitch, 0);	// 走査線・マスク・ぼけが無ければ、フィルター無し (最近傍)
	else if (disp == Config::disp_plasma)
		draw.SetCrtFilter(0, 0, config.crtsharp, config.crtbright, config.crtglow, config.crtpitch,
			0x20 | (Limit(config.plasmalevels, 3, 0) << 6) | (config.plasmabg ? 0x1000 : 0));
			// 0x20: プラズマディスプレイ (ぼけはハレーション)、bit7-6: 階調 (2・4・8・16)、bit12: 黒も弱く光らせる
	else if (disp == Config::disp_green || disp == Config::disp_white || disp == Config::disp_amber)
	{
		int phos = disp == Config::disp_green ? 1 : disp == Config::disp_white ? 2 : 3;
		draw.SetCrtFilter(config.crtscan, 0, config.crtsharp, config.crtbright, config.crtglow, config.crtpitch,
			config.crtbreath | (config.crtbreathexp ? 0x10 : 0) | (phos << 8) | edge);	// bit11-8: 白黒の管の蛍光体 (1: 緑 2: 白 3: 琥珀)。マスクは無い
	}
	else
		draw.SetCrtFilter(config.crtscan, config.crtmask, config.crtsharp, config.crtbright, config.crtglow, config.crtpitch,
			config.crtbreath | (config.crtbreathexp ? 0x10 : 0) | (disp == Config::disp_digital ? 0x400 : 0) | edge);
			// 0x10: 明るいと膨らむ、bit11-8 = 4: デジタル RGB (R・G・B をそれぞれ点く・消えるの 2 値に)
}

// ---------------------------------------------------------------------------
//	WinUI::ApplyConfig
//
void WinUI::ApplyConfig()
{
	config.mainsubratio = 
		(config.clock >= 60 || (config.flags & Config::fullspeed)) ? 2 : 1;
	if (config.dipsw != 1)
	{
		config.flags &= ~Config::specialpalette;
		config.flag2 &= ~(Config::mask0 | Config::mask1 | Config::mask2);
	}
		
	core.ApplyConfig(&config);
	keyif.ApplyConfig(&config);
	draw.SetPriorityLow((config.flags & Config::drawprioritylow) != 0);
	SetCrtFilter();
	core.GetDebugger()->SetIntSkipEnabled(!(config.flag2 & PC8801::Config::disableintskip));

	MENUITEMINFO mii;
	memset(&mii, 0, sizeof(mii));
	mii.cbSize = WINVAR(MIISIZE);
	mii.fMask = MIIM_TYPE;
	mii.fType = MFT_STRING;
	mii.dwTypeData = (config.flags & Config::disablef12reset) ? "&Reset" : "&Reset\tF12";
	SetMenuItemInfo(hmenumain, IDM_RESET, false, &mii);
	ShowStatusWindow();

	if (config.dipsw == 1)
	{
		if (!hmenudbg)
		{
			hmenudbg = LoadMenu(hinst, MAKEINTRESOURCE(IDR_MENU_DEBUG));

			mii.fMask = MIIM_TYPE | MIIM_SUBMENU;
			mii.fType = MFT_STRING;
			mii.dwTypeData = "Control &Plane";
			mii.hSubMenu = hmenudbg;
			SetMenuItemInfo(hmenumain, IDM_WATCHREGISTER, false, &mii);
		}
	}
	else
	{
		mii.fMask = MIIM_TYPE | MIIM_SUBMENU;
		mii.fType = MFT_STRING;
		mii.dwTypeData = "Show &Register";
		mii.hSubMenu = 0;
		SetMenuItemInfo(hmenumain, IDM_WATCHREGISTER, false, &mii);
		hmenudbg = 0;
	}
}

// ---------------------------------------------------------------------------
//	WinUI::Reset
//
void WinUI::Reset()
{
	if (config.flags & Config::askbeforereset)
	{
		SetGUIFlag(true);
		int res = MessageBox(hwnd, "リセットしますか？", "M88", MB_ICONQUESTION | MB_OKCANCEL | MB_DEFBUTTON2);
		SetGUIFlag(false);
		if (res != IDOK)
			return;
	}
	keyif.ApplyConfig(&config);
	core.ApplyConfig(&config);
	core.Reset();
}


// ---------------------------------------------------------------------------
//	ChangeDiskImage
//	ディスク入れ替え
//
void WinUI::ChangeDiskImage(HWND hwnd, int drive)
{
	HANDLE hthread = GetCurrentThread();
	int prev = GetThreadPriority(hthread);
	SetThreadPriority(hthread, THREAD_PRIORITY_ABOVE_NORMAL);
	
	SetGUIFlag(true);
	
	bool was = DiskMounted(drive);
	bool was1 = DiskMounted(1);
	if (!diskmgr->Unmount(drive))
	{
		MessageBox(hwnd, 
			"DiskManger::Unmount failed\nディスクの取り外しに失敗しました.",
			"M88",
			MB_ICONERROR | MB_OK);
	}
	if (was)
		core.FDDMechSound(false);			// 取り出す音

	OFNV5 ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = WINVAR(OFNSIZE);
	ofn.FlagsEx = 0;		// 位置バーを消す指定 (OFN_EX_NOPLACESBAR) を付けると、Windows が古い形のダイアログを出す (DPI 対応の窓ではツールバーのアイコンが小さい)

	char filename[MAX_PATH];
	filename[0] = 0;
	
	ofn.hwndOwner = hwnd;
	ofn.lpstrFilter = "Support disk image (*.d88;*.fdx)\0*.d88;*.fdx\0"
					  "All Files (*.*)\0*.*\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_CREATEPROMPT | OFN_SHAREAWARE;
	ofn.lpstrDefExt = "d88";
	ofn.lpstrTitle = "Open disk image";
	
	(*EnableIME)(hwnd, true);
	bool isopen = !!GetOpenFileName(&ofn);
	(*EnableIME)(hwnd, false);

	if (isopen)
	{
		// 指定されたファイルは存在するか？
		bool createnew = false;
		if (!diskmgr->IsImageOpen(filename))
		{
			FileIO file;
			if (!file.Open(filename, FileIO::readonly))
			{
				if (file.GetError() == FileIO::file_not_found)
				{
					// 拡張子の制限
					char ext[_MAX_EXT];
					_splitpath(filename, NULL, NULL, NULL, ext);
					if (stricmp(ext, ".d88") != 0 && stricmp(ext, ".fdx") != 0) {
						::MessageBox(hwnd, "拡張子はd88またはfdxを指定してして下さい", "M88", MB_ICONERROR | MB_OK);
						return;
					}

					// ファイルが存在しない
					createnew = true;
					if (!newdisk.Show(hinst, hwnd))
						return;
				}
				else
				{
					// 何らかの理由でアクセスできない
					return;
				}
			}
		}

		OpenDiskImage(drive, filename, ofn.Flags & OFN_READONLY, 0, createnew);

		if (createnew && diskmgr->GetNumDisks(drive) == 0)
		{
			diskmgr->AddDisk(drive, newdisk.GetTitle(), newdisk.GetType());
			OpenDiskImage(drive, filename, 0, 0, false);
			if (newdisk.DoFormat())
				diskmgr->FormatDisk(drive);
		}
		if (DiskMounted(drive))
			core.FDDMechSound(true);		// 入れる音
		if (drive == 0 && !diskinfo[1].filename[0] && diskmgr->GetNumDisks(0) > 1)
		{
			OpenDiskImage(1, filename, ofn.Flags & OFN_READONLY, 1, false);
			if (!was1 && DiskMounted(1))
				core.FDDMechSound(true);	// 2 枚目を入れる音
		}
	}
	else
		OpenDiskImage(drive, 0, 0, 0, false);
	
	SetGUIFlag(false);
	SetThreadPriority(hthread, prev);
	snapshotchanged = true;
}

// ---------------------------------------------------------------------------
//	適当にディスクイメージを開く
//
void WinUI::OpenDiskImage(const char* path)
{
	// ディスクイメージをマウントする
	OpenDiskImage(0, path, 0, 0, false);
	if (diskmgr->GetNumDisks(0) > 1)
	{
		OpenDiskImage(1, path, 0, 1, false);
	}
	else
	{
		diskmgr->Unmount(1);
		OpenDiskImage(1, 0, 0, 0, false);
	}
}

// ---------------------------------------------------------------------------
//	ドライブにディスクが入っているか
//
bool WinUI::DiskMounted(int drive)
{
	PC8801::FDU* fdu = diskmgr->GetFDU(drive);
	return fdu && fdu->IsMounted();
}

// ---------------------------------------------------------------------------
//	ファイルネームの部分を取り出す
//
static void GetFileNameTitle(char* title, size_t tlen, const char* name)
{
	if (name)
	{
		uchar* ptr;
		ptr = _mbsrchr((uchar*) name, '\\');
		_mbscpy_s((uchar*) title, tlen, ptr ? ptr+1 : (uchar*)(name));
		ptr = _mbschr((uchar*) title, '.');
		if (ptr)
			*ptr = 0;
	}
}

// ---------------------------------------------------------------------------
//	OpenDiskImage
//	ディスクイメージを開く
//
bool WinUI::OpenDiskImage
(int drive, const char* name, bool readonly, int id, bool create)
{
	DiskInfo& dinfo = diskinfo[drive];

	bool result = false;
	if (name)
	{
		strncpy_s(dinfo.filename, sizeof(dinfo.filename), name, _TRUNCATE);
		result = diskmgr->Mount(drive, dinfo.filename, readonly, id, create);
		dinfo.readonly = readonly;
		dinfo.currentdisk = diskmgr->GetCurrentDisk(0);
	}
	if (!result)
		dinfo.filename[0] = 0;

	// デバッガの設定: ドライブ 1 にディスクを入れたらそのディスク、取り出したらテープ (開いていれば) に切り替える
	if (drive == 0)
		SetDebugKey(dinfo.filename[0] ? dinfo.filename : tapefile);

	CreateDiskMenu(drive);
	return true;
}

// ---------------------------------------------------------------------------
//	WinUI::SetDebugKey
//	デバッガの設定を保存・読み込みするイメージを切り替える。最後に入れた (開いた) ほう (ドライブ 1 のディスクか
//	テープ) に合わせ、取り出したら残っているほうに戻す
//
void WinUI::SetDebugKey(const char* newkey)
{
	SyncDebugSettings(debugkey, newkey);
	strncpy_s(debugkey, sizeof(debugkey), newkey, _TRUNCATE);
}

// ---------------------------------------------------------------------------
//	WinUI::SyncDebugSettings
//	イメージ (ドライブ 1 のディスクかテープ) が入れ替わったときに、ブレークポイント・ウォッチを
//	「イメージの名前.m88dbg」へ保存・読み込みする (Save Settings per Disk/Tape がオンのときだけ。
//	オフなら今のリストに触らない。M88am から。テープは M88gn で足した)
//
void WinUI::SyncDebugSettings(const char* oldkey, const char* newkey)
{
	if (oldkey[0] && !strcmp(oldkey, newkey))
		return;		// 同じイメージを入れ直しただけ
	if (!(config.flag2 & PC8801::Config::savedebugperdisk))
		return;

	core.Lock();
	PC8801::Debugger* dbg = core.GetDebugger();
	if (oldkey[0])
	{
		if (!PC8801::SaveDebugSettings(dbg, oldkey))
			statusdisplay.Show(90, 3000, "デバッグ設定を保存できません: %s", oldkey);
	}
	else if (!debugnodiskwarned && (dbg->GetBreakPointCount() || dbg->GetWatchCount() || dbg->GetLabelCount()))
	{
		statusdisplay.Show(90, 4000, "ディスク・テープが無いため保存されません.");
		debugnodiskwarned = true;
	}
	dbg->DeleteAllBreakPoints();
	dbg->DeleteAllWatches();
	dbg->DeleteAllLabels();
	if (newkey[0])
	{
		int renamed = 0;
		PC8801::LoadDebugSettings(dbg, newkey, &renamed);
		if (renamed)
			statusdisplay.Show(90, 6000, "デバッグ設定のラベル %d 個の名前を変えました (名前の重複・使えない字).", renamed);
	}
	core.Unlock();
}

// ---------------------------------------------------------------------------
//	SelectDisk
//	ディスクセット
//
bool WinUI::SelectDisk(uint drive, int id, bool menuonly)
{
	DiskInfo& dinfo = diskinfo[drive];
	if (drive >= 2 || id >= 64)
		return false;

	CheckMenuItem(dinfo.hmenu,
		dinfo.idchgdisk+(dinfo.currentdisk < 0 ? 63 : dinfo.currentdisk),
		MF_BYCOMMAND | MF_UNCHECKED);
	
	bool result = true;
	if (!menuonly)
	{
		// ディスクを替える: 取り出す音と入れる音
		if (DiskMounted(drive))
			core.FDDMechSound(false);
		result = diskmgr->Mount(drive, dinfo.filename, dinfo.readonly, id, false);
		if (DiskMounted(drive))
			core.FDDMechSound(true);
	}
	
	int current = result ? diskmgr->GetCurrentDisk(drive) : -1;
	
	CheckMenuItem(dinfo.hmenu,
				  dinfo.idchgdisk+(current < 0 ? 63 : current),
				  MF_BYCOMMAND | MF_CHECKED);
	dinfo.currentdisk = current;
	return true;
}

// ---------------------------------------------------------------------------
//	CreateDiskMenu
//	マルチディスクイメージ用メニューの作成
//
bool WinUI::CreateDiskMenu(uint drive)
{
	char buf[MAX_PATH + 16];
	
	DiskInfo& dinfo = diskinfo[drive];
	HMENU hmenuprev = dinfo.hmenu;
	dinfo.currentdisk = -1;

	int ndisks = Min(diskmgr->GetNumDisks(drive), 60);
	if (ndisks)
	{
		// メニュー作成
		dinfo.hmenu = CreatePopupMenu();
		if (!dinfo.hmenu)
			return false;
		
		for (int i=0; i<ndisks; i++)
		{
			const char* title = diskmgr->GetImageTitle(drive, i);
			
			if (!title) break;
			if (!title[0]) title = "(untitled)";
			
			wsprintf(buf, i < 9 ? "&%d %.16s" : "%d %.16s", i+1, title);
			AppendMenu(	dinfo.hmenu,
						MF_STRING | (i && !(i % 20) ? MF_MENUBARBREAK : 0),
						dinfo.idchgdisk + i,
						buf );
		}

		AppendMenu(dinfo.hmenu, MF_SEPARATOR, 0, 0);
		AppendMenu(dinfo.hmenu, MF_STRING, dinfo.idchgdisk + 63, "&N No disk");
		AppendMenu(dinfo.hmenu, MF_STRING, dinfo.idchgdisk + 64, "&0 Change disk");
		SetMenuDefaultItem(dinfo.hmenu, dinfo.idchgdisk + 64, FALSE );
	}

	MENUITEMINFO mii;
	memset(&mii, 0, sizeof(mii));
	mii.cbSize = WINVAR(MIISIZE);
	mii.fType = MFT_STRING;
	mii.fMask = MIIM_TYPE | MIIM_SUBMENU;
	mii.dwTypeData = buf;

	if (!ndisks)
	{
		wsprintf(buf, "Drive &%d...", drive+1);
		mii.hSubMenu = 0;
	}
	else
	{
		char title[MAX_PATH] = "";
		GetFileNameTitle(title, sizeof(title), diskinfo[drive].filename);
	
		wsprintf(buf, "Drive &%d - %s", drive+1, title);
		mii.hSubMenu = dinfo.hmenu;
	}
	SetMenuItemInfo(hmenumain, drive ? IDM_DRIVE_2 : IDM_DRIVE_1, false, &mii);	// メニューバーは使っていない (タイトルの段。M88gn)
	if (hmenuprev)
		DestroyMenu(hmenuprev);
	
	if (ndisks)
		SelectDisk(drive, dinfo.currentdisk, true);

	return true;
}


// ---------------------------------------------------------------------------
//	ChangeTapeImage
//	ディスク入れ替え
//
void WinUI::ChangeTapeImage()
{
	HANDLE hthread = GetCurrentThread();
	int prev = GetThreadPriority(hthread);
	SetThreadPriority(hthread, THREAD_PRIORITY_ABOVE_NORMAL);
	
	SetGUIFlag(true);
	
	core.Lock();
	tapemgr->Close();
	core.Unlock();

	OFNV5 ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = WINVAR(OFNSIZE);
	ofn.FlagsEx = 0;		// 位置バーを消す指定 (OFN_EX_NOPLACESBAR) を付けると、Windows が古い形のダイアログを出す (DPI 対応の窓ではツールバーのアイコンが小さい)

	char filename[MAX_PATH];
	filename[0] = 0;
	
	ofn.hwndOwner = hwnd;
	ofn.lpstrFilter = "Tape image (*.t88;*.cmt;*.raw)\0*.t88;*.cmt;*.raw\0"
					  "T88 tape image (*.t88)\0*.t88\0"
					  "CMT tape image (*.cmt)\0*.cmt\0"
					  "PicoCmt RAW (*.raw)\0*.raw\0"
					  "All Files (*.*)\0*.*\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_CREATEPROMPT | OFN_SHAREAWARE;
	ofn.lpstrDefExt = "t88";
	ofn.lpstrTitle = "Open tape image";
	
	(*EnableIME)(hwnd, true);
	bool isopen = !!GetOpenFileName(&ofn);
	(*EnableIME)(hwnd, false);

	if (isopen)
	{
		// 無い名前なら (作るか聞かれて はい なら) 空の T88 を作る
		bool ok = true;
		if (GetFileAttributes(filename) == INVALID_FILE_ATTRIBUTES)
		{
			size_t len = strlen(filename);
			if (len >= 4 && (!_stricmp(filename + len - 4, ".raw") || !_stricmp(filename + len - 4, ".cmt")))
				ok = false;		// RAW・CMT は作れない (T88 だけ)
			else
				ok = TapeManager::CreateNew(filename);
			if (!ok)
				MessageBox(hwnd, "ファイルを作れませんでした。", "M88", MB_OK | MB_ICONERROR);
		}
		if (ok)
			OpenTapeImage(filename, !!(ofn.Flags & OFN_READONLY));
	}
	else
		OpenTapeImage("", false);		// 開かなかった (前のテープは閉じている)

	SetGUIFlag(false);
	SetThreadPriority(hthread, prev);
	snapshotchanged = true;
}

// ---------------------------------------------------------------------------
//	WinUI::AskNewImageName
//	新しく作るイメージの名前を、保存の窓で聞く (M88gn)。既にある名前なら置き換えるか聞く (OFN_OVERWRITEPROMPT)
//
bool WinUI::AskNewImageName(char* filename, const char* filter, const char* defext, const char* title)
{
	OFNV5 ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = WINVAR(OFNSIZE);
	ofn.FlagsEx = 0;

	filename[0] = 0;
	ofn.hwndOwner = hwnd;
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOREADONLYRETURN;
	ofn.lpstrDefExt = defext;
	ofn.lpstrTitle = title;

	(*EnableIME)(hwnd, true);
	bool r = !!GetSaveFileName(&ofn);
	(*EnableIME)(hwnd, false);
	return r;
}

// ---------------------------------------------------------------------------
//	WinUI::NewDiskImage
//	新しいディスクイメージを作る (M88gn)。drive に入れる (-1 なら入れない)。
//	既にある名前は、置き換えてよいと答えたら消してから作る (ドライブに入っているイメージは置き換えない)
//
void WinUI::NewDiskImage(int drive)
{
	HANDLE hthread = GetCurrentThread();
	int prev = GetThreadPriority(hthread);
	SetThreadPriority(hthread, THREAD_PRIORITY_ABOVE_NORMAL);
	SetGUIFlag(true);

	char filename[MAX_PATH];
	if (AskNewImageName(filename, "D88 disk image (*.d88)\0*.d88\0"
								  "FDX disk image (*.fdx)\0*.fdx\0",
						"d88", "New disk image"))
	{
		char ext[_MAX_EXT];
		_splitpath(filename, NULL, NULL, NULL, ext);
		bool fdx = !_stricmp(ext, ".fdx");
		if (_stricmp(ext, ".d88") && !fdx)
			MessageBox(hwnd, "拡張子はd88またはfdxを指定して下さい", "M88", MB_ICONERROR | MB_OK);
		else if (diskmgr->IsImageOpen(filename))
			MessageBox(hwnd, "このディスクイメージはドライブに入っているため、置き換えられません。", "M88", MB_ICONERROR | MB_OK);
		else if (newdisk.Show(hinst, hwnd))
		{
			bool ok = true;
			if (GetFileAttributes(filename) != INVALID_FILE_ATTRIBUTES && !DeleteFile(filename))
				ok = false;
			if (ok)
				ok = diskmgr->CreateImage(filename, newdisk.GetTitle(), newdisk.GetType(),
										  newdisk.DoFormat() && !fdx);
			if (!ok)
				MessageBox(hwnd, "ディスクイメージを作れませんでした。", "M88", MB_ICONERROR | MB_OK);
			else if (drive >= 0)
			{
				bool was = DiskMounted(drive);
				if (!diskmgr->Unmount(drive))
					MessageBox(hwnd, "DiskManger::Unmount failed\nディスクの取り外しに失敗しました.", "M88", MB_ICONERROR | MB_OK);
				if (was)
					core.FDDMechSound(false);		// 取り出す音
				OpenDiskImage(drive, filename, false, 0, false);
				if (DiskMounted(drive))
					core.FDDMechSound(true);		// 入れる音
				snapshotchanged = true;
			}
			else
				statusdisplay.Show(70, 3000, "ディスクイメージを作りました.");
			if (ok && fdx && newdisk.DoFormat())
				statusdisplay.Show(70, 5000, "FDXのディスクはフォーマットしていません.");
		}
	}

	SetGUIFlag(false);
	SetThreadPriority(hthread, prev);
}

// ---------------------------------------------------------------------------
//	WinUI::NewTapeImage
//	空の T88 を作って開く (M88gn)。前のテープは閉じる。既にある名前は、置き換えてよいと答えたら作り直す
//
void WinUI::NewTapeImage()
{
	HANDLE hthread = GetCurrentThread();
	int prev = GetThreadPriority(hthread);
	SetThreadPriority(hthread, THREAD_PRIORITY_ABOVE_NORMAL);
	SetGUIFlag(true);

	char filename[MAX_PATH];
	if (AskNewImageName(filename, "T88 tape image (*.t88)\0*.t88\0", "t88", "New tape image"))
	{
		char ext[_MAX_EXT];
		_splitpath(filename, NULL, NULL, NULL, ext);
		if (_stricmp(ext, ".t88"))
			MessageBox(hwnd, "拡張子はt88を指定して下さい", "M88", MB_ICONERROR | MB_OK);
		else
		{
			core.Lock();
			tapemgr->Close();			// 開いているテープを置き換えることもあるので、先に閉じる
			core.Unlock();
			if (TapeManager::CreateNew(filename))
				OpenTapeImage(filename, false);
			else
			{
				OpenTapeImage("", false);
				MessageBox(hwnd, "ファイルを作れませんでした。", "M88", MB_OK | MB_ICONERROR);
			}
			snapshotchanged = true;
		}
	}

	SetGUIFlag(false);
	SetThreadPriority(hthread, prev);
}

void WinUI::OpenTapeImage(const char* filename, bool readonly)
{
	char buf[MAX_PATH+32];
	MENUITEMINFO mii;
	memset(&mii, 0, sizeof(mii));
	mii.cbSize = WINVAR(MIISIZE);
	mii.fMask = MIIM_TYPE;
	mii.fType = MFT_STRING;
		
	core.Lock();
	bool r = *filename && tapemgr->Open(filename, readonly);
	bool wp = tapemgr->IsWriteProtected();
	core.Unlock();

	// デバッガの設定: テープを開いたらそのテープ、閉じたら (開けなかったら) ドライブ 1 のディスクに切り替える
	strncpy_s(tapefile, sizeof(tapefile), r ? filename : "", _TRUNCATE);
	SetDebugKey(r ? filename : diskinfo[0].filename);
	if (r)
	{
		GetFileNameTitle(tapetitle, sizeof(tapetitle), filename);
		wsprintf(buf, wp ? "&Tape - %s (read only)..." : "&Tape - %s...", tapetitle);	// ディスクの Drive 1 - 名前 と同じ形 (M88gn)
		mii.dwTypeData = buf;
	}
	else
	{
		mii.dwTypeData = "&Open...";
	}
	SetMenuItemInfo(hmenumain, IDM_TAPE, false, &mii);
}

// ---------------------------------------------------------------------------
//	WinUI::TapeNotify
//	テープからの知らせ (エミュレーションのスレッドから呼ばれる)
//
void WinUI::TapeNotify(void* ctx, int code)
{
	WinUI* ui = reinterpret_cast<WinUI*>(ctx);
	PostMessage(ui->hwnd, WM_M88_TAPERECORD, code, 0);
}

// ---------------------------------------------------------------------------
//	WinUI::M88DebugBreak
//	デバッガがブレークポイントやステップで止めたときに Sequencer から届く。
//	VM のスレッドから UI を触らないよう、ここでまとめて直す (M88am から)
//
LRESULT WinUI::M88DebugBreak(HWND, WPARAM, LPARAM)
{
	// 知らせが届くまでの間に次のステップが始まっていたら捨てる (走り出したステップを止めないため)
	if (!core.GetDebugger()->IsBroken())
		return 0;
	if (!core.IsPaused())
		core.Pause(true);
	statusdisplay.SetStopped(true);
	ShowPauseStatus();
	codemon.ShowCpu(core.GetDebugger()->GetBreakCpu());
	codemon.FollowPC();
	// パターンのブレーク (M88gn): メモリの窓で揃った所を見せる
	if (core.GetDebugger()->GetBreakReason() == Debugger::brk_pattern)
		memmon.ShowMatch(core.GetDebugger()->GetBreakAddr(), core.GetDebugger()->GetBreakLength());
	tracemon.Refresh();
	videomon.Refresh();
	return 0;
}

void WinUI::ShowPauseStatus()
{
	// M88gn: 止まった理由 (どのブレークポイントか・読み書きした番地と値・その命令の番地) と、
	// 前に止まってからのメインの CPU のクロック数・時間 (ストップウォッチ)。同じものをステータスの窓にも出す
	// 止めたときは知らせが 2 回来ることがあるので、クロック数が変わっていなければ前の差をそのまま出す
	uint32 tick = core.GetCPUTick();
	bool newstop = !stoptickvalid || tick != stoptick;
	if (newstop)
	{
		laptext[0] = 0;
		if (stoptickvalid)
		{
			uint32 d = tick - stoptick;
			uint clock = core.GetCPUSpeed() ? core.GetCPUSpeed() : 1;	// 100kHz 単位 (40 = 4MHz)
			uint us = uint((unsigned __int64)(d) * 10 / clock);
			wsprintf(laptext, "+%u clk (%u.%.3u ms)", d, us / 1000, us % 1000);
		}
		stoptick = tick;
		stoptickvalid = true;
	}
	char why[160];
	statmon.CaptureStop(why, sizeof(why), laptext);
	// ラップタイムの窓: 止まった CPU とその PC (ブレークでなければメイン)
	if (newstop)
	{
		Debugger* dbg = core.GetDebugger();
		int cpu = dbg->IsBroken() ? int(dbg->GetBreakCpu()) : 0;
		lapmon.Record(tick, cpu, cpu ? core.GetCPU2()->GetPC() : core.GetCPU1()->GetPC(), why);
	}
	statusdisplay.Show(20, 0, "PAUSED  %s  PC:%.4x/%.4x  %s", why, core.GetCPU1()->GetPC(), core.GetCPU2()->GetPC(), laptext);
	statusdisplay.Update();
}

// ---------------------------------------------------------------------------
//	ステップの対象の CPU: ブレークしていれば、ブレークした CPU。そうでなければメイン
//
PC8801::Debugger::CpuTarget WinUI::GetStepTarget()
{
	if (!core.IsStopped())
		return PC8801::Debugger::cpu_main;
	PC8801::Debugger* dbg = core.GetDebugger();
	if (dbg->IsBroken())
		return dbg->GetBreakCpu();
	if (codemon.IsOpen() && codemon.GetShownCpu() == PC8801::Debugger::cpu_sub && core.IsSubCPUActive())
		return PC8801::Debugger::cpu_sub;
	return PC8801::Debugger::cpu_main;
}

// ---------------------------------------------------------------------------
//	WinUI::DebugKey
//	デバッガのキー: 止まっている間 (ステップ中を含む) だけ F5 = Play、F11 = Step In、F10 = Step Over、
//	Shift+F11 = Step Out。動いている間は PC-8801 のキーとして渡す。押したキーは離したのも渡さない
//
bool WinUI::DebugKey(const MSG& msg)
{
	if (msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP)
	{
		if (debugswallow && msg.wParam == debugswallow)
		{
			debugswallow = 0;
			return true;
		}
		return false;
	}
	if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN)
		return false;
	uint vk = (uint) msg.wParam;
	if (vk != VK_F5 && vk != VK_F10 && vk != VK_F11)
		return false;
	if (GetKeyState(VK_MENU) < 0)
		return false;						// Alt+F10 などはほかのショートカット
	// IsStopped は VM のスレッドでブレークした瞬間から立つ (知らせを UI が処理するまでの隙間も含める)
	if (!core.IsStopped() && !core.IsStepping())
		return false;
	uint id = vk == VK_F5 ? IDM_DBG_PLAY : vk == VK_F10 ? IDM_DBG_STEPOVER
			: GetKeyState(VK_SHIFT) < 0 ? IDM_DBG_STEPOUT : IDM_DBG_STEPIN;
	SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(id, 1), 0);
	debugswallow = vk;
	return true;
}

// ---------------------------------------------------------------------------
//	WinUI::M88TapeNotify
//	テープからの知らせを処理する
//
LRESULT WinUI::M88TapeNotify(HWND hwnd, WPARAM code, LPARAM)
{
	switch (code)
	{
	case TapeManager::notify_norecord:
		ShowOverlay("Tape: Record がオフなので、このセーブはテープに記録されません");
		break;

	case TapeManager::notify_notape:
		ShowOverlay("Tape: テープを開いていないので、このセーブは記録されません");
		break;
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::ShowOverlay
//	M88 の画面の中央に、知らせを数秒出す (フォーカスは移さない)
//
void WinUI::ShowOverlay(const char* msg)
{
	if (!hoverlay)
	{
		hoverlay = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
			"STATIC", "", WS_POPUP | WS_BORDER | SS_CENTER | SS_CENTERIMAGE,
			0, 0, 0, 0, hwnd, 0, hinst, 0);
		if (!hoverlay)
			return;
		overlaybrush = CreateSolidBrush(RGB(255, 236, 140));
	}
	// 字の大きさは画面の DPI に合わせる (DPI の違う画面に移ったら作り直す)
	UINT dpi = Dpi::GetForWindow(hwnd);
	if (!overlayfont || overlaydpi != dpi)
	{
		HFONT font = CreateFont(-MulDiv(16, dpi, 96), 0, 0, 0, FW_BOLD, 0, 0, 0, SHIFTJIS_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, "MS UI Gothic");
		if (font)
		{
			SendMessage(hoverlay, WM_SETFONT, (WPARAM) font, 0);
			if (overlayfont)
				DeleteObject(overlayfont);
			overlayfont = font;
			overlaydpi = dpi;
		}
	}

	// 字の大きさから窓の大きさを決める
	RECT tr = { 0, 0, 0, 0 };
	HDC hdc = GetDC(hoverlay);
	HGDIOBJ old = SelectObject(hdc, overlayfont ? (HGDIOBJ) overlayfont : GetStockObject(DEFAULT_GUI_FONT));
	DrawText(hdc, msg, -1, &tr, DT_CALCRECT | DT_SINGLELINE);
	SelectObject(hdc, old);
	ReleaseDC(hoverlay, hdc);
	int w = tr.right - tr.left + 40;
	int h = tr.bottom - tr.top + 24;

	RECT cr;
	GetClientRect(hwnd, &cr);
	POINT c = { (cr.left + cr.right) / 2, (cr.top + cr.bottom) / 2 };
	ClientToScreen(hwnd, &c);

	SetWindowText(hoverlay, msg);
	// 重なり順は変えない (持ち主の本体の窓より上にいつもある。HWND_TOPMOST で上げ直すと本体の窓もいっしょに上がり、
	// 開いているメニューを隠す。StatusDisplay の右下の表示と同じ)
	SetWindowPos(hoverlay, 0, c.x - w / 2, c.y - h / 2, w, h, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
	InvalidateRect(hoverlay, 0, TRUE);
	::SetTimer(hwnd, 9, 4000, 0);
}

// ---------------------------------------------------------------------------
//	WinUI::AppShortcut
//	ショートカットにアプリケーションキーを使う設定のとき、アプリケーションキー + キーを M88 の操作にする。
//	そのキーは、押したのも離したのも PC-8801 に渡さない (Alt はいつも GRPH として渡せる)
//	使ったら true
//
static const struct { uint vk; uint id; const char* name; } appshortcuts[] =
{
	{ VK_RETURN,	IDM_TOGGLEDISPLAY,	"Enter" },
	{ VK_F1,		IDM_SNAPSHOT_LOAD,	"F1" },
	{ VK_F2,		IDM_CAPTURE,		"F2" },
	{ VK_F10,		IDM_SNAPSHOT_SAVE,	"F10" },
	{ VK_F5,		IDM_DBG_PAUSE,		"F5" },			// デバッガ: 止める
	{ 'M',			IDM_SHOWMENU,		"M" },			// メニューを出す (メニューバーを隠しているとき。M88gn)
};

bool WinUI::AppShortcut(const MSG& msg)
{
	if (msg.hwnd != hwnd && !IsChild(hwnd, msg.hwnd))
		return false;
	if (msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP)
	{
		// ショートカットに使ったキーを離した (F12 を離すとリセットになるので、ここで止める)
		if (appswallow && msg.wParam == appswallow)
		{
			appswallow = 0;
			return true;
		}
		return false;
	}
	if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN)
		return false;
	if (!(config.flag2 & Config::appshortcut) || GetKeyState(VK_APPS) >= 0)
		return false;
	for (int i = 0; i < sizeof(appshortcuts) / sizeof(appshortcuts[0]); i++)
	{
		if (msg.wParam == appshortcuts[i].vk)
		{
			if (!(msg.lParam & 0x40000000))		// キーリピートでは繰り返さない
				SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(appshortcuts[i].id, 1), 0);
			appswallow = appshortcuts[i].vk;
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
//	WinUI::SetShortcutLabels
//	メニューのショートカットの表示を、設定 (Alt / アプリケーションキー) に合わせる
//
void WinUI::SetShortcutLabels(HMENU hmenu)
{
	bool app = (config.flag2 & Config::appshortcut) != 0;
	for (int i = 0; i < sizeof(appshortcuts) / sizeof(appshortcuts[0]); i++)
	{
		char buf[128];
		if (!GetMenuString(hmenu, appshortcuts[i].id, buf, sizeof(buf) - 16, MF_BYCOMMAND))
			continue;
		char* tab = strchr(buf, '\t');
		if (!tab)
			continue;				// ショートカットを書いていない項目 (Debug の Step など)
		wsprintf(tab, "\t%s+%s", app ? "App" : "Alt", appshortcuts[i].name);
		MENUITEMINFO mii;
		memset(&mii, 0, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_STRING;		// 文字だけ変える (スナップショットの項目のサブメニューは残す)
		mii.dwTypeData = buf;
		SetMenuItemInfo(hmenu, appshortcuts[i].id, false, &mii);
	}
}

// ---------------------------------------------------------------------------
//	WinUI::SetScaleFlag
//	設定のダイアログの「画面2倍」を、表示の倍率に合わせる (2 倍以上なら入れる)
//
void WinUI::SetScaleFlag()
{
	if (config.screenscale >= 2)
		config.flag2 |= Config::stretchx2;
	else
		config.flag2 &= ~Config::stretchx2;
}

// ---------------------------------------------------------------------------
//	WinUI::WmDpiChanged
//	DPI の違う画面に移った: 位置は Windows の勧めに従い、大きさは表示の倍率どおりに作り直す
//	(枠・メニューの大きさが DPI で変わるため)
//
LRESULT WinUI::WmDpiChanged(HWND hwnd, WPARAM, LPARAM lp)
{
	if (!fullscreen)
	{
		layoutguard++;		// 移る途中の大きさで Free にしない (M88gn)
		// 勧めの大きさは WM_GETDPISCALEDSIZE で返した大きさ
		const RECT* r = (const RECT*) lp;
		SetWindowPos(hwnd, 0, r->left, r->top, r->right - r->left, r->bottom - r->top,
					 SWP_NOZORDER | SWP_NOACTIVATE);
		// ステータスバーの字と高さは作ったときの DPI のままなので、作り直す
		statusdisplay.Disable();
		ShowStatusWindow();			// ステータスバーを作り直し、窓の大きさも直す
		statusdisplay.Update();		// 作り直したステータスバーに知らせを出し直す
		dpimoved = true;
		layoutguard--;
	}
	else
	{
		// 全画面: メニューバーの右の端か右下に重ねた知らせとランプの字を、新しい DPI で作り直す (M88gn)
		ApplyFullScreenMenu();
		statusdisplay.Update();
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::WmGetDpiScaledSize
//	DPI の違う画面に移る前に、移った後の窓の大きさを Windows に知らせる。
//	答えないと Windows は DPI の比で縮めた大きさにし、窓をドラッグしている間は
//	WM_DPICHANGED で直した大きさもその大きさで上書きされる
//
LRESULT WinUI::WmGetDpiScaledSize(HWND hwnd, WPARAM wp, LPARAM lp)
{
	UINT newdpi = (UINT) wp;
	UINT olddpi = Dpi::GetForWindow(hwnd);
	if (fullscreen || !newdpi || !olddpi || scalefree || IsZoomed(hwnd))	// Free なら Windows の勧めの大きさ (M88gn)
		return FALSE;

	uint width, height;
	GetClientSize(width, height);
	RECT rect;
	rect.left = 0;	rect.right = width;
	rect.top  = 0;	rect.bottom = height + MulDiv(statusdisplay.GetHeight(), newdpi, olddpi);
	if (TitleActive())
		rect.bottom += MulDiv(32, newdpi, 96);		// タイトルの段 (uititle.cpp の title_h)
	if (dockactive)		// 開いているペインの分 (M88gn)
	{
		int bits = dock.VisibleBits();
		for (int s = 0; s < DockHost::nsides; s++)
			if (bits & (1 << s))
				(s < DockHost::top ? rect.right : rect.bottom) += MulDiv(dock.DesiredExtent(s), newdpi, olddpi);
	}
	AdjustMainRect(&rect, newdpi);

	SIZE* size = (SIZE*) lp;
	size->cx = rect.right - rect.left;
	size->cy = rect.bottom - rect.top;
	return TRUE;
}

// ---------------------------------------------------------------------------
//	WinUI::GetClientSize
//	画面の部分の大きさ (表示の倍率・回転を入れる。ステータスバーは含まない)
//
void WinUI::GetClientSize(uint& width, uint& height)
{
	width = 640, height = 400;
	if (draw.CanCrtFilter() && (Limit(config.screenrotate, 3, 0) & 1))
		width = 400, height = 640;
	int scale = Limit(config.screenscale, 4, 1);
	width *= scale;
	height *= scale;
}

// ---------------------------------------------------------------------------
//	WinUI::ResizeWindow
//	ウィンドウの大きさを変える
//
void WinUI::ResizeWindow(uint width, uint height)
{
	appliedrot = draw.CanCrtFilter() ? Limit(config.screenrotate, 3, 0) : 0;
	if (appliedrot & 1)
	{
		uint t = width;
		width = height, height = t;
	}
	// ペインを出している間 (M88gn): 画面が表示の倍率の大きさになるように、ペインの分を足した窓にする
	if (dockactive && !fullscreen)
	{
		layoutguard++;
		if (!IsZoomed(hwnd) && !scalefree)		// Free なら窓の大きさは変えない (M88gn)
		{
			int sc = Limit(config.screenscale, 4, 1);
			int bits = dock.VisibleBits();
			RECT rect = { 0, 0, int(width) * sc, int(height) * sc + statusdisplay.GetHeight() };
			for (int s = 0; s < DockHost::nsides; s++)
				if (bits & (1 << s))
					(s < DockHost::top ? rect.right : rect.bottom) += dock.DesiredExtent(s);
			rect.bottom += TitleHeight();
			AdjustMainRect(&rect);
			SetWindowPos(hwnd, 0, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
						 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		dock.Layout();
		layoutguard--;
		return;
	}
	int scale = Limit(config.screenscale, 4, 1);
	width *= scale;
	height *= scale;

	RECT rect;
	rect.left = 0;	rect.right = width;
	rect.top  = 0;  rect.bottom = height + statusdisplay.GetHeight();

	int th = TitleHeight();		// タイトルの段 (M88gn)
	rect.bottom += th;
	AdjustMainRect(&rect);		// メニューバーは隠していることがある
	SetWindowPos(hwnd, 0, 0, 0, rect.right-rect.left, rect.bottom-rect.top,
				 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	PostMessage(hwnd, WM_SIZE, SIZE_RESTORED, MAKELONG(width, height));
	RECT v = { 0, th, int(width), th + int(height) };
	draw.SetViewRect(th ? &v : 0);	// 画面はタイトルの段の下
	draw.Resize( width, height );
}

// ---------------------------------------------------------------------------
//	ステータスバー表示切り替え
//
void WinUI::ShowStatusWindow()
{
	if (!fullscreen)
	{
		layoutguard++;		// M88gn
		if (config.flags & PC8801::Config::showstatusbar)
			statusdisplay.Enable((config.flags & PC8801::Config::showfdcstatus) != 0);
		else
			statusdisplay.Disable();
		SetMenuBar(!HideMenuBar());
		ResizeWindow(640, 400);
		layoutguard--;
	}
	else
		ApplyFullScreenMenu();		// PC-8801 のマウスの設定が変わったときなど
}

// ---------------------------------------------------------------------------
//	WinUI::WmDrawItem
//	WM_DRAWITEM ハンドラ
//
LRESULT WinUI::WmDrawItem(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*) lparam;
	if (dis->CtlType == ODT_MENU && dis->itemID == IDM_STATUS_MENU)
		statusdisplay.DrawMenuItem(dis);
	else if ((UINT) wparam == 1)
		statusdisplay.DrawItem(dis);
	return TRUE;
}

// ---------------------------------------------------------------------------
//	WinUI::WmMeasureItem
//	全画面のメニューバーの知らせ・ランプの項目の大きさ
//
LRESULT WinUI::WmMeasureItem(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*) lparam;
	if (mis->CtlType == ODT_MENU && mis->itemID == IDM_STATUS_MENU)
	{
		statusdisplay.MeasureMenuItem(mis);
		return TRUE;
	}
	return DefWindowProc(hwnd, WM_MEASUREITEM, wparam, lparam);
}

LRESULT WinUI::M88StatusMenu(HWND, WPARAM, LPARAM)
{
	statusdisplay.RedrawMenu();
	statusdisplay.RedrawOverlay();
	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::ToggleDisplayMode
//	全画面・ウィンドウ表示切替  (ALT+ENTER)
//
void WinUI::ToggleDisplayMode()
{
	uint tick = GetTickCount();
	if ((tick - displaychangedtime) < 1000)
		return;

	displaychangedtime = GetTickCount();
	if (!fullscreen && dockactive)
		GetWindowRect(hwnd, &dockrect);		// ドックモード: 戻すときの位置・大きさ (M88gn)
	fullscreen = !fullscreen;

	if (fullscreen)
		statusdisplay.Disable();
	ChangeDisplayType(fullscreen);
}

// ---------------------------------------------------------------------------
//	WinUI::ChangeDisplayType
//	表示メソッド変更
//
void WinUI::ChangeDisplayType(bool savepos)
{
	if (winconfig.IsOpen())
	{
		winconfig.Close();
		SetGUIFlag(false);
	}
	if (savepos)
	{
		RECT rect;
		GetWindowRect(hwnd, &rect);
		point.x = rect.left; point.y = rect.top;
	}
	PostMessage(hwnd, WM_M88_CHANGEDISPLAY, 0, 0);
}

// ---------------------------------------------------------------------------
//	WinUI::M88ChangeDisplay
//	表示メソッドの変更
//
LRESULT WinUI::M88ChangeDisplay(HWND hwnd, WPARAM, LPARAM)
{
	// 画面ドライバの切替え
	// ドライバが false を返した場合 GDI ドライバが使用されることになる
	// ドックモードは窓でも縦横比を保って拡大する描き方 (M88gn)。全画面の間はペインを隠す
	layoutguard++;		// 全画面の行き来で Free にしない (M88gn)
	dock.SetSuspended(fullscreen);
	dock.SetTopInset(TitleHeight());
	if (fullscreen)
		draw.SetViewRect(0);		// 全画面は窓いっぱい (窓の表示はタイトルの段の下。M88gn)
	UpdateDockButtons();
	if (!draw.ChangeDisplayMode
				(fullscreen || dockactive,
				(config.flags & PC8801::Config::force480) != 0,
				Limit(config.screenscale, 4, 1)))
		fullscreen = false;

	// 全画面: マウスが止まってしばらくしたらカーソルを消す。ウィンドウに戻ったら出す
	if (cursorhidden && !fullscreen)
		SetCursor(LoadCursor(0, IDC_ARROW));
	cursorhidden = false;
	RestartCursorTimer();

	// ウィンドウスタイル関係の変更
	wstyle = (DWORD)GetWindowLongPtr(hwnd, GWL_STYLE);
	LONG_PTR exstyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

	if (!fullscreen)
	{
		wstyle = (wstyle & ~WS_POPUP) | (WS_CAPTION | WS_OVERLAPPED | WS_SYSMENU);
		if (dockactive)
			wstyle |= WS_THICKFRAME | WS_MAXIMIZEBOX;	// ドックモード (M88gn)
		exstyle &= ~WS_EX_TOPMOST;
		
//		SetCapture(hwnd);
		SetWindowLongPtr(hwnd, GWL_STYLE, wstyle);
		SetWindowLongPtr(hwnd, GWL_EXSTYLE, exstyle);
		statusdisplay.DisableOverlay();
		statusdisplay.DisableMenu();
		SetMenuBar(!HideMenuBar());		// ウィンドウ表示の設定に合わせる
		ResizeWindow(640, 400);
		if (dockactive && dockrect.right > dockrect.left)	// ドックモード: 全画面の前の位置・大きさに (M88gn)
			SetWindowPos(hwnd, HWND_NOTOPMOST, dockrect.left, dockrect.top, dockrect.right - dockrect.left,
						 dockrect.bottom - dockrect.top, SWP_FRAMECHANGED);
		else
			SetWindowPos(hwnd, HWND_NOTOPMOST, point.x, point.y, 0, 0, SWP_NOSIZE | SWP_FRAMECHANGED);
		ShowStatusWindow();
		report = true;
		guimodebymouse = false;
	}
	else
	{
		if (guimodebymouse)
			SetGUIFlag(false);
			
//	ReleaseCapture();
		wstyle = (wstyle & ~(WS_CAPTION | WS_OVERLAPPED | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX)) | WS_POPUP;	// ペインの間の枠も外す (M88gn)
		exstyle |= WS_EX_TOPMOST;
		SetWindowLongPtr(hwnd, GWL_STYLE, wstyle);
		SetWindowLongPtr(hwnd, GWL_EXSTYLE, exstyle);
		SetWindowText(hwnd, "M88");

		// 解像度は変えず、M88 のあるモニターいっぱいにする (メニューバーは上に出したまま)
		MONITORINFO mi;
		memset(&mi, 0, sizeof(mi));
		mi.cbSize = sizeof(mi);
		GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
		RECT& mr = mi.rcMonitor;
		SetWindowPos(hwnd, HWND_TOPMOST, mr.left, mr.top, mr.right - mr.left, mr.bottom - mr.top,
					 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		draw.Resize(640, 400);
		// メニューバーを隠すか。ステータスバーの代わりに、知らせとランプをメニューバーの右の端
		// (隠しているなら画面の右下に重ねて) 出す
		ApplyFullScreenMenu();

		report = false;
	}
	SetGUIFlag(true);
	SetGUIFlag(false);
	SetGUIFlag(true);
	SetGUIFlag(false);
	layoutguard--;
	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::WmEnterMenuLoop
//	WM_ENTERMENULOOP ハンドラ
//
LRESULT WinUI::WmEnterMenuLoop(HWND, WPARAM wp, LPARAM)
{
	if (!wp)
	{
		keyif.Activate(false);
		SetGUIFlag(true);
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::WmExitMenuLoop
//	WM_EXITMENULOOP ハンドラ
//
LRESULT WinUI::WmExitMenuLoop(HWND, WPARAM wp, LPARAM)
{
	if (!wp)
	{
		keyif.Activate(true);
		SetGUIFlag(false);
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	ボリューム変更
//
LRESULT WinUI::M88ChangeVolume(HWND, WPARAM c, LPARAM)
{
	if (c)
	{
		core.SetVolume((PC8801::Config*) c);
		core.GetSound()->SetMasterVolume(((PC8801::Config*) c)->volmaster);
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::WmDisplayChange
//	WM_DISPLAYCHANGE ハンドラ
//
LRESULT WinUI::WmDisplayChange(HWND hwnd, WPARAM wp, LPARAM lp)
{
	resetwindowsize = fullscreen ? 0 : 5;
	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::DropFiles
//	WM_DROPFILES ハンドラ
//	午後Ｔ氏の実装をもとに作成しました．
//
LRESULT WinUI::WmDropFiles(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	HDROP hdrop = (HDROP) wparam;

	// 受け取ったファイルの数を確認
	int nfiles = DragQueryFile(hdrop, ~0, 0, 0);
	if (nfiles != 1)
	{
		statusdisplay.Show(50, 3000, "ドロップできるのはファイル１つだけです.");
		DragFinish(hdrop);
		return 0;
	}

	// ファイルネームを取得
	char path[MAX_PATH];
	DragQueryFile(hdrop, 0, path, 512);
	DragFinish(hdrop);

	char ext[_MAX_EXT];
	{
		char drive[_MAX_DRIVE];
		char dir[_MAX_DIR];
		char fname[_MAX_FNAME];
		_splitpath(path, drive, dir, fname, ext);
	}

	if (strcmpi(ext, ".d88") == 0 || strcmpi(ext, ".fdx") == 0) {
		// 入っていたディスクを取り出す音、入れた音
		bool was[2] = { DiskMounted(0), DiskMounted(1) };
		OpenDiskImage(path);
		for (int i = 0; i < 2; i++)
			if (was[i])
				core.FDDMechSound(false);
		for (int i = 0; i < 2; i++)
			if (DiskMounted(i))
				core.FDDMechSound(true);

		// 正常にマウントできたらリセット
		if (diskinfo[0].filename[0])
		  Reset();
	} else if (strcmpi(ext, ".t88") == 0 || strcmpi(ext, ".cmt") == 0 || strcmpi(ext, ".raw") == 0) {
		OpenTapeImage(path);
	} else {
		statusdisplay.Show(50, 3000, "非対応のファイルです.");
	}

	return 0;
}

// ---------------------------------------------------------------------------
//	WinUI::CaptureScreen
//
void WinUI::CaptureScreen()
{
	int bmpsize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFO) 
					  + 15 * sizeof(RGBQUAD) + 320 * 400;
	uint8* bmp = new uint8[bmpsize];
	if (!bmp) return;
	
	bmpsize = draw.CaptureScreen(bmp);
	if (bmpsize)
	{
		bool save;
		char filename[MAX_PATH];
		
		if (config.flag2 & Config::genscrnshotname)
		{
			SYSTEMTIME time;
			GetLocalTime(&time);
			wsprintf(filename, "%.2d%.2d%.2d%.2d%.2d.bmp", 
					 time.wDay, time.wHour, time.wMinute, time.wSecond,
					 time.wMilliseconds / 10);
			save = true;
			statusdisplay.Show(80, 1500, "画面イメージを %s に保存しました", filename);
		}
		else
		{
			filename[0] = 0;
			
			OFNV5 ofn;
			memset(&ofn, 0, sizeof(ofn));
			ofn.lStructSize = WINVAR(OFNSIZE);
			ofn.hwndOwner = hwnd;
			ofn.lpstrFilter = "bitmap image [4bpp] (*.bmp)\0*.bmp\0";
			ofn.lpstrFile = filename;
			ofn.nMaxFile = MAX_PATH;
			ofn.Flags = OFN_CREATEPROMPT | OFN_NOREADONLYRETURN;
			ofn.lpstrDefExt = "bmp";
			ofn.lpstrTitle = "Save captured image";
			ofn.FlagsEx = 0;		// 位置バーを消す指定 (OFN_EX_NOPLACESBAR) を付けると、Windows が古い形のダイアログを出す (DPI 対応の窓ではツールバーのアイコンが小さい)
			
			SetGUIFlag(true);
			(*EnableIME)(hwnd, true);
			save = !!GetSaveFileName(&ofn);
			(*EnableIME)(hwnd, false);
			SetGUIFlag(false);
		}
		if (save)
		{
			FileIO file;
			if (file.Open(filename, FileIO::create))
				file.Write(bmp, bmpsize);
		}
	}
	delete[] bmp;
}

// ---------------------------------------------------------------------------
//	マウス状態の取得
//
LRESULT WinUI::WmLButtonDown(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	if (capturemouse)
	{
		mousebutton |= 1;
		return 0;
	}
	return DefWindowProc(hwnd, WM_LBUTTONDOWN, wparam, lparam);
}

LRESULT WinUI::WmLButtonUp(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	if (capturemouse)
	{
		mousebutton &= ~1;
		return 0;
	}
	return DefWindowProc(hwnd, WM_LBUTTONDOWN, wparam, lparam);
}

LRESULT WinUI::WmRButtonDown(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	if (capturemouse)
	{
		mousebutton |= 2;
		return 0;
	}
	return DefWindowProc(hwnd, WM_LBUTTONDOWN, wparam, lparam);
}

LRESULT WinUI::WmRButtonUp(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	if (capturemouse)
		mousebutton &= ~2;

	// 右クリックで本体のメニューを出す (PC-8801 のマウスを使う設定なら、右ボタンは PC-8801 のもの)。
	// 描画の子の窓の上で離したときは、子の窓の WM_CONTEXTMENU が届く (WmContextMenu)
	if (!(config.flags & Config::enablemouse) && (short) HIWORD(lparam) >= TitleHeight())
	{
		POINT pt;
		GetCursorPos(&pt);
		ShowFullScreenMenu(pt.x, pt.y);
		return 0;
	}
	if (capturemouse)
		return 0;
	return DefWindowProc(hwnd, WM_LBUTTONDOWN, wparam, lparam);
}

// ---------------------------------------------------------------------------
//	WinUI::WmContextMenu
//	描画の子の窓 (D3D11・D2D) の上で右ボタンを離すと、子の窓の DefWindowProc が親に送ってくる
//
LRESULT WinUI::WmContextMenu(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	// キーボード (Shift+F10・アプリケーションキー) からのものは使わない (アプリケーションキーはショートカットに使うため)。
	// タイトルバー・メニューバーの上 (窓の中でない所) は Windows に任せる (タイトルバーならシステムメニュー)
	if (!(config.flags & Config::enablemouse) && lparam != (LPARAM) -1
		&& SendMessage(hwnd, WM_NCHITTEST, 0, lparam) == HTCLIENT)
	{
		POINT pt = { (short) LOWORD(lparam), (short) HIWORD(lparam) };
		ScreenToClient(hwnd, &pt);
		if (pt.y >= TitleHeight())		// タイトルの段の上では出さない
			ShowFullScreenMenu((short) LOWORD(lparam), (short) HIWORD(lparam));
		return 0;
	}
	return DefWindowProc(hwnd, WM_CONTEXTMENU, wparam, lparam);
}

// ---------------------------------------------------------------------------
//	WinUI::ShowFullScreenMenu
//	右クリック: 本体のメニューバーと同じ中身 (Control・Disk・…) をポップアップで出す (M88gn)
//
void WinUI::ShowFullScreenMenu(int x, int y)
{
	if (!hmenumain)
		return;
	HMENU hpopup = CreatePopupMenu();
	if (!hpopup)
		return;

	// メニューバーの各項目のサブメニューを、そのまま借りて並べる (知らせ・ランプの項目は飛ばす)
	int n = GetMenuItemCount(hmenumain);
	int added = 0;
	for (int i=0; i<n; i++)
	{
		HMENU sub = GetSubMenu(hmenumain, i);
		char text[64];
		if (!sub || !GetMenuString(hmenumain, i, text, sizeof(text), MF_BYPOSITION))
			continue;
		if (AppendMenu(hpopup, MF_POPUP | MF_STRING, (UINT_PTR) sub, text))
			added++;
	}

	// メニューの間は PC-8801 にキーを渡さず、エミュレーションを止める (メニューバーのときと同じ)
	keyif.Activate(false);
	SetGUIFlag(true);
	if (cursorhidden)
		SetCursor(LoadCursor(0, IDC_ARROW));

	// オーナーがフォアグラウンドでないと、出た直後に消えることがある (TrackPopupMenu の決まりごと)
	SetForegroundWindow(hwnd);
	// 選んだ項目はメニューが WM_COMMAND で送る (TPM_RETURNCMD は使わない。設定 (IDM_CONFIG) の ID は 0 で、
	// 返り値では「何も選ばなかった」と区別できないため)
	TrackPopupMenu(hpopup, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, x, y, 0, hwnd, 0);
	PostMessage(hwnd, WM_NULL, 0, 0);

	// 借りたサブメニューは外してから消す (DestroyMenu はサブメニューも消すため)
	while (added-- > 0)
		RemoveMenu(hpopup, 0, MF_BYPOSITION);
	DestroyMenu(hpopup);

	SetGUIFlag(false);
	keyif.Activate(true);
	RestartCursorTimer();
}

// ---------------------------------------------------------------------------
//	WinUI::ApplyFullScreenMenu
//	全画面: 設定に合わせてメニューバーを隠す・出す。知らせとランプは、メニューバーを出しているなら
//	その右の端に、隠しているなら画面の右下に重ねて出す (M88gn)
//
void WinUI::ApplyFullScreenMenu()
{
	if (!fullscreen || !hmenumain)
		return;
	bool show = (config.flags & PC8801::Config::showstatusbar) != 0;
	bool showfd = (config.flags & PC8801::Config::showfdcstatus) != 0;
	if (HideMenuBar())
	{
		statusdisplay.DisableMenu();
		SetMenuBar(false);
		if (show)
			statusdisplay.EnableOverlay(showfd);
		else
			statusdisplay.DisableOverlay();
	}
	else
	{
		statusdisplay.DisableOverlay();
		SetMenuBar(true);
		if (show)
			statusdisplay.EnableMenu(showfd);
		else
			statusdisplay.DisableMenu();
	}
}

// ---------------------------------------------------------------------------
//	WinUI::HideMenuBar
//	今の表示 (全画面・ウィンドウ) でメニューバーを隠すか (M88gn)。
//	PC-8801 のマウスを使う設定では、右クリックでメニューを出せないので隠さない
//
bool WinUI::HideMenuBar()
{
	if (config.flags & Config::enablemouse)
		return false;
	return (fullscreen ? config.fullhidemenu : config.winhidemenu) != 0;
}

bool WinUI::SetMenuBar(bool show)
{
	if (!hmenumain)
		return false;
	HMENU want = (show && !TitleActive()) ? hmenumain : 0;	// 窓の表示はタイトルの段 (M88gn)
	InvalidateTitle();
	if (GetMenu(hwnd) == want)
		return false;
	SetMenu(hwnd, want);
	return true;
}

// ---------------------------------------------------------------------------
//	ウィンドウ移動モードに入る・出る
//
LRESULT WinUI::WmEnterSizeMove(HWND hwnd, WPARAM, LPARAM)
{
//	core.ActivateMouse(false);
	return 0;
}

LRESULT WinUI::WmExitSizeMove(HWND hwnd, WPARAM, LPARAM)
{
//	core.ActivateMouse(true);
	// ドラッグの間に DPI が変わったら、動かし終えたところで大きさを確かめ直す
	if (dpimoved)
	{
		dpimoved = false;
		if (!fullscreen)
			ResizeWindow(640, 400);
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	WM_MOVE
//
LRESULT WinUI::WmMove(HWND hwnd, WPARAM, LPARAM)
{
	POINT srcpoint;
	srcpoint.x = 0, srcpoint.y = 0;
	ClientToScreen(hwnd, &srcpoint);

	draw.WindowMoved(srcpoint.x, srcpoint.y);
	return 0;
}

// ---------------------------------------------------------------------------
//	WM_M88_CLIPCURSOR
//
LRESULT WinUI::M88ClipCursor(HWND hwnd, WPARAM op, LPARAM)
{
	if (int(op) > 0)
		clipmode |= op;
	else
		clipmode &= ~(-(int)op);

	if (clipmode && !(clipmode & CLIPCURSOR_RELEASE))
	{
		RECT rect;
		POINT center;
		GetWindowRect(hwnd, &rect);

		if (clipmode & CLIPCURSOR_MOUSE)
		{
			center.x = (rect.left + rect.right) / 2;
			center.y = (rect.top + rect.bottom) / 2;
			rect.left   = center.x - 180;
			rect.right  = center.x + 180;
			rect.top    = center.y - 180;
			rect.bottom = center.y + 180;
		}				
		LOG4("rect: %d %d %d %d\n", rect.left, rect.top, rect.right, rect.bottom);
		ClipCursor(&rect);
	}
	else
	{
		ClipCursor(0);
	}
	return 1;
}

LRESULT WinUI::WmMouseMove(HWND hwnd, WPARAM wp, LPARAM lp)
{
/*	if (fullscreen)
	{
		POINTS p;
		p = MAKEPOINTS(lp);
		uint menu = p.y < 8;
		if (!guimodebymouse)
		{
			if (menu)
			{
				guimodebymouse = true;
				SetGUIFlag(true);
			}
		}
		else
		{
			if (!menu)
			{
				guimodebymouse = false;
				SetGUIFlag(false);
			}
		}
	}
*/	return 0;

}

// ---------------------------------------------------------------------------
//	全画面でカーソルを消すまでの時間を数え直す
//
void WinUI::RestartCursorTimer()
{
	if (fullscreen)
		::SetTimer(hwnd, cursortimer, cursorhidems, 0);
	else
		KillTimer(hwnd, cursortimer);
}

// ---------------------------------------------------------------------------
//	カーソルが画面 (本体の窓のクライアント領域。メニューバーは除く) の上にあるか
//
bool WinUI::CursorOnScreen()
{
	POINT pt;
	if (!GetCursorPos(&pt))
		return false;
	HWND hw = WindowFromPoint(pt);
	if (hw != hwnd && GetParent(hw) != hwnd)
		return false;
	RECT rc;
	GetClientRect(hwnd, &rc);
	ScreenToClient(hwnd, &pt);
	return PtInRect(&rc, pt) != 0;
}

LRESULT WinUI::WmSetCursor(HWND hwnd, WPARAM wp, LPARAM lp)
{
	// 全画面: マウスが動いたらカーソルを出し、止まってしばらくしたら消す
	if (fullscreen && LOWORD(lp) == HTCLIENT)
	{
		POINT pt;
		GetCursorPos(&pt);
		if (pt.x != cursorpos.x || pt.y != cursorpos.y)
		{
			cursorpos = pt;
			cursorhidden = false;
			RestartCursorTimer();
		}
		if (cursorhidden)
		{
			SetCursor(0);
			return TRUE;
		}
	}
	else if (cursorhidden)
	{
		cursorhidden = false;
		RestartCursorTimer();
	}

	if (fullscreen)
	{
		uint menu = LOWORD(lp) == HTMENU;
		if (!guimodebymouse)
		{
			if (menu)
			{
				guimodebymouse = true;
				SetGUIFlag(true);
			}
		}
		else
		{
			if (!menu)
			{
				guimodebymouse = false;
				SetGUIFlag(false);
			}
		}
	}
//	statusdisplay.Show(0, 0, "%d", LOWORD(lp));
	return DefWindowProc(hwnd, WM_SETCURSOR, wp, lp);
}

// ---------------------------------------------------------------------------
//	GUI 操作モードに入る
//
void WinUI::SetGUIFlag(bool gui)
{
	if (gui && !background)
	{
		if (!background)
			::DrawMenuBar(hwnd);
	}
//	core.SetGUIFlag(gui);
	draw.SetGUIFlag(gui);
}

// ---------------------------------------------------------------------------
//	スナップショットの名前を作る
//
void WinUI::GetSnapshotName(char* name, int n)
{
	char buf[MAX_PATH];
	char* title;

	if (diskmgr->GetNumDisks(0))
	{
		GetFileNameTitle(buf, sizeof(buf), diskinfo[0].filename);
		title = buf;
	}
	else if (tapemgr->IsOpen())
	{
		title = tapetitle;
	}
	else
	{
		title = "snapshot";
	}
	
	if (n >= 0)
		wsprintf(name, "%s_%d.s88", title, n);
	else
		wsprintf(name, "%s_?.s88", title);
}

// ---------------------------------------------------------------------------
//	スナップショットの書込み
//
void WinUI::SaveSnapshot(int n)
{
	char name[MAX_PATH];
	GetSnapshotName(name, n);
	if (core.SaveShapshot(name))
		statusdisplay.Show(80, 3000, "%s に保存しました", name);
	else
		statusdisplay.Show(80, 3000, "%s に保存できません", name);
	currentsnapshot = n;
	snapshotchanged = true;
}

// ---------------------------------------------------------------------------
//	スナップショットの読み込み
//
void WinUI::LoadSnapshot(int n)
{
	char name[MAX_PATH];
	GetSnapshotName(name, n);
	bool r;
	if (diskinfo[0].filename && diskmgr->GetNumDisks(0) >= 2)
	{
		OpenDiskImage(1, diskinfo[0].filename, diskinfo[0].readonly, 1, false);
		r = core.LoadShapshot(name, diskinfo[0].filename);
	}
	else
	{
		r = core.LoadShapshot(name, 0);
	}

	if (r)
		statusdisplay.Show(80, 2500, "%s から復元しました", name);
	else
		statusdisplay.Show(80, 2500, "%s から復元できません", name);
	for (uint i=0; i<2; i++)
		CreateDiskMenu(i);
	currentsnapshot = n;
	snapshotchanged = true;
}	

// ---------------------------------------------------------------------------
//	スナップショット用のメニューを作成
//
bool WinUI::MakeSnapshotMenu()
{
	if (snapshotchanged)
	{
		int i;
		snapshotchanged = false;

		// メニューを元に戻す
		MENUITEMINFO mii;
		memset(&mii, 0, sizeof(mii));
		mii.cbSize = WINVAR(MIISIZE);
		mii.fMask = MIIM_SUBMENU;
		mii.hSubMenu = 0;
		SetMenuItemInfo(hmenumain, IDM_SNAPSHOT_LOAD, false, &mii);
		SetMenuItemInfo(hmenumain, IDM_SNAPSHOT_SAVE, false, &mii);
		
		// メニュー作成
		for (i=0; i<2; i++)
		{
			if (IsMenu(hmenuss[i]))
				DestroyMenu(hmenuss[i]);
			hmenuss[i] = CreatePopupMenu();
			if (!hmenuss[i])
				return false;
		}
		
		// スナップショットを検索
		int entries = 0;
		FileFinder ff;
		char buf[MAX_PATH];

		GetSnapshotName(buf, -1);
		size_t p = strlen(buf) - 5;
		ff.FindFile(buf);
		while (ff.FindNext())
		{
			int n = ff.GetFileName()[p] - '0';
			if (0 <= n && n <= 9)
				entries |= 1 << n;
		}

		// メニュー作成
		for (i=0; i<10; i++)
		{
			wsprintf(buf, "&%d", i);
			AppendMenu(hmenuss[0], MF_STRING, IDM_SNAPSHOT_SAVE_0 + i, buf);
			
			if (entries & (1 << i))
			{
				CheckMenuItem(hmenuss[0], IDM_SNAPSHOT_SAVE_0 + i, MF_BYCOMMAND | MF_CHECKED);
				AppendMenu(hmenuss[1], MF_STRING, IDM_SNAPSHOT_LOAD_0 + i, buf);
				if (i == currentsnapshot)
					SetMenuDefaultItem(hmenuss[1], IDM_SNAPSHOT_LOAD_0 + i, FALSE );
			}
		}
		SetMenuDefaultItem(hmenuss[0], IDM_SNAPSHOT_SAVE_0 + currentsnapshot, FALSE );
		
		mii.hSubMenu = hmenuss[0];
		SetMenuItemInfo(hmenumain, IDM_SNAPSHOT_SAVE, false, &mii);
		if (entries)
		{
			mii.hSubMenu = hmenuss[1];
			SetMenuItemInfo(hmenumain, IDM_SNAPSHOT_LOAD, false, &mii);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
//	コマンドライン走査
//
//	書式:
//	M88 [-flags] diskimagepath
//
//	-bN		basic mode (hex)
//	-fA,B	flags (16進)	(A=flagsの中身, B=反映させるビット)
//	-gA,B	flag2 (16進)	(A=flag2の中身, B=反映させるビット)
//	-cCLK	clock (MHz) (10進)
//	-F		フルスクリーン起動
//
//	N, A, B の値は src/pc88/config.h を参照．
//	
//	現バージョンでは設定ダイアログでは設定できない組み合わせも
//	受け付けてしまうので要注意
//	例えばマウスを使用しながらキーによるメニュー抑制なんかはヤバイかも．
//
//	例:	M88 -b31 -c8 -f10,10 popfulmail.d88
//		V2 モード，8MHz，OPNA モードで popfulmail.d88 を起動する
//	
//	説明が分かりづらいのは百も承知です(^^;
//	詳しくは下を参照するか，開発室にでも質問してください．
//
//	他のパラメータも変更できるようにしたい場合も，一言頂ければ対応します．
//
void WinUI::ApplyCommandLine(const char* cmdline)
{
	bool change = false;
	while (*cmdline)
	{
		while (*cmdline == ' ')
			cmdline++;

		if (*cmdline == '-')
		{
			cmdline += 2;
			switch (cmdline[-1])
			{
				char* endptr;
				int32 newflags, activate;

			// BASIC モードを設定  -bモード番号
			case 'b':
				config.basicmode =
					Config::BASICMode(strtoul(cmdline, &endptr, 16));
				cmdline = endptr;
				change = true;
				break;

			// clock を設定  -cクロック
			case 'c':
				config.clock = Limit(strtoul(cmdline, &endptr, 10), 100, 1) * 10;
				cmdline = endptr;
				change = true;
				break;

			// フルスクリーン起動
			case 'F':
				fullscreen = true;
				break;

			// flags の値を設定  -g値,有効ビット
			case 'f':
				newflags = strtoul(cmdline, &endptr, 16);
				activate = ~0;
				if (*endptr == ',')
				{
					activate = strtoul(endptr+1, &endptr, 16);
				}
				cmdline = endptr;
				config.flags = (config.flags & ~activate) | (newflags & activate);
				change = true;
				break;

			// flag2 の値を設定  -g値,有効ビット
			case 'g':
				newflags = strtoul(cmdline, &endptr, 16);
				activate = ~0;
				if (*endptr == ',')
				{
					activate = strtoul(endptr+1, &endptr, 16);
				}
				cmdline = endptr;
				config.flag2 = (config.flag2 & ~activate) | (newflags & activate);
				change = true;
				break;
			}

			while (*cmdline && *cmdline != ' ')
				cmdline++;
			continue;
		}

		// 残りは多分ファイル名
		OpenDiskImage(cmdline);
		break;
	}
}

// ---------------------------------------------------------------------------
//	録画 (M88gn): 始める・止める。保存先は始めるときに選ぶ
//
void WinUI::VideoAudioCb(void*, const int32* s, int samples, int rate)
{
	videorecorder.AddAudio((const int*) s, samples, rate);
}

void WinUI::ToggleVideoRecord()
{
	if (videorecorder.IsActive())
	{
		core.Lock();			// エミュレーションを止めてから (絵と音を渡している最中に終えないように)
		videorecorder.Stop();
		core.Unlock();
		return;
	}
	if (!draw.CanCrtFilter())
	{
		statusdisplay.Show(100, 4000, "録画は Direct3D 11 で描いているときだけ使えます");
		return;
	}
	char filename[MAX_PATH];
	SYSTEMTIME t;
	GetLocalTime(&t);
	wsprintf(filename, "M88_%.4d%.2d%.2d_%.2d%.2d%.2d.mp4", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	OPENFILENAME ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwnd;
	ofn.lpstrFilter = "MP4 video (*.mp4)\0*.mp4\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = sizeof(filename);
	ofn.lpstrDefExt = "mp4";
	ofn.lpstrTitle = "Record Video";
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
	SetGUIFlag(true);
	bool ok = GetSaveFileName(&ofn) != 0;
	SetGUIFlag(false);
	if (!ok)
		return;
	int mode = GetPrivateProfileInt("M88p2 for Windows", "VideoMode", 0, PC8801::GetM88IniPath()) ? VideoRecorder::raw : VideoRecorder::filtered;
	core.Lock();
	int fps = GetPrivateProfileInt("M88p2 for Windows", "VideoFps", 60, PC8801::GetM88IniPath());
	bool r = videorecorder.Prepare(filename, mode, fps);
	core.Unlock();
	if (!r)
		statusdisplay.Show(100, 5000, "録画を始められません: %s", videorecorder.GetError());
}
