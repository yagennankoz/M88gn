// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	テーマ: 本体の窓の枠まわり (タイトルの段・ペインのタブと帯・仕切り・ステータスバー) の色
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#pragma once

namespace Theme
{
	enum { system = 0, dark, beige, count };		// Windows に合わせる・ダーク・ベージュ (PC-8801 本体の色)

	struct Colors
	{
		COLORREF titlebg, titlefg;			// タイトルの段 (アクティブ)
		COLORREF titleibg, titleifg;		// タイトルの段 (アクティブでない)
		COLORREF panebg;					// ペインの地 (タブの並びの地)
		COLORREF tabfg;						// 選んでいないタブの字
		COLORREF tabselbg, tabselfg;		// 選んでいるタブ (メニューの帯も同じ地)
		COLORREF tabaccent;					// 選んでいるタブの上の線
		COLORREF tabsep;					// タブの区切りの線
		COLORREF stripfg, stripgray;		// メニューの帯の字・押せない項目の字
		COLORREF splitter;					// 仕切り
		COLORREF emptybg;					// 窓の無い区画
		COLORREF statusbg, statusfg;		// ステータスバー
		COLORREF statussep;					// ステータスバーの欄の区切り
	};

	int Get();
	void Set(int t);					// M88.ini にも書く
	void Load();						// 起動時に M88.ini から
	const Colors& C();					// 今の色 (Windows に合わせるときは、呼ぶたびに Windows の色から作る)
	COLORREF Mix(COLORREF a, COLORREF b, int t);	// a に b を t/256 混ぜる

	// 窓のスクロールバーをテーマに合わせる (Dark なら暗いスクロールバー、ほかは Windows の既定)。
	// uxtheme の SetWindowTheme に "DarkMode_Explorer" を渡す (Windows 10 1809 から。無ければ既定のまま)
	void ApplyScrollBars(HWND hwnd);

	// ペインのメニューの帯を描いている間 (自分で描く項目 (ボタン・番地の欄) が帯の色で描くため)
	extern bool stripdrawing;
}
