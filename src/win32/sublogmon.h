// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	サブシステムとの通信のログのウインドウ
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#pragma once

#include "winmon.h"
#include "wincore.h"
#include "pc88/debugger.h"

// ---------------------------------------------------------------------------

namespace PC8801
{

class SubLogMonitor : public WinMonitor
{
public:
	SubLogMonitor();
	~SubLogMonitor();

	bool Init(WinCore*);

private:
	void UpdateText();
	void Start();
	BOOL DlgProc(HWND, UINT, WPARAM, LPARAM);

	void LoadMonitorConfig();
	void SaveMonitorConfig();

	// 1 件の説明 (FF への OUT はポート C のビットの操作として読む)
	static void Describe(const Debugger::SubLogEntry& e, char* buf);
	bool BuildText(TextBuffer& tb);
	bool CopyToClipboard();
	bool SaveToFile();

	WinCore* core;
	bool started;			// 一度開いたら、ログを取り始める
	int lastcount;			// 前に描いたときの件数 (増えていなければ描き直さない)
};

}
