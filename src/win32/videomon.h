// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	Video の窓 (GVRAM・テキストの VRAM・パレットを見る。デバッガ用)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#pragma once

#include "types.h"
#include "pc88/screen.h"
#include "dbgbuttons.h"
#include "dockclient.h"

class WinCore;

namespace PC8801
{

class MemoryMonitor;

class VideoMonitor : public DockClient
{
public:
	VideoMonitor();
	~VideoMonitor();

	bool Init(WinCore* core, MemoryMonitor* mem);
	void Show(HINSTANCE hinst, HWND owner, bool show);
	bool IsOpen() { return hwnd != 0; }
	void Refresh();					// 止まっているときに呼ぶ。中身を写し取って描き直す
	void Close();					// 終わるとき (位置を残す)

	// DockClient (ペインに入れる。M88gn)
	HWND DockWindow() { return hwnd; }
	const char* DockSection() { return "MonitorVideo"; }
	void DockFloatRect(RECT* r) { SetRect(r, posx, posy, posx + width, posy + height); }
	bool DockIsOpen() { return hwnd != 0; }
	void DockOpen(HINSTANCE hi, HWND own) { Show(hi, own, true); }

private:
	enum { tab_graphic = 0, tab_text, tab_palette, ntabs };
	enum { maxtext = 0x1000 };
	enum { buttontimer = 1 };
	enum { attrlabelchars = 4, attrcellchars = 7 };	// 属性の格子: 左の列の桁数、1 組の欄の桁数

	static LRESULT CALLBACK WndProcGate(HWND, UINT, WPARAM, LPARAM);
	LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

	void Create();
	void Capture();
	void MakeFont();
	void Layout();
	int Scale(int v) { return MulDiv(v, dpi, 96); }

	void Paint(HDC hdc, const RECT& rc);
	void PaintGraphic(HDC hdc, const RECT& rc);
	void PaintText(HDC hdc, const RECT& rc);
	void PaintPalette(HDC hdc, const RECT& rc);
	void PaintStatus(HDC hdc, const RECT& rc, const char* fields, const char* hint);
	// 下の情報 (ステータス) の高さ。今の字と、出しうる一番長い字・案内の字を折り返して一番高いもの
	// (マウスを動かしても絵や格子の範囲が変わらないように)
	int StatusHeight(HDC hdc, const RECT& rc, const char* longest, const char* hint);
	int AttrLines(HDC hdc, int width);		// 属性の並びが最も長いときの行数
	int statush;							// 今のタブのステータスの高さ

	bool GraphicPixel(int px, int py, int* gx, int* gy);	// 窓の座標 → GVRAM の座標
	void TextAttrAt(int row, int col, uint* colcode, uint* deco);	// その文字に効いている属性 (色と装飾)
	void DescribeAttr(uint colcode, uint deco, char* buf);
	// 全部の桁の属性 ([行 * 128 + 桁])。colids・decoids を渡せば、その属性を設定した組
	// (行 * 64 + 組の番号 + 1、0 はフレームの初めのまま) も
	void CellAttrs(uint8* colcodes, uint8* decos, uint16* colids = 0, uint16* decoids = 0);
	bool IsSemi(uint colcode, uint deco) { return (textmode & 2) ? (colcode & 0x10) != 0 : (deco & 0x80) != 0; }
	void OnMouseMove(int x, int y);
	void OnClick(int x, int y);
	void SetZoom(int z, int px, int py);					// 拡大を変える。(px, py) の所の絵を動かさない
	void Scroll(int dx, int dy);

	void LoadPlacement();
	void SavePlacement();

	WinCore* core;
	MemoryMonitor* memmon;
	HINSTANCE hinst;
	HWND hwnd, owner;
	// タブ (自分で描く。M88gn: タブの部品は字が詰まって崩れて見えたため)
	HFONT tabfont;
	RECT tabrc[ntabs];
	int tabh;						// タブの段の高さ
	void PaintTabs(HDC hdc, const RECT& rc);
	int TabHit(int x, int y);
	HFONT font;
	int dpi;
	int tab;

	// 写し取ったもの
	bool captured;
	uint8 gvram[3][0x4000];
	Screen::DebugInfo sinfo;
	uint8 text[maxtext];
	uint textbase, textw, texth, textattrs, textbytes, textmode, textlines;
	uint32 capturetick;

	// Graphic のボタン
	bool planeon[3];
	bool usepalette;
	RECT btn[4];					// B・R・G・Palette
	RECT imgrect;					// 絵を描いた所 (拡大していれば表示の範囲より大きい)
	RECT imgarea;					// 絵を出す範囲
	RECT zbtn[5];					// Fit・x1・x2・x4・x8
	int zoom;						// 0 = 窓に合わせる、1・2・4・8 = 640x400 (96 DPI 換算) の何倍か
	int scrollx, scrolly;			// 拡大しているときの表示の左上 (絵の中のピクセル)
	bool dragging;					// 右ドラッグで動かしている
	POINT dragpt;
	int imgw, imgh;

	// Text の格子
	RECT textrect;
	RECT semibtn;					// Semigraphics の切り替え
	RECT tzbtn[5];					// 文字の大きさ: Fit・14・18・24・32
	int textzoom;					// 0 = 窓に合わせる、ほかは文字の高さ (96 DPI 換算のピクセル)
	HFONT tfont;					// 格子の字
	int tfontpx;					// tfont の高さ (ピクセル。0: まだ)
	int tcw, tch;					// 格子の 1 文字の幅・高さ
	RECT textview;					// 格子を出す範囲 (はみ出す分はスクロール)
	int tscrollx, tscrolly;			// 格子のスクロール (ピクセル)
	void TextFont(HDC hdc, int px);	// 格子の字を px の高さで (変わったときだけ作り直す)
	void SetTextZoom(int z);
	bool semimode;					// セミグラフィックの属性の桁を点で描く
	int cellw, cellh;

	// マウスの下
	char hover[160];
	int hoverrow;					// Text: マウスの下の行 (-1 なら無し)
	int hovercol;

	int posx, posy, width, height;
	bool placed;

	DebugButtons buttons;			// メニューバーの Pause/Play/Step のボタン
};

}
