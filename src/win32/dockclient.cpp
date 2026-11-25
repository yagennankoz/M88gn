// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ペインに入れられる窓 (DockHost が扱う窓の共通の部分)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#include "headers.h"
#include "dockclient.h"
#include "uitheme.h"

// タイトルバーのメニューに足す項目 (下の 4 ビットは Windows が使うので 0)
enum { sc_dockleft = 0x1040, sc_dockright = 0x1050, sc_docktop = 0x1060, sc_dockbottom = 0x1070,
	   sc_newleft = 0x1080, sc_newright = 0x1090, sc_newtop = 0x10a0, sc_newbottom = 0x10b0 };

void (*DockClient::dockhook)(DockClient* c, int side) = 0;
void (*DockClient::showhook)(DockClient* c) = 0;
DockClient* DockClient::clients[DockClient::maxclients];

DockClient::DockClient()
: docked(false)
{
	for (int i = 0; i < maxclients; i++)
	{
		if (!clients[i])
		{
			clients[i] = this;
			break;
		}
	}
}

DockClient::~DockClient()
{
	for (int i = 0; i < maxclients; i++)
		if (clients[i] == this)
			clients[i] = 0;
}

DockClient* DockClient::FindBySection(const char* section)
{
	for (int i = 0; i < maxclients; i++)
	{
		const char* s = clients[i] ? clients[i]->DockSection() : 0;
		if (s && !lstrcmpi(s, section))
			return clients[i];
	}
	return 0;
}

DockClient* DockClient::FindByWindow(HWND h)
{
	if (!h)
		return 0;
	for (int i = 0; i < maxclients; i++)
		if (clients[i] && clients[i]->DockWindow() == h)
			return clients[i];
	return 0;
}

void DockClient::ApplyThemeAll()
{
	for (int i = 0; i < maxclients; i++)
		if (clients[i] && clients[i]->DockWindow())
			Theme::ApplyScrollBars(clients[i]->DockWindow());
}

void DockClient::AddDockMenu(HMENU hsys)
{
	if (!hsys || !dockhook)
		return;
	AppendMenu(hsys, MF_SEPARATOR, 0, 0);
	AppendMenu(hsys, MF_STRING, sc_dockleft, "左のペインにドック");
	AppendMenu(hsys, MF_STRING, sc_dockright, "右のペインにドック");
	AppendMenu(hsys, MF_STRING, sc_docktop, "上のペインにドック");
	AppendMenu(hsys, MF_STRING, sc_dockbottom, "下のペインにドック");
	HMENU hnew = CreatePopupMenu();		// 区画を分けて入れる
	AppendMenu(hnew, MF_STRING, sc_newleft, "左のペイン");
	AppendMenu(hnew, MF_STRING, sc_newright, "右のペイン");
	AppendMenu(hnew, MF_STRING, sc_newtop, "上のペイン");
	AppendMenu(hnew, MF_STRING, sc_newbottom, "下のペイン");
	AppendMenu(hsys, MF_POPUP, (UINT_PTR) hnew, "新しい区画にドック");
}

bool DockClient::DockSysCommand(WPARAM wp)
{
	int side;
	switch (wp & 0xfff0)
	{
	case sc_dockleft:	side = 0; break;
	case sc_dockright:	side = 1; break;
	case sc_docktop:	side = 2; break;
	case sc_dockbottom:	side = 3; break;
	case sc_newleft:	side = 4; break;		// 4～7: 新しい区画
	case sc_newright:	side = 5; break;
	case sc_newtop:		side = 6; break;
	case sc_newbottom:	side = 7; break;
	default:
		return false;
	}
	if (dockhook)
		dockhook(this, side);
	return true;
}
