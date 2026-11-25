// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ラベル (番地に付ける名前) を入力する窓。コードの窓・メモリの窓の右クリックから
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#pragma once

#include "pc88/debugger.h"

class WinCore;

namespace PC8801
{

// cpu・addr のラベルを入力させて Debugger に入れる。空にすると消す。OK なら true。
// bank: そのバンクにラベルがあればそれを直す。無ければ新しく付けるときに最初に選んでおくバンク
// (MemoryViewer::Type か Debugger::label_anybank。サブ CPU では使わない)
bool EditLabel(HWND owner, HINSTANCE hinst, Debugger* dbg, int cpu, uint addr, int bank);
// 番地と名前を入れて新しく足す (ラベルの一覧の Add)。足したら true と、そのラベルの CPU・番地・バンク
bool AddLabel(HWND owner, HINSTANCE hinst, Debugger* dbg, int* cpu, uint* addr, int* bank);

// 名前を決まりに合わせる (使えない字は _ に、レジスターの名前は後ろに _、ほかのラベルと重なれば 名前_2…)。
// 直した理由 (1 = 使えない字、2 = レジスターの名前、4 = 名前の重複) を返す。直さなければ 0。
// .m88dbg の読み込みで使う (読み込み Import Labels も同じ決まり)
int FixLabelForSet(Debugger* dbg, int cpu, uint addr, int bank, char* name, int size);

// --- ラベルを窓のあちこちで使うための関数 (M88gn) ---
// 本体 (ui が起動時に渡す)
void SetLabelCore(WinCore* core);
// 番地の入力欄の読み方: ラベルの名前か、16 進 (1234・1234H・$1234・0x1234)。読めれば true。
// ラベルの名前と同じなら先にラベル (16 進に見える名前のため)。cpu にラベルの CPU (16 進なら -1)
bool ParseAddrInput(const char* text, uint* addr, int* cpu = 0);
// その番地の今のラベル (CPU がその番地で今見ているバンクのもの、無ければ Any)。無ければ 0
const char* LabelNow(int cpu, uint addr);
// 表示用: ラベルがあれば「名前(1234)」、無ければ「1234」。h なら番地の後に H
void AddrLabelText(char* out, int cpu, uint addr, bool h = false);

// バンクの名前 (Any は "Any")。書き出し・一覧で使う
const char* LabelBankName(int bank);

// ラベルをテキストファイルに書き出す・読み込む (Debug メニューから)。数を返す (やめた・失敗は -1)
int ExportLabels(HWND owner, Debugger* dbg);
int ImportLabels(HWND owner, Debugger* dbg, int* skipped);

}
