// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	パターンのブレークポイントの追加・編集の窓
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	CPU の書き込みで、指定したバンク・範囲の中に文字列かバイトの並びが揃ったら止める
// ---------------------------------------------------------------------------

#pragma once

#include "wincore.h"
#include "pc88/debugger.h"

namespace PC8801
{

// index が -1 なら追加 (init があればその値で始める)。追加・変更したらブレークポイントの番号、やめたら -1
int EditPatternBreak(HWND owner, HINSTANCE hinst, WinCore* core, int index, const Debugger::BreakPoint* init);

// 一覧などに出す説明 (pattern "文字列" in RAM のような形。buf は 256 バイト)
void DescribePatternBreak(const Debugger::BreakPoint& bp, char* buf);

// パターンのブレークで見られるバンクか (メインの CPU)
bool IsPatternBank(int bank);

}
