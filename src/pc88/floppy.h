// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator.
//	Copyright (C) cisc 1998, 1999.
// ---------------------------------------------------------------------------
//	$Id: floppy.h,v 1.5 2000/02/29 12:29:52 cisc Exp $

#pragma once

#include "types.h"

// ---------------------------------------------------------------------------
// FDXサポート

#define FDX_MAX_CYLINDER	84			/* 最大シリンダ数 */
#define FDX_MAX_HEADS		2			/* 最大ヘッド数 */
#define FDX_MAX_TRACK		168			/* 最大トラック数 */
#define FDX_MAX_SECTOR		96			/* 最大セクタ数 */
#define FDX_MAX_CELLS		0x4000		/* 16384 */
#define FDX_CELL_BITS		16			/* 1セル当たりのビット数 */
#define FDX_ENC_BYTES		0x8000		/* エンコードデータバッファサイズ */
#define FDX_RAW_BYTES		0x40000		/* RAWデータバッファサイズ */
#define FDX_TYPE_2D			0			// 2D
#define FDX_TYPE_2DD		1			// 2DD
#define FDX_TYPE_2HD		2			// 2HD
#define FDX_TYPE_RAW		9			// RAW
#define FDX_ERR_MASK		0xff000000	// エラーマスク
#define FDX_ERR_ICRC		0x40000000	// CRCエラー(ID)
#define FDX_ERR_IINV		0x20000000	// 不正エラー(ID)
#define FDX_ERR_DCRC		0x10000000	// CRCエラー(DATA)
#define FDX_ERR_DINV		0x08000000	// 不正エラー(DATA)
#define FDX_ERR_UNKN		0x01000000	// 解析エラー(その他)
#define FDX_INF_MASK		0x00ff0000	// 情報マスク
#define FDX_INF_ISTR		0x00800000	// インデックス跨り
#define FDX_INF_MFM			0x00400000	// MFMデータ
#define FDX_INF_FM			0x00200000	// FMデータ
#define FDX_INF_IAM			0x00100000	// IAM
#define FDX_INF_SECMSK		0x000000ff	// セクタ数マスク
#define FDX_SEC_MFM			1			// MFMセクタ
#define FDX_SEC_FM			2			// FMセクタ
#define FDX_FDD_NOERROR		0x0000		// エラー無し
#define FDX_FDD_DDAM		0x4000		// DDAMセクタ
#define FDX_FDD_DATAERR		0x2000		// IDCRCまたはデータCRC(ReadIDを除く)
#define FDX_FDD_IDCRC		0x0800		// IDフィールドCRC
#define FDX_FDD_MAM			0x0100		// アドレスマークなし
#define FDX_FDD_DATACRC		0x0020		// データCRC
#define FDX_FDD_MDAM		0x0001		// DAMが見つからない

static const uint8 FDX_IAM_MFM[16] = {
	0xAA, 0xAA, 0xAA, 0xAA,			// $00$00のエンコード
	0xAA, 0xAA, 0xAA, 0xAA,			// $00$00のエンコード
	0x52, 0x24, 0x52, 0x24,			// $C2$C2のエンコード
	0x52, 0x24, 0x55, 0x52			// $C2$FCのエンコード
};

static const uint8 FDX_IDAM_MFM[16] = {
	0xAA, 0xAA, 0xAA, 0xAA,			// $00$00のエンコード
	0xAA, 0xAA, 0xAA, 0xAA,			// $00$00のエンコード
	0x44, 0x89, 0x44, 0x89,			// $A1$A1のエンコード
	0x44, 0x89, 0x55, 0x54			// $A1$FEのエンコード
};

static const uint8 FDX_DAM_MFM[16] = {
	0xAA, 0xAA, 0xAA, 0xAA,			// $00$00のエンコード
	0xAA, 0xAA, 0xAA, 0xAA,			// $00$00のエンコード
	0x44, 0x89, 0x44, 0x89,			// $A1$A1のエンコード
	0x44, 0x89, 0x55, 0x45			// $A1$FBのエンコード
};

static const uint8 FDX_DDAM_MFM[16] = {
	0xAA, 0xAA, 0xAA, 0xAA,			// $00$00のエンコード
	0xAA, 0xAA, 0xAA, 0xAA,			// $00$00のエンコード
	0x44, 0x89, 0x44, 0x89,			// $A1$A1のエンコード
	0x44, 0x89, 0x55, 0x4A			// $A1$F8のエンコード
};

static const uint8 FDX_IAM_FM[12] = {
	0x88, 0x88, 0x88, 0x88,			// $00のエンコード
	0x88, 0x88, 0x88, 0x88,			// $00のエンコード
	0xAA, 0x2A, 0x2A, 0x88			// $FCのエンコード
};

static const uint8 FDX_IDAM_FM[12] = {
	0x88, 0x88, 0x88, 0x88,			// $00のエンコード
	0x88, 0x88, 0x88, 0x88,			// $00のエンコード
	0xAA, 0x22, 0x2A, 0xA8			// $FEのエンコード
};

static const uint8 FDX_DAM_FM[12] = {
	0x88, 0x88, 0x88, 0x88,			// $00のエンコード
	0x88, 0x88, 0x88, 0x88,			// $00のエンコード
	0xAA, 0x22, 0x28, 0xAA			// $FBのエンコード
};

static const uint8 FDX_DDAM_FM[12] = {
	0x88, 0x88, 0x88, 0x88,			// $00のエンコード
	0x88, 0x88, 0x88, 0x88,			// $00のエンコード
	0xAA, 0x22, 0x28, 0x88			// $F8のエンコード
};

class FDX
{
public:
	typedef struct {
		uint8 signature[3];						// 識別子('F','D','X')
		uint8 revision;							// リビジョン(3)
		uint8 name[60];							// ディスク名
		uint8 pad[4];							// パディング
		int type;								// タイプ(0:2D 1:2DD 2:2HD 9:RAW)
		int cylinders;							// シリンダー数
		int heads;								// ヘッド数
		int rate;								// 転送レート(クロック込み)
		int rpm;								// RPM
		uint32 writeprotect;						// ライトプロテクト(0: OFF 1:ON)
		uint32 option;							// 動作オプション
		uint32 unused;							// 未使用
		int trackblk;							// トラックブロック長(バイト数)
		uint8 reserve[152];						// リザーブ(全体256バイトに調整)
	} fdxheader_t;

	typedef struct {
		int cylinder;							// シリンダー
		int head;								// ヘッド
		int index;								// インデックスホール位置(ビット数)
		int length;								// トラックデータ長(ビット数)
		uint8 data[1];							// データポインタ
	} fdxtrack_t;

	typedef struct {
		int type;								// セクタタイプ(0:不正 1:MFM 2:FM)
		uint32 chrn[4];							// CHRN
		int gap2;								// GAP3サイズ
		int size;								// データサイズ
		int gap3;								// GAP3サイズ
		uint32 err;								// エラーコード
		uint8 *data;							// デコードデータ
		uint8 icrc[2];							// ID CRC
		uint8 dcrc[2];							// DATA CRC
		int idamoffset;							// IDAMオフセット
		int damoffset;							// DAM/DDAMオフセット
		int eosoffset;							// セクタ末尾オフセット
		uint8 *encdata;							// エンコードデータ
		int enclen;								// エンコードデータ長
	} fdxsecstat_t;

	typedef struct {
		uint32 status;							// ステータス
		int length;								// トラック長
		int num;								// セクタ数
		int iamoffset;							// IAMオフセット
		int gap1;								// GAP1サイズ
		int gap4a;								// GAP4aサイズ
		int gap4b;								// GAP4bサイズ
		fdxsecstat_t sector[FDX_MAX_SECTOR];	// セクターデータ
	} fdxtrkstat_t;

	static uint32 rand_xor()
	{
		static uint32 y = 0x92D68CA2;

		y = y ^ (y << 13);
		y = y ^ (y >> 17);
		y = y ^ (y << 5);

		return y;
	}

	static uint16 gen_crc(uint8 *ptr, int len);
	static void bf_setbf(uint8 *dst, int dlen, int offset, uint8 *src, int slen);
	static void bf_seek(uint8 *buf, int length, int offset);
	static int bf_splitclock(uint8 *buf, int length, int offset, uint8 *dbuf, int dlen, bool mfm, bool instab);
	static int encode_to_raw(uint8 *enc, int enclen, uint8 *raw, int rawlen);
	static int raw_to_encode(uint8 *raw, int rawlen, uint8 *enc, int enclen);
	static int encode_to_raw_pos(uint8 *raw, int rawlen, int encpos);
	static int gen_iam(uint8 *buf, bool mfm);
	static int gen_idam(uint8 *buf, bool mfm);
	static int gen_dam(uint8 *buf, bool del, bool mfm);
	static int fill_mfm(uint8 *dst, uint8 data, int size, int *last);
	static int fill_fm(uint8 *dst, uint8 data, int size);
	static int gen_mfm(uint8 *dst, uint8 *src, int size, int *last);
	static int gen_fm(uint8 *dst, uint8 *src, int size);
	static fdxtrkstat_t* analizetrack(uint8 *buf, int buflen, bool israw, bool needdata);
	static void release_analyzeobject(fdxtrkstat_t *stat);

private:
	static uint8 bf_getbit(const uint8 *buf, int length, int offset);
	static void bf_setbit(uint8 *buf, int length, int offset);
	static void bf_clrbit(uint8 *buf, int length, int offset);
	static uint8 bf_getbyte(const uint8 *buf, int length, int offset);
	static void bf_setbyte(uint8 *buf, int length, int offset, uint8 data);
	static void bf_rotateright(uint8 *buf, int length, int offset);
	static void bf_copy(uint8 *dst, int dlen, int doff, const uint8 *src, int slen, int soff, int len);
	static int bf_find_in(const uint8 *buf, int length, int offset, int limit, const uint8 *pat, int plen);
	static int bf_find(const uint8 *buf, int length, int offset, int limit, const uint8 *pat, int plen);
	static void bf_clr(uint8 *dst, int dlen, int offset, int slen);
	static int measure_pulse(int diff);
	static fdxtrkstat_t* analizetrack_in(fdxtrkstat_t *stat, uint8 *enc, int enclen, int idams[][2], bool trkmfm, bool needdata);
};

// ---------------------------------------------------------------------------
//	フロッピーディスク
//
class FloppyDisk
{
public:
	enum SectorFlags
	{
		deleted		= 1,
		datacrc		= 2,
		idcrc		= 4,
		mam			= 8,
		density		= 0x40,		// MFM = 0x40, FM = 0x00
		highdensity = 0x80,		// 2HD?
	};
	enum DiskType
	{
		MD2D=0, MD2DD, MD2HD
	};
	struct IDR
	{
		uint8 c, h, r, n;

		bool operator==(const IDR& i)
		{ return ((c == i.c) && (h == i.h) && (r == i.r) && (n == i.n)); }
	};

	struct Sector
	{
		IDR id;
		uint flags;
		uint8* image;
		uint size;
		uint gap3;
		bool update;
		Sector* next;

		// FDX拡張
		int fdx_enclen;
		uint8* fdx_encdata;
		int fdx_ioffset;
		int fdx_doffset;
		int fdx_eoffset;
	};

	class Track
	{
	public:
		Track() : sector(0)
		{
			formated = false;

			// FDX拡張
			fdx_track = 0;
			fdx_rawlen = 0;
			fdx_rawdata = 0;
		}
		~Track()
		{
			for (Sector* s = sector; s;)
			{
				Sector* n = s->next;

				// FDX拡張
				if (s->fdx_encdata) {
					delete[] s->fdx_encdata;
					s->fdx_encdata = 0;
				}

				delete[] s->image;
				delete s;
				s = n;
			}

			// FDX拡張
			if (fdx_track) {
				delete fdx_track;
				fdx_track = 0;
			}

			fdx_rawrate = 0;
			fdx_rawlen = 0;

			if (fdx_rawdata) {
				delete[] fdx_rawdata;
				fdx_rawdata = 0;
			}
		}
		
		Sector* sector;
		bool formated;

		// FDX拡張
		FDX::fdxtrack_t *fdx_track;
		int fdx_rawrate;
		int fdx_rawlen;
		uint8* fdx_rawdata;
	};

public:
	FloppyDisk();
	~FloppyDisk();

	bool Init(DiskType type, bool readonly);
		
	bool IsReadOnly() { return readonly; }
	DiskType GetType() { return type; }
	
	Track* Seek(uint tr);
	Track* GetTrack();
	Sector* GetSector();
	bool FindID(IDR idr, uint density);
	uint GetNumSectors();
	uint GetTrackCapacity();
	uint GetTrackSize();
	uint GetNumTracks() { return ntracks; }
	bool Resize(Sector* sector, uint newsize);
	bool FormatTrack(int nsec, int secsize);
	Sector* AddSector(int secsize);
	Sector* GetFirstSector(uint track);
	void IndexHole() { cursector = 0; }

	// FDX拡張
	bool IsFdx() { return fdx_header.signature[0] == 'F'; }

private:
	Track tracks[168];
	int ntracks;
	DiskType type;
	bool readonly;

	Track* curtrack;
	Sector* cursector;
	uint curtracknum;

public:
	// FDX拡張
	FDX::fdxheader_t fdx_header;
};
