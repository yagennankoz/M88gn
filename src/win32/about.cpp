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
#include "dpi.h"

// ---------------------------------------------------------------------------
//	構築/消滅
//
M88About::M88About()
: bigicon(0)
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
//	アイコン (32x32 の点の絵) を整数倍に拡大する (M88gn)。
//	DPI 対応の窓では、Windows がアイコンを画面の DPI の大きさ (175% で 56 ピクセル) に引き伸ばすが、
//	半端な倍率では点の大きさがそろわないので、最も近い整数倍にし、ぼかさずに拡大する
//
static HICON ScaleIcon(HICON src, int scale)
{
	ICONINFO ii;
	if (!GetIconInfo(src, &ii))
		return 0;
	BITMAP bm;
	GetObject(ii.hbmMask, sizeof(bm), &bm);
	int w = bm.bmWidth, h = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
	int sw = w * scale, sh = h * scale;

	HDC screen = GetDC(0);
	HDC sdc = CreateCompatibleDC(screen);
	HDC ddc = CreateCompatibleDC(screen);
	HBITMAP color = CreateCompatibleBitmap(screen, sw, sh);
	HBITMAP mask = CreateBitmap(sw, sh, 1, 1, 0);
	HICON icon = 0;
	if (color && mask && ii.hbmColor)
	{
		HGDIOBJ sold = SelectObject(sdc, ii.hbmColor);
		HGDIOBJ dold = SelectObject(ddc, color);
		SetStretchBltMode(ddc, COLORONCOLOR);
		StretchBlt(ddc, 0, 0, sw, sh, sdc, 0, 0, w, h, SRCCOPY);
		SelectObject(sdc, ii.hbmMask);
		SelectObject(ddc, mask);
		SetStretchBltMode(ddc, COLORONCOLOR);
		StretchBlt(ddc, 0, 0, sw, sh, sdc, 0, 0, w, h, SRCCOPY);
		SelectObject(sdc, sold);
		SelectObject(ddc, dold);

		ICONINFO ni = { TRUE, 0, 0, mask, color };
		icon = CreateIconIndirect(&ni);
	}
	if (color)
		DeleteObject(color);
	if (mask)
		DeleteObject(mask);
	DeleteDC(sdc);
	DeleteDC(ddc);
	ReleaseDC(0, screen);
	if (ii.hbmColor)
		DeleteObject(ii.hbmColor);
	DeleteObject(ii.hbmMask);
	return icon;
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

		// アイコンを画面の DPI に合わせて整数倍にする (M88gn。150% 以上で 2 倍)
		{
			int scale = (Dpi::GetForWindow(hdlg) + 48) / 96;
			HWND hicon = GetDlgItem(hdlg, -1);		// .rc の ICON (IDC_STATIC)
			if (scale > 1 && hicon)
			{
				// LoadIcon は画面の DPI の大きさ (175% で 56) に引き伸ばしたものを返すので、元の 32x32 で読む
				HICON src = (HICON) LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDI_ICON_M88), IMAGE_ICON, 32, 32, 0);
				bigicon = src ? ScaleIcon(src, scale) : 0;
				if (src)
					DestroyIcon(src);
				if (bigicon)
					SendMessage(hicon, STM_SETICON, (WPARAM) bigicon, 0);
			}
		}

		SetFocus(GetDlgItem(hdlg, IDOK));
		return false;

	case WM_DESTROY:
		if (bigicon)
		{
			DestroyIcon(bigicon);
			bigicon = 0;
		}
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
	"テープ (T88) まわりなどを改修したものです．"
	"デバッガと、エミュレーションの不具合の修正の一部は、蕎麦P 氏の M88am のものを取り込みました．\r\n"
	"\r\n"
	"主な改修: テープまわり、音 (テープ・ディスク・BEEP)、画面 (CRT 風のフィルター・全画面表示)、操作性 (ソフトウェアキーボード)、Z80 の不具合の修正\r\n"
	"\r\n"
	"https://github.com/yagennankoz/M88gn\r\n"
	"\r\n"
	"M88gn について、cisc 氏・rururutan 氏・GIMONS 氏・蕎麦P 氏に問い合わせないでください．\r\n"
	"\r\n"
	"rururutan 氏の M88 私家版\r\n"
	"https://github.com/rururutan/m88\r\n"
	"\r\n"
	"M88g・FDX68 (GIMONS DEVELOPER WORKS)\r\n"
	"http://www.retropc.net/gimons/\r\n"
	"\r\n"
	"M88am (蕎麦P 氏)\r\n"
	"https://www.ambientworks.jp/m88am/\r\n"
	"\r\n"
	"オリジナル M88 のページ\r\n"
	"http://www.retropc.net/cisc/m88/\r\n"
	"\r\n"
	"FM 音源ユニットの作成にあたっては，\r\n"
	"佐藤達之氏作の fm.c を参考にさせていただきました．\r\n"
	"\r\n"
	"N80/SR モードは arearea 氏のコードを元に実装されています．\r\n"
	;

