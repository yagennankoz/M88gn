// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	文字列か 1～16 バイトの並びの入力 (メモリの窓の検索と、パターンのブレークの窓で使う)
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#include "headers.h"
#include "resource.h"
#include "patinput.h"

using namespace PC8801;

PatternInput::PatternInput()
: hex(false), mcase(false), bytes(4), len(0)
{
	text[0] = 0;
	memset(box, 0, sizeof(box));
	memset(pat, 0, sizeof(pat));
}

void PatternInput::Set(const uint8* p, int n, bool h, bool c)
{
	n = n < 1 ? 1 : n > textmax ? textmax : n;
	hex = h || n > hexmax;
	mcase = c;
	memcpy(pat, p, n);
	len = n;
	memset(box, 0, sizeof(box));
	text[0] = 0;
	if (hex && n <= hexmax)
	{
		bytes = n;
		for (int i = 0; i < n; i++)
			wsprintf(box[i], "%.2X", p[i]);
	}
	else
	{
		hex = false;
		memcpy(text, p, n);
		text[n] = 0;
	}
}

// 16 進の欄は、バイト数より後ろを灰色にする
void PatternInput::EnableBoxes(HWND hdlg)
{
	int n = int(SendDlgItemMessage(hdlg, IDC_SEARCH_BYTES, CB_GETCURSEL, 0, 0)) + 1;
	if (n < 1 || n > hexmax)
		n = bytes;
	for (int i = 0; i < hexmax; i++)
		EnableWindow(GetDlgItem(hdlg, IDC_SEARCH_B0 + i), i < n);
}

void PatternInput::InitDialog(HWND hdlg, bool focus)
{
	SendDlgItemMessage(hdlg, IDC_SEARCH_STR, EM_LIMITTEXT, textmax, 0);
	SetDlgItemText(hdlg, IDC_SEARCH_STR, text);
	CheckDlgButton(hdlg, IDC_SEARCH_CASE, mcase ? BST_CHECKED : BST_UNCHECKED);
	CheckRadioButton(hdlg, IDC_SEARCH_TEXT, IDC_SEARCH_HEX, hex ? IDC_SEARCH_HEX : IDC_SEARCH_TEXT);
	for (int i = 1; i <= hexmax; i++)
	{
		char buf[4];
		wsprintf(buf, "%d", i);
		SendDlgItemMessage(hdlg, IDC_SEARCH_BYTES, CB_ADDSTRING, 0, (LPARAM) buf);
	}
	SendDlgItemMessage(hdlg, IDC_SEARCH_BYTES, CB_SETCURSEL, bytes - 1, 0);
	for (int i = 0; i < hexmax; i++)
	{
		SendDlgItemMessage(hdlg, IDC_SEARCH_B0 + i, EM_LIMITTEXT, 2, 0);
		SetDlgItemText(hdlg, IDC_SEARCH_B0 + i, box[i]);
	}
	EnableBoxes(hdlg);
	if (focus)
	{
		int f = hex ? IDC_SEARCH_B0 : IDC_SEARCH_STR;
		SetFocus(GetDlgItem(hdlg, f));
		SendDlgItemMessage(hdlg, f, EM_SETSEL, 0, -1);
	}
}

bool PatternInput::Command(HWND hdlg, int id, int code)
{
	bool isbox = id >= IDC_SEARCH_B0 && id < IDC_SEARCH_B0 + hexmax;
	// 欄にフォーカスを移すと、その方式を選ぶ
	if ((code == EN_SETFOCUS && (id == IDC_SEARCH_STR || isbox))
		|| (id == IDC_SEARCH_BYTES && code == CBN_SETFOCUS))
	{
		bool h = id != IDC_SEARCH_STR;
		CheckRadioButton(hdlg, IDC_SEARCH_TEXT, IDC_SEARCH_HEX, h ? IDC_SEARCH_HEX : IDC_SEARCH_TEXT);
		if (code == EN_SETFOCUS)
			SendDlgItemMessage(hdlg, id, EM_SETSEL, 0, -1);
		return true;
	}
	// Text と Hex は別のグループ (間に欄がある) なので、押したら自分で片方を外す
	if ((id == IDC_SEARCH_TEXT || id == IDC_SEARCH_HEX) && code == BN_CLICKED)
	{
		CheckRadioButton(hdlg, IDC_SEARCH_TEXT, IDC_SEARCH_HEX, id);
		return true;
	}
	if (id == IDC_SEARCH_CASE && code == BN_CLICKED)
	{
		CheckRadioButton(hdlg, IDC_SEARCH_TEXT, IDC_SEARCH_HEX, IDC_SEARCH_TEXT);
		return true;
	}
	if (id == IDC_SEARCH_BYTES && code == CBN_SELCHANGE)
	{
		EnableBoxes(hdlg);
		return true;
	}
	// 16 進の欄は 2 桁入れたら次の欄へ
	if (isbox && code == EN_CHANGE)
	{
		HWND h = GetDlgItem(hdlg, id);
		if (GetFocus() == h && GetWindowTextLength(h) == 2)
		{
			HWND next = GetDlgItem(hdlg, id + 1);
			if (id + 1 < IDC_SEARCH_B0 + hexmax && IsWindowEnabled(next))
				SetFocus(next);
		}
		return true;
	}
	return false;
}

bool PatternInput::Read(HWND hdlg, const char* title)
{
	bool h = IsDlgButtonChecked(hdlg, IDC_SEARCH_HEX) == BST_CHECKED;
	bool c = IsDlgButtonChecked(hdlg, IDC_SEARCH_CASE) == BST_CHECKED;
	char t[textmax + 1];
	GetDlgItemText(hdlg, IDC_SEARCH_STR, t, sizeof(t));
	int n = int(SendDlgItemMessage(hdlg, IDC_SEARCH_BYTES, CB_GETCURSEL, 0, 0)) + 1;
	if (n < 1 || n > hexmax)
		n = bytes;
	char b[hexmax][3];
	for (int i = 0; i < hexmax; i++)
		GetDlgItemText(hdlg, IDC_SEARCH_B0 + i, b[i], 3);

	uint8 p[textmax];
	int l = 0;
	if (h)
	{
		for (int i = 0; i < n; i++)
		{
			const char* s = b[i];
			bool ok = s[0] && isxdigit((uint8) s[0]) && (!s[1] || isxdigit((uint8) s[1]));
			if (!ok)
			{
				char msg[64];
				wsprintf(msg, "%d バイト目を 16 進の 1～2 桁で入れてください。", i + 1);
				MessageBox(hdlg, msg, title, MB_OK | MB_ICONWARNING);
				SetFocus(GetDlgItem(hdlg, IDC_SEARCH_B0 + i));
				SendDlgItemMessage(hdlg, IDC_SEARCH_B0 + i, EM_SETSEL, 0, -1);
				return false;
			}
			p[l++] = uint8(strtoul(s, 0, 16));
		}
	}
	else
	{
		l = int(strlen(t));
		if (l == 0)
		{
			MessageBox(hdlg, "文字列を入れてください。", title, MB_OK | MB_ICONWARNING);
			SetFocus(GetDlgItem(hdlg, IDC_SEARCH_STR));
			return false;
		}
		memcpy(p, t, l);		// 半角カナは PC-8801 の文字コード (JIS X 0201) と同じ
	}

	hex = h;
	mcase = c;
	bytes = n;
	strcpy(text, t);
	memcpy(box, b, sizeof(box));
	memcpy(pat, p, l);
	len = l;
	return true;
}

void PatternInput::Describe(char* buf) const
{
	if (hex)
	{
		char* w = buf;
		*w = 0;
		for (int i = 0; i < len; i++)
			w += wsprintf(w, i ? " %.2X" : "%.2X", pat[i]);
	}
	else
		wsprintf(buf, "\"%s\"", text);
}
