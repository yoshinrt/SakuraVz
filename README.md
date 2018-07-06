# Sakura Editor VZ 化 fork
[![Build status](https://ci.appveyor.com/api/projects/status/mf8tx836jtb7epq8/branch/vz_mode?svg=true)](https://ci.appveyor.com/project/YoshiNRT/sakura/branch/vz_mode)

サクラエディタに VZ Editor (以下，Vz と表記) の機能のいくつか (と追加で細かい機能) を実装するプロジェクトです．現在以下の機能が追加されています．下記以外の細かい修正点は [issues](https://github.com/yoshinrt/sakura/issues?utf8=%E2%9C%93&q=is%3Aissue+is%3Aclosed+-label%3Awontfix) を参照してください．

機能の on/off を GUI で設定する機能はありません．`sakura.ini` を直接編集してください．本 fork のビルドでは，デフォルトで全て有効になっています．

最新バイナリは [AppVeyor](https://ci.appveyor.com/project/YoshiNRT/sakura/branch/vz_mode) の Configuration:Release→ARTIFACTS にあります．

### テキストスタック

- クリップボードにデータを格納するコマンドはすべて，テキストスタックに PUSH 動作を行います．
- クリップボードからデータを貼り付けるコマンドはすべて，テキストスタックからの POP 動作を行います．
- 「貼り付け(スタックPOPなし)」コマンドを追加しています (マクロ命令: `CopyPaste()`)．Vz で Shift+F8 に割り当てられていた機能です．

`sakura.ini`: `bVzModeEnableTextStack=1` で有効になります

### 方向感応式の行単位・文字単位選択切り替え
選択中にカーソル↑↓で行単位の選択，カーソル←→で文字単位の選択に切り替えます．

`sakura.ini`: `nVzModeSelectMode=1` で有効になります

### 「ラインモード貼付けを可能にする」の対象コマンドを拡大
以下の，行単位の Cut or Copy を行う操作に対し，共通設定→編集→ラインモード貼付けを可能にする オプションの設定が反映されるようになりました．

- 選択中にカーソル↑↓
- 行番号エリアをドラッグ時の行選択
- メニューの編集→高度な操作→範囲選択内全行～コピー

### Vz 互換の「単語単位で検索」仕様

- `*` で単語境界のチェックをキャンセルします．
- スペースで複数単語を検索できる仕様は削除しています

`sakura.ini`: `bVzModeWordSearch=1` で有効になります

### 直前の置換を再実行

直前の置換を再実行する機能 (Vz で Shift+F7) を追加しました．`ReplaceAll()` で，引数が 0 個のとき，直前の置換条件で置換を再実行します．

コマンドではなくマクロ関数として実装されていますので，キーに割り当てるためには次のマクロを組む必要があります．

```
ReplaceAll();
ReDraw();
```

### 実行中のマクロ名，マクロ ID を取得する `GetMacroInfo( mode )` マクロ命令
`mode` には以下の値のいずれかを指定します．

- 0: 「共通設定」→「マクロ」の「マクロ名」に設定された文字列を取得します．
- 1: 「共通設定」→「マクロ」の「番号」を取得します．
- 2: 「共通設定」→「マクロ」の「ファイル名」を取得します．

以下のようなマクロを組むことで，1つの *.js ファイルを複数のマクロで共有することができます．共通設定→マクロ の設定で，「マクロ名」に `Hoge` または `Fuga` を指定し，ファイル名は同じものを指定します．

```
var FuncTable = {};

// 検索・置換ダイアログ //////////////////////////////////////////////////////

FuncTable.Hoge = function(){
	MessageBox( "Hoge" );
}

FuncTable.Fuga = function(){
	MessageBox( "Fuga" );
}

// function ジャンプ /////////////////////////////////////////////////////////

if( FuncTable[ GetMacroInfo()]) FuncTable[ GetMacroInfo()]();
else throw new Error( "見つかりません: " + GetMacroInfo() + "\nfile:" + GetMacroInfo( 2 ));
```

### 検索・置換ダイアログのオプション，デフォルトボタンを固定できるパラメータ追加
`SearchDialog( mode )`, `ReplaceDialog( mode )` に引数を追加しました．mode には以下の値のいくつかを `|` で繋げて指定します．

- 0x00000001 / 0x00000002:「単語単位で探す」チェックボックス クリア / セット
- 0x00000004 / 0x00000008:「英大文字と小文字を区別する」チェックボックス クリア / セット
- 0x00000010 / 0x00000020:「正規表現」チェックボックス クリア / セット
- 0x00000040 / 0x00000080:「見つからないときにメッセージを表示」チェックボックス クリア / セット
- 0x00000100 / 0x00000200:「検索 (置換) ダイアログを自動的に閉じる」チェックボックス クリア / セット
- 0x00000400 / 0x00000800:「先頭(末尾)から再検索する」チェックボックス クリア / セット
- 0x00001000 / 0x00002000:「クリップボードから貼り付ける」チェックボックス クリア / セット
- 0x80000000:「上検索」ボタンをデフォルトにする
- 0x40000000:「下検索」ボタンをデフォルトにする
- 0x20000000:「該当行マーク」ボタンをデフォルトにする
- 0x10000000:「置換」ボタンをデフォルトにする
- 0x08000000:「すべて置換」ボタンをデフォルトにする
- 0x04000000: 検索ダイアログにおいて「上検索」「下検索」が押されたとき，検索条件のみセットし，検索動作は行いません．

### 他アプリで編集されたときの問い合わせダイアログボックス抑制
オープン中のファイルが他アプリで変更されたとき，サクラエディタで未編集状態であれば，問い合わせなしで再読込するオプションを追加しました．

`sakura.ini`: `bVzModeNoAskWhenFileUpdate=1` で有効になります

----
以下は，サクラエディタ本家の README になります．本 fork には適用されない情報もありますので，ご了承ください．

# Sakura Editor
[![Build status](https://ci.appveyor.com/api/projects/status/xlsp22h1q91mh96j/branch/master?svg=true)](https://ci.appveyor.com/project/sakuraeditor/sakura/branch/master)

A free Japanese text editor for Windows

## Web Site
- [Sakura Editor Portal](https://sakura-editor.github.io/)

## 開発参加ポリシー
開発ポリシーを以下にまとめていきます。開発にご参加いただける方はこちらご参照ください。  
https://github.com/sakura-editor/sakura/wiki

## Build Requirements
### Visual Studio Community 2017
- [Visual Studio Community 2017](https://www.visualstudio.com/downloads/)

#### Visual Studio Install options required
- Windows SDK
- Windows XP Support for C++
- C++に関するWindows XP サポート

More information: https://github.com/sakura-editor/sakura/issues/6

## How to build
Visual Studio Community 2017 で `sakura.sln` を開いてビルド。

## CI Build (AppVeyor)
本リポジトリの最新 master は以下の AppVeyor プロジェクト上で自動ビルドされます。  
https://ci.appveyor.com/project/sakuraeditor/sakura/branch/master

最新のビルド結果（バイナリ）はここから取得できます。  
https://ci.appveyor.com/project/sakuraeditor/sakura/branch/master/artifacts  
[`これ`](installer/warning.txt) を読んでからご利用ください。

[`x64 版は alpha 版`](installer/warning-alpha.txt)です。  
対応中のため予期せぬ不具合がある可能性があります。  

最新以外のビルド結果は以下から参照できます。  
https://ci.appveyor.com/project/sakuraeditor/sakura/history
