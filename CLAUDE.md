# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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
```

成果物は `_build/debug/Miata.app` および `_build/release/Miata.app` に生成される。

## アーキテクチャ概要

macOS 専用のファイルブラウザアプリケーション「Miata」。**フルネイティブ AppKit**（`NSWindow`/`NSView`/`NSScrollView`/`NSTextField`/`NSButton`/`NSPopUpButton` 等）で描画し、Lua スクリプトでキーバインドとコマンドを定義する。

以前は sokol + Dear ImGui でレンダリングしていたが、**現在は完全にネイティブ AppKit へ移行済み**。`packages/sokol`・`packages/imgui` は Git submodule として存在するが、`CMakeLists.txt` はどちらもビルド・リンク対象に含めていない（`add_subdirectory(packages)` は `lua` のみをビルドする）。sokol/ImGui 関連の API（`simgui_*`、`sapp_*`、`MTKView`、`CADisplayLink` 等）はコードベース中に一切登場しない。

**技術スタック:**
- C++23、Objective-C++（`.mm`。macOS バインディングとAppKitを直接扱う層はすべて `.mm`）
- レンダリング: フルネイティブ AppKit（`drawRect:` によるカスタム描画、`NSScrollView` 等の標準コントロール）
- スクリプト: Lua（コルーチンベースのダイアログ制御）
- リアクティブ: RxCpp（`ReactiveProperty` 等のユーティリティ、`FileListModel` のパス変更通知などに使用）
- 外部依存はすべて `packages/` に Git submodule として存在（実際にビルドされるのは `lua` のみ）

**構成:**
- `src/models/` — データモデル（`FileListModel`、`FileEntryModel`、`BrowserModel`）
- `src/views/` — UI レイヤー。`View`（ダイアログキュー管理・ブラウザ操作の起点）、`BrowserView`（左右ペインの `NSSplitView` コンテナ）、`FileListView`（ファイル一覧本体。`NSScrollView` + 自前 `NSView.drawRect` で描画）、`Dialog`（`IDialog`/`DialogPanel` によるダイアログ基盤）
- `src/widgets/` は存在しない（過去のドキュメントの残骸。汎用ウィジェットは今のところ `views/` 直下に個別実装されている）
- `platforms/` — OS 固有実装（`.mm`）。現状 macOS 用の `osx.mm` と `main.mm` のみ

**主要クラスの役割:**

| ファイル | 役割 |
|---------|------|
| `Application.cc/h` | `pl_start_timer` による 0.05 秒周期の更新処理（`Update()`。旧 `FrameImpl` 相当だが実際のフレームループではない）、キーイベント処理、`Miata.command.*`/`Miata._private.*` のコマンド登録（シングルトン） |
| `Script.cc/h` | Lua VM 管理、C++ 関数登録（`RegisterFunctions`）、`resources/` の読み込み、コルーチン駆動（`InvokeRefFunctionOnThread`/`Update`） |
| `KeyBinding.h` | キーストローク解析、モード別（Normal/Dialog）キーマップ管理 |
| `views/View.h/.mm` | ダイアログキュー管理（`RequestDialog`/`CheckDialogState`）、ブラウザ操作へのキー入力ルーティング |
| `views/Dialog.h/.mm` | ダイアログ基盤。`IDialog`（confirm/yesno/inputtext/custom/filterlist の基底）と `DialogPanel`（実体となる非モーダル NSView オーバーレイ）。詳細は後述 |
| `views/FileListView.h/.mm` | ファイル一覧の描画・スクロール・キーボードカーソル移動（`NSScrollView` + 自前描画）。マーク済みファイルのドラッグ元（`NSDraggingSource`）も兼ねる（後述） |
| `misc.h` | `Flags`、`ReactiveProperty`、`MessageBroker` などのユーティリティ |
| `platform.h` | OS 依存処理の抽象境界（`pl_*` 関数群の宣言）。色・フォント・ダイアログ用構造体・ファイル操作・プロセス起動など |
| `platforms/osx.mm` | `platform.h` の macOS 実装。AppKit 型はこの層（と `views/*.mm`）にのみ閉じ込め、ヘッダ（`.h`）には持ち込まない規約 |

**Lua スクリプト:**
- `resources/base.lua` — コアユーティリティとダイアログヘルパー定義
- `resources/test.lua` — キーバインド設定とコマンド定義

C++ 側は `Miata.command.*`（`Application.cc` の `commands[]`）と `Miata._private.*`（同 `privates[]`）の名前空間で Lua 関数を登録し、スクリプト側から呼び出す。ダイアログはコルーチンで非同期制御される（後述）。

## ダイアログの仕組み（NSAlert は使っていない）

ダイアログは `NSAlert`/`runModal` を一切使わない。実体は `DialogPanel`（`views/Dialog.h/.mm`）が生成する、角丸背景つきの**非モーダルな `NSView`** で、メインウィンドウの `contentView` に直接 `addSubview` される（OS のイベントループは奪わないため、ネストしたイベントループ用の再入ガードは不要）。

**Lua ⇔ C++ の連携パターン**（`resources/base.lua` の各 `dialog_*` 関数、`Application.cc` の `lua_private_dialog_*` を参照）:
1. Lua が `Miata._private.dialog_open(type, opts)` を呼ぶと、対応する `IDialog` 派生（`ConfirmDialog`/`YesNoDialog`/`InputTextDialog`/`CustomDialog`/`FilterListDialog`）が生成され、`View` のキューに積まれ、Lua には userdata の「ハンドル」が返る。
2. Lua 側は `coroutine.yield()` を挟みつつ `Miata._private.dialog_is_open(handle)` をポーリングし続ける。これは `Application::Update()`（0.05 秒タイマー駆動）が毎ティック `Script::Update()` でコルーチンを再開することで進む、**タイマー駆動の疑似同期**であり、C++/OS レベルでは非同期。
3. ダイアログが閉じると `Miata._private.dialog_result(handle, type)` で結果（文字列・真偽値・テーブル・nil 等）を取得する。

**既存のダイアログ種別:**

| 関数（Lua） | type 文字列 | 説明 |
|------------|------------|------|
| `dialog_confirm(message, button_text)` | `"confirm"` | OK ボタンのみ |
| `dialog_yes_no(message, default_focus, yes_text, no_text)` | `"yesno"` | YES/NO ボタン |
| `dialog_input(message, initial)` | `"inputtext"` | `NSTextField` 1つ。戻り値は文字列 or nil |
| `dialog_custom(spec)` | `"custom"` | チェックボックス・ポップアップ・複数ボタン |
| `dialog_filter_list(spec)` | `"filterlist"` | 大量の文字列(`items`)から `fzf` 絞り込みで1件選択。戻り値は文字列 or nil |

`dialog_custom` の `spec` テーブル形式：
```lua
{
    title = "タイトル",
    message = "説明文",          -- optional
    buttons = {"OK", "キャンセル"},
    checkboxes = { { label = "ラベル", checked = false } },
    selects   = { { label = "ラベル", options = {"A","B"}, selected = 1 } },
}
-- 戻り値: { button=1, checkboxes={false}, selects={2} } または nil
```

**注意（テキストフィールドとキー入力の関係）**：`NSTextField` がダイアログ内で first responder になっている間（`dialog_input`/`dialog_filter_list` 表示中など）、`MiataRootView::keyDown:`（`platforms/osx.mm`）は一切呼ばれない。つまり通常のキーバインド経路（`KeyBindingMap::Dialog` → `IDialog::Navigate()` → `DialogPanel::NavigateUp/Down/...`）はテキストフィールドには効かない。矢印キー/Enter/Escape をテキスト入力と共存させる必要がある場合は、`FilterListDialog.mm` のように `NSTextFieldDelegate` の `control:textView:doCommandBySelector:`（IME 変換中は呼ばれないため日本語入力と安全に共存できる）で個別に横取りする。

## マーク済みファイルのドラッグ&ドロップ（他アプリへの持ち出し）

一覧は基本キーボード操作で、マウスを受け付けるのはこれだけ（クリックでのカーソル移動等は無い）。実装は `views/FileListView.mm` の `_MiataFileListNSView`（`mouseDown:`/`mouseDragged:` + `NSDraggingSource`）と `FileListView::BeginDrag()`/`EndDrag()`。

- **運ぶのは「そのペインのマーク済みファイル」だけ**。押した行がどれかは判定しない（未マークの行から始めても同じ）。マークが0件なら何もしない。順序は画面表示順（`list_` 順）。
- **ダイアログ表示中は無効**。`DialogPanel` は画面中央の小さな `NSView` でマウスを遮らないため、裏の一覧にもマウスが届く。`View` のコンストラクタが `SetDragGuard`（`BrowserView` 経由で左右ペインへ）で `!IsAnyDialogOpened()` を注入している（`FileListView` は `View` を知らない）。
- `_MiataFileListNSView` は `acceptsFirstResponder` を YES にしないこと（キー入力は `MiataRootView` に届き続ける必要がある）。
- **ドロップ後（`EndDrag`）**：ファイル監視の仕組みが無く、宛先（Finder 等）は受理後に非同期で移動を進めるため、0.3 秒待ってから**実ファイルの有無**で判断する。1件でも消えていれば `Reload()` で再スキャン（カーソルは維持され、移されなかったファイルのマークは残る）、そうでなければ `ClearMarks()` のみ。宛先が申告する操作種別（Move/Copy/Generic）は自己申告なので当てにしない。キャンセル/拒否時はマーク維持。
- **罠**：`NSDraggingItem.draggingFrame` にサイズ 0 は指定できず `NSRangeException` で落ちる。画像を省く件（`contents=nil`）でも枠は非ゼロにすること。多数マーク時にアイコンを先頭 16 件に絞っているのはこのため（残りは画像なしで運ぶ）。
- 自アプリ内へのドロップ（ペイン間ドラッグ）は未実装（`NSDraggingDestination` が必要）。

## 一覧の再読み込み（`reload`）

`Miata.command.reload([pane])` → `Application::lua_command_reload` → `FileListView::Reload()` → `FileListModel::Reload()`。1つのペイン（引数省略/nil なら現在のペイン、`"left"`/`"right"` ならそのペイン）だけを再スキャンし、**マークとカーソルをパスで引き継ぐ**。

- **ペインの表し方**：C++ では `views::constants::Pane`（`Constants.h`）、Lua には `"left"`/`"right"` の文字列で公開する。文字列⇔enum の変換は `Application.cc` の `PaneName()`/`ParsePane()` に集約し、`Miata.command.current_pane()`（戻り値）と `reload(pane)`（引数）が同じ表記を使うようにしている（戻り値をそのまま引数に渡せることが前提）。今後ペインを引数に取るコマンドを足すときもこれを使う。ペインを指定して取るアクセサは `View::CurrentPane()`/`GetFileListView(pane)`/`GetList(pane)`。
- 対象が現在のペインでなくても、カーソル・マークの復元は同じ（各ペインが自分の `cursorIndex_` とモデルを持つため）。フォーカスは動かさない。

- **役割分担**：マークはモデル（`FileEntryModel` のフラグ）なので `FileListModel::Reload()` が新旧をパスで突き合わせて引き継ぐ。カーソルはビューの状態で、`cursorIndex_` は「画面表示順の添字」なので、`FileListView` がパスで復元する（並びが変わっても同じファイルに追従。消えていたら再スキャン前の並びで次に残っているもの→無ければその前→無ければ先頭）。
- **罠（寿命）**：旧エントリはモデルの再スキャン時（`ObservePath()` の通知**より前**）に破棄される。ビューが通知を受けてから `list_` を読むと寿命切れの参照になるので、カーソル復元用の記録（`CursorMemo`：表示順のパス列とカーソル位置）は**再スキャン前に**パスとして控え、通知の購読側が `Fetch()` の直後に `RestoreCursor()` する。`FileListModel` は再スキャンで `entries_` を差し替えたら必ず `ObservePath()` へ通知すること（ビューが `list_` を作り直せるように）。
- **`JumpTo` は「ディレクトリを移動する」専用**（カーソルは先頭、マークは消える）。同じディレクトリを再スキャンして最新にしたいだけの処理（mkdir/rename/`delete_marked`/コピー・移動の完了後/ドロップ後）は `JumpTo(Path())` ではなく **`View::ReloadList(model, cursor_to)`** を使う（カーソルとマークが維持され、ゴミ箱や移動に失敗したファイルのマークも残る。再スキャン自体の失敗は無視する）。新しくそういう処理を足すときも `JumpTo(Path())` を使わないこと。
- `Reload` / `ReloadList` の `cursor_to`：旧パスが消えて新しいパスに移る操作（リネーム）では新しいパスを渡す。渡さないと、旧名が消えるのでカーソルは「次のファイル」に寄ってしまう。`rename` は `ReloadList(list, dest)` を使っている。Copy の移動元は中身が変わらないので、従来どおり `ClearMarks()` のまま（`Reload` ではない）。
- 失敗（ディレクトリが消えた・権限なし）は `std::expected` で返し、モデルもビューも一切変えない。`directory_iterator` の例外を投げる版は、外部で変更された後に使う再読み込みでは落ちるので使わない（エラーコード版）。

## C++ から Lua へ関数を登録する手順

1. `Application.h` の `Application` クラスに `static int lua_command_XXX(lua_State* L)`（`Miata.command.*` 用）または `static int lua_private_XXX(lua_State* L)`（`Miata._private.*` 用）を追加
2. `Application.cc` の `commands[]`（`Miata.command` 用）または `privates[]`（`Miata._private` 用）に `{ "name", lua_command_XXX }` を追加
3. OS 依存の実装が必要な場合は `platform.h` に `pl_*` 関数を宣言し、`platforms/osx.mm` に実装を追加（AppKit 型はここか `views/*.mm` にのみ閉じ込め、`.h` には持ち込まない）

**注意**：`Miata.command.*` と `Miata._private.*` は同じ実装が入り得る別の名前空間ではあるが、`Miata.command` テーブル自身の中で Lua 側（`base.lua`）の関数と C++ 側の関数に同じキー名を使ってはいけない。`Application::InitializeImpl()` は `script.Initialize()`（`base.lua` 読み込み）の後に `RegisterFunctions("Miata.command", commands)` を実行するため、同名なら C++ 側が Lua 側を**無言で上書きする**（コンパイルエラーにも起動時エラーにもならず、該当キーを実際に呼び出した時だけ引数不一致などで失敗する）。Lua ラッパー＋その内部で使う生の C++ 実行関数、という組み合わせを作る場合は、`dialog_input`（`Miata.command`）/ `dialog_open`（`Miata._private`）や `make_folder`（`Miata.command`）/ `make_directory`（`Miata.command`だが別名）のように、公開する名前と内部実装の名前を必ず分ける（内部実装は `Miata._private.*` に置くのが基本）。

## ドキュメントの同期

Lua に公開するコマンドやダイアログ種別を追加・変更した場合は、その都度 `README.md` の「Lua スクリプト API」セクション（引数・戻り値・注意点）も更新すること。アーキテクチャに影響する変更（新しいダイアログ種別の追加、`pl_*` の新設、ディレクトリ構成の変更など）があれば、この CLAUDE.md 自体も合わせて更新する。
