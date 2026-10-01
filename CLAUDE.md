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
rake 'icon[icon.png]' # アプリアイコン(resources/AppIcon.icns)を PNG から作り直す
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
- `src/views/` — UI レイヤー。`View`（ダイアログキュー管理・ブラウザ操作の起点）、`BrowserView`（左右ペインの `NSSplitView` と、その上に被せるプレビューの覆いを持つコンテナ）、`FileListView`（ファイル一覧本体。`NSScrollView` + 自前 `NSView.drawRect` で描画）、`QuickLookView`（Quick Look のプレビューを載せる覆い）、`Dialog`（`IDialog`/`DialogPanel` によるダイアログ基盤）
- `src/widgets/` は存在しない（過去のドキュメントの残骸。汎用ウィジェットは今のところ `views/` 直下に個別実装されている）
- `platforms/` — OS 固有実装（`.mm`）。現状 macOS 用の `osx.mm` と `main.mm` のみ

**主要クラスの役割:**

| ファイル | 役割 |
|---------|------|
| `Application.cc/h` | `pl_start_timer` による 0.05 秒周期の更新処理（`Update()`。旧 `FrameImpl` 相当だが実際のフレームループではない）、キーイベント処理、`Miata.command.*`/`Miata._private.*` のコマンド登録（シングルトン） |
| `Script.cc/h` | Lua VM 管理、C++ 関数登録（`RegisterFunctions`）、設定の読み込み（`PostInitialize`: 組み込みの既定の設定 → ユーザーの `init.lua`。後述）、コルーチン駆動（`InvokeRefFunctionOnThread`/`Update`） |
| `KeyBinding.h` | キーストローク解析、モード別（Normal/Dialog）キーマップ管理 |
| `views/View.h/.mm` | ダイアログキュー管理（`RequestDialog`/`CheckDialogState`）、ブラウザ操作へのキー入力ルーティング |
| `views/Dialog.h/.mm` | ダイアログ基盤。`IDialog`（confirm/yesno/inputtext/custom/filterlist の基底）と `DialogPanel`（実体となる非モーダル NSView オーバーレイ）。詳細は後述 |
| `views/FileListView.h/.mm` | ファイル一覧の描画・スクロール・キーボードカーソル移動（`NSScrollView` + 自前描画）。マーク済みファイルのドラッグ元（`NSDraggingSource`）も兼ねる（後述） |
| `views/QuickLookView.h/.mm` | Quick Look（`QLPreviewView`）のプレビューを一覧の上に被せる覆い。クリックを止めてキー入力を守る（後述） |
| `misc.h` | `Flags`、`ReactiveProperty`、`MessageBroker` などのユーティリティ |
| `platform.h` | OS 依存処理の抽象境界（`pl_*` 関数群の宣言）。色・フォント・ダイアログ用構造体・ファイル操作・ディレクトリ監視（`pl_watch_directory`）・プロセス起動など |
| `platforms/osx.mm` | `platform.h` の macOS 実装。AppKit 型はこの層（と `views/*.mm`）にのみ閉じ込め、ヘッダ（`.h`）には持ち込まない規約 |

**Lua スクリプト:**
- `resources/base.lua` — コアユーティリティとダイアログヘルパー定義
- `resources/test.lua` — **組み込みの既定の設定**（キーバインドとコマンド定義）。`.app` の `Contents/Resources/` に入る
- `~/.config/miata/init.lua` — **ユーザーの設定**。無くてよい。あれば、既定の設定の後に読み込まれて上書きする（後述「設定の読み込み」）

C++ 側は `Miata.command.*`（`Application.cc` の `InitializeScript()` 内の `config_commands[]`/`view_commands[]`）と `Miata._private.*`（同 `privates[]`）の名前空間で Lua 関数を登録し、スクリプト側から呼び出す。ダイアログはコルーチンで非同期制御される（後述）。

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
- **ドロップ後（`EndDrag`）**：宛先（Finder 等）は受理後に非同期で移動を進めるため、0.3 秒待ってから**実ファイルの有無**で判断する。1件でも消えていれば `Reload()` で再スキャン（カーソルは維持され、移されなかったファイルのマークは残る）、そうでなければ `ClearMarks()` のみ。宛先が申告する操作種別（Move/Copy/Generic）は自己申告なので当てにしない。キャンセル/拒否時はマーク維持。宛先の処理が 0.3 秒より遅くても、その後の一覧への反映は「ディレクトリ監視」（後述）が追随する（マークを解除するかどうかの判断だけは、この 0.3 秒の時点で決まる）。
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
- **モデルの `ClearMarks()` はビューに通知しない**（フラグを書き換えるだけ）。マークを解除して画面にも反映したいときは `FileListView::ClearMarks()`／`View::ClearListMarks(model)`（再描画する）を使う。コピー完了後の移動元でモデルの `ClearMarks()` を直接呼んでいたため、解除したマークが次の再描画（カーソル移動など）まで画面に残る不具合があった。
- 失敗（ディレクトリが消えた・権限なし）は `std::expected` で返し、モデルもビューも一切変えない。`directory_iterator` の例外を投げる版は、外部で変更された後に使う再読み込みでは落ちるので使わない（エラーコード版）。

## ディレクトリ監視（自動リロード）

各ペインが表示中のディレクトリを監視し、変化を検知したら `Reload()` する。流れ：`pl_watch_directory`（`platform.h`／`osx.mm`。FSEvents のストリーム、コールバックはメインキュー）→ コールバックは `FileListView::OnDirectoryChanged()` で `stale_`／`reload_due_` を立てるだけ → `Application::Update()` のティックから `View::UpdateAutoReload()` → `FileListView::UpdateAutoReload(allow)` が期限を過ぎていれば `Reload()`。判断（まとめる・保留する・間隔をあける）は `FileListView` 側、OS 依存は `pl_watch_directory` の内側に閉じているので、検知の仕組みだけを差し替えられる。

- **FSEvents を選んだ理由（実測済み）**：ディレクトリの fd に張る kqueue／GCD の vnode ソースは、直下のエントリの追加・削除・改名しか検知できず、**既存ファイルへの追記・更新日時・属性の変更を検知できない**（一覧に出しているサイズ・更新日時が古くなる）。FSEvents（`kFSEventStreamCreateFlagFileEvents`）は両方検知でき、遅延は 12〜20ms 程度。代償は、再帰的なので無関係なイベントも届くこと（`$HOME` で 30 秒に 352 件＝毎秒 12 件ほど、直下の子は 0 件）。届いた path を見て**直下の子だけ**を拾い、それ以外は捨てる（`IsRelevantDirEvent`）。
- **監視先は実パスにそろえる**：FSEvents は常に実パス（`/tmp`→`/private/tmp`、`/var/folders`→`/private/var/folders`）で報告するので、`std::filesystem::canonical` した path を渡し、それと比べる。報告された path が想定の形（監視先の配下）でないときは、見逃すよりは変化ありとみなして通知する（安全側）。
- **イベントの取りこぼしや、何が変わったか分からない場合も通知する**：`MustScanSubDirs`／`UserDropped`／`KernelDropped`／`RootChanged`（監視先自身の移動・削除・再作成）／`Mount`／`Unmount`。監視はパス基準なので、監視先が削除されて同じパスに再作成されても、張り直さずに届き続ける。
- **フラグで絞り込まない**：FSEvents のフラグは path ごとに**累積**する（古いファイルへの `setxattr` だけの変更にも `Created|Modified` が付いて届く）ので、「拡張属性だけの変更は無視」のような絞り込みは信用できない。直下の子への変更なら何でも拾う。
- **`FSEventStreamContext` の retain/release に `CFRetain`／`CFRelease` を渡す**（`info` は ObjC オブジェクト `MiataDirWatchState`）。ストリームが解放されるまで `info` が生きる。破棄は `Stop`→`Invalidate`→`Release` の順。`alive` フラグは念のための保護（`Stop`／`Invalidate` の後に FSEvents がコールバックを呼ばないことは実測で確認している）。
- **コールバックの中で `Reload()` しない**：コールバックはフラグを立てるだけ。再スキャンはティック（`UpdateAutoReload`）で行う（監視のコールバックの中から監視自体を破棄することになるのを避け、続く変化を 1 回にまとめるため）。
- **監視は、表示するパスが変わったときだけ張り直す**：`WatchDirectory()` はパス変更の購読側（＝JumpTo／Reload の直後）から呼ばれるが、同じパスを見ている監視が生きていれば何もしない。張り直すと、その間の変更（特に、更新日時に現れない既存ファイルの中身の更新）を取りこぼすため。ストリームは再スキャンの間も生かし続ける。**新しく張るとき**（起動時・ディレクトリの移動時）の、走査から張るまでの隙間の変更は、`FileListModel::ScannedMtime()`（走査の**直前**に取ったディレクトリの更新日時）と現在の更新日時を比べて、食い違っていればもう一度反映待ちにする（追加・削除・改名を確認できる）。
- **自分の走査は、イベントを起こさない**（読むだけ）ので、自動リロードが連鎖することはない。一方、自分の操作（mkdir／rename／コピー・移動など）や、走査が完了する前に届いたイベントの分は、走査の後に届いて、約 0.3 秒後にもう 1 回リロードされることがある（内容は同じで、無害）。
- **ダイアログ表示中は保留する**：`View::UpdateAutoReload()` が `!IsAnyDialogOpened()` を渡す。リネームの入力中に外部でそのファイルが消えると、反映によってカーソルが隣のファイルへ動き、確定時の `rename_execute`（カーソル位置のエントリを改名する）が**別のファイルを改名してしまう**ため。`Application::Update()` では `Script::Update()`（コルーチンがダイアログの結果を受けて `rename_execute` などを呼ぶ）と `CheckDialogState()` の**後**に `UpdateAutoReload()` を呼ぶこと。
- **待ちとスロットル**（`FileListView.mm` の定数）：検知から 300ms 待って反映し、自動リロードどうしは最短 500ms（走査に時間がかかるときは、かかった時間の 8 倍）あける。失敗（ディレクトリが消えた等）は再試行しない（次のイベントか、手動のリロードを待つ）。
- **テストの罠**：`rxcpp` の subscription はスコープを抜けても購読解除されない。ローカル変数を参照するラムダを `ObservePath().subscribe` に渡したら、変数の寿命が切れる前に `unsubscribe()` すること（アプリ側は `misc::SubscriptionGuard`）。

## プレビュー（Quick Look）

`Miata.command.quick_look([pane])` → `Application::lua_command_quick_look` → `View::ToggleQuickLook` → `BrowserView::ToggleQuickLook`。カーソル下のファイルを `QLPreviewView`（QuickLookUI）で、一覧の上に被せて表示する。別ウィンドウの `QLPreviewPanel`（旧 `pl_quick_preview`）は使わない（パネルはウィンドウで、ビューにできないため。旧実装は削除済み）。

- **構造**：`BrowserView::NativeView()` は `_MiataBrowserContainer`（`BrowserView.mm`）で、`NSSplitView`（左右ペイン）と、その上に重ねる覆い（`QuickLookView::NativeView()` = `_MiataQuickLookShield`）を子に持つ。覆いの中に `QLPreviewView` を入れる。覆いの位置は、範囲（`constants::QuickLookArea` = `Both`/`Left`/`Right`）に応じてペインの frame から決め、**`NSSplitView` のデリゲート（`splitViewDidResizeSubviews:`）で追従**する（ウィンドウのリサイズもディバイダのドラッグもここに来る）。覆いは不透明に塗る（読み込み中や空のとき、下の一覧が透けないように）。
- **範囲と「何を見せるか」は別**：見せるのは常に「カーソルのあるペインのカーソル下のファイル」で、範囲は被せる場所だけを決める。Lua には `"both"`/`"left"`/`"right"`（省略・nil は both）で公開し、変換は `Application.cc` の `ParseQuickLookArea()`（`ParsePane()` に `"both"` を足したもの）に集約している。`ToggleQuickLook(area)` は、同じ範囲なら閉じ、別の範囲なら範囲だけ切り替える（`QLPreviewView` は作り直さない）。`Navigate::Cancel`（Esc）でも閉じる。
- **追従はティック駆動**：カーソル下のファイルは、カーソル移動・ペイン切替・ディレクトリ移動・再読み込み・ソートなど多くの経路で変わり、通知点が 1 つに揃っていない。通知を集めず、`Application::Update()` → `BrowserView::UpdateQuickLook()` が毎ティック `FileListView::CurrentPath()` を見て、直近の値（`quick_look_target_`）とプレビューに渡した値（`quick_look_shown_`）を比べる（`UpdateAutoReload` と同じく、状態を見て判断する方式）。変わってから 0.1 秒（`kQuickLookSettleDelay`）落ち着いたら切り替える。出した時点のファイルは待たずに設定する。`FileListView::GetCurrent()` は一覧が空だと範囲外を読むので、空になり得る場所では `CurrentPath()`（空なら nullopt）を使う。
- **キー入力の安全（実測済み）**：`QLPreviewView` は `acceptsFirstResponder` が YES で、**クリックすると first responder を奪う**（覆い無しで確認済み）。奪われるとキー入力が `MiataRootView` に届かなくなる恐れがあるので、覆いが `hitTest:` で常に自分を返し、クリックを `QLPreviewView` に渡さない（`_MiataFileListNSView` が first responder にならないのと同じ考え方）。代償として、プレビューの中のマウス操作（PDF のスクロール、動画の再生ボタン）と、覆った範囲でのマーク済みファイルのドラッグはできない。一方、`QLPreviewView` は読み込み（`previewItem` の設定）では first responder を取りに来ず（キーウィンドウでも確認済み）、`performKeyEquivalent:` も横取りしなかったので、「奪われたら戻す」処理は入れていない。
- **罠（`QLPreviewView` の `close`、実測。守らないとプロセスが異常終了する）**：`close` は**ウィンドウに載っている間に、1 回だけ**呼ぶ（二重に呼ぶ・ウィンドウから外れた後に呼ぶと、QuickLook の `_QLRaiseAssert` で abort）。`close` した後のビューには**何も触らない**（`previewItem` の設定を含む）。だから `QuickLookView::Hide()` は、外す前に `close` してすぐ手放し、出すたびに `QLPreviewView` を作り直す（1 回 0.5ms 未満）。`shouldCloseWithWindow` は **NO** にする（既定の YES だと、ウィンドウを閉じるときに QuickLook 側も自動で `close` するため、その後の `Hide()` が二重 close になる。ウィンドウを閉じるとアプリは終了するが、その間もタイマーのティックは回る）。
- **テストの罠**：ヘッドレスのハーネスでも、覆いを載せる親はウィンドウに入れておくこと（ウィンドウ外での `close` は abort する）。`QLPreviewView` の `setPreviewItem:`/`close` をメソッド差し替えで記録すると、`close` 後の設定などの違反を検出できる。

## ウィンドウ位置・サイズの保存

`platforms/osx.mm` の `pl_create_main_window()` が、`[g_window center]` の**後**に `setFrameAutosaveName:@"MiataMainWindow"` を呼ぶ。AppKit が、移動・リサイズのたびに `NSUserDefaults`（バンドルIDのドメインの `NSWindow Frame MiataMainWindow`）へ自動で保存し、この呼び出しの時点で、保存済みなら復元する(初回は保存が無いので `center` のまま)。終了時にまとめて保存する処理は無い(強制終了でも最後の状態が残る)。

- **`center` を先に呼ぶこと**：保存済みなら復元で上書きされ、無ければ中央のまま出る。順序を逆にすると、毎回中央に戻る。
- **画面外の保存値**：保存された位置がどの画面にも掛からない場合(モニタを外した後など)は、復元の時点でAppKitが画面内へ寄せる(実測。`-20000, 5000` を保存して復元すると画面内に収まった)ので、自前の補正は要らない。
- **復元はViewを作る前**：`pl_create_main_window()` は `InitializeImpl()` の最初に呼ばれ、`View` はその後にcontentViewのサイズを基準にレイアウトされるので、復元後のサイズで始まる。復元で `windowDidResize:` が呼ばれても、リサイズのハンドラ(`pl_set_resize_handler`)はまだ未設定なので何も起きない。
- **保存しているのはウィンドウだけ**：左右ペインの境界(`NSSplitView`)の位置と、各ペインのディレクトリは保存しない(`NSSplitView.autosaveName` は、覆い(プレビュー)の位置追従のデリゲートとの相互作用が未確認なので、入れていない)。
- **テスト**：実物の `pl_create_main_window` を、`makeKeyAndOrderFront:`/`activateIgnoringOtherApps:` を何もしないものに差し替えて(画面に出さず)呼べる。ハーネスの実行ファイル名のドメインに保存されるので、本物のアプリの設定には触れない。起動をまたぐ復元は、プロセスを分けて確かめる。

## アプリアイコン

`resources/AppIcon.icns` が `.app` のアイコン。`CMakeLists.txt` の `file(GLOB_RECURSE RESOURCES "resources/*")` が、`resources/` の全ファイルを `MACOSX_PACKAGE_LOCATION Resources`（= `Contents/Resources/` 直下に平らにコピー）で `.app` に入れるので、アイコンも同じ経路で入る。`Info.plist` の `CFBundleIconFile` は、`set_target_properties(app PROPERTIES MACOSX_BUNDLE_ICON_FILE "AppIcon")` で登録している（拡張子なしの `AppIcon`）。

- **差し替え**：`rake 'icon[path/to.png]'`（`Rakefile` の `icon` タスク）が、正方形の PNG から `sips` で10サイズ(16〜1024 とその `@2x`)の `.iconset` を作り、`iconutil -c icns` で `resources/AppIcon.icns` を作る。入力が正方形でない/読めない/無い場合は、書き込む前にエラーで止まる(既存の `.icns` は壊れない)。今の `.icns` は、このタスクで作った仮のアイコン。
- **新しい `resources/` のファイルは cmake の再生成が要る**：`GLOB_RECURSE` は構成時にしか評価されない。`rake build:debug`/`build:release` は cmake を再生成するので、ふつうは気にしなくてよい。直接 `make` するときだけ注意。
- **macOS のアイコンの作法**：角丸の形と余白(1024 の枠に 824 の角丸四角)は画像の側に含める。システムは形を整えない。
- **確認のしかた**：`NSWorkspace iconForFile:` に `.app` のパスを渡すと、システムが返すアイコンが分かる(ビルドしたバンドルで、仮のアイコンが返ることを確認済み)。Dock/Finder のキャッシュで古い絵が残る場合は、この方法で `.app` 側が正しいかを切り分けられる。

## 設定の読み込み（組み込みの既定の設定 + `~/.config/miata/init.lua`）

`Application::InitializeImpl()` → `InitializeScript()`（`Script::Initialize()` で `base.lua` → コマンド登録 → `Config::ScriptInitialize()` → `Script::PostInitialize()`）。`PostInitialize()` が、**組み込みの既定の設定（リソースの `test.lua`）→ ユーザーの設定（`Script::UserConfigFile()` = `pl_get_config_dir()/miata/init.lua`）** の順に、同じ Lua ステートで実行する。後から実行したものが、同じ設定を上書きする(`bind` は同じキーを無言で上書きし、`unbind` で外せる。`Miata.config.*` は後の代入が勝つ)。

- **読み込みは Viewを作る前**：`FileListView` の構築時に、フォントサイズ(ヘッダーの高さ)を読むため、設定はその前に済ませる必要がある。`View` を作った**後**に `ReportConfigErrors()` でエラーのダイアログを出す(`PostInitialize()` はエラーを返すだけで、画面には触らない)。
- **`.app` への埋め込みは、既にCMakeの仕組みで成立している**：`resources/*` が `Contents/Resources/` にコピーされ、`pl_read_resource_file()` は `[NSBundle mainBundle] pathForResource:` で、そこだけを読む(ソースツリーを見ない。Release の実行ファイルにソースツリーのパスは残っていない)。`resources/test.lua` を直したら、ビルドし直さないと反映されない。ビルドし直さない設定変更は `init.lua` の役目。
- **エラーの扱い**：`DoFile()`/`DoResourceFile()`/`DoString()` は `std::expected<bool, std::string>` で返し、`PostInitialize()` が `Script::ConfigError { file, message }` の列にして返す。失敗しても起動は続け、エラーの行より前の設定は有効(後は実行されない)。既定の設定が壊れていても、ユーザーの設定は続けて読む。ダイアログは1つにまとめる(`ConfirmDialog`)。キー入力で呼ばれた関数のエラーは、従来どおり標準出力(`KeyDown`)だけ。
- **ファイルは `luaL_loadfile`、リソースは `luaL_loadbuffer(..., "@名前")` で読む**：エラーメッセージに「ファイル名:行番号:」が付く(`luaL_dostring` だと、`[string "…"]` になって行が分かりにくい)。`luaL_loadfile` は、先頭のBOMと `#!` の行を読み飛ばし、開けない/読めないファイル(ディレクトリ、権限なし)をエラーとして返す。ただし、Luaが長いパスを縮める(`...` で始める)ので、メッセージのパスは欠けることがある。`ReportConfigErrors()` は、メッセージにファイルのパスが含まれなければ足す。
- **ユーザーの設定が「無い」の判定は `symlink_status`**：`exists` だと、リンク切れのシンボリックリンク(dotfiles管理でありがち)が「無い」扱いで黙って無視される。`not_found` 以外は読みに行き、読めなければエラーにする。
- **罠：`error({})` のように、文字列でない値を投げる設定**：`lua_tostring` が NULL を返し、そのまま `std::string` にすると未定義動作で落ちる。エラーオブジェクトは `Script.cc` の `ErrorMessage()` を通す(`(error object is a table value)` 等にする)。`InvokeRefFunction`/`InvokeRefFunctionOnThread` のエラー経路も同じ。
- **罠：読み込み中に、Viewを操作するコマンドを呼ぶ**：`view_` はまだ null なので、`app.view_->…` で落ちる。`view_commands[]` と `privates[]` は、`Script::RegisterFunctions(..., wrapper)` の `wrapper` に `Application::lua_view_trampoline` を渡して**クロージャとして登録**し(本来の関数は upvalue(1))、トランポリンが「`view_` が無ければ `luaL_error`、あれば本来の関数へ中継」する。Luaの関数としては同じ名前・同じ引数のままなので、`Miata.command.X` を Lua で再定義して上書きする使い方も壊れない(登録の順序を変えて対処していない理由)。
- **`pl_get_config_dir()`**：`$XDG_CONFIG_HOME` が**絶対パス**のときだけ使い、空や相対パスは無視して `~/.config`(XDG の仕様どおり。相対パスだと、起動した場所で設定の場所が変わってしまう)。GUI(Dock/Finder)から起動したアプリには、シェルの環境変数は渡らないので、ふつうは `~/.config/miata/init.lua`。
- **テスト**：本物の `.app` と同じ構成の「模擬バンドル」(`Fake.app/Contents/{MacOS,Resources}` に、ハーネスの実行ファイルと、ビルド済みの `base.lua`/`test.lua` のコピー)の中から、実物の `Application::InitializeScript()` を呼び、`XDG_CONFIG_HOME` を切り替えて、`init.lua` のパターン(無し/上書き/`unbind`/構文エラー/実行時エラー/`error({})`/読み込み中のViewコマンド/ディレクトリ/権限なし/リンク切れ/BOM/`#!`/空/既定の設定が壊れている…)ごとにプロセスを分けて確かめる。クラッシュするケース(ミュータント)は、シグナルを自分で受けて静かに終了させる(macOS のクラッシュレポートのダイアログを出さないため)。

## C++ から Lua へ関数を登録する手順

1. `Application.h` の `Application` クラスに `static int lua_command_XXX(lua_State* L)`（`Miata.command.*` 用）または `static int lua_private_XXX(lua_State* L)`（`Miata._private.*` 用）を追加
2. `Application.cc` の `InitializeScript()` の `view_commands[]`（`Miata.command` 用）または `privates[]`（`Miata._private` 用）に `{ "name", lua_command_XXX }` を追加。Viewに触らず、設定ファイルの読み込み中にも使えるべきもの(`bind`/`unbind` のような)だけ `config_commands[]` に入れる（後述「設定の読み込み」のトランポリン）
3. OS 依存の実装が必要な場合は `platform.h` に `pl_*` 関数を宣言し、`platforms/osx.mm` に実装を追加（AppKit 型はここか `views/*.mm` にのみ閉じ込め、`.h` には持ち込まない）

**注意**：`Miata.command.*` と `Miata._private.*` は同じ実装が入り得る別の名前空間ではあるが、`Miata.command` テーブル自身の中で Lua 側（`base.lua`）の関数と C++ 側の関数に同じキー名を使ってはいけない。`Application::InitializeScript()` は `script.Initialize()`（`base.lua` 読み込み）の後に `RegisterFunctions("Miata.command", ...)` を実行するため、同名なら C++ 側が Lua 側を**無言で上書きする**（コンパイルエラーにも起動時エラーにもならず、該当キーを実際に呼び出した時だけ引数不一致などで失敗する）。Lua ラッパー＋その内部で使う生の C++ 実行関数、という組み合わせを作る場合は、`dialog_input`（`Miata.command`）/ `dialog_open`（`Miata._private`）や `make_folder`（`Miata.command`）/ `make_directory`（`Miata.command`だが別名）のように、公開する名前と内部実装の名前を必ず分ける（内部実装は `Miata._private.*` に置くのが基本）。

## ドキュメントの同期

Lua に公開するコマンドやダイアログ種別を追加・変更した場合は、その都度 `README.md` の「Lua スクリプト API」セクション（引数・戻り値・注意点）も更新すること。アーキテクチャに影響する変更（新しいダイアログ種別の追加、`pl_*` の新設、ディレクトリ構成の変更など）があれば、この CLAUDE.md 自体も合わせて更新する。
