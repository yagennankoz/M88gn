// ---------------------------------------------------------------------------
//	M88 - PC-8801 emulator
//	デバッグ設定(ブレークポイント/ウォッチ)のディスクイメージ単位の保存
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#pragma once

#include "pc88/debugger.h"

// ---------------------------------------------------------------------------

namespace PC8801
{

// 保存先は <diskimage> + ".m88dbg" (INI形式)。
// diskimage が NULL または空文字列なら何もせず false を返す。
// 呼び出し元で core->Lock()/Unlock() すること(Debugger の
// GetBreakPoint/AddBreakPoint 等は Lock() の中で呼ぶ規約のため)

// dbg の内容をファイルへ書き出す。書き込みに失敗したら false
// (読み取り専用メディア等。呼び出し元はステータス表示に出すだけに留め、
// 落とさないこと)
bool SaveDebugSettings(Debugger* dbg, const char* diskimage);

// ファイルから dbg へ読み込む。dbg の既存のブレークポイント/ウォッチは
// 読み込み前に全て削除される。ファイルが無い、または Version が
// 想定と異なる(将来の新しい形式)ときは何もせず false を返す
// renamed に、名前を直したラベルの数 (重複・使えない字。M88gn)
bool LoadDebugSettings(Debugger* dbg, const char* diskimage, int* renamed = 0);

}
