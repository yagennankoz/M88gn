// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Copyright (C) cisc 1998, 1999.
// ---------------------------------------------------------------------------
//	uPD765A
// ---------------------------------------------------------------------------
//	$Id: fdc.cpp,v 1.20 2003/05/12 22:26:35 cisc Exp $

#include "headers.h"

#include "FDC.h"
#include "FDU.h"
#include "misc.h"
#include "critsect.h"
#include "diskmgr.h"
#include "status.h"
#include "config.h"

#define LOGNAME "fdc"
#include "diag.h"

using namespace PC8801;

// ---------------------------------------------------------------------------
//	構築/消滅
//
FDC::FDC(const ID& id)
	: Device(id)
{
	buffer = 0;
	timerhandle = 0;
	litdrive = 0;
	seektime = 0;
	for (int i = 0; i < num_drives; i++) {
		drive[i].cyrinder = 0;
		drive[i].result = 0;
		drive[i].hd = 0;
		drive[i].dd = 1;
	}
}

FDC::~FDC()
{
	delete[] buffer;
}

// ---------------------------------------------------------------------------
//	初期化
//
bool FDC::Init(DiskManager* dm, Scheduler* s, IOBus* b, int ip, int sp)
{
	diskmgr = dm;
	scheduler = s;
	bus = b;
	pintr = ip;
	pfdstat = sp;

	showstatus = 0;
	seekstate = 0;
	fdstat = 0;
	diskwait = true;

	if (!buffer) {
		buffer = new uint8[0x4000];
	}

	if (!buffer) {
		return false;
	}

	memset(buffer, 0, 0x4000);

	LOG0("FDC LOG\n");
	Reset();
	return true;
}

// ---------------------------------------------------------------------------
//	設定反映
//
void FDC::ApplyConfig(const Config* cfg)
{
	diskwait = !(cfg->flag2 & Config::fddnowait);
	showstatus = (cfg->flags & Config::showfdcstatus) != 0;
}

// ---------------------------------------------------------------------------
//	ドライブ制御
//	0f4h/out	b5  b4  b3  b2  b1  b0
//				CLK DSI TD1 TD0 RV1 RV0
//
//	RVx:	ドライブのモード
//			0: 2D/2DD
//			1: 2HD
//
//	TDx:	ドライブのトラック密度
//			0: 48TPI (2D)
//			1: 96TPI (2DD/2HD)
//
void IOCALL FDC::DriveControl(uint, uint data)
{
	int hdprev;
	LOG1("Drive control (%.2x) ", data);

	for (int d = 0; d < 2; d++) {
		hdprev = drive[d].hd;
		drive[d].hd = data & (1 << d) ? 0x80 : 0x00;
		drive[d].dd = data & (4 << d) ? 0 : 1;

		if (hdprev != drive[d].hd) {
			int bit = 4 << d;
			fdstat = (fdstat & ~bit) | (drive[d].hd ? bit : 0);
			statusdisplay.FDAccess(d, drive[d].hd != 0, false);
		}
		LOG2("<2%c%c>", drive[d].hd ? 'H' : 'D', drive[d].dd ? ' ' : 'D');
	}
	if (pfdstat >= 0) {
		bus->Out(pfdstat, fdstat);
	}
	LOG0(">\n");
}

// ---------------------------------------------------------------------------
//	FDC::Intr
//	割り込み発生
//
inline void FDC::Intr(bool i)
{
	bus->Out(pintr, i);
}

// ---------------------------------------------------------------------------
//	Reset
//
void IOCALL FDC::Reset(uint, uint)
{
	LOG0("Reset\n");
	ShiftToIdlePhase();
	int_requested = false;
	Intr(false);
	scheduler->DelEvent(this);
	DriveControl(0, 0);
	for (int d = 0; d < 2; d++) {
		statusdisplay.FDAccess(d, drive[d].hd != 0, false);
	}
	fdstat &= ~3;
	if (pfdstat) {
		bus->Out(pfdstat, fdstat);
	}
}

// ---------------------------------------------------------------------------
//	FDC::Status
//	ステータスレジスタ
//
//	MSB                         LSB
//	RQM DIO NDM CB  D3B D2B D1B D0B
//
//	CB  = idlephase 以外
//	NDM = E-Phase
//	DIO = 0 なら CPU->FDC (Put)  1 なら FDC->CPU (Get)
//	RQM = データの送信・受信の用意ができた
//
uint IOCALL FDC::Status(uint)
{
	return seekstate | status;
}

// ---------------------------------------------------------------------------
//	FDC::SetData
//	CPU から FDC にデータを送る
//
void IOCALL FDC::SetData(uint, uint d)
{
	// 受け取れる状況かチェック
	if ((status & (S_RQM | S_DIO)) == S_RQM) {
		data = d;
		status &= ~S_RQM;
		Intr(false);

		switch (phase)
		{
			// コマンドを受け取る
			case idlephase:
				LOG1("\n[%.2x] ", data);
				command = data;
				(this->*CommandTable[command & 31])();
				break;

				// コマンドのパラメータを受け取る
			case commandphase:
				*bufptr++ = data;
				if (--count) {
					status |= S_RQM;
				} else {
					(this->*CommandTable[command & 31])();
				}
				break;

				// E-Phase (転送中)
			case execwritephase:
				*bufptr++ = data;
				if (--count) {
					if (!diskwait) {
						status |= S_RQM;
						Intr(true);
					}
				} else {
					status &= ~S_NDM;
					(this->*CommandTable[command & 31]) ();
				}
				break;

				// E-Phase (比較中)
			case execscanphase:
				if (data != 0xff) {
					if (((command & 31) == 0x11 && *bufptr != data)
						|| ((command & 31) == 0x19 && *bufptr > data)
						|| ((command & 31) == 0x1d && *bufptr < data)) {
						// 条件に合わない
						result &= ~ST2_SH;
					}
				}
				bufptr++;

				if (--count) {
					if (!diskwait) {
						status |= S_RQM;
						Intr(true);
					}
				} else {
					status &= ~S_NDM;
					CmdScanEqual();
				}
				break;
		}
	}
}


// ---------------------------------------------------------------------------
//	FDC::GetData
//	FDC -> CPU
//
uint IOCALL FDC::GetData(uint)
{
	if ((status & (S_RQM | S_DIO)) == (S_RQM | S_DIO)) {
		Intr(false);
		status &= ~S_RQM;

		switch (phase)
		{
			// リゾルト・フェイズ
			case resultphase:
				data = *bufptr++;
				LOG1(" %.2x", data);
				if (--count) {
					status |= S_RQM;
				} else {
					LOG0(" }\n");
					ShiftToIdlePhase();
				}
				break;

				// E-Phase(転送中)
			case execreadphase:
				data = *bufptr++;
				if (--count) {
					if (!diskwait) {
						status |= S_RQM, Intr(true);
					}
				} else {
					status &= ~S_NDM;
					(this->*CommandTable[command & 31]) ();
				}
				break;
		}
	}
	return data;
}

// ---------------------------------------------------------------------------
//	TC (転送終了)
//
uint IOCALL FDC::TC(uint)
{
	if (accepttc) {
		LOG0(" <TC>");
		prevphase = phase;
		phase = tcphase;
		accepttc = false;
		(this->*CommandTable[command & 31]) ();
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	I-PHASE (コマンド待ち)
//
void FDC::ShiftToIdlePhase()
{
	phase = idlephase;
	status = S_RQM;
	accepttc = false;
	statusdisplay.FDAccess(litdrive, drive[litdrive].hd != 0, false);

	fdstat &= ~(1 << litdrive);
	if (pfdstat >= 0) bus->Out(pfdstat, fdstat);

	LOG1("FD %d Off\n", litdrive);
}

// ---------------------------------------------------------------------------
//	C-PHASE (パラメータ待ち)
//
void FDC::ShiftToCommandPhase(int nbytes)
{
	phase = commandphase;
	status = S_RQM | S_CB;
	accepttc = false;

	bufptr = buffer, count = nbytes;
	litdrive = hdu & 1;
	LOG4("(MT=%d, MF=%d, SK=%d) FD=%d ON\n",
		(command >> 7) & 1, (command >> 6) & 1, (command >> 5) & 1,
		litdrive);
	statusdisplay.FDAccess(litdrive, drive[litdrive].hd != 0, true);

	fdstat |= 1 << litdrive;
	if (pfdstat >= 0) bus->Out(pfdstat, fdstat);
}

// ---------------------------------------------------------------------------
//	E-PHASE
//
void FDC::ShiftToExecutePhase()
{
	phase = executephase;
	(this->*CommandTable[command & 31]) ();
}

// ---------------------------------------------------------------------------
//	E-PHASE (FDC->CPU)
//
void FDC::ShiftToExecReadPhase(int nbytes, bool intr, uint8* data)
{
	phase = execreadphase;
	status = S_DIO | S_NDM | S_CB;
	accepttc = true;

	bufptr = data ? data : buffer;
	count = nbytes;

	if (intr) {
		status |= S_RQM;
		Intr(true);
	}
}

// ---------------------------------------------------------------------------
//	E-PHASE (CPU->FDC)
//
void FDC::ShiftToExecWritePhase(int nbytes, bool intr)
{
	phase = execwritephase;
	status = S_NDM | S_CB;
	accepttc = true;

	bufptr = buffer;
	count = nbytes;

	if (intr) {
		status |= S_RQM;
		Intr(true);
	}
}

// ---------------------------------------------------------------------------
//	E-PHASE (CPU->FDC, COMPARE)
//
void FDC::ShiftToExecScanPhase(int nbytes, bool intr)
{
	phase = execscanphase;
	status = S_NDM | S_CB;
	accepttc = true;
	result = ST2_SH;

	bufptr = buffer;
	count = nbytes;

	if (intr) {
		status |= S_RQM;
		Intr(true);
	}
}

// ---------------------------------------------------------------------------
//	R-PHASE
//
void FDC::ShiftToResultPhase(int nbytes)
{
	phase = resultphase;
	status = S_RQM | S_CB | S_DIO;
	accepttc = false;

	bufptr = buffer, count = nbytes;
	LOG0("\t{");
}

// ---------------------------------------------------------------------------
//	R/W DATA 系 resultphase (ST0/ST1/ST2/C/H/R/N)

void FDC::ShiftToResultPhase7()
{
	buffer[0] = (result & 0xf8) | (hdue & 7);
	buffer[1] = uint8(result >> 8);
	buffer[2] = uint8(result >> 16);
	buffer[3] = idr.c;
	buffer[4] = idr.h;
	buffer[5] = idr.r;
	buffer[6] = idr.n;
	Intr(true);
	ShiftToResultPhase(7);
}

// ---------------------------------------------------------------------------
//	command や EOT を参考にレコード増加
//
bool FDC::IDIncrement()
{
	if ((command & 19) == 17) {
		// Scan*Equal
		if ((dtl & 0xff) == 0x02) {
			idr.r++;
		}
	}

	if (idr.r++ != eot) {
		return true;
	}

	idr.r = 1;
	if (command & 0x80) {
		hdu ^= 4;
		idr.h ^= 1;
		if (idr.h & 1) {
			return true;
		}
	}
	idr.c++;
	return false;
}


// ---------------------------------------------------------------------------
//	タイマー
//
void FDC::SetTimer(Phase p, int ticks)
{
	t_phase = p;
	if (!diskwait) {
		ticks = (ticks + 127) / 128;
	}
	timerhandle = scheduler->AddEvent
		(ticks, this, STATIC_CAST(TimeFunc, &FDC::PhaseTimer), p);
}

void FDC::DelTimer()
{
	if (timerhandle) {
		scheduler->DelEvent(timerhandle);
	}
	timerhandle = 0;
}

void IOCALL FDC::PhaseTimer(uint p)
{
	timerhandle = 0;
	phase = Phase(p);
	(this->*CommandTable[command & 31]) ();
}

// ---------------------------------------------------------------------------

const FDC::CommandFunc FDC::CommandTable[32] =
{
	&CmdInvalid, &CmdInvalid,				// 0
	&CmdReadDiagnostic, &CmdSpecify,
	&CmdSenceDeviceStatus, &CmdWriteData,	// 4
	&CmdReadData, &CmdRecalibrate,
	&CmdSenceIntStatus, &CmdWriteData,		// 8
	&CmdReadID, &CmdInvalid,
	&CmdReadData, &CmdWriteID,				// c
	&CmdInvalid, &CmdSeek,
	&CmdInvalid, &CmdScanEqual,				// 10
	&CmdInvalid, &CmdInvalid,
	&CmdInvalid, &CmdInvalid,				// 14
	&CmdInvalid, &CmdInvalid,
	&CmdInvalid, &CmdScanEqual,				// 18
	&CmdInvalid, &CmdInvalid,
	&CmdInvalid, &CmdScanEqual,				// 1c
	&CmdInvalid, &CmdInvalid,
};

// ---------------------------------------------------------------------------
//	ReadData
//
void FDC::CmdReadData()
{
	switch (phase)
	{
		case idlephase:
			LOG0((command & 31) == 12 ? "ReadDeletedData" : "ReadData ");
			ShiftToCommandPhase(8);		// パラメータは 8 個
			return;

		case commandphase:
			GetSectorParameters();

		case executephase:
			ReadData((command & 31) == 12, false);
			return;

		case execreadphase:
			if (diskwait && count > 0) {
				// データ転送リクエスト
				if (!(status & S_RQM)) {
					// 初回に終了時間に変換
					if (bufptr == buffer) {
						txtime += scheduler->GetTime();
					}

					status |= S_RQM;
					Intr(true);
				}

				// 止まらないように継続する(LOST ERRORはTODO)
				if (count >= 2) {
					int ticks = 3;
					if (count == 2) {
						// 最後の残り時間
						ticks = txtime - scheduler->GetTime();
						txtime = 0;
					} else {
						// 平均転送時間
						ticks = (txtime - scheduler->GetTime()) / count;
					}
					ticks = Max(1, ticks);
					ticks = Min(4, ticks);
					SetTimer(execreadphase, ticks);
				}
				return;
			}

			if (!IDIncrement()) {
				SetTimer(timerphase, 20);
				return;
			}

			SetTimer(executephase, 3);
			return;

		case tcphase:
			DelTimer();

			// 割り込み中なら解除
			if (status & S_RQM) {
				status &= ~S_RQM;
				Intr(false);
			}

			if (diskwait && count > 0) {
				// 途中打ち切りなら残り時間経過後に再突入
				if (txtime) {
					int ticks = txtime - scheduler->GetTime();
					txtime = 0;
					if (ticks > 0) {
						SetTimer(tcphase, ticks);
						return;
					}
				}
			}

			LOG1("\tTC at 0x%x byte\n", bufptr - buffer);
			ShiftToResultPhase7();
			return;

		case timerphase:
			result = ST0_AT | ST1_EN;
			ShiftToResultPhase7();
			return;
	}
}

// ---------------------------------------------------------------------------
//	Scan*Equal
//
void FDC::CmdScanEqual()
{
	switch (phase)
	{
		case idlephase:
			LOG0("Scan");
			if ((command & 31) == 0x19)
				LOG0("LowOr");
			else if ((command & 31) == 0x1d)
				LOG0("HighOr");
			LOG0("Equal");
			ShiftToCommandPhase(9);
			return;

		case commandphase:
			GetSectorParameters();
			dtl = dtl | 0x100;		// STP パラメータ．DTL として無効な値を代入する．

		case executephase:
			ReadData(false, true);
			return;

		case execscanphase:
			if (diskwait && count > 0) {
				// データ転送リクエスト
				if (!(status & S_RQM)) {
					// 初回に終了時間に変換
					if (bufptr == buffer) {
						txtime += scheduler->GetTime();
					}

					status |= S_RQM;
					Intr(true);
				}

				// 止まらないように継続する(LOST ERRORはTODO)
				if (count >= 2) {
					int ticks = 3;
					if (count == 2) {
						// 最後の残り時間
						ticks = txtime - scheduler->GetTime();
						txtime = 0;
					} else {
						// 平均転送時間
						ticks = (txtime - scheduler->GetTime()) / count;
					}
					ticks = Max(1, ticks);
					ticks = Min(4, ticks);
					SetTimer(execscanphase, ticks);
				}
				return;
			}

			if (!IDIncrement()) {
				SetTimer(timerphase, 20);
				return;
			}

			SetTimer(executephase, 3);
			return;

		case tcphase:
			DelTimer();

			// 割り込み中なら解除
			if (status & S_RQM) {
				status &= ~S_RQM;
				Intr(false);
			}

			if (diskwait && count > 0) {
				// 途中打ち切りなら残り時間経過後に再突入
				if (txtime) {
					int ticks = txtime - scheduler->GetTime();
					txtime = 0;
					if (ticks > 0) {
						SetTimer(tcphase, ticks);
						return;
					}
				}
			}

			LOG1("\tTC at 0x%x byte\n", bufptr - buffer);
			ShiftToResultPhase7();
			return;

		case timerphase:
			// 終了，みつかんなかった～
			result = ST1_EN | ST2_SN;
			ShiftToResultPhase7();
			return;
	}
}

void FDC::ReadData(bool deleted, bool scan)
{
	LOG5("\tRead(%.2x) %.2x %.2x %.2x %.2x\n", command, idr.c, idr.h, idr.r, idr.n);
	if (showstatus) {
		statusdisplay.Show(
			85, 0, "%s DRV %d C %.2x H %.2x R %.2x N %.2x",
			scan ? "Scan" : "Read", hdu & 3, idr.c, idr.h, idr.r, idr.n);
	}

	CriticalSection::Lock lock(diskmgr->GetCS());
	result = CheckCondition(false);
	if (result & ST1_MA)
	{
		// ディスクが無い場合，100ms 後に再挑戦
		SetTimer(executephase, 10000);
		LOG0("Disk not mounted: Retry\n");
		return;
	}

	if (result) {
		ShiftToResultPhase7();
		return;
	}

	uint dr = hdu & 3;
	uint flags = ((hdu >> 2) & 1) | (command & 0x40) | (drive[dr].hd & 0x80);

	int size;
	int stime = 0;
	result = diskmgr->GetFDU(dr)->ReadSector(flags, idr, buffer, &size, &stime, &txtime);
	if (!diskwait) {
		stime = 0;
		txtime = 0;
	}

	if (deleted) {
		result ^= ST2_CM;
	}

	// データエラー以外
	if ((result & ~ST2_CM) && !(result & ST2_DD)) {
		ShiftToResultPhase7();
		return;
	}

	// 0バイト転送でスキップさせる
	if ((result & ST2_CM) && (command & 0x20)) {
		ShiftToExecReadPhase(0, false);
		SetTimer(execreadphase, Max(stime + txtime, 3));

		return;
	}

	int xbyte = idr.n ? (0x80 << Min(7, idr.n)) : (Min(dtl, 0x80));
	if (!scan) {
		ShiftToExecReadPhase(xbyte, !diskwait);
	} else {
		ShiftToExecScanPhase(xbyte, !diskwait);
	}

	if (diskwait) {
		if (!scan) {
			SetTimer(execreadphase, Max(stime, 3));
		} else {
			SetTimer(execscanphase, Max(stime, 3));
		}

		if (size != xbyte) {
			txtime = (txtime * xbyte) / size;
		}
	}
}

// ---------------------------------------------------------------------------
//	Seek
//
void FDC::CmdSeek()
{
	switch (phase)
	{
		case idlephase:
			LOG0("Seek ");
			ShiftToCommandPhase(2);
			return;

		case commandphase:
			LOG2("(%.2x %.2x)\n", buffer[0], buffer[1]);
			Seek(buffer[0], buffer[1]);
			ShiftToIdlePhase();
			return;
	}
}

// ---------------------------------------------------------------------------
//	Recalibrate
//
void FDC::CmdRecalibrate()
{
	switch (phase)
	{
		case idlephase:
			LOG0("Recalibrate ");
			ShiftToCommandPhase(1);
			return;

		case commandphase:
			LOG1("(%.2x)\n", buffer[0]);

			Seek(buffer[0] & 3, 0);
			ShiftToIdlePhase();
			return;
	}
}

// ---------------------------------------------------------------------------
//	指定のドライブをシークする
//
void FDC::Seek(uint dr, uint cy)
{
	dr &= 3;

	cy <<= drive[dr].dd;
	int seekcount = Abs(cy - drive[dr].cyrinder);
	if (GetDeviceStatus(dr) & 0x80) {
		// FAULT
		LOG1("\tSeek on unconnected drive (%d)\n", dr);
		drive[dr].result = (dr & 3) | ST0_SE | ST0_NR | ST0_AT;
		Intr(true);
		int_requested = true;
	} else {
		LOG3("Seek: %d -> %d (%d)\n", drive[dr].cyrinder, cy, seekcount);
		drive[dr].cyrinder = cy;
		seektime = seekcount && diskwait ? (400 * Abs(seekcount) + 500) : 10;
		scheduler->AddEvent
			(seektime, this, STATIC_CAST(TimeFunc, &FDC::SeekEvent), dr);
		seekstate |= 1 << dr;

		if (seektime > 10) {
			fdstat = (fdstat & ~(1 << dr)) | 0x10;
			if (pfdstat >= 0) {
				bus->Out(pfdstat, fdstat);
			}
		}
	}
}

void IOCALL FDC::SeekEvent(uint dr)
{
	LOG1("\tSeek (%d) ", dr);
	CriticalSection::Lock lock(diskmgr->GetCS());

	if (seektime > 1000) {
		fdstat &= ~0x10;
		if (pfdstat >= 0) {
			bus->Out(pfdstat, fdstat);
		}
	}

	seektime = 0;
	if (dr > num_drives || !diskmgr->GetFDU(dr)->Seek(drive[dr].cyrinder)) {
		drive[dr].result = (dr & 3) | ST0_SE;
		LOG0("success.\n");
	} else {
		drive[dr].result = (dr & 3) | ST0_SE | ST0_NR | ST0_AT;
		LOG0("failed.\n");
	}

	Intr(true);
	int_requested = true;
	seekstate &= ~(1 << dr);
}

// ---------------------------------------------------------------------------
//	Specify
//
void FDC::CmdSpecify()
{
	switch (phase)
	{
		case idlephase:
			LOG0("Specify ");
			ShiftToCommandPhase(2);
			return;

		case commandphase:
			LOG2("(%.2x %.2x)\n", buffer[0], buffer[1]);
			ShiftToIdlePhase();
			return;
	}
}

// ---------------------------------------------------------------------------
//	Invalid
//
void FDC::CmdInvalid()
{
	LOG0("Invalid\n");
	buffer[0] = uint8(ST0_IC);
	ShiftToResultPhase(1);
}

// ---------------------------------------------------------------------------
//	SenceIntState
//
void FDC::CmdSenceIntStatus()
{
	if (int_requested) {
		LOG0("SenceIntStatus ");
		int_requested = false;

		int i;
		for (i = 0; i < 4; i++) {
			if (drive[i].result) {
				buffer[0] = uint8(drive[i].result);
				buffer[1] = uint8(drive[i].cyrinder >> drive[i].dd);
				drive[i].result = 0;
				ShiftToResultPhase(2);
				break;
			}
		}

		for (; i < 4; i++) {
			if (drive[i].result)
				int_requested = true;
		}
	} else {
		LOG0("Invalid(SenceIntStatus)\n");
		buffer[0] = uint8(ST0_IC);
		ShiftToResultPhase(1);
	}
}

// ---------------------------------------------------------------------------
//	SenceDeviceStatus
//
void FDC::CmdSenceDeviceStatus()
{
	switch (phase)
	{
		case idlephase:
			LOG0("SenceDeviceStatus ");
			ShiftToCommandPhase(1);
			return;

		case commandphase:
			LOG1("(%.2x) ", buffer[0]);
			buffer[0] = GetDeviceStatus(buffer[0] & 3);
			ShiftToResultPhase(1);
			return;
	}
}

// ---------------------------------------------------------------------------
//	GetDeviceStatus
//
uint FDC::GetDeviceStatus(uint dr)
{
	CriticalSection::Lock lock(diskmgr->GetCS());
	hdu = (hdu & ~3) | (dr & 3);
	if (dr < num_drives) {
		return diskmgr->GetFDU(dr)->SenceDeviceStatus() | dr;
	} else {
		return 0x80 | dr;
	}
}

// ---------------------------------------------------------------------------
//	WriteData
//
void FDC::CmdWriteData()
{
	switch (phase)
	{
		case idlephase:
			LOG0((command & 31) == 9 ? "WriteDeletedData" : "WriteData ");
			ShiftToCommandPhase(8);
			return;

		case commandphase:
			GetSectorParameters();

		case executephase:
			WriteData();
			return;

		case execwritephase:
			if (diskwait && count > 0) {
				// データ転送リクエスト
				if (!(status & S_RQM)) {
					// 初回に終了時間に変換
					if (bufptr == buffer) {
						txtime += scheduler->GetTime();
					}

					status |= S_RQM;
					Intr(true);
				}

				// 止まらないように継続する(LOST ERRORはTODO)
				if (count >= 2) {
					int ticks = 3;
					if (count == 2) {
						// 最後の残り時間
						ticks = txtime - scheduler->GetTime();
						txtime = 0;
					} else {
						// 平均転送時間
						ticks = (txtime - scheduler->GetTime()) / count;
					}
					ticks = Max(1, ticks);
					ticks = Min(4, ticks);
					SetTimer(execwritephase, ticks);
				}
				return;
			}

			// データ書き込み
			FlushData((command & 31) == 9);

			if (!IDIncrement()) {
				SetTimer(timerphase, 20);
				return;
			}

			SetTimer(executephase, 3);
			return;

		case tcphase:
			DelTimer();

			// 割り込み中なら解除
			if (status & S_RQM) {
				status &= ~S_RQM;
				Intr(false);
			}

			if (diskwait && count > 0) {
				// 途中打ち切りなら残り時間経過後に再突入
				if (txtime) {
					int ticks = txtime - scheduler->GetTime();
					txtime = 0;
					if (ticks > 0) {
						SetTimer(tcphase, ticks);
						return;
					}
				}
			}

			LOG1("\tTC at 0x%x byte\n", bufptr - buffer);
			if (prevphase == execwritephase && count > 0) {
				// 転送中？
				LOG0("flush");
				memset(bufptr, 0, count);
				FlushData((command & 31) == 9);
			}

			ShiftToResultPhase7();
			return;

		case timerphase:
			// 次のセクタを処理しない
			result = ST0_AT | ST1_EN;
			ShiftToResultPhase7();
			return;
	}
}

// ---------------------------------------------------------------------------
//	WriteData Execution
//
void FDC::WriteData()
{
	LOG5("\tWrite(%.2x) %.2x %.2x %.2x %.2x\n", command, idr.c, idr.h, idr.r, idr.n);
	if (showstatus)
		statusdisplay.Show(85, 0, "Write (%d) %.2x %.2x %.2x %.2x", hdu & 3, idr.c, idr.h, idr.r, idr.n);

	CriticalSection::Lock lock(diskmgr->GetCS());
	result = CheckCondition(true);
	if (result & ST1_MA) {
		// ディスクが無い場合，100ms 後に再挑戦
		SetTimer(executephase, 10000);
		LOG0("Disk not mounted: Retry\n");
		return;
	}

	if (result) {
		ShiftToResultPhase7();
		return;
	}

	uint dr = hdu & 3;
	uint flags = ((hdu >> 2) & 1) | (command & 0x40) | (drive[dr].hd & 0x80);
	int stime = 0;

	int xbyte = 0x80 << Min(7, idr.n);
	if (!idr.n)
	{
		xbyte = Min(dtl, 0x80);
		memset(buffer + xbyte, 0, 0x80 - xbyte);
	}

	result = diskmgr->GetFDU(dr)->FindID(flags, idr, xbyte, &stime, &txtime);
	if (!diskwait) {
		stime = 0;
		txtime = 0;
	}

	if (result)
	{
		ShiftToResultPhase7();
		return;
	}

	ShiftToExecWritePhase(xbyte, !diskwait);
	if (diskwait) {
		SetTimer(execwritephase, Max(stime, 3));
	}
}

// ---------------------------------------------------------------------------
//	FlushData
//
void FDC::FlushData(bool deleted)
{
	CriticalSection::Lock lock(diskmgr->GetCS());
	if (result = CheckCondition(true)) {
		return;
	}

	uint dr = hdu & 3;
	uint flags = ((hdu >> 2) & 1) | (command & 0x40) | (drive[dr].hd & 0x80);
	result = diskmgr->GetFDU(dr)->WriteSector(flags, idr, buffer, deleted);
}

// ---------------------------------------------------------------------------
//	ReadID
//
void FDC::CmdReadID()
{
	switch (phase)
	{
		case idlephase:
			LOG0("ReadID ");
			ShiftToCommandPhase(1);
			return;

		case commandphase:
			LOG1("(%.2x)", buffer[0]);
			hdu = buffer[0];

		case executephase:
			ReadID();
			return;

		case timerphase:
			ShiftToResultPhase7();
			return;
	}
}

// ---------------------------------------------------------------------------
//	ReadID Execution
//
void FDC::ReadID()
{
	CriticalSection::Lock lock(diskmgr->GetCS());
	result = CheckCondition(false);
	if (result & ST1_MA) {
		// ディスクが無い場合，100ms 後に再挑戦
		SetTimer(executephase, 10000);
		LOG0("Disk not mounted: Retry\n");
		return;
	}

	if (result) {
		ShiftToResultPhase7();
		return;
	}

	uint dr = hdu & 3;
	uint flags = ((hdu >> 2) & 1) | (command & 0x40) | (drive[dr].hd & 0x80);

	if (showstatus) {
		statusdisplay.Show(85, 0,
			"ReadID DRV %d FLAG %.2x C %.2x H %.2x R %.2x N %.2x",
			dr, flags, idr.c, idr.h, idr.r, idr.n);
	}

	int stime = 0;
	result = diskmgr->GetFDU(dr)->ReadID(flags, &idr, &stime);
	if (!diskwait) {
		stime = 0;
	}

	if (diskwait) {
		SetTimer(timerphase, Max(stime, 3));
		return;
	}

	ShiftToResultPhase7();
}

// ---------------------------------------------------------------------------
//	ReadDiagnostic
//
void FDC::CmdReadDiagnostic()
{
	switch (phase)
	{
		case idlephase:
			LOG0("ReadDiagnostic ");
			ShiftToCommandPhase(8);		// パラメータは 8 個
			readdiagptr = 0;
			return;

		case commandphase:
			GetSectorParameters();
			SearchIndex();
			return;

		case executephase:
			ReadDiagnostic();
			return;

		case execreadphase:
			if (diskwait && count > 0) {
				// データ転送リクエスト
				if (!(status & S_RQM)) {
					// 初回に終了時間に変換
					if (bufptr == buffer) {
						txtime += scheduler->GetTime();
					}

					status |= S_RQM;
					Intr(true);
				}

				// 止まらないように継続する(LOST ERRORはTODO)
				if (count >= 2) {
					int ticks = 3;
					if (count == 2) {
						// 最後の残り時間
						ticks = txtime - scheduler->GetTime();
						txtime = 0;
					} else {
						// 平均転送時間
						ticks = (txtime - scheduler->GetTime()) / count;
					}
					ticks = Max(1, ticks);
					ticks = Min(4, ticks);
					SetTimer(execreadphase, ticks);
				}
				return;
			}

			if (!IDIncrement()) {
				SetTimer(timerphase, 3);
				return;
			}

			SetTimer(executephase, 3);
			return;

		case tcphase:
			DelTimer();

			// 割り込み中なら解除
			if (status & S_RQM) {
				status &= ~S_RQM;
				Intr(false);
			}

			if (diskwait && count > 0) {
				// 途中打ち切りなら残り時間経過後に再突入
				if (txtime) {
					int ticks = txtime - scheduler->GetTime();
					txtime = 0;
					if (ticks > 0) {
						SetTimer(tcphase, ticks);
						return;
					}
				}
			}

			LOG1("\tTC at 0x%x byte\n", bufptr - buffer);
			ShiftToResultPhase7();
			return;

		case timerphase:
			result = ST0_AT | ST1_EN;
			ShiftToResultPhase7();
			return;
	}
}

void FDC::SearchIndex()
{
	CriticalSection::Lock lock(diskmgr->GetCS());
	result = CheckCondition(false);
	if (result & ST1_MA) {
		// ディスクが無い場合，100ms 後に再挑戦
		SetTimer(executephase, 10000);
		LOG0("Disk not mounted: Retry\n");
		return;
	}

	if (result) {
		ShiftToResultPhase7();
		return;
	}

	uint dr = hdu & 3;
	uint flags = ((hdu >> 2) & 1) | (command & 0x40) | (drive[dr].hd & 0x80);
	int stime = 0;
	result = diskmgr->GetFDU(dr)->SearchIndex(flags, &stime);
	if (!diskwait) {
		stime = 0;
	}

	if (result) {
		ShiftToResultPhase7();
	}

	SetTimer(executephase, Max(stime, 3));
}

void FDC::ReadDiagnostic()
{
	LOG0("\tReadDiagnostic ");

	CriticalSection::Lock lock(diskmgr->GetCS());
	uint dr = hdu & 3;
	uint flags = ((hdu >> 2) & 1) | (command & 0x40) | (drive[dr].hd & 0x80);
	uint size = 128 << Min(idr.n, 7);

	int tr = (drive[dr].cyrinder >> drive[dr].dd) * 2 + ((hdu >> 2) & 1);
	if (showstatus) {
		statusdisplay.Show(84, showstatus ? 1000 : 2000,
			"ReadDiagnostic DRV %d C %.2x H %.2x (T %d)",
			dr, (drive[dr].cyrinder >> drive[dr].dd), ((hdu >> 2) & 1), tr);
	}

	int stime = 0;
	result = diskmgr->GetFDU(dr)->ReadDiag(flags, buffer, &size, idr, &stime, &txtime);
	if (!diskwait) {
		stime = 0;
		txtime = 0;
	}

	if (result & ST0_AT) {
		ShiftToResultPhase7();
		return;
	}

	xbyte = idr.n ? 0x80 << Min(7, idr.n) : Min(dtl, 0x80);
	ShiftToExecReadPhase(xbyte, !diskwait);

	if (diskwait) {
		SetTimer(execreadphase, Max(stime, 3));

		if (size != xbyte) {
			txtime = (txtime * xbyte) / size;
		}
	}
}

// ---------------------------------------------------------------------------
//	WriteID
//
void FDC::CmdWriteID()
{
	switch (phase)
	{
		case idlephase:
			LOG0("WriteID ");
			ShiftToCommandPhase(5);
			return;

		case commandphase:
			LOG5("(%.2x %.2x %.2x %.2x %.2x)\n",
				buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);

			// コマンド記録
			wid.idr = 0;
			hdu = buffer[0];
			wid.n = buffer[1];
			wid.sc = 0;
			wid.gpl = buffer[3];
			wid.d = buffer[4];
			eot = buffer[2];

			// セクタ数が0ならばエラーにしておこう
			if (!eot) {
				result = ST0_AT | ST1_EN;
				ShiftToResultPhase7();
				return;
			}

			// プリアンブルを書く
			WritePreamble();
			return;

		case executephase:
			ShiftToExecWritePhase(4, !diskwait);
			if (!diskwait) {
				return;
			}

		case execwritephase:
			if (wid.sc < eot) {
				if (diskwait) {
					if (count > 0) {
						// 次の1バイト転送のリクエストを上げる
						int ticks = 3;
						if (!(status & S_RQM)) {
							status |= S_RQM;
							Intr(true);
						}

						// 止まらないように継続する(LOST ERRORはTODO)
						if (count > 1) {
							SetTimer(execwritephase, ticks);
						}
						return;
					}
				}

				// IDR取得
				idr.c = buffer[0];
				idr.h = buffer[1];
				idr.r = buffer[2];
				idr.n = buffer[3];
				wid.idr = &idr;

				// IDを書く
				WriteID();
			} else {
				// ポストアンブルを書く
				WritePostamble();
			}
			return;

		case tcphase:
			DelTimer();
			LOG1("\tTC at 0x%x byte\n", bufptr - buffer);

			// ポストアンブルを書く
			WritePostamble();
			return;

		case timerphase:
			ShiftToResultPhase7();
			return;
	}
}
// ---------------------------------------------------------------------------
//	WritePreamble Execution
//
void FDC::WritePreamble()
{
	CriticalSection::Lock lock(diskmgr->GetCS());
	result = CheckCondition(true);
	if (result) {
		ShiftToResultPhase7();
		return;
	}

	uint dr = hdu & 3;
	uint flags = ((hdu >> 2) & 1) | (command & 0x40) | (drive[dr].hd & 0x80);

	// インデックスホール待ち
	int stime = 0;
	result = diskmgr->GetFDU(dr)->SearchIndex(flags, &stime);
	if (!diskwait) {
		stime = 0;
	}

	if (result) {
		ShiftToResultPhase7();
	}

	// プリアンブル書き込み
	result = diskmgr->GetFDU(dr)->WritePreamble(flags, &txtime);
	if (!diskwait) {
		txtime = 0;
	}

	if (result) {
		ShiftToResultPhase7();
	}

	SetTimer(executephase, Max((stime + txtime), 3));
}

// ---------------------------------------------------------------------------
//	WriteID Execution
//
void FDC::WriteID()
{
	CriticalSection::Lock lock(diskmgr->GetCS());
	uint dr = hdu & 3;
	uint flags = ((hdu >> 2) & 1) | (command & 0x40) | (drive[dr].hd & 0x80);

	LOG4("\tID %.2x %.2x %.2x %.2x\n", idr.c, idr.h, idr.r, idr.n);
	if (showstatus) {
		statusdisplay.Show(85, 0, "WriteID dr:%d tr:%.2d sc:%.2d (%.2x %.2x %.2x %.2x)",
			dr, (drive[dr].cyrinder >> drive[dr].dd) * 2 + ((hdu >> 2) & 1), wid.sc, idr.c, idr.h, idr.r, idr.n);
	}

	result = diskmgr->GetFDU(dr)->WriteID(flags, &wid, &txtime);
	if (result) {
		ShiftToResultPhase7();
	}

	if (!diskwait) {
		txtime = 0;
	}

	// 次のセクタ
	wid.sc++;

	// 書き終わってから再度実行フェーズ
	SetTimer(executephase, Max(txtime, 3));
}

// ---------------------------------------------------------------------------
//	WritePostamble Execution
//
void FDC::WritePostamble()
{
	// ポストアンブルを書く
	CriticalSection::Lock lock(diskmgr->GetCS());
	uint dr = hdu & 3;
	uint flags = ((hdu >> 2) & 1) | (command & 0x40) | (drive[dr].hd & 0x80);
	result = diskmgr->GetFDU(dr)->WritePostamble(flags, &txtime);
	if (result) {
		ShiftToResultPhase7();
	}

	if (!diskwait) {
		txtime = 0;
	}

	// 書き終わってからタイマーフェーズ
	SetTimer(timerphase, Max(txtime, 3));
}

// ---------------------------------------------------------------------------
//	Read/Write 操作が実行可能かどうかを確認
//	
uint FDC::CheckCondition(bool write)
{
	uint dr = hdu & 3;
	hdue = hdu;
	if (dr >= num_drives) {
		return ST0_AT | ST0_NR;
	}

	if (!diskmgr->GetFDU(dr)->IsMounted()) {
		return ST0_AT | ST1_MA;
	}

	return 0;
}

// ---------------------------------------------------------------------------
//	Read/Write Data 系のパラメータを得る
//	
void FDC::GetSectorParameters()
{
	LOG8("(%.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x)\n",
		buffer[0], buffer[1], buffer[2], buffer[3],
		buffer[4], buffer[5], buffer[6], buffer[7]);

	hdu = hdue = buffer[0];
	idr.c = buffer[1];
	idr.h = buffer[2];
	idr.r = buffer[3];
	idr.n = buffer[4];
	eot = buffer[5];
	gpl = buffer[6];
	dtl = buffer[7];
}

// ---------------------------------------------------------------------------
//	状態保存
//
uint IFCALL FDC::GetStatusSize()
{
	return sizeof(Snapshot);
}

bool IFCALL FDC::SaveStatus(uint8* s)
{
	Snapshot* st = (Snapshot*)s;

	st->rev = ssrev;
	st->bufptr = bufptr ? bufptr - buffer : ~0;
	st->count = count;
	st->status = status;
	st->command = command;
	st->data = data;
	st->phase = phase;
	st->prevphase = prevphase;
	st->t_phase = timerhandle ? t_phase : idlephase;
	st->int_requested = int_requested;
	st->accepttc = accepttc;
	st->idr = idr;
	st->hdu = hdu;
	st->hdue = hdue;
	st->dtl = dtl;
	st->eot = eot;
	st->seekstate = seekstate;
	st->result = result;
	st->readdiagptr = readdiagptr ? readdiagptr - buffer : ~0;
	st->readdiaglim = readdiaglim - buffer;
	st->xbyte = xbyte;
	st->readdiagcount = readdiagcount;
	st->wid = wid;
	memcpy(st->buf, buffer, 0x4000);
	for (int d = 0; d < num_drives; d++)
		st->dr[d] = drive[d];
	LOG1("save status  bufptr = %p\n", bufptr);

	return true;
}

bool IFCALL FDC::LoadStatus(const uint8* s)
{
	const Snapshot* st = (const Snapshot*)s;
	if (st->rev != ssrev) {
		return false;
	}

	bufptr = (st->bufptr == ~0) ? 0 : buffer + st->bufptr;
	count = st->count;
	status = st->status;
	command = st->command;
	data = st->data;
	phase = st->phase;
	prevphase = st->prevphase;
	t_phase = st->t_phase;
	int_requested = st->int_requested;
	accepttc = st->accepttc;
	idr = st->idr;
	hdu = st->hdu;
	hdue = st->hdue;
	dtl = st->dtl;
	eot = st->eot;
	seekstate = st->seekstate;
	result = st->result;
	readdiagptr = (st->readdiagptr == ~0) ? 0 : buffer + st->readdiagptr;
	readdiaglim = buffer + st->readdiaglim;
	xbyte = st->xbyte;
	readdiagcount = st->readdiagcount;
	wid = st->wid;
	memcpy(buffer, st->buf, 0x4000);

	if ((command & 19) == 17)
		dtl |= 0x100;

	scheduler->DelEvent(this);
	if (st->t_phase != idlephase) {
		timerhandle = scheduler->AddEvent(diskwait ? 100 : 10, this, STATIC_CAST(TimeFunc, &FDC::PhaseTimer), st->t_phase);
	}

	fdstat = 0;
	for (int d = 0; d < num_drives; d++) {
		drive[d] = st->dr[d];
		diskmgr->GetFDU(d)->Seek(drive[d].cyrinder);
		if (seekstate & (1 << d)) {
			scheduler->AddEvent(diskwait ? 100 : 10, this, STATIC_CAST(TimeFunc, &FDC::SeekEvent), d);
			fdstat |= 0x10;
		}
		statusdisplay.FDAccess(d, drive[d].hd != 0, false);

		fdstat |= drive[d].hd ? 4 << d : 0;
	}

	if (pfdstat >= 0) {
		bus->Out(pfdstat, fdstat);
	}

	LOG1("load status  bufptr = %p\n", bufptr);

	return true;
}

// ---------------------------------------------------------------------------
//	device description
//
const Device::Descriptor FDC::descriptor = { indef, outdef };

const Device::OutFuncPtr FDC::outdef[] =
{
	STATIC_CAST(Device::OutFuncPtr, &Reset),
	STATIC_CAST(Device::OutFuncPtr, &SetData),
	STATIC_CAST(Device::OutFuncPtr, &DriveControl),
	STATIC_CAST(Device::OutFuncPtr, &MotorControl)
};

const Device::InFuncPtr FDC::indef[] =
{
	STATIC_CAST(Device::InFuncPtr, &Status),
	STATIC_CAST(Device::InFuncPtr, &GetData),
	STATIC_CAST(Device::InFuncPtr, &TC),
};

