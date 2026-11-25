// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ペインに入れられる窓 (DockHost が扱う窓の共通の部分)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	デバッグの窓 (WinMonitor)・Video の窓・テープの窓が継ぐ。DockHost はこれを通して窓を入れる・出す。
//	ペインに入っている間 (docked) は、窓の位置・大きさを保存しない (浮かせたときの位置のまま)
// ---------------------------------------------------------------------------

#pragma once

class DockClient
{
public:
	DockClient();
	virtual ~DockClient();

	virtual HWND DockWindow() = 0;				// 窓 (開いていなければ 0)
	virtual const char* DockSection() = 0;		// M88.ini の [Dock] で窓を見分ける名前 (0 なら入れられない)
	virtual void DockFloatRect(RECT* r) = 0;	// 浮かせたときの位置・大きさ (画面の座標)
	virtual bool DockIsOpen() = 0;
	virtual void DockOpen(HINSTANCE hinst, HWND owner) = 0;	// 開く (起動時に前のペインへ戻すとき)
	virtual void SetDocked(bool d) { docked = d; }
	bool IsDocked() { return docked; }

	static DockClient* FindBySection(const char* section);
	static DockClient* FindByWindow(HWND hwnd);
	// 開いている窓すべてのスクロールバーをテーマに合わせる (テーマを切り替えたとき)
	static void ApplyThemeAll();

	// タイトルバーのメニューの「ドック」を選んだとき (side: 0 左 1 右 2 上 3 下、4～7 は新しい区画) と、
	// 窓を開いたときに呼ぶ (ui が決める)
	static void (*dockhook)(DockClient* c, int side);
	static void (*showhook)(DockClient* c);

	// タイトルバーのメニュー (システムメニュー) に「～のペインにドック」を足す
	static void AddDockMenu(HMENU sysmenu);
	// WM_SYSCOMMAND: ドックの項目なら処理して true
	bool DockSysCommand(WPARAM wp);
	// 窓を開いた直後に呼ぶ (前にペインにあったなら、そのペインへ)
	void DockShown() { if (showhook) showhook(this); }

protected:
	bool docked;

private:
	enum { maxclients = 64 };
	static DockClient* clients[maxclients];
};

#ifndef WM_DPICHANGED_AFTERPARENT
#define WM_DPICHANGED_AFTERPARENT	0x02e3		// 子の窓に: 親の窓の DPI が変わった (Windows 10 1703 から)
#endif
