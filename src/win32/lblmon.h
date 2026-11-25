// ---------------------------------------------------------------------------
//	M88gn - PC-8801 emulator
//	ラベルの一覧の窓
//	Copyright (C) 2026
//	2-clause BSD license
// ---------------------------------------------------------------------------

#pragma once

#include "winmon.h"
#include "wincore.h"
#include <vector>
#include <string>

// ---------------------------------------------------------------------------

namespace PC8801
{

class CodeMonitor;
class MemoryMonitor;

class LabelMonitor : public WinMonitor
{
public:
	LabelMonitor();
	~LabelMonitor();

	bool Init(WinCore*);
	// 番地へ移る先と、ラベルを変えたときに書き直す窓 (ui が渡す)
	void SetMonitors(CodeMonitor* c, MemoryMonitor* m) { codemon = c; memmon = m; }

private:
	struct Entry
	{
		int cpu;
		uint addr;
		int bank;			// MemoryViewer::Type か Debugger::label_anybank
		std::string name;
		int Key() const { return cpu << 24 | bank << 16 | int(addr); }
	};

	void UpdateText();
	void Start();
	BOOL DlgProc(HWND, UINT, WPARAM, LPARAM);
	void LoadMonitorConfig();
	void SaveMonitorConfig();
	bool CopyToClipboard();

	void BuildList();				// Debugger のラベルを並び順に並べる
	int  RowAt(POINT p);			// 文字の位置の行のラベル (entries の番号)。無ければ -1
	int  SelectedIndex();			// 選んでいるラベルの entries の番号。無ければ -1
	void ShowRowMenu(HWND hdlg, int index);
	void AddNew();					// Add (M88gn)
	void EditSelected();
	void RemoveSelected();
	void ShowInCode(const Entry& e);
	void ShowInMemory(const Entry& e);
	void Changed();					// ラベルを変えたら、ほかの窓も書き直す

	WinCore* core;
	CodeMonitor* codemon;
	MemoryMonitor* memmon;
	std::vector<Entry> entries;
	bool byname;					// false = 番地順、true = 名前順。M88.ini に保存する
	int selkey;						// 選んでいるラベル (Entry::Key、-1 なら無し)
};

}
