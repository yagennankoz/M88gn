// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Direct3D 11 の描画: ピクセルシェーダー
//	パレット番号の絵 (640x400) からパレットで色を付ける。
//	フィルター無しなら最近傍の拡大 (今までと同じ絵)。
//	CRT 風のフィルター: 横はビームのにじみ、縦は走査線、出力のピクセルごとに蛍光体のマスク、
//	明るい所の周りの丸いぼけ (drawd3d11_glow.hlsl で作った絵を拡大して足す)。
//	計算はリニアな明るさで行い、最後に sRGB に戻す
// ---------------------------------------------------------------------------

Texture2D<uint>   Src : register(t0);	// 640x400 のパレット番号
Texture2D<float4> Pal : register(t1);	// 256x1 のパレット (sRGB)
Texture2D<float4> GlowTex : register(t2);	// 640x400 のぼかした絵 (リニア)
SamplerState Lin : register(s0);		// 双線形

cbuffer CB : register(b0)
{
	float4 Dst;							// 描く範囲 (x, y, 幅, 高さ。出力のピクセル)
	float4 Scan;						// 走査線の数 (200 / 400), 走査線の鋭さ (0: 走査線なし), 横のにじみの鋭さ, 明るさの補正
	float4 Mask;						// マスクの種類 (0: なし 1: アパーチャグリル 2: シャドウマスク 3: スロットマスク 4: アパーチャグリル (FD)), 暗い所, 明るい所, フィルター (0: なし 1: あり)
	float4 Glow;						// ぼけの広さ (元の画素), ぼけの量 (0: なし), ぼかす向き (drawd3d11_glow.hlsl), マスクの蛍光体の 1 組の幅 (出力のピクセル。0: ピクセル単位のマスク)
	float4 Breath;						// 伸び縮み: 偏向の角度に掛ける倍率 (1: なし), 画面の端の偏向の角度の tan (横, 縦), アパーチャグリルのワイヤーの影の太さ (出力のピクセル。0: なし)
	float4 View;						// 画面の回転 (0～3、右に 90° ずつ), プラズマ (0: なし 1: あり), プラズマの隔壁の濃さ, プラズマの階調の数 - 1
	float4 Mono;						// 白黒の管: 蛍光体の色 (sRGB), 種類 (0: なし 1: 白黒の管 2: デジタル RGB)
	float4 Edge;						// 周辺減光の量 (四隅で暗くなる割合), 減光の縦の重み (横は 1 - 縦), 周辺のぼけ (四隅でビームの点が 1 + この値の倍), ぼけの縦の重み
};

static const int2 SrcSize = int2(640, 400);

float3 ToLinear(float3 c)
{
	return c <= 0.04045 ? c / 12.92 : pow(abs((c + 0.055) / 1.055), 2.4);
}

float3 ToSrgb(float3 c)
{
	return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1 / 2.4) - 0.055;
}

float3 Fetch(int x, int row)
{
	int2 p = clamp(int2(x, row), int2(0, 0), SrcSize - 1);
	float3 c = Pal.Load(int3(Src.Load(int3(p, 0)), 0, 0)).rgb;
	if (Mono.w > 1.5)
		c = step(0.5, c);											// デジタル RGB: 0～7 の 4 以上 (146/255 以上) で点く
	else if (Mono.w > 0)
		c = dot(c, float3(0.27, 0.55, 0.18)) * Mono.rgb;		// 白黒の管: 輝度を蛍光体の色で (NTSC の重みより青を持ち上げる)
	return c;
}

// 指数が h のガウス (d = 0 で 1)
float Gaus(float d, float h)
{
	return exp2(h * d * d);
}

// 横: 近くの 6 画素をビームの太さ (ガウスの指数 h) でなじませる (リニアな明るさ。重みの和で割る)
float3 Horz(float sx, int row, float h)
{
	float x = sx - 0.5;
	int i = (int) floor(x);
	float f = x - i;
	float3 s = 0;
	float w = 0;
	[unroll] for (int k = -2; k <= 3; k++)
	{
		float wk = Gaus(k - f, h);
		s += ToLinear(Fetch(i + k, row)) * wk;
		w += wk;
	}
	return s / w;
}

// 蛍光体のマスク (出力のピクセルの座標で掛ける。描く範囲の左上を原点にする)
// 0: R、1: G、2: B だけを明るく
float3 Phosphor(uint i)
{
	return i == 0 ? float3(Mask.z, Mask.y, Mask.y)
		 : i == 1 ? float3(Mask.y, Mask.z, Mask.y)
		 :          float3(Mask.y, Mask.y, Mask.z);
}

// 周期 1 の [lo, lo + w) のしまが、区間 [a, b] に占める割合
float Cover(float a, float b, float lo, float w)
{
	float fa = floor(a) * w + clamp(frac(a) - lo, 0, w);
	float fb = floor(b) * w + clamp(frac(b) - lo, 0, w);
	return (fb - fa) / (b - a);
}

// 実在の CRT の寸法のマスク: 蛍光体の 1 組の幅を p (出力のピクセル、半端でよい) とし、
// 出力のピクセル (pos は左上が原点のピクセルの真ん中) に R・G・B のしまが占める割合で明るさを決める
//	アパーチャグリル: R・G・B の縦じま (1 組の 1/3 ずつ)
//	シャドウマスク: 高さ 0.5 組の行ごとに半組ずらし、行の間を 15% 暗くする (点を三角に並べたものの近似)
//	スロットマスク: 縦じまを 1 組の高さごとに区切り (区切り 15%)、隣の組とは半周期ずらす
float3 MaskPhys(float2 pos)
{
	float p = Glow.w;
	float x0 = (pos.x - 0.5) / p, x1 = x0 + 1 / p;
	float y0 = (pos.y - 0.5) / p, y1 = y0 + 1 / p;
	float rowh = 1, gap = 0;
	if (Mask.x == 2)
	{
		rowh = 0.5;
		gap = 0.15;
		float sh = 0.5 * fmod(floor(pos.y / p / rowh), 2);	// 行ごとに半組ずらす
		x0 += sh, x1 += sh;
	}
	else if (Mask.x == 3)
	{
		gap = 0.15;
		float sh = 0.5 * fmod(floor(pos.x / p), 2);			// 組ごとに半周期ずらす
		y0 += sh, y1 += sh;
	}
	float3 cov = float3(Cover(x0, x1, 0, 1.0 / 3), Cover(x0, x1, 1.0 / 3, 1.0 / 3), Cover(x0, x1, 2.0 / 3, 1.0 / 3));
	float3 m = Mask.y + (Mask.z - Mask.y) * cov;			// しまに全部入れば明るい色、入らなければ暗い色
	if (gap > 0)
	{
		float g = Cover(y0 / rowh, y1 / rowh, 0, gap);		// 区切りが占める割合
		m *= 1 - g * (1 - Mask.y);
	}
	return m;
}

// ピクセル単位のマスク
float3 MaskPixel(float2 pos)
{
	uint x = (uint) pos.x, y = (uint) pos.y;
	if (Mask.x == 1 || Mask.x == 4)
	{
		// アパーチャグリル: R・G・B の縦じま (3 ピクセルで 1 組)
		return Phosphor(x % 3);
	}
	else if (Mask.x == 2)
	{
		// シャドウマスク: 1 色 2 ピクセル、2 行ごとに 3 ピクセルずらす
		uint xx = x + 3 * ((y >> 1) & 1);
		return Phosphor((xx % 6) >> 1);
	}
	else
	{
		// スロットマスク: R・G・B の縦じまを 4 行ごとに切り、隣の組とは 2 行ずらす
		float3 m = Phosphor(x % 3);
		if (((y + ((x / 3) & 1) * 2) & 3) == 0)
			m *= Mask.y;
		return m;
	}
}

float3 MaskAt(float2 pos)
{
	return Glow.w > 0 ? MaskPhys(pos) : MaskPixel(pos);
}

// 管の画面の形: 画面の上の位置 = (1 - f) × 角度 + f × tan(角度)。f = 1 は平らな画面 (外周ほど大きく動く)、
// 小さいほど偏向の中心を中心とした球面に近い (全体がそろって動く)。曲率半径 0.7m で約 0.6、1.0m で約 0.7 (14 インチ、
// 偏向の中心から画面まで 0.15m で計算)。アパーチャグリル (トリニトロン) は円筒の画面で、横は曲面・縦は平ら。
// ほかの管は球面
float2 Flatness()
{
	return Mask.x == 1 ? float2(0.7, 1.0) : float2(0.6, 0.6);
}

float2 Bend(float2 th, float2 f)
{
	return (1 - f) * th + f * tan(th);
}

// 伸び縮みした絵の位置 p (-1～1) に来る、元の絵の位置を求める
float2 Unbend(float2 p, float g)
{
	float2 f = Flatness();
	float2 thmax = atan(Breath.yz);
	float2 pmax = Bend(thmax, f);
	// 画面の位置から偏向の角度を求める (ニュートン法)
	float2 target = p * pmax;
	float2 th = atan(target);
	[unroll] for (int i = 0; i < 4; i++)
	{
		float2 c = cos(th);
		th -= (Bend(th, f) - target) / ((1 - f) + f / (c * c));
	}
	// 倍率を戻した角度の位置
	return Bend(th / g, f) / pmax;
}

// FD (平らな画面のアパーチャグリル): 画面の位置 = tan(角度) を横と縦で別々に。平らな画面はそのままでは糸巻き型になるのを
// 回路で引き伸ばしてまっすぐにしているので、高圧が下がると引き伸ばしが追いつかず、辺の中ほどが角より余分に縮む
// (辺が内側に凹む。縮んだ分 (1 - g) に比例して、もう一方の軸の中ほどで最大、角で 0)
float2 UnbendFD(float2 p, float g)
{
	float2 t = Breath.yz;
	float2 q = tan(atan(p * t) / g) / t;
	q *= 1 + (1 - g) * (1 - p.yx * p.yx);
	return q;
}

float Bend1(float th, float f)
{
	return (1 - f) * th + f * tan(th);
}

// 球面の管: 偏向は画面の中心からの向きに放射状に効くので、角は辺の中ほどより大きく動き、縮むと太鼓型になる。
// 画面の端の tan (Breath.yz) で縦横の比を合わせ、中心からの距離で角度を逆算する。太鼓型が分かるように、
// 縮んだ分 (1 - g) に比例して、中心からの距離の 2 乗で外側ほど余分に縮める (角で 1 - g)
float2 UnbendRadial(float2 p, float g)
{
	float f = 0.6;
	float2 tp = p * Breath.yz;					// 平らな画面の tan の単位
	float rc = length(Breath.yz);				// 角までの距離
	float r = length(tp);
	if (r < 1e-5)
		return p / g;
	float pc = Bend1(atan(rc), f);
	float target = r / rc * pc;
	float th = atan(target);
	[unroll] for (int i = 0; i < 4; i++)
	{
		float c = cos(th);
		th -= (Bend1(th, f) - target) / ((1 - f) + f / (c * c));
	}
	float r2 = Bend1(th / g, f) / pc * rc;
	float rn = r / rc;
	r2 *= 1 + (1 - g) * rn * rn;
	return tp * (r2 / r) / Breath.yz;
}

// アパーチャグリルの横のワイヤーの影 (高さの 1/3 と 2/3、ガラスに付いているので伸び縮みしない)。
// 太さ Breath.w の帯が出力のピクセルに占める割合で暗くする (帯の中は 75% 暗い) (細いときは薄くなる)
float Wires(float y)
{
	float w = Breath.w;
	float y0 = y - 0.5, y1 = y + 0.5;
	float k = 1;
	[unroll] for (int i = 1; i <= 2; i++)
	{
		float yc = Dst.w * i / 3;
		float cov = max(0, min(y1, yc + w / 2) - max(y0, yc - w / 2));
		k *= 1 - min(cov, 1) * 0.75;
	}
	return k;
}

// 画面の回転: 出力の画素の位置から、回す前の絵の上の位置を求める (Dst は回す前の絵の大きさ。出力の上では、
// 90°・270° なら縦横が入れ替わる)。マスク・走査線などもいっしょに回る (ブラウン管ごと回したのと同じ)
float2 Unrotate(float2 pos)
{
	float2 o = pos - Dst.xy;
	int r = (int) View.x;
	float2 l = r == 1 ? float2(o.y, Dst.w - o.x)
			 : r == 2 ? float2(Dst.z - o.x, Dst.w - o.y)
			 : r == 3 ? float2(Dst.z - o.y, o.x)
			 :          o;
	return Dst.xy + l;
}

// ---------------------------------------------------------------------------
// 橙色の白黒のプラズマディスプレイ風: 640x400 のセルを、色の明るさを 4x4 のディザで階調にして橙色で光らせる。
// セルの間に隔壁、消えている所はガラスの色、明るい所の周りにハレーション (ぼけの絵の明るさ)。
// 前面の平らなガラス: 四隅に向けてごく薄い周辺減光と、上からの環境光の赤茶色の映り込み。
// 計算は sRGB のまま (最後に ToSrgb しない)

static const float Bayer[16] =
{
	 0.0 / 16,  8.0 / 16,  2.0 / 16, 10.0 / 16,
	12.0 / 16,  4.0 / 16, 14.0 / 16,  6.0 / 16,
	 3.0 / 16, 11.0 / 16,  1.0 / 16,  9.0 / 16,
	15.0 / 16,  7.0 / 16, 13.0 / 16,  5.0 / 16,
};

// 明るさ。青が見えなくならないよう、ふつうの係数より B を少し持ち上げる
float PlasmaLuma(float3 rgb)
{
	return dot(rgb, float3(0.28, 0.54, 0.18));
}

float4 Plasma(float2 pos, float2 uv)
{
	// セル (640x400)。200 ラインなら偶数の行を 2 行に使う
	int2 ip = clamp(int2(floor(uv * SrcSize)), int2(0, 0), SrcSize - 1);
	int row = Scan.x == 200 ? (ip.y & ~1) : ip.y;
	float luma = PlasmaLuma(Fetch(ip.x, row));

	// 4x4 のディザで階調にする
	float dither = Bayer[(ip.y & 3) * 4 + (ip.x & 3)] - 0.5;
	float levels = View.w;
	float q = floor(saturate(luma + dither / levels) * levels + 0.5) / levels;
	q = Mono.x + (1 - Mono.x) * q;			// 黒も弱く光らせる: 階調をその分ずらす (Mono.x: 黒の所の明るさ。プラズマのときだけこの意味)

	// 発光の色: 弱いと赤橙、ふつうはネオンの橙、最大でわずかに黄色
	float3 darkOrange = float3(0.45, 0.05, 0.00);
	float3 midOrange  = float3(1.00, 0.30, 0.01);
	float3 brightCore = float3(1.00, 0.68, 0.22);
	float3 col = lerp(darkOrange, midOrange, smoothstep(0.0, 0.5, q));
	col = lerp(col, brightCore, smoothstep(0.5, 1.0, q));
	col *= q;

	// セルの間の隔壁: 出力のピクセルに隔壁 (セルの境の両側 0.1 ずつ) が占める割合で暗くする
	float2 cell = Dst.zw / float2(SrcSize);			// 1 セルの大きさ (出力のピクセル)
	float2 a = (pos - Dst.xy - 0.5) / cell + 0.1;		// 隔壁を [0, 0.2) に置く
	float2 b = a + 1 / cell;
	float gx = Cover(a.x, b.x, 0, 0.2), gy = Cover(a.y, b.y, 0, 0.2);
	float mask = lerp(1 - View.z, 1, (1 - gx) * (1 - gy));

	// 消えている所のガラスの色 + 発光 + ハレーション
	float3 glass = float3(0.075, 0.022, 0.012);		// 消えている所: 前面の橙のフィルターで暗い赤茶に見える
	glass += float3(0.022, 0.010, 0.006) * (1 - uv.y) * (1 - uv.y);	// 映り込み (上ほど明るい)
	float3 c = glass + col * mask;
	if (Glow.y > 0)
	{
		float gl = PlasmaLuma(ToSrgb(saturate(GlowTex.Sample(Lin, uv).rgb)));
		c += midOrange * pow(gl, 1.5) * Glow.y;
	}
	float2 v = uv * 2 - 1;
	float r2 = dot(v, v) / 2;					// 四隅で 1
	c *= 1 - 0.12 * r2 * r2;					// 周辺減光 (四隅で 12%)
	return float4(saturate(c), 1);
}

// 白黒の管の明るい所: 明るさ (最も明るい成分) が 0.8 を超えたら、なだらかに 1 に近づけ (上限で同じ色に張り付かないように)、
// 1 を超えた分の割合だけ白に寄せる (強く光る蛍光体は白っぽく見える)
float3 MonoTone(float3 c)
{
	float m = max(c.r, max(c.g, c.b));
	const float knee = 0.8;
	if (m <= knee)
		return c;
	float nm = knee + (1 - knee) * (1 - exp(-(m - knee) / (1 - knee)));
	float3 s = c * (nm / m);
	float t = saturate((m - 1) / m) * 0.6;
	return lerp(s, nm, t);
}

// 周辺減光: ガラスの上の位置 g (0～1) の明るさの割合。真ん中は平らで端ほど急に落ちる (四隅で 1 - Edge.x)
float Vignette(float2 g)
{
	float2 v = g * 2 - 1;
	float d = (1 - Edge.y) * v.x * v.x + Edge.y * v.y * v.y;	// 四隅で 1
	return 1 - Edge.x * d * d;
}

float4 main(float4 pos : SV_Position) : SV_Target
{
	pos.xy = Unrotate(pos.xy);
	float2 uv = (pos.xy - Dst.xy) / Dst.zw;
	float2 glassuv = uv;					// ガラスの上の位置 (伸び縮みしない)
	if (Breath.x != 1)
	{
		// 伸び縮み: 角度が Breath.x 倍になった絵の、この位置に来る元の位置を求める (中央を原点に -1～1、横と縦は別々に)。
		// 外に出た所は黒
		float2 p = Mask.x == 1 ? Unbend(uv * 2 - 1, Breath.x) : Mask.x == 4 ? UnbendFD(uv * 2 - 1, Breath.x)
				 : UnbendRadial(uv * 2 - 1, Breath.x);
		if (abs(p.x) > 1 || abs(p.y) > 1)
			return float4(0, 0, 0, 1);
		uv = (p + 1) / 2;
	}

	if (View.y > 0)
		return Plasma(pos.xy, uv);

	if (Mask.w == 0)
	{
		// フィルター無し: 最近傍
		int2 ip = clamp(int2(floor(uv * SrcSize)), int2(0, 0), SrcSize - 1);
		return float4(Fetch(ip.x, ip.y), 1);
	}

	float sx = uv.x * SrcSize.x;
	// 周辺のぼけ: ビームの点の大きさ (中心で 1、四隅で 1 + Edge.z)。ガウスの指数は 2 乗で割る
	float2 bv = uv * 2 - 1;
	float spot = 1 + Edge.z * ((1 - Edge.w) * bv.x * bv.x + Edge.w * bv.y * bv.y);	// 四隅で 1 + Edge.z
	float sp2 = 1 / (spot * spot);
	float3 c;
	if (Scan.y == 0 && Edge.z == 0)
	{
		// 走査線なし: 縦は今までどおり (640x400 の行をそのまま)
		c = Horz(sx, (int) floor(uv.y * SrcSize.y), Scan.z);
	}
	else if (Scan.y == 0)
	{
		// 走査線なしで周辺のぼけ: 縦も 640x400 の行をガウスでなじませる (真ん中は行がほぼそのまま。重みの和で割る)
		float y = uv.y * SrcSize.y - 0.5;
		int l = (int) floor(y);
		float f = y - l;
		c = 0;
		float w = 0;
		[unroll] for (int k = -2; k <= 3; k++)
		{
			float wk = Gaus(k - f, -40 * sp2);
			c += Horz(sx, clamp(l + k, 0, SrcSize.y - 1), Scan.z * sp2) * wk;
			w += wk;
		}
		c /= w;
	}
	else
	{
		// 走査線: 近くの 6 本をガウスで重ねる (明るい線も太さは同じ。明るさのにじみはぼけで出す)。
		// 周辺のぼけで線が太くなった分は、重なりで明るくならないよう点の大きさで割る
		int step = Scan.x == 200 ? 2 : 1;		// 200 ラインなら偶数の行だけを読む
		float y = uv.y * Scan.x - 0.5;
		int l = (int) floor(y);
		float f = y - l;
		c = 0;
		[unroll] for (int k = -2; k <= 3; k++)
		{
			int ln = clamp(l + k, 0, (int) Scan.x - 1);
			c += Horz(sx, ln * step, Scan.z * sp2) * Gaus(k - f, Scan.y * sp2);
		}
		c /= spot;
	}

	if (Mask.x != 0)
		c *= MaskAt(pos.xy - Dst.xy);
	if (Breath.w > 0)
		c *= Wires(pos.y - Dst.y);
	c *= Scan.w;
	if (Glow.y > 0)
		c += GlowTex.Sample(Lin, uv).rgb * Glow.y;		// 明るい所の周りの丸いぼけ
	if (Mono.w > 0 && Mono.w < 1.5)
		c = MonoTone(c);
	c = saturate(c);
	if (Edge.x > 0)
		c *= Vignette(glassuv);		// 光った後にガラスで暗くなるので、明るさの上限で切った後に掛ける
	return float4(ToSrgb(c), 1);
}
