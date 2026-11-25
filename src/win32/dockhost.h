// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ドックモード: デバッグの窓を、本体の窓の上下左右のペインにタブで入れる
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------
//	ペインは本体の窓の子の窓。左右は縦いっぱい、上下は左右のペインの間。真ん中に PC-8801 の画面
//	(描く部品の子の窓を WinDraw::SetViewRect で置く)。ペインの中は区画に分けられ (左右のペインは縦に、
//	上下のペインは横に並べる)、区画ごとにタブ・メニューの帯・窓を持つ。窓を入れると、その窓は子の窓に
//	なりメニューバーが無くなるので、メニューは区画の帯に描いて、押すと同じメニューを出す
// ---------------------------------------------------------------------------

#pragma once

#include <vector>
#include <string>
#include <map>

class DockClient;

namespace PC8801
{

class DockHost
{
public:
	enum Side { left = 0, right, top, bottom, nsides };
	enum { newgroup = -2, lastgroup = -1 };		// Dock の group: 新しい区画を最後に足す / 最後の区画

	DockHost();
	~DockHost();

	bool Init(HWND mainwnd, HINSTANCE hinst);
	bool IsEnabled() const { return enabled; }
	void SetEnabled(bool on);				// ペインを作る・消す (入れていた窓は浮かせる)
	void SetSuspended(bool s);				// 全画面の間はペインを隠し、画面を窓いっぱいに
	void Repaint();							// 色が変わったとき
	void Layout();							// 本体の窓の大きさが変わったとき
	// 開いている窓をペインに入れる (ドックモードでなければ何もしない)。group は区画の番号、
	// lastgroup なら最後の区画 (無ければ作る)、newgroup なら新しい区画を最後に足す
	void Dock(DockClient* mon, int side, int group = lastgroup);
	void Float(HWND hwnd);					// ペインから出して浮かせる
	void OnShown(DockClient* mon);			// 窓を開いたとき: 前にペインにあったなら、そのペインへ
	void ShowPane(int side, bool show);
	bool IsPaneShown(int side) const { return panes[side].shown; }
	bool HasWindows(int side) const { return PaneItemCount(side) > 0; }	// ペインに窓が入っているか
	void Tick();							// 0.2 秒ごと: 閉じた窓を外す、タブの名前・帯を描き直す
	void LoadLayout();						// 起動時: 前にペインにあった窓を開いて入れる
	void SaveLayout();						// 終わるとき
	void SetTopInset(int px) { topinset = px; }	// 本体の窓の中の上の端から空ける高さ (タイトルの段)
	void GetView(RECT* r) { *r = view; }	// PC-8801 の画面を置く四角 (本体の窓の中の座標)
	bool InView(POINT clientpt);			// 本体の窓のその点が PC-8801 の画面の上か
	// 画面を置く四角が変わったときに呼ぶ (r が 0 なら窓いっぱい)。ui が描く部品に渡す
	void SetViewCallback(void (*cb)(void* ctx, const RECT* r), void* ctx) { viewcb = cb; viewctx = ctx; }
	int VisibleBits() const;				// 窓が入っていて開いているペイン (bit = 辺。全画面の間も前のまま)
	int DesiredExtent(int side);			// 開いたときのペインの幅・高さ (ピクセル、仕切り込み)
	int ActualExtent(int side);			// 今のペインの幅・高さ (見えていなければ 0)
	// 開いているペインの組が変わったときに呼ぶ (並べる前。本体の窓の大きさを変えてよい)
	void SetChangeCallback(void (*cb)(void* ctx, int oldbits, int newbits), void* ctx) { changecb = cb; changectx = ctx; }

private:
	struct Item
	{
		HWND hwnd;
		HMENU menu;			// 窓から外したメニュー (浮かせるときに戻す)
		LONG_PTR style, exstyle;
		std::string section;
	};
	struct StripItem
	{
		RECT rect;
		int index;			// メニューの項目の位置
	};
	// 区画: タブ・メニューの帯・窓
	struct Group
	{
		RECT tabrect;		// タブの並び (ペインの窓の座標。自分で描く)
		std::vector<RECT> tabitems;	// 各タブ
		std::vector<std::string> titles;	// タブの字 (窓の題)
		std::vector<Item> items;
		int cur;			// 見せているタブ
		int weight;			// 大きさの割合 (96 DPI 換算のピクセル)
		RECT area;			// 区画 (ペインの窓の座標)
		RECT strip;			// メニューの帯
		RECT body;			// 窓を置く所
		RECT splitter;		// 次の区画との仕切り (最後の区画は空)
		int striprows;		// 帯の段の数 (右に寄せる項目が入らなければ 2 段目に折り返す)
		std::vector<StripItem> stripitems;
	};
	struct Pane
	{
		HWND hwnd;
		std::vector<Group> groups;
		int size;			// 幅 (左右) か高さ (上下)。96 DPI 換算のピクセル
		bool shown;			// 開いている (閉じても窓は入れたまま)
		RECT splitter;		// ペインの内側の端の仕切り (ペインの窓の座標)
	};
	struct Where
	{
		int side, group;
	};

	static LRESULT CALLBACK PaneProc(HWND, UINT, WPARAM, LPARAM);
	LRESULT PaneMessage(int side, HWND, UINT, WPARAM, LPARAM);
	bool CreatePanes();
	void DestroyPanes();
	int AddGroup(int side, int at);			// 区画を作って at に入れる (-1 なら最後)。番号を返す
	void RemoveEmptyGroups(int side);
	bool FindItem(HWND hwnd, int* side, int* group, int* index);
	Item TakeItem(int side, int group, int index);	// 区画から外す (窓はそのまま)
	void FloatItem(const Item& it);
	// タブのドラッグ (M88gn): 落とす先
	enum DropKind { drop_none, drop_join, drop_newgroup, drop_pane, drop_float };
	struct DropTarget
	{
		int kind;
		int side, group, index;		// join: 区画とタブの位置 (-1: 最後)、newgroup: 区画を入れる位置、pane: 辺
		RECT rect;					// 落とす先を示す四角 (画面の座標)
	};
	int TabHit(int side, int group, POINT panept);	// その点のタブ (-1: 無い)
	void MeasureTabs(int side, int group);
	void PaintTabs(int side, int group, HDC dc);
	DropTarget FindDropTarget(POINT screen);
	void ShowDropIndicator(const RECT* r);
	void DoDrop();
	void PutItem(const Item& it, int side, int group, int index = -1);	// 区画に入れて選ぶ
	void Select(int side, int group, int index);
	void LayoutPane(int side);
	void LayoutGroup(int side, int group);
	void UpdateTabs(int side, int group);
	void MeasureStrip(Group& g, HDC dc, std::vector<int>& widths, int& rightfrom);
	void PaintStrip(int side, int group, HDC dc);
	void ClickStrip(int side, int group, POINT p);
	void TabMenu(int side, int group, int index);
	HFONT MenuFont();
	int Scale(int v);
	bool PaneVisible(int side) const;
	int PaneItemCount(int side) const;

	HWND hwndmain;
	HINSTANCE hinst;
	bool enabled;
	bool suspended;
	Pane panes[nsides];
	RECT view;
	HFONT menufont;
	UINT menufontdpi;
	int dragging;			// 仕切りを動かしているペイン (-1: 無し)
	int tabdownside, tabdowngroup;			// タブの上でボタンを押した (ドラッグの候補)
	int tabdownindex;
	POINT tabdownpt;
	bool tabdragging;		// タブをドラッグしている
	HWND dragwnd;			// ドラッグしている窓
	DropTarget droptarget;
	HWND hwnddrop;			// 落とす先を示す半透明の窓
	int draggroup;			// 区画の間の仕切りなら、前の区画の番号 (-1: ペインの端の仕切り)
	void (*viewcb)(void* ctx, const RECT* r);
	void (*changecb)(void* ctx, int oldbits, int newbits);
	void* changectx;
	int topinset;
	int lastbits;			// 前に並べたときの VisibleBits
	void* viewctx;
	std::vector<int> loadweights[nsides];		// 読み込んだ区画の大きさ (LoadLayout の間だけ)
	std::map<std::string, Where> lastplace;		// 閉じた窓が前にあったペイン・区画 (開き直したときに戻す)
};

}
