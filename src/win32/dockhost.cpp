// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ドックモード: デバッグの窓を、本体の窓の上下左右のペインにタブで入れる
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#include "headers.h"
#include <commctrl.h>
#include "resource.h"
#include "dockhost.h"
#include "dockclient.h"
#include "status.h"
#include "misc.h"
#include "dpi.h"
#include "88config.h"
#include "uitheme.h"

using namespace PC8801;

#ifndef SPI_GETFLATMENU
#define SPI_GETFLATMENU		0x1022
#endif
#ifndef COLOR_MENUBAR
#define COLOR_MENUBAR		30
#endif

static const char* const panename[DockHost::nsides] = { "左", "右", "上", "下" };
static const char* const paneclass = "M88gn DockPane";

// 区画を縦に並べるペイン (左右のペイン)
static inline bool Vertical(int side) { return side == DockHost::left || side == DockHost::right; }

// ---------------------------------------------------------------------------
//	構築/消滅
//
DockHost::DockHost()
: hwndmain(0), hinst(0), enabled(false), suspended(false), menufont(0), menufontdpi(0), dragging(-1), draggroup(-1),
  tabdownside(-1), tabdowngroup(-1), tabdownindex(-1), tabdragging(false), dragwnd(0), hwnddrop(0),
  viewcb(0), viewctx(0), changecb(0), changectx(0), topinset(0), lastbits(0)
{
	for (int i = 0; i < nsides; i++)
	{
		panes[i].hwnd = 0;
		panes[i].size = 0;
		panes[i].shown = true;
		SetRectEmpty(&panes[i].splitter);
	}
	SetRectEmpty(&view);
	tabdownpt.x = tabdownpt.y = 0;
	memset(&droptarget, 0, sizeof(droptarget));
}

DockHost::~DockHost()
{
	if (menufont)
		DeleteObject(menufont);
}

bool DockHost::Init(HWND mainwnd, HINSTANCE hi)
{
	hwndmain = mainwnd;
	hinst = hi;
	WNDCLASS wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = PaneProc;
	wc.hInstance = hinst;
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = 0;
	wc.lpszClassName = paneclass;
	RegisterClass(&wc);
	// 大きさの初期値 (96 DPI 換算)
	panes[left].size = panes[right].size = 420;
	panes[top].size = panes[bottom].size = 260;
	return true;
}

int DockHost::Scale(int v)
{
	return MulDiv(v, hwndmain ? Dpi::GetForWindow(hwndmain) : 96, 96);
}

HFONT DockHost::MenuFont()
{
	UINT dpi = hwndmain ? Dpi::GetForWindow(hwndmain) : 96;
	if (menufont && menufontdpi == dpi)
		return menufont;
	if (menufont)
		DeleteObject(menufont);
	NONCLIENTMETRICSW ncm;
	Dpi::GetNonClientMetrics(&ncm, dpi);
	menufont = CreateFontIndirectW(&ncm.lfMenuFont);
	menufontdpi = dpi;
	return menufont;
}

int DockHost::PaneItemCount(int side) const
{
	int n = 0;
	for (size_t g = 0; g < panes[side].groups.size(); g++)
		n += int(panes[side].groups[g].items.size());
	return n;
}

int DockHost::VisibleBits() const
{
	int bits = 0;
	if (enabled)
		for (int s = 0; s < nsides; s++)
			if (panes[s].shown && PaneItemCount(s) > 0)
				bits |= 1 << s;
	return bits;
}

int DockHost::DesiredExtent(int side)
{
	return Scale(panes[side].size);
}

int DockHost::ActualExtent(int side)
{
	HWND h = panes[side].hwnd;
	if (!h || !IsWindowVisible(h))
		return 0;
	RECT r;
	GetWindowRect(h, &r);
	return side < top ? r.right - r.left : r.bottom - r.top;
}

bool DockHost::PaneVisible(int side) const
{
	return enabled && !suspended && panes[side].shown && PaneItemCount(side) > 0;
}

// ---------------------------------------------------------------------------
//	ドックモードを始める・やめる
//
void DockHost::SetEnabled(bool on)
{
	if (on == enabled)
		return;
	if (on)
	{
		enabled = true;
		CreatePanes();
		Layout();
	}
	else
	{
		// 入れていた窓は浮かせる
		for (int s = 0; s < nsides; s++)
		{
			Pane& p = panes[s];
			for (size_t g = 0; g < p.groups.size(); g++)
				for (size_t i = 0; i < p.groups[g].items.size(); i++)
					FloatItem(p.groups[g].items[i]);
		}
		DestroyPanes();
		enabled = false;
		if (viewcb)
			viewcb(viewctx, 0);
	}
}

void DockHost::SetSuspended(bool s)
{
	suspended = s;
	if (enabled)
		Layout();
}

bool DockHost::CreatePanes()
{
	for (int s = 0; s < nsides; s++)
	{
		Pane& p = panes[s];
		if (p.hwnd)
			continue;
		p.hwnd = CreateWindowEx(0, paneclass, "", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
								0, 0, 10, 10, hwndmain, 0, hinst, 0);
		SetWindowLongPtr(p.hwnd, GWLP_USERDATA, (LONG_PTR) this);
		SetWindowLongPtr(p.hwnd, GWLP_ID, 0x7f00 + s);	// 子の窓の番号で、どのペインかを見分ける
	}
	return true;
}

void DockHost::DestroyPanes()
{
	for (int s = 0; s < nsides; s++)
	{
		if (panes[s].hwnd)
			DestroyWindow(panes[s].hwnd);		// 区画のタブも一緒に壊れる
		panes[s].hwnd = 0;
		panes[s].groups.clear();
	}
}

// 区画を作って at に入れる (-1 なら最後)
int DockHost::AddGroup(int side, int at)
{
	Pane& p = panes[side];
	Group g;
	SetRectEmpty(&g.tabrect);
	g.cur = -1;
	// 大きさは、今ある区画の平均 (無ければ適当に)
	int sum = 0;
	for (size_t i = 0; i < p.groups.size(); i++)
		sum += p.groups[i].weight;
	g.weight = p.groups.empty() ? 300 : Max(60, sum / int(p.groups.size()));
	if (at >= 0 && at < int(loadweights[side].size()) && loadweights[side][at] > 0)
		g.weight = loadweights[side][at];
	else if (at < 0 && p.groups.size() < loadweights[side].size() && loadweights[side][p.groups.size()] > 0)
		g.weight = loadweights[side][p.groups.size()];
	g.striprows = 1;
	SetRectEmpty(&g.area);
	SetRectEmpty(&g.strip);
	SetRectEmpty(&g.body);
	SetRectEmpty(&g.splitter);
	if (at < 0 || at > int(p.groups.size()))
		at = int(p.groups.size());
	p.groups.insert(p.groups.begin() + at, g);
	return at;
}

void DockHost::RemoveEmptyGroups(int side)
{
	Pane& p = panes[side];
	for (int g = int(p.groups.size()) - 1; g >= 0; g--)
	{
		if (!p.groups[g].items.empty())
			continue;
		p.groups.erase(p.groups.begin() + g);
	}
}

// ---------------------------------------------------------------------------
//	並べる
//
void DockHost::Layout()
{
	if (!enabled || !hwndmain)
		return;
	// 開いているペインの組が変わった: 本体の窓の大きさを合わせてもらう (その中でまた並べることがある)
	if (!suspended)
	{
		int bits = VisibleBits();
		if (bits != lastbits)
		{
			int old = lastbits;
			lastbits = bits;
			if (changecb)
				changecb(changectx, old, bits);
		}
	}
	RECT c;
	GetClientRect(hwndmain, &c);
	c.top += topinset;
	HWND sb = statusdisplay.GetHWnd();
	if (sb && IsWindowVisible(sb))
		c.bottom -= statusdisplay.GetHeight();
	if (suspended)
	{
		for (int s = 0; s < nsides; s++)
			if (panes[s].hwnd)
				ShowWindow(panes[s].hwnd, SW_HIDE);
		if (viewcb)
			viewcb(viewctx, 0);
		return;
	}

	int w = c.right - c.left, h = c.bottom - c.top;
	int minview = Scale(120), minpane = Scale(80);
	int sz[nsides];
	for (int s = 0; s < nsides; s++)
		sz[s] = PaneVisible(s) ? Scale(panes[s].size) : 0;
	// 画面の分を残す
	if (sz[left] + sz[right] > w - minview)
	{
		int room = Max(0, w - minview);
		int total = sz[left] + sz[right];
		if (total > 0)
			sz[left] = sz[left] * room / total, sz[right] = room - sz[left];
	}
	if (sz[top] + sz[bottom] > h - minview)
	{
		int room = Max(0, h - minview);
		int total = sz[top] + sz[bottom];
		if (total > 0)
			sz[top] = sz[top] * room / total, sz[bottom] = room - sz[top];
	}
	for (int s = 0; s < nsides; s++)
		if (PaneVisible(s))
			sz[s] = Max(sz[s], Min(minpane, (s < top ? w : h) / 3));

	RECT r[nsides];
	SetRect(&r[left], c.left, c.top, c.left + sz[left], c.bottom);
	SetRect(&r[right], c.right - sz[right], c.top, c.right, c.bottom);
	SetRect(&r[top], c.left + sz[left], c.top, c.right - sz[right], c.top + sz[top]);
	SetRect(&r[bottom], c.left + sz[left], c.bottom - sz[bottom], c.right - sz[right], c.bottom);
	SetRect(&view, c.left + sz[left], c.top + sz[top], c.right - sz[right], c.bottom - sz[bottom]);
	if (view.right <= view.left)
		view.right = view.left + 1;
	if (view.bottom <= view.top)
		view.bottom = view.top + 1;

	for (int s = 0; s < nsides; s++)
	{
		Pane& p = panes[s];
		if (!p.hwnd)
			continue;
		if (PaneVisible(s))
		{
			SetWindowPos(p.hwnd, HWND_TOP, r[s].left, r[s].top, r[s].right - r[s].left, r[s].bottom - r[s].top,
						 SWP_SHOWWINDOW | SWP_NOACTIVATE);
			LayoutPane(s);
		}
		else
			ShowWindow(p.hwnd, SW_HIDE);
	}
	if (viewcb)
		viewcb(viewctx, &view);
}

// ペインの中: 内側の端の仕切りと、区画 (左右のペインは縦に、上下のペインは横に並べる)
void DockHost::LayoutPane(int side)
{
	Pane& p = panes[side];
	RECT c;
	GetClientRect(p.hwnd, &c);
	int sp = Scale(5);
	RECT in = c;
	switch (side)
	{
	case left:		SetRect(&p.splitter, c.right - sp, c.top, c.right, c.bottom); in.right -= sp; break;
	case right:		SetRect(&p.splitter, c.left, c.top, c.left + sp, c.bottom); in.left += sp; break;
	case top:		SetRect(&p.splitter, c.left, c.bottom - sp, c.right, c.bottom); in.bottom -= sp; break;
	case bottom:	SetRect(&p.splitter, c.left, c.top, c.right, c.top + sp); in.top += sp; break;
	}
	int n = int(p.groups.size());
	if (!n)
		return;
	bool vert = Vertical(side);
	int len = (vert ? in.bottom - in.top : in.right - in.left) - sp * (n - 1);
	int sum = 0;
	for (int g = 0; g < n; g++)
		sum += Max(1, p.groups[g].weight);
	int pos = vert ? in.top : in.left;
	int used = 0;
	for (int g = 0; g < n; g++)
	{
		Group& gr = p.groups[g];
		int l = (g == n - 1) ? len - used : MulDiv(len, Max(1, gr.weight), sum);
		used += l;
		if (vert)
		{
			SetRect(&gr.area, in.left, pos, in.right, pos + l);
			pos += l;
			if (g < n - 1)
				SetRect(&gr.splitter, in.left, pos, in.right, pos + sp);
			else
				SetRectEmpty(&gr.splitter);
		}
		else
		{
			SetRect(&gr.area, pos, in.top, pos + l, in.bottom);
			pos += l;
			if (g < n - 1)
				SetRect(&gr.splitter, pos, in.top, pos + sp, in.bottom);
			else
				SetRectEmpty(&gr.splitter);
		}
		pos += sp;
		LayoutGroup(side, g);
	}
	InvalidateRect(p.hwnd, 0, FALSE);
}

// 区画の中: タブ、メニューの帯、窓
void DockHost::LayoutGroup(int side, int group)
{
	Pane& p = panes[side];
	Group& g = p.groups[group];
	RECT in = g.area;
	HDC dc = GetDC(p.hwnd);
	HGDIOBJ old = SelectObject(dc, MenuFont());
	TEXTMETRIC tm;
	GetTextMetrics(dc, &tm);
	// 帯: 項目が 1 段に入らなければ、右に寄せる項目を 2 段目に
	std::vector<int> widths;
	int rightfrom;
	MeasureStrip(g, dc, widths, rightfrom);
	SelectObject(dc, old);
	ReleaseDC(p.hwnd, dc);
	int fh = tm.tmHeight;
	int tabh = fh + Scale(10);
	int total = Scale(2);
	for (size_t i = 0; i < widths.size(); i++)
		total += widths[i];
	g.striprows = (rightfrom < int(widths.size()) && total > in.right - in.left) ? 2 : 1;
	int striph = widths.empty() ? 0 : (fh + Scale(8)) * g.striprows;	// メニューの無い窓は帯を出さない
	SetRect(&g.tabrect, in.left, in.top, in.right, Min(int(in.bottom), int(in.top + tabh)));
	MeasureTabs(side, group);
	SetRect(&g.strip, in.left, in.top + tabh, in.right, Min(int(in.bottom), int(in.top + tabh + striph)));
	SetRect(&g.body, in.left, g.strip.bottom, in.right, in.bottom);
	if (g.body.bottom < g.body.top)
		g.body.bottom = g.body.top;
	for (size_t i = 0; i < g.items.size(); i++)
	{
		HWND h = g.items[i].hwnd;
		if (int(i) == g.cur)
			SetWindowPos(h, HWND_TOP, g.body.left, g.body.top, g.body.right - g.body.left, g.body.bottom - g.body.top,
						 SWP_SHOWWINDOW | SWP_NOACTIVATE);
		else
			ShowWindow(h, SW_HIDE);
	}
}

bool DockHost::InView(POINT pt)
{
	return enabled && !suspended && PtInRect(&view, pt);
}

// ---------------------------------------------------------------------------
//	入れる・出す
//
bool DockHost::FindItem(HWND hwnd, int* side, int* group, int* index)
{
	for (int s = 0; s < nsides; s++)
		for (size_t g = 0; g < panes[s].groups.size(); g++)
			for (size_t i = 0; i < panes[s].groups[g].items.size(); i++)
				if (panes[s].groups[g].items[i].hwnd == hwnd)
				{
					*side = s, *group = int(g), *index = int(i);
					return true;
				}
	return false;
}

DockHost::Item DockHost::TakeItem(int side, int group, int index)
{
	Group& g = panes[side].groups[group];
	Item it = g.items[index];
	g.items.erase(g.items.begin() + index);
	if (g.cur >= index && g.cur > 0)
		g.cur--;
	if (g.items.empty())
		g.cur = -1;
	UpdateTabs(side, group);
	return it;
}

void DockHost::PutItem(const Item& it, int side, int group, int index)
{
	Pane& p = panes[side];
	Group& g = p.groups[group];
	if (GetParent(it.hwnd) != p.hwnd)
		SetParent(it.hwnd, p.hwnd);
	if (index < 0 || index > int(g.items.size()))
		index = int(g.items.size());
	g.items.insert(g.items.begin() + index, it);
	g.cur = index;
	Where w = { side, group };
	lastplace[it.section] = w;
	UpdateTabs(side, group);
}

void DockHost::Dock(DockClient* mon, int side, int group)
{
	if (!mon || side < 0 || side >= nsides || !enabled)
		return;
	HWND h = mon->DockWindow();
	if (!h || !mon->DockSection())
		return;
	Pane& p = panes[side];

	Item it;
	int os, og, oi;
	bool moved = FindItem(h, &os, &og, &oi);
	if (moved)
	{
		// 入れたままで同じ区画なら選ぶだけ
		if (os == side && (og == group || (group == lastgroup && og == int(p.groups.size()) - 1)))
		{
			Select(side, og, oi);
			return;
		}
		it = TakeItem(os, og, oi);
	}
	else
	{
		it.hwnd = h;
		it.menu = GetMenu(h);
		it.style = GetWindowLongPtr(h, GWL_STYLE);
		it.exstyle = GetWindowLongPtr(h, GWL_EXSTYLE);
		it.section = mon->DockSection();
		mon->SetDocked(true);		// これ以降、位置・大きさを保存しない (浮かせたときのまま)
		SetMenu(h, 0);
		LONG_PTR st = (it.style & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX))
					  | WS_CHILD | WS_CLIPSIBLINGS;
		LONG_PTR ex = it.exstyle & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_TOOLWINDOW | WS_EX_APPWINDOW);
		SetWindowLongPtr(h, GWL_STYLE, st);
		SetWindowLongPtr(h, GWL_EXSTYLE, ex);
		SetParent(h, p.hwnd);
		SetWindowLongPtr(h, GWLP_ID, 0);		// 子の窓の番号 (メニューの場所) を 0 に。GetMenu が 0 を返す
		SetWindowPos(h, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
	}

	// 入れる区画
	int g;
	if (group == newgroup || p.groups.empty() || group >= int(p.groups.size()))
		g = AddGroup(side, -1);
	else if (group == lastgroup)
		g = int(p.groups.size()) - 1;
	else
		g = group;
	PutItem(it, side, g);
	if (moved)
		RemoveEmptyGroups(os);
	p.shown = true;
	Layout();
}

void DockHost::FloatItem(const Item& it)
{
	if (!IsWindow(it.hwnd))
		return;
	DockClient* mon = DockClient::FindByWindow(it.hwnd);
	ShowWindow(it.hwnd, SW_HIDE);
	SetParent(it.hwnd, 0);
	SetWindowLongPtr(it.hwnd, GWL_STYLE, it.style);
	SetWindowLongPtr(it.hwnd, GWL_EXSTYLE, it.exstyle);
	SetWindowLongPtr(it.hwnd, GWLP_HWNDPARENT, (LONG_PTR) hwndmain);	// 持ち主は本体の窓 (前と同じ)
	SetMenu(it.hwnd, it.menu);
	RECT fr;
	SetRectEmpty(&fr);
	if (mon)
	{
		mon->SetDocked(false);
		mon->DockFloatRect(&fr);
	}
	if (fr.right > fr.left && fr.bottom > fr.top)
		SetWindowPos(it.hwnd, HWND_TOP, fr.left, fr.top, fr.right - fr.left, fr.bottom - fr.top,
					 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
	else
		SetWindowPos(it.hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
	DrawMenuBar(it.hwnd);
}

void DockHost::Float(HWND hwnd)
{
	int s, g, i;
	if (!FindItem(hwnd, &s, &g, &i))
		return;
	Item it = TakeItem(s, g, i);
	lastplace.erase(it.section);
	FloatItem(it);
	RemoveEmptyGroups(s);
	Layout();
}

void DockHost::OnShown(DockClient* mon)
{
	if (!enabled || !mon || !mon->DockSection())
		return;
	std::map<std::string, Where>::iterator i = lastplace.find(mon->DockSection());
	if (i != lastplace.end())
		Dock(mon, i->second.side, i->second.group);
}

void DockHost::ShowPane(int side, bool show)
{
	panes[side].shown = show;
	Layout();
}

void DockHost::Select(int side, int group, int index)
{
	Group& g = panes[side].groups[group];
	if (index < 0 || index >= int(g.items.size()))
		return;
	g.cur = index;
	LayoutGroup(side, group);
	InvalidateRect(panes[side].hwnd, 0, FALSE);
}

// タブを窓の題に合わせて作り直す
void DockHost::UpdateTabs(int side, int group)
{
	Pane& p = panes[side];
	Group& g = p.groups[group];
	bool changed = g.titles.size() != g.items.size();
	std::vector<std::string> t(g.items.size());
	for (size_t i = 0; i < g.items.size(); i++)
	{
		char buf[128] = "";
		GetWindowText(g.items[i].hwnd, buf, sizeof(buf));
		t[i] = buf;
		if (!changed && g.titles[i] != t[i])
			changed = true;
	}
	if (!changed)
		return;
	g.titles = t;
	MeasureTabs(side, group);
	if (p.hwnd)
		InvalidateRect(p.hwnd, &g.tabrect, FALSE);
}

// タブの大きさ: 字の幅 + 左右の余白。区画の幅に入らなければ、同じ割合で縮める
void DockHost::MeasureTabs(int side, int group)
{
	Pane& p = panes[side];
	Group& g = p.groups[group];
	g.tabitems.assign(g.items.size(), RECT());
	if (!p.hwnd || g.items.empty() || IsRectEmpty(&g.tabrect))
		return;
	if (g.titles.size() != g.items.size())
	{
		g.titles.resize(g.items.size());
		for (size_t i = 0; i < g.items.size(); i++)
		{
			char buf[128] = "";
			GetWindowText(g.items[i].hwnd, buf, sizeof(buf));
			g.titles[i] = buf;
		}
	}
	HDC dc = GetDC(p.hwnd);
	HGDIOBJ old = SelectObject(dc, MenuFont());
	int pad = Scale(12);
	std::vector<int> w(g.items.size());
	int total = 0;
	for (size_t i = 0; i < g.items.size(); i++)
	{
		SIZE sz = { 0, 0 };
		GetTextExtentPoint32(dc, g.titles[i].c_str(), int(g.titles[i].size()), &sz);
		w[i] = sz.cx + pad * 2;
		total += w[i];
	}
	SelectObject(dc, old);
	ReleaseDC(p.hwnd, dc);
	int room = g.tabrect.right - g.tabrect.left;
	int x = g.tabrect.left;
	for (size_t i = 0; i < w.size(); i++)
	{
		int wi = total > room ? Max(Scale(24), MulDiv(w[i], room, total)) : w[i];
		SetRect(&g.tabitems[i], x, g.tabrect.top, x + wi, g.tabrect.bottom);
		x += wi;
	}
}

int DockHost::TabHit(int side, int group, POINT pt)
{
	Group& g = panes[side].groups[group];
	for (size_t i = 0; i < g.tabitems.size(); i++)
		if (PtInRect(&g.tabitems[i], pt))
			return int(i);
	return -1;
}

// タブ: 選んでいるタブはメニューの帯と同じ地で、上に色の線。ほかは地を沈め、間に区切りの線
void DockHost::PaintTabs(int side, int group, HDC dc)
{
	Group& g = panes[side].groups[group];
	RECT r = g.tabrect;
	int w = r.right - r.left, h = r.bottom - r.top;
	if (w <= 0 || h <= 0)
		return;
	const Theme::Colors& c = Theme::C();
	HDC mdc = CreateCompatibleDC(dc);
	HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
	HGDIOBJ oldbmp = SelectObject(mdc, bmp);
	HBRUSH bg = CreateSolidBrush(c.panebg);
	RECT lr = { 0, 0, w, h };
	FillRect(mdc, &lr, bg);
	DeleteObject(bg);
	HGDIOBJ oldfont = SelectObject(mdc, MenuFont());
	SetBkMode(mdc, TRANSPARENT);
	int line = Max(1, Scale(2));
	for (size_t i = 0; i < g.tabitems.size() && i < g.titles.size(); i++)
	{
		RECT ir = g.tabitems[i];
		OffsetRect(&ir, -r.left, -r.top);
		bool sel = int(i) == g.cur;
		if (sel)
		{
			HBRUSH b = CreateSolidBrush(c.tabselbg);
			FillRect(mdc, &ir, b);
			DeleteObject(b);
			RECT ar = { ir.left, ir.top, ir.right, ir.top + line };
			b = CreateSolidBrush(c.tabaccent);
			FillRect(mdc, &ar, b);
			DeleteObject(b);
		}
		// 区切り: 選んでいないタブの右 (次が選んでいるタブなら要らない)
		bool nextsel = int(i) + 1 == g.cur;
		if (!sel && !nextsel)
		{
			int m = Scale(6);
			RECT sr = { ir.right - 1, ir.top + m, ir.right, ir.bottom - m };
			HBRUSH b = CreateSolidBrush(c.tabsep);
			FillRect(mdc, &sr, b);
			DeleteObject(b);
		}
		RECT tr = ir;
		InflateRect(&tr, -Scale(8), 0);
		SetTextColor(mdc, sel ? c.tabselfg : c.tabfg);
		DrawText(mdc, g.titles[i].c_str(), -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	}
	SelectObject(mdc, oldfont);
	BitBlt(dc, r.left, r.top, w, h, mdc, 0, 0, SRCCOPY);
	SelectObject(mdc, oldbmp);
	DeleteObject(bmp);
	DeleteDC(mdc);
}

// 0.2 秒ごと: 閉じた窓を外す、タブの題・メニューの帯
void DockHost::Tick()
{
	if (!enabled)
		return;
	bool relayout = false;
	for (int s = 0; s < nsides; s++)
	{
		Pane& p = panes[s];
		for (size_t g = 0; g < p.groups.size(); g++)
		{
			Group& gr = p.groups[g];
			for (int i = int(gr.items.size()) - 1; i >= 0; i--)
			{
				if (!IsWindow(gr.items[i].hwnd) || !IsChild(p.hwnd, gr.items[i].hwnd))
				{
					// 閉じた: 開き直したときに戻すペイン・区画を覚える
					Where w = { s, int(g) };
					lastplace[gr.items[i].section] = w;
					gr.items.erase(gr.items.begin() + i);
					if (gr.cur >= i && gr.cur > 0)
						gr.cur--;
					if (gr.items.empty())
						gr.cur = -1;
					relayout = true;
				}
			}
			UpdateTabs(s, int(g));
			if (PaneVisible(s))
				InvalidateRect(p.hwnd, &gr.strip, FALSE);
		}
		if (relayout)
			RemoveEmptyGroups(s);
	}
	if (relayout)
		Layout();
}

// ---------------------------------------------------------------------------
//	メニューの帯 (ドックした窓のメニューバーの代わり)
//
void DockHost::MeasureStrip(Group& g, HDC dc, std::vector<int>& widths, int& rightfrom)
{
	widths.clear();
	rightfrom = 0;
	if (g.cur < 0 || g.cur >= int(g.items.size()) || !g.items[g.cur].menu)
		return;
	Item& it = g.items[g.cur];
	HMENU m = it.menu;
	int n = GetMenuItemCount(m);
	widths.resize(n);
	rightfrom = n;
	int pad = Scale(8);
	for (int i = 0; i < n; i++)
	{
		char text[128] = "";
		MENUITEMINFO mii;
		memset(&mii, 0, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_STATE;
		mii.dwTypeData = text;
		mii.cch = sizeof(text);
		GetMenuItemInfo(m, i, TRUE, &mii);
		if ((mii.fType & MFT_RIGHTJUSTIFY) && rightfrom == n)
			rightfrom = i;
		if (mii.fType & MFT_OWNERDRAW)
		{
			MEASUREITEMSTRUCT mis;
			memset(&mis, 0, sizeof(mis));
			mis.CtlType = ODT_MENU;
			mis.itemID = mii.wID;
			SendMessage(it.hwnd, WM_MEASUREITEM, 0, (LPARAM) &mis);
			widths[i] = mis.itemWidth + pad;
		}
		else
		{
			RECT tr = { 0, 0, 0, 0 };
			DrawText(dc, text, -1, &tr, DT_CALCRECT | DT_SINGLELINE);
			widths[i] = tr.right - tr.left + pad * 2;
		}
	}
}

void DockHost::PaintStrip(int side, int group, HDC dc)
{
	Group& g = panes[side].groups[group];
	RECT r = g.strip;
	int w = r.right - r.left, h = r.bottom - r.top;
	if (w <= 0 || h <= 0)
		return;
	HDC mdc = CreateCompatibleDC(dc);
	HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
	HGDIOBJ oldbmp = SelectObject(mdc, bmp);
	RECT lr = { 0, 0, w, h };
	BOOL flat = FALSE;
	SystemParametersInfo(SPI_GETFLATMENU, 0, &flat, 0);
	{
		HBRUSH sb = CreateSolidBrush(Theme::C().tabselbg);	// 選んでいるタブと同じ地 (テーマ)
		FillRect(mdc, &lr, sb);
		DeleteObject(sb);
	}
	(void) flat;
	g.stripitems.clear();

	if (g.cur >= 0 && g.cur < int(g.items.size()) && g.items[g.cur].menu)
	{
		Item& it = g.items[g.cur];
		HMENU m = it.menu;
		HGDIOBJ oldfont = SelectObject(mdc, MenuFont());
		SetBkMode(mdc, TRANSPARENT);
		std::vector<int> widths;
		int rightfrom;
		MeasureStrip(g, mdc, widths, rightfrom);
		int n = int(widths.size());
		int rightw = 0;
		for (int i = rightfrom; i < n; i++)
			rightw += widths[i];
		// 2 段なら、右に寄せる項目は 2 段目の右に
		int rows = Max(1, g.striprows);
		int rowh = h / rows;
		int x = Scale(2), y = 0;
		for (int i = 0; i < n; i++)
		{
			if (i == rightfrom)
			{
				if (rows > 1)
					y = rowh, x = Scale(2);
				x = Max(x, w - rightw);
			}
			RECT ir = { x, y, x + widths[i], y + rowh };
			x += widths[i];
			char text[128] = "";
			MENUITEMINFO mii;
			memset(&mii, 0, sizeof(mii));
			mii.cbSize = sizeof(mii);
			mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_STATE | MIIM_DATA;
			mii.dwTypeData = text;
			mii.cch = sizeof(text);
			GetMenuItemInfo(m, i, TRUE, &mii);
			bool gray = (mii.fState & (MFS_GRAYED | MFS_DISABLED)) != 0;
			if (mii.fType & MFT_OWNERDRAW)
			{
				DRAWITEMSTRUCT dis;
				memset(&dis, 0, sizeof(dis));
				dis.CtlType = ODT_MENU;
				dis.itemID = mii.wID;
				dis.itemAction = ODA_DRAWENTIRE;
				dis.itemState = gray ? (ODS_GRAYED | ODS_DISABLED) : 0;
				dis.hwndItem = (HWND) m;
				dis.hDC = mdc;
				dis.rcItem = ir;
				dis.itemData = mii.dwItemData;
				Theme::stripdrawing = true;		// 項目は帯の色で描く
				SendMessage(it.hwnd, WM_DRAWITEM, 0, (LPARAM) &dis);
				Theme::stripdrawing = false;
				SelectObject(mdc, MenuFont());
				SetBkMode(mdc, TRANSPARENT);
			}
			else
			{
				SetTextColor(mdc, gray ? Theme::C().stripgray : Theme::C().stripfg);
				DrawText(mdc, text, -1, &ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			}
			StripItem si;
			si.rect = ir;
			OffsetRect(&si.rect, r.left, r.top);
			si.index = i;
			g.stripitems.push_back(si);
		}
		SelectObject(mdc, oldfont);
	}
	BitBlt(dc, r.left, r.top, w, h, mdc, 0, 0, SRCCOPY);
	SelectObject(mdc, oldbmp);
	DeleteObject(bmp);
	DeleteDC(mdc);
}

void DockHost::ClickStrip(int side, int group, POINT pt)
{
	Pane& p = panes[side];
	Group& g = p.groups[group];
	if (g.cur < 0 || g.cur >= int(g.items.size()))
		return;
	Item it = g.items[g.cur];
	for (size_t k = 0; k < g.stripitems.size(); k++)
	{
		const StripItem& si = g.stripitems[k];
		if (!PtInRect(&si.rect, pt))
			continue;
		MENUITEMINFO mii;
		memset(&mii, 0, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_ID | MIIM_STATE | MIIM_SUBMENU;
		GetMenuItemInfo(it.menu, si.index, TRUE, &mii);
		RECT ir = si.rect;
		if (mii.hSubMenu)
		{
			// メニューを開く前の処理 (チェックなど) は窓の WM_INITMENU にあるので、先に送る
			SendMessage(it.hwnd, WM_INITMENU, (WPARAM) it.menu, 0);
			POINT sp = { ir.left, ir.bottom };
			ClientToScreen(p.hwnd, &sp);
			TrackPopupMenu(mii.hSubMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, sp.x, sp.y, 0, it.hwnd, 0);
		}
		else if (!(mii.fState & (MFS_GRAYED | MFS_DISABLED)))
			PostMessage(it.hwnd, WM_COMMAND, MAKEWPARAM(mii.wID, 0), 0);
		InvalidateRect(p.hwnd, 0, FALSE);
		return;
	}
}

// タブの右クリック: 区画を分ける・隣の区画へ・別のペインへ・浮かせる・閉じる
void DockHost::TabMenu(int side, int group, int index)
{
	Pane& p = panes[side];
	Group& g = p.groups[group];
	if (index < 0 || index >= int(g.items.size()))
		return;
	HWND h = g.items[index].hwnd;
	bool vert = Vertical(side);
	enum { cmd_split = 20, cmd_prev, cmd_next, cmd_float, cmd_close };
	HMENU hm = CreatePopupMenu();
	AppendMenu(hm, MF_STRING | (g.items.size() >= 2 ? 0 : MF_GRAYED), cmd_split,
			   vert ? "区画を分けて下へ移す" : "区画を分けて右へ移す");
	AppendMenu(hm, MF_STRING | (group > 0 ? 0 : MF_GRAYED), cmd_prev, vert ? "上の区画へ移す" : "左の区画へ移す");
	AppendMenu(hm, MF_STRING | (group < int(p.groups.size()) - 1 ? 0 : MF_GRAYED), cmd_next,
			   vert ? "下の区画へ移す" : "右の区画へ移す");
	AppendMenu(hm, MF_SEPARATOR, 0, 0);
	char buf[64];
	for (int s = 0; s < nsides; s++)
	{
		wsprintf(buf, "%sのペインへ", panename[s]);
		AppendMenu(hm, MF_STRING | (s == side ? MF_GRAYED : 0), 1 + s, buf);
	}
	AppendMenu(hm, MF_SEPARATOR, 0, 0);
	AppendMenu(hm, MF_STRING, cmd_float, "浮かせる (ペインから出す)");
	AppendMenu(hm, MF_STRING, cmd_close, "閉じる");
	POINT sp;
	GetCursorPos(&sp);
	int cmd = TrackPopupMenu(hm, TPM_RETURNCMD | TPM_RIGHTBUTTON, sp.x, sp.y, 0, p.hwnd, 0);
	DestroyMenu(hm);

	if (cmd >= 1 && cmd <= nsides)
		Dock(DockClient::FindByWindow(h), cmd - 1, lastgroup);
	else if (cmd == cmd_split)
	{
		Item it = TakeItem(side, group, index);
		int ng = AddGroup(side, group + 1);
		PutItem(it, side, ng);
		Layout();
	}
	else if (cmd == cmd_prev || cmd == cmd_next)
	{
		Item it = TakeItem(side, group, index);
		PutItem(it, side, cmd == cmd_prev ? group - 1 : group + 1);
		RemoveEmptyGroups(side);
		Layout();
	}
	else if (cmd == cmd_float)
		Float(h);
	else if (cmd == cmd_close)
		PostMessage(h, WM_CLOSE, 0, 0);
}

// ---------------------------------------------------------------------------
//	ペインの窓
//
LRESULT CALLBACK DockHost::PaneProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	DockHost* host = (DockHost*) GetWindowLongPtr(hwnd, GWLP_USERDATA);
	int side = int(GetWindowLongPtr(hwnd, GWLP_ID)) - 0x7f00;
	if (host && side >= 0 && side < nsides && host->panes[side].hwnd == hwnd)
		return host->PaneMessage(side, hwnd, msg, wp, lp);
	return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT DockHost::PaneMessage(int side, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	Pane& p = panes[side];
	switch (msg)
	{
	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(hwnd, &ps);
			const Theme::Colors& c = Theme::C();
			HBRUSH spb = CreateSolidBrush(c.splitter);
			FillRect(dc, &p.splitter, spb);
			for (size_t g = 0; g < p.groups.size(); g++)
			{
				Group& gr = p.groups[g];
				if (!IsRectEmpty(&gr.splitter))
					FillRect(dc, &gr.splitter, spb);
				PaintTabs(side, int(g), dc);
				PaintStrip(side, int(g), dc);
				if (gr.cur < 0)
				{
					HBRUSH eb = CreateSolidBrush(c.emptybg);
					FillRect(dc, &gr.body, eb);
					DeleteObject(eb);
				}
			}
			DeleteObject(spb);
			EndPaint(hwnd, &ps);
		}
		return 0;

	case WM_ERASEBKGND:
		return 1;

	case WM_SIZE:
		LayoutPane(side);
		return 0;

	case WM_SETCURSOR:
		{
			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(hwnd, &pt);
			if (PtInRect(&p.splitter, pt) || (dragging == side && draggroup < 0))
			{
				SetCursor(LoadCursor(0, side < top ? IDC_SIZEWE : IDC_SIZENS));
				return TRUE;
			}
			for (size_t g = 0; g < p.groups.size(); g++)
				if (PtInRect(&p.groups[g].splitter, pt) || (dragging == side && draggroup == int(g)))
				{
					SetCursor(LoadCursor(0, Vertical(side) ? IDC_SIZENS : IDC_SIZEWE));
					return TRUE;
				}
		}
		break;

	case WM_LBUTTONDOWN:
		{
			POINT pt = { (short) LOWORD(lp), (short) HIWORD(lp) };
			if (PtInRect(&p.splitter, pt))
			{
				dragging = side, draggroup = -1;
				SetCapture(hwnd);
				return 0;
			}
			for (size_t g = 0; g < p.groups.size(); g++)
			{
				if (PtInRect(&p.groups[g].splitter, pt))
				{
					dragging = side, draggroup = int(g);
					SetCapture(hwnd);
					return 0;
				}
				if (PtInRect(&p.groups[g].strip, pt))
				{
					ClickStrip(side, int(g), pt);
					return 0;
				}
				int ti = TabHit(side, int(g), pt);
				if (ti >= 0)
				{
					// タブを選ぶ。押したまま動かすとドラッグ
					Select(side, int(g), ti);
					tabdownside = side, tabdowngroup = int(g), tabdownindex = ti;
					tabdownpt = pt;
					ClientToScreen(hwnd, &tabdownpt);
					SetCapture(hwnd);
					return 0;
				}
			}
		}
		return 0;

	case WM_MOUSEMOVE:
		if (tabdragging)
		{
			if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
			{
				ReleaseCapture();		// WM_CAPTURECHANGED でやめる
				return 0;
			}
			POINT pt;
			GetCursorPos(&pt);
			droptarget = FindDropTarget(pt);
			ShowDropIndicator(droptarget.kind != drop_none ? &droptarget.rect : 0);
			SetCursor(LoadCursor(0, droptarget.kind != drop_none ? IDC_ARROW : IDC_NO));
			return 0;
		}
		if (tabdownside == side && (wp & MK_LBUTTON))
		{
			POINT pt;
			GetCursorPos(&pt);
			if (abs(pt.x - tabdownpt.x) > GetSystemMetrics(SM_CXDRAG) || abs(pt.y - tabdownpt.y) > GetSystemMetrics(SM_CYDRAG))
			{
				// ドラッグを始める
				dragwnd = 0;
				if (tabdowngroup < int(p.groups.size()) && tabdownindex < int(p.groups[tabdowngroup].items.size()))
					dragwnd = p.groups[tabdowngroup].items[tabdownindex].hwnd;
				tabdownside = -1;
				if (dragwnd)
				{
					tabdragging = true;
					droptarget.kind = drop_none;
				}
				return 0;
			}
		}
		if (dragging == side)
		{
			int dpi = Dpi::GetForWindow(hwndmain);
			if (draggroup < 0)
			{
				// ペインの幅
				POINT pt;
				GetCursorPos(&pt);
				ScreenToClient(hwndmain, &pt);
				RECT c;
				GetClientRect(hwndmain, &c);
				HWND sb = statusdisplay.GetHWnd();
				if (sb && IsWindowVisible(sb))
					c.bottom -= statusdisplay.GetHeight();
				int v = 0;
				switch (side)
				{
				case left:		v = pt.x - c.left; break;
				case right:		v = c.right - pt.x; break;
				case top:		v = pt.y - c.top; break;
				case bottom:	v = c.bottom - pt.y; break;
				}
				p.size = Max(80, MulDiv(v, 96, dpi));		// 96 DPI 換算で持つ
				Layout();
			}
			else if (draggroup + 1 < int(p.groups.size()))
			{
				// 区画の間: 前後の区画の大きさを分け直す
				Group& a = p.groups[draggroup];
				Group& b = p.groups[draggroup + 1];
				POINT pt = { (short) LOWORD(lp), (short) HIWORD(lp) };
				bool vert = Vertical(side);
				int start = vert ? a.area.top : a.area.left;
				int span = vert ? b.area.bottom - a.area.top : b.area.right - a.area.left;
				int sp = Scale(5);
				int minl = Scale(40);
				int la = Limit((vert ? pt.y : pt.x) - start, span - sp - minl, minl);
				int lb = span - sp - la;
				// 区画の割合は実際の大きさに合わせて持つ (96 DPI 換算)
				for (size_t g = 0; g < p.groups.size(); g++)
				{
					Group& gr = p.groups[g];
					gr.weight = MulDiv(vert ? gr.area.bottom - gr.area.top : gr.area.right - gr.area.left, 96, dpi);
				}
				a.weight = Max(1, MulDiv(la, 96, dpi));
				b.weight = Max(1, MulDiv(lb, 96, dpi));
				LayoutPane(side);
			}
		}
		return 0;

	case WM_LBUTTONUP:
		if (tabdragging)
		{
			DropTarget t = droptarget;
			ReleaseCapture();			// WM_CAPTURECHANGED で後始末 (droptarget は消える)
			droptarget = t;
			PostMessage(hwnd, WM_APP + 1, 0, 0);
			return 0;
		}
		if (tabdownside >= 0)
		{
			tabdownside = -1;
			ReleaseCapture();
			return 0;
		}
		if (dragging == side)
		{
			dragging = -1, draggroup = -1;
			ReleaseCapture();
		}
		return 0;

	case WM_RBUTTONDOWN:
		if (tabdragging)
			ReleaseCapture();
		return 0;

	case WM_RBUTTONUP:
		{
			POINT pt = { (short) LOWORD(lp), (short) HIWORD(lp) };
			for (size_t g = 0; g < p.groups.size(); g++)
			{
				int ti = TabHit(side, int(g), pt);
				if (ti >= 0)
				{
					TabMenu(side, int(g), ti);
					return 0;
				}
			}
		}
		return 0;

	case WM_CAPTURECHANGED:
		if (tabdragging && (HWND) lp != hwnd)
		{
			tabdragging = false;
			droptarget.kind = drop_none;
			ShowDropIndicator(0);
		}
		tabdownside = -1;
		break;

	case WM_APP + 1:		// タブのドラッグを落とす
		DoDrop();
		return 0;

	}
	return DefWindowProc(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
//	保存・読み込み (M88.ini の [Dock])
//
void DockHost::SaveLayout()
{
	const char* ini = GetM88IniPath();
	char key[24], val[96];
	for (int s = 0; s < nsides; s++)
	{
		Pane& p = panes[s];
		wsprintf(key, "Size%d", s);
		wsprintf(val, "%d", p.size);
		WritePrivateProfileString("Dock", key, val, ini);
		wsprintf(key, "Shown%d", s);
		WritePrivateProfileString("Dock", key, p.shown ? "1" : "0", ini);
		// 区画の大きさ
		wsprintf(key, "Groups%d", s);
		wsprintf(val, "%d", int(p.groups.size()));
		WritePrivateProfileString("Dock", key, val, ini);
		for (size_t g = 0; g < p.groups.size(); g++)
		{
			wsprintf(key, "Group%d_%d", s, int(g));
			wsprintf(val, "%d", p.groups[g].weight);
			WritePrivateProfileString("Dock", key, val, ini);
		}
	}
	// 窓: 入れている順 (ペイン・区画ごと)。閉じていても前にあったペイン・区画は覚えておく
	int n = 0;
	std::map<std::string, Where> rest = lastplace;
	for (int s = 0; s < nsides; s++)
		for (size_t g = 0; g < panes[s].groups.size(); g++)
			for (size_t i = 0; i < panes[s].groups[g].items.size(); i++)
			{
				const std::string& sec = panes[s].groups[g].items[i].section;
				wsprintf(key, "W%d", n++);
				wsprintf(val, "%s,%d,1,%d", sec.c_str(), s, int(g));
				WritePrivateProfileString("Dock", key, val, ini);
				rest.erase(sec);
			}
	for (std::map<std::string, Where>::iterator i = rest.begin(); i != rest.end(); ++i)
	{
		wsprintf(key, "W%d", n++);
		wsprintf(val, "%s,%d,0,%d", i->first.c_str(), i->second.side, i->second.group);
		WritePrivateProfileString("Dock", key, val, ini);
	}
	wsprintf(val, "%d", n);
	WritePrivateProfileString("Dock", "Count", val, ini);
}

void DockHost::LoadLayout()
{
	const char* ini = GetM88IniPath();
	char key[24], val[96];
	for (int s = 0; s < nsides; s++)
	{
		wsprintf(key, "Size%d", s);
		int v = GetPrivateProfileInt("Dock", key, 0, ini);
		if (v > 0)
			panes[s].size = v;
		wsprintf(key, "Shown%d", s);
		panes[s].shown = GetPrivateProfileInt("Dock", key, 1, ini) != 0;
		wsprintf(key, "Groups%d", s);
		int ng = GetPrivateProfileInt("Dock", key, 0, ini);
		loadweights[s].clear();
		for (int g = 0; g < ng; g++)
		{
			wsprintf(key, "Group%d_%d", s, g);
			loadweights[s].push_back(GetPrivateProfileInt("Dock", key, 0, ini));
		}
	}
	bool shown[nsides];
	for (int s = 0; s < nsides; s++)
		shown[s] = panes[s].shown;
	int n = GetPrivateProfileInt("Dock", "Count", 0, ini);
	for (int i = 0; i < n; i++)
	{
		wsprintf(key, "W%d", i);
		if (!GetPrivateProfileString("Dock", key, "", val, sizeof(val), ini))
			continue;
		// 節の名前,辺,開いているか,区画
		char* c1 = strchr(val, ',');
		if (!c1)
			continue;
		*c1 = 0;
		int side = atoi(c1 + 1);
		char* c2 = strchr(c1 + 1, ',');
		bool open = c2 && atoi(c2 + 1) != 0;
		char* c3 = c2 ? strchr(c2 + 1, ',') : 0;
		int group = c3 ? atoi(c3 + 1) : 0;
		if (side < 0 || side >= nsides)
			continue;
		Where w = { side, group };
		lastplace[val] = w;
		if (!open || !enabled)
			continue;
		DockClient* mon = DockClient::FindBySection(val);
		if (!mon)
			continue;
		if (!mon->DockIsOpen())
			mon->DockOpen(hinst, hwndmain);		// 開くと OnShown がペイン・区画に入れる
		else
			Dock(mon, side, group);
	}
	for (int s = 0; s < nsides; s++)
	{
		loadweights[s].clear();
		panes[s].shown = shown[s];		// 窓を入れると開くので、閉じていたペインは閉じ直す
	}
	Layout();
}

// ---------------------------------------------------------------------------
//	タブのドラッグ (M88gn): タブを押したまま動かすと、ほかの区画・新しい区画・ほかのペイン・
//	浮いた窓へ移せる。落とす先は半透明の四角で示す。Esc か右ボタンでやめる
//
// 落とす先: 区画のタブの上 = その区画に (その位置に)、区画の上下 (左右) の 1/4 = 区画を分ける、
// 真ん中 = その区画に、画面の端の 1/5 = そのペインに、本体の窓の外 = 浮かせる
DockHost::DropTarget DockHost::FindDropTarget(POINT pt)
{
	DropTarget t;
	t.kind = drop_none;
	t.side = t.group = t.index = -1;
	SetRectEmpty(&t.rect);
	int ss = -1, sg = -1, si = -1;
	FindItem(dragwnd, &ss, &sg, &si);

	RECT wr;
	GetWindowRect(hwndmain, &wr);
	if (!PtInRect(&wr, pt))
	{
		// 浮かせる: 浮いていたときの大きさで、マウスの所に
		DockClient* mon = DockClient::FindByWindow(dragwnd);
		RECT fr;
		SetRectEmpty(&fr);
		if (mon)
			mon->DockFloatRect(&fr);
		int w = fr.right - fr.left, h = fr.bottom - fr.top;
		if (w <= 0 || h <= 0)
			w = Scale(500), h = Scale(400);
		t.kind = drop_float;
		SetRect(&t.rect, pt.x - Scale(60), pt.y - Scale(12), pt.x - Scale(60) + w, pt.y - Scale(12) + h);
		return t;
	}

	for (int s = 0; s < nsides; s++)
	{
		Pane& p = panes[s];
		if (!PaneVisible(s))
			continue;
		for (size_t g = 0; g < p.groups.size(); g++)
		{
			Group& gr = p.groups[g];
			RECT a = gr.area;
			MapWindowPoints(p.hwnd, 0, (POINT*) &a, 2);
			if (!PtInRect(&a, pt))
				continue;
			RECT tr = gr.tabrect;
			MapWindowPoints(p.hwnd, 0, (POINT*) &tr, 2);
			if (PtInRect(&tr, pt))
			{
				// タブの上: その位置に入れる
				POINT cp = pt;
				ScreenToClient(p.hwnd, &cp);
				int i = TabHit(s, int(g), cp);
				t.kind = drop_join, t.side = s, t.group = int(g), t.index = i;
				t.rect = a;
				if (s == ss && int(g) == sg && (i == si || gr.items.size() == 1))
					t.kind = drop_none;		// 動かない
				return t;
			}
			bool vert = s == left || s == right;
			int len = vert ? a.bottom - a.top : a.right - a.left;
			int pos = vert ? pt.y - a.top : pt.x - a.left;
			t.side = s;
			if (pos < len / 4 || pos >= len - len / 4)
			{
				bool after = pos >= len / 2;
				t.kind = drop_newgroup, t.group = int(g) + (after ? 1 : 0);
				t.rect = a;
				if (vert)
					(after ? t.rect.top : t.rect.bottom) = a.top + len / 2;
				else
					(after ? t.rect.left : t.rect.right) = a.left + len / 2;
				if (s == ss && int(g) == sg && gr.items.size() == 1)
					t.kind = drop_none;		// 自分だけの区画を自分の隣に分けても同じ
				return t;
			}
			t.kind = drop_join, t.group = int(g), t.index = -1;
			t.rect = a;
			if (s == ss && int(g) == sg)
				t.kind = drop_none;
			return t;
		}
	}

	// 画面 (真ん中) の端: そのペインに
	RECT v = view;
	MapWindowPoints(hwndmain, 0, (POINT*) &v, 2);
	if (PtInRect(&v, pt))
	{
		int w = v.right - v.left, h = v.bottom - v.top;
		int fx = pt.x - v.left, fy = pt.y - v.top;
		int side = -1;
		int dl = fx, dr = w - fx, dt = fy, db = h - fy;
		int m = Min(Min(dl, dr), Min(dt, db));
		if (m == dl && dl < w / 5)		side = left;
		else if (m == dr && dr < w / 5)	side = right;
		else if (m == dt && dt < h / 5)	side = top;
		else if (m == db && db < h / 5)	side = bottom;
		if (side >= 0)
		{
			int ext = PaneVisible(side) ? 0 : DesiredExtent(side);
			t.kind = drop_pane, t.side = side;
			t.rect = v;
			if (PaneVisible(side))
			{
				// 開いているペインの区画の並びの最後に足す: ペインの所を示す
				t.rect = v;
				RECT pr;
				GetWindowRect(panes[side].hwnd, &pr);
				t.rect = pr;
			}
			else
			{
				switch (side)
				{
				case left:		t.rect.right = t.rect.left + Min(ext, w / 2); break;
				case right:		t.rect.left = t.rect.right - Min(ext, w / 2); break;
				case top:		t.rect.bottom = t.rect.top + Min(ext, h / 2); break;
				case bottom:	t.rect.top = t.rect.bottom - Min(ext, h / 2); break;
				}
			}
			return t;
		}
	}
	return t;
}

void DockHost::ShowDropIndicator(const RECT* r)
{
	if (!r)
	{
		if (hwnddrop)
			ShowWindow(hwnddrop, SW_HIDE);
		return;
	}
	if (!hwnddrop)
	{
		static bool registered = false;
		if (!registered)
		{
			WNDCLASS wc;
			memset(&wc, 0, sizeof(wc));
			wc.lpfnWndProc = DefWindowProc;
			wc.hInstance = hinst;
			wc.hbrBackground = GetSysColorBrush(COLOR_HIGHLIGHT);
			wc.lpszClassName = "M88gn DockDrop";
			RegisterClass(&wc);
			registered = true;
		}
		hwnddrop = CreateWindowEx(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
								  "M88gn DockDrop", "", WS_POPUP, 0, 0, 1, 1, hwndmain, 0, hinst, 0);
		if (!hwnddrop)
			return;
		SetLayeredWindowAttributes(hwnddrop, 0, 90, LWA_ALPHA);
	}
	SetWindowPos(hwnddrop, HWND_TOPMOST, r->left, r->top, r->right - r->left, r->bottom - r->top,
				 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void DockHost::DoDrop()
{
	DropTarget t = droptarget;
	droptarget.kind = drop_none;
	HWND h = dragwnd;
	dragwnd = 0;
	int ss, sg, si;
	if (!h || !FindItem(h, &ss, &sg, &si))
		return;
	DockClient* mon = DockClient::FindByWindow(h);
	switch (t.kind)
	{
	case drop_float:
		Float(h);
		SetWindowPos(h, 0, t.rect.left, t.rect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
		break;

	case drop_pane:
		if (mon)
			Dock(mon, t.side, PaneVisible(t.side) ? int(newgroup) : int(lastgroup));
		break;

	case drop_join:
	case drop_newgroup:
		{
			Item it = TakeItem(ss, sg, si);
			int g = t.group, idx = t.index;
			if (t.kind == drop_join && ss == t.side && sg == g && idx > si)
				idx--;			// 同じ区画の中: 外した分だけ前に
			if (t.kind == drop_newgroup)
				g = AddGroup(t.side, g), idx = -1;
			else if (ss == t.side && g >= int(panes[t.side].groups.size()))
				g = int(panes[t.side].groups.size()) - 1;
			PutItem(it, t.side, g, idx);
			RemoveEmptyGroups(ss);
			if (ss != t.side)
				RemoveEmptyGroups(t.side);
			panes[t.side].shown = true;
			Layout();
		}
		break;
	}
}

void DockHost::Repaint()
{
	for (int s = 0; s < nsides; s++)
		if (panes[s].hwnd)
			InvalidateRect(panes[s].hwnd, 0, FALSE);
}
