// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator.
//	Copyright (C) cisc 1998, 1999.
// ---------------------------------------------------------------------------
//	$Id: fdu.cpp,v 1.5 1999/07/29 14:35:31 cisc Exp $

#include "headers.h"

#include "misc.h"
#include "fdc.h"
#include "fdu.h"
#include "diskmgr.h"

using namespace PC8801;

// ---------------------------------------------------------------------------
//	構築・破棄
//
FDU::FDU()
{
	disk = 0;
	cyrinder = 0;
}

FDU::~FDU()
{
	if (disk)
		Unmount();
}

// ---------------------------------------------------------------------------
//	初期化
//
bool FDU::Init(DiskManager* dm, int dr, Scheduler *s)
{
	scheduler = s;
	diskmgr = dm;
	drive = dr;
	return true;
}

// ---------------------------------------------------------------------------
//	FDU::Mount
//	イメージを割り当てる（＝ドライブにディスクを入れる）
//
bool FDU::Mount(FloppyDisk* fd)
{
	disk = fd;
	return true;
}

// ---------------------------------------------------------------------------
//	FDU::Unmount
//
bool FDU::Unmount()
{
	disk = 0;
	return true;
}

// ---------------------------------------------------------------------------
//	FDU::GetCurrentPos
//
uint FDU::GetCurrentPos()
{
	if (!disk) {
		return 0;
	}

	uint capacity = disk->GetTrackCapacity();
	uint pos = (uint)scheduler->GetTime();
	if (disk->GetType() == FloppyDisk::DiskType::MD2HD) {
		// RPM 360(単位は0.01ms)
		pos %= 16600;
		pos *= capacity;
		pos /= 16600;
	} else {
		// RPM 300(単位は0.01ms)
		pos %= 20000;
		pos *= capacity;
		pos /= 20000;
	}

	return pos;
}

// ---------------------------------------------------------------------------
//	FDU::Bytes2Time
//
uint FDU::Bytes2Time(uint len)
{
	if (!disk) {
		return 0;
	}

	// 10us単位に変換
	if (disk->GetType() == FloppyDisk::DiskType::MD2HD) {
		// 1000kbps
		len *= 16;
		len *= 1000;
		len /= 10000;
	} else {
		// 500kbps
		len *= 16;
		len *= 1000;
		len /= 5000;
	}

	return len;
}

// ---------------------------------------------------------------------------
//	FDU::SetHead
//	ヘッドの指定
//
inline void FDU::SetHead(uint hd)
{
	if (!disk) {
		return;
	}

	head = hd & 1;
	track = (cyrinder << 1) + (hd & 1);
	disk->Seek(track);
}

// ---------------------------------------------------------------------------
//	FDU::Seek
//	指定されたシリンダー番号へシークする
//
//	cyrinder シーク先
//
uint FDU::Seek(uint cy)
{
	cyrinder = cy;
	return 0;
}

// ---------------------------------------------------------------------------
//	FDU::NextSector
//	次のセクタを取得する
//
FloppyDisk::Sector* FDU::NextSector(uint flags)
{
	if (!disk) {
		return 0;
	}

	for (int i = disk->GetNumSectors(); i > 0; i--) {
		FloppyDisk::Sector* sector = disk->GetSector();
		if (!sector) {
			disk->IndexHole();
			sector = disk->GetSector();
		}

		if ((flags & 0xc0) == (sector->flags & 0xc0)) {
			return sector;
		}
	}

	disk->IndexHole();
	return 0;
}

// ---------------------------------------------------------------------------
//	FDU::SearchID
//	セクタを検索する
//
uint FDU::SearchID(uint flags, IDR *id, bool idmatch, bool ignorecrc, int *stime)
{
	uint result = 0;
	int num;
	bool find = false;

	if (!disk)
		return FDC::ST0_AT | FDC::ST1_MA;

	// 時間情報をクリア
	*stime = 0;

	// セクタ検索
	sector = 0;
	num = disk->GetNumSectors();
	while (num--) {
		// 次のセクタを取得
		sector = NextSector(flags);
		if (!sector) {
			break;
		}

		if (idmatch) {
			// IDが一致するセクタを発見
			if (*id == sector->id) {
				find = true;
				result = 0;

				// IDCRCが発生しているセクタならデータエラーで中断(ST2_DDはネゲート)
				if (!ignorecrc && sector->flags & FloppyDisk::idcrc) {
					result = FDC::ST0_AT | FDC::ST1_DE;
				}
				break;
			} else {
				if (id->c != sector->id.c) {
					if (sector->id.c == 0xff) {
						result |= FDC::ST2_BC;
					} else {
						result |= FDC::ST2_NC;
					}
				}
			}
		} else {
			// CRCエラーが発生していないセクタ発見
			if (ignorecrc || !(sector->flags & FloppyDisk::idcrc)) {
				*id = sector->id;
				find = true;
				result = 0;
				break;
			}
		}
	}

	// ID発見
	if (find) {
		// このセクタまでの時間
		if (disk->IsFdx() && !sector->update) {
			uint pos = GetCurrentPos();
			*stime = sector->fdx_ioffset +
				((sector->flags & 0x40) ? 10 * 16 : 7 * 32);
			*stime = (*stime >> 4) - pos;
			if (*stime < 0) { *stime += disk->GetTrackCapacity(); }
			*stime = Bytes2Time(*stime);
		}
		return result;
	}

	// IDが見つからなかった
	if (!sector) {
		// 密度にあったセクタがない
		result = FDC::ST0_AT | FDC::ST1_MA;
	} else {
		// ノーデータ
		result |= FDC::ST0_AT | FDC::ST1_ND;
	}

	// セクタが無ければインデックスホールを2回検出するまで検索した
	uint pos = GetCurrentPos();
	*stime = Bytes2Time(disk->GetTrackCapacity() * 2 - pos);
	return result;
}

// ---------------------------------------------------------------------------
//	FDU::ReadSector
//	セクタを読む
//
//	head	ヘッド番号
//	id		読み込むセクタのセクタ ID
//	data	データの転送先
//	size	転送サイズ
//	stime	転送開始までの時間(us単位)
//	txtime	転送時間(us単位)
//
uint FDU::ReadSector(uint flags, IDR id, uint8* data, int *size, int *stime, int *txtime)
{
	// 時間をクリア
	*stime = 0;
	*txtime = 0;

	if (!disk)
		return FDC::ST0_AT | FDC::ST1_MA;
	SetHead(flags);

	// セクタを探す
	uint result = SearchID(flags, &id, true, false, stime);
	if (result) {
		return result;
	}

	// MAMが発生しているセクタならミッシングアドレスマークで中断
	if (sector->flags & FloppyDisk::mam) {
		return FDC::ST0_AT | FDC::ST1_MA | FDC::ST2_MD;
	}

	// FDX拡張
	// データの位置(DAM/DDAMをスキップ)
	if (disk->IsFdx() && !sector->update) {
		int offset = sector->fdx_doffset;
		if (sector->flags & 0x40) {
			offset += 64;
		} else {
			offset += 32;
		}

		// データの開始位置までの時間
		uint pos = GetCurrentPos();
		*stime = (offset >> 4) - pos;
		if (*stime < 0) {
			*stime += disk->GetTrackCapacity();
		}
		*stime = Bytes2Time(*stime);
	}

	// FDX拡張
	// セクタが更新されておらずCRCエラーが発生していれば
	// クロック分離して返す(不安定データも再現する)
	if (disk->IsFdx() && !sector->update && (sector->flags & FloppyDisk::datacrc)) {
		FDX::bf_splitclock(
			sector->fdx_encdata, sector->fdx_enclen, 0,
			data, sector->size,
			sector->flags & 0x40 ? true : false, true);

		// 転送サイズ、転送時間は無効
		*size = sector->size;
	} else {
		// 転送サイズ、転送時間は無効
		*size = Min(0x2000, sector->size);

		// 単純コピー
		memcpy(data, sector->image, *size);
	}

	// 転送に必要な時間
	*txtime = *size * ((sector->flags & 0x40) ? 1 : 2);
	*txtime = Bytes2Time(*txtime);

	// FDX拡張
	// 正確な転送時間を返す(ウェーブ再現)
	FloppyDisk::Track *track = disk->GetTrack();
	if (disk->IsFdx() && track->fdx_rawdata && !sector->update && !(sector->flags & FloppyDisk::mam)) {
		int start = FDX::encode_to_raw_pos(
			track->fdx_rawdata, track->fdx_rawlen,
			sector->fdx_doffset + FDX_CELL_BITS * ((sector->flags & 0x40) ? 4 : 1));
		int end = FDX::encode_to_raw_pos(
			track->fdx_rawdata, track->fdx_rawlen,
			sector->fdx_eoffset - FDX_CELL_BITS * 2);
		if (start >= 0 && end >= 0) {
			int elapse = end - start;
			if (elapse < 0) {
				elapse += track->fdx_rawlen;
			}
			elapse >>= 7;
			*txtime = Bytes2Time(elapse);
		}
	}

	if (sector->flags & FloppyDisk::datacrc) {
		return FDC::ST0_AT | FDC::ST1_DE | FDC::ST2_DD;
	}

	if (sector->flags & FloppyDisk::deleted) {
		return FDC::ST2_CM;
	}

	return 0;
}

// ---------------------------------------------------------------------------
//	FindID	
//
uint FDU::FindID(uint flags, IDR id, int size, int *stime, int *txtime)
{
	// 時間をクリア
	*stime = 0;
	*txtime = 0;

	if (!disk)
		return FDC::ST0_AT | FDC::ST1_MA;
	SetHead(flags);

	if (disk->IsReadOnly())
		return FDC::ST0_AT | FDC::ST1_NW;

	// セクタを探す
	uint result = SearchID(flags, &id, true, false, stime);
	if (result) {
		return result;
	}

	// データ転送時間
	*txtime = size * ((sector->flags & 0x40) ? 1 : 2);
	*txtime = Bytes2Time(*txtime);
	return 0;
}

// ---------------------------------------------------------------------------
//	FDU::WriteSector
//	セクタに書く
//
uint FDU::WriteSector(uint flags, IDR id, const uint8* data, bool deleted)
{
	if (!disk)
		return 0;

	uint writesize = 0x80 << Min(7, id.n);

	if (writesize > sector->size)
	{
		if (!disk->Resize(sector, writesize))
			return 0;

		// フォーマットされたことにする
		disk->GetTrack()->formated = true;
	}

	memcpy(sector->image, data, writesize);
	sector->flags = (flags & 0xc0) | (deleted ? FloppyDisk::deleted : 0);
	sector->size = writesize;
	sector->update = true;
	diskmgr->Modified(drive, track);
	return 0;
}

// ---------------------------------------------------------------------------
//	FDU::SenceDeviceStatus
//	デバイス・スタータスを得る
//
uint FDU::SenceDeviceStatus()
{
	uint result = 0x20 | (cyrinder ? 0 : 0x10) | (head ? 4 : 0);
	if (disk)
		result |= disk->IsReadOnly() ? 0x48 : 8;
	return result;
}

// ---------------------------------------------------------------------------
//	FDU::ReadID
//	セクタを一個とってくる
//
uint FDU::ReadID(uint flags, IDR* id, int *stime)
{
	// 時間をクリア
	*stime = 0;

	if (!disk)
		return FDC::ST0_AT | FDC::ST1_MA;
	SetHead(flags);

	// セクタを探す
	return SearchID(flags, id, false, false, stime);
}

// ---------------------------------------------------------------------------
//	FDU::SearchIndex
//	インデックスホールを検索する
//
uint FDU::SearchIndex(uint flags, int *stime)
{
	// 時間をクリア
	*stime = 0;

	if (!disk)
		return FDC::ST0_AT | FDC::ST1_MA;
	SetHead(flags);

	// インデックホール直後に移動
	disk->IndexHole();

	// 次のインデックスホールまでの時間
	uint pos = GetCurrentPos();
	*stime = Bytes2Time(disk->GetTrackCapacity() - pos);
	return 0;
}

// ---------------------------------------------------------------------------
//	ReadDiag
//
uint FDU::ReadDiag(uint flags, uint8* data, uint* size, IDR idr, int *stime, int *txtime)
{
	if (!disk)
		return FDC::ST0_AT | FDC::ST1_MA;

	// FDX拡張
	if (disk->IsFdx() && !disk->GetTrack()->formated) {
		return ReadDiagFdx(flags, data, size, idr, stime, txtime);
	}

	// 時間情報をクリア
	*stime = 0;
	*txtime = 0;

	// 次のIDを探す
	IDR id;
	uint result = SearchID(flags, &id, false, true, stime);
	if (result) {
		return result;
	}

	// データ無し
	if (sector->flags & FloppyDisk::mam) {
		return FDC::ST0_AT | FDC::ST1_MA | FDC::ST2_MD;
	}

	// ID比較(何故かR=1で比較するようだ)
	if (sector->id.r != 1 || sector->id.n != idr.n) {
		result |= FDC::ST1_ND;
	}

	// IDCRC発生
	if (sector->flags & FloppyDisk::idcrc) {
		result |= FDC::ST1_DE;
	}

	// セクタサイズと同じならセクタのDATACRCエラーを反映
	if (sector->id.n == idr.n) {
		if (sector->flags & FloppyDisk::datacrc) {
			result |= FDC::ST1_DE | FDC::ST2_DD;
		}
	}

	// セクタサイズと異なるならDATACRCがエラーとなるはず
	if (sector->id.n != idr.n) {
		result |= FDC::ST1_DE | FDC::ST2_DD;
	}

	// DELETEDセクタ
	if (sector->flags & FloppyDisk::deleted) {
		result |= FDC::ST2_CM;
	}

	uint capacity = disk->GetTrackCapacity();
	if ((flags & 0x40) == 0) capacity >>= 1;

	FloppyDisk::Sector* sec = disk->GetFirstSector(track);
	uint8 buf[0x4000];
	uint8* dest = buf;
	uint8* limit = dest + capacity;
	uint startpos = 0;
	uint endpos = 0;
	FloppyDisk::Sector* lastsec = 0;

	// プリアンプル
	if (flags & 0x40)
	{	// MFM
		memset(dest, 0x4e, 80);				// GAP4a
		memset(dest + 80, 0x00, 12);		// Sync
		dest[92] = 0xc2; dest[93] = 0xc2;	// IAM
		dest[94] = 0xc2; dest[95] = 0xfc;
		memset(dest + 96, 0x4e, 50);		// GAP1
		dest += 146;
	} else
	{	// FM
		memset(dest, 0xff, 40);				// GAP4a
		memset(dest + 40, 0x00, 6);			// Sync
		dest[46] = 0xfc;					// IAM
		memset(dest + 47, 0xff, 26);		// GAP1
		dest += 73;
	}

	for (; sec && dest < limit; sec = sec->next)
	{
		if (((flags ^ sec->flags) & 0xc0) == 0)
		{
			// tracksize = 94/49 + secsize
			// ID
			if (flags & 0x40)
			{	// MFM
				memset(dest, 0, 12);			// SYNC
				dest[12] = 0xa1; dest[13] = 0xa1; // IDAM
				dest[14] = 0xa1; dest[15] = 0xfe;
				dest[16] = sec->id.c; dest[17] = sec->id.h;
				dest[18] = sec->id.r; dest[19] = sec->id.n;
				memset(dest + 20, 0x4e, 22 + 2);	// CRC+GAP2
				memset(dest + 44, 0, 12);			// SYNC
				dest[56] = 0xa1; dest[57] = 0xa1; // IDAM
				dest[58] = 0xa1; dest[59] = sec->flags & FloppyDisk::deleted ? 0xf8 : 0xfb;
				dest += 60;
			} else
			{	// FM
				memset(dest, 0, 6);			// SYNC
				dest[6] = 0xfe;					// IDAM
				dest[7] = sec->id.c; dest[8] = sec->id.h;
				dest[9] = sec->id.r; dest[10] = sec->id.n;
				memset(dest + 11, 0xff, 11 + 2);	// CRC+GAP2
				memset(dest + 24, 0, 6);			// SYNC
				dest[30] = sec->flags & FloppyDisk::deleted ? 0xf8 : 0xfb;
				dest += 31;
			}

			// Diagデータの開始点
			if (sec == sector) {
				startpos = dest - buf;
			}

			memcpy(dest, sec->image, sec->size);
			dest += sec->size;
			if (flags & 0x40)
				memset(dest, 0x4e, 2 + 0x20), dest += 0x22;
			else
				memset(dest, 0xff, 2 + 0x10), dest += 0x12;
		} else
		{
			// Diagデータの開始点
			if (sec == sector) {
				startpos = dest - buf;
			}

			if (flags & 0x40)
			{
				memset(dest, 0, (49 + sec->size) * 2);
				dest += (49 + sec->size) * 2;
			} else
			{
				memset(dest, 0, (94 + sec->size) / 2);
				dest += (94 + sec->size) / 2;
			}
		}

		// 最終セクタを確保
		lastsec = sec;
	}

	if (dest < limit)
	{
		memset(dest, (flags & 0x40) ? 0x4e : 0xff, limit - dest);
	}

	// 転送(最大二周する可能性がある)
	uint rest = *size;
	uint offset = startpos;
	dest = data;
	while (rest--) {
		*dest++ = buf[offset++];
		if (offset >= capacity) {
			offset = 0;
		}
	}

	// データ転送時間
	*txtime = *size * ((sector->flags & 0x40) ? 1 : 2);
	*txtime = Bytes2Time(*txtime);

	// Diagデータの完了時点のセクタを算出
	// この時点で最終セクタを指している
	endpos = startpos + *size;
	while (endpos >= capacity) {
		endpos -= capacity;
	}

	offset = (flags & 0x40) ? 146 : 73;
	if (endpos > offset) {
		sec = disk->GetFirstSector(track);
		for (; sec; sec = sec->next) {
			if (((flags ^ sec->flags) & 0xc0) == 0) {
				if (flags & 0x40) {
					offset += 60;
				} else {
					offset += 31;
				}

				offset += sec->size;

				if (flags & 0x40) {
					offset += 0x22;
				} else {
					offset += 0x12;
				}
			} else {
				if (flags & 0x40) {
					offset += (49 + sec->size) * 2;
				} else {
					offset += (94 + sec->size) / 2;
				}
			}

			lastsec = sec;

			if (offset > endpos) {
				break;
			}
		}
	}

	// 現在のセクタに移動
	while (true) {
		sector = NextSector(flags);
		if (sector == lastsec) {
			break;
		}
	}

	return result;
}

// ---------------------------------------------------------------------------
//	ReadDiag 用のデータ作成(FDX形式)
//
uint FDU::ReadDiagFdx(uint flags, uint8* data, uint* size, IDR idr, int *stime, int *txtime)
{
	if (!disk)
		return FDC::ST0_AT | FDC::ST1_MA;

	// 時間情報をクリア
	*stime = 0;
	*txtime = 0;

	// 次のIDを探す
	IDR id;
	uint result = SearchID(flags, &id, false, true, stime);
	if (result) {
		return result;
	}

	// ID比較(何故かR=1で比較するようだ)
	if (sector->id.r != 1 || sector->id.n != idr.n) {
		result |= FDC::ST1_ND;
	}

	// IDCRC発生
	if (sector->flags & FloppyDisk::idcrc) {
		result |= FDC::ST1_DE;
	}

	// データ無し
	if (sector->flags & FloppyDisk::mam) {
		result |= FDC::ST0_AT | FDC::ST1_MA | FDC::ST2_MD;
		return result;
	}

	// セクタサイズと同じならセクタのDATACRCエラーを反映
	if (sector->id.n == idr.n) {
		if (sector->flags & FloppyDisk::datacrc) {
			result |= FDC::ST1_DE | FDC::ST2_DD;
		}
	}

	// セクタサイズと異なるならDATACRCがエラーとなるはず
	if (sector->id.n != idr.n) {
		result |= FDC::ST1_DE | FDC::ST2_DD;
	}

	// DELETEDセクタ
	if (sector->flags & FloppyDisk::deleted) {
		result |= FDC::ST2_CM;
	}

	// データの位置(DAM/DDAMをスキップ)
	int offset = sector->fdx_doffset;
	if (sector->flags & 0x40) {
		offset += 64;
	} else {
		offset += 32;
	}

	// データの開始位置までの時間
	uint pos = GetCurrentPos();
	*stime = (offset >> 4) - pos;
	if (*stime < 0) {
		*stime += disk->GetTrackCapacity();
	}
	*stime = Bytes2Time(*stime);

	// バッファの制限有り
	if (*size > 0x4000) {
		*size = 0x4000;
	}

	// エンコードデータから変換
	FDX::fdxtrack_t *fdxt = disk->GetTrack()->fdx_track;
	FDX::bf_splitclock(
		fdxt->data, fdxt->length, offset, data,
		*size, flags & 0x40 ? true : false, true);

	// データ転送時間
	*txtime = *size * ((sector->flags & 0x40) ? 1 : 2);
	*txtime = Bytes2Time(*txtime);

	// 変換終了位置のセクタに強制的に移動
	offset += *size * (flags & 0x40 ? 16 : 32);

	// 現在位置の次のセクタを探して現在のセクタを決める
	FloppyDisk::Sector* lastsec = disk->GetFirstSector(track);
	FloppyDisk::Sector* nextsec = lastsec->next;
	while (nextsec) {
		if (nextsec->fdx_ioffset > offset) {
			break;
		}
		lastsec = nextsec;
		nextsec = nextsec->next;
	}

	// 現在のセクタに移動
	while (true) {
		sector = NextSector(flags);
		if (sector == lastsec) {
			break;
		}
	}

	return result;
}

// ---------------------------------------------------------------------------
//	FDU::WritePreamble
//
uint FDU::WritePreamble(uint flags, int *txtime)
{
	if (!disk)
		return FDC::ST0_AT | FDC::ST1_MA;

	// 時間をクリア
	*txtime = 0;

	// トラック更新有り
	diskmgr->Modified(drive, track);

	// セクタを全て消す
	if (!disk->FormatTrack(0, 0))
	{
		return FDC::ST0_AT | FDC::ST0_EC;
	}

	// プリアンプル
	uint len;
	if (flags & 0x40)
	{	// MFM
		len = 80 + 12 + 4 + 50;
	} else
	{	// FM
		len = 40 + 6 + 1 + 26;
	}

	// FMは2倍
	if (!(flags & 0x40)) {
		len *= 2;
	}

	// 転送時間
	*txtime = Bytes2Time(len);
	return 0;
}

// ---------------------------------------------------------------------------
//	FDU::WriteID
//
uint FDU::WriteID(uint flags, WIDDESC* wid, int *txtime)
{
	if (!disk)
		return 0;

	FloppyDisk::Sector* sec = disk->AddSector(0x80 << Min(7, wid->n));
	sec->id = *wid->idr;
	sec->flags = flags & 0xc0;
	sec->gap3 = wid->gpl;
	sec->update = true;
	memset(sec->image, wid->d, sec->size);

	// 書き込むまでの所要時間
	uint len;
	if (flags & 0x40)
	{	// MFM
		len = 12 + 4 + 4 + 2 + 22 + 12 + 4 + 2;
	} else
	{	// FM
		len = 6 + 1 + 4 + 2 + 11 + 6 + 1 + 2;
	}
	len += sec->size + sec->gap3;

	// FMは2倍
	if (!(flags & 0x40)) {
		len *= 2;
	}

	*txtime = Bytes2Time(len);
	return 0;
}

// ---------------------------------------------------------------------------
//	FDU::WritePostamble
//
uint FDU::WritePostamble(uint flags, int *txtime)
{
	if (!disk)
		return 0;

	// プリアンプル
	uint len;
	if (flags & 0x40)
	{	// MFM
		len = 80 + 12 + 4 + 50;	
	} else
	{	// FM
		len = 40 + 6 + 1 + 26;
	}

	// セクタ
	FloppyDisk::Sector* sec = disk->GetFirstSector(track);
	for (; sec; sec = sec->next)
	{
		if (flags & 0x40)
		{	// MFM
			len += 12 + 4 + 4 + 2 + 22 + 12 + 4 + 2;
		} else
		{	// FM
			len += 6 + 1 + 4 + 2 + 11 + 6 + 1 + 2;
		}

		len += sec->size + sec->gap3;
	}

	// FMは2倍
	if (!(flags & 0x40)) {
		len *= 2;
	}

	// ポストアンブルの転送時間
	uint limit = disk->GetTrackCapacity();
	if (len < limit)
	{
		// トラックの容量までGAPで埋める時間
		*txtime = Bytes2Time(limit - len);
	} else {
		// トラックの容量を超えたのでアンフォーマットにする
		disk->FormatTrack(0, 0);
		*txtime = 0;
	}

	disk->IndexHole();
	return 0;
}
