// ---------------------------------------------------------------------------
//  M88 - PC-8801 emulator
//	Copyright (C) cisc 1998, 2000.
// ---------------------------------------------------------------------------
//	About Dialog Box for M88
// ---------------------------------------------------------------------------
//	$Id: about.cpp,v 1.28 2003/09/28 14:35:35 cisc Exp $

#include "headers.h"
#include "resource.h"
#include "about.h"
#include "version.h"
#include "filetest.h"

// ---------------------------------------------------------------------------
//	構築/消滅
//
M88About::M88About()
{
	SanityCheck(&crc);
}

// ---------------------------------------------------------------------------
//	ダイアログ表示
//
void M88About::Show(HINSTANCE hinst, HWND hwndparent)
{
	DialogBoxParam(hinst, MAKEINTRESOURCE(IDD_ABOUT), 
				   hwndparent, DLGPROC((void*)DlgProcGate), (LPARAM)this); 
}

// ---------------------------------------------------------------------------
//	ダイアログ処理
//
INT_PTR M88About::DlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
	char buf[4096];
	
	switch (msg)
	{
	case WM_INITDIALOG:
		wsprintf(buf, "M88gn for Win32 ver " M88GN_VER_STRING "\n"
					  "(based on M88 rel " APP_VER_STRING ")\n"
					  "PC-8801 series emulator.\n"
					  "Copyright (C) 1998, 2003 cisc\n");
	
		SetDlgItemText(hdlg, IDC_ABOUT_TEXT, buf);

		wsprintf(buf, abouttext, crc);

		SetDlgItemText(hdlg, IDC_ABOUT_BOX, buf);

		SetFocus(GetDlgItem(hdlg, IDOK));
		return false;

	case WM_COMMAND:
		if (IDOK == LOWORD(wp))
		{
			EndDialog(hdlg, true);
			break;
		}
		return true;

	case WM_CLOSE:
		EndDialog(hdlg, false);
		return true;

	default:
		return false;
	}
	return false;
}

INT_PTR CALLBACK M88About::DlgProcGate
(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
	M88About* about = 0;
	if ( m == WM_INITDIALOG ) {
		about = reinterpret_cast<M88About*>(l);
		if (about) {
			::SetWindowLongPtr( hwnd, GWLP_USERDATA, (LONG_PTR)about );
		}
	} else {
		about = (M88About*)::GetWindowLongPtr( hwnd, GWLP_USERDATA );
	}

	if (about) {
		return about->DlgProc(hwnd, m, w, l);
	} else {
		return FALSE;
	}
}

// ---------------------------------------------------------------------------
//	about 用のテキスト
//
const char M88About::abouttext[] =
	"build date:" __DATE__ " (%.8x)\r\n"
	"\r\n"
	"M88gn は、rururutan 氏の M88 私家版に GIMONS 氏の M88g の差分を当てたものをもとに、"
	"テープ (T88) まわりなどを改修したものです．\r\n"
	"\r\n"
	"主な改修: テープまわり、音 (テープ・ディスク・BEEP)、操作性 (ソフトウェアキーボード・全画面表示)\r\n"
	"\r\n"
	"https://github.com/yagennankoz/M88gn\r\n"
	"\r\n"
	"M88gn について、cisc 氏・rururutan 氏・GIMONS 氏に問い合わせないでください．\r\n"
	"\r\n"
	"rururutan 氏の M88 私家版\r\n"
	"https://github.com/rururutan/m88\r\n"
	"\r\n"
	"M88g・FDX68 (GIMONS DEVELOPER WORKS)\r\n"
	"http://www.retropc.net/gimons/\r\n"
	"\r\n"
	"オリジナル M88 のページ\r\n"
	"http://www.retropc.net/cisc/m88/\r\n"
	"\r\n"
	"FM 音源ユニットの作成にあたっては，\r\n"
	"佐藤達之氏作の fm.c を参考にさせていただきました．\r\n"
	"\r\n"
	"N80/SR モードは arearea 氏のコードを元に実装されています．\r\n"
	;

