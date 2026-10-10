# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**この CLAUDE.md は、毎セッション全文が読み込まれる。短く保つ（200 行以内）。** 機能別の設計メモ（決定・罠・限界）は `.claude/rules/`（`paths:` のファイルを触ったときだけ読み込まれる。下の「設計メモの索引」）、テスト・実測・実機未確認の記録は `.claude/notes/`（自動では読み込まれない）に置く。書き足す規則は、末尾の「ドキュメントの同期」。

## ビルドと実行

ビルドには Rake を使用する（CMake のラッパー）。

```bash
rake cmake:debug      # Debug 用 CMake 構成を生成
rake cmake:release    # Release 用 CMake 構成を生成
rake cmake:xcode      # Xcode プロジェクトを生成
rake build:debug      # Debug ビルド
rake build:release    # Release ビルド
rake run:debug        # Debug ビルドして実行
rake run:release      # Release ビルドして実行
rake icon             # アプリアイコン(assets/AppIcon.png → resources/AppIcon.icns)を作り直す
```

成果物は `_build/debug/Miata.app` および `_build/release/Miata.app` に生成される。

署名（任意）：環境変数 `MIATA_CODESIGN_IDENTITY`（証明書の SHA-1 ハッシュか名前）を付けて `rake build:*` / `rake run:*` すると、`make` のあとに、その証明書で `.app` に署名する。無ければ、これまでどおりアドホック署名。証明書の名前・チーム ID は、コミットするファイルに書かない。TCC の許可がビルドし直しても残るようになる。詳細は「権限エラーの案内（macOS の保護）」の「署名」。

## アーキテクチャ概要

macOS 専用のファイルブラウザアプリケーション「Miata」。**フルネイティブ AppKit**（`NSWindow`/`NSView`/`NSScrollView`/`NSTextField`/`NSButton` 等）で描画し、Lua スクリプトでキーバインドとコマンドを定義する。

以前は sokol + Dear ImGui でレンダリングしていたが、**現在は完全にネイティブ AppKit へ移行済み**。`packages/sokol`・`packages/imgui` は Git submodule として存在するが、`CMakeLists.txt` はどちらもビルド・リンク対象に含めていない（`add_subdirectory(packages)` は `lua` のみをビルドする）。sokol/ImGui 関連の API（`simgui_*`、`sapp_*`、`MTKView`、`CADisplayLink` 等）はコードベース中に一切登場しない。

**技術スタック:**
- C++23、Objective-C++（`.mm`。macOS バインディングとAppKitを直接扱う層はすべて `.mm`）
- レンダリング: フルネイティブ AppKit（`drawRect:` によるカスタム描画、`NSScrollView` 等の標準コントロール）
- スクリプト: Lua（コルーチンベースのダイアログ制御）
- リアクティブ: RxCpp（`ReactiveProperty` 等のユーティリティ、`FileListModel` のパス変更通知などに使用）
- 外部依存はすべて `packages/` に Git submodule として存在（実際にビルドされるのは `lua` のみ）

**構成:**
- `src/models/` — データモデル（`FileListModel`、`FileEntryModel`、`BrowserModel`）と、フォルダの履歴 `PathHistory`（AppKit 非依存。左右で 1 つを共有し、`BrowserModel` が持つ）と、ペインの保存用の状態 `PaneState`（AppKit 非依存。保存と復元は `View`）
- `src/views/` — UI レイヤー。`View`（ダイアログキュー管理・ブラウザ操作の起点）、`BrowserView`（左右ペインの `NSSplitView` と、その上に被せるプレビューの覆い、各ペインの下の入力バー（検索・絞り込み）を持つコンテナ）、`FileListView`（ファイル一覧本体。`NSScrollView` + 自前 `NSView.drawRect` で描画）、`QuickLookView`（Quick Look のプレビューを載せる覆い）、`QueryBar`（各ペインの下の入力バー）、`ProgressOverlay`（ファイル操作の進捗を、ウィンドウの右上に重ねる半透明のパネル）、`ProgressState`（そのパネルに、何をいつ出すかの判断。AppKit 非依存）、`SearchState`・`FilterState`（検索・絞り込みの状態。AppKit 非依存）、`NameMatcher`（名前に語が一致するかの判定=部分一致。検索と絞り込みが共用する）、`Dialog`（`IDialog`/`DialogPanel` によるダイアログ基盤）
- `src/widgets/` は存在しない（過去のドキュメントの残骸。汎用ウィジェットは今のところ `views/` 直下に個別実装されている）
- `platforms/` — OS 固有実装（`.mm`）。現状 macOS 用の `osx.mm` と `main.mm` のみ

**主要クラスの役割:**

| ファイル | 役割 |
|---------|------|
| `Application.cc/h` | `pl_start_timer` による 0.05 秒周期の更新処理（`Update()`。旧 `FrameImpl` 相当だが実際のフレームループではない）、キーイベント処理、`Miata.command.*`/`Miata._private.*` のコマンド登録（シングルトン） |
| `Script.cc/h` | Lua VM 管理、C++ 関数登録（`RegisterFunctions`）、設定の読み込み（`PostInitialize`: 組み込みの既定の設定 → ユーザーの `init.lua`。後述）、コルーチン駆動（`InvokeRefFunctionOnThread`/`Update`） |
| `KeyBinding.h` | キーストローク解析、モード別（Normal/Dialog）キーマップ管理、キーコード → 英数字の逆引き（`KeyCodeToAscii`。ダイアログ項目のショートカットが使う） |
| `views/View.h/.mm` | ダイアログキュー管理（`RequestDialog`/`CheckDialogState`）、ブラウザ操作へのキー入力ルーティング |
| `views/Dialog.h/.mm` | ダイアログ基盤。`IDialog`（confirm/yesno/inputtext/custom/filterlist の基底）と `DialogPanel`（実体となる非モーダル NSView オーバーレイ）。詳細は後述 |
| `views/FileListView.h/.mm` | ファイル一覧の描画・スクロール・キーボードカーソル移動（`NSScrollView` + 自前描画）。マーク済みファイルのドラッグ元（`NSDraggingSource`）も兼ねる（後述）。見えているマーク済みのエントリを画面の並び順で返す `MarkedEntries()` は、ファイル操作の対象・ドラッグ・Lua の `marked_entries` が使う（後述「Lua から状況を取る」）。マークの一括操作（`MarkAll` / `MarkRange` / `MarkSearchHits` / `StepMark`）も持つ（後述「まとめてマークする」）。絞り込み（`sorted_` から語に一致する行を選んで `list_` を作る `ApplyFilter` ほか。あいまい一致は外部の fzf に任せる `FuzzySource`）も持つ（後述「絞り込み（フィルタ）」） |
| `views/QuickLookView.h/.mm` | Quick Look（`QLPreviewView`）のプレビューを一覧の上に被せる覆い。クリックを止めてキー入力を守る。ピンチは、ズームできる中のビューへ渡し、倍率の指定（`Zoom` / `SetZoom`）も持つ（後述） |
| `views/ProgressOverlay.h/.mm` | ファイル操作の進捗を、ウィンドウの右上に重ねる半透明のパネル（1 操作 = 1 枚。縦に積む）。クリックもキーも受けない（素通し）。何を出すかは `ProgressState` が決め、ここは渡された内容を描くだけ（後述「進捗パネル」） |
| `views/ProgressState.h/.cc` | 進捗パネルに、何をいつ出すかの判断（0.3 秒たってから出す・フェード・「完了」を見せる・題の文言・割合）。時刻を注入できる純ロジックで、AppKit 非依存。`View` が持つ（後述「進捗パネル」） |
| `views/SearchState.h/.cc` | ファイル名の検索の状態（遷移・ヒット・n/N と件数の計算）。AppKit 非依存。1ペイン分で、`FileListView` が持つ（後述「ファイル名の検索」） |
| `views/QueryBar.h/.mm` | 検索・絞り込みをしているペインの下に出す入力バー（プロンプト + `NSTextField` の入力欄 + 件数）。ペインごと・種類（`constants::QueryKind`）ごとに1つ、`BrowserView` が持つ（後述） |
| `views/FilterState.h/.cc` | 絞り込みの状態（遷移・基準の位置・行ごとの一致箇所）と、バーの件数の文言 `FilterCountText`、カーソルの寄せ先 `NearestShownRow`。AppKit 非依存。1ペイン分で、`FileListView` が持つ（後述「絞り込み（フィルタ）」） |
| `views/NameMatcher.h/.mm` | 語がファイル名に一致するかの判定（いまは部分一致のみ。スマートケース）と、一致箇所（UTF-16 の範囲）。検索（`RebuildSearchHits`）と絞り込み（`ApplyFilter`）が共用する |
| `views/QueryTypes.h` | 検索と絞り込みが共有する値の型（`QueryMode`・`MatchKind`=部分一致/あいまい一致・`MatchRange`・`ListPosition`）。AppKit 非依存 |
| `views/Mnemonic.h/.cc` | ダイアログ項目のラベルの `&x`（ショートカット）の解析。AppKit 非依存の純関数 `ParseMnemonicLabel`（後述「ダイアログ項目のショートカット」） |
| `views/ViewMetrics.h` | 一覧・入力バー・進捗パネルが共有するフォント（`MakeFont`）・1 行の高さ（`LineHeight`）・ヘッダーの高さ（`HeaderHeight`）。`.mm` 専用 |
| `views/FileIconCache.h` | ファイル名の頭のアイコン（Finder と同じ `iconForFile:`）を、パスごとに保持する（全ペインで共有。ディレクトリ移動と件数の上限で捨てる。クラウドストレージの中(`pl_is_in_cloud_storage`)・ダウンロード前の印が付いたもの・UTF-8 として不正なパスは、拡張子の種類のアイコン）。`FileListView::Draw` が使う。`.mm` 専用（後述「ファイル名の頭のアイコン」。入り切りは `Config::ShowIcons()` / `Miata.command.toggle_icons`） |
| `models/PaneState.h/.cc` | ペインの保存用の状態（いるフォルダ・ソートの名前・降順か）と、保存用の辞書との変換、保存してあった値の検査（`IsPlainAbsolutePath`）。AppKit・Lua・Config のどれも知らない純ロジック（`PathHistory` と同じ作り）。保存と復元は `View` が行う（後述「ペインの状態の保存」） |
| `models/PathHistory.h/.cc` | フォルダの履歴（新しい順・重複なし・上限・現在地の除外・保存していない変更の有無）。AppKit・Lua・Config・Pane のどれも知らない純ロジック。左右のペインで 1 つを共有し、`BrowserModel` が持つ（左右の `FileListModel` は、そこへのポインタで記録する。後述「フォルダの履歴」） |
| `FzfFilter.h/.cc` | 外部の fzf（`fzf --filter`）による文字列一覧の絞り込み。AppKit 非依存。候補を一時ファイルに書き出し、語ごとに fzf を起動する（`pl_run_process`）。`dialog_filter_list` と、絞り込みのあいまい一致（`FileListView::FuzzySource`）が使う。fzf が無いときは部分一致にフォールバック（後述「絞り込み（フィルタ）」「外部コマンドの起動」） |
| `Utf8.h/.cc` | UTF-8 の検証と修復（`IsValidUtf8` / `RepairUtf8`）。AppKit 非依存。ファイル名を、UTF-8 として正しい形にするために使う（後述「名前が UTF-8 として不正なファイル」） |
| `FileError.h` | ファイル操作の失敗（`FileError` = OS の説明 + 「権限が無い失敗か」、`FileErrorSummary` = 複数の失敗のまとめ）。権限の失敗に、許可のしかたを案内するために使う（後述「権限エラーの案内」） |
| `FileOperation.h/.cc` | コピー・移動を裏スレッドで実行する `FileOperationManager`（開始 `Start`・毎ティックの `Update`・完了のコールバック・実行中の操作の進捗の写し `Running`）と、始める前の確認 `FileOperationGuard`（同じフォルダ・フォルダを自分の中へ・先の同名のフォルダが元の祖先、を断る）と、コピーの中身（自前の再帰 `CopyEntry` / `CopyDirectory`。ファイルごとに `pl_copy_file`）。AppKit 非依存（`pl_*` は `platforms/osx.mm`）。呼ぶのは `Application`（後述「ファイル操作（コピー・移動）」） |
| `FileOperationProgress.h` | 操作の種類 `FileOpType`（と、画面に出す名前 `FileOpLabel`）・操作の番号 `FileOperationId`・実行中のコピー・移動の進捗の写し（`FileOperationProgress` = 項目数・バイト数・いま処理している名前、`FileOperationStatus` = 操作の ID・種類・開始時刻と進捗）。値の型だけで、AppKit 非依存。`FileOperationManager` が作り、`ProgressState` が読む |
| `misc.h` | `Flags`、`ReactiveProperty`、`MessageBroker` などのユーティリティ |
| `platform.h` | OS 依存処理の抽象境界（`pl_*` 関数群の宣言）。色・フォント・ダイアログ用構造体・ファイル操作・ディレクトリ監視（`pl_watch_directory`）・設定の保存（`pl_save_string_list` / `pl_save_string_map`）・プロセス起動・ファイルを開く/Finderで表示/クリップボード（`pl_open_paths` / `pl_reveal_paths` / `pl_set_clipboard_text` / `pl_find_application`）・ゴミ箱（`pl_trash_file`。クラウドストレージの中で、権限で断られたときは、Finder に頼む `pl_trash_file_via_finder` / `pl_is_in_file_provider_domain`。後述「権限エラーの案内」）・外部コマンドの起動（`pl_run_process`。後述）・ファイルのコピー（`pl_copy_file` / `pl_copy_directory_attributes`。`copyfile(3)`。後述「コピーの中身」）・Finder のエイリアスの判定（`pl_is_alias_file`）・ダウンロード前のファイルの判定（`pl_is_dataless_file`）（どちらも後述「一覧の右側の札」）など |
| `platforms/osx.mm` | `platform.h` の macOS 実装。AppKit 型はこの層（と `views/*.mm`）にのみ閉じ込め、ヘッダ（`.h`）には持ち込まない規約 |

**Lua スクリプト:**
- `resources/base.lua` — コアユーティリティとダイアログヘルパー定義
- `resources/test.lua` — **組み込みの既定の設定**（キーバインドとコマンド定義）。`.app` の `Contents/Resources/` に入る
- `~/.config/miata/init.lua` — **ユーザーの設定**。無くてよい。あれば、既定の設定の後に読み込まれて上書きする（後述「設定の読み込み」）

C++ 側は `Miata.command.*`（`Application.cc` の `InitializeScript()` 内の `config_commands[]`/`view_commands[]`）と `Miata._private.*`（同 `privates[]`）の名前空間で Lua 関数を登録し、スクリプト側から呼び出す。ダイアログはコルーチンで非同期制御される（後述）。

## 横断的な約束事（どの機能にも効く）

複数の機能が同居するファイル（`Application.cc`・`osx.mm`・`FileListView.mm` ほか）を触るときも効く規則。根拠と細部は、括弧内のルール（食い違ったら、ルールが正）。

- **`luaL_error` は longjmp**（Lua は C としてビルドされている）：デストラクタを持つオブジェクト（`std::string` など）を作った後に呼ばない。引数を検証して `luaL_error` を呼んでから、オブジェクトを作る。（`lua-commands.md`・`history-panes.md`）
- **ユーザー操作の移動は `TryJumpTo` / `NavigateToParent`**：`JumpTo` は、読めないと例外を投げる（コードベースに `catch` は無い）ので、起動時（ホーム）とテスト専用。同じディレクトリを再スキャンするだけなら `View::ReloadList(model, cursor_to)`（`JumpTo(Path())` ではない。カーソルとマークが消える）。（`permissions-signing.md`・`file-list.md`）
- **ファイル操作・移動の失敗は、必ず `View::ReportFileError` に通す**（権限の失敗に、許可のしかたの案内が付く）。例外は、起動時のペインの復元（案内を出さず、黙ってホームのまま。直さない）。（`permissions-signing.md`・`history-panes.md`）
- **マークを外して画面にも反映するなら `UnmarkPaths`**（モデルの `Mark()` はビューに通知しない）。コピー・移動・ゴミ箱・リネームの対象は「見えているマーク」（`FileListView::MarkedEntries()`）で、完了後に外すのは、操作したファイルのマークだけ。（`file-list.md`・`search-filter.md`）
- **エントリ（`FileEntryModel*`）のポインタを持ち越さない**：再スキャンで旧エントリは、通知より前に破棄される。持つのはパス。Lua に渡すのも、呼んだ時点の値の写し。（`file-list.md`・`lua-commands.md`）
- **名前は `Name()`、パスは `Path()`**：`Name()` は NFC で、UTF-8 として不正なバイトは修復済み（表示・並べ替え・検索用）。`Path()` は元のバイト列（コピー・移動・ゴミ箱・OS の呼び出し用）。外から来た文字列を NSString にするときは、`RepairUtf8` に通す（不正だと nil になり、`labelWithString:nil` などで落ちる）。（`file-list.md`）
- **空の一覧に備える**：カーソル下は `CurrentOrNull()`（空なら nullptr）か `CurrentPath()`。範囲外を読む入口を作らない（絞り込みで 0 行になるのは、日常の状態）。（`file-list.md`）
- **`std::filesystem` は、外部で変更されうる処理ではエラーコード版を使う**（例外版は、消えたディレクトリなどで落ちる）。失敗は `std::expected` / `FileError` で返す。（`file-list.md`・`permissions-signing.md`）
- **新しいビューは、`acceptsFirstResponder` を YES にしない**：クリックで first responder を奪うと、キー入力（`MiataRootView::keyDown:`）が届かなくなる。入力欄（`NSTextField`）が first responder の間は `keyDown:` が呼ばれないので、Enter / Esc / ↑↓ は delegate の `doCommandBySelector:` で横取りする。（`dialog.md`・`search-filter.md`・`quicklook.md`）
- **ダイアログは `NSAlert` / `runModal` を使わない**（非モーダルな `NSView`。`DialogPanel`）。Lua からは、コルーチンの中で `dialog_*` を呼び、結果を待つ。（`dialog.md`）
- **色の名前（`Config::Color`）を足したら、描く側と `resources/test.lua` の既定値を同時に直す**（既定値が無いと、不透明な黒で描かれる。古いアプリが新しい `test.lua` を読むと、未知の色名のエラーで、後ろの設定が実行されない）。背景は `Config::Background()`（alpha 1）で、OS の色は使わない（ダイアログだけ OS の色のまま）。（`config.md`・`file-list.md`）
- **新しい `resources/` のファイルは、cmake の再生成が要る**（`GLOB_RECURSE` は構成時にしか評価されない。`rake build:*` は再生成する。`make` を直接使うときだけ注意）。（`permissions-signing.md`）
- **テストは、リポジトリの外（スクラッチ）に作る**：実物の `Application::Initialize()` を模擬 `.app` から起動し、合成した `NSEvent` を送る。実装を 1 か所ずつ壊して、検出できることも確かめる（変異テスト）。各機能の検証の記録とハーネスの罠は、`.claude/notes/`。

## C++ から Lua へ関数を登録する手順

1. `Application.h` の `Application` クラスに `static int lua_command_XXX(lua_State* L)`（`Miata.command.*` 用）または `static int lua_private_XXX(lua_State* L)`（`Miata._private.*` 用）を追加
2. `Application.cc` の `InitializeScript()` の `view_commands[]`（`Miata.command` 用）または `privates[]`（`Miata._private` 用）に `{ "name", lua_command_XXX }` を追加。Viewに触らず、設定ファイルの読み込み中にも使えるべきもの(`bind`/`unbind` のような)だけ `config_commands[]` に入れる（後述「設定の読み込み」のトランポリン）。**設定の関数 `Miata.config.set_*` は、これらではなく `Config.cc` の `config_funcs[]`**（`.claude/rules/config.md`「その他の設定」）
3. OS 依存の実装が必要な場合は `platform.h` に `pl_*` 関数を宣言し、`platforms/osx.mm` に実装を追加（AppKit 型はここか `views/*.mm` にのみ閉じ込め、`.h` には持ち込まない。例外は、`.mm` からだけ include する AppKit 依存のヘッダ: `views/ViewMetrics.h`・`NSColorUtil.h`・`FileIconCache.h`）

**注意**：`Miata.command.*` と `Miata._private.*` は同じ実装が入り得る別の名前空間ではあるが、`Miata.command` テーブル自身の中で Lua 側（`base.lua`）の関数と C++ 側の関数に同じキー名を使ってはいけない。`Application::InitializeScript()` は `script.Initialize()`（`base.lua` 読み込み）の後に `RegisterFunctions("Miata.command", ...)` を実行するため、同名なら C++ 側が Lua 側を**無言で上書きする**（コンパイルエラーにも起動時エラーにもならず、該当キーを実際に呼び出した時だけ引数不一致などで失敗する）。Lua ラッパー＋その内部で使う生の C++ 実行関数、という組み合わせを作る場合は、`dialog_input`（`Miata.command`）/ `dialog_open`（`Miata._private`）や `make_folder`（`Miata.command`）/ `make_directory`（`Miata.command`だが別名）のように、公開する名前と内部実装の名前を必ず分ける（内部実装は `Miata._private.*` に置くのが基本）。

## 設計メモの索引（`.claude/rules/`）

機能別の設計メモ（決定・罠・限界）は `.claude/rules/` にある。`paths:` に挙げたファイルを Read / Edit すると、自動で読み込まれる。**`Application.cc`・`osx.mm`・`FileListView.mm` など、複数の機能が同居するファイルでは自動では載らない**ので、下の表から該当するファイルを自分で読むこと。本文中の「後述「…」」「前述「…」」は、下の表の節の名前で探す。テスト・実測・実機未確認の記録は、同じ名前で `.claude/notes/` にある（自動では読み込まれない）。

| ルール（`.claude/rules/`） | 扱う節 | 主な実装（複数の機能が同居するファイルを含む） |
|---|---|---|
| `dialog.md` | ダイアログの仕組み／選択リスト（`dialog_custom` の `select`）／ダイアログ項目のショートカット（`&x`） | `views/Dialog.*`・`views/FilterListDialog.*`・`views/Mnemonic.*`、`Application.cc`（`lua_private_dialog_*`）、`resources/base.lua`（`dialog_*`） |
| `file-operations.md` | ファイル操作（始める前の断り・コピーの中身・進捗パネル） | `FileOperation.*`・`FileOperationProgress.h`・`views/Progress*`、`Application.cc`（`StartFileOperation`）、`osx.mm`（`pl_copy_file`） |
| `permissions-signing.md` | 権限エラーの案内（TCC・Finder 経由のゴミ箱・署名）／アプリアイコン | `FileError.h`、`View::ReportFileError`、`osx.mm`（`pl_trash_file*`）、`Rakefile`（`sign_app` / `icon`）、`cmake/Info.plist.in` |
| `search-filter.md` | ファイル名の検索／絞り込み（部分一致・あいまい一致）／外部コマンドの起動（`pl_run_process`） | `views/SearchState.*`・`FilterState.*`・`NameMatcher.*`・`QueryBar.*`、`FzfFilter.*`、`FileListView.mm`・`BrowserView.mm`、`Application.cc`（`lua_command_search*` / `lua_command_filter*`） |
| `quicklook.md` | プレビュー（Quick Look。倍率・ピンチ・スクロール） | `views/QuickLookView.*`、`BrowserView.mm`（覆いの配置）、`Application.cc`（`lua_command_quick_look*`） |
| `history-panes.md` | フォルダの履歴／ペインの状態の保存／ウィンドウ位置・サイズの保存 | `models/PathHistory.*`・`PaneState.*`・`BrowserModel.*`、`View.mm`（`RestorePanes` ほか）、`osx.mm`（`pl_create_main_window`） |
| `file-list.md` | 一覧の右側の札（`<DIR>` ほか）／ファイル名の頭のアイコン／ドラッグ&ドロップ／再読み込み／ディレクトリ監視／空のディレクトリ／UTF-8 として不正な名前 | `models/FileListModel.*`・`FileEntryModel.*`、`Utf8.*`、`views/FileIconCache.h`、`FileListView.mm`、`osx.mm`（`pl_watch_directory` / `pl_is_alias_file` / `pl_is_dataless_file` ほか） |
| `lua-commands.md` | Lua から状況を取る／ファイルを開く／まとめてマークする | `Application.cc`（`lua_command_*`）、`FileListView.mm`、`osx.mm`（`pl_open_paths` ほか）、`resources/base.lua` |
| `config.md` | 設定の読み込み／キーバインド（`bind` / `unbind`）／色の設定（`Miata.config.color.*`）／その他の設定（`Miata.config.set_*`。実行中に切り替える `show_icons` の例外） | `Script.*`・`Config.*`・`KeyBinding.*`、`resources/test.lua`、`Application.cc`（`InitializeScript` / `lua_command_bind` / `lua_command_toggle_icons`） |

## ドキュメントの同期

- Lua に公開するコマンドやダイアログ種別を追加・変更した場合は、その都度 `README.md` の「Lua スクリプト API」セクション（引数・戻り値・注意点）も更新すること。
- アーキテクチャに影響する変更（新しいダイアログ種別の追加、`pl_*` の新設、ディレクトリ構成の変更など）があれば、関係する `.claude/rules/*.md` を更新する。ファイルの構成や、上の「アーキテクチャ概要」の表が変わるときは、この CLAUDE.md の表も直す。
- **この CLAUDE.md は増やさない**（200 行以内を目安にする）。新しい機能の設計メモ（決定・罠・限界）は、`.claude/rules/<機能>.md` に書き（`paths:` には、その機能に固有のファイルを挙げる）、この CLAUDE.md には索引の 1 行だけ足す（どの機能にも効く規則だけは、「横断的な約束事」に 1 行足す）。`Application.cc` など複数の機能が同居するファイルを `paths:` に入れない（触るたびに大半が載る）。
- テストの件数・変異テストの統計・実測のログ・実機未確認の一覧は、`.claude/notes/` に書く（CLAUDE.md と rules には書かない）。経緯（「以前は…」）も、PR とコミットメッセージに任せる。
- `.claude/rules/` の下に、`paths:` の無いファイルや、記録用のサブフォルダを作らない（`.md` は再帰的に探され、`paths:` が無ければ起動時に全部読み込まれる）。`paths:` の YAML を壊したときも同じ（黙って「`paths` なし」として扱われる）。
