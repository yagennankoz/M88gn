// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	画面の DPI (表示スケール) の扱い
// ---------------------------------------------------------------------------
//	M88 の本体の窓 (と、その子の窓・メニュー・ファイルの選択などの標準のダイアログ) だけを
//	DPI 対応 (Per Monitor v2) にする。物理ピクセルで描くので、表示スケールが 100% 以外でも
//	Windows に拡大されてぼけることが無い。
//	ソフトウェアキーボード・デバッグ用のモニター・設定・About・新しいディスクの窓も DPI 対応
//	(ダイアログの大きさと字は Windows が画面の DPI に合わせる。M88gn 1.4.0 から)。
//	DPI 非対応のまま作りたい窓は UnawareScope で囲む (今は使っていない)。
//	Windows 10 1703 より前の Windows では何もしない (今までと同じく、全体が DPI 非対応)。

#pragma once

#include <windows.h>

namespace Dpi
{
	// メインのスレッドを DPI 対応にする (窓を作る前に 1 回呼ぶ)。できなければ false
	bool Init();
	// ほかのスレッドを DPI 対応にする (本体の窓の大きさを測る・窓を動かすスレッドの初めに呼ぶ。
	// DPI 非対応のスレッドからは、本体の窓の大きさが表示スケールで割った値で返る)
	void InitThread();
	bool IsAware();

	// 窓のある画面の DPI (96 = 100%)
	UINT GetForWindow(HWND hwnd);

	// 窓の DPI に合わせた AdjustWindowRectEx
	void AdjustWindowRect(RECT* rect, DWORD style, bool menu, DWORD exstyle, HWND hwnd);
	// DPI を指定した AdjustWindowRectEx (DPI 非対応なら DPI は使わない)
	void AdjustWindowRectForDpi(RECT* rect, DWORD style, bool menu, DWORD exstyle, UINT dpi);
	// その DPI のメニュー・ステータスバーなどの字 (SystemParametersInfo はいつも主モニターの DPI の値を返すため)。
	// SystemParametersInfoForDpi は Unicode 版の構造体しか受けないので、W 版で返す
	void GetNonClientMetrics(NONCLIENTMETRICSW* ncm, UINT dpi);

	// 囲んだ間だけ、このスレッドが作る・動かす窓を DPI 非対応として扱う
	class UnawareScope
	{
	public:
		UnawareScope();
		~UnawareScope();
	private:
		void* prev;
	};
}
