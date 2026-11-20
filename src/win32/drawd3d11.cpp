// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Direct3D 11 による画面描画
// ---------------------------------------------------------------------------

#include "headers.h"
#include <d3d11.h>
#include <math.h>
#include "drawd3d11.h"
#include "misc.h"

// シェーダー (ビルドのときに fxc で作る。drawd3d11_vs.hlsl / drawd3d11_ps.hlsl)
#include "drawd3d11_vs.h"
#include "drawd3d11_ps.h"
#include "drawd3d11_glow.h"

typedef HRESULT (WINAPI* PFD3D11CreateDeviceAndSwapChain)(
	IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
	const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

template <class T> static void SafeRelease(T** p)
{
	if (*p)
	{
		(*p)->Release();
		*p = 0;
	}
}

// ---------------------------------------------------------------------------
//	構築/消滅
//
WinDrawD3D11::WinDrawD3D11()
{
	hwndparent = 0;
	hwnd = 0;
	width = srcw, height = srch;
	mag = 1;
	fit = false;
	bufw = bufh = 0;
	image = 0;
	memset(palette, 0, sizeof(palette));
	palchanged = true;
	devicelost = false;
	needrender = false;
	lines = 400;
	crtscan = 0;
	crtmask = 0;
	crtsharp = 1;
	crtbright = 1;
	crtglow = 0;
	crtpitch = 0;
	crtbreath = 0;
	rotation = 0;
	lumtarget = -1;
	lumnow = 0;
	lumgain = 1;
	lumtime = 0;
	lumdirty = true;
	hd3d11 = 0;
	device = 0;
	context = 0;
	swapchain = 0;
	rtv = 0;
	srctex = 0;
	srcsrv = 0;
	paltex = 0;
	palsrv = 0;
	vs = 0;
	ps = 0;
	cb = 0;
	psglow = 0;
	sampler = 0;
	for (int i = 0; i < 2; i++)
		glowtex[i] = 0, glowsrv[i] = 0, glowrtv[i] = 0;
}

WinDrawD3D11::~WinDrawD3D11()
{
	Cleanup();
	delete[] image;
	if (hd3d11)
		FreeLibrary(hd3d11);
}

// ---------------------------------------------------------------------------
//	初期化 (mag = 0: 全画面。親の窓いっぱいに、縦横比を保って拡大する)
//
bool WinDrawD3D11::Init(HWND hwnd_, uint w, uint h, GUID*, int mag_)
{
	hwndparent = hwnd_;
	fit = mag_ == 0;
	mag = fit ? 1 : mag_;

	if (!image)
	{
		image = new uint8[srcw * srch];
		if (!image)
			return false;
		memset(image, 0x40, srcw * srch);
	}

	// 描く子の窓 (D2D と同じく、親の窓の左上に置く)
	hwnd = CreateWindowEx(WS_EX_TRANSPARENT, "M88p2 WinUI", "", WS_CHILD,
						  0, 0, Max(1, int(w)), Max(1, int(h)), hwndparent, 0, 0, 0);
	if (!hwnd)
		return false;
	ShowWindow(hwnd, SW_SHOW);

	width = w, height = h;
	if (!CreateDevice())
	{
		Cleanup();
		return false;
	}
	return Resize(w, h);
}

bool WinDrawD3D11::Cleanup()
{
	ReleaseDevice();
	if (hwnd)
	{
		DestroyWindow(hwnd);
		hwnd = 0;
	}
	return true;
}

// ---------------------------------------------------------------------------
//	デバイス・スワップチェーン・テクスチャ・シェーダーを作る
//
bool WinDrawD3D11::CreateDevice()
{
	if (!hd3d11)
		hd3d11 = LoadLibrary("d3d11.dll");
	if (!hd3d11)
		return false;
	PFD3D11CreateDeviceAndSwapChain create =
		(PFD3D11CreateDeviceAndSwapChain) GetProcAddress(hd3d11, "D3D11CreateDeviceAndSwapChain");
	if (!create)
		return false;

	RECT rc;
	GetClientRect(hwnd, &rc);
	bufw = Max(1, int(rc.right - rc.left));
	bufh = Max(1, int(rc.bottom - rc.top));

	DXGI_SWAP_CHAIN_DESC sd;
	memset(&sd, 0, sizeof(sd));
	sd.BufferDesc.Width = bufw;
	sd.BufferDesc.Height = bufh;
	sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sd.SampleDesc.Count = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hwnd;
	sd.Windowed = TRUE;

	static const D3D_FEATURE_LEVEL levels[] =
	{
		D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
	};
	D3D_FEATURE_LEVEL got;

	// フリップモデル (Windows 10 以降) が使えなければ、古い方式にする
	// バッファーは 3 つ (2 つだと、前の絵を画面に出し終わるまで Present が待ち、エミュレーションの 1 フレームを超えて 30fps になる)
	sd.BufferCount = 3;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	HRESULT hr = create(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, levels, sizeof(levels) / sizeof(levels[0]),
						D3D11_SDK_VERSION, &sd, &swapchain, &device, &got, &context);
	if (FAILED(hr))
	{
		sd.BufferCount = 1;
		sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		hr = create(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, levels, sizeof(levels) / sizeof(levels[0]),
					D3D11_SDK_VERSION, &sd, &swapchain, &device, &got, &context);
	}
	if (FAILED(hr))
		return false;

	// DXGI が Alt+Enter で勝手に全画面にしないようにする (全画面は M88 が自分で切り替える)
	IDXGIFactory* factory = 0;
	if (SUCCEEDED(swapchain->GetParent(__uuidof(IDXGIFactory), (void**) &factory)))
	{
		factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
		factory->MakeWindowAssociation(hwndparent, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
		factory->Release();
	}

	// 絵 (パレット番号) とパレットのテクスチャ
	D3D11_TEXTURE2D_DESC td;
	memset(&td, 0, sizeof(td));
	td.Width = srcw;
	td.Height = srch;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R8_UINT;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(device->CreateTexture2D(&td, 0, &srctex)) ||
		FAILED(device->CreateShaderResourceView(srctex, 0, &srcsrv)))
		return false;

	td.Width = 256;
	td.Height = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	if (FAILED(device->CreateTexture2D(&td, 0, &paltex)) ||
		FAILED(device->CreateShaderResourceView(paltex, 0, &palsrv)))
		return false;

	if (FAILED(device->CreateVertexShader(g_drawd3d11_vs, sizeof(g_drawd3d11_vs), 0, &vs)) ||
		FAILED(device->CreatePixelShader(g_drawd3d11_ps, sizeof(g_drawd3d11_ps), 0, &ps)) ||
		FAILED(device->CreatePixelShader(g_drawd3d11_glow, sizeof(g_drawd3d11_glow), 0, &psglow)))
		return false;

	// 丸いぼけの絵 (640x400、リニアな明るさなので 16bit の浮動小数)
	td.Width = srcw;
	td.Height = srch;
	td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	for (int i = 0; i < 2; i++)
	{
		if (FAILED(device->CreateTexture2D(&td, 0, &glowtex[i])) ||
			FAILED(device->CreateShaderResourceView(glowtex[i], 0, &glowsrv[i])) ||
			FAILED(device->CreateRenderTargetView(glowtex[i], 0, &glowrtv[i])))
			return false;
	}
	D3D11_SAMPLER_DESC smp;
	memset(&smp, 0, sizeof(smp));
	smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	smp.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(device->CreateSamplerState(&smp, &sampler)))
		return false;

	D3D11_BUFFER_DESC bd;
	memset(&bd, 0, sizeof(bd));
	bd.ByteWidth = 96;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	if (FAILED(device->CreateBuffer(&bd, 0, &cb)))
		return false;

	if (!CreateTarget())
		return false;

	palchanged = true;
	status |= Draw::shouldrefresh;
	return true;
}

bool WinDrawD3D11::CreateTarget()
{
	ID3D11Texture2D* back = 0;
	if (FAILED(swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**) &back)))
		return false;
	HRESULT hr = device->CreateRenderTargetView(back, 0, &rtv);
	back->Release();
	return SUCCEEDED(hr);
}

void WinDrawD3D11::ReleaseDevice()
{
	if (context)
		context->ClearState();
	SafeRelease(&cb);
	SafeRelease(&sampler);
	for (int i = 0; i < 2; i++)
	{
		SafeRelease(&glowrtv[i]);
		SafeRelease(&glowsrv[i]);
		SafeRelease(&glowtex[i]);
	}
	SafeRelease(&psglow);
	SafeRelease(&ps);
	SafeRelease(&vs);
	SafeRelease(&palsrv);
	SafeRelease(&paltex);
	SafeRelease(&srcsrv);
	SafeRelease(&srctex);
	SafeRelease(&rtv);
	SafeRelease(&swapchain);
	SafeRelease(&context);
	SafeRelease(&device);
	bufw = bufh = 0;
}

// ---------------------------------------------------------------------------
//	大きさを変える (width, height は倍率を掛けた後の大きさ)
//
bool WinDrawD3D11::Resize(uint w, uint h)
{
	width = w, height = h;
	status |= Draw::shouldrefresh;

	// 描く窓の大きさ (全画面なら親の窓のクライアント領域いっぱい)
	int cw = width, ch = height;
	if (fit)
	{
		RECT pr;
		GetClientRect(hwndparent, &pr);
		cw = pr.right - pr.left;
		ch = pr.bottom - pr.top;
	}
	cw = Max(1, cw), ch = Max(1, ch);
	SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, cw, ch, SWP_SHOWWINDOW);

	if (swapchain && (uint(cw) != bufw || uint(ch) != bufh))
	{
		context->OMSetRenderTargets(0, 0, 0);
		SafeRelease(&rtv);
		if (FAILED(swapchain->ResizeBuffers(0, cw, ch, DXGI_FORMAT_UNKNOWN, 0)))
		{
			devicelost = true;
			return true;
		}
		bufw = cw, bufh = ch;
		if (!CreateTarget())
			devicelost = true;
	}
	return true;
}

// ---------------------------------------------------------------------------
//	パレット
//
void WinDrawD3D11::SetPalette(PALETTEENTRY* pe, int index, int nentries)
{
	for (; nentries > 0; nentries--, index++, pe++)
		palette[index] = pe->peRed | (pe->peGreen << 8) | (pe->peBlue << 16) | 0xff000000;
	palchanged = true;
}

// ---------------------------------------------------------------------------
//	描く (変わった行だけ GPU に送り、全体を描き直す)
//
void WinDrawD3D11::DrawScreen(const RECT& _rect, bool refresh)
{
	if (devicelost)
	{
		// GPU が外れた・ドライバが更新されたなど: 作り直す
		ReleaseDevice();
		devicelost = false;
		if (!CreateDevice())
		{
			ReleaseDevice();
			devicelost = true;
			return;
		}
		refresh = true;
	}
	if (!device || !rtv)
		return;

	RECT rc = _rect;
	bool valid = rc.left < rc.right && rc.top < rc.bottom;
	if (refresh)
	{
		SetRect(&rc, 0, 0, srcw, srch);
		valid = true;
	}
	if (valid || palchanged)
		lumdirty = true;
	// 伸び縮み: 明るさが変わったら測り直し、遅れを付けた明るさが追いつくまで描き直し続ける
	if ((crtbreath & 3) > 0)
	{
		if (lumdirty)
		{
			lumdirty = false;
			lumtarget = MeasureLuminance();
		}
		float eff = lumtarget * lumgain;
		if (fabsf(lumnow - (eff < 1 ? eff : 1)) > 0.0005f)
			needrender = true;
	}
	if (!valid && !palchanged && !needrender)
		return;
	needrender = false;

	if (valid)
	{
		rc.left = Max(0, int(rc.left)), rc.top = Max(0, int(rc.top));
		rc.right = Min(int(srcw), int(rc.right)), rc.bottom = Min(int(srch), int(rc.bottom));
		if (rc.left < rc.right && rc.top < rc.bottom)
		{
			D3D11_BOX box = { UINT(rc.left), UINT(rc.top), 0, UINT(rc.right), UINT(rc.bottom), 1 };
			context->UpdateSubresource(srctex, 0, &box, image + rc.top * srcw + rc.left, srcw, 0);
		}
	}
	if (palchanged)
	{
		palchanged = false;
		context->UpdateSubresource(paltex, 0, 0, palette, sizeof(palette), 0);
	}
	Render();
}

void WinDrawD3D11::Render()
{
	// 描く範囲: ウィンドウなら倍率どおり、全画面なら縦横比を保っていっぱいに
	// 90°・270° に回すときは、出力の上で縦横が入れ替わる (ow, oh)。dw, dh は回す前の絵の大きさ
	bool side = (rotation & 1) != 0;
	uint sw = side ? srch : srcw, sh = side ? srcw : srch;
	float dx = 0, dy = 0, ow = float(sw * mag), oh = float(sh * mag);
	if (fit)
	{
		if (bufw * sh <= bufh * sw)
			ow = float(bufw), oh = float(bufw * sh / sw);
		else
			oh = float(bufh), ow = float(bufh * sw / sh);
		dx = float(int((bufw - ow) / 2));
		dy = float(int((bufh - oh) / 2));
	}
	float dw = side ? oh : ow, dh = side ? ow : oh;

	// フィルターの定数 (drawd3d11_ps.hlsl の CB)
	//	走査線の鋭さ: ガウスの指数。線のすき間の明るさは、弱で中央の約 8 割、中で約半分、強で約 1 割
	//	マスク: 明るい色 1.5、ほかの 2 色 0.5 (平均で約 0.83)
	//	横のシャープさ: 隣の画素とのなじませ方 (ガウスの指数)。半画素離れた所の重みは Sharp で約 0.18、Normal で約 0.35、Soft で約 0.59
	//	明るさの補正: 走査線とマスクで落ちた平均の明るさを戻す割合 (指数)。Low 0.35、Normal 0.7、High 1.0
	//	(補正は掛け算なので、上げるほど文字の横のにじみも明るくなり、白は飽和して線が太く見える)
	//	マスクの間隔: 実在の CRT の蛍光体の 1 組の幅 (mm)。エミュレーターの画面の横 (640 ドット) を 14 インチのモニターの表示幅 240mm とみなし、
	//	  出力のピクセルに直す (4K の全画面 3456 ピクセルで 0.31mm なら約 4.5 ピクセル)。Pixel なら今までどおり出力のピクセル単位の模様
	//	丸いぼけ: 明るい所の周りに、ぼかした絵を足す。量は弱 0.1・中 0.2・強 0.35、広さ (ガウスの σ) は元の画素で 0.7 (Soft なら 1.4)
	static const float hardscan[4] = { 0, -5, -9, -16 };
	static const float hardpix[3] = { -10, -6, -3 };
	static const float brightexp[3] = { 0.35f, 0.7f, 1.0f };
	static const float glowamount[4] = { 0, 0.1f, 0.2f, 0.35f };
	float hscan = hardscan[Limit(crtscan, 3, 0)];
	int mtype = Limit(crtmask, 3, 0);
	int sharp = Limit(crtsharp, 2, 0);
	float gamount = glowamount[Limit(crtglow, 3, 0)];
	bool filter = crtscan > 0 || mtype > 0 || gamount > 0;
	float mdark = 0.5f, mlight = 1.5f;
	float mean = 1;
	if (hscan < 0)
		mean *= sqrtf(3.14159265f / (-hscan * 0.69314718f));	// ガウスの面積 (1 本の間隔あたり)
	if (mtype == 3)
		mean *= (mlight + 2 * mdark) / 3 * (3 + mdark) / 4;	// スロットマスクは 4 行に 1 行暗い
	else if (mtype > 0)
		mean *= (mlight + 2 * mdark) / 3;
	static const float pitchmm[4] = { 0, 0.25f, 0.31f, 0.39f };
	float ppitch = pitchmm[Limit(crtpitch, 3, 0)] * dw / 240.0f;		// 蛍光体の 1 組の幅 (出力のピクセル)
	if (ppitch > 0 && (mtype == 2 || mtype == 3))
		mean *= 1 - 0.15f * (1 - mdark);							// 実寸のシャドウマスク・スロットマスクの区切り
	if (ppitch > 0 && mtype == 3)
		mean /= (3 + mdark) / 4;									// ピクセル単位のスロットマスクの区切りは使わない
	float bright = powf(1 / (mean > 0.1f ? mean : 0.1f), brightexp[Limit(crtbright, 2, 0)]);
	float hpix = hardpix[sharp];

	// 伸び縮み: 偏向の角度に掛ける倍率 (真っ白で 1)。画面の端の偏向の角度の tan は、14 インチの 90° の管 (4:3) を想定して
	// 横 0.8・縦 0.6。シェーダーが出力の画素ごとに元の位置を逆算する (外周ほど大きく動く。画面の形はマスクで決める)
	// アパーチャグリルの横のワイヤーの影: 太さ 0.09mm を、14 インチの画面の高さ 180mm として出力のピクセルに直す
	// 伸び縮みの元の明るさは、フィルターで出す明るさ (走査線・マスクで落ちる分 × 明るさの補正、ぼけの足し分)
	float bs = BreathScale(filter ? mean * bright * (1 + gamount) : 1);
	float wire = (mtype == 1) ? 0.09f * dh / 180.0f : 0;

	float cbdata[24] =
	{
		dx, dy, dw, dh,
		float(lines == 200 ? 200 : 400), hscan, hpix, bright,
		float(mtype), mdark, mlight, filter ? 1.0f : 0.0f,
		sharp == 2 ? 1.4f : 0.7f, gamount, 0, ppitch,
		bs, 0.8f, 0.6f, wire,
		float(rotation & 3), 0, 0, 0,
	};

	context->IASetInputLayout(0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(vs, 0, 0);
	context->PSSetConstantBuffers(0, 1, &cb);
	if (filter && gamount > 0)
		RenderGlow(cbdata);

	context->UpdateSubresource(cb, 0, 0, cbdata, 0, 0);
	static const float black[4] = { 0, 0, 0, 1 };
	context->OMSetRenderTargets(1, &rtv, 0);
	context->ClearRenderTargetView(rtv, black);
	D3D11_VIEWPORT vp = { dx, dy, ow, oh, 0, 1 };
	context->RSSetViewports(1, &vp);
	context->PSSetShader(ps, 0, 0);
	ID3D11ShaderResourceView* srv[3] = { srcsrv, palsrv, glowsrv[1] };
	context->PSSetShaderResources(0, 3, srv);
	context->PSSetSamplers(0, 1, &sampler);
	context->Draw(3, 0);
	ID3D11ShaderResourceView* none[3] = { 0, 0, 0 };
	context->PSSetShaderResources(0, 3, none);		// 次にぼけの絵へ描くときのために外す

	// 垂直同期を待たない。描画はエミュレーションのスレッドで呼ばれるので、待つと 1 フレームの時間を超え、
	// 次のフレームの描画が飛ばされて 30fps で頭打ちになる。フリップモデルなら、DWM が次の垂直同期で
	// いちばん新しい絵を出すので、ちらつき (テアリング) は出ない
	HRESULT hr = swapchain->Present(0, 0);
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
		devicelost = true;
}

// ---------------------------------------------------------------------------
//	明るい所の周りの丸いぼけの絵を作る (640x400 を横、縦の順にぼかす)
//
void WinDrawD3D11::RenderGlow(const float* cbdata)
{
	float c[24];
	memcpy(c, cbdata, sizeof(c));
	D3D11_VIEWPORT vp = { 0, 0, float(srcw), float(srch), 0, 1 };
	context->RSSetViewports(1, &vp);
	context->PSSetShader(psglow, 0, 0);
	for (int pass = 0; pass < 2; pass++)
	{
		c[14] = float(pass);
		context->UpdateSubresource(cb, 0, 0, c, 0, 0);
		ID3D11ShaderResourceView* none[3] = { 0, 0, 0 };
		context->PSSetShaderResources(0, 3, none);
		context->OMSetRenderTargets(1, &glowrtv[pass], 0);
		ID3D11ShaderResourceView* srv[3] = { srcsrv, palsrv, pass ? glowsrv[0] : 0 };
		context->PSSetShaderResources(0, 3, srv);
		context->Draw(3, 0);
	}
	context->OMSetRenderTargets(0, 0, 0);
}

// ---------------------------------------------------------------------------
//	エミュレータが描く絵
//
bool WinDrawD3D11::Lock(uint8** pimage, int* pbpl)
{
	*pimage = image;
	*pbpl = srcw;
	return image != 0;
}

bool WinDrawD3D11::Unlock()
{
	status &= ~Draw::shouldrefresh;
	return true;
}

// ---------------------------------------------------------------------------
//	走査線の数 (200 / 400 ライン。エミュレータの画面のモード)
//
void WinDrawD3D11::SetLines(int l)
{
	l = (l == 200) ? 200 : 400;
	if (l != lines)
	{
		lines = l;
		needrender = true;
	}
}

// ---------------------------------------------------------------------------
//	CRT 風のフィルター
//
void WinDrawD3D11::SetRotation(int rot)
{
	if (rot != rotation)
	{
		rotation = rot;
		needrender = true;
	}
}

void WinDrawD3D11::SetCrtFilter(int scan, int mask, int sharp, int bright, int glow, int pitch, int breath)
{
	if (scan != crtscan || mask != crtmask || sharp != crtsharp || bright != crtbright || glow != crtglow || pitch != crtpitch || breath != crtbreath)
	{
		crtscan = scan, crtmask = mask, crtsharp = sharp, crtbright = bright, crtglow = glow, crtpitch = pitch;
		if (breath != crtbreath)
		{
			crtbreath = breath;
			lumtarget = -1;			// 次に測った明るさから始める (いきなり伸び縮みしない)
			lumdirty = true;
		}
		needrender = true;
	}
}

// ---------------------------------------------------------------------------
//	画面全体の平均の明るさ (リニア、0～1)。ブラウン管のビームの電流に比例するものとして、
//	パレットの色を sRGB からリニアに直し、輝度の重みで足す。200 ラインなら偶数の行だけ (描くのと同じ)
//
float WinDrawD3D11::MeasureLuminance()
{
	if (!image)
		return 0;
	uint hist[256];
	memset(hist, 0, sizeof(hist));
	int step = (lines == 200) ? 2 : 1;
	uint n = 0;
	for (uint y = 0; y < srch; y += step)
	{
		const uint8* src = image + y * srcw;
		for (uint x = 0; x < srcw; x++)
			hist[src[x]]++;
		n += srcw;
	}
	double sum = 0;
	for (int i = 0; i < 256; i++)
	{
		if (!hist[i])
			continue;
		uint32 c = palette[i];
		double r = pow((c & 0xff) / 255.0, 2.2);
		double g = pow(((c >> 8) & 0xff) / 255.0, 2.2);
		double b = pow(((c >> 16) & 0xff) / 255.0, 2.2);
		sum += hist[i] * (0.2126 * r + 0.7152 * g + 0.0722 * b);
	}
	return n ? float(sum / n) : 0;
}

// ---------------------------------------------------------------------------
//	伸び縮みしたときに、左右の辺の中ほどが内側に動く割合 (画面の中心から端までに対して)。
//	drawd3d11_ps.hlsl の Unbend (アパーチャグリル) / UnbendRadial (ほか) と同じ計算で、元の絵の端が来る位置を二分法で求める
//
static double BreathBend(double th, double f)
{
	return (1 - f) * th + f * tan(th);
}

static double BreathUnbend(double target, double f)
{
	double th = atan(target);
	for (int i = 0; i < 8; i++)
	{
		double c = cos(th);
		th -= (BreathBend(th, f) - target) / ((1 - f) + f / (c * c));
	}
	return th;
}

static double BreathEdgeShrink(double g, bool grille)
{
	const double tx = 0.8;							// 画面の端の偏向の角度の tan (横)
	double lo = 0.5, hi = 1;						// 出力の位置 (中心 0、端 1)
	for (int i = 0; i < 40; i++)
	{
		double pm = (lo + hi) / 2, src;
		if (grille)
		{
			double pmax = BreathBend(atan(tx), 0.7);
			src = BreathBend(BreathUnbend(pm * pmax, 0.7) / g, 0.7) / pmax;
		}
		else
		{
			const double rc = 1.0;					// 角までの距離 (tan の単位、0.8 と 0.6 の斜辺)
			double r = pm * tx;
			double pc = BreathBend(atan(rc), 0.6);
			double r2 = BreathBend(BreathUnbend(r / rc * pc, 0.6) / g, 0.6) / pc * rc;
			double rn = r / rc;
			r2 *= 1 + (1 - g) * rn * rn;
			src = r2 / tx;
		}
		if (src > 1)
			hi = pm;
		else
			lo = pm;
	}
	return 1 - lo;
}

// ---------------------------------------------------------------------------
//	伸び縮みの倍率 (偏向の角度に掛ける)。出す明るさ (絵の明るさ × gain) に遅れ (時定数 0.03 秒、ブラウン管の高圧の電源の応答) を付ける。
//	強さは、真っ黒と真っ白の間で左右の辺の中ほどが動く割合 (弱 1%・中 2%・強 3%。強は伸び縮みの目立つ管のつもり)。
//	角度の倍率は、管の形 (マスク) ごとに辺の動きと倍率の比を求めて逆算する (角は辺の中ほどより大きく動く)
//
float WinDrawD3D11::BreathScale(float gain)
{
	static const float amount[4] = { 0, 0.01f, 0.02f, 0.03f };
	static float ratio[2] = { 0, 0 };				// 辺の中ほどの動き / (1 - 倍率)。0: ほかの管 1: アパーチャグリル
	float k = amount[crtbreath & 3];
	lumgain = gain;
	DWORD now = timeGetTime();
	if (k == 0 || lumtarget < 0)
	{
		lumtime = 0;			// 次に測れたら、その明るさから始める
		return 1;
	}
	if (lumtime == 0)
		lumnow = (lumtarget * gain < 1 ? lumtarget * gain : 1), lumtime = now;
	float dt = Min(int(now - lumtime), 500) / 1000.0f;
	lumtime = now;
	float eff = lumtarget * gain;
	if (eff > 1)
		eff = 1;
	lumnow += (eff - lumnow) * (1 - expf(-dt / 0.03f));
	float l = lumnow < 0 ? 0 : (lumnow > 1 ? 1 : lumnow);
	int gi = (crtmask == 1) ? 1 : 0;
	if (ratio[gi] == 0)
		ratio[gi] = float(BreathEdgeShrink(0.98, gi == 1) / 0.02);
	// 黒から明るくなり始めた所で大きく伸び、白に近づくほど緩やかにする (平方根)。
	// 基準: 真っ白で本来の大きさ (暗いと縮む)、または中間の明るさ (平方根で半分、出す明るさ 0.25) で本来の大きさ
	// (暗いと縮み、明るいと膨らむ。それぞれ強さの半分。膨らむと端の絵は少し切れる)。
	// 膨らむときも縮むときの比を使う (動きが小さいので、ほぼ同じ)
	if (crtbreath & 0x10)
		return 1 + k / ratio[gi] * (sqrtf(l) - 0.5f);
	return 1 - k / ratio[gi] * (1 - sqrtf(l));
}
