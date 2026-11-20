// ---------------------------------------------------------------------------
//	M88 - PC88 emulator
//	Copyright (c) cisc 1998, 1999.
// ---------------------------------------------------------------------------
//	PC88 Keyboard Interface Emulation for Win32/106 key (Rev. 3)
// ---------------------------------------------------------------------------
//	$Id: WinKeyIF.h,v 1.3 1999/10/10 01:47:20 cisc Exp $

#pragma once

#include "device.h"
#include "CritSect.h"

// ---------------------------------------------------------------------------
namespace PC8801
{

class Config;

class WinKeyIF : public Device
{
public:
	enum
	{
		reset = 0, vsync,
		in = 0,
	};

public:
	WinKeyIF();
	~WinKeyIF();
	bool Init(HWND);
	void ApplyConfig(const Config* config);

	uint IOCALL In(uint port);
	void IOCALL VSync(uint, uint data);
	void IOCALL Reset(uint=0, uint=0);
	
	void Activate(bool);
	void Disable(bool);
	void KeyDown(uint, uint32);
	void KeyUp(uint, uint32);

	// ソフトウェアキーボード
	void SetSoftKeys(const uint8* keys);	// 押しているキー (ポート 0～15 のビット。1 = 押している)
	void GetMatrix(uint8* keys);			// PC-8801 に見えているキー (ポート 0～15。1 = 押している)

	const Descriptor* IFCALL GetDesc() const { return &descriptor; }

private:
	enum KeyState
	{
		locked = 1,
		down   = 2,
		downex = 4,
	};
	enum KeyFlags
	{
		none = 0, lock, nex, ext, arrowten, keyb, noarrowten, noarrowtenex, 
		pc80sft, pc80key,
	};
	struct Key
	{
		uint8 k, f;
	};

	uint GetKey(const Key* key);
	uint ReadPort(uint port);				// パソコンのキーボードから求めたポートの値 (0 = 押している)
	void UpdateBurstCut();					// Burst mode・全力駆動: 長く押したキーを離したことにする (M88gn)

	static const Key KeyTable98[16 * 8][8];
	static const Key KeyTable106[16 * 8][8];
	static const Key KeyTable101[16 * 8][8];

	const Key* keytable;
	int keyboardtype;
	bool active;
	bool disable;
	bool usearrow;
	bool pc80mode;
	HWND hwnd;
	HANDLE hevent;
	uint basicmode;
	int keyport[16];
	uint8 keyboard[256];
	uint8 keystate[512];
	LONG shiftuptime;					// 最後に Shift が離されたときのメッセージの時刻
	bool shiftupvalid;
	volatile uint8 softkey[16];			// ソフトウェアキーボードで押しているキー (1 = 押している)

	// Burst mode・全力駆動 (M88gn)。エミュレーターの中の時間が実時間より速く進むので、
	// キーを普通に押しただけでも PC-8801 からは長く押したように見え、キーリピートで同じ文字が大量に入る。
	// 押してから burstholdframes フレーム (エミュレーターの中の時間) たったキーは、指を離すまで離したことにする
	enum { burstholdframes = 6 };
	volatile bool burst;
	uint8 heldframes[16][8];			// 押し続けているフレームの数
	volatile uint8 burstcut[16];		// 離したことにしているキー (1 = 離したことにする)

private:
	static const Descriptor descriptor;
	static const InFuncPtr  indef[];
	static const OutFuncPtr outdef[];
};

}

