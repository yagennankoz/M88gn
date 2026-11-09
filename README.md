
# M88gn - PC8801 Series Emulator

## 初めに

M88gn は、cisc 氏作の PC-8801 エミュレータ [M88](http://retropc.net/cisc/m88/) をもとにした私家版です。もとにしたのは次の 2 つです。

* rururutan (RuRuRu) 氏の M88 私家版 (https://github.com/rururutan/m88)
* その私家版に、GIMONS 氏が FDX 形式のフロッピーイメージへの対応などを加えた M88g ([GIMONS DEVELOPER WORKS](http://retropc.net/gimons/emulators/) の `m88g_20250513.zip`)

M88gn は、rururutan 氏の私家版に M88g の差分を当て、その上でテープ (T88) まわりなどを改修したものです。

* rururutan 氏の私家版での変更は [README_RuRuRu.md](README_RuRuRu.md) にあります。
* M88g での変更は [README_M88g.md](README_M88g.md) にあります。
* cisc 氏のオリジナルの説明と履歴は [readme.txt](readme.txt) と [HISTORY.TXT](HISTORY.TXT) にあります。

M88gn について、cisc 氏・rururutan 氏・GIMONS 氏に問い合わせないでください。

## M88gn での主な改修

### テープの読み込み

* T88 の長いマシン語のデータ (N88-BASIC のモニタで C000〜CFFF を読むときなど) が、途中から化けてエラーになる不具合を直しました。データを実機と同じ間隔で SIO に渡します。
* テープの速さ (600 / 1200ボー) と PC-8801 側の設定が違うときは、データを渡しません。M88 の元の動きでは、`CAS:` で保存したものが `CAS2:` で読めていました (逆も)。
* T88 の DATA タグの型 (実データ種別) を [仕様](https://quagma.sakura.ne.jp/manuke/t88format.html) どおりに扱います。8251 のモード (データ長・パリティ・ストップビット) が書かれたテープを、その形式で読みます。CPU の設定と形式が違えば、SIO にパリティエラー・フレーミングエラーが立ちます。
* PicoCmt (https://github.com/yagennankoz/PicoCMT) で録音した `.RAW` (波形のまま記録した形式) を読めます。詳しくは下の「PicoCmt の RAW」を見てください。

### テープの音

* 再生中のテープの音 (MARK・SPACE・データの FSK) を鳴らします。
* モーターのリレーの音 (カチッ) を鳴らします。PC-8001mkII の実機から録音した音です。
* Tape メニューの **Tape sound volume** で音量を変えられます (Off / 25 / 50 / 75 / 100%)。

### テープの速さ

* Tape メニューの **Tape speed** で、ロードとセーブを速くできます (Normal / x2 / x4 / x8)。
  * データは CPU が読んだのに合わせて送ります。
  * データの間の MARK・SPACE などは、CPU がキャリアを待っている間だけ縮めます。多段ロードのゲームでも、次のファイルの先頭を逃さないようにしています。
* 速くしても、音の高さは変わりません。

### テープへのセーブ

* M88 の元の動きでは、テープへのセーブは記録されていませんでした。M88gn は、データレコーダーと同じように T88 に記録します。
  1. Tape → **Open...** でテープを開きます。無い名前を入れると、空の T88 を作ります。ロードとセーブは同じファイルです。
  2. Tape → **Record** をオンにします。既にデータがあるテープなら、追記してよいか確認します。
  3. PC-8801 でセーブすると、モーターが回っている間の記録をテープの最後に追記します。モーターが止まると Record はオフに戻ります。
* ファイルを開くときに「読み取り専用」を選んだテープと、読み取り専用のファイルには記録できません。
* Record がオフのままセーブすると、画面に「このセーブは記録されません」と出ます。
* Tape → **Rewind** でテープを先頭に戻します。

### BEEP の音

* BEEP で音楽を鳴らすソフトの音が、実機より濁っていたのを直しました。M88 の元の合成では、短い間隔の BEEP の切り替えがつぶれていました (パルスの幅で音量や和音を作るものなど)。今は切り替えた時刻どおりに音を作ります。

### 全画面

* Control メニューに **Full screen** (Alt+Enter) を追加しました。
* 画面の解像度を変えずに、M88 のあるモニターいっぱいに縦横比を保って表示します。M88 の元の全画面では解像度を切り替えていたので、ウィンドウに戻ったときに、ほかのアプリのウィンドウの大きさや位置が変わることがありました。
* メニューバーはいつも表示します。設定の「全画面時 640x480 にする」は効きません。

## PicoCmt の RAW

Tape → Open... で `.raw` のファイルを選ぶと、波形のまま再生します。形式は PicoCmt のリポジトリの RAW_FORMAT.md にあります。

* M88gn の中で復調します。半周期が 312us より短ければ 2400Hz ("1")、長ければ 1200Hz ("0") とします。
* 復調した信号を CPU が設定した形式でビットごとに読んで SIO に渡します。2400Hz が続いている間は、キャリア (ポート 40h の bit2) を立てます。
* 録音したデッキの速さのずれは、2400Hz が続く所で測ります。テープの時間を本来の速さに直してから読みます。周波数の違う部分 (高速変調など) や雑音の所では測らず、前の値のまま進めます。
* RAW は Tape speed が効かず、いつも実機の速さで読みます。読み取り専用で、Record はできません。
* CPU がキャリアを読んで復調する高速ロードのテープでも読めるように作っていますが、実物のテープでは確かめていません。

## 設定 (M88.ini)

M88gn で増えた設定です。メニューで変えると保存されます。

| キー | 内容 | 既定 |
|---|---|---|
| `TapeVolume` | テープの音の音量 (0〜100) | 100 |
| `TapeSpeed` | テープの速さ (1 / 2 / 4 / 8) | 1 |

## 動作環境

* Windows 11 で動作を確かめています。
* PC-8801 の ROM が要ります (下のライセンスの条件を守ってください)。

## ビルド

* Visual Studio 2022 で `M88_2022.sln` を開き、Release|Win32 または Debug|Win32 でビルドします。出力は `build\<構成>\M88.exe` です。
  * MFC の無い Visual Studio でもビルドできるように、`vs2022/afxres.h` を置いています。
  * コマンドでビルドする場合:
    ```
    MSBuild.exe M88_2022.sln -p:Configuration=Release -p:Platform=Win32
    ```
* rururutan 氏の私家版の VS2008 のプロジェクト (`M88_2008.sln`) も残しています。ソースは VS2008 で通る書き方にしていますが、M88gn ではビルドを確かめていません。

## 謝辞

* M88 の作者 cisc 氏、私家版の rururutan 氏、M88g の GIMONS 氏に感謝します。

## ライセンス

* rururutan 氏のライセンス条項に従います。M88g の FDX 対応のソースと同じく、M88gn で追加・変更したコードも追加コードの扱いです。

* FM 音源・PSG は、cisc 氏の fmgen (`src/devices`、Copyright (C) by cisc 1998, 2003) を使っています。fmgen の利用の条件は [src/devices/readme.txt](src/devices/readme.txt) にあります。

c86ctl.hは2条項BSDライセンスです。

新規ファイル/追加コードは2条項BSDライセンスになります。

### M88gn で追加・変更したコード

M88gn で追加・変更したコードは、次の2条項BSDライセンスです。

```
Copyright (c) 2026 YagenNankoz
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

既存のファイルに加えた変更は、そのファイルの元のライセンス (下の M88 のライセンス) にも従います。

既存のファイルは以下のオリジナルライセンスに従います。

* M88 はcisc氏が著作権を所有しています。
* M88 とそのソースコードは一切無保証です。
* M88 そのもの、または M88 の使用や、M88 を使用できなかったことなど、M88 に関して生じた損害はすべて使用者が自ら負うものとします。作者は一切責任を負いません。また、作者は M88 に関してバグ、不具合等があったとしてもそれに対処する義務を負いません。
* M88 の転載、及び配布は禁止します。但し，M88 のソースコードに改変を加えたもの，及び M88 のソースコードを利用したソフトに関しては，その限りではありません。
* M88.exe の使用者は NEC PC-8801 シリーズの本体を所有しなければなりません。また、使用する ROM データはその本体から直接取り出した ROM データでなければなりません。使用者の所有物でない本体から取り出した ROM データは使用できません。

* M88 のソースコードの一部，または全部を組み込んだソフトは，フリーソフトとして公開することが出来ます。
* 但し，src\pc88 のディレクトリの下にあるファイルを組み込む場合，または商用ソフト(シェアウェア含む)へのプラグインソフトとして配布する場合は，同時にそのソフトのソースコードもフリーソフトとして公開ください。
* 公開の際には，ドキュメント等に M88 のソースコードの一部または全部を組み込んだ事と，著作権表示を明示してください．また，作者への連絡を頂ければ幸いです。
* M88 のソースコードを利用したソフトのソースを配布する際には，M88 のソースコードのうち，そのソフトのコンパイルに必要なものに限り，添付することを認めます。
* 商用ソフト(シェアウェア含む) に M88 のソースコードの一部，または全部を組み込む際には，事前に M88 の作者の合意を得る必要があります。
* M88 に改変を加えたソフトを配布する場合は，M88 の著作権表示，および改変内容を明示してください。
