// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	I/O ポートの一覧 (I/O ブレークポイントの窓で、選ぶメニューと名前の列に使う)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	M88 が装置をつないでいるポートを元にした、主な用途の説明。機種やモードで意味の変わるものもある
//	name は一覧の窓に出す短い英語の名前 (モニターのフォントに日本語が無くても読めるように)、
//	desc は選ぶメニューに出す日本語の説明
// ---------------------------------------------------------------------------

#pragma once

namespace PC8801
{

struct IOPortInfo
{
	uint8 port;
	uint8 sub;				// 1 ならサブ CPU (ディスク側) のポート
	const char* group;		// 選ぶメニューの分類
	const char* name;
	const char* desc;
};

static const IOPortInfo ioports[] =
{
	// キーボード
	{ 0x00, 0, "キーボード", "Keyboard row 0", "キーボード 行 0 (テンキー 0～7) (IN)" },
	{ 0x01, 0, "キーボード", "Keyboard row 1", "キーボード 行 1 (テンキー 8・9・*・+・=・,・.・RETURN) (IN)" },
	{ 0x02, 0, "キーボード", "Keyboard row 2", "キーボード 行 2 (@・A～G) (IN)" },
	{ 0x03, 0, "キーボード", "Keyboard row 3", "キーボード 行 3 (H～O) (IN)" },
	{ 0x04, 0, "キーボード", "Keyboard row 4", "キーボード 行 4 (P～W) (IN)" },
	{ 0x05, 0, "キーボード", "Keyboard row 5", "キーボード 行 5 (X～Z・[・\\・]・^・-) (IN)" },
	{ 0x06, 0, "キーボード", "Keyboard row 6", "キーボード 行 6 (0～7) (IN)" },
	{ 0x07, 0, "キーボード", "Keyboard row 7", "キーボード 行 7 (8・9・:・;・,・.・/・_) (IN)" },
	{ 0x08, 0, "キーボード", "Keyboard row 8", "キーボード 行 8 (CLR・↑・→・INS/DEL・GRPH・カナ・SHIFT・CTRL) (IN)" },
	{ 0x09, 0, "キーボード", "Keyboard row 9", "キーボード 行 9 (STOP・f1～f5・SPACE・ESC) (IN)" },
	{ 0x0a, 0, "キーボード", "Keyboard row A", "キーボード 行 A (TAB・↓・←・HELP・COPY・-・/・CAPS) (IN)" },
	{ 0x0b, 0, "キーボード", "Keyboard row B", "キーボード 行 B (ROLL UP・ROLL DOWN) (IN)" },
	{ 0x0c, 0, "キーボード", "Keyboard row C", "キーボード 行 C (f6～f10・BS・INS・DEL) (IN)" },
	{ 0x0d, 0, "キーボード", "Keyboard row D", "キーボード 行 D (変換・決定・PC・全角) (IN)" },
	{ 0x0e, 0, "キーボード", "Keyboard row E", "キーボード 行 E (RETURN・左右の SHIFT) (IN)" },

	// システム制御
	{ 0x30, 0, "システム制御", "DIPSW1/System ctrl 1", "IN: ディップスイッチ 1 / OUT: システム制御 (CMT のモーター・CMT と RS-232C・40/80 桁・カラー/白黒)" },
	{ 0x31, 0, "システム制御", "DIPSW2/System ctrl 2", "IN: ディップスイッチ 2 / OUT: システム制御 (ROM/RAM・N/N88・200/400 ライン・グラフィックの表示)" },
	{ 0x32, 0, "システム制御", "System ctrl 3 (V2)", "システム制御 (N88 V2: 拡張 ROM・ALU・パレットのモード・音源の割り込みのマスク)" },
	{ 0x33, 0, "システム制御", "System ctrl (N80V2)", "システム制御 (N80 モード: bit7 = N80 V2 (N80SR) の ROM、bit6 = ALU と GVRAM (8000～BFFF))" },
	{ 0x40, 0, "システム制御", "System status/strobe", "IN: システムの状態 (VRTC・CMT のキャリア・カレンダー・プリンター) / OUT: ストローブ (プリンター・カレンダー・BEEP・SING)" },
	{ 0x6e, 0, "システム制御", "CPU clock info", "IN: CPU のクロックなどの情報" },
	{ 0x10, 0, "システム制御", "Printer/Calendar data", "OUT: プリンターの出力・カレンダー時計のデータ" },

	// 画面
	{ 0x50, 0, "画面 (CRTC・DMA)", "CRTC parameter", "CRTC (μPD3301) のパラメーター / ステータス" },
	{ 0x51, 0, "画面 (CRTC・DMA)", "CRTC command", "CRTC のコマンド" },
	{ 0x60, 0, "画面 (CRTC・DMA)", "DMAC ch0 address", "DMA コントローラー (μPD8257) ch0 のアドレス" },
	{ 0x61, 0, "画面 (CRTC・DMA)", "DMAC ch0 count", "DMA コントローラー ch0 のカウント" },
	{ 0x62, 0, "画面 (CRTC・DMA)", "DMAC ch1 address", "DMA コントローラー ch1 のアドレス" },
	{ 0x63, 0, "画面 (CRTC・DMA)", "DMAC ch1 count", "DMA コントローラー ch1 のカウント" },
	{ 0x64, 0, "画面 (CRTC・DMA)", "DMAC ch2 address", "DMA コントローラー ch2 のアドレス (テキストの転送)" },
	{ 0x65, 0, "画面 (CRTC・DMA)", "DMAC ch2 count", "DMA コントローラー ch2 のカウント" },
	{ 0x66, 0, "画面 (CRTC・DMA)", "DMAC ch3 address", "DMA コントローラー ch3 のアドレス" },
	{ 0x67, 0, "画面 (CRTC・DMA)", "DMAC ch3 count", "DMA コントローラー ch3 のカウント" },
	{ 0x68, 0, "画面 (CRTC・DMA)", "DMAC mode/status", "DMA コントローラーのモード / ステータス" },

	// 色
	{ 0x52, 0, "パレット・表示", "Background color", "背景色" },
	{ 0x53, 0, "パレット・表示", "Layer control", "画面の表示の選択 (テキスト・グラフィックの各面を消す)" },
	{ 0x54, 0, "パレット・表示", "Palette 0", "パレット 0" },
	{ 0x55, 0, "パレット・表示", "Palette 1", "パレット 1" },
	{ 0x56, 0, "パレット・表示", "Palette 2", "パレット 2" },
	{ 0x57, 0, "パレット・表示", "Palette 3", "パレット 3" },
	{ 0x58, 0, "パレット・表示", "Palette 4", "パレット 4" },
	{ 0x59, 0, "パレット・表示", "Palette 5", "パレット 5" },
	{ 0x5a, 0, "パレット・表示", "Palette 6", "パレット 6" },
	{ 0x5b, 0, "パレット・表示", "Palette 7", "パレット 7" },

	// メモリ
	{ 0x5c, 0, "メモリ・バンク", "GVRAM plane B", "OUT: GVRAM の青の面を選ぶ / IN: 選んでいる面 (GVRAM は C000～FFFF、N80 モードでは 8000～BFFF)" },
	{ 0x5d, 0, "メモリ・バンク", "GVRAM plane R", "OUT: GVRAM の赤の面を選ぶ" },
	{ 0x5e, 0, "メモリ・バンク", "GVRAM plane G", "OUT: GVRAM の緑の面を選ぶ" },
	{ 0x5f, 0, "メモリ・バンク", "Main RAM", "OUT: C000～FFFF をメイン RAM に戻す" },
	{ 0x34, 0, "メモリ・バンク", "ALU control 1", "ALU (GVRAM の論理演算) の制御 1" },
	{ 0x35, 0, "メモリ・バンク", "ALU control 2", "ALU の制御 2 (GVRAM のアクセスのモード)" },
	{ 0x70, 0, "メモリ・バンク", "Text window offset", "テキストウインドウ (8000～83FF) の位置 (N88 の ROM のとき。N/N80 モードでは使わない)" },
	{ 0x78, 0, "メモリ・バンク", "Text window +1", "テキストウインドウの位置を 1 進める (N88 の ROM のとき)" },
	{ 0x71, 0, "メモリ・バンク", "Ext ROM select", "拡張 ROM の選択 (N80 モード: bit0 = N80SR の ROM の 6000～7FFF の切り替え)" },
	{ 0xe2, 0, "メモリ・バンク", "ERAM control", "拡張 RAM の読み書きの許可" },
	{ 0xe3, 0, "メモリ・バンク", "ERAM bank", "拡張 RAM のバンクの選択" },
	{ 0x99, 0, "メモリ・バンク", "CD-BIOS select", "CD-ROM BIOS の ROM の切り替え (CD-ROM の付いた機種)" },

	// 割り込み
	{ 0xe4, 0, "割り込み", "Interrupt level", "割り込みのレベル" },
	{ 0xe6, 0, "割り込み", "Interrupt mask", "割り込みのマスク (USART・VRTC・クロック)" },

	// 音源
	{ 0x44, 0, "音源", "OPN address/status", "FM 音源 (OPN/OPNA) のアドレス / ステータス" },
	{ 0x45, 0, "音源", "OPN data", "FM 音源 (OPN/OPNA) のデータ" },
	{ 0x46, 0, "音源", "OPNA ext address", "OPNA の拡張側 (FM 4～6・リズム・ADPCM) のアドレス / ステータス" },
	{ 0x47, 0, "音源", "OPNA ext data", "OPNA の拡張側のデータ" },
	{ 0xa8, 0, "音源", "SB2 OPN address", "サウンドボード II の FM 音源のアドレス / ステータス" },
	{ 0xa9, 0, "音源", "SB2 OPN data", "サウンドボード II の FM 音源のデータ" },
	{ 0xaa, 0, "音源", "SB2 interrupt mask", "サウンドボード II の割り込みのマスク" },
	{ 0xac, 0, "音源", "SB2 OPNA ext address", "サウンドボード II の OPNA の拡張側のアドレス / ステータス" },
	{ 0xad, 0, "音源", "SB2 OPNA ext data", "サウンドボード II の OPNA の拡張側のデータ" },

	// 漢字 ROM・辞書 ROM
	{ 0xe8, 0, "漢字 ROM・辞書 ROM", "Kanji1 addr L/data", "漢字 ROM 第 1 水準 (OUT: アドレスの下位 / IN: データ)" },
	{ 0xe9, 0, "漢字 ROM・辞書 ROM", "Kanji1 addr H/data", "漢字 ROM 第 1 水準 (OUT: アドレスの上位 / IN: データ)" },
	{ 0xec, 0, "漢字 ROM・辞書 ROM", "Kanji2 addr L/data", "漢字 ROM 第 2 水準 (OUT: アドレスの下位 / IN: データ)" },
	{ 0xed, 0, "漢字 ROM・辞書 ROM", "Kanji2 addr H/data", "漢字 ROM 第 2 水準 (OUT: アドレスの上位 / IN: データ)" },
	{ 0xf0, 0, "漢字 ROM・辞書 ROM", "Dictionary bank", "辞書 ROM のバンクの選択" },
	{ 0xf1, 0, "漢字 ROM・辞書 ROM", "Dictionary select", "辞書 ROM の選択" },

	// シリアル
	{ 0x20, 0, "シリアル (CMT・RS-232C)", "USART data", "USART (8251) のデータ (CMT・RS-232C)" },
	{ 0x21, 0, "シリアル (CMT・RS-232C)", "USART cmd/status", "USART (8251) のコマンド / ステータス" },
	{ 0xc2, 0, "シリアル (CMT・RS-232C)", "USART2 data", "2 つ目の USART (RS-232C) のデータ" },
	{ 0xc3, 0, "シリアル (CMT・RS-232C)", "USART2 cmd/status", "2 つ目の USART のコマンド / ステータス" },

	// サブシステム (メイン側)
	{ 0xfc, 0, "サブシステムとの通信 (メイン側)", "8255 port A", "サブシステムとの 8255 のポート A (サブからのデータ)" },
	{ 0xfd, 0, "サブシステムとの通信 (メイン側)", "8255 port B", "サブシステムとの 8255 のポート B (サブへのデータ)" },
	{ 0xfe, 0, "サブシステムとの通信 (メイン側)", "8255 port C", "サブシステムとの 8255 のポート C (ハンドシェイク)" },
	{ 0xff, 0, "サブシステムとの通信 (メイン側)", "8255 control", "サブシステムとの 8255 の制御 (ポート C のビットのセット/リセット)" },

	// サブ CPU (ディスク側)
	{ 0xf4, 1, "サブ CPU (ディスク側)", "Drive control", "ドライブの制御 (2D/2HD など) (OUT)" },
	{ 0xf8, 1, "サブ CPU (ディスク側)", "Motor/TC", "OUT: ドライブのモーター / IN: TC (転送の終わり)" },
	{ 0xfa, 1, "サブ CPU (ディスク側)", "FDC status", "FDC (μPD765) のステータス (IN)" },
	{ 0xfb, 1, "サブ CPU (ディスク側)", "FDC data", "FDC のデータ" },
	{ 0xfc, 1, "サブ CPU (ディスク側)", "8255 port A", "メインとの 8255 のポート A (メインからのデータ)" },
	{ 0xfd, 1, "サブ CPU (ディスク側)", "8255 port B", "メインとの 8255 のポート B (メインへのデータ)" },
	{ 0xfe, 1, "サブ CPU (ディスク側)", "8255 port C", "メインとの 8255 のポート C (ハンドシェイク)" },
	{ 0xff, 1, "サブ CPU (ディスク側)", "8255 control", "メインとの 8255 の制御" },
};
static const int nioports = sizeof(ioports) / sizeof(ioports[0]);

// ポートの短い名前 (一覧の窓用)。無ければ空
inline const char* IOPortName(int sub, uint port)
{
	for (int i = 0; i < nioports; i++)
		if (ioports[i].sub == sub && ioports[i].port == port)
			return ioports[i].name;
	return "";
}

}
