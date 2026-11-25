// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1998, 1999.
// ---------------------------------------------------------------------------
//	$Id: regmon.h,v 1.1 2000/11/02 12:43:51 cisc Exp $

#pragma once

#include "device.h"
#include "winmon.h"
#include "pc88/pc88.h"


class PC88;
class WinCore;
namespace PC8801 { class CodeMonitor; class MemoryMonitor; }

// ---------------------------------------------------------------------------

class Z80RegMonitor : public WinMonitor
{
public:
	Z80RegMonitor();
	~Z80RegMonitor();

	bool Init(WinCore* core);
	// 右クリックで番地を見せる先 (ui が渡す、M88gn)
	void SetMonitors(PC8801::CodeMonitor* c, PC8801::MemoryMonitor* m) { codemon = c; memmon = m; }

private:
	// レジスタ表示上の1フィールド (行/列とレジスタの対応)
	struct RegField
	{
		int row;
		int col;		// メインCPU側の表示開始列 (サブCPU側は +subcpuoffset)
		int width;		// クリック判定に使う幅 (ラベル+値)
		int valcol;		// メインCPU側の値の開始列
		int vallen;		// 値の桁数 (2 か 4)
		const char* name;
	};
	enum RegId
	{
		reg_pc, reg_sp, reg_af, reg_bc, reg_hl, reg_de, reg_ix, reg_iy,
		reg_af2, reg_bc2, reg_hl2, reg_de2, reg_i, reg_im,
		reg_max
	};

	void UpdateText();
	BOOL DlgProc(HWND, UINT, WPARAM, LPARAM);
	void DrawMain(HDC, bool);

	uint GetRegValue(int cpu, int id);
	void SetRegValue(int cpu, int id, uint value);
	bool FindField(POINT p, int* cpu, int* id);
	void ShowFieldMenu(HWND hdlg, POINT p);	// 右クリックのメニュー (M88gn)

	BOOL EDlgProc(HWND, UINT, WPARAM, LPARAM);
	static BOOL CALLBACK EDlgProcGate(HWND, UINT, WPARAM, LPARAM);

	WinCore* core;
	PC8801::CodeMonitor* codemon;
	PC8801::MemoryMonitor* memmon;
	int editcpu;
	int editid;

	static const RegField fields[reg_max];
	static const int subcpuoffset = 18;
};

