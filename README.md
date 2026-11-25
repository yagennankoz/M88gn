
# M88gn - PC8801 Series Emulator

## 初めに

M88gnは、cisc氏作のPC-8801エミュレータ[M88](http://retropc.net/cisc/m88/)をもとにした私家版です。もとにしたのは次の2つです。

* rururutan (RuRuRu)氏のM88私家版（<https://github.com/rururutan/m88>）
* その私家版に、GIMONS氏がFDX形式のフロッピーイメージへの対応などを加えたM88g（[GIMONS DEVELOPER WORKS](http://retropc.net/gimons/emulators/)の`m88g_20250513.zip`）

M88gnは、rururutan氏の私家版にM88gの差分を当て、その上でテープ（T88）まわりなどを改修したものです。デバッガと、エミュレーションの不具合の修正の一部は、蕎麦P氏のM88am（<https://www.ambientworks.jp/m88am/>）のものを取り込みました。

* rururutan氏の私家版での変更は[README_RuRuRu.md](README_RuRuRu.md)にあります。
* M88gでの変更は[README_M88g.md](README_M88g.md)にあります。
* cisc氏のオリジナルの説明と履歴は[readme.txt](readme.txt)と[HISTORY.TXT](HISTORY.TXT)にあります。

M88gnについて、cisc氏・rururutan氏・GIMONS氏・蕎麦P氏に問い合わせないでください。

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
  1. Tape → **Open...**でテープを開きます。無い名前を入れると、空のT88を作ります。Tape → **New...**でも空のT88を作って開けます（既にある名前なら、置き換えるか聞きます）。ロードとセーブは同じファイルです。
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

### テープの窓

Tapeメニューの**Tape window**で、テープの様子を小さなウィンドウに出します。

* ファイル名、状態（Stopped/Playing/Recording、Tape speedの倍率）、今の位置と長さ（分:秒）、区切りに印の付いたバー。
* カセットテープの絵。テープが動いている間はハブが回り、テープの位置に合わせて左の巻きが減り、右の巻きが増えます（真ん中の窓から見えます）。テープの長さは、T88の長さが収まる規格の長さ（C-10・C-15・C-20・C-46・C-60・C-90・C-120のうち一番短いもの。ラベルに出します）とし、巻きの大きさとハブの回る速さは実物の寸法（ハブの半径約11mm、テープの厚さ）から求めます。等速なら、巻き取り側のハブは巻き始めに約41回転/分です。ラベルの書き込み欄にはファイル名を出し（1行に入らなければ、空白や_などの単語の区切りで2行目に折り返し、それでも入らなければ字を小さくします）、録音中はラベルの帯が赤くなります。
* ボタン：**Open**（Tapeメニューの**Open...**と同じ）、**Rewind**、**Prev**/**Next**（**Previous gap**/**Next gap**と同じ）、**Rec**（**Record**と同じ）。
* 右上の**Speed**（x1/x2/x4/x8）と**Volume**（Off/25%/50%/75%/100%）のプルダウン。Tapeメニューの**Tape speed**/**Tape sound volume**と同じです。
* 頭出しできる所（テープの先頭と区切り）の一覧。それぞれの時刻、中身の見当（BASICのファイル名、モニターで保存したマシン語の先頭の番地）、速さとバイト数を出します。今の位置の行は色が変わります。行をクリックすると、そこへ頭出しします。
* クリックしてもM88のキー入力は続けられます（ウィンドウが入力を取りません）。位置と大きさは保存します。
* デバッグの窓と同じく、本体の窓のペインに入れられます（タイトルバーのメニューの「〜のペインにドック」。[docs/debugger.md](docs/debugger.md)を参照）。
* Tapeメニューの先頭には、Diskメニューと同じく開いているテープのファイル名を出します（「Tape - 名前...」）。
* RAWのテープでは、一覧は先頭だけです。Tape speedが効かない（いつも等速）ので、Speedのプルダウンはx1のまま灰色になります。

### BEEPの音

* BEEPで音楽を鳴らすソフトの音が、実機より濁っていたのを直しました。M88の元の合成では、短い間隔のBEEPの切り替えがつぶれていました（パルスの幅で音量や和音を作るものなど）。今は切り替えた時刻どおりに音を作ります。

### 画面

* 画面の項目を**Screen**メニュー（TapeとToolsの間）にまとめました。
* 画面はDirect3D 11で描きます。使えなければDirect2D、それもだめならGDIで描きます（CRT風のフィルターはDirect3D 11のときだけ）。
* 本体の窓は、タイトルバーとメニューバーを1段にまとめました。左からアイコン・メニューの名前・題・ペインの開閉ボタン・最小化/最大化/閉じるのボタンが並びます。メニューは開いたまま、マウスや←→キーで隣のメニューへ移れます。全画面では今までどおりのメニューバーです。
* fps（画面の更新の回数）と動作周波数は、タイトルバーからステータスバーに移しました。
* 本体の窓の枠をドラッグして、窓の大きさをいつでも変えられます。PC-8801の画面は、縦横の比を保って窓（ペインを開いていればその残り）いっぱいに表示します。余った所は濃い灰色で塗ります。ペインを開いている間は画面の周りに枠を描き、キー入力がPC-8801に行っている間は青くなります。
* **Window scale**で、表示の倍率を選べます（x1/x2/x3/x4/Free）。x1〜x4を選ぶと、画面がその倍率になる大きさ（開いているペインの分も足します）に窓を合わせます。窓の大きさを変えたり、ペインを開閉したりして倍率どおりでなくなると**Free**になり、その窓の大きさを次の起動でも使います。
* **Rotation**で、画面を右回りに90度ずつ回せます（0°/90°/180°/270°）。90°と270°ではウィンドウが縦長になります。CRT風のフィルター（走査線・マスクなど）も、ブラウン管ごと回したように一緒に回ります。Direct3D 11で描くときだけ使えます。画面の取り込み（Capture）は回しません。
* 表示スケールが100%以外のWindowsでも、Windowsに拡大されてぼけないように、M88の本体の窓・ソフトウェアキーボード・設定・About・新しいディスクの作成の窓をDPIに対応させました（デバッグの窓は[docs/debugger.md](docs/debugger.md)を参照）。

### 全画面

* Screenメニューの**Full screen** (Alt+Enter)で全画面にします。
* 画面の解像度を変えずに、M88のあるモニターいっぱいに縦横比を保って表示します。M88の元の全画面では解像度を切り替えていたので、ウィンドウに戻ったときに、ほかのアプリのウィンドウの大きさや位置が変わることがありました。
* ステータスバーの代わりに、メニューバーの右の端に知らせとランプを出します。
* Screenメニューの**Hide menu bar**の**In full screen**をオンにすると、全画面ではメニューバーを隠して画面いっぱいに表示します。メニューは画面を右クリックして出します（下の「右クリックのメニュー」）。知らせとランプは、画面の右下に半透明で重ねて出します。知らせが無く、ランプも灯っていない間は薄くします。全画面でも、Toolsメニューの**Show Status**で、知らせとランプを出すかどうかを切り替えられます。
* マウスを3秒動かさないと、マウスカーソルを消します。

### 右クリックのメニュー

* 画面を右クリックすると、メニューバーと同じメニュー（Control・Disk・Tape・Screen・Tools・Debug・Help）を出します。全画面でもウィンドウでも使えます。
* Screenメニューの**Hide menu bar**の**In window**をオンにすると、ウィンドウでもタイトルの段にメニューの名前を出しません。
* メニューバーを隠しているときは、**Alt+M**（アプリケーションキーの設定ではApp+M）でもメニューを出せます（Screen→Hide menu bar→Show menu）。
* 設定でPC-8801のマウスを使うときは、右ボタンはPC-8801に渡すので出ません。このときはメニューバーを隠す設定がオンでも、メニューバーを出します。

### テーマ

* Toolsメニューの**Theme**で、窓まわりの色を選べます。
  * **Windows default**：Windowsの設定の色（タイトルの段は、Windowsの設定でタイトルバーにアクセントの色を付けていればその色）。
  * **Dark**：暗い色。テープの窓とデバッグの窓のスクロールバーも暗くします。
  * **Beige**：PC-8801の本体のようなアイボリー・ベージュに、こげ茶の字。
* 色が変わるのは、タイトルの段・ペインのタブと帯・仕切り・ステータスバー・テープの窓です。デバッグの窓の中とWindowsのメニューの色は変えません。

### 録画

* Toolsメニューの**Record Video**→**Record...**で保存先を選ぶと、画面と音をMP4（H.264・AAC）に録画します。もう一度選ぶと止めます。
* **With screen filter**は画面に出している絵（CRT風のフィルター込み、画面と同じ大きさ）、**Without filter (x2)**はフィルター無しの1280x800（回転していれば800x1280）で録ります。
* フレームレートは60/30/20/15fpsから選べます。
* 時間はエミュレーターの中の時間で数えます。Burst modeなどで速く動かしても、録った動画は実機の速さで再生されます。
* Direct3D 11で描いているときだけ使えます。WindowsのMedia Foundationを使います（無いWindowsでは使えません）。

### CRT風のフィルター

ブラウン管のモニターで見たような絵にします。Screenメニューで選びます。

* **Display**：表示の型を1つ選びます。下の各項目は、選んだ型で使えるものだけ選べます（使えないものは設定を残したまま灰色になります）。
  * **Direct (no filter)**：フィルターを掛けずに元の絵をそのまま出します。
  * **Analogue RGB CRT**：カラーのブラウン管です。下の項目がすべて使えます。
  * **Digital RGB CRT (8 colors)**：8色のデジタルRGBのブラウン管です。アナログのパレットの色は、R・G・Bそれぞれ0〜7の4以上で点きます（デジタルの端子につないだときのつもり）。下の項目がすべて使えます。
  * **Green/Amber/White monochrome CRT**：白黒（緑・琥珀・白）のブラウン管です。パレットの色を明るさにして蛍光体の色で描き、明るい所は白っぽくします。Mask・Mask pitchは使えません。
  * **Plasma (orange)**：橙色の白黒のプラズマディスプレイです。明るさをディザで階調にして描き、Glowはハレーションに使います。ブラウン管の項目（Scanlines〜Corner blur）は使えません。
* **Plasma levels**：プラズマの階調（2 (on/off)/4/8/16）。**Dim lit background**をオンにすると、消えている所も弱く光ります。
* **Scanlines**：走査線の強さ（Off/Weak/Medium/Strong）。走査線の本数は、画面のモードに合わせます（200ライン/400ライン）。
* **Mask**：蛍光体のマスク（None/Aperture grille/Aperture grille (FD, flat)/Shadow mask/Slot mask）。Aperture grilleのときは、トリニトロン管のように横のダンパーワイヤーの影を2本（画面の高さの1/3と2/3）描きます。Aperture grille (FD, flat)は、縦横とも平らな画面のアパーチャグリルです（模様とワイヤーはAperture grilleと同じで、BreathingとVignette・Corner blurの出方が違います）。
* **Mask pitch**：マスクの間隔。**Pixel**は画面のピクセル単位の模様です。**0.25mm/0.31mm/0.39mm**は、PC-8801の画面（横640ドット）を14インチのモニターの表示の幅（240mm）とみなしたときの、実在のブラウン管の蛍光体の間隔です。ウィンドウを大きくするほど、しまも大きくなります。
* **Sharpness**：横のにじみ（Sharp/Normal/Soft）。SoftはGlowのぼけも広くします。
* **Brightness**：走査線とマスクで暗くなる分の明るさの補正（Low/Normal/High）。
* **Glow**：明るい所の周りの、丸いぼけ（Off/Weak/Normal/Strong）。
* **Breathing**：画面全体の明るさで絵が伸び縮みします（Off/Weak/Normal/Strong）。外周ほど大きく動き、Aperture grilleは円筒の画面（辺はまっすぐ）、Aperture grille (FD, flat)は平らな画面（縮むと辺の中ほどが内側に凹む）、ほかのマスクは球面の画面（縮むと太鼓型）として歪みます。
  * **Expand when bright (mid = normal size)**：オンにすると、中間の明るさのときが基準の大きさになり、暗いと縮み、明るいと膨らみます。膨らむと画面の端の絵が少し切れます。
* **Vignette**：周辺減光（Off/Weak/Normal/Strong、四隅で10/25/40%暗くなる）。Aperture grilleは左右の端が、ほかのマスクは四隅が暗くなります。
* **Corner blur**：周辺のぼけ（Off/Weak/Normal/Strong）。画面の端ほどビームの点が大きくなり、にじみます。Aperture grilleは左右の端に、ほかのマスクは四隅に出ます。
* 既定は、すべてのフィルターがオフ（今までと同じ絵）です。

### 音量

* 設定の音量のページに**Master**（全体の音量、-40〜+20dB）を追加しました。
* テープの音（再生音・リレー）とディスクの音には、Masterを掛けません。それぞれTape sound volume/Disk sound volumeだけで決まります。

### ディスクの新規作成

* Diskメニューの**New disk image**で、空のディスクイメージ（D88かFDX）を作れます。作ったディスクをドライブ1に入れる（In drive 1...）、ドライブ2に入れる（In drive 2...）、入れない（Without inserting...）から選びます。名前を決めた後、M88の元からある「新しいディスクの作成」の窓で、タイトル・種類（2D/2DD/2HD）・BASICのフォーマットを選びます（フォーマットは2DのD88だけ）。
* 既にある名前を選ぶと、置き換えるか聞きます。ドライブに入っているイメージは置き換えられません。
* M88の元の作り方（ドライブのOpenの窓で、無い名前を入れる）も今までどおり使えます。ドライブ2で作ったディスクにBASICのフォーマットを掛けても、ファイルに書かれなかった（ドライブ1のディスクを余分に書き直していた）のを直しました。

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
* 設定の「その他」に「ショートカットにAltでなくアプリケーションキーを使う」を追加しました。M88の元の動きでは、Alt (= GRPH) + Enterで全画面になるので、PC-8801にGRPH + RETURNを入れられませんでした。オンにすると、AltはいつもGRPHとしてPC-8801に渡し、全画面などの操作はアプリケーションキー+Enter/F1/F2/F5/F10で行います（そのキーはPC-8801に渡しません）。
* Burst mode（Controlメニュー）や設定の「全力駆動」で速く動かしているとき、キーを普通に押しただけでもキーリピートで同じ文字が大量に入っていたのを直しました。エミュレーターの中の時間で約0.1秒より長く押したキーは、指を離すまで離したことにします（SHIFT・CTRL・GRPH・カナ・CAPSは除く）。そのため、速く動かしている間は、キーを押し続ける操作（キーリピートや、押している間だけ動くゲームなど）はできません。

### 設定・ファイルダイアログ

* 設定ダイアログの、M88gnでは効かない・意味が合わなくなった項目（全画面時640x480にする、全画面モードでVSyncに同期、ウィンドウモードでサイズを2倍にする、ファイルダイアログの位置バーを表示する）を、値を残したまま灰色にしました。倍率はScreenメニューのWindow scaleで選びます。
* ファイルダイアログが古い形で開き、ツールバーのアイコンが小さかったのを直しました。今の形のダイアログでは、読み取り専用で開くのは「開く」ボタンの▼から選びます。

### CPU (Z80)

* x86版のZ80エミュレーション（`src/devices/Z80_x86.cpp`）の、OUTI/OUTD/OTIR/OTDRの不具合を直しました。
  * M88の元の動きでは、これらの命令の後に割り込みが待っていると、次の命令を先に実行してからHLを増減していたので、次の命令がHLに入れた値が1ずれました。OTIR/OTDRは、くり返しの途中でも次の命令を先に実行していました。
  * 今はHLを増減してから割り込みを確かめます。OTIR/OTDRは、くり返している間は確かめません。
  * これにより、XeGrader体験版（PC-8801mkIISR用）がプレイ中に止まっていたのが直りました。

### エミュレーションの修正（M88amから）

蕎麦P氏のM88amで直された、エミュレーションの不具合の修正を取り込みました。

* ADPCM（YM2608）：ADPCMのRAMをデータの置き場に使うソフトで、「RAM無し」と判定されたり止まったりしていたのを直しました（外部メモリアクセスのモードで、読み書きの起点がスタートアドレスで決まるようにしました）。繰り返し再生で、ループの端でもEOSを立てるようにしました（PC-8801FA/MAの店頭デモが次へ進まなかったのが直ります）。
* CRTC：アトリビュートの無いテキストのモードで、画面の反転（START DISPLAYのreverse）が効いていなかったのを直しました。
* 画面の割り込み（VRTC）：表示の最後の1行が時間を使っていなかったなどで、1フレームが短くなっていたのを直しました（24kHzで57.50Hz→55.41Hz、実機は55.42Hz）。VRTCを数えて時間を計るソフトの進み方が、実機に近くなります。
* 600Hzの割り込み：598.8Hzになっていたのを、600Hzにしました。
* スナップショット：画面の反転の直しに合わせて、CRTCの版を上げました。1.3.0までのスナップショットは読めます（反転中に保存したものも正しく表示します）が、1.4.0で保存したスナップショットは1.3.0までのM88gnでは読めません。

### デバッガとペイン

蕎麦P氏のM88am（<https://www.ambientworks.jp/m88am/>、2026/09-01版。rururutan氏の私家版をもとにした派生）のデバッガを取り込み、機能を足しました。Debugメニューで使います。

* 止める・進める（Pause・Play・Step In・Step Over・Step Out）、実行・メモリ・I/Oポート・パターンのブレークポイント（条件式と回数）、ウォッチ、トレース、ラベル、ストップウォッチとラップタイム。
* コード・メモリ・レジスタ・ブレークポイント・ウォッチ・トレース・ラベル・ステータス・ラップタイム・ニーモニック表・サブシステムの記録・Video（GVRAM・テキストVRAM・パレット）の窓。
* デバッグの窓・Videoの窓・テープの窓は、本体の窓の上下左右のペインにタブで入れられます（ドック）。
* どの窓も右クリックメニューを強化したので、とりあえず右クリックしてみて下さい。

使い方は、図入りの説明[docs/debugger.md](docs/debugger.md)にまとめました。

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
| `TapeWinX`/`TapeWinY`/`TapeWinW`/`TapeWinH` | テープの窓の位置と大きさ（幅が0なら既定の大きさで、M88のウィンドウの右に出す） | 0 |
| `TapeWinShow` | テープの窓を表示しているか（0/1） | 0 |
| `ScreenScale` | ウィンドウの表示の倍率（1〜4） | 無ければ、設定の「画面2倍」か画面のDPIから決める |
| `ScreenScaleFree` | Window scaleがFreeか（0/1） | 0 |
| `WindowWidth`/`WindowHeight` | 本体の窓の大きさ（Freeのとき、次の起動で使う。物理ピクセル） |  |
| `Theme` | テーマ（0: Windows default 1: Dark 2: Beige） | 0 |
| `VideoMode` | 録画の絵（0: With screen filter 1: Without filter (x2)） | 0 |
| `VideoFps` | 録画のフレームレート（60/30/20/15） | 60 |
| `ScreenRotate` | 画面の回転（0: 0° 1: 90° 2: 180° 3: 270°、右回り） | 0 |
| `FullScreenHideMenu` | 全画面でメニューバーを隠す（0/1） | 0 |
| `WindowHideMenu` | ウィンドウでメニューバーを隠す（0/1） | 0 |
| `ScreenDisplay` | 表示の型（0: Analogue RGB CRT 1: Direct 2: Plasma 3: Green 4: White 5: Amber 6: Digital RGB） | 無ければ`CrtDirect`から決める |
| `CrtDirect` | 1.4.2までの「Direct」の設定（前の版のために、`ScreenDisplay`に合わせて書きます） | 0 |
| `PlasmaLevels` | プラズマの階調（0: 2 1: 4 2: 8 3: 16） | 2 |
| `PlasmaBackground` | プラズマの消えている所も弱く光らせる（0/1） | 0 |
| `CrtScanline` | 走査線（0: Off 1: Weak 2: Medium 3: Strong） | 0 |
| `CrtMask` | マスク（0: None 1: Aperture grille 2: Shadow mask 3: Slot mask 4: Aperture grille (FD, flat)） | 0 |
| `CrtMaskPitch` | マスクの間隔（0: Pixel 1: 0.25mm 2: 0.31mm 3: 0.39mm） | 0 |
| `CrtSharpness` | 横のにじみ（0: Sharp 1: Normal 2: Soft） | 1 |
| `CrtBrightness` | 明るさの補正（0: Low 1: Normal 2: High） | 1 |
| `CrtGlow` | 明るい所の周りのぼけ（0: Off 1: Weak 2: Normal 3: Strong） | 0 |
| `CrtBreathing` | 明るさで絵が伸び縮みする（0: Off 1: Weak 2: Normal 3: Strong） | 0 |
| `CrtBreathExpand` | 伸び縮みの基準（0: 真っ白で基準の大きさ 1: 中間の明るさで基準の大きさ） | 0 |
| `CrtVignette` | 周辺減光（0: Off 1: Weak 2: Normal 3: Strong） | 0 |
| `CrtCornerBlur` | 周辺のぼけ（0: Off 1: Weak 2: Normal 3: Strong） | 0 |
| `VolumeMaster` | 全体の音量（dB + 100。60で消音） | 100 |

デバッグの窓は、それぞれのセクション（`[MonitorCode]`、`[MonitorMemory]`、`[MonitorRegister]`、`[MonitorSound]`、`[MonitorBasic]`、`[MonitorIO]`、`[MonitorBP]`、`[MonitorIOBP]`、`[MonitorWatch]`、`[MonitorTrace]`、`[MonitorMnemonic]`、`[MonitorSubLog]`、`[MonitorStatus]`、`[MonitorLap]`、`[MonitorLabel]`、`[MonitorVideo]`）に、位置と大きさ（`PosX`/`PosY`/`Width`/`Height`、物理ピクセル）、そのときのDPI（`Dpi`）、文字の大きさ（`FontSize`、96DPIでのピクセル）を保存します。`Dpi`の無い値（1.3.0までの値）は、96DPIの値として読み替えます。フォントは`[Monitor]`の`FontFace`・`FontCharset`に保存します。コードの窓はPCを追うか（`PCFollow`）、トレースの窓は記録件数（`Capacity`）と並び順（`Ascending`）、コードの窓はサイクル数を出すか（`Cycles`）、メモリの窓はチェックサムの方式（`Checksum`）、Videoの窓は開いていたタブ（`Tab`）・拡大（`Zoom`）・文字の大きさ（`TextZoom`）・セミグラフィックの表示（`Semigraphics`）も保存します。ラベルの一覧の窓は並び順（`SortByName`）も保存します。窓をペインに入れている間は、浮かせたときの位置を変えません。ペインの並びは`[Dock]`に保存します（[docs/debugger.md](docs/debugger.md)を参照）。

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

* M88の作者cisc氏、私家版のrururutan氏、M88gのGIMONS氏、M88amの蕎麦P氏に感謝します。
* デバッガと、エミュレーションの不具合の修正の一部は、蕎麦P氏のM88am（<https://www.ambientworks.jp/m88am/>。rururutan氏の私家版をもとにした派生）のものを取り込みました。

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

### M88gから取り込んだコード

FDX形式のフロッピーイメージへの対応などは、GIMONS氏のM88g（<http://retropc.net/gimons/emulators/>）のものを取り込みました。M88gで変更されたのは既存のファイルだけで、FDX対応のコードは追加コードとして2条項BSDライセンスに、それぞれのファイルはM88のライセンスにも従います。

### M88amから取り込んだコード

デバッガと、エミュレーションの不具合の修正（ADPCM、CRTCの反転、VRTC・600Hzの割り込みのタイミング）は、蕎麦P氏のM88am（<https://www.ambientworks.jp/m88am/>）のものを取り込みました。M88amで新しく作られたファイルはM88amの2条項BSDライセンスに、M88amで変更された既存のファイルはM88のライセンスに従います。

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
