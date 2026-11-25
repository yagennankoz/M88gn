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
		SetJumpMark(addr);
		JumpTo(addr & 0xffff);
		Update();
	}
	// CPU を選んで番地を見せる (M88gn。レジスターの窓から)。サブならサブの RAM、
	// メインで今サブを見ていればメインに戻す (Auto なら CPU がいま見ているバンク)
	void ShowAddressCpu(uint addr, bool sub, HINSTANCE hinst, HWND parent)
	{
		if (!IsOpen())
			Show(hinst, parent, true);
		if (sub)
			a0 = MemoryViewer::sub;
		else if (a0 == MemoryViewer::sub)
		{
			a0 = MemoryViewer::mainram;
			AutoBank();
		}
		SetBank();
		SetJumpMark(addr);
		JumpTo(addr & 0xffff);
		Update();
	}
	// c000 のバンクを選んで番地を見せる (M88gn。Video の窓から)。0000 が Sub ならメインの RAM に
	// 番地を、そのバンク (MemoryViewer::Type) で見せる (M88gn。ブレークポイント・ウォッチ・ラベルの窓から)。
	// bank が負か Debugger::label_anybank (0xff) なら今のバンクのまま。len バイトに印を付ける
	void ShowAddressIn(uint addr, bool sub, int bank, uint len, HINSTANCE hinst, HWND parent);
	// ジャンプした先 (M88gn)。次のジャンプまで色を変える
	void SetJumpMark(uint addr, uint len = 1) { jumpaddr = int(addr & 0xffff); jumplen = int(len ? len : 1); }
	bool InJump(uint addr) { return jumpaddr >= 0 && uint(int(addr & 0xffff) - jumpaddr) < uint(jumplen); }
	void ShowAddressBank(uint addr, MemoryViewer::Type cbank, HINSTANCE hinst, HWND parent)
	{
		if (!IsOpen())
			Show(hinst, parent, true);
		if (a0 == MemoryViewer::sub)
			a0 = MemoryViewer::mainram;
		ac = cbank;
		SetBank();
		SetJumpMark(addr);
		JumpTo(addr & 0xffff);
		Update();
	}

protected:
	int jumpaddr, jumplen;		// ジャンプした先 (M88gn、jumpaddr が負なら無し)
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
public:
	// 番地に今出しているバンク (MemoryViewer::Type。M88gn、ラベル・ウォッチの色の判定に使う)。サブ CPU を見ていれば -1
	int ShownBankAt(uint addr);
protected:
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

