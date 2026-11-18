// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Direct3D 11 の描画: 明るい所の周りの丸いぼけ (グロー) を作るピクセルシェーダー
//	640x400 の絵をリニアな明るさで、横 (Glow.z == 0: パレット番号の絵から) と
//	縦 (Glow.z == 1: 横にぼかした絵から) の 2 回に分けてガウスでぼかす。
//	ぼけの広さ Glow.x は元の画素の数 (縦も 640x400 の行で同じ数なので、丸く広がる)。
//	200 ラインなら偶数の行だけを読む (奇数の行の黒でぼけが暗くならないように)
// ---------------------------------------------------------------------------

Texture2D<uint>   Src : register(t0);	// 640x400 のパレット番号
Texture2D<float4> Pal : register(t1);	// 256x1 のパレット (sRGB)
Texture2D<float4> Tmp : register(t2);	// 横にぼかした絵 (縦のとき)

cbuffer CB : register(b0)
{
	float4 Dst;
	float4 Scan;						// 走査線の数 (200 / 400), ...
	float4 Mask;
	float4 Glow;						// ぼけの広さ, ぼけの量, ぼかす向き (0: 横 1: 縦), 未使用
};

static const int2 SrcSize = int2(640, 400);

float3 ToLinear(float3 c)
{
	return c <= 0.04045 ? c / 12.92 : pow(abs((c + 0.055) / 1.055), 2.4);
}

float4 main(float4 pos : SV_Position) : SV_Target
{
	int2 p = int2(pos.xy);
	float3 s = 0;
	float w = 0;
	[unroll] for (int k = -7; k <= 7; k++)
	{
		float wk = exp(-0.5 * (k / Glow.x) * (k / Glow.x));
		float3 c = 0;
		if (Glow.z == 0)
		{
			int x = p.x + k;
			int row = Scan.x == 200 ? (p.y & ~1) : p.y;
			if (x >= 0 && x < SrcSize.x)		// 絵の外は黒
				c = ToLinear(Pal.Load(int3(Src.Load(int3(x, row, 0)), 0, 0)).rgb);
		}
		else
		{
			int y = p.y + k;
			if (y >= 0 && y < SrcSize.y)
				c = Tmp.Load(int3(p.x, y, 0)).rgb;
		}
		s += c * wk;
		w += wk;
	}
	return float4(s / w, 1);
}
