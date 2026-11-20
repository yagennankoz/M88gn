// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1998, 1999.
// ---------------------------------------------------------------------------
//	User Interface for Win32
// ---------------------------------------------------------------------------
//	$Id: ui.h,v 1.33 2002/05/31 09:45:21 cisc Exp $

#pragma once

#include "types.h"
#include "wincore.h"
#include "WinDraw.h"
#include "WinKeyIF.h"
#include "88config.h"
#include "wincfg.h"
#include "newdisk.h"
#include "soundmon.h"
#include "memmon.h"
#include "codemon.h"
#include "basmon.h"
#include "regmon.h"
#include "loadmon.h"
#include "iomon.h"
#include "bpmon.h"
#include "watchmon.h"
#include "tracemon.h"
#include "dbgsettings.h"
#include "softkbd.h"
#include "tapewin.h"

// ---------------------------------------------------------------------------

class WinUI
{
public:
	WinUI(HINSTANCE hinst);
	~WinUI();
	
	bool InitWindow(int nwinmode);
	int  Main(const char* cmdline);
	uint GetMouseButton() { return mousebutton; }
	HWND GetHWnd() { return hwnd; }

private:
	struct DiskInfo
	{
		HMENU hmenu;
		int currentdisk;
		int idchgdisk;
		bool readonly;
		char filename[MAX_PATH];
	};
	struct ExtNode
	{
		ExtNode* next;
		IWinUIExtention* ext;
	};

private:
	bool InitM88(const char* cmdline);
	void CleanupM88();
	LRESULT WinProc(HWND, UINT, WPARAM, LPARAM);
	static LRESULT CALLBACK WinProcGate(HWND, UINT, WPARAM, LPARAM);
	void ReportError();
	void Reset();

	void ApplyConfig();
	void SetCrtFilter();
	void ApplyCommandLine(const char* cmdline);

	void ToggleDisplayMode();
	void ChangeDisplayType(bool savepos);

	void ChangeDiskImage(HWND hwnd, int drive);
	bool OpenDiskImage(int drive, const char* filename, bool readonly, int id, bool create);
	void OpenDiskImage(const char* filename);
	bool DiskMounted(int drive);
	bool SelectDisk(uint drive, int id, bool menuonly);
	bool CreateDiskMenu(uint drive);

	void ChangeTapeImage();
	void NewDiskImage(int drive);		// 新しいディスクイメージを作る (drive: 入れるドライブ、-1 なら入れない。M88gn)
	void NewTapeImage();				// 空の T88 を作って開く (M88gn)
	bool AskNewImageName(char* filename, const char* filter, const char* defext, const char* title);
	void OpenTapeImage(const char* filename, bool readonly = false);
	static void TapeNotify(void* ctx, int code);
	void ShowOverlay(const char* msg);
	void ApplyFullScreenMenu();			// 全画面: メニューバーを隠す・出す、知らせとランプの置き場所 (M88gn)
	bool HideMenuBar();					// 今の表示 (全画面・ウィンドウ) でメニューバーを隠すか (M88gn)
	bool SetMenuBar(bool show);			// メニューバーを出す・隠す。変わったら true (M88gn)
	void ShowFullScreenMenu(int x, int y);	// 右クリックで本体のメニューを出す (全画面・ウィンドウとも。M88gn)

	void ShowStatusWindow();
	void ResizeWindow(uint width, uint height);
	void GetClientSize(uint& width, uint& height);
	void SetScaleFlag();
	void SetGUIFlag(bool);

	void SaveSnapshot(int n);
	void LoadSnapshot(int n);
	void GetSnapshotName(char*, int);
	bool MakeSnapshotMenu();

	void CaptureScreen();

	void SaveWindowPosition();
	void LoadWindowPosition();

	int AllocControlID();
	void FreeControlID(int);

	// ウインドウ関係
	HWND hwnd;
	HINSTANCE hinst;
	HACCEL accel;
	HACCEL accelnoalt;			// ショートカットにアプリケーションキーを使うときの表
	uint appswallow;			// アプリケーションキーのショートカットに使ったキー (離したのも PC-8801 に渡さない)
	bool AppShortcut(const MSG& msg);
	uint debugswallow;			// デバッガのキー (F5/F10/F11) に使ったキー (離したのも PC-8801 に渡さない)
	bool DebugKey(const MSG& msg);
	void ShowPauseStatus();
	PC8801::Debugger::CpuTarget GetStepTarget();
	void SetShortcutLabels(HMENU hmenu);
	HMENU hmenudbg;

	// 状態表示用
	UINT_PTR timerid;
	bool report;
	volatile bool active;

	// ウインドウの状態
	bool background;
	bool fullscreen;
	HMENU hmenumain;			// 本体のメニュー (全画面で隠している間は GetMenu(hwnd) が 0 を返すので、こちらを使う)
	uint displaychangedtime;
	uint resetwindowsize;
	DWORD wstyle;
	POINT point;
	int clipmode;
	bool guimodebymouse;
	bool dpimoved;			// 窓を動かしている間に DPI が変わった (動かし終えたら大きさを直す)
	int appliedrot;			// 窓の大きさに使った画面の回転 (変わったら窓の大きさを直す)
	bool cursorhidden;		// 全画面でマウスが止まっていたので、カーソルを消している
	POINT cursorpos;		// カーソルが動いたかを見るための前の位置
	enum { cursortimer = 10, cursorhidems = 3000 };
	void RestartCursorTimer();
	bool CursorOnScreen();
	
	// disk
	DiskInfo diskinfo[2];
	char tapetitle[MAX_PATH];

	// 画面の上に数秒出す知らせ (テープのセーブが記録されないときなど)
	HWND hoverlay;
	HFONT overlayfont;
	UINT overlaydpi;			// overlayfont を作ったときの DPI
	HBRUSH overlaybrush;
	
	// snapshot 関係
	HMENU hmenuss[2];
	int currentsnapshot;
	bool snapshotchanged;

	// PC88
	bool capturemouse;
	uint mousebutton;
	
	WinCore core;
	WinDraw draw;
	PC8801::WinKeyIF keyif;
	SoftKeyboard softkbd;
	TapeWindow tapewin;				// テープの窓 (M88gn)
	PC8801::Config config;
	PC8801::WinConfig winconfig;
	WinNewDisk newdisk;
	OPNMonitor opnmon;
	PC8801::MemoryMonitor memmon;
	PC8801::CodeMonitor codemon;
	PC8801::BasicMonitor basmon;
	PC8801::IOMonitor iomon;
	PC8801::BreakMonitor bpmon;
	PC8801::WatchMonitor watchmon;
	PC8801::TraceMonitor tracemon;
	bool debugnodiskwarned;		// ディスクが無いので保存できないことを一度知らせた
	void SyncDebugSettings(const char* oldkey, const char* newkey);
	char debugkey[MAX_PATH];	// デバッガの設定を保存・読み込みしている今のイメージ (ドライブ 1 のディスクかテープ。空: 無し)
	char tapefile[MAX_PATH];	// 開いているテープのファイル (空: 無し)
	void SetDebugKey(const char* newkey);
	Z80RegMonitor regmon;
	LoadMonitor loadmon;
	DiskManager* diskmgr;
	TapeManager* tapemgr;

	bool exec;

private:
	// メッセージ関数
	LRESULT M88ChangeDisplay(HWND, WPARAM, LPARAM);
	LRESULT M88ChangeVolume(HWND, WPARAM, LPARAM);
	LRESULT M88ApplyConfig(HWND, WPARAM, LPARAM);
	LRESULT M88SendKeyState(HWND, WPARAM, LPARAM);
	LRESULT M88ClipCursor(HWND, WPARAM, LPARAM);
	LRESULT M88TapeNotify(HWND, WPARAM, LPARAM);
	LRESULT M88DebugBreak(HWND, WPARAM, LPARAM);
	LRESULT WmDropFiles(HWND, WPARAM, LPARAM);
	LRESULT WmDisplayChange(HWND, WPARAM, LPARAM);
	LRESULT WmKeyDown(HWND, WPARAM, LPARAM);
	LRESULT WmKeyUp(HWND, WPARAM, LPARAM);
	LRESULT WmSysKeyDown(HWND, WPARAM, LPARAM);
	LRESULT WmSysKeyUp(HWND, WPARAM, LPARAM);
	LRESULT WmInitMenu(HWND, WPARAM, LPARAM);
	LRESULT WmQueryNewPalette(HWND, WPARAM, LPARAM);
	LRESULT WmPaletteChanged(HWND, WPARAM, LPARAM);
	LRESULT WmActivate(HWND, WPARAM, LPARAM);
	LRESULT WmTimer(HWND, WPARAM, LPARAM);
	LRESULT WmClose(HWND, WPARAM, LPARAM);
	LRESULT WmCreate(HWND, WPARAM, LPARAM);
	LRESULT WmDestroy(HWND, WPARAM, LPARAM);
	LRESULT WmPaint(HWND, WPARAM, LPARAM);
	LRESULT WmCommand(HWND, WPARAM, LPARAM);
	LRESULT WmSize(HWND, WPARAM, LPARAM);
	LRESULT WmDpiChanged(HWND, WPARAM, LPARAM);
	LRESULT WmGetDpiScaledSize(HWND, WPARAM, LPARAM);
	LRESULT WmDrawItem(HWND, WPARAM, LPARAM);
	LRESULT WmMeasureItem(HWND, WPARAM, LPARAM);
	LRESULT M88StatusMenu(HWND, WPARAM, LPARAM);
	LRESULT WmEnterMenuLoop(HWND, WPARAM, LPARAM);
	LRESULT WmExitMenuLoop(HWND, WPARAM, LPARAM);
	LRESULT WmLButtonDown(HWND, WPARAM, LPARAM);
	LRESULT WmLButtonUp(HWND, WPARAM, LPARAM);
	LRESULT WmRButtonDown(HWND, WPARAM, LPARAM);
	LRESULT WmRButtonUp(HWND, WPARAM, LPARAM);
	LRESULT WmContextMenu(HWND, WPARAM, LPARAM);
	LRESULT WmEnterSizeMove(HWND, WPARAM, LPARAM);
	LRESULT WmExitSizeMove(HWND, WPARAM, LPARAM);
	LRESULT WmMove(HWND, WPARAM, LPARAM);
	LRESULT WmMouseMove(HWND, WPARAM, LPARAM);
	LRESULT WmSetCursor(HWND, WPARAM, LPARAM);
};

