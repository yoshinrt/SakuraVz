# Sakura Editor VZ 化 fork
[![Build status](https://ci.appveyor.com/api/projects/status/mf8tx836jtb7epq8/branch/vz_mode?svg=true)](https://ci.appveyor.com/project/YoshiNRT/sakura/branch/vz_mode)
[![License: Zlib](https://img.shields.io/badge/License-Zlib-lightgrey.svg)](https://opensource.org/licenses/Zlib)

サクラエディタに VZ Editor (以下，Vz と表記) の機能のいくつか (と追加で細かい機能) を実装するプロジェクトです．現在以下の機能が追加されています．下記以外の細かい修正点は [issues](https://github.com/yoshinrt/SakuraVz/issues?q=is%3Aissue+is%3Aclosed+-label%3Awontfix+-label%3Atask+-label%3Aenbug+-label%3Arefactoring+sort%3Aupdated-desc) を参照してください．

機能の on/off を GUI で設定する機能はありませんので，`sakura.ini` を直接編集してください．本 fork のビルドでは，デフォルトで全て有効になっています．

最新バイナリは [AppVeyor](https://ci.appveyor.com/project/YoshiNRT/sakura/branch/vz_mode) の Platform:Win32 (または x64)→ARTIFACTS にあります．
### 一覧

- [テキストスタック](#テキストスタック)
- [改行をまたぐ検索・置換](#改行をまたぐ検索置換)
- [ファイルオープンの高速化](#ファイルオープンの高速化)
- [方向感応式の行単位・文字単位選択切り替え](#方向感応式の行単位文字単位選択切り替え)
- [「ラインモード貼付けを可能にする」の対象コマンドを拡大](#ラインモード貼付けを可能にするの対象コマンドを拡大)
- [改行混在を許可しないモード](#改行混在を許可しないモード)
- [直前の置換を再実行](#直前の置換を再実行)
- [他アプリで編集されたときの問い合わせダイアログボックス抑制](#他アプリで編集されたときの問い合わせダイアログボックス抑制)
- [JavaScript ファイルを共通にするためのマクロ関数](#javascriptファイルを共通にするためのマクロ関数)
- [カーソル位置に関するマクロ関数](#カーソル位置に関するマクロ関数)
- [検索・置換ダイアログのオプション，デフォルトボタンを固定できるパラメータ追加](#検索置換ダイアログのオプションデフォルトボタンを固定できるパラメータ追加)
- [コマンドラインオプションでGREP置換ダイアログ呼び出し](#コマンドラインオプションでGREP置換ダイアログ呼び出し)


### テキストスタック

- クリップボードにデータを格納するコマンドはすべて，テキストスタックに PUSH 動作を行います．
- クリップボードからデータを貼り付けるコマンドはすべて，テキストスタックからの POP 動作を行います．
- 「貼り付け(スタックPOPなし)」コマンドを追加しています (マクロ命令: `CopyPaste()`)．Vz で Shift+F8 に割り当てられていた機能です．

`sakura.ini`: `bVzModeEnableTextStack=1` で有効になります

### 改行をまたぐ検索・置換
正規表現で改行をまたぐ検索・置換に対応しました．

- 検索の「該当行マーク」でマッチ結果が改行をまたぐ場合，先頭行のみマークされます．
- 検索でマッチ結果が改行をまたぐ場合，色付けされません．
- アウトラインの正規表現，正規表現キーワード では改行をまたぐ検索はできません．

また検索・置換の仕様を以下のように変更しています．

- 正規表現エンジンとして [PCRE2](https://www.pcre.org/current/doc/html/pcre2syntax.html) を内蔵しています．bregonig.dll 等の外部 DLL は使用できません．
- サクラエディタ標準の「単語単位で検索」仕様は削除されました．正規表現を使用してください．
- 上記に代わり Vz 互換の「単語単位で検索」仕様を追加しました．`*` で単語境界のチェックをキャンセルします．
  - 例: `AB*` で，AB から始まる単語にマッチします
- 置換の「置換対象」は削除されました．正規表現を使用してください．
- 置換の「すべて置換は置換の繰り返し」は削除されました．

### ファイルオープンの高速化

特に大きなファイルで，ファイルオープンを高速化しました．

- 一部の処理に並列処理を導入しました．
- 巨大なファイル (10MB 以上，設定可能) では高速モードになり，一部の機能を制限することにより更に高速化します．高速モードでは，以下の動作に制限が入ります．
  - 折り返し設定は 5120文字で固定され，他の折り返し設定は使用できません．全角・半角にかかわらず 5120文字なので，全角・半角が混在する文書では折り返し位置が揃わないことになります．
  - ブロックコメントや文字列リテラル等の一部のカラーリングが，不正確になります．

400MB, 1000万行のファイルオープンに要する時間 (秒) は以下のとおりです．(使用 PC: AMD A8-7670K (4スレッド)  Mem:8GB)

|折返し設定|[Sakura本家 v2.4.0.2219](https://github.com/sakura-editor/sakura/tree/40147238dc12b2490466de0dfee78006c8e73cba)|SakuraVz (非高速モード)|SakuraVz (高速モード)|
|--------|---:|---:|---:|
|折返さない  |32.9|26.3|5.4|
|右端で折返す|50.1|25.7|5.4<br>(強制的に折返さない)|

`sakura.ini`: `nVzModeLargeFileSize` にファイルサイズ (MB単位) を設定することにより，このファイルサイズ以上のファイルは高速モードでオープンします．`nVzModeLargeFileSize=0` で高速モードは無効になります．

### 方向感応式の行単位・文字単位選択切り替え
選択中にカーソル移動 または マウスドラッグによる選択で，↑↓で行単位の選択，←→で文字単位の選択に切り替えます．

`sakura.ini`: `nVzModeSelectMode=1` で有効になります

### 「ラインモード貼付けを可能にする」の対象コマンドを拡大
以下の，行単位の Cut or Copy を行う操作に対し，共通設定→編集→ラインモード貼付けを可能にする オプションの設定が反映されるようになりました．

- 選択中にカーソル↑↓
- マウス操作による行選択
- メニューの編集→高度な操作→範囲選択内全行～コピー

### 改行混在を許可しないモード

標準では一つの文書内で CRLF, LF, CR 等の改行コードを混在させることができますが，メニューの 設定→共通設定→編集→改行コードを変換して貼り付ける を有効にすることで，改行コードの混在を許可しないモードになります．これにより，挙動が以下のように変更されます．

- 開いたファイルは，エディタ内部ですべて LF に統一され，保存時に元の改行に統一してから保存します．これにより，
    -  正規表現やマクロの `InsText()` で改行を示す文字列は，ファイルの改行コードによらず，( `\r\n` 等ではなく ) `\n` に統一されます．
    - メニューの「入力改行コードの指定」，マクロの `GetLineCode()`, `ChgmodEOL()` は，入力時の改行コードではなく，保存時の改行コードの設定・取得になります．入力時に LF 以外を入力することはできません．
- 貼り付け時に，クリップボードに格納された文字列は，改行コードが LF に統一されてから貼り付けられます．
- コピー時にクリップボードに格納される文字列は，改行コードが CRLF に統一されます．
- 改行が混在している (例: `InsText( "\r" )` を実行した) ファイルを保存する場合，ユーザに問い合わせすることなく，設定された改行コードに統一してから保存します．

### 直前の置換を再実行

直前の置換を再実行する機能 (Vz で Shift+F7) を追加しました．`ReplaceAll()` で，引数が 0 個のとき，直前の置換条件で置換を再実行します．

コマンドではなくマクロ関数として実装されていますので，キーに割り当てるためには次のマクロを組む必要があります．

```
ReplaceAll();
```

### 他アプリで編集されたときの問い合わせダイアログボックス抑制
オープン中のファイルが他アプリで変更されたとき，サクラエディタで未編集状態であれば，問い合わせなしで再読込するオプションを追加しました．

`sakura.ini`: `bVzModeNoAskWhenFileUpdate=1` で有効になります

### JavaScriptファイルを共通にするためのマクロ関数
標準では 1マクロ = 1 JavaScript ファイル ですが，マクロファイル管理の容易化のため，1 JavaScript ファイルを複数のマクロ・プラグインで共有することができる仕組みを追加しました．

- 実行中のマクロ情報を取得する関数 `GetMacroInfo( mode )` を追加しました．`mode` には以下の値のいずれかを指定します．
  - 0: 「共通設定」→「マクロ」の「マクロ名」に設定された文字列を取得します．
  - 1: 「共通設定」→「マクロ」の「番号」を取得します．
  - 2: 「共通設定」→「マクロ」の「ファイル名」を取得します．
- プラグインとして呼ばれたときのジャック名を取得する関数 `Plugin.GetPluginInfo( 0 )` を追加しました．引数は 0 を指定してください．

以下のようなマクロを組むことで，1つの *.js ファイルを複数のマクロで共有することができます．共通設定→マクロ の設定で，「マクロ名」に `Hoge` または `Fuga` を指定し，ファイル名は同じものを指定します．

```
var FuncTable = {};

// 個別のマクロ
FuncTable.Hoge = function(){
  MessageBox( "Hoge" );
}

FuncTable.Fuga = function(){
  MessageBox( "Fuga" );
}

// DocumentOpen プラグイン
FuncTable.PluginDocumentOpen = function(){
  MessageBox( "Plugin: DocumentOpen" );
}

// 個別のマクロにジャンプ
var FuncName = typeof( Plugin ) != "undefined" ?
  "Plugin" + Plugin.GetPluginInfo( 0/*JackName*/ ) : GetMacroInfo();

if( FuncTable[ FuncName ]) FuncTable[ FuncName ]();
else throw new Error( "見つかりません: " + FuncName + "\nfile:" + GetMacroInfo( 2 ));
```

### カーソル位置に関するマクロ関数
カーソル位置に関するマクロ関数を追加しました．

 - `GetCursorPosX()`: カーソル位置の行頭からのレイアウト桁位置 (行頭 = 0) を返します．
 - `GetCursorPosXLogic()`: カーソル位置の行頭からの論理桁位置 (行頭 = 0) を返します．
 - `GetCursorPosY()`: カーソル位置のファイル先頭からの表示行位置 (先頭行 = 0) を返します．
 - `GetCursorPosYLogic()`: カーソル位置のファイル先頭からの論理行位置 (先頭行 = 0) を返します．
 - `GetLayoutWidthX()`: レイアウト 1桁分の幅を返します．
 - `GetCursorChar()`: カーソル位置の 1文字を取得します．
 - `IsCursorEOL()`: カーソル位置が EOL の場合，非 0 を返します．
 - `IsCursorEOF()`: カーソル位置が EOF の場合，非 0 を返します．
 - `MoveTo( line, col [, sel] )`: レイアウト座標 line 行，col 桁に移動します．sel に 1 を指定した場合，選択状態を維持します．

### 検索・置換ダイアログのオプション，デフォルトボタンを固定できるパラメータ追加
`SearchDialog( mode )`, `ReplaceDialog( mode )` に引数を追加しました．mode には以下の値のいくつかを `|` で繋げて指定します．

- 0x00000001 / 0x00000002:「単語単位で探す」チェックボックス クリア / セット
- 0x00000004 / 0x00000008:「英大文字と小文字を区別する」チェックボックス クリア / セット
- 0x00000010 / 0x00000020:「正規表現」チェックボックス クリア / セット
- 0x00000040 / 0x00000080:「見つからないときにメッセージを表示」チェックボックス クリア / セット
- 0x00000100 / 0x00000200:「検索 (置換) ダイアログを自動的に閉じる」チェックボックス クリア / セット
- 0x00000400 / 0x00000800:「先頭(末尾)から再検索する」チェックボックス クリア / セット
- 0x00001000 / 0x00002000:「クリップボードから貼り付ける」チェックボックス クリア / セット
- 0x10000000:「上検索」ボタンをデフォルトにする
- 0x20000000:「下検索」ボタンをデフォルトにする
- 0x30000000:「該当行マーク」ボタンをデフォルトにする
- 0x40000000:「置換」ボタンをデフォルトにする
- 0x50000000:「すべて置換」ボタンをデフォルトにする
- 0x60000000: 検索ダイアログにおいて「上検索」「下検索」が押されたとき，検索条件のみセットし，検索動作は行いません．
- 0x70000000: 検索ダイアログを表示せず，カーソル位置の単語を検索語に設定します．

### コマンドラインオプションでGREP置換ダイアログ呼び出し

コマンドラインオプションで，`-grepdlg` と `-grepr=` オプションを同時に指定することで，GREP 置換ダイアログを呼び出せるようになりました．

例:
```
sakura.exe -grepmode -grepdlg -GREPR="a"
```


以下は，サクラエディタ本家の README になります．本 fork には適用されない情報もありますので，ご了承ください．

----

# Sakura Editor
[![Build status](https://ci.appveyor.com/api/projects/status/xlsp22h1q91mh96j/branch/master?svg=true)](https://ci.appveyor.com/project/sakuraeditor/sakura/branch/master)
[![Github Releases All](https://img.shields.io/github/downloads/sakura-editor/sakura/total.svg)](https://github.com/sakura-editor/sakura/releases "All Releases")
[![License: Zlib](https://img.shields.io/badge/License-Zlib-lightgrey.svg)](https://opensource.org/licenses/Zlib)
[![CodeFactor](https://www.codefactor.io/repository/github/sakura-editor/sakura/badge)](https://www.codefactor.io/repository/github/sakura-editor/sakura)
[![Build Status](https://dev.azure.com/sakuraeditor/sakura/_apis/build/status/sakura-editor.sakura?branchName=master)](https://dev.azure.com/sakuraeditor/sakura/_build/latest?definitionId=3&branchName=master)
[![build sakura](https://github.com/sakura-editor/sakura/workflows/build%20sakura/badge.svg)](https://github.com/sakura-editor/sakura/actions?query=workflow%3A%22build+sakura%22)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=sakura-editor_sakura&metric=alert_status)](https://sonarcloud.io/dashboard?id=sakura-editor_sakura)
[![Star History](https://img.shields.io/badge/star-histroy-yellow.svg)](https://star-history.t9t.io/#sakura-editor/sakura)

<!-- 以下は Markdownの参照形式によるリンク の定義です。 -->
<!-- 参照 https://hail2u.net/blog/coding/markdown-reference-style-links.html -->

[Visual Studio Community 2017]: https://visualstudio.microsoft.com/vs/older-downloads/ "Visual Studio Community 2017"
[Visual Studio Community 2019]: https://visualstudio.microsoft.com/ja/downloads/ "Visual Studio Community 2019"
[Markdown をローカルで確認する方法]: https://github.com/sakura-editor/sakura/wiki/markdown-%E3%82%92%E3%83%AD%E3%83%BC%E3%82%AB%E3%83%AB%E3%81%A7%E7%A2%BA%E8%AA%8D%E3%81%99%E3%82%8B%E6%96%B9%E6%B3%95
[How to extract currently installed Visual Studio component IDs?]: https://stackoverflow.com/questions/52946333/how-to-extract-currently-installed-visual-studio-component-ids
[Configure Visual Studio across your organization with .vsconfig]: https://devblogs.microsoft.com/setup/configure-visual-studio-across-your-organization-with-vsconfig/
[インストール構成をインポートまたはエクスポートする]: https://docs.microsoft.com/ja-jp/visualstudio/install/import-export-installation-configurations?view=vs-2019
[コマンド ライン パラメーターを使用して Visual Studio をインストールする]: https://docs.microsoft.com/ja-jp/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=vs-2019
[不足しているコンポーネントを自動的にインストールする]: https://docs.microsoft.com/ja-jp/visualstudio/install/import-export-installation-configurations?view=vs-2019#automatically-install-missing-components

<!-- TOC -->

- [Sakura Editor](#sakura-editor)
  - [Hot topic](#hot-topic)
  - [Web Site](#web-site)
  - [開発参加ポリシー](#開発参加ポリシー)
  - [Build Requirements](#build-requirements)
    - [Visual Studio Install options required](#visual-studio-install-options-required)
    - [Visual Studio Community 2019 対応に関して](#visual-studio-community-2019-対応に関して)
    - [.vsconfig に関して](#vsconfig-に関して)
      - [Visaul Studio 2017/2019 共通](#visaul-studio-20172019-共通)
      - [Visaul Studio 2019 のみ](#visaul-studio-2019-のみ)
      - [参照](#参照)
  - [How to build](#how-to-build)
    - [詳細情報](#詳細情報)
  - [CI Buildおよびローカルビルドの環境変数](#ci-buildおよびローカルビルドの環境変数)
  - [PR(Pull Request) を簡単にローカルに取得する方法](#prpull-request-を簡単にローカルに取得する方法)
  - [CI Build (Azure Pipelines)](#ci-build-azure-pipelines)
    - [ビルドの仕組み (Azure Pipelines)](#ビルドの仕組み-azure-pipelines)
  - [CI Build (AppVeyor)](#ci-build-appveyor)
    - [ビルドの仕組み (AppVeyor)](#ビルドの仕組み-appveyor)
    - [ビルド成果物を利用する上での注意事項](#ビルド成果物を利用する上での注意事項)
    - [ビルド成果物のダウンロード(バイナリ、インストーラなど)](#ビルド成果物のダウンロードバイナリインストーラなど)
      - [master の 最新](#master-の-最新)
      - [master の 最新以外](#master-の-最新以外)
  - [開発情報](#開発情報)
    - [単体テスト](#単体テスト)
    - [デバッグ方法](#デバッグ方法)
  - [変更履歴](#変更履歴)
  - [マクロのサンプル](#マクロのサンプル)
  - [静的コード解析](#静的コード解析)

<!-- /TOC -->

A free Japanese text editor for Windows

## Hot topic
Project(カンバン)運用を始めます。

- [Projects](https://github.com/orgs/sakura-editor/projects)
- [カンバン運用](https://github.com/sakura-editor/sakura/wiki/ProjectOperation)

## Web Site
- [Sakura Editor Portal](https://sakura-editor.github.io/)

## 開発参加ポリシー
開発ポリシーを以下にまとめていきます。開発にご参加いただける方はこちらご参照ください。  
https://github.com/sakura-editor/sakura/wiki

## Build Requirements

Sakura Editor をコンパイルするためにはいずれかが必要
-  [Visual Studio Community 2017][Visual Studio Community 2017]
-  [Visual Studio Community 2019][Visual Studio Community 2019]

正式バイナリは [Visual Studio Community 2017][Visual Studio Community 2017] でビルドされます。

### Visual Studio Install options required
- Windows SDK
- Windows XP Support for C++
- Windows 8.1 SDK と UCRT SDK
- C++ に関する Windows XP サポート

More information: https://github.com/sakura-editor/sakura/issues/6

### Visual Studio Community 2019 対応に関して

Visual Studio 2017/2019 の両対応に関しては  [#866](https://github.com/sakura-editor/sakura/issues/866) で対処済みです。  
[仕組みに関してはこちらを参照](vcx-props/project-PlatformToolset.md)

### .vsconfig に関して

Sakura Editor のコンパイルに必要なコンポーネントを Visual Studio 2017/2019 にインストールするために [.vsconfig](.vsconfig) という設定ファイルを用意しています。

[#1162](https://github.com/sakura-editor/sakura/pull/1162) で [.vsconfig](.vsconfig) というファイルを sakura.sln と同じディレクトリに配置しています。

#### Visaul Studio 2017/2019 共通

`vs_community__XXXXX.exe` でインストールする際に、--config オプションをつけてインストールする。
あるいは構成変更することにより、必要なコンポーネントを自動的にインストールします。

```
vs_community__XXXXX.exe --config <.vsconfig のファイルパス>
```

#### Visaul Studio 2019 のみ

`sakura.sln` と同じディレクトリに [.vsconfig](.vsconfig) が存在するので、
Visaul Studio 2019 で `sakura.sln` を開くと必要なコンポーネントが足りない場合、インストールを促す表示が出るので、インストールをクリックすると自動的にインストールする。

#### 参照

- [How to extract currently installed Visual Studio component IDs?][How to extract currently installed Visual Studio component IDs?]
- [Configure Visual Studio across your organization with .vsconfig][Configure Visual Studio across your organization with .vsconfig]
- [インストール構成をインポートまたはエクスポートする][インストール構成をインポートまたはエクスポートする]
- [コマンド ライン パラメーターを使用して Visual Studio をインストールする][コマンド ライン パラメーターを使用して Visual Studio をインストールする]
- [不足しているコンポーネントを自動的にインストールする][不足しているコンポーネントを自動的にインストールする]

## How to build

- [7Zip](https://sevenzip.osdn.jp/) のインストールして 7z.exe へのパスを通します。
- Visual Studio Community 2017 で `sakura.sln` を開いてビルドします。

### 詳細情報

詳しくは [こちら](build.md) を参照

## CI Buildおよびローカルビルドの環境変数

[CI でのビルド](ci-build.md) を参照

## PR(Pull Request) を簡単にローカルに取得する方法

- [PR(Pull Request) を簡単にローカルに取得する方法](get-PR.md)

## CI Build (Azure Pipelines)

### ビルドの仕組み (Azure Pipelines)

[azure-pipelines.md](azure-pipelines.md) でビルドの仕組みを説明しています。

## CI Build (AppVeyor)

### ビルドの仕組み (AppVeyor)

[appveyor.md](appveyor.md) でビルドの仕組みを説明しています。

### ビルド成果物を利用する上での注意事項

[`これ`](installer/warning.txt) を読んでからご利用ください。

[`x64 版は alpha 版`](installer/warning-alpha.txt)です。  
対応中のため予期せぬ不具合がある可能性があります。 

### ビルド成果物のダウンロード(バイナリ、インストーラなど)

#### master の 最新

1. https://ci.appveyor.com/project/sakuraeditor/sakura/branch/master にアクセスする
2. 右端にある `Jobs` をクリックします。
3. 自分がダウンロードしたいビルド構成 (例: `Configuration: Release; Platform: Win32`) をクリックします。
4. 右端にある `ARTIFACTS` をクリックします。
5. 自分がダウンロードしたいものをクリックしてダウンロードします。
   - (ユーザー用) 末尾に `Exe` がついてるのが実行ファイルのセットです。
   - (ユーザー用) 末尾に `Installer` がついてるのがインストーラのセットです。
   - ~~(すべて欲しい人向け) `All` がついてるのがバイナリ、インストーラ、ビルドログ、アセンブラ出力のフルセットです。~~ ([#514](https://github.com/sakura-editor/sakura/issues/514) の軽減のため無効化中) 
   - (開発者用) 末尾に `Log` がついてるのがビルドログのセットです。
   - (開発者用) 末尾に `Asm` がついてるのがアセンブラ出力セットです。

#### master の 最新以外

以下から取得したいビルドを選択後、同様にしてダウンロードできます。  
https://ci.appveyor.com/project/sakuraeditor/sakura/history

## 開発情報

### 単体テスト

[単体テスト](unittest.md) を参照

### デバッグ方法

- [タスクトレイのメニュー項目をデバッグする方法](debug-tasktray-menu.md) を参照
- [大きなファイルの作成方法](create-big-file.md)

## 変更履歴

- 変更履歴は [CHANGELOG.md](https://github.com/sakura-editor/sakura/blob/master/CHANGELOG.md) を参照してください。
- 最新の `CHANGELOG.md` は [ここからダウンロード](https://ci.appveyor.com/project/sakuraeditor/changelog-sakura/branch/master/artifacts) できます([Markdown をローカルで確認する方法](https://github.com/sakura-editor/sakura/wiki/markdown-%E3%82%92%E3%83%AD%E3%83%BC%E3%82%AB%E3%83%AB%E3%81%A7%E7%A2%BA%E8%AA%8D%E3%81%99%E3%82%8B%E6%96%B9%E6%B3%95)
で説明している手順でローカルで確認できます)。
- `CHANGELOG.md` は [PullRequest](https://github.com/sakura-editor/sakura/pulls) から自動的に生成しています。
  - 具体的には [github-changelog-generator](https://github.com/github-changelog-generator/github-changelog-generator) というソフトを使用して [changelog-sakura](https://github.com/sakura-editor/changelog-sakura) のリポジトリで [appveyor](https://ci.appveyor.com/project/sakuraeditor/changelog-sakura) で生成しています。
  - 詳細は wiki の [CHANGELOG.mdについて](https://github.com/sakura-editor/sakura/wiki/CHANGELOG.md%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6) をご覧ください。

## マクロのサンプル

[こちら](tools/macro)でマクロのサンプルを提供してます。  
もしサンプルを作ってもいいよ～という方がおられましたら PR の作成お願いします。

## 静的コード解析

- CodeFactor を利用しています。
    - [cpplint.py](CPPLINT.md) で cpp のスタイルのチェックを行っています。


