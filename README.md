# Sakura Editor VZ 化 fork

サクラエディタに VZ Editor (以下，Vz と表記) の機能のいくつかを実装するプロジェクトです．現在以下の機能が追加されています．

### Vz の機能

- テキストスタック
- Vz 互換の「単語単位で検索」仕様．`*` で単語境界のチェックをキャンセルします．

### Vz とは関係ない機能

- 実行中のマクロ名，マクロ ID を取得する `GetMacroInfo()` マクロ関数追加
- 検索・置換ダイアログのオプションを固定できるよう，`SearchDialog()`, `ReplaceDialog()` に引数追加

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
