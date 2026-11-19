
# M88gn - PC8801 Series Emulator

## 初めに

M88gnは、cisc氏作のPC-8801エミュレータ[M88](http://retropc.net/cisc/m88/)をもとにした私家版です。もとにしたのは次の2つです。

* rururutan (RuRuRu)氏のM88私家版（<https://github.com/rururutan/m88>）
* その私家版に、GIMONS氏がFDX形式のフロッピーイメージへの対応などを加えたM88g（[GIMONS DEVELOPER WORKS](http://retropc.net/gimons/emulators/)の`m88g_20250513.zip`）

M88gnは、rururutan氏の私家版にM88gの差分を当て、その上でテープ（T88）まわりなどを改修したものです。

* rururutan氏の私家版での変更は[README_RuRuRu.md](README_RuRuRu.md)にあります。
* M88gでの変更は[README_M88g.md](README_M88g.md)にあります。
* cisc氏のオリジナルの説明と履歴は[readme.txt](readme.txt)と[HISTORY.TXT](HISTORY.TXT)にあります。

M88gnについて、cisc氏・rururutan氏・GIMONS氏に問い合わせないでください。

## M88gnでの主な改修

### テープの読み込み

* T88の長いマシン語のデータ（N88-BASICのモニタでC000〜CFFFを読むときなど）が、途中から化けてエラーになる不具合を直しました。データを実機と同じ間隔でSIOに渡します。
* テープの速さ（600/1200ボー）とPC-8801側の設定が違うときは、データを渡しません。M88の元の動きでは、`CAS:`で保存したものが`CAS2:`で読めていました（逆も）。
* T88のDATAタグの型（実データ種別）を[仕様](https://quagma.sakura.ne.jp/manuke/t88format.html)どおりに扱います。8251のモード（データ長・パリティ・ストップビット）が書かれたテープを、その形式で読みます。CPUの設定と形式が違えば、SIOにパリティエラー・フレーミングエラーが立ちます。
* PicoCmt（<https://github.com/yagennankoz/PicoCMT>）で録音した`.RAW`（波形のまま記録した形式）を読めます。詳しくは下の「PicoCmtのRAW」を見てください。
* `.cmt`（テープのバイトを並べただけの形式）を読めます。詳しくは下の「CMT」を見てください。
* ファイルの終わりの印の後に余分なバイトがあるテープで、ROMがモーターを止めた後にそのバイトを送り、次のファイルの読み込みに失敗することがあったのを直しました（多段ロードのゲームで、Tape speedを上げたときなど）。

### テープの音

* 再生中のテープの音（MARK・SPACE・データのFSK）を鳴らします。
* モーターのリレーの音（カチッ）を鳴らします。PC-8001mkIIの実機から録音した音です。
* Tapeメニューの**Tape sound volume**で音量を変えられます（Off/25/50/75/100%）。

### テープの速さ

* Tapeメニューの**Tape speed**で、ロードとセーブを速くできます（Normal/x2/x4/x8）。
  * データはCPUが読んだのに合わせて送ります。
  * データの間のMARK・SPACEなどは、CPUがキャリアを待っている間だけ縮めます。多段ロードのゲームでも、次のファイルの先頭を逃さないようにしています。
* 速くしても、音の高さは変わりません。

### テープへのセーブ

* M88の元の動きでは、テープへのセーブは記録されていませんでした。M88gnは、データレコーダーと同じようにT88に記録します。
  1. Tape → **Open...**でテープを開きます。無い名前を入れると、空のT88を作ります。ロードとセーブは同じファイルです。
  2. Tape → **Record**をオンにします。既にデータがあるテープなら、追記してよいか確認します。
  3. PC-8801でセーブすると、モーターが回っている間の記録をテープの最後に追記します。モーターが止まるとRecordはオフに戻ります。
* ファイルを開くときに「読み取り専用」を選んだテープと、読み取り専用のファイルには記録できません。
* Recordがオフのままセーブすると、画面に「このセーブは記録されません」と出ます。
* Tape → **Rewind**でテープを先頭に戻します。

### テープの頭出し

* Tapeメニューの**Previous gap**/**Next gap**で、前/次のファイルの区切りに移ります。何本も入ったテープで、読みたいファイルの前に移るのに使います。
* 区切りは、BLANK（無音）の始まりと、MARKにはさまれたSPACEの始まりです。データの前のリーダーは区切りにしません。
* Previous gapは、区切りの後のデータが始まっていればその区切りへ、区切りのすぐ後（無音やリーダーの途中）なら、もう1つ前の区切りへ移ります。前に区切りが無ければ先頭に戻ります。
* 移った位置（分:秒）をステータスバーに出します。メニューから選ぶと、続けて選べるようにTapeメニューが開き直します。
* Recordがオンの間と、RAWのテープでは使えません。CMTのテープでは、M88gnがファイルの終わりの後に入れた間が区切りになります。

### BEEPの音

* BEEPで音楽を鳴らすソフトの音が、実機より濁っていたのを直しました。M88の元の合成では、短い間隔のBEEPの切り替えがつぶれていました（パルスの幅で音量や和音を作るものなど）。今は切り替えた時刻どおりに音を作ります。

### 画面

* 画面の項目を**Screen**メニュー（TapeとToolsの間）にまとめました。
* 画面はDirect3D 11で描きます。使えなければDirect2D、それもだめならGDIで描きます（CRT風のフィルターはDirect3D 11のときだけ）。
* **Window scale**で、ウィンドウの表示の倍率を選べます（x1/x2/x3/x4）。
* **Rotation**で、画面を右回りに90度ずつ回せます（0°/90°/180°/270°）。90°と270°ではウィンドウが縦長になります。CRT風のフィルター（走査線・マスクなど）も、ブラウン管ごと回したように一緒に回ります。Direct3D 11で描くときだけ使えます。画面の取り込み（Capture）は回しません。
* 表示スケールが100%以外のWindowsでも、Windowsに拡大されてぼけないように、M88の本体の窓とソフトウェアキーボードをDPIに対応させました（設定・モニターなどの窓は今までどおりです）。

### 全画面

* Screenメニューの**Full screen** (Alt+Enter)で全画面にします。
* 画面の解像度を変えずに、M88のあるモニターいっぱいに縦横比を保って表示します。M88の元の全画面では解像度を切り替えていたので、ウィンドウに戻ったときに、ほかのアプリのウィンドウの大きさや位置が変わることがありました。
* メニューバーはいつも表示します。ステータスバーの代わりに、メニューバーの右の端に知らせとランプを出します。
* マウスを3秒動かさないと、マウスカーソルを消します。

### CRT風のフィルター

ブラウン管のモニターで見たような絵にします。Screenメニューで選びます。

* **Direct (no filter)**：オンにすると、フィルターを掛けずに元の絵をそのまま出します。下の設定は残したまま灰色になります。
* **Scanlines**：走査線の強さ（Off/Weak/Medium/Strong）。走査線の本数は、画面のモードに合わせます（200ライン/400ライン）。
* **Mask**：蛍光体のマスク（None/Aperture grille/Shadow mask/Slot mask）。Aperture grilleのときは、トリニトロン管のように横のダンパーワイヤーの影を2本（画面の高さの1/3と2/3）描きます。
* **Mask pitch**：マスクの間隔。**Pixel**は画面のピクセル単位の模様です。**0.25mm/0.31mm/0.39mm**は、PC-8801の画面（横640ドット）を14インチのモニターの表示の幅（240mm）とみなしたときの、実在のブラウン管の蛍光体の間隔です。ウィンドウを大きくするほど、しまも大きくなります。
* **Sharpness**：横のにじみ（Sharp/Normal/Soft）。SoftはGlowのぼけも広くします。
* **Brightness**：走査線とマスクで暗くなる分の明るさの補正（Low/Normal/High）。
* **Glow**：明るい所の周りの、丸いぼけ（Off/Weak/Normal/Strong）。
* **Breathing**：画面全体の明るさで絵が伸び縮みします（Off/Weak/Normal/Strong）。外周ほど大きく動き、Aperture grilleは円筒の画面（辺はまっすぐ）、ほかのマスクは球面の画面（縮むと太鼓型）として歪みます。
  * **Expand when bright (mid = normal size)**：オンにすると、中間の明るさのときが基準の大きさになり、暗いと縮み、明るいと膨らみます。膨らむと画面の端の絵が少し切れます。
* 既定は、すべてのフィルターがオフ（今までと同じ絵）です。

### 音量

* 設定の音量のページに**Master**（全体の音量、-40〜+20dB）を追加しました。
* テープの音（再生音・リレー）とディスクの音には、Masterを掛けません。それぞれTape sound volume/Disk sound volumeだけで決まります。

### ディスクの音

* フロッピーディスクのドライブがシークする音を鳴らします。
* ディスクを入れる音・取り出す音を鳴らします（メニューやドラッグアンドドロップで入れ替えたとき。起動時とスナップショットの読み込みでは鳴らしません）。
* シークしない読み書きの音は鳴らしません。
* Diskメニューの**Disk sound volume**で音量を変えられます（Off/25/50/75/100%）。

### ソフトウェアキーボード

* Toolsメニューの**Software keyboard**で、PC-8801のキーボードの絵を出します。マウスでクリックしたキーがPC-8801に届きます。
  * M88のウィンドウの外にも置けます。大きさは縦横の比を保って変えられます。位置・大きさ・表示しているかは保存し、次に起動したときに戻します。
  * クリックしてもM88からフォーカスを取りません。パソコンのキーボードと一緒に使えます。
* SHIFT・CTRL・GRPHは、1回押すと次のキーまで押したままにします（黄色）。もう1回押すと戻ります。素早く2回押すと固定します（橙色・太い枠）。かな・大文字は押すたびに固定/解除します。
* PC-8801に届いているキー（パソコンのキーボードで押したものも）を青く光らせます。

### キー入力

* パソコンのShiftを押したままテンキーを押すと、PC-8801にSHIFTが届かなかったのを直しました（NumLockがオンのとき）。
* 設定の「その他」に「ショートカットにAltでなくアプリケーションキーを使う」を追加しました。M88の元の動きでは、Alt (= GRPH) + Enterで全画面になるので、PC-8801にGRPH + RETURNを入れられませんでした。オンにすると、AltはいつもGRPHとしてPC-8801に渡し、全画面などの操作はアプリケーションキー+Enter/F1/F2/F10/F12で行います（そのキーはPC-8801に渡しません）。

### 設定・ファイルダイアログ

* 設定ダイアログの、M88gnでは効かない・意味が合わなくなった項目（全画面時640x480にする、全画面モードでVSyncに同期、ウィンドウモードでサイズを2倍にする、ファイルダイアログの位置バーを表示する）を、値を残したまま灰色にしました。倍率はScreenメニューのWindow scaleで選びます。
* ファイルダイアログが古い形で開き、ツールバーのアイコンが小さかったのを直しました。今の形のダイアログでは、読み取り専用で開くのは「開く」ボタンの▼から選びます。

### CPU (Z80)

* x86版のZ80エミュレーション（`src/devices/Z80_x86.cpp`）の、OUTI/OUTD/OTIR/OTDRの不具合を直しました。
  * M88の元の動きでは、これらの命令の後に割り込みが待っていると、次の命令を先に実行してからHLを増減していたので、次の命令がHLに入れた値が1ずれました。OTIR/OTDRは、くり返しの途中でも次の命令を先に実行していました。
  * 今はHLを増減してから割り込みを確かめます。OTIR/OTDRは、くり返している間は確かめません。
  * これにより、XeGrader体験版（PC-8801mkIISR用）がプレイ中に止まっていたのが直りました。

## PicoCmtのRAW

Tape → Open...で`.raw`のファイルを選ぶと、波形のまま再生します。形式はPicoCmtのリポジトリのRAW_FORMAT.mdにあります。

* M88gnの中で復調します。半周期が312usより短ければ2400Hz ("1")、長ければ1200Hz ("0")とします。
* 復調した信号をCPUが設定した形式でビットごとに読んでSIOに渡します。2400Hzが続いている間は、キャリア（ポート40hのbit2）を立てます。
* 録音したデッキの速さのずれは、2400Hzが続く所で測ります。テープの時間を本来の速さに直してから読みます。周波数の違う部分（高速変調など）や雑音の所では測らず、前の値のまま進めます。
* RAWはTape speedが効かず、いつも実機の速さで読みます。読み取り専用で、Recordはできません。
* CPUがキャリアを読んで復調する高速ロードのテープでも読めるように作っていますが、実物のテープでは確かめていません。

## CMT

Tape → Open...で`.cmt`のファイルを選ぶと読めます。`.cmt`はテープから読んだバイトを並べただけの形式で、ファイルの区切りや速さの情報がありません。M88gnは次のようにして再生します。

* 先頭にSPACEとMARK（リーダー）を置き、データの前に0x00を8バイト入れます（PicoCmtの`.cmt`の再生と同じです）。
* データの中のヘッダー（BASICのD3×10＋名前、N88-BASICのモニタの$$$＋名前）の後に、短いMARKを入れます。
* マシン語（モニタの形式）とBASICのプログラムの終わりを見つけて、その後にファイルの間の信号（MARK・SPACE・無音・SPACE・MARK）を入れます。多段ロードのゲームで、次のファイルを待つローダーのためです。
* 中身から区切りが分からない所（独自の形式のローダー）では、データの途中でモーターが回り始めたときに、SPACEとMARKを挟んでから続きを送ります。
* 速さ（600/1200ボー）は、1バイトごとにPC-8801側の設定に合わせます。途中で速さを切り替えるテープも読めます。
* 読み取り専用で、Recordはできません。

## 設定（M88.ini）

M88gnで増えた設定です。メニューで変えると保存されます。

| キー | 内容 | 既定 |
| --- | --- | --- |
| `TapeVolume` | テープの音の音量（0〜100） | 100 |
| `TapeSpeed` | テープの速さ（1/2/4/8） | 1 |
| `DiskVolume` | ディスクの音の音量（0〜100） | 100 |
| `SoftKbdX`/`SoftKbdY`/`SoftKbdW` | ソフトウェアキーボードの位置と幅（幅が0なら既定の大きさで、M88のウィンドウの下に出す） | 0 |
| `SoftKbdShow` | ソフトウェアキーボードを表示しているか（0/1） | 0 |
| `SoftKbdDpi` | ソフトウェアキーボードの位置・大きさを保存したときのDPI（無ければ1.1.0までの値とみなして、起動時に直す） |  |
| `ScreenScale` | ウィンドウの表示の倍率（1〜4） | 無ければ、設定の「画面2倍」か画面のDPIから決める |
| `ScreenRotate` | 画面の回転（0: 0° 1: 90° 2: 180° 3: 270°、右回り） | 0 |
| `CrtDirect` | CRT風のフィルターを掛けない（0/1） | 0 |
| `CrtScanline` | 走査線（0: Off 1: Weak 2: Medium 3: Strong） | 0 |
| `CrtMask` | マスク（0: None 1: Aperture grille 2: Shadow mask 3: Slot mask） | 0 |
| `CrtMaskPitch` | マスクの間隔（0: Pixel 1: 0.25mm 2: 0.31mm 3: 0.39mm） | 0 |
| `CrtSharpness` | 横のにじみ（0: Sharp 1: Normal 2: Soft） | 1 |
| `CrtBrightness` | 明るさの補正（0: Low 1: Normal 2: High） | 1 |
| `CrtGlow` | 明るい所の周りのぼけ（0: Off 1: Weak 2: Normal 3: Strong） | 0 |
| `CrtBreathing` | 明るさで絵が伸び縮みする（0: Off 1: Weak 2: Normal 3: Strong） | 0 |
| `CrtBreathExpand` | 伸び縮みの基準（0: 真っ白で基準の大きさ 1: 中間の明るさで基準の大きさ） | 0 |
| `VolumeMaster` | 全体の音量（dB + 100。60で消音） | 100 |

## 動作環境

* Windows 11で動作を確かめています。
* PC-8801のROMが要ります（下のライセンスの条件を守ってください）。

## ビルド

* Visual Studio 2022で`M88_2022.sln`を開き、Release|Win32またはDebug|Win32でビルドします。出力は`build\<構成>\M88.exe`です。
  * MFCの無いVisual Studioでもビルドできるように、`vs2022/afxres.h`を置いています。
  * コマンドでビルドする場合：
    ```
    MSBuild.exe M88_2022.sln -p:Configuration=Release -p:Platform=Win32
    ```
* rururutan氏の私家版のVS2008のプロジェクト（`M88_2008.sln`）も残しています。ソースはVS2008で通る書き方にしていますが、M88gnではビルドを確かめていません。

## 謝辞

* M88の作者cisc氏、私家版のrururutan氏、M88gのGIMONS氏に感謝します。

## ライセンス

* rururutan氏のライセンス条項に従います。M88gのFDX対応のソースと同じく、M88gnで追加・変更したコードも追加コードの扱いです。

* FM音源・PSGは、cisc氏のfmgen（`src/devices`、Copyright (C) by cisc 1998, 2003）を使っています。fmgenの利用の条件は[src/devices/readme.txt](src/devices/readme.txt)にあります。

c86ctl.hは2条項BSDライセンスです。

新規ファイル/追加コードは2条項BSDライセンスになります。

### M88gnで追加・変更したコード

M88gnで追加・変更したコードは、次の2条項BSDライセンスです。

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

既存のファイルに加えた変更は、そのファイルの元のライセンス（下のM88のライセンス）にも従います。

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
