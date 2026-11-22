// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1998, 2000.
// ---------------------------------------------------------------------------
//	$Id: mvmon.h,v 1.3 2003/05/19 02:33:56 cisc Exp $

#pragma once

#include "device.h"
#include "winmon.h"
#include "pc88/memview.h"

// ---------------------------------------------------------------------------

class PC88;

namespace PC8801
{

class MemViewMonitor : public WinMonitor
{
public:
	MemViewMonitor();
	~MemViewMonitor();

	bool Init(LPCTSTR tmpl, PC88*, const char* inisection = 0,
			  int defcols = 0, int defrows = 0);

	// 番地を見せる (M88gn。コードの窓・メモリの窓の右クリックから)。窓が閉じていれば開く
	void ShowAddress(uint addr, HINSTANCE hinst, HWND parent)
	{
		if (!IsOpen())
			Show(hinst, parent, true);
		JumpTo(addr & 0xffff);
		Update();
	}
	// c000 のバンクを選んで番地を見せる (M88gn。Video の窓から)。0000 が Sub ならメインの RAM に
	void ShowAddressBank(uint addr, MemoryViewer::Type cbank, HINSTANCE hinst, HWND parent)
	{
		if (!IsOpen())
			Show(hinst, parent, true);
		if (a0 == MemoryViewer::sub)
			a0 = MemoryViewer::mainram;
		ac = cbank;
		SetBank();
		JumpTo(addr & 0xffff);
		Update();
	}

protected:
	MemoryBus* GetBus() { return bus; }
	BOOL DlgProc(HWND, UINT, WPARAM, LPARAM);

	void StatClear();
	uint StatExec(uint a);
	virtual void SetBank();

	// 任意アドレスへ飛ぶ(ダイアログを出して JumpTo を呼ぶ)。Ctrl+G と
	// メニューの Go to Address... の両方から呼ばれる
	void GotoAddress();
	// SetLine() に渡す行番号への換算。既定は CodeMonitor 相当(1行=1アドレス)。
	// MemoryMonitor は 1行=16バイトなのでオーバーライドする
	virtual void JumpTo(uint addr) { SetLine(addr); }

	MemoryViewer mv;

	MemoryViewer::Type GetA0() { return a0; }
	void SetA0(MemoryViewer::Type t) { a0 = t; }
	MemoryViewer::Type GetA6() { return a6; }
	// 6000-7FFF に実際に出しているバンク (M88gn。a6 は 0000 が N88 / N80SR のときだけ効く)
	MemoryViewer::Type GetShown6();
	MemoryViewer::Type GetAc() { return ac; }
	MemoryViewer::Type GetAf() { return af; }

	// Auto: 0000～7FFF と F000～FFFF のバンクを、メイン CPU がいま見ているものに合わせる (M88gn)。
	// メニューでバンクを選ぶと外れる。書き直す前 (UpdateText の初め) に呼ぶ。変わったら true
	bool AutoBank();
	bool IsAutoBank() { return autobank; }

private:
	BOOL GotoDlgProc(HWND, UINT, WPARAM, LPARAM);
	static BOOL CALLBACK GotoDlgProcGate(HWND, UINT, WPARAM, LPARAM);

	MemoryBus* bus;
	MemoryViewer::Type a0;
	MemoryViewer::Type a6;
	MemoryViewer::Type ac;
	MemoryViewer::Type af;
	bool autobank;
	PC8801::Memory* mem1;
};

}

