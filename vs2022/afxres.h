// VS2022 用: MFC を入れずに M88.rc をビルドするための代わりのヘッダー
// (M88.rc は MFC の afxres.h を読むが、使っているのは Windows SDK の winres.h にあるものだけ)
#pragma once
#include <winres.h>
#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif
