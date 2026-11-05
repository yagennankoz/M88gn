// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator.
//	Copyright (C) cisc 1998, 1999.
// ---------------------------------------------------------------------------
//	$Id: fdu.h,v 1.2 1999/03/25 11:29:20 cisc Exp $

#pragma once

#include "floppy.h"

class DiskManager;
class Scheduler;

namespace PC8801
{

// ---------------------------------------------------------------------------
//	FDU
//	フロッピードライブをエミュレーションするクラス
//
//	ReadSector(ID id, uint8* data);
//	セクタを読む
//
//	WriteSector(ID id, uint8* data);
//	 
class FDU
{
public:
	typedef FloppyDisk::IDR IDR;
	struct WIDDESC
	{
		IDR* idr;
		uint8 n, sc, gpl, d;
	};

public:
	enum Flags
	{
		MFM = 0x40, head1 = 0x01,
	};

	FDU();
	~FDU();

	bool Init(DiskManager* diskmgr, int dr, Scheduler *scheduler);

	bool Mount(FloppyDisk* disk);
	bool Unmount();

	bool IsMounted() const { return disk != 0; }
	uint ReadSector(uint flags, IDR id, uint8* data, int *size, int *stime, int *txtime);
	uint FindID(uint flags, IDR id, int size, int *stime, int *txtime);
	uint WriteSector(uint flags, IDR id, const uint8* data, bool deleted);
	uint Seek(uint cyrinder);
	uint SenceDeviceStatus();
	uint ReadID(uint flags, IDR* id, int *stime);
	uint SearchIndex(uint flags, int *stime);
	uint ReadDiag(uint flags, uint8* data, uint* size, IDR idr, int *stime, int *txtime);
	uint WritePreamble(uint flags, int *txtime);
	uint WriteID(uint flags, WIDDESC* wid, int *txtime);
	uint WritePostamble(uint flags, int *txtime);

private:
	struct DiagInfo
	{
		IDR idr;
		uint8* data;
	};

	Scheduler *scheduler;

	uint GetCurrentPos();
	uint Bytes2Time(uint len);
	void SetHead(uint hd);
	FloppyDisk::Sector* NextSector(uint flags);
	uint SearchID(uint flags, IDR *id, bool idmatch, bool ignorecrc, int *stime);
	uint ReadDiagFdx(uint flags, uint8* data, uint* size, IDR idr, int *stime, int *txtime);

	FloppyDisk* disk;
	FloppyDisk::Sector* sector;
	DiskManager* diskmgr;
	int cyrinder;
	int head;
	int drive;
	int track;
};

}

