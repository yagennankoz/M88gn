// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Direct3D 11 による画面描画
// ---------------------------------------------------------------------------
//	エミュレータが描いた 640x400 のパレット番号の絵 (8bpp) とパレット (256 色) を GPU に送り、
//	ピクセルシェーダーで色を付けて描く。拡大は最近傍 (今の D2D・GDI と同じ見た目)。
//	CRT 風のフィルター (走査線・蛍光体のマスク・明るい所の周りの丸いぼけ) もシェーダーで掛ける (SetCrtFilter)。
//	d3d11.dll は動的に読む。使えなければ Init が false を返し、WinDraw が D2D・GDI に替える。

#pragma once

#include "windraw.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11Buffer;
struct ID3D11SamplerState;

class WinDrawD3D11 : public WinDrawSub
{
public:
	WinDrawD3D11();
	~WinDrawD3D11();

	bool Init(HWND hwnd, uint w, uint h, GUID* g, int mag);
	void SetViewRect(const RECT* r);	// M88gn
	bool Resize(uint width, uint height);
	bool Cleanup();
	void SetPalette(PALETTEENTRY* pal, int index, int nentries);
	void DrawScreen(const RECT& rect, bool refresh);
	bool Lock(uint8** pimage, int* pbpl);
	bool Unlock();
	void SetLines(int lines);
	void SetCrtFilter(int scan, int mask, int sharp, int bright, int glow, int pitch, int breath);
	void SetRotation(int rot);

private:
	enum { srcw = 640, srch = 400 };

	bool CreateDevice();
	bool CreateTarget();
	void ReleaseDevice();
	void Render(int mode = 0);		// 0: 画面 1: 録画 (フィルター込み) 2: 録画 (フィルター無し)
	void RecordFrame(float ow, float oh);	// 録画中なら、録画の絵を描いて渡す (M88gn)
	bool CreateRecordTarget(int w, int h);
	void ReleaseRecordTarget();
	void RenderGlow(const float* cbdata);
	float MeasureLuminance();
	float BreathScale(float gain);

	HWND hwndparent;
	HWND hwnd;						// 描く子の窓
	uint width, height;				// 描く大きさ (倍率を掛けた後。全画面なら親の窓に合わせる)
	int mag;						// 倍率 (全画面なら 1)
	bool fit;
	RECT viewrect;			// ドックモードで置く四角 (hasview のとき。M88gn)
	bool hasview;						// 全画面: 親の窓いっぱいに、縦横比を保って拡大する
	uint bufw, bufh;				// スワップチェーンの大きさ

	uint8* image;					// エミュレータが描く絵 (パレット番号、640x400)
	uint32 palette[256];			// R8G8B8A8
	bool palchanged;
	bool devicelost;
	bool needrender;				// 絵は変わらないが描き直す (フィルター・走査線の数が変わった)

	int lines;						// 走査線の数 (200 / 400)
	int crtscan;					// 走査線 (0: なし 1: 弱 2: 中 3: 強)
	int crtmask;					// マスク (0: なし 1: アパーチャグリル 2: シャドウマスク 3: スロットマスク)
	int crtsharp;					// 横のシャープさ (0: Sharp 1: Normal 2: Soft)
	int crtbright;					// 明るさの補正 (0: Low 1: Normal 2: High)
	int crtpitch;					// マスクの間隔 (0: ピクセル単位 1: 0.25mm 2: 0.31mm 3: 0.39mm)
	int crtbreath;					// 画面全体の明るさで絵が伸び縮みする (bit1-0 = 0: なし 1: 弱 2: 中 3: 強、bit4 = 1: 中間の明るさで本来の大きさ、明るいと膨らむ)
	float lumtarget;				// 画面全体の平均の明るさ (リニア、0～1。-1: まだ測っていない)
	float lumgain;					// 出す明るさ / 絵の明るさ (走査線・マスク・明るさの補正・ぼけ。Render で求める)
	float lumnow;					// 遅れを付けた明るさ (伸び縮みに使う)
	DWORD lumtime;					// lumnow を進めた時刻 (ms)
	bool lumdirty;					// 絵かパレットが変わったので測り直す
	int rotation;					// 画面の回転 (0～3、右に 90° ずつ)
	int crtglow;					// 明るい所の周りの丸いぼけ (0: なし 1: 弱 2: 中 3: 強)

	HMODULE hd3d11;
	ID3D11Device* device;
	ID3D11DeviceContext* context;
	IDXGISwapChain* swapchain;
	ID3D11RenderTargetView* rtv;
	ID3D11Texture2D* srctex;
	ID3D11ShaderResourceView* srcsrv;
	ID3D11Texture2D* paltex;
	ID3D11ShaderResourceView* palsrv;
	ID3D11VertexShader* vs;
	ID3D11PixelShader* ps;
	ID3D11Buffer* cb;
	ID3D11PixelShader* psglow;		// ぼけを作る (drawd3d11_glow.hlsl)
	ID3D11Texture2D* glowtex[2];	// 0: 横にぼかした絵 1: 縦にもぼかした絵 (640x400、リニア)
	ID3D11ShaderResourceView* glowsrv[2];
	ID3D11RenderTargetView* glowrtv[2];
	ID3D11SamplerState* sampler;	// ぼけの絵を拡大する (双線形)
	// 録画 (M88gn): 録画の大きさの描き先と、読み戻す所
	ID3D11Texture2D* rectex;
	ID3D11RenderTargetView* recrtv;
	ID3D11Texture2D* recstaging;
	int recw, rech;
	float lastbs;					// 画面に描いたときの伸び縮みの倍率 (録画で同じものを使う)
};
