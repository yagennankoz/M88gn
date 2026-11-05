// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator.
//	Copyright (C) cisc 1998, 1999.
// ---------------------------------------------------------------------------
//	$Id: floppy.cpp,v 1.4 1999/07/29 14:35:31 cisc Exp $

#include "headers.h"
#include "floppy.h"

// ---------------------------------------------------------------------------
//	構築
//
FloppyDisk::FloppyDisk()
{
	ntracks = 0;
	curtrack = 0;
	cursector = 0;
	curtracknum = ~0;
	readonly = false;
	type = MD2D;
}

FloppyDisk::~FloppyDisk()
{
}

// ---------------------------------------------------------------------------
//	初期化
//
bool FloppyDisk::Init(DiskType _type, bool _readonly)
{
	static const int trtbl[] = { 84, 164, 164 };
	
	type = _type;
	readonly = _readonly;
	ntracks = trtbl[type];

	curtrack = 0;
	cursector = 0;
	curtracknum = ~0;

	// FDX拡張
	memset(&fdx_header, 0x00, sizeof(FDX::fdxheader_t));
	for (int i = 0; i < 168; i++) {
		if (tracks[i].fdx_track) {
			delete tracks[i].fdx_track;
		}
		if (tracks[i].fdx_rawdata) {
			delete[] tracks[i].fdx_rawdata;
		}

		tracks[i].fdx_track = 0;
		tracks[i].fdx_rawlen = 0;
		tracks[i].fdx_rawdata = 0;
	}

	return true;
}

// ---------------------------------------------------------------------------
//	指定のトラックにシーク
//
FloppyDisk::Track* FloppyDisk::Seek(uint tr)
{
	if (tr != curtracknum)
	{
		curtracknum = tr;
		curtrack = tr < 168 ? tracks + tr : 0;
		cursector = 0;
	}

	return curtrack;
}

// ---------------------------------------------------------------------------
//	現在トラック読み出し
//
FloppyDisk::Track* FloppyDisk::GetTrack()
{
	return curtrack;
}

// ---------------------------------------------------------------------------
//	セクタ一つ読み出し
//
FloppyDisk::Sector* FloppyDisk::GetSector()
{
	if (!cursector)
	{
		if (curtrack)
			cursector = curtrack->sector;
	}
	
	Sector* ret = cursector;
	
	if (cursector)
		cursector = cursector->next;
	
	return ret;
}

// ---------------------------------------------------------------------------
//	指定した ID を検索
//
bool FloppyDisk::FindID(IDR idr, uint density)
{
	if (!curtrack)
		return false;
	
	Sector* first = cursector;
	do
	{
		if (cursector)
		{
			if (cursector->id == idr)
			{
				if ((cursector->flags & 0xc0) == (density & 0xc0))
					return true;
			}
			cursector = cursector->next;
		}
		else
		{
			cursector = curtrack->sector;
		}
	} while (cursector != first);
	
	return false;
}

// ---------------------------------------------------------------------------
//	セクタ数を得る
//
uint FloppyDisk::GetNumSectors()
{
	int n = 0;
	if (curtrack)
	{
		Sector* sec = curtrack->sector;
		while (sec)
		{
			sec = sec->next;
			n++;
		}
	}
	return n;
}

// ---------------------------------------------------------------------------
//	トラック中のセクタデータの総量を得る
//
uint FloppyDisk::GetTrackSize()
{
	int size=0;

	if (curtrack)
	{
		Sector* sec = curtrack->sector;
		while (sec)
		{
			size += sec->size;
			sec = sec->next;
		}
	}
	return size;
}

// ---------------------------------------------------------------------------
//	Floppy::Resize
//	セクタのサイズを大きくした場合におけるセクタ潰しの再現
//	sector は現在選択しているトラックに属している必要がある．
//
bool FloppyDisk::Resize(Sector* sec, uint newsize)
{
	assert(curtrack && sec);

	int extend = newsize - sec->size - 0x40;
	
	// sector 自身の resize
	delete[] sec->image;
	sec->image = new uint8[newsize];
	sec->size = newsize;

	if (!sec->image)
	{
		sec->size = 0;
		return false;
	}
	
	cursector = sec->next;
	while (extend > 0 && cursector)
	{
		Sector* next = cursector->next;
		extend -= cursector->size + 0xc0;
		delete[] cursector->image;
		delete cursector;
		sec->next = cursector = next;
	}
	if (extend > 0)
	{
		int gapsize = GetTrackCapacity() - GetTrackSize() - 0x60 * GetNumSectors();
		extend -= gapsize;
	}
	while (extend > 0 && cursector)
	{
		Sector* next = cursector->next;
		extend -= cursector->size + 0xc0;
		delete[] cursector->image;
		delete cursector;
		curtrack->sector = cursector = next;
	}
	if (extend > 0)
		return false;

	return true;
}

// ---------------------------------------------------------------------------
//	FloppyDisk::FormatTrack
//
bool FloppyDisk::FormatTrack(int nsec, int secsize)
{
	Sector* sec;

	if (!curtrack)
		return false;

	// フォーマットフラグ
	curtrack->formated = true;

	// 今あるトラックを破棄
	sec = curtrack->sector;
	while (sec)
	{
		Sector* next = sec->next;

		// FDX拡張
		if (sec->fdx_encdata) {
			delete[] sec->fdx_encdata;
			sec->fdx_encdata = 0;
		}

		delete[] sec->image;
		delete sec;
		sec = next;
	}
	curtrack->sector = 0;
	
	if (nsec)
	{
		// セクタを作成
		cursector = 0;
		for (int i=0; i<nsec; i++)
		{
			Sector* newsector = new Sector;
			if (!newsector)
				return false;

			newsector->update = false;

			// FDX拡張
			newsector->fdx_enclen = 0;
			newsector->fdx_encdata = 0;
			newsector->fdx_ioffset = 0;
			newsector->fdx_doffset = 0;
			newsector->fdx_eoffset = 0;

			curtrack->sector = newsector;
			newsector->next = cursector;
			newsector->size = secsize;
			if (secsize)
			{
				newsector->image = new uint8[secsize];
				if (!newsector->image)
				{
					newsector->size = 0;
					return false;
				}
			}
			else
			{
				newsector->image = 0;
			}
			cursector = newsector;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
//	セクタ一つ追加
//
FloppyDisk::Sector* FloppyDisk::AddSector(int size)
{
	if (!curtrack)
		return 0;
	
	Sector* newsector = new Sector;
	if (!newsector)
		return 0;

	newsector->gap3 = 0;
	newsector->update = false;
	newsector->size = size;

	// FDX拡張
	newsector->fdx_enclen = 0;
	newsector->fdx_encdata = 0;
	newsector->fdx_ioffset = 0;
	newsector->fdx_doffset = 0;
	newsector->fdx_eoffset = 0;

	if (size)
	{
		newsector->image = new uint8[size];
		if (!newsector->image)
		{
			delete newsector;
			return 0;
		}
	}
	else
	{
		newsector->image = 0;
	}
	
	if (!cursector)
		cursector = curtrack->sector;
	
	if (cursector)
	{
		newsector->next = cursector->next;
		cursector->next = newsector;
	}
	else
	{
		newsector->next = 0;
		curtrack->sector = newsector;
	}
	cursector = newsector;
	return newsector;
}

// ---------------------------------------------------------------------------
//	トラックの容量を得る
//
uint FloppyDisk::GetTrackCapacity()
{
	static const int table[3] = { 6240, 6240, 10416 }; 

	if (IsFdx()) {
		if (curtrack && curtrack->fdx_track) {
			return curtrack->fdx_track->length >> 4;
		}
	}
		
	return table[type];
}

// ---------------------------------------------------------------------------
//	トラックを得る
//
FloppyDisk::Sector* FloppyDisk::GetFirstSector(uint tr)
{
	if (tr < 168)
		return tracks[tr].sector;
	return 0;
}

//---------------------------------------------------------------------------
//	FDXサポート

//---------------------------------------------------------------------------
//	CRC算出
//
uint16 FDX::gen_crc(uint8 *ptr, int len)
{
	static const uint16 crctbl[0x100] = {
		0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
		0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
		0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
		0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
		0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
		0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
		0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
		0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
		0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
		0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
		0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
		0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
		0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
		0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
		0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
		0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
		0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
		0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
		0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
		0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
		0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
		0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
		0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
		0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
		0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
		0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
		0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
		0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
		0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
		0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
		0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
		0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
	};
	uint16 crc;
	int i;

	// 初期化
	crc = 0xffff;

	// ループ
	for (i = 0; i < len; i++) {
		crc = (uint16)((crc << 8) ^ crctbl[(uint8)(crc >> 8) ^ (uint8)*ptr++]);
	}

	return crc;
}

// ---------------------------------------------------------------------------
//	ビット取得
//
uint8 FDX::bf_getbit(const uint8 *buf, int length, int offset)
{
	// ラップ
	while (offset < 0) {
		offset += length;
	}
	while (offset >= length) {
		offset -= length;
	}

	return (buf[offset >> 3] >> (7 - (offset & 7))) & 1;
}

// ---------------------------------------------------------------------------
//	ビット設定
//
void FDX::bf_setbit(uint8 *buf, int length, int offset)
{
	// ラップ
	while (offset < 0) {
		offset += length;
	}
	while (offset >= length) {
		offset -= length;
	}

	buf[offset >> 3] |= (0x80 >> (offset & 7));
}

// ---------------------------------------------------------------------------
//	ビットクリア
//
void FDX::bf_clrbit(uint8 *buf, int length, int offset)
{
	// ラップ
	while (offset < 0) {
		offset += length;
	}
	while (offset >= length) {
		offset -= length;
	}

	buf[offset >> 3] &= ~(0x80 >> (offset & 7));
}

// ---------------------------------------------------------------------------
//	バイト取得
//
uint8 FDX::bf_getbyte(const uint8 *buf, int length, int offset)
{
	// ラップ
	while (offset < 0) {
		offset += length;
	}
	while (offset >= length) {
		offset -= length;
	}

	if ((offset + 7) < length) {
		if ((offset & 7) == 0) {
			// 単純取得
			return buf[offset >> 3];
		} else {
			// 合成取得
			int i = offset >> 3;
			int j = offset & 7;
			return buf[i] << j | buf[i + 1] >> (8 - j);
		}
	} else {
		// ビット取得
		uint8 data = 0;
		for (int i = 0; i < 8; i++) {
			data <<= 1;
			data |= bf_getbit(buf, length, offset + i);
		}
		return data;
	}
}

// ---------------------------------------------------------------------------
//	バイト設定
//
void FDX::bf_setbyte(
	uint8 *buf, int length, int offset, uint8 data)
{
	// ラップ
	while (offset < 0) {
		offset += length;
	}
	while (offset >= length) {
		offset -= length;
	}

	if ((offset + 7) < length) {
		if ((offset & 7) == 0) {
			// 1バイト設定
			buf[offset >> 3] = data;
		}
		else {
			// 2バイト設定
			int i = offset >> 3;
			int rs = offset & 7;
			int ls = 8 - rs;
			uint8 mask;

			// 1バイト目設定
			mask = 0xff << ls;
			buf[i] = (buf[i] & mask) | (data >> rs);

			// 2バイト目設定
			mask = 0xff >> rs;
			buf[i + 1] = (buf[i + 1] & mask) | (data << ls);
		}
	}
	else {
		// ビット単位で設定
		for (int i = 0; i < 8; i++) {
			if (data & 0x80) {
				bf_setbit(buf, length, offset + i);
			}
			else {
				bf_clrbit(buf, length, offset + i);
			}
			data <<= 1;
		}
	}
}

//---------------------------------------------------------------------------
//	ビットフィールド設定
//
void FDX::bf_setbf(
	uint8 *dst, int dlen, int offset, uint8 *src, int slen)
{
	int i;
	int j;
	int c;
	uint8 data;

	// バイト単位で設定
	i = offset;
	j = 0;
	c = slen >> 3;
	while (c) {
		data = bf_getbyte(src, slen, j);
		bf_setbyte(dst, dlen, i, data);
		i += 8;
		j += 8;
		c--;
	}

	// ビット単位で設定
	c = slen & 7;
	while (c) {
		if (bf_getbit(src, slen, j)) {
			bf_setbit(dst, dlen, i);
		} else {
			bf_clrbit(dst, dlen, i);
		}
		i++;
		j++;
		c--;
	}
}

// ---------------------------------------------------------------------------
//	右ローテート
//
void FDX::bf_rotateright(uint8 *buf, int length, int offset)
{
	int i;
	int j;
	int size;
	uint8 *work;
	uint8 data;

	// ワーク確保
	size = (length + 7) >> 3;
	work = (uint8 *)malloc(size);

	// 左ローテート
	if (offset < 0) {
		offset = length + offset;
	}

	// ローテート
	for (i = 0, j = offset; i < length - 7; i+=8, j+=8) {
		data = bf_getbyte(buf, length, i);
		bf_setbyte(work, length, j, data);
	}

	// 残り
	if (length & 7) {
		for (; i < length; i++, j++) {
			if (bf_getbit(buf, length, i)) {
				bf_setbit(work, length, j);
			} else {
				bf_clrbit(work, length, j);
			}
		}
	}

	// ワークから戻す
	memcpy(buf, work, size);

	// 解放
	free(work);
}

// ---------------------------------------------------------------------------
//	ビットフィールドシーク
//
void FDX::bf_seek(uint8 *buf, int length, int offset)
{
	bf_rotateright(buf, length, -offset);
}

//---------------------------------------------------------------------------
//	コピー
//
void FDX::bf_copy(
	uint8 *dst, int dlen, int doff,
	const uint8 *src, int slen, int soff, int len)
{
	int i;
	int j;
	int c;
	uint8 data;

	// バイト単位で設定
	i = soff;
	j = doff;
	c = len >> 3;
	while (c) {
		data = bf_getbyte(src, slen, i);
		bf_setbyte(dst, dlen, j, data);
		i += 8;
		j += 8;
		c--;
	}

	// ビット単位で設定
	c = len & 7;
	while (c) {
		if (bf_getbit(src, slen, i)) {
			bf_setbit(dst, dlen, j);
		} else {
			bf_clrbit(dst, dlen, j);
		}
		i++;
		j++;
		c--;
	}
}

//---------------------------------------------------------------------------
//	パターン探索(内部)
//
int FDX::bf_find_in(
	const uint8 *buf, int length, int offset, int limit,
	const uint8 *pat, int plen)
{
	int i;
	int j;
	int pos;

	// パターン長を削減
	limit -= (plen - 1);

	if (plen & 7) {
		// ビット単位比較
		for (i = 0; i < limit; i++) {
			pos = offset + i;

			for (j = 0; j < plen; j++) {
				if (bf_getbit(buf, length, pos + j) !=
					bf_getbit(pat, plen, j)) {
					break;
				}
			}

			if (j >= plen) {
				return i;
			}
		}
	} else {
		// バイト単位比較
		for (i = 0; i < limit; i++) {
			pos = offset + i;

			for (j = 0; j < plen; j+=8) {
				if (bf_getbyte(buf, length, pos + j) !=
					bf_getbyte(pat, plen, j)) {
					break;
				}
			}

			if (j >= plen) {
				return i;
			}
		}
	}

	return -1;
}

//---------------------------------------------------------------------------
//	パターン探索
//
int FDX::bf_find(
	const uint8 *buf, int length, int offset, int limit,
	const uint8 *pat, int plen)
{
	// 4バイト単位でなければ通常のビットフィールド探索
	if (plen & 31) {
		return bf_find_in(buf, length, offset, limit, pat, plen);
	}

	// 探索リミット
	int pos = offset;
	limit += offset;
	limit -= plen;

	// 検索できない
	if (pos > limit) {
		return -1;
	}

	// ワーク作成
	int num = plen / 32;
	uint32 *pat4 = new uint32[num];
	uint32 *comp = new uint32[num + 1];

	// パターン生成
	for (int i = 0; i < num; i++) {
		pat4[i] = pat[i * 4 + 0];
		pat4[i] <<= 8;
		pat4[i] |= pat[i * 4 + 1];
		pat4[i] <<= 8;
		pat4[i] |= pat[i * 4 + 2];
		pat4[i] <<= 8;
		pat4[i] |= pat[i * 4 + 3];
	}

	// 初回充填
	for (int i = 0; i < num; i++) {
		comp[i] = bf_getbyte(buf, length, pos + i * 32 + 0);
		comp[i] <<= 8;
		comp[i] |= bf_getbyte(buf, length, pos + i * 32 + 8);
		comp[i] <<= 8;
		comp[i] |= bf_getbyte(buf, length, pos + i * 32 + 16);
		comp[i] <<= 8;
		comp[i] |= bf_getbyte(buf, length, pos + i * 32 + 24);
	}

	do {
		// 後続4バイト
		comp[num] = bf_getbyte(buf, length, pos + num * 32 + 0);
		comp[num] <<= 8;
		comp[num] |= bf_getbyte(buf, length, pos + num * 32 + 8);
		comp[num] <<= 8;
		comp[num] |= bf_getbyte(buf, length, pos + num * 32 + 16);
		comp[num] <<= 8;
		comp[num] |= bf_getbyte(buf, length, pos + num * 32 + 24);

		// シフトしながら32ビット比較
		for (int i = 0; i < 32; i++) {
			int j;
			for (j = 0; j < num; j++) {
				if (comp[j] != pat4[j]) {
					break;
				}
			}

			// 全て一致
			if (j == num) {
				delete[] pat4;
				delete[] comp;
				if (pos > limit) {
					return -1;
				} else {
					return pos + i - offset;
				}
			}

			// シフト
			for (j = 0; j < num; j++) {
				comp[j] <<= 1;
				comp[j] |= (comp[j + 1] >> 31) & 1;
			}
			comp[num] <<= 1;
		}

		// 次へ
		pos += 32;
	} while (pos <= limit);

	// ワーク解放
	delete[] pat4;
	delete[] comp;

	return -1;
}

//---------------------------------------------------------------------------
//	ビットフィールドクリア
//
void FDX::bf_clr(uint8 *dst, int dlen, int offset, int slen)
{
	int c;

	// クリア長チェック
	if (slen <= 0) {
		return;
	}

	// バイト単位で設定
	c = slen >> 3;
	while (c) {
		bf_setbyte(dst, dlen, offset, 0);
		offset += 8;
		c--;
	}

	// ビット単位で設定
	c = slen & 7;
	while (c) {
		bf_clrbit(dst, dlen, offset);
		offset++;
		c--;
	}
}

// ---------------------------------------------------------------------------
//	エンコードデータ->RAWデータ
//
int FDX::encode_to_raw(uint8 *enc, int enclen, uint8 *raw, int rawlen)
{
	int i;
	uint8 pulse;

	// 変換先をクリア
	memset(raw, 0x00, (rawlen + 7) >> 3);

	// パルスデータ
	pulse = 0xe0;

	// 変換
	for (i = 0; i < enclen; i++) {
		if (bf_getbit(enc, enclen, i)) {
			bf_setbyte(raw, rawlen, i << 3, pulse);
		}
	}

	// データ長を返す
	return enclen << 3;
}

// ---------------------------------------------------------------------------
//	RAWデータ->エンコードデータ
//
int FDX::raw_to_encode(uint8 *raw, int rawlen, uint8 *enc, int enclen)
{
	// エンコードデータを先にクリア
	memset(enc, 0x00, (enclen + 7) >> 3);

	// 初回の立ち上がりエッジを探す
	uint8 data = bf_getbit(raw, rawlen, rawlen - 1);
	int offset;
	for (offset = 0; offset < rawlen; offset++) {
		data <<= 1;
		data |= bf_getbit(raw, rawlen, offset);

		// 立ち上がりエッジ
		if ((data & 3) == 1) {
			break;
		}
	}

	// アンフォーマットの場合処理を中止
	if (offset >= rawlen) {
		return rawlen >> 3;
	}

	// 変換用カウンタ類初期化
	uint8 prev = 0;
	int cur = 0;
	int epos = offset >> 3;
	int diff = 0;
	int last_epos = epos;
	int last_diff = 0;

	// 初回パルス設定
	bf_setbit(enc, enclen, epos);

	// 変換処理
	while (cur < rawlen) {
		// データ取得
		data = bf_getbyte(raw, rawlen, cur + offset);

		// エッジ算出
		uint8 edge = (((prev << 7) | (data >> 1)) ^ data) & data;
		prev = data;

		// エッジが無ければスキップ
		if (!edge) {
			diff += 8;
			cur += 8;
			continue;
		}

		// ビット走査
		for (int i = 0; i < 8; i++) {
			// 立ち上がり検査
			if (edge & 0x80) {
				// パルス間隔算出
				int width = measure_pulse(diff);

				// パルス間隔が小さすぎる
				if (width == 1) {
					// 前回パルスをキャンセル
					bf_clrbit(enc, enclen, epos);

					// 前回分の位置情報から復元
					epos = last_epos;
					diff += last_diff;
					width = measure_pulse(diff);
				}

				// 起点位置とパルス間隔を保存
				last_epos = epos;
				last_diff = diff;

				// エンコードデータ位置
				epos += width;

				// カウンタクリア
				diff = 0;

				// パルス設定
				bf_setbit(enc, enclen, epos);
			}

			// カウンタ加算
			diff++;
			cur++;

			// 現在位置チェック
			if (cur >= rawlen) {
				break;
			}

			// 次のエッジ
			edge <<= 1;

			// 残りのエッジが無い
			if (edge == 0) {
				diff += 7 - i;
				cur += 7 - i;
				break;
			}
		}
	}

	// オーバー分を引く
	if (cur > rawlen) {
		diff -= (cur - rawlen);
	}

	// 記録されたデータ長を返す
	enclen = epos - (offset >> 3) + measure_pulse(diff);

	// データ長が切り詰められた時に最後のパルスを再設定する
	if (epos >= enclen) {
		bf_setbit(enc, enclen, epos);
	}

	return enclen;
}

//---------------------------------------------------------------------------
//	RAWデータ上のエンコードデータ位置を探索
//
int FDX::encode_to_raw_pos(uint8 *raw, int rawlen, int encpos)
{
	// 初回の立ち上がりエッジを探す
	uint8 data = bf_getbit(raw, rawlen, rawlen - 1);
	int offset;
	for (offset = 0; offset < rawlen; offset++) {
		data <<= 1;
		data |= bf_getbit(raw, rawlen, offset);

		// 立ち上がりエッジ
		if ((data & 3) == 1) {
			break;
		}
	}

	// アンフォーマットの場合処理を中止
	if (offset >= rawlen) {
		return encpos << 3;
	}

	// 変換用カウンタ類初期化
	uint8 prev = 0;
	int cur = 0;
	int epos = offset >> 3;
	int diff = 0;
	int last_epos = epos;
	int last_diff = 0;
	data = 0;

	// 変換処理
	while (cur < rawlen) {
		// データ取得
		data = bf_getbyte(raw, rawlen, cur + offset);

		// エッジ算出
		uint8 edge = (((prev << 7) | (data >> 1)) ^ data) & data;
		prev = data;

		// エッジが無ければスキップ
		if (!edge) {
			diff += 8;
			cur += 8;
			continue;
		}

		// ビット走査
		for (int i = 0; i < 8; i++) {
			// 立ち上がり検査
			if (edge & 0x80) {
				// パルス間隔算出
				int width = measure_pulse(diff);

				// パルス間隔が小さすぎる
				if (width == 1) {
					// 前回分の位置情報から復元
					epos = last_epos;
					diff += last_diff;
					width = measure_pulse(diff);
				}

				// 起点位置とパルス間隔を保存
				last_epos = epos;
				last_diff = diff;

				// エンコードデータ位置
				epos += width;

				// カウンタクリア
				diff = 0;

				// RAWデータの位置を記録
				if (epos >= encpos) {
					return cur + offset - ((epos - encpos) << 3);
				}
			}

			// カウンタ加算
			diff++;
			cur++;

			// 現在位置チェック
			if (cur >= rawlen) {
				break;
			}

			// 次のエッジ
			edge <<= 1;

			// 残りのエッジが無い
			if (edge == 0) {
				diff += 7 - i;
				cur += 7 - i;
				break;
			}
		}
	}

	return cur;
}

//---------------------------------------------------------------------------
//	クロック分離
//	アンフォーマットの箇所は不安定データに置き換える
//
int FDX::bf_splitclock(
	uint8 *buf, int length, int offset,
	uint8 *dbuf, int dlen, bool mfm, bool instab)
{
	int i;
	int j;
	int total;
	int count;
	int zeros;
	uint8 bit;
	uint8 temp[4];
	uint8 data;
	uint32 rand;

	// クロック分離のみ
	total = 0;

	if (!instab) {
		if (mfm) {
			for (i = 0; i < dlen; i++) {
				data = 0;
				bf_copy(temp, 16, 0, buf, length, offset, 16);
				for (j = 0; j < 2; j++) {
					data = (data << 1) | ((temp[j] >> 6) & 0x01);
					data = (data << 1) | ((temp[j] >> 4) & 0x01);
					data = (data << 1) | ((temp[j] >> 2) & 0x01);
					data = (data << 1) | ((temp[j] >> 0) & 0x01);
				}
				offset += 16;
				total += 16;
				*dbuf++ = data;
			}
		} else {
			for (i = 0; i < dlen; i++) {
				data = 0;
				bf_copy(temp, 32, 0, buf, length, offset, 32);
				for (j = 0; j < 4; j++) {
					data = (data << 1) | ((temp[j] >> 5) & 0x01);
					data = (data << 1) | ((temp[j] >> 1) & 0x01);
				}
				offset += 32;
				total += 32;
				*dbuf++ = data;
			}
		}

		return total;
	}

	// 不安定データ再現
	count = 0;
	zeros = 0;

	if (mfm) {
		for (i = 0; i < dlen; i++) {
			data = 0;
			rand = rand_xor();
			for (j = 0; j < 16; j++) {
				bit = bf_getbit(buf, length, offset);
				if (j & 1) {
					data <<= 1;
					data |= bit;
				}

				if (bit) {
					count = 0;
					zeros = 0;
				} else {
					count++;
					zeros++;
				}

				// ランダムノイズ
				if (count >= 4 && (rand & 1)) {
					data |= 1;
					count = 0;
				}

				// 不安定領域のビットずれ
				if (zeros >= 4 && ((zeros - 4) & 0x7f) == 0) {
					rand = rand_xor();
					if (rand & 1) {
						offset -= (rand >> 16) & 3;
					} else {
						offset += (rand >> 16) & 3;
					}
				}

				offset++;
				total++;
				rand >>= 1;
			}
			*dbuf++ = data;
		}
	} else {
		for (i = 0; i < dlen; i++) {
			data = 0;
			rand = rand_xor();
			for (j = 0; j < 32; j++) {
				bit = bf_getbit(buf, length, offset);
				if ((j & 3) == 2) {
					data <<= 1;
					data |= bit;
				}

				if (bit) {
					count = 0;
					zeros = 0;
				} else {
					count++;
					zeros++;
				}

				// ランダムノイズ
				if (count >= 4 && (rand & 1)) {
					data |= 1;
					count = 0;
				}

				// 不安定領域のビットずれ
				if (zeros >= 4 && ((zeros - 4) & 0x7f) == 0) {
					rand = rand_xor();
					if (rand & 1) {
						offset -= (rand >> 16) & 3;
					} else {
						offset += (rand >> 16) & 3;
					}
				}

				offset++;
				total++;
				rand >>= 1;
			}
			*dbuf++ = data;
		}
	}

	return total;
}

//---------------------------------------------------------------------------
//	パルス幅計測
//
int FDX::measure_pulse(int diff)
{
	static int prev = 0;
	int bias;

	if (prev > diff) {
		bias = 3;
	} else {
		bias = 4;
	}

	prev = diff;
	return (diff + bias) >> 3;
}

//---------------------------------------------------------------------------
//	トラックデータ解析(MFM)
//
FDX::fdxtrkstat_t* FDX::analizetrack_in(
	FDX::fdxtrkstat_t *stat, uint8 *enc, int enclen,
	int idams[][2], bool trkmfm, bool needdata)
{
	// IAM探索(トラックのMFM/FM種別に従う)
	int pos;
	if (trkmfm) {
		pos = bf_find(enc, enclen, 0, enclen, FDX_IAM_MFM, 16 * 8);
	} else {
		pos = bf_find(enc, enclen, 0, enclen, FDX_IAM_FM, 12 * 8);
	}
	if (pos >= 0) {
		stat->status |= FDX_INF_IAM;
		stat->iamoffset = pos + 8 * 8;
	}

	// 全セクターを解析
	int num = 0;
	for (int i = 0;; i++) {
		// セクタのMFM/FM種別
		bool secmfm;
		int cellsize;
		if (idams[i][1] == FDX_SEC_MFM) {
			secmfm = true;
			cellsize = 16;
		} else if (idams[i][1] == FDX_SEC_FM ){
			secmfm = false;
			cellsize = 32;
		} else {
			break;
		}

		// エラーコード初期化
		uint32 err = FDX_FDD_NOERROR;

		// オフセット設定
		int offset = idams[i][0];

		// クロック成分を除いてデータをデコード
		uint8 buf[0x4000 + 6];
		uint8 *p;
		if (secmfm) {
			p = buf;
			offset += FDX::bf_splitclock(
				enc, enclen, offset, p, 10, secmfm, false);
		} else {
			p = &buf[3];
			offset += FDX::bf_splitclock(
				enc, enclen, offset, p, 7, secmfm, false);
		}

		// ID CRCチェック
		uint16 crc;
		if (secmfm) {
			crc = FDX::gen_crc(p, 8);
		} else {
			crc = FDX::gen_crc(p, 5);
		}

		if ((uint8)(crc >> 8) != buf[8] ||
			(uint8)crc != buf[9]) {
			err |= (FDX_FDD_DATAERR | FDX_FDD_IDCRC);
		}

		// CHRNとセクタ長算出
		uint8 chrn[4];
		chrn[0] = buf[4];
		chrn[1] = buf[5];
		chrn[2] = buf[6];
		chrn[3] = buf[7];
		int siz = buf[7];

		// セクタ長チェック
		if (siz > 7) {
			siz = 128 << 7;
		} else {
			siz = 128 << siz;
		}

		// DAM/DDAMの検索は次のIDAMまで
		int limit;
		if ((secmfm && idams[i + 1][1] == FDX_SEC_MFM) ||
			(!secmfm && idams[i + 1][1] == FDX_SEC_FM)) {
			limit = idams[i + 1][0] - offset;
		} else {
			limit = enclen - offset;
		}

		// DAM探索
		int dam;
		if (secmfm) {
			dam = FDX::bf_find(
				enc, enclen, offset, limit, FDX_DAM_MFM, 16 * 8);
		} else {
			dam = FDX::bf_find(
				enc, enclen, offset, limit, FDX_DAM_FM, 12 * 8);
		}

		// SYNCパターン分調整
		if (dam >= 0) {
			dam += 8 * 8;
		}

		// DDAM探索
		int ddam;
		if (secmfm) {
			ddam = FDX::bf_find(
				enc, enclen, offset, limit, FDX_DDAM_MFM, 16 * 8);
		} else {
			ddam = FDX::bf_find(
				enc, enclen, offset, limit, FDX_DDAM_FM, 12 * 8);
		}

		// SYNCパターン分調整
		if (ddam >= 0) {
			ddam += 8 * 8;
		}

		// DAM/DDAM探索結果評価
		if (dam < 0 && ddam < 0) {
			// DAM/DDAMが見つからない
			err |= FDX_FDD_MDAM;
			pos = -1;
		} else if (dam >= 0 && ddam < 0) {
			// DAMが見つかった
			pos = dam;
		} else if (dam < 0 && ddam >= 0) {
			// DDAMが見つかった
			err |= FDX_FDD_DDAM;
			pos = ddam;
		} else {
			// 両方見つかった
			if (dam < ddam) {
				// DAMが先
				pos = dam;
			} else {
				// DDAMが先
				err |= FDX_FDD_DDAM;
				pos = ddam;
			}
		}

		// DAM/DDAMが見つかったらGAP2のサイズを決定
		int gap = 0;
		if (pos >= 0) {
			gap = pos - (secmfm ? (12 * 16) : (6 * 32));
			gap /= cellsize;
		}

		// MFM/FMのセクタが存在する
		stat->status |= secmfm ? FDX_INF_MFM : FDX_INF_FM;

		// セクタ作成
		fdxsecstat_t *sector = &stat->sector[num++];
		sector->type = secmfm ? FDX_SEC_MFM : FDX_SEC_FM;
		sector->idamoffset = idams[i][0];
		sector->chrn[0] = chrn[0];
		sector->chrn[1] = chrn[1];
		sector->chrn[2] = chrn[2];
		sector->chrn[3] = chrn[3];
		sector->gap2 = gap;
		sector->size = siz;
		sector->icrc[0] = buf[8];
		sector->icrc[1] = buf[9];
		sector->enclen = siz * cellsize;
		if (needdata) {
			sector->data = new uint8[siz];
			sector->encdata = new uint8[siz * cellsize >> 3];
		}

		// IDのエラーをスタータスに反映
		if (err & FDX_FDD_IDCRC) {
			stat->status |= FDX_ERR_ICRC;
			stat->status |= FDX_ERR_IINV;
		}

		// DAM/DDAMが見つからないので以下スキップ
		if (err & FDX_FDD_MDAM) {
			stat->status |= FDX_ERR_DINV;
			sector->err = err;
			continue;
		}

		// DAM/DDAMの位置を補正
		offset += pos;
		sector->damoffset = offset;

		// クロック成分を除いてデータをデコード
		if (secmfm) {
			// DAM/DDAM(4バイト)とCRC(2バイト)を含む
			offset += FDX::bf_splitclock(
				enc, enclen, offset, p, siz + 6, secmfm, false);
		} else {
			// DAM/DDAM(1バイト)とCRC(2バイト)を含む
			offset += FDX::bf_splitclock(
				enc, enclen, offset, p, siz + 3, secmfm, false);
		}

		// セクタ末尾オフセット
		sector->eosoffset = offset;

		// DATA CRCチェック
		if (secmfm) {
			crc = FDX::gen_crc(p, siz + 4);
		} else {
			crc = FDX::gen_crc(p, siz + 1);
		}

		if ((uint8)(crc >> 8) != buf[siz + 4] ||
			(uint8)crc != buf[siz + 5]) {
			stat->status |= FDX_ERR_DCRC;
			err |= (FDX_FDD_DATAERR | FDX_FDD_DATACRC);
		}
		sector->dcrc[0] = buf[siz + 4];
		sector->dcrc[1] = buf[siz + 5];

		// セクタにデータ転送
		sector->err = err;
		if (needdata) {
			memcpy(sector->data, &buf[4], siz);
		}

		// エンコードデータを設定
		if (needdata) {
			offset = sector->damoffset;
			offset += (secmfm ? 4 * cellsize : 1 * cellsize);
			FDX::bf_copy(sector->encdata, sector->enclen, 0,
				enc, enclen, offset, sector->enclen);
		}
	}

	// セクタ数確定
	stat->num = num;

	// ステータスにもセクタ数を設定
	stat->status |= (stat->num & FDX_INF_SECMSK);

	// GAP3の算出
	for (int i = 0; i < stat->num; i++) {
		fdxsecstat_t *sector = &stat->sector[i];
		if (sector->err & FDX_FDD_MDAM) {
			continue;
		}

		int offset = sector->eosoffset;
		int cellsize = sector->type == FDX_SEC_MFM ? 16 : 32;

		int gap = 0;
		if (i == stat->num - 1) {
			// 最終は全データ長まで
			if (offset < enclen) {
				gap = (enclen - offset) / cellsize;
			}
		} else {
			// 途中は次のIDAMの位置まで
			if (offset < stat->sector[i + 1].idamoffset) {
				gap = stat->sector[i + 1].idamoffset
					- offset - 12 * cellsize;
				gap /= cellsize;
				if (gap < 0) {
					gap = 0;
				}
			}
		}
		sector->gap3 = gap;
	}

	// 以後トラックのMFM/FM種別でセル数を計算
	int cellsize = trkmfm ? 16 : 32;

	// GAP1のサイズ算出
	if (stat->status & FDX_INF_IAM) {
		for (int i = 0; i < stat->num; i++) {
			fdxsecstat_t *sector = &stat->sector[i];
			if (sector->idamoffset > stat->iamoffset) {
				stat->gap1 = sector->idamoffset;
				stat->gap1 -= 12 * cellsize;
				stat->gap1 -= stat->iamoffset;
				stat->gap1 -= trkmfm? 4 * cellsize : 1* cellsize;
				stat->gap1 /= cellsize;
				break;
			}
		}
	}

	// GAP4bのサイズ算出(終端が最も後ろにあるセクタの後方)
	int maxeos = -1;
	fdxsecstat_t *maxsec = NULL;
	for (int i = 0; i < stat->num; i++) {
		fdxsecstat_t *sector = &stat->sector[i];
		if (sector->eosoffset > maxeos) {
			maxeos = sector->eosoffset;
			maxsec = sector;
		}
	}

	if (maxsec) {
		if (enclen > maxeos) {
			stat->gap4b = (enclen - maxeos) / cellsize;
		} else {
			stat->gap4b = 0;
		}
		maxsec->gap3 = 0;
	}

	// GAP4aのサイズ調整
	if (stat->status & FDX_INF_IAM) {
		stat->gap4a = (stat->iamoffset - 12 * cellsize) / cellsize;
	} else {
		stat->gap4a =
			(stat->sector[0].idamoffset - 12 * cellsize) / cellsize;
	}
	if (stat->num > 0) {
		fdxsecstat_t *sector = &stat->sector[stat->num - 1];
		if (sector->idamoffset > sector->eosoffset) {
			stat->gap4a -= sector->eosoffset / cellsize;
		}
	}
	if (stat->gap4a < 0) {
		stat->gap4a = 0;
	}

	// ラップ調整
	for (int i = 0; i < stat->num; i++) {
		fdxsecstat_t *sector = &stat->sector[i];
		if (sector->idamoffset >= enclen) {
			sector->idamoffset -= enclen;
		}
		if (sector->damoffset >= enclen) {
			sector->damoffset -= enclen;
		}
		if (sector->eosoffset >= enclen) {
			sector->eosoffset -= enclen;
		}
	}

	// インデックスホール跨りのセクタが存在するか
	for (int i = 0; i < stat->num; i++) {
		fdxsecstat_t *sector = &stat->sector[i];
		if (sector->idamoffset > sector->eosoffset) {
			stat->status |= FDX_INF_ISTR;
			break;
		}
	}

	return stat;
}

//---------------------------------------------------------------------------
//	トラックデータ解析
//
FDX::fdxtrkstat_t* FDX::analizetrack(
	uint8 *buf, int buflen, bool israw, bool needdata)
{
	// 初期化
	int idams[FDX_MAX_SECTOR][2];
	memset(idams, 0x00, sizeof(idams));
	fdxtrkstat_t *stat = new fdxtrkstat_t;
	memset(stat, 0x00, sizeof(fdxtrkstat_t));

	// RAWデータならエンコードデータに変換してから解析
	uint8 *enc;
	int enclen;
	if (israw) {
		enc = new uint8[FDX_ENC_BYTES];
		enclen = FDX_ENC_BYTES << 3;
		enclen = raw_to_encode(buf, buflen, enc, enclen);
	} else {
		enc = buf;
		enclen = buflen;
	}

	// IDAM探索(MFM)
	int m = 0;
	int midams[FDX_MAX_SECTOR];
	for (int i = 0; i < enclen;) {
		// パターン探索
		int pos = bf_find(enc, enclen, i, enclen - i, FDX_IDAM_MFM, 16 * 8);
		if (pos < 0) {
			break;
		}

		// IDAM($A1,$A1,$A1,$FE)の位置を記録
		if (m < FDX_MAX_SECTOR) {
			midams[m++] = i + pos + 8 * 8;
		}

		// 探索開始位置を更新
		i += pos + 16 * 8;
	}

	// IDAM探索(FM)
	int f = 0;
	int fidams[FDX_MAX_SECTOR];
	for (int i = 0; i < enclen;) {
		// パターン探索
		int pos = bf_find(enc, enclen, i, enclen - i, FDX_IDAM_FM, 12 * 8);
		if (pos < 0) {
			break;
		}

		// IDAM($FE)の位置を記録
		if (f < FDX_MAX_SECTOR) {
			fidams[f++] = i + pos + 8 * 8;
		}

		// 探索開始位置を更新
		i += pos + 12 * 8;
	}

	// エンコードデータ長設定
	stat->length = enclen;

	// アンフォーマットデータ
	if ((m + f) == 0) {
		stat->status = FDX_ERR_UNKN;
		return stat;
	}

	// セクタ位でをソートする
	for (int i = 0, j = 0, k = 0; i < m || j < f;) {
		// MFM/FMのどちらかのセクタを選択
		bool mfm;
		if (i < m && j == f) {
			mfm = true;
		} else if (i == m && j < f) {
			mfm = false;
		} else {
			if (midams[i] < fidams[j]) {
				mfm = true;
			} else {
				mfm = false;
			}
		}

		// セクタ位置の記録と種別設定
		if (mfm) {
			idams[k][0] = midams[i];
			idams[k][1] = FDX_SEC_MFM;
			i++;
		} else {
			idams[k][0] = fidams[j];
			idams[k][1] = FDX_SEC_FM;
			j++;
		}

		// 次へ
		k++;
	}

	// トラックとしてのエンコード形式を決める
	bool mfm;
	if (m >= f) {
		mfm = true;
	} else {
		mfm = false;
	}

	// セクタ解析
	analizetrack_in(stat, enc, enclen, idams, mfm, needdata);

	// ワーク解放
	if (israw) {
		delete[] enc;
	}

	return stat;
}

//---------------------------------------------------------------------------
//	トラックデータ解析ワーク解放
//
void FDX::release_analyzeobject(fdxtrkstat_t *stat)
{
	int i;
	fdxsecstat_t *secstat;

	for (i = 0; i < stat->num; i++) {
		secstat = &stat->sector[i];
		if (secstat->data) {
			delete secstat->data;
		}
		if (secstat->encdata) {
			delete secstat->encdata;
		}
	}

	delete stat;
}

//---------------------------------------------------------------------------
//	IAM生成
//
int FDX::gen_iam(uint8 *buf, bool mfm)
{
	// エンコーディング済み
	if (mfm) {
		*buf++ = 0x52;
		*buf++ = 0x24;
		*buf++ = 0x52;
		*buf++ = 0x24;
		*buf++ = 0x52;
		*buf++ = 0x24;
		*buf++ = 0x55;
		*buf++ = 0x52;

		return 8;
	} else {
		*buf++ = 0xAA;
		*buf++ = 0x2A;
		*buf++ = 0x2A;
		*buf++ = 0x88;

		return 4;
	}
}

//---------------------------------------------------------------------------
//	IDAM生成
//
int FDX::gen_idam(uint8 *buf, bool mfm)
{
	// エンコーディング済み
	if (mfm) {
		*buf++ = 0x44;
		*buf++ = 0x89;
		*buf++ = 0x44;
		*buf++ = 0x89;
		*buf++ = 0x44;
		*buf++ = 0x89;
		*buf++ = 0x55;
		*buf++ = 0x54;

		return 8;
	} else {
		*buf++ = 0xAA;
		*buf++ = 0x22;
		*buf++ = 0x2A;
		*buf++ = 0xA8;

		return 4;
	}
}

//---------------------------------------------------------------------------
//	DAM/DDAM生成
//
int FDX::gen_dam(uint8 *buf, bool del, bool mfm)
{
	// エンコーディング済み
	if (mfm) {
		*buf++ = 0x44;
		*buf++ = 0x89;
		*buf++ = 0x44;
		*buf++ = 0x89;
		*buf++ = 0x44;
		*buf++ = 0x89;
		if (del) {
			*buf++ = 0x55;
			*buf++ = 0x4A;
		} else {
			*buf++ = 0x55;
			*buf++ = 0x45;
		}

		return 8;
	} else {
		if (del) {
			*buf++ = 0xAA;
			*buf++ = 0x22;
			*buf++ = 0x28;
			*buf++ = 0x88;
		} else {
			*buf++ = 0xAA;
			*buf++ = 0x22;
			*buf++ = 0x28;
			*buf++ = 0xAA;
		}

		return 4;
	}
}

//---------------------------------------------------------------------------
//	MFMデータテーブル
//
static const uint16 MFMTable[256]=
{
	0xAAAA, 0xAAA9, 0xAAA4, 0xAAA5, 0xAA92, 0xAA91, 0xAA94, 0xAA95,
	0xAA4A, 0xAA49, 0xAA44, 0xAA45, 0xAA52, 0xAA51, 0xAA54, 0xAA55,
	0xA92A, 0xA929, 0xA924, 0xA925, 0xA912, 0xA911, 0xA914, 0xA915,
	0xA94A, 0xA949, 0xA944, 0xA945, 0xA952, 0xA951, 0xA954, 0xA955,
	0xA4AA, 0xA4A9, 0xA4A4, 0xA4A5, 0xA492, 0xA491, 0xA494, 0xA495,
	0xA44A, 0xA449, 0xA444, 0xA445, 0xA452, 0xA451, 0xA454, 0xA455,
	0xA52A, 0xA529, 0xA524, 0xA525, 0xA512, 0xA511, 0xA514, 0xA515,
	0xA54A, 0xA549, 0xA544, 0xA545, 0xA552, 0xA551, 0xA554, 0xA555,
	0x92AA, 0x92A9, 0x92A4, 0x92A5, 0x9292, 0x9291, 0x9294, 0x9295,
	0x924A, 0x9249, 0x9244, 0x9245, 0x9252, 0x9251, 0x9254, 0x9255,
	0x912A, 0x9129, 0x9124, 0x9125, 0x9112, 0x9111, 0x9114, 0x9115,
	0x914A, 0x9149, 0x9144, 0x9145, 0x9152, 0x9151, 0x9154, 0x9155,
	0x94AA, 0x94A9, 0x94A4, 0x94A5, 0x9492, 0x9491, 0x9494, 0x9495,
	0x944A, 0x9449, 0x9444, 0x9445, 0x9452, 0x9451, 0x9454, 0x9455,
	0x952A, 0x9529, 0x9524, 0x9525, 0x9512, 0x9511, 0x9514, 0x9515,
	0x954A, 0x9549, 0x9544, 0x9545, 0x9552, 0x9551, 0x9554, 0x9555,
	0x4AAA, 0x4AA9, 0x4AA4, 0x4AA5, 0x4A92, 0x4A91, 0x4A94, 0x4A95,
	0x4A4A, 0x4A49, 0x4A44, 0x4A45, 0x4A52, 0x4A51, 0x4A54, 0x4A55,
	0x492A, 0x4929, 0x4924, 0x4925, 0x4912, 0x4911, 0x4914, 0x4915,
	0x494A, 0x4949, 0x4944, 0x4945, 0x4952, 0x4951, 0x4954, 0x4955,
	0x44AA, 0x44A9, 0x44A4, 0x44A5, 0x4492, 0x4491, 0x4494, 0x4495,
	0x444A, 0x4449, 0x4444, 0x4445, 0x4452, 0x4451, 0x4454, 0x4455,
	0x452A, 0x4529, 0x4524, 0x4525, 0x4512, 0x4511, 0x4514, 0x4515,
	0x454A, 0x4549, 0x4544, 0x4545, 0x4552, 0x4551, 0x4554, 0x4555,
	0x52AA, 0x52A9, 0x52A4, 0x52A5, 0x5292, 0x5291, 0x5294, 0x5295,
	0x524A, 0x5249, 0x5244, 0x5245, 0x5252, 0x5251, 0x5254, 0x5255,
	0x512A, 0x5129, 0x5124, 0x5125, 0x5112, 0x5111, 0x5114, 0x5115,
	0x514A, 0x5149, 0x5144, 0x5145, 0x5152, 0x5151, 0x5154, 0x5155,
	0x54AA, 0x54A9, 0x54A4, 0x54A5, 0x5492, 0x5491, 0x5494, 0x5495,
	0x544A, 0x5449, 0x5444, 0x5445, 0x5452, 0x5451, 0x5454, 0x5455,
	0x552A, 0x5529, 0x5524, 0x5525, 0x5512, 0x5511, 0x5514, 0x5515,
	0x554A, 0x5549, 0x5544, 0x5545, 0x5552, 0x5551, 0x5554, 0x5555
};

//---------------------------------------------------------------------------
//	MFMデータフィル
//
int FDX::fill_mfm(uint8 *dst, uint8 data, int size, int *last)
{
	int i;
	uint16 ret;

	for (i = 0; i < size; i++) {
		ret = MFMTable[data];
		if (*last) {
			ret &= ~0x8000;
		}
		*last = ret & 1 ? 1 : 0;
		*dst++ = (uint8)(ret >> 8);
		*dst++ = (uint8)ret;
	}

	return size * 2;
}

//---------------------------------------------------------------------------
//	MFMデータ生成
//
int FDX::gen_mfm(uint8 *dst, uint8 *src, int size, int *last)
{
	int i;
	uint16 ret;

	for (i = 0; i < size; i++) {
		ret = MFMTable[*src];
		src++;
		if (*last) {
			ret &= ~0x8000;
		}
		*last = ret & 1 ? 1 : 0;
		*dst++ = (uint8)(ret >> 8);
		*dst++ = (uint8)ret;
	}

	return size * 2;
}

//---------------------------------------------------------------------------
//	FMデータテーブル
//
static const uint32 FMTable[256]=
{
	0x88888888, 0x8888888A, 0x888888A8, 0x888888AA,
	0x88888A88, 0x88888A8A, 0x88888AA8, 0x88888AAA,
	0x8888A888, 0x8888A88A, 0x8888A8A8, 0x8888A8AA,
	0x8888AA88, 0x8888AA8A, 0x8888AAA8, 0x8888AAAA,
	0x888A8888, 0x888A888A, 0x888A88A8, 0x888A88AA,
	0x888A8A88, 0x888A8A8A, 0x888A8AA8, 0x888A8AAA,
	0x888AA888, 0x888AA88A, 0x888AA8A8, 0x888AA8AA,
	0x888AAA88, 0x888AAA8A, 0x888AAAA8, 0x888AAAAA,
	0x88A88888, 0x88A8888A, 0x88A888A8, 0x88A888AA,
	0x88A88A88, 0x88A88A8A, 0x88A88AA8, 0x88A88AAA,
	0x88A8A888, 0x88A8A88A, 0x88A8A8A8, 0x88A8A8AA,
	0x88A8AA88, 0x88A8AA8A, 0x88A8AAA8, 0x88A8AAAA,
	0x88AA8888, 0x88AA888A, 0x88AA88A8, 0x88AA88AA,
	0x88AA8A88, 0x88AA8A8A, 0x88AA8AA8, 0x88AA8AAA,
	0x88AAA888, 0x88AAA88A, 0x88AAA8A8, 0x88AAA8AA,
	0x88AAAA88, 0x88AAAA8A, 0x88AAAAA8, 0x88AAAAAA,
	0x8A888888, 0x8A88888A, 0x8A8888A8, 0x8A8888AA,
	0x8A888A88, 0x8A888A8A, 0x8A888AA8, 0x8A888AAA,
	0x8A88A888, 0x8A88A88A, 0x8A88A8A8, 0x8A88A8AA,
	0x8A88AA88, 0x8A88AA8A, 0x8A88AAA8, 0x8A88AAAA,
	0x8A8A8888, 0x8A8A888A, 0x8A8A88A8, 0x8A8A88AA,
	0x8A8A8A88, 0x8A8A8A8A, 0x8A8A8AA8, 0x8A8A8AAA,
	0x8A8AA888, 0x8A8AA88A, 0x8A8AA8A8, 0x8A8AA8AA,
	0x8A8AAA88, 0x8A8AAA8A, 0x8A8AAAA8, 0x8A8AAAAA,
	0x8AA88888, 0x8AA8888A, 0x8AA888A8, 0x8AA888AA,
	0x8AA88A88, 0x8AA88A8A, 0x8AA88AA8, 0x8AA88AAA,
	0x8AA8A888, 0x8AA8A88A, 0x8AA8A8A8, 0x8AA8A8AA,
	0x8AA8AA88, 0x8AA8AA8A, 0x8AA8AAA8, 0x8AA8AAAA,
	0x8AAA8888, 0x8AAA888A, 0x8AAA88A8, 0x8AAA88AA,
	0x8AAA8A88, 0x8AAA8A8A, 0x8AAA8AA8, 0x8AAA8AAA,
	0x8AAAA888, 0x8AAAA88A, 0x8AAAA8A8, 0x8AAAA8AA,
	0x8AAAAA88, 0x8AAAAA8A, 0x8AAAAAA8, 0x8AAAAAAA,
	0xA8888888, 0xA888888A, 0xA88888A8, 0xA88888AA,
	0xA8888A88, 0xA8888A8A, 0xA8888AA8, 0xA8888AAA,
	0xA888A888, 0xA888A88A, 0xA888A8A8, 0xA888A8AA,
	0xA888AA88, 0xA888AA8A, 0xA888AAA8, 0xA888AAAA,
	0xA88A8888, 0xA88A888A, 0xA88A88A8, 0xA88A88AA,
	0xA88A8A88, 0xA88A8A8A, 0xA88A8AA8, 0xA88A8AAA,
	0xA88AA888, 0xA88AA88A, 0xA88AA8A8, 0xA88AA8AA,
	0xA88AAA88, 0xA88AAA8A, 0xA88AAAA8, 0xA88AAAAA,
	0xA8A88888, 0xA8A8888A, 0xA8A888A8, 0xA8A888AA,
	0xA8A88A88, 0xA8A88A8A, 0xA8A88AA8, 0xA8A88AAA,
	0xA8A8A888, 0xA8A8A88A, 0xA8A8A8A8, 0xA8A8A8AA,
	0xA8A8AA88, 0xA8A8AA8A, 0xA8A8AAA8, 0xA8A8AAAA,
	0xA8AA8888, 0xA8AA888A, 0xA8AA88A8, 0xA8AA88AA,
	0xA8AA8A88, 0xA8AA8A8A, 0xA8AA8AA8, 0xA8AA8AAA,
	0xA8AAA888, 0xA8AAA88A, 0xA8AAA8A8, 0xA8AAA8AA,
	0xA8AAAA88, 0xA8AAAA8A, 0xA8AAAAA8, 0xA8AAAAAA,
	0xAA888888, 0xAA88888A, 0xAA8888A8, 0xAA8888AA,
	0xAA888A88, 0xAA888A8A, 0xAA888AA8, 0xAA888AAA,
	0xAA88A888, 0xAA88A88A, 0xAA88A8A8, 0xAA88A8AA,
	0xAA88AA88, 0xAA88AA8A, 0xAA88AAA8, 0xAA88AAAA,
	0xAA8A8888, 0xAA8A888A, 0xAA8A88A8, 0xAA8A88AA,
	0xAA8A8A88, 0xAA8A8A8A, 0xAA8A8AA8, 0xAA8A8AAA,
	0xAA8AA888, 0xAA8AA88A, 0xAA8AA8A8, 0xAA8AA8AA,
	0xAA8AAA88, 0xAA8AAA8A, 0xAA8AAAA8, 0xAA8AAAAA,
	0xAAA88888, 0xAAA8888A, 0xAAA888A8, 0xAAA888AA,
	0xAAA88A88, 0xAAA88A8A, 0xAAA88AA8, 0xAAA88AAA,
	0xAAA8A888, 0xAAA8A88A, 0xAAA8A8A8, 0xAAA8A8AA,
	0xAAA8AA88, 0xAAA8AA8A, 0xAAA8AAA8, 0xAAA8AAAA,
	0xAAAA8888, 0xAAAA888A, 0xAAAA88A8, 0xAAAA88AA,
	0xAAAA8A88, 0xAAAA8A8A, 0xAAAA8AA8, 0xAAAA8AAA,
	0xAAAAA888, 0xAAAAA88A, 0xAAAAA8A8, 0xAAAAA8AA,
	0xAAAAAA88, 0xAAAAAA8A, 0xAAAAAAA8, 0xAAAAAAAA
};

//---------------------------------------------------------------------------
//	FMデータフィル
//
int FDX::fill_fm(uint8 *dst, uint8 data, int size)
{
	int i;
	uint32 ret;

	for (i = 0; i < size; i++) {
		ret = FMTable[data];

		*dst++ = (uint8)(ret >> 24);
		*dst++ = (uint8)(ret >> 16);
		*dst++ = (uint8)(ret >> 8);
		*dst++ = (uint8)ret;
	}

	return size * 4;
}

//---------------------------------------------------------------------------
//	FMデータ生成
//
int FDX::gen_fm(uint8 *dst, uint8 *src, int size)
{
	int i;
	uint32 ret;

	for (i = 0; i < size; i++) {
		ret = FMTable[*src];
		src++;

		*dst++ = (uint8)(ret >> 24);
		*dst++ = (uint8)(ret >> 16);
		*dst++ = (uint8)(ret >> 8);
		*dst++ = (uint8)ret;
	}

	return size * 4;
}
