// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Copyright (C) cisc 1999.
// ---------------------------------------------------------------------------
//	$Id: diskmgr.cpp,v 1.13 1999/11/26 10:13:46 cisc Exp $

#include "headers.h"
#include "diskmgr.h"
#include "status.h"
#include "misc.h"

using namespace D88;

// ---------------------------------------------------------------------------
//	構築・破棄
//
DiskImageHolder::DiskImageHolder()
{
	ref = 0;
}

DiskImageHolder::~DiskImageHolder()
{
	Close();
}

// ---------------------------------------------------------------------------
//	ファイルを開く
//
bool DiskImageHolder::Open(const char* filename, bool ro, bool create)
{
	// 既に持っているファイルかどうかを確認
	if (Connect(filename))
		return true;
	
	if (ref > 0)
		return false;
	
	// ファイルを開く
	readonly = ro;
	
	if (readonly || !fio.Open(filename, 0))
	{
		if (fio.Open(filename, FileIO::readonly))
		{
			if (!readonly)
				statusdisplay.Show(100, 3000, "読取専用ファイルです");
			readonly = true;
		}
		else
		{
			// 新しいディスクイメージ？
			if (!create || !fio.Open(filename, FileIO::create))
			{
				statusdisplay.Show(80, 3000, "ディスクイメージを開けません");
				return false;
			}
		}
	}
	
	// ファイル名を登録
	strncpy(diskname, filename, MAX_PATH-1);
	diskname[MAX_PATH-1] = 0;

	if (!ReadHeaders())
		return false;
	
	ref = 1;
	return true;
}

// ---------------------------------------------------------------------------
//	新しいディスクイメージを加える(FDXの場合はここには来ないはず)
//	type:	2D 0 / 2DD 1 / 2HD 2
//
bool DiskImageHolder::AddDisk(const char* title, uint type)
{
	// FDXの場合
	char ext[_MAX_EXT];
	_splitpath(diskname, NULL, NULL, NULL, ext);
	if (stricmp(ext, ".FDX") == 0) {
		return AddDiskFDX(title, type);
	}

	// D88の場合
	if (ndisks >= max_disks)
		return false;

	int32 diskpos = 0;
	if (ndisks > 0)
	{
		diskpos = disks[ndisks-1].pos + disks[ndisks-1].size;
	}
	DiskInfo& disk = disks[ndisks++];
	disk.pos = diskpos;
	disk.size = sizeof(ImageHeader);

	ImageHeader ih;
	memset(&ih, 0, sizeof(ImageHeader));
	strncpy(ih.title, title, 16);
	ih.disktype = type * 0x10;
	ih.disksize = sizeof(ImageHeader);
	fio.SetLogicalOrigin(0);
	fio.Seek(diskpos, FileIO::begin);
	fio.Write(&ih, sizeof(ImageHeader));
	return true;
}

// ---------------------------------------------------------------------------
//	ディスクイメージの情報を得る
//
bool DiskImageHolder::ReadHeaders()
{
	fio.SetLogicalOrigin(0);
	fio.Seek(0, FileIO::end);
	if (fio.Tellp() == 0)
	{
		// new file
		ndisks = 0;
		return true;
	}
	
	fio.Seek(0, FileIO::begin);
	
	ImageHeader ih;
	for (ndisks = 0; ndisks < max_disks; ndisks++)
	{
		// ヘッダー読み込み
		DiskInfo& disk = disks[ndisks];
		disk.pos = fio.Tellp();
		
		// 256+16 は Raw イメージの最小サイズ
		if (fio.Read(&ih, sizeof(ImageHeader)) < 256+16)
			break;
		
		if (memcmp(ih.title, "M88 RawDiskImage", 16) == 0)
		{
			if (ndisks != 0)
			{
				statusdisplay.Show(80, 3000, "READER 系ディスクイメージは連結できません");
				return false;
			}

			strncpy(disk.title, "(no name)", 16);
			fio.Seek(0, FileIO::end);
			disk.size = fio.Tellp() - disk.pos;
		}
		else if (memcmp(ih.title, "FDX", 3) == 0 && ih.title[3] == 3)
		{
			if (ndisks != 0)
			{
				statusdisplay.Show(80, 3000, "FDX 系ディスクイメージは連結できません");
				return false;
			}

			FDX::fdxheader_t *fh;
			fh = (FDX::fdxheader_t*)&ih;
			strncpy(disk.title, (char*)fh->name, 16);
			disk.title[16] = 0;
			fio.Seek(0, FileIO::end);
			disk.size = fio.Tellp() - disk.pos;
		}
		else
		{
			if (!IsValidHeader(ih))
			{
				statusdisplay.Show(90, 3000, "イメージに無効なデータが含まれています");
				break;
			}
			
			strncpy(disk.title, ih.title, 16);
			disk.title[16] = 0;
			disk.size = ih.disksize;
			fio.Seek(disk.pos + disk.size, FileIO::begin);
		}
	}
	if (!ndisks)
		return false;

	return true;
}

// ---------------------------------------------------------------------------
//	とじる
//
void DiskImageHolder::Close()
{
	fio.Close();
	ndisks = 0;
	diskname[0] = 0;
	ref = 0;
}

// ---------------------------------------------------------------------------
//	Connect
//
bool DiskImageHolder::Connect(const char* filename)
{
	// 既に持っているファイルかどうかを確認
	if (!strnicmp(diskname, filename, MAX_PATH))
	{
		ref++;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
//	Disconnect
//
bool DiskImageHolder::Disconnect()
{
	if (--ref <= 0)
		Close();
	return true;
}

// ---------------------------------------------------------------------------
//	ヘッダーが有効かどうかを確認
//	
bool DiskImageHolder::IsValidHeader(ImageHeader& ih)
{
	int i;
	// 2D イメージの場合余計な領域は見なかったことにする
	if (ih.disktype == 0)
		memset(&ih.trackptr[84], 0, 4*80);

	// 条件: title が 25 文字以下であること
	for (i=0; i<25 && ih.title[i]; i++)
		;
	if (i==25)
		return false;
	
	// 条件: disksize <= 4M
	if (ih.disksize > 4 * 1024 * 1024)
		return false;

	// 条件: trackptr[0-159] < disksize
	uint trackstart = sizeof(ImageHeader);
	for (int t=0; t<160; t++)
	{
		if (ih.trackptr[t] >= ih.disksize)
			break;
		if (ih.trackptr[t] && ih.trackptr[t] < trackstart)
			trackstart = uint(ih.trackptr[t]);
	}
	
	// 条件: 32+4*84 <= trackstart
	if (trackstart < 32 + 4 * 84)
		return false;
	
	return true;
}

// ---------------------------------------------------------------------------
//	GetTitle
//
const char* DiskImageHolder::GetTitle(int index)
{
	if (index < ndisks)
		return disks[index].title;
	return 0;
}

// ---------------------------------------------------------------------------
//	GetDisk
//
FileIO* DiskImageHolder::GetDisk(int index)
{
	if (index < ndisks)
	{
		fio.SetLogicalOrigin(disks[index].pos);
		return &fio;
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	SetDiskSize
//
bool DiskImageHolder::SetDiskSize(int index, int newsize)
{
	int i;
	if (index >= ndisks)
		return false;

	int32 sizediff = newsize - disks[index].size;
	if (!sizediff)
		return true;

	// 移動させる必要のあるデータのサイズを計算する
	int32 sizemove=0;
	for (i=index+1; i<ndisks; i++)
	{
		sizemove += disks[i].size;
	}

	fio.SetLogicalOrigin(0);
	if (sizemove)
	{
		int32 moveorg = disks[index+1].pos;
		uint8* data = new uint8[sizemove];
		if (!data)
			return false;

		fio.Seek(moveorg, FileIO::begin);
		fio.Read(data, sizemove);
		fio.Seek(moveorg + sizediff, FileIO::begin);
		fio.Write(data, sizemove);

		delete[] data;

		for (i=index+1; i<ndisks; i++)
			disks[i].pos += sizemove;
	}
	else
	{
		fio.Seek(disks[index].pos + newsize, FileIO::begin);
	}
	fio.SetEndOfFile();
	disks[index].size = newsize;
	return true;
}


// ---------------------------------------------------------------------------
//	ブランクディスク作成(FDX)
//
bool DiskImageHolder::AddDiskFDX(const char *title, uint type)
{
	int i;
	FDX::fdxheader_t header;
	int tracklen;
	int c;
	int h;
	FDX::fdxtrack_t *t;
	FDX::fdxtrack_t *trackbuf[FDX_MAX_CYLINDER][FDX_MAX_HEADS];
	bool ret;

	// 連結には対応していない
	if (ndisks != 0)
		return false;

	// シーク
	int32 diskpos = 0;
	fio.SetLogicalOrigin(0);
	fio.Seek(diskpos, FileIO::begin);

	// ヘッダ作成
	memset(&header, 0, sizeof(FDX::fdxheader_t));
	header.signature[0] = 'F';
	header.signature[1] = 'D';
	header.signature[2] = 'X';
	header.revision = 3;
	if (title != NULL) {
		for (i=0; i<16; i++) {
			if (title[i] == '\0') {
				break;
			}
			header.name[i] = title[i];
		}
	}

	// 密度(2D,2DD,2HD)
	if (type == FloppyDisk::MD2HD) {
		header.type = FDX_TYPE_2HD;
		header.cylinders = FDX_MAX_CYLINDER;
		header.heads = FDX_MAX_HEADS;
		header.rate = 1000;
		header.rpm = 360;
	} else if (type == FloppyDisk::MD2DD) {
		header.type = FDX_TYPE_2DD;
		header.cylinders = FDX_MAX_CYLINDER;
		header.heads = FDX_MAX_HEADS;
		header.rate = 500;
		header.rpm = 300;
	} else {
		header.type = FDX_TYPE_2D;
		header.cylinders = FDX_MAX_CYLINDER / 2;
		header.heads = FDX_MAX_HEADS;
		header.rate = 500;
		header.rpm = 300;
	}

	// トラックブロック長
	if (type == FloppyDisk::MD2HD) {
		header.trackblk = (FDX_MAX_CELLS * 2);
	} else {
		header.trackblk = (FDX_MAX_CELLS * 2) / 2;
	}

	// ヘッダ書き込み
	if (!fio.Write((uint8*)&header, sizeof(FDX::fdxheader_t))) {
		return false;
	}

	// トラックデータ長
	tracklen = header.rate * 1000;
	tracklen *= 60;
	tracklen /=	header.rpm;

	// トラック情報初期化
	for (c = 0; c < header.cylinders; c++) {
		for (h = 0; h < header.heads; h++) {
			t = (FDX::fdxtrack_t *)malloc(header.trackblk);
			memset(t, 0x00, header.trackblk);
			t->cylinder = c;
			t->head = h;
			t->index = 0;
			t->length = tracklen;
			trackbuf[c][h] = t;
		}
	}

	// トラック情報書き込み
	ret = true;
	for (c = 0; c < header.cylinders; c++) {
		for (h = 0; h < header.heads; h++) {
			t = trackbuf[c][h];
			if (!fio.Write((uint8*)t, header.trackblk)) {
				ret = FALSE;
				goto save_exit;
			}
		}
	}

save_exit:
	// トラック情報解放
	for (c = 0; c < header.cylinders; c++) {
		for (h = 0; h < header.heads; h++) {
			if (trackbuf[c][h]) {
				free(trackbuf[c][h]);
				trackbuf[c][h] = NULL;
			}
		}
	}

	// ディスク情報設定
	if (ret) {
		DiskInfo& disk = disks[ndisks++];
		disk.pos = diskpos;
		disk.size = sizeof(FDX::fdxheader_t) +
			header.cylinders * header.heads * header.trackblk;
	}

	return ret;
}

// ---------------------------------------------------------------------------
//	構築・破棄
//
DiskManager::DiskManager()
{
}

DiskManager::~DiskManager()
{
	for (int i=0; i<max_drives; i++)
		Unmount(i);
}

// ---------------------------------------------------------------------------
//	初期化
//
bool DiskManager::Init(Scheduler *s)
{
	scheduler = s;

	for (int i=0; i<max_drives; i++)
	{
		drive[i].holder = 0;
		if (!drive[i].fdu.Init(this, i, s))
			return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
//	ディスクイメージが既に開かれているかどうか確認
//	arg:diskname	ディスクイメージのファイルネーム
//	
bool DiskManager::IsImageOpen(const char* diskname)
{
	CriticalSection::Lock lock(cs);
	
	for (int i=0; i<max_drives; i++)
	{
		if (holder[i].Connect(diskname))
		{
			holder[i].Disconnect();
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
//	Mount
//	arg:dr			Mount するドライブ
//		diskname	ディスクイメージのファイルネーム
//		readonly	読み込みのみ
//		index		mount するディスクイメージの番号 (-1 == no disk)
//
bool DiskManager::Mount
(uint dr, const char* diskname, bool readonly, int index, bool create)
{
	int i;

	Unmount(dr);
	
	CriticalSection::Lock lock(cs);
	// ディスクイメージがすでに hold されているかどうかを確認
	DiskImageHolder* h = 0;
	for (i=0; i<max_drives; i++)
	{
		if (holder[i].Connect(diskname))
		{
			h = &holder[i];
			// これから開くディスクが既に開かれていないことを確認する
			if (index >= 0)
			{
				for (uint d=0; d<max_drives; d++)
				{
					if ((d != dr) && (drive[d].holder == h) && (drive[d].index == index))
					{
						index = -1;		// no disk
						statusdisplay.Show(90, 3000, "このディスクは使用中です");
						break;
					}
				}
			}
			break;
		}
	}
	if (!h)			// 空いている holder に hold させる
	{
		for (i=0; i<max_drives; i++)
		{
			if (!holder[i].IsOpen())
			{
				h = &holder[i];
				break;
			}
		}
		if (!h || !h->Open(diskname, readonly, create))
		{
			if (h)
				h->Disconnect();
			return 0;
		}
	}

	if (!h->GetNumDisks())
		index = -1;

	FileIO* fio = 0;
	if (index >= 0)
	{
		fio = h->GetDisk(index);
		if (!fio)
		{
			h->Disconnect();
			return false;
		}
	}
	drive[dr].holder = h;
	drive[dr].index = index;
	drive[dr].sizechanged = false;
	
	if (fio)
	{
		fio->Seek(0, FileIO::begin);
		if (!ReadDiskImage(fio, &drive[dr]))
		{
			h->Disconnect();
			drive[dr].holder = 0;
			return false;
		}
		memset(drive[dr].modified, 0, 164);
		
		drive[dr].fdu.Mount(&drive[dr].disk);
	}
	return true;
}	

// ---------------------------------------------------------------------------
//	ディスクを取り外す
//
bool DiskManager::Unmount(uint dr)
{
	CriticalSection::Lock lock(cs);
	
	bool ret = true;
	Drive& drv = drive[dr];
	drive[dr].fdu.Unmount();
	if (drv.holder)
	{
		if (drv.index >= 0)
		{
			for (int t=0; t<164; t++)
			{
				if (drv.modified[t])
				{
					uint32 disksize = GetDiskImageSize(&drv);
					if (!drv.holder->SetDiskSize(drv.index, disksize))
					{
						ret = false;
						break;
					}

					FileIO* fio = drv.holder->GetDisk(drv.index);
					ret = fio ? WriteDiskImage(fio, &drv) : false;
					break;
				}
			}
		}
		drv.holder->Disconnect();
		drv.holder = 0;
	}
	if (!ret)
		statusdisplay.Show(50, 3000, "ディスクの更新に失敗しました");
	return ret;
}

// ---------------------------------------------------------------------------
//	ディスクイメージを読み込む
//
bool DiskManager::ReadDiskImage(FileIO* fio, Drive* drive)
{
	uint t;
	ImageHeader ih;
	fio->Read(&ih, sizeof(ImageHeader));
	if (!memcmp(ih.title, "M88 RawDiskImage", 16))
		return ReadDiskImageRaw(fio, drive);

	if (!memcmp(ih.title, "FDX", 3) && ih.title[3] == 3)
		return ReadDiskImageFdx(fio, drive);

	// ディスクのタイプチェック
	FloppyDisk::DiskType type;
	uint hd = 0;
	switch (ih.disktype)
	{
	case 0x00:
		type = FloppyDisk::MD2D; 
		memset(&ih.trackptr[84], 0, 4*80);
		break;

	case 0x10: 
		type = FloppyDisk::MD2DD; 
		break;

	case 0x20: 
		type = FloppyDisk::MD2HD; 
		hd = FloppyDisk::highdensity; 
		break;

	default:
		statusdisplay.Show(90, 3000, "サポートしていないメディアです");
		return false;
	}
	bool readonly = drive->holder->IsReadOnly() || ih.readonly;
	
	FloppyDisk& disk = drive->disk;
	if (!disk.Init(type, readonly))
	{
		statusdisplay.Show(70, 3000, "作業用領域を割り当てることができませんでした");
		return false;
	}

	// ごみそうじその１
	for (t=0; t<disk.GetNumTracks(); t++)
	{
		if (ih.trackptr[t] >= ih.disksize)
			break;
	}
	if (t<164)
		memset(&ih.trackptr[t], 0, (164-t) * 4);
	if (t<(uint) Min(160, disk.GetNumTracks()))
		statusdisplay.Show(80, 3000, "ヘッダーに無効なデータが含まれています");

	// trackptr のごみそうじ
	uint trackstart = sizeof(ImageHeader);
	for (t=0; t<84; t++)
	{
		if (ih.trackptr[t] && ih.trackptr[t] < trackstart)
			trackstart = (uint) ih.trackptr[t];
	}
	if (trackstart < sizeof(ImageHeader))
		memset(((char*) &ih) + trackstart, 0, sizeof(ImageHeader)-trackstart);

	// trackptr データの保存
	for (t=0; t<164; t++)
	{
		drive->trackpos[t] = ih.trackptr[t];
		drive->tracksize[t] = 0;
	}
	for (t=0; t<168; t++)
	{
		disk.Seek(t);
		disk.FormatTrack(0, 0);
	}

	// 各トラックの読み込み
	for (t=0; t<disk.GetNumTracks(); t++)
	{
		int cy = t >> 1;
		if (type == FloppyDisk::MD2D)
			cy *= 2;
		disk.Seek((cy * 2) + (t & 1));
		if (ih.trackptr[t])
		{
			fio->Seek(ih.trackptr[t], FileIO::begin);
			int sot = 0;
			int i = 0;
			SectorHeader sh;
			do
			{
				if (fio->Read(&sh, sizeof(sh)) != sizeof(sh))
					break;
				
				FloppyDisk::Sector* sec = disk.AddSector(sh.length);
				if (!sec)
					break;
				sec->id = sh.id;
				sec->size = sh.length;
				sec->flags = (sh.density ^ 0x40) | hd;
				sec->gap3 = 0;
				if (sh.deleted == 0x10)
					sec->flags |= FloppyDisk::deleted;

				switch (sh.status)
				{
				case 0xa0: sec->flags |= FloppyDisk::idcrc;   break;
				case 0xb0: sec->flags |= FloppyDisk::datacrc; break;
				case 0xf0: sec->flags |= FloppyDisk::mam;     break;
				}
				if (fio->Read(sec->image, sh.length) != sh.length)
					break;
				sot += 0x10 + sh.length;
				sec->update = false;
			} while (++i < sh.sectors);

			drive->tracksize[t] = sot;
		}

		// フォーマットフラグをオフにする
		disk.GetTrack()->formated = false;
	}
	return true;
}

// ---------------------------------------------------------------------------
//	ディスクイメージ (READER 形式) を読み込む
//
bool DiskManager::ReadDiskImageRaw(FileIO* fio, Drive* drive)
{
	fio->Seek(16, FileIO::begin);

	bool readonly = drive->holder->IsReadOnly();
	
	FloppyDisk& disk = drive->disk;
	if (!disk.Init(FloppyDisk::MD2D, readonly))
	{
		statusdisplay.Show(70, 3000, "作業用領域を割り当てることができませんでした");
		return false;
	}

	int t;
	for (t=0; t<164; t++)
	{
		drive->trackpos[t] = 0;
		drive->tracksize[t] = 0;
	}
	for (t=0; t<168; t++)
	{
		disk.Seek(t);
		disk.FormatTrack(0, 0);
	}

	// 各トラックの読み込み
	uint8 buf[256];
	FloppyDisk::IDR id;
	id.n = 1;
	for (t=0; t<80; t++)
	{
		id.c = t / 2;
		id.h = t & 1;

		disk.Seek(id.c * 4 + id.h);
		disk.FormatTrack(0, 0);

		for (int r=1; r<=16; r++)
		{
			id.r = r;
			
			if (fio->Read(buf, 256) != 256)
				break;

			FloppyDisk::Sector* sec = disk.AddSector(256);
			if (!sec)
				break;
			sec->id = id;
			sec->size = 256;
			sec->flags = 0x40;
			sec->gap3 = 0;
			memcpy(sec->image, buf,256);
			sec->update = false;
		}

		// フォーマットフラグをオフにする
		disk.GetTrack()->formated = false;
	}
	drive->sizechanged = false;

	return true;
}

// ---------------------------------------------------------------------------
//	ディスクイメージのサイズを計算する
//
uint DiskManager::GetDiskImageSize(Drive* drv)
{
	// FDX拡張
	if (drv->disk.IsFdx()) {
		uint disksize = drv->disk.fdx_header.trackblk;
		disksize *= drv->disk.fdx_header.cylinders;
		disksize *= drv->disk.fdx_header.heads;
		disksize += sizeof(FDX::fdxheader_t);
		return disksize;
	}

	uint disksize = sizeof(ImageHeader);

	for (int t=drv->disk.GetNumTracks()-1; t>=0; t--)
	{
		int tr = (drv->disk.GetType() == FloppyDisk::MD2D) ? t & ~1 : t >> 1;
		tr = (tr << 1) | (t & 1);

		FloppyDisk::Sector* sec;
		for (sec = drv->disk.GetFirstSector(tr); sec; sec=sec->next)
		{
			disksize += sec->size + sizeof(SectorHeader);
		}
	}
	return disksize;
}
	
// ---------------------------------------------------------------------------
//	ディスクイメージの書き出し
//	必要となる領域はあらかじめ確保されていることとする
//
bool DiskManager::WriteDiskImage(FileIO* fio, Drive* drv)
{
	static const uint8 typetbl[3] = { 0x00, 0x10, 0x20 };
	int t;

	// FDX拡張
	if (drv->disk.IsFdx()) {
		return WriteDiskImageFdx(fio, drv);
	}

	// Header の作成
	ImageHeader ih;
	memset(&ih, 0, sizeof(ImageHeader));
	strcpy(ih.title, drv->holder->GetTitle(drv->index));
	
	ih.disktype = typetbl[drv->disk.GetType()];
	ih.readonly = drv->disk.IsReadOnly() ? 0x10 : 0;
	
	uint32 disksize = sizeof(ImageHeader);
	int ntracks = drv->disk.GetNumTracks();

	for (t=0; t<ntracks; t++)
	{
		int tracksize = 0;
		int tr = (drv->disk.GetType() == FloppyDisk::MD2D) ? t & ~1 : t >> 1;
		tr = (tr << 1) | (t & 1);

		FloppyDisk::Sector* sec;
		for (sec = drv->disk.GetFirstSector(tr); sec; sec=sec->next)
			tracksize += sec->size + sizeof(SectorHeader);

		ih.trackptr[t] = tracksize ? disksize : 0;
		disksize += tracksize;
	}
	for (; t<164; t++)
		ih.trackptr[t] = 0;
	
	ih.disksize = disksize;

	if (!fio->Seek(0, FileIO::begin))
		return false;
	if (fio->Write(&ih, sizeof(ImageHeader)) != sizeof(ImageHeader))
		return false;

	for (t=0; t<ntracks; t++)
	{
		int tr = (drv->disk.GetType() == FloppyDisk::MD2D) ? t & ~1 : t >> 1;
		tr = (tr << 1) | (t & 1);
		if (!WriteTrackImage(fio, drv, tr))
			return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
//	トラック一つ分のイメージをかく
//
bool DiskManager::WriteTrackImage(FileIO* fio, Drive* drv, int t)
{
	// FDX拡張
	if (drv->disk.IsFdx()) {
		return WriteTrackImageFdx(fio, drv, t);
	}

	SectorHeader sh;
	memset(&sh, 0, sizeof(SectorHeader));
	
	FloppyDisk::Sector* sec;
	int nsect = 0;
	for (sec = drv->disk.GetFirstSector(t); sec; sec=sec->next)
		nsect++;
	sh.sectors = nsect;
	
	for (sec = drv->disk.GetFirstSector(t); sec; sec=sec->next)
	{
		sh.id = sec->id;
		sh.density = (~sec->flags) & 0x40;
		sh.deleted = sec->flags & 1 ? 0x10 : 0;
		sh.length = sec->size;
		sh.status = 0;
		switch (sec->flags & 14)
		{
		case FloppyDisk::idcrc:		sh.status = 0xa0; break;
		case FloppyDisk::datacrc:	sh.status = 0xb0; break;
		case FloppyDisk::mam:		sh.status = 0xf0; break;
		}
		if (fio->Write(&sh, sizeof(SectorHeader)) != sizeof(SectorHeader))
			return false;
		if (uint(fio->Write(sec->image, sec->size)) != sec->size)
			return false;;
	}
	return true;
}

// ---------------------------------------------------------------------------
//	Unlock
//	Disk 変更宣言
//
void DiskManager::Modified(int dr, int tr)
{
	if (0 <= tr && tr < 164 && !drive[dr].disk.IsReadOnly())
	{
		drive[dr].modified[tr] = true;
	}
}

// ---------------------------------------------------------------------------
//	Update
//	トラックの位置を変えずに更新できる変更をかきこむ
//
void DiskManager::Update()
{
	for (int d=0; d<max_drives; d++)
		UpdateDrive(&drive[d]);
}

// ---------------------------------------------------------------------------
//	UpdateDrive
//
void DiskManager::UpdateDrive(Drive* drv)
{
	if (!drv->holder || drv->sizechanged)
		return;

	CriticalSection::Lock lock(cs);
	int t;
	for (t=0; t<164 && !drv->modified[t]; t++)
		;
	if (t < 164)
	{
		FileIO* fio = drv->holder->GetDisk(drv->index);
		if (fio)
		{
			for (; t<164; t++)
			{
				if (drv->modified[t])
				{
					// FDXはそのまま書き込む(というかここ使ってない？)
					if (drv->disk.IsFdx()) {
						drv->modified[t] = false;
						fio->Seek(drv->trackpos[t], FileIO::begin);
						WriteTrackImageFdx(fio, drv, t);
						continue;
					}

					FloppyDisk::Sector* sec;
					int tracksize = 0;
					
					for (sec = drv->disk.GetFirstSector(t); sec; sec=sec->next)
						tracksize += sec->size + sizeof(SectorHeader);
					
					if (tracksize <= drv->tracksize[t])
					{
						drv->modified[t] = false;
						fio->Seek(drv->trackpos[t], FileIO::begin);
						WriteTrackImage(fio, drv, t);
					}
					else
					{
						drv->sizechanged = true;
						break;
					}
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------
//	イメージタイトル取得
//
const char* DiskManager::GetImageTitle(uint dr, uint index)
{
	if (dr < max_drives && drive[dr].holder)
	{
		return drive[dr].holder->GetTitle(index);
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	イメージの数取得
//
uint DiskManager::GetNumDisks(uint dr)
{
	if (dr < max_drives)
	{
		if (drive[dr].holder)
			return drive[dr].holder->GetNumDisks();
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	現在選択されているディスクの番号を取得
//
int DiskManager::GetCurrentDisk(uint dr)
{
	if (dr < max_drives)
	{
		if (drive[dr].holder)
			return drive[dr].index;
	}
	return -1;
}

// ---------------------------------------------------------------------------
//	ディスク追加
//	dr		対象ドライブ
//	title	ディスクタイトル
//	type	b1-0	ディスクのメディアタイプ
//					00 = 2D, 01 = 2DD, 10 = 2HD
//
bool DiskManager::AddDisk(uint dr, const char* title, uint type)
{
	if (dr < max_drives)
	{
		if (drive[dr].holder && drive[dr].holder->AddDisk(title, type))
			return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
//	N88-BASIC 標準フォーマットを掛ける
//	豪快な方法で(^^;
//
bool DiskManager::FormatDisk(uint dr)
{
	if (!drive[dr].holder || drive[dr].disk.GetType() != FloppyDisk::MD2D)
		return false;
//	statusdisplay.Show(10, 5000, "Format drive : %d", dr);
	
	uint8* buf = new uint8[80*16*256];
	if (!buf)
		return false;

	// フォーマット
	memset(buf, 0xff, 80*16*256);
	// IPL
	buf[0] = 0xc9;
	// ID
	memset(&buf[0x25c00], 0, 256);
	buf[0x25c01] = 0xff;
	// FAT
	buf[0x25d4a] = 0xfe; buf[0x25d4b] = 0xfe;
	buf[0x25e4a] = 0xfe; buf[0x25e4b] = 0xfe;
	buf[0x25f4a] = 0xfe; buf[0x25f4b] = 0xfe;
	
	// 書き込み
	FloppyDisk& disk = drive[dr].disk;
	FloppyDisk::IDR id;
	id.n = 1;
	uint8* dest = buf;

	for (int t=0; t<80; t++)
	{
		id.c = t / 2, id.h = t & 1;

		disk.Seek(id.c * 4 + id.h);
		disk.FormatTrack(0, 0);

		for (int r=1; r<=16; r++)
		{
			id.r = r;

			FloppyDisk::Sector* sec = disk.AddSector(256);
			if (!sec)
				break;
			sec->id = id, sec->size = 256;
			sec->flags = 0x40;
			sec->gap3 = 54;
			memcpy(sec->image, dest, 256);
			dest += 256;
		}
	}
	drive->sizechanged = true;
	drive->modified[0] = true;
	delete[] buf;
	return true;
}

//	FDXサポート

// ---------------------------------------------------------------------------
//	ディスクイメージ (FDX 形式) を読み込む
//
bool DiskManager::ReadDiskImageFdx(FileIO* fio, Drive* drive)
{
	int t;
	FDX::fdxheader_t fdxheader;
	fio->Seek(0, FileIO::begin);

	// ヘッダー読み込み
	if (fio->Read(&fdxheader, 256) != 256) {
		statusdisplay.Show(70, 3000, "作業用領域を割り当てることができませんでした");
		return false;
	}

	// シグネイチャ確認
	if (fdxheader.signature[0] != 'F' ||
		fdxheader.signature[1] != 'D' ||
		fdxheader.signature[2] != 'X') {
		statusdisplay.Show(70, 3000, "FDX形式ではありません");
		return false;
	}

	// リビジョンは3のみ
	if (fdxheader.revision != 3) {
		statusdisplay.Show(70, 3000, "FDX形式のリビジョンは3のみをサポートします");
		return false;
	}

	// ヘッド数は2のみ
	if (fdxheader.heads != 2) {
		statusdisplay.Show(70, 3000, "FDX形式のヘッド数は2のみをサポートします");
		return false;
	}

	// ディスクのタイプチェック
	FloppyDisk::DiskType type;
	uint hd = 0;
	switch (fdxheader.type)
	{
	case FDX_TYPE_2D:
		type = FloppyDisk::MD2D;
		break;

	case FDX_TYPE_2DD: 
		type = FloppyDisk::MD2DD; 
		break;

	case FDX_TYPE_2HD: 
		type = FloppyDisk::MD2HD; 
		hd = FloppyDisk::highdensity; 
		break;

	case FDX_TYPE_RAW:
		// RAW形式は転送レートとRPM、シリンダ数から判断
		if (fdxheader.rate == 4000 && fdxheader.rpm == 300) {
			if (fdxheader.cylinders <= FDX_MAX_CYLINDER / 2) {
				type = FloppyDisk::MD2D;
			} else {
				type = FloppyDisk::MD2DD;
			}
			break;
		} else if (fdxheader.rate == 8000 && fdxheader.rpm == 360) {
			type = FloppyDisk::MD2HD;
			hd = FloppyDisk::highdensity; 
			break;
		}

	default:
		statusdisplay.Show(90, 3000, "サポートしていないメディアです");
		return false;
	}
	bool readonly = drive->holder->IsReadOnly() || fdxheader.writeprotect;
	
	FloppyDisk& disk = drive->disk;
	if (!disk.Init(type, readonly))
	{
		statusdisplay.Show(70, 3000, "作業用領域を割り当てることができませんでした");
		return false;
	}

	// trackptr データの保存
	for (t=0; t<fdxheader.cylinders * fdxheader.heads; t++)
	{
		drive->trackpos[t] = sizeof(FDX::fdxheader_t) + t * fdxheader.trackblk;
		drive->tracksize[t] = fdxheader.trackblk;
	}
	for (t=0; t<168; t++)
	{
		disk.Seek(t);
		disk.FormatTrack(0, 0);
	}

	// FDXヘッダー情報転記
	disk.fdx_header = fdxheader;

	// 各トラックの読み込み
	for (t=0; t<fdxheader.cylinders * fdxheader.heads; t++)
	{
		int cy = t >> 1;
		if (type == FloppyDisk::MD2D)
			cy *= 2;
		FloppyDisk::Track* ptrack = disk.Seek((cy * 2) + (t & 1));
		if (drive->trackpos[t])
		{
			// FDXのトラック情報を読み込む
			fio->Seek(drive->trackpos[t], FileIO::begin);
			FDX::fdxtrack_t* pft = (FDX::fdxtrack_t*)new uint8[fdxheader.trackblk];
			fio->Read(pft, fdxheader.trackblk);
			ptrack->fdx_track = pft;

			// インデックスオフセットの位置調整
			if (pft->index != 0) {
				FDX::bf_seek(pft->data, pft->length, pft->index);
				pft->index = 0;
			}

			FDX::fdxtrkstat_t *trkstat;

			// RAWデータは事前にエンコードデータに変換する
			if (fdxheader.type == FDX_TYPE_RAW) {
				// RAWデータは退避しておく
				ptrack->fdx_rawrate = fdxheader.rate;
				ptrack->fdx_rawdata = new uint8[(pft->length + 7) >> 3];
				memcpy(ptrack->fdx_rawdata, pft->data, (pft->length + 7) >> 3);
				ptrack->fdx_rawlen = pft->length;

				// RAW->エンコード
				pft->length = FDX::raw_to_encode(
					ptrack->fdx_rawdata, ptrack->fdx_rawlen,
					pft->data, pft->length);
			}
			
			// セクタ情報にデコード
			trkstat = FDX::analizetrack(pft->data, pft->length, false, true);

			for (int i = 0; i < trkstat->num; i++)
			{
				FDX::fdxsecstat_t *fdxs = &trkstat->sector[i];
				FloppyDisk::Sector* sec = disk.AddSector(fdxs->size);
				if (!sec)
					break;

				// M88のデータを準備
				sec->id.c = (uint8)fdxs->chrn[0];
				sec->id.h = (uint8)fdxs->chrn[1];
				sec->id.r = (uint8)fdxs->chrn[2];
				sec->id.n = (uint8)fdxs->chrn[3];
				sec->size = fdxs->size;
				if (fdxs->type == FDX_SEC_MFM) {
					sec->flags = 0x40;
				} else {
					sec->flags = 0x00;
				}

				sec->flags |= hd;
				if (fdxs->err & FDX_FDD_DDAM)
					sec->flags |= FloppyDisk::deleted;

				if (fdxs->err & FDX_FDD_IDCRC)
					sec->flags |= FloppyDisk::idcrc;

				if (fdxs->err & FDX_FDD_DATACRC)
					sec->flags |= FloppyDisk::datacrc;

				if (fdxs->err & FDX_FDD_MDAM)
					sec->flags |= FloppyDisk::mam;

				sec->gap3 = fdxs->gap3;

				memcpy(sec->image, fdxs->data, fdxs->size);

				// FDXのデータを準備
				if (fdxs->err & FDX_FDD_DATACRC) {
					sec->fdx_encdata = new uint8[(fdxs->enclen + 7) >> 3];
					memcpy(sec->fdx_encdata, fdxs->encdata, (fdxs->enclen + 7) >> 3);
					sec->fdx_enclen = fdxs->enclen;
				}

				// FDXのトラックデータ(エンコード)の位置は必ず設定しておく
				sec->fdx_ioffset = fdxs->idamoffset;
				sec->fdx_doffset = fdxs->damoffset;
				sec->fdx_eoffset = fdxs->eosoffset;

				// 更新なしにする
				sec->update = false;
			}

			// 解析用ワーク解放
			FDX::release_analyzeobject(trkstat);
		}

		// フォーマットフラグをオフにする
		disk.GetTrack()->formated = false;
	}

	return true;
}

// ---------------------------------------------------------------------------
//	ディスクイメージの書き出し(FDX)
//	必要となる領域はあらかじめ確保されていることとする
//
bool DiskManager::WriteDiskImageFdx(FileIO* fio, Drive* drv)
{
	if (!fio->Seek(0, FileIO::begin))
		return false;
	if (fio->Write(&drv->disk.fdx_header, sizeof(FDX::fdxheader_t)) != sizeof(FDX::fdxheader_t))
		return false;

	int ntracks = drv->disk.fdx_header.cylinders * drv->disk.fdx_header.heads;
	for (int t=0; t<ntracks; t++)
	{
		int tr = (drv->disk.GetType() == FloppyDisk::MD2D) ? t & ~1 : t >> 1;
		tr = (tr << 1) | (t & 1);
		if (!drv->modified[tr]) continue;

		if (!WriteTrackImageFdx(fio, drv, tr))
			return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
//	トラック一つ分のイメージをかく(FDX)
//
bool DiskManager::WriteTrackImageFdx(FileIO* fio, Drive* drv, int t)
{
	// ヘッダとトラック数×ブロックサイズでシーク
	int fpos = (drv->disk.GetType() == FloppyDisk::MD2D) ? (t >> 1) | (t & 1) : t;
	fpos *= drv->disk.fdx_header.trackblk;
	fpos += sizeof(FDX::fdxheader_t);
	if (!fio->Seek(fpos, FileIO::begin))
		return false;

	// ワーク確保
	uint8 *buf = new uint8[FDX_ENC_BYTES];
	if (!buf) {
		return false;
	}

	uint8 *enc = new uint8[FDX_ENC_BYTES];
	if (!enc) {
		return false;
	}

	// セクタがなければアンフォーマット
	FloppyDisk::Track* track = drv->disk.Seek(t);
	FloppyDisk::Sector* sec = drv->disk.GetFirstSector(t);
	if (!sec) {
		memset(track->fdx_track->data, 0x00, (track->fdx_track->length + 7) >> 3);
		goto save_exit;
	}

	// 有効なセクタを含む様に新たにフォーマットされたか
	bool format = drv->disk.GetTrack()->formated;
	bool mfm = sec->flags & 0x40 ? true : false;

	if (format) {
		// フォーマットされている->トラック全体を作成
		uint8 *p = track->fdx_track->data;
		int last = 1;

		// プリアンプル
		if (mfm) {
			p += FDX::fill_mfm(p, 0x4e, 80, &last);			/* GAP4a */

			p += FDX::fill_mfm(p, 0x00, 12, &last);			/* SYNC */
			p += FDX::gen_iam(p, true);						/* IAM */
			last = 0;

			p += FDX::fill_mfm(p, 0x4e, 50, &last);			/* GAP1 */
		} else {
			p += FDX::fill_fm(p, 0xff, 40);					/* GAP4a */

			p += FDX::fill_fm(p, 0x00, 6);					/* SYNC */
			p += FDX::gen_iam(p, false);					/* IAM */

			p += FDX::fill_fm(p, 0xff, 26);					/* GAP1 */
		}

		// セクタループ
		for (sec = drv->disk.GetFirstSector(t); sec; sec = sec->next)
		{
			buf[0] = 0xa1;
			buf[1] = 0xa1;
			buf[2] = 0xa1;
			buf[3] = 0xfe;
			buf[4] = sec->id.c;
			buf[5] = sec->id.h;
			buf[6] = sec->id.r;
			buf[7] = sec->id.n;
			uint16 crc;
			if (mfm) {
				crc = FDX::gen_crc(buf, 8);
			} else {
				crc = FDX::gen_crc(&buf[3], 5);
			}
			buf[8] = (uint8)(crc >> 8);
			buf[9] = (uint8)crc;

			if (mfm) {
				p += FDX::fill_mfm(p, 0x00, 12, &last);		/* SYNC */
				p += FDX::gen_idam(p, true);				/* IDAM */
				last = 0;
				p += FDX::gen_mfm(p, &buf[4], 4, &last);	/* CHRN */
				p += FDX::gen_mfm(p, &buf[8], 2, &last);	/* CRC */

				p += FDX::fill_mfm(p, 0x4e, 22, &last);		/* GAP2 */
			} else {
				p += FDX::fill_fm(p, 0x00, 6);				/* SYNC */
				p += FDX::gen_idam(p, false);				/* IDAM */

				p += FDX::gen_fm(p, &buf[4], 4);			/* CHRN */
				p += FDX::gen_fm(p, &buf[8], 2);			/* CRC */

				p += FDX::fill_fm(p, 0xff, 11);				/* GAP2 */
			}

			bool del = sec->flags & FloppyDisk::deleted ? true : false;
			buf[0] = 0xa1;
			buf[1] = 0xa1;
			buf[2] = 0xa1;
			buf[3] = del ? 0xf8 : 0xfb;
			memcpy(&buf[4], sec->image, sec->size);

			if (mfm) {
				crc = FDX::gen_crc(buf, 4 + sec->size);
			} else {
				crc = FDX::gen_crc(&buf[3], 1 + sec->size);
			}

			buf[4 + sec->size    ] = (uint8)(crc >> 8);
			buf[4 + sec->size + 1] = (uint8)crc;

			int gap3 = 0;

			if (mfm) {
				p += FDX::fill_mfm(p, 0x00, 12, &last);				/* SYNC */
				p += FDX::gen_dam(p, del, true);					/* DAM */
				last = del ? 0 : 1;

				p += FDX::gen_mfm(p, &buf[4], sec->size, &last);	/* DATA */
				p += FDX::gen_mfm(p, &buf[4 + sec->size], 2, &last);/* CRC */

				gap3 = sec->gap3 == 0 ? 32 : sec->gap3;				/* GAP3 */
				p += FDX::fill_mfm(p, 0x4e, gap3, &last);
			} else {
				p += FDX::fill_fm(p, 0x00, 6);						/* SYNC */
				p += FDX::gen_dam(p, del, false);					/* DAM */

				p += FDX::gen_fm(p, &buf[4], sec->size);			/* DATA */
				p += FDX::gen_fm(p, &buf[4 + sec->size], 2);		/* CRC */

				gap3 = sec->gap3 == 0 ? 16 : sec->gap3;				/* GAP3 */
				p += FDX::fill_fm(p, 0xff, gap3);
			}
		}

		int gap4b = track->fdx_track->length;
		gap4b /= 8;
		gap4b -= (p - track->fdx_track->data);
		if (mfm) {
			p += FDX::fill_mfm(p, 0x4e, gap4b / 2, &last);	/* GAP4b */
		} else {
			p += FDX::fill_fm(p, 0xff, gap4b /4);			/* GAP4b */
		}
	} else {
		// フォーマットされていない->セクタデータを置き換え
		int last = 1;

		// セクタループ
		for (sec = drv->disk.GetFirstSector(t); sec; sec = sec->next)
		{
			if (!sec->update) {
				continue;
			}

			bool del = sec->flags & FloppyDisk::deleted ? true : false;
			buf[0] = 0xa1;
			buf[1] = 0xa1;
			buf[2] = 0xa1;
			buf[3] = del ? 0xf8 : 0xfb;
			memcpy(&buf[4], sec->image, sec->size);

			uint16 crc;
			if (mfm) {
				crc = FDX::gen_crc(buf, 4 + sec->size);
			} else {
				crc = FDX::gen_crc(&buf[3], 1 + sec->size);
			}

			buf[4 + sec->size    ] = (uint8)(crc >> 8);
			buf[4 + sec->size + 1] = (uint8)crc;

			uint8 *p = enc;
			if (mfm) {
				p += FDX::fill_mfm(p, 0x00, 12, &last);				/* SYNC */
				p += FDX::gen_dam(p, del, true);					/* DAM */
				last = del ? 0 : 1;

				p += FDX::gen_mfm(p, &buf[4], sec->size, &last);	/* DATA */
				p += FDX::gen_mfm(p, &buf[4 + sec->size], 2, &last);/* CRC */
			} else {
				p += FDX::fill_fm(p, 0x00, 6);						/* SYNC */
				p += FDX::gen_dam(p, del, false);					/* DAM */

				p += FDX::gen_fm(p, &buf[4], sec->size);			/* DATA */
				p += FDX::gen_fm(p, &buf[4 + sec->size], 2);		/* CRC */
			}

			if (mfm) {
				FDX::bf_setbf(track->fdx_track->data, track->fdx_track->length,
					sec->fdx_ioffset + (4 + 4 + 2 + 22) * 2 * 8,
					enc, (12 + 4 + sec->size + 2) * 2 * 8);
			} else {
				FDX::bf_setbf(track->fdx_track->data, track->fdx_track->length,
					sec->fdx_ioffset + (1 + 4 + 2 + 11) * 4 * 8,
					enc, (6 + 1 + sec->size + 2) * 4 * 8);
			}
		}
	}

save_exit:
	// ワーク解放
	delete[] buf;
	delete[] enc;

	// ワーク作成
	uint8 *savedata;
	uint8 *rawdata = NULL;

	// RAWデータ変換
	if (drv->disk.fdx_header.type == FDX_TYPE_RAW) {
		// RAWデータ長
		int rawlen = FDX_RAW_BYTES << 3;

		// ワーク確保
		try {
			rawdata = new uint8[FDX_RAW_BYTES];
		}
		catch (...) {
			return false;
		}
		if (!rawdata) {
			return false;
		}

		// エンコードデータ->RAWデータ
		track->fdx_track->length = FDX::encode_to_raw(
			track->fdx_track->data, track->fdx_track->length,
			rawdata, rawlen);
		savedata = rawdata;
	} else {
		savedata =track->fdx_track->data;
	}

	// トラックヘッダ書き込み
	if (fio->Write(track->fdx_track, sizeof(int) * 4) != sizeof(int) * 4) {
		if (rawdata) {
			delete[] rawdata;
		}
		return false;;
	}

	// トラックデータ書き込み
	int blocklen = drv->disk.fdx_header.trackblk;
	if (fio->Write(savedata, blocklen - sizeof(int) * 4) != blocklen - sizeof(int) * 4) {
		if (rawdata) {
			delete[] rawdata;
		}
		return false;
	}

	// ワーク解放
	if (rawdata) {
		delete[] rawdata;
	}

	// 終了
	return true;
}
