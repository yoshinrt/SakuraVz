# Sakura Editor VZ 化 fork

サクラエディタに VZ Editor (以下，Vz と表記) の機能のいくつかを実装するプロジェクトです．現在以下の機能が追加されています．

### Vz の機能

- テキストスタック
    - クリップボードにデータを格納するコマンドはすべて，テキストスタックに PUSH 動作を行います．
    - クリップボードからデータを貼り付けるコマンドはすべて，テキストスタックからの POP 動作を行います．
    - 「貼り付け(スタックPOPなし)」コマンドを追加しています (マクロ命令: `CopyPaste()`)．Vz で Shift+F8 に割り当てられていた機能です．
- Vz 互換の「単語単位で検索」仕様．`*` で単語境界のチェックをキャンセルします．
    - スペースで複数単語を検索できる仕様は削除しています

### Vz とは関係ない機能

- 実行中のマクロ名，マクロ ID を取得する `GetMacroInfo( mode )` マクロ命令を追加しています．`mode` には以下の値のいずれかを指定します．
    - 0: 「共通設定」→「マクロ」の「マクロ名」に設定された文字列を取得します．
    - 1: 「共通設定」→「マクロ」の「番号」を取得します．
    - 2: 「共通設定」→「マクロ」の「ファイル名」を取得します．
- 検索・置換ダイアログのオプション，デフォルトボタンを固定できるよう，`SearchDialog( mode )`, `ReplaceDialog( mode )` に引数を追加しました．mode には以下の値のいくつかを `|` で繋げて指定します．
    - 0x00000001:「単語単位で探す」チェックボックス クリア
    - 0x00000002:「単語単位で探す」チェックボックス セット
    - 0x00000004:「英大文字と小文字を区別する」チェックボックス クリア
    - 0x00000008:「英大文字と小文字を区別する」チェックボックス セット
    - 0x00000010:「正規表現」チェックボックス クリア
    - 0x00000020:「正規表現」チェックボックス セット
    - 0x00000040:「見つからないときにメッセージを表示」チェックボックス クリア
    - 0x00000080:「見つからないときにメッセージを表示」チェックボックス セット
    - 0x00000100:「検索 (置換) ダイアログを自動的に閉じる」チェックボックス クリア
    - 0x00000200:「検索 (置換) ダイアログを自動的に閉じる」チェックボックス セット
    - 0x00000400:「先頭(末尾)から再検索する」チェックボックス クリア
    - 0x00000800:「先頭(末尾)から再検索する」チェックボックス セット
    - 0x00001000:「クリップボードから貼り付ける」チェックボックス クリア
    - 0x00002000:「クリップボードから貼り付ける」チェックボックス セット
    - 0x80000000:「上検索」ボタンをデフォルトにする
    - 0x40000000:「下検索」ボタンをデフォルトにする
    - 0x20000000:「該当行マーク」ボタンをデフォルトにする
    - 0x10000000:「置換」ボタンをデフォルトにする
    - 0x08000000:「すべて置換」ボタンをデフォルトにする
    - 0x04000000: 検索ダイアログにおいて「上検索」「下検索」が押されたとき，検索条件のみセットし，検索動作は行いません．

その他細かい修正点は [issues](https://github.com/yoshinrt/sakura/issues?utf8=%E2%9C%93&q=is%3Aissue+is%3Aclosed+-label%3Awontfix) を参照してください．

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

最新以外のビルド結果は以下から参照できます。  
https://ci.appveyor.com/project/sakuraeditor/sakura/history
