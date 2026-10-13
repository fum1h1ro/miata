# 権限エラーの案内（macOS の保護）・署名・アプリアイコン・ビルドの版 — テスト・実測・未確認の記録

> 自動では読み込まれない。テストを書く・直す・検証するとき、実機で確かめることを探すときに読む。
> 設計の決定と罠は `.claude/rules/permissions-signing.md`。

## 権限エラーの案内（macOS の保護）

- **未確認**：「システム設定を開く」のURLが、フルディスクアクセスの画面を直接開いたか(開いたことは確認、画面までは未確認)。
  - **テスト**（リポジトリ外のスクラッチ。`ftrash_test.mm`）：`platforms/osx.mm` を直接 `#include` して（リンクからは `osx.mm.o` を除く）、静的関数と差し替え口を使う。ObjC の `Nil` はマクロなので、列挙子の名前にできない。**本物の Finder に頼む部分は、署名した小さな `.app`（試験用のアプリ ID）から動かす**（許可のダイアログが、その `.app` の名前で出る。端末から直接動かすと、許可が端末のものになる）。確かめたこと：判定（純関数の表と、偽のホームの総当たり: 領域の奥・直下・領域そのもの・`CloudStorage` 直下・`CloudStorageX`・リンク経由・リンクそのもの・親が無い・`CloudStorage` が無いホーム・相対パス）、失敗の説明の割り当て、方針（`trashItemAtURL:`・許可の確認・送信を差し替えて: 標準が成功なら頼まない・領域の中の権限の失敗で頼む（イベントの class/id・対象のアプリ ID・直接の引数の型と URL・オプションとタイムアウト・許可の確認の引数）・権限でない失敗では頼まない・許可の拒否・タイムアウト・Finder が断った（`errn`/`errs`。`errn` が 0 なら成功）・返事も説明も無い・領域そのもの・変わった名前（URL を戻すと元のパスになる）・UTF-8 として不正な名前（URL に `%FF`）・シンボリックリンクは許可の確認も送信もしない・領域の外は頼まない）、本物の Finder（上の挙動と、標準の経路の回帰、許可済みの所要時間、タイマーが発火しないこと）。実装を 1 か所ずつ壊す変異は、走らせていない。**`pl_trash_file` に、クラウドストレージの中のパスを渡すと、権限の失敗のとき本物の Finder に頼む**ので、テストでは、必ず `trashItemAtURL:` と送信と許可の確認を差し替える（下の「必ず失敗するケースだけ」の注意に加えて）
  - **未確認（実機）**：本物の Dropbox での、フォルダ・複数のファイル・大きなファイル、Google Drive・OneDrive・iCloud Drive、Miata 本体のオートメーションの許可がビルドし直しで残るか、クラウドストレージの外とのコピー・移動
- **テスト**：権限の失敗は、スクラッチの書き込めない（`0555`）・読めない（`0000`）・走査だけできない（`0111`）ディレクトリと、`UF_IMMUTABLE`（`chflags uchg`）で、実際に起こす。**`pl_trash_file` は、必ず失敗するケースだけ**（書き込めないディレクトリの中、存在しないファイル）で呼ぶこと（成功すると、実際にユーザーのゴミ箱へ移る）。**システム設定を開かない**：`-[NSWorkspace openURL:]` を `class_replaceMethod` で、URL を記録するだけのものに差し替える（実物が呼ばれると、ユーザーの画面にシステム設定が出る）。ダイアログの回答は `OnButton(yes_id_)` + `CheckDialogState()`。権限を落としたディレクトリは、後始末で必ず戻す（テストが途中で落ちても消せるよう、実行前に `chmod -R 755` と `chflags -R nouchg` をかける）。

## ビルドの版（ウィンドウのタイトル）

表示だけの変更なので、テストは軽め（ユーザーの指示。変異テストはしていない）。

- **テスト**（リポジトリ外のスクラッチ）：(1) `rake build:debug` が通る（新しい警告なし）。(2) ビルドした `Info.plist` の 3 つのキー（`plutil -p`。`plutil -lint` も OK）：未コミット → `CFBundleLongVersionString` = `615ad1e-dirty  feat/build-version  Debug`・`CFBundleShortVersionString` = `615ad1e-dirty`・`CFBundleVersion` = `128`。コミット後に `rake build:debug` → `a6be73d  feat/build-version  Debug`・`a6be73d`・`129`（`-dirty` が消え、HEAD と一致、コミット数が 1 増える）。(3) 実物の `Application::Initialize()` を、模擬 `.app`（バンドル ID は別。ユーザーの保存済みのペイン・ウィンドウ位置に触れないため）から起動して、タイトルを読んだ：版あり → `Miata  a6be73d  feat/build-version  Debug`、キーを除いたもの → `Miata`（これまでどおり）。標準出力に `build: …` が出る。(4) ウィンドウの枠（タイトルバーを含む）を、画面に出さずに描いた PNG を、目で確認した（長さは 1024pt の幅に収まる。非アクティブ表示は灰色）
- **`make` を直接使うと更新されない（実測）**：コミットしたあとに、`_build/debug` で `make` だけを実行しても、`Info.plist` は `615ad1e-dirty  …` のまま（構成が走らない）。`rake build:debug`（`cmake` を再実行する）で更新された
- **未確認**：実機（Retina の実画面・アクティブなウィンドウ）での見た目。Xcode（`rake cmake:xcode`）での構成（`CMAKE_BUILD_TYPE` が空なので、`Debug`/`Release` が付かないはず）。git が無い環境（`unknown`）。detached HEAD（`detached`）。ブランチ名に `&` などが入った場合の置き換え（コードで `_` に置き換えているだけ）。`NSWindow.subtitle` にした場合の描かれ方
