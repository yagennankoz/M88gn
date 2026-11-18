// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	画面の DPI (表示スケール) の扱い
// ---------------------------------------------------------------------------
//	M88 の本体の窓 (と、その子の窓・メニュー・ファイルの選択などの標準のダイアログ) だけを
//	DPI 対応 (Per Monitor v2) にする。物理ピクセルで描くので、表示スケールが 100% 以外でも
//	Windows に拡大されてぼけることが無い。
//	ソフトウェアキーボード・設定・デバッグ用のモニターなどの窓は、今までどおり DPI 非対応のまま
//	(Windows が表示スケールに合わせて拡大する)。それらの窓を作る・動かすときは UnawareScope で囲む。
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
