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
rake icon             # アプリアイコン(assets/AppIcon.png → resources/AppIcon.icns)を作り直す
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
- `src/views/` — UI レイヤー。`View`（ダイアログキュー管理・ブラウザ操作の起点）、`BrowserView`（左右ペインの `NSSplitView` と、その上に被せるプレビューの覆い、各ペインの下の検索バーを持つコンテナ）、`FileListView`（ファイル一覧本体。`NSScrollView` + 自前 `NSView.drawRect` で描画）、`QuickLookView`（Quick Look のプレビューを載せる覆い）、`SearchBar`（下端の検索バー）と `SearchState`（検索の状態。AppKit 非依存）、`Dialog`（`IDialog`/`DialogPanel` によるダイアログ基盤）
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
| `views/SearchState.h/.cc` | ファイル名の検索の状態（遷移・ヒット・n/N と件数の計算）。AppKit 非依存。1ペイン分で、`FileListView` が持つ（後述「ファイル名の検索」） |
| `views/SearchBar.h/.mm` | 検索しているペインの下に出す検索バー（`NSTextField` の入力欄 + 件数）。ペインごとに1つ、`BrowserView` が持つ（後述） |
| `views/ViewMetrics.h` | 一覧と検索バーが共有するフォント（`MakeFont`）とヘッダーの高さ（`HeaderHeight`）。`.mm` 専用 |
| `FileError.h` | ファイル操作の失敗（`FileError` = OS の説明 + 「権限が無い失敗か」、`FileErrorSummary` = 複数の失敗のまとめ）。権限の失敗に、許可のしかたを案内するために使う（後述「権限エラーの案内」） |
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

- **構造**：`BrowserView::NativeView()` は `_MiataBrowserContainer`（`BrowserView.mm`）で、`NSSplitView`（左右ペイン）と、その上に重ねる覆い（`QuickLookView::NativeView()` = `_MiataQuickLookShield`）、各ペインの下の検索バー（後述「ファイル名の検索」。左右で2つ。覆いより手前に重なる）を子に持つ。覆いの中に `QLPreviewView` を入れる。覆いの位置は、範囲（`constants::QuickLookArea` = `Both`/`Left`/`Right`）に応じてペインの frame から決め、**`NSSplitView` のデリゲート（`splitViewDidResizeSubviews:`）で追従**する（ウィンドウのリサイズもディバイダのドラッグもここに来る）。覆いは不透明に塗る（読み込み中や空のとき、下の一覧が透けないように）。色は一覧と同じ設定の背景色（`Config::Background()`。後述「色の設定」）。
- **範囲と「何を見せるか」は別**：見せるのは常に「カーソルのあるペインのカーソル下のファイル」で、範囲は被せる場所だけを決める。Lua には `"both"`/`"left"`/`"right"`（省略・nil は both）で公開し、変換は `Application.cc` の `ParseQuickLookArea()`（`ParsePane()` に `"both"` を足したもの）に集約している。`ToggleQuickLook(area)` は、同じ範囲なら閉じ、別の範囲なら範囲だけ切り替える（`QLPreviewView` は作り直さない）。`Navigate::Cancel`（Esc）でも閉じる。
- **追従はティック駆動**：カーソル下のファイルは、カーソル移動・ペイン切替・ディレクトリ移動・再読み込み・ソートなど多くの経路で変わり、通知点が 1 つに揃っていない。通知を集めず、`Application::Update()` → `BrowserView::UpdateQuickLook()` が毎ティック `FileListView::CurrentPath()` を見て、直近の値（`quick_look_target_`）とプレビューに渡した値（`quick_look_shown_`）を比べる（`UpdateAutoReload` と同じく、状態を見て判断する方式）。変わってから 0.1 秒（`kQuickLookSettleDelay`）落ち着いたら切り替える。出した時点のファイルは待たずに設定する。`FileListView::GetCurrent()` は一覧が空だと範囲外を読むので、空になり得る場所では `CurrentPath()`（空なら nullopt）を使う。
- **キー入力の安全（実測済み）**：`QLPreviewView` は `acceptsFirstResponder` が YES で、**クリックすると first responder を奪う**（覆い無しで確認済み）。奪われるとキー入力が `MiataRootView` に届かなくなる恐れがあるので、覆いが `hitTest:` で常に自分を返し、クリックを `QLPreviewView` に渡さない（`_MiataFileListNSView` が first responder にならないのと同じ考え方）。代償として、プレビューの中のマウス操作（PDF のスクロール、動画の再生ボタン）と、覆った範囲でのマーク済みファイルのドラッグはできない。一方、`QLPreviewView` は読み込み（`previewItem` の設定）では first responder を取りに来ず（キーウィンドウでも確認済み）、`performKeyEquivalent:` も横取りしなかったので、「奪われたら戻す」処理は入れていない。
- **罠（`QLPreviewView` の `close`、実測。守らないとプロセスが異常終了する）**：`close` は**ウィンドウに載っている間に、1 回だけ**呼ぶ（二重に呼ぶ・ウィンドウから外れた後に呼ぶと、QuickLook の `_QLRaiseAssert` で abort）。`close` した後のビューには**何も触らない**（`previewItem` の設定を含む）。だから `QuickLookView::Hide()` は、外す前に `close` してすぐ手放し、出すたびに `QLPreviewView` を作り直す（1 回 0.5ms 未満）。`shouldCloseWithWindow` は **NO** にする（既定の YES だと、ウィンドウを閉じるときに QuickLook 側も自動で `close` するため、その後の `Hide()` が二重 close になる。ウィンドウを閉じるとアプリは終了するが、その間もタイマーのティックは回る）。
- **テストの罠**：ヘッドレスのハーネスでも、覆いを載せる親はウィンドウに入れておくこと（ウィンドウ外での `close` は abort する）。`QLPreviewView` の `setPreviewItem:`/`close` をメソッド差し替えで記録すると、`close` 後の設定などの違反を検出できる。

## ファイル名の検索（インクリメンタル検索）

`Miata.command.search()` / `search_next()` / `search_prev()` / `search_clear()`（`Application.cc` の `lua_command_search*`）→ `View::BeginSearch` ほか → `BrowserView` → `FileListView`。vim の `/` `n` `N` のように、検索語を入れていくと一致するファイル名へカーソルが飛び、名前の一致部分が強調される。ユーザー向けの仕様は README の「ファイル名の検索」。ここには、設計の決定と罠を書く。

- **3つに分けた**：`SearchState`（AppKit 非依存。1ペイン分の状態遷移 Idle → Typing → Committed と、ヒットからの計算 `FirstHitFrom`/`Step`/`Ordinal`/`RangesFor` だけ。マッチ自体は持たない）、`FileListView`（実際のマッチ `RebuildSearchHits`、カーソルの移動 `JumpCursorTo`、ハイライトの描画 `Draw`。検索状態は `search_`）、`SearchBar`（pimpl。背景のビュー + `NSTextField` 3つ（「/」・入力欄・件数）+ delegate）。バーは**ペインごとに1つ**、`BrowserView` が持ち、入力の始まりと終わり、ペインの状態との整合を担う
- **マッチ**：`NSString` の `rangeOfString:options:range:`（名前は `@(Name().c_str())`）。返る `NSRange` は名前の UTF-16 の添字で、そのまま `NSAttributedString` の属性範囲に使える（UTF-8 との変換が要らない）。スマートケース：語に Unicode の大文字（`uppercaseLetterCharacterSet`）が1文字でもあれば `0`（区別する）、無ければ `NSCaseInsensitiveSearch`。**`NSLiteralSearch` は付けない**（付けると、合成済みの文字と分解された文字（NFC の `が` と `か` + 濁点）が一致しなくなる。`Name()` は NFC だが、検索語は NFD で来ることもある）。範囲の長さは**名前の側**の長さ（検索語の長さと同じとは限らない）。全角/半角・ひらがな/カタカナ・アクセントは畳み込まない（実測）。`FzfFilter` は使えない（外部プロセスを起動し、一致位置を返さず、結果がスコア順）
- **ヒットは `Fetch()` の末尾で作り直す**：`list_` を作り直すのは `Fetch()` だけで（ディレクトリ移動・手動/自動リロード・ソートはすべてここを通る）、ヒットは `list_` の添字なので、そのたびに無効になる。`Draw()` の中では計算しない。検索状態に持てるのは、検索語・位置のパス（`SearchPosition`。パスが見つからないときの代わりに添字も）・ヒットの添字だけ。**`FileEntryView*`/`FileEntryModel*` は持たない**（再スキャンで旧エントリが通知より前に破棄される）。起点（`Anchor`）と戻り先（`Restore`）は、`CursorMemo` と同じく**パス**で持ち、`ResolveRow()` で今の添字にする
- **「ディレクトリ移動」の判定は `reload_memo_` の有無**：購読（`ObservePath`）の中で、`FileListView::Reload()` の間だけ立つ `reload_memo_` が無い通知は移動とみなし、`search_.Clear()` する（カーソルを 0 に戻すのと同じ判定）。`FileListModel::Reload()` を `FileListView::Reload()` を介さずに呼ぶ処理を足すと、再スキャンなのに検索が消える。`TryJumpTo` は、同じパスへでも消す（ルートでの `navigate_left` など。無害）
- **メンバーの宣言順**：`search_` は `subscriptions_` より前に宣言する（購読の通知は `Fetch()` を呼び、`Fetch()` は `search_` を使う。破棄は宣言の逆順なので、購読が先に止まる）
- **遷移**（`SearchState`）：`Begin`（Idle/Committed → Typing。確定済みの語は Esc で戻れるよう取っておく。新しい語は空で、前のヒットは隠す）、`Commit`（Typing → Committed。**語が空なら `Cancel` と同じ**=検索を始める前の状態に戻る）、`Cancel`（Typing → 始める前の状態。カーソルを `Origin`（検索を始めた位置）へ戻すのは `FileListView::CancelSearch`。`Origin` は `Cancel` では消えず、`Clear` で消える）、`Clear`。**語が変わる遷移（`Begin`/`Cancel`/`Clear`、語が空の `Commit`）はヒットを空にする**ので、今の語のヒットが要るなら `RebuildSearchHits()` で作り直す（語がある普通の `Commit` は、語もヒットも変わらない）
- **入力中の動き**：打つたびに `Anchor`（最初は `/` を押した位置）から前方（ラップ）の最初のヒット（`Anchor` 自身を含む）へ。ヒットが無ければ `Anchor` に戻る。`↓`/`↑` は `Step(cursorIndex_)` で、動いた先を新しい `Anchor` にする（Esc の戻り先 `Origin` は動かさない）。**語を全部消したら、`Anchor` を `Origin` に戻す**（検索を始めた位置からやり直す）。`n`/`N`（`Step`）は、カーソルがヒット上でも、ヒットでない行でも、同じ式（カーソルの行そのものは含まない）
- **画面外へ飛ぶときだけ中央寄せ**（`JumpCursorTo`）：`Redraw()` のスクロールは行が見える最小限なので、遠くへ飛ぶと端に張り付く。飛ぶ前に行が見えていたか（`NSContainsRect(visibleRect, ...)`）を調べ、見えていなかったときだけ `scrollPoint:` で中央へ
- **ハイライトの描画**：`NSMutableAttributedString` に `NSBackgroundColorAttributeName` を付けて `drawInRect:`（実測：塗りは文字の行の高さ。20pt の行の中でほぼ中央）。カーソルのある行は `search_current`、ほかは `search_match`。カーソルの下線と同じく、**フォーカスのあるペインだけ** `search_current`。`Draw(min_y, max_y)` は、**描き直す範囲（`drawRect:` の `dirtyRect` の y）にかかる行だけ**描く（以前は全行を描いていた。行ごとに `file_size()` を呼ぶので、数万件で重い。1万件・全件ヒットで、描き直しが 2ms ほど、1打鍵の検索が 10ms ほど）
- **検索バーは、検索しているペインの下だけ**（ペインと同じ幅。反対側のペインには出ず、反対側の一覧も縮まない）：バーは `_MiataBrowserContainer` の子（`searchBars`。ペイン番号が添字）で、`layoutSearchBars` が、ペインの frame（`convertRect:` でコンテナの座標に直す）の下端に重ねる。位置が決まるのは `layoutOverlay` と同じで、`NSSplitView` のデリゲート（`splitViewDidResizeSubviews:`。ウィンドウのリサイズもディバイダのドラッグもここに来る）と、`setSearchBar:visible:` から。分割ビューは元のまま autoresizing で追従する。バーは**覆いより手前**（Quick Look で覆っているときも、バーが見える。覆いはペインの全体を覆うので、バーは覆いの下端に重なる）。バーの分だけ一覧を空けるのは、一覧の側（`FileListView::SetBottomInset` → `_MiataFileListLayoutContainer.footerHeight`。スクロール部分の高さが縮み、カーソルが見える位置までスクロールする）。バーの高さは `HeaderHeight()`
- **バーの出入りは、そのペイン自身の検索の状態だけで決まる**（`SyncSearchBarView`）：`mode != Idle` のあいだ、そのペインの一覧を縮めて、そのバーを出す。中身（検索語・件数）もそのペインのもの。カーソルのペインには依存しないので、`h`/`l` でペインを切り替えても、一覧の高さは変わらず、バーの内容も変わらない。以前は、バーを全幅に1つだけ出し、「どちらかのペインに検索があれば出し続ける」ことで切り替えのガタつきを避けていたが、反対側のペインまで縮み、検索の無いペインにカーソルがあると空の帯が出るので、ペインごとにした
- **罠：入力欄が first responder を持つ間は `MiataRootView::keyDown:` が呼ばれない**（Normal のキーバインドも効かない）。Enter/Esc/↑↓/Tab は delegate の `doCommandBySelector:` で横取りする。Enter/Esc は `dispatch_async` で次のランループへ（確定・取り消しで first responder を手放す処理を、`doCommandBySelector:` の呼び出しの中で行わないため。`FilterListDialog.mm` は、閉じる過程で delegate 自身が破棄されるため、同じく逃がしている）。Tab は握りつぶす（キービューループで入力欄を失う）。**IME の変換中**（`hasMarkedText`）は、検索語にしない（読みの途中の文字を追いかけて、カーソルが飛び回る）。文字を読む `SearchBar::SettledText()` が、入力中で変換中でないときだけ返し、`BrowserView::PullSearchQuery()` がそれを検索語に取り込む（通知の `controlTextDidChange:` と、毎ティックの照合 `ReconcileSearchInput` の両方が、同じ入口を通る。通知の取りこぼしは、ティックが拾う）
- **罠：`DialogPanel::Hide()` は first responder を無条件に `MiataRootView` へ戻す**（`Dialog.mm`）。入力中にダイアログ（ファイル操作の完了など）が閉じると、入力欄が first responder を失うが、バーは入力中のまま、打鍵が Normal のキーバインドへ流れる。対策は2つ：(1) `View::OpenNextDialogIfNeeded` が、**ダイアログを開く直前に**入力中の検索を確定する（`CommitSearchInput`。入力欄を持ったままだと、ダイアログ向けの Enter/Esc を入力欄の delegate が受けてしまう）、(2) 毎ティックの `BrowserView::UpdateSearchBar` が、入力欄と各ペインの状態を突き合わせて整える（`UpdateQuickLook` と同じ「状態を見て判断する」方式）：入力欄が first responder を失っていれば確定する。`controlTextDidEndEditing:` は使わない（毎ティックの照合で足りるうえ、通知の中から `makeFirstResponder:` を呼ぶと再入しうる）。**状態を先に落としてから `makeFirstResponder:` を呼ぶ**（再入しても何もしない）
- **罠：入力欄を first responder にするのは、バーを出した後／バーを隠すのは、手放した後**：隠れたビューは文字を受けられず、first responder を持ったまま隠すとキー入力が beep になる。`BeginSearch` は `SyncSearchBarView()`（バーの出入りと中身だけを合わせる。状態は変えない）→ `BeginInput()` の順。ここで `UpdateSearchBar()`（`ReconcileSearchInput` を含む）を使うと、入力欄がまだ first responder でないので、「フォーカスを失った」と誤って確定する。手放すのは `ReconcileSearchInput`（バーを隠す `SyncSearchBarView` より先）
- **確定後の入力欄は、編集不可・選択不可・`refusesFirstResponder`**、バー全体の `hitTest:` が自分を返す（`_MiataQuickLookShield` と同じ考え方）。クリックで first responder を奪われると、キー入力が `MiataRootView` に届かなくなる。入力中だけ `hitTest:` を通常に戻す（キャレットを動かせるように）
- **罠：フィールドエディタ（`NSTextView`）はウィンドウの全テキスト欄が共有する**：入力中のキャレットの色（`insertionPointColor`）を変えると、次に別の入力欄（リネームなどのダイアログ）が同じエディタを使うときにも残る（実測）。`BeginInput` で元の色を控え、`EndInput` で戻す（入力欄が first responder を奪われた後でも、`[window fieldEditor:NO forObject:nil]` で共有のエディタを取れる）。`stringValue` も、編集が終わるまで欄に取り込まれず、終わると取り込まれて残る（以前は、取り消した語が、検索の無いペインの空の帯に残った）ので、`SearchBar::Update` は、覚えていた値ではなく、**欄の実際の中身と比べる**
- **罠：ビューの `drawRect:` で `dirtyRect` をそのまま塗らない**（`NSIntersectionRect(dirtyRect, self.bounds)` を取る）。macOS 14 以降、ビューは既定で自分の範囲に描画を切り詰めない。親をまるごとビットマップに描く（`cacheDisplayInRect:`）と、`dirtyRect` が子の範囲を超えて渡され、不透明に塗る子（バー）が、隣のビュー（一覧）を塗りつぶす。画面への通常の描画では、dirty 領域が範囲内に収まるので気づきにくい（ヘッドレスの描画テストで発覚した）。`_MiataQuickLookShield::drawRect:` も、以前は `dirtyRect` をそのまま塗っていた（覆いが片側のペインだけのとき、親をまるごと描くと、反対側の一覧まで塗りつぶされることをハーネスで確認した）ので、同じ対策を入れた
- **Esc**：入力中は検索の取り消し（入力欄が受ける。Quick Look は閉じない）。通常時の Esc（`navigate_cancel` → `View::NavigateForBrowser` の `Cancel`）は、プレビューを閉じ、カーソルのペインの検索を消す（`ClearSearch`）。1回で両方
- **`n`/`N` が動けないとき（検索なし・ヒット0件）は beep**（`lua_command_search_next/prev`）。ダイアログの表示中は、検索を始められず（`View::BeginSearch` が false）、`n`/`N` も動かない
- **空の一覧**：検索は始められ（ヒット0）、`GetCurrent()` ではなく `CurrentPath()` と `list_` のサイズで扱う。空ディレクトリで `GetCurrent()` を無ガードで呼ぶ既存の箇所（`View.mm` の `NavigateForBrowser` の `Ok`、`Mark`/`Unmark`/`ToggleMark`）は未解決（未定義動作。検索とは別件）
- **未確認（実機）**：実際の IME の候補ウィンドウ（`setMarkedText:` での変換中の挙動はハーネスで確認済み）、⌘V（メインメニューに Edit が無いので効かないはず。ダイアログの入力欄と同じ）、暗い背景でのキャレットの見え方（`insertionPointColor` は文字の色にしてある）
- **テスト**：`FileListView.mm` を `#include` して、実物の `FileListView` で、ヒット（別実装の答え合わせ）、Unicode（絵文字の UTF-16 位置、NFD/NFC、`É`/`é`）、遷移、再スキャン・ソート・移動、空/全件、画素（ハイライトの位置と色）、スクロール、1万件を検証した。実物の `pl_create_main_window`（`makeKeyAndOrderFront:` を差し替えて画面に出さない）+ `views::View` で、バーのレイアウト・first responder・Enter/Esc/Tab/↑↓・IME（`setMarkedText:`/`insertText:`）・ダイアログとの関係・Quick Look との関係・Lua コマンドを検証した。入力は、本物のフィールドエディタ（`field.currentEditor`）に `insertText:`/`doCommandBySelector:` を送って行う（`controlTextDidChange:` と delegate が、実際の経路で動く）。**罠**：(1) 色の読み戻しは、彩度の高い色ほどずれる（黄 `(1,1,0)` → `(255,252,102)`、水色 → `(131,250,254)`、赤 → `(241,75,45)`）ので、しきい値は実測に合わせる。(2) `#define private public` を使うハーネスでは、対象のヘッダ（`FileListView.h`/`SearchState.h` など）を、その前に include しない（先に読むと `private` のまま）。(3) 親をまるごと描くと `dirtyRect` が範囲を超える（上記）

## ウィンドウ位置・サイズの保存

`platforms/osx.mm` の `pl_create_main_window()` が、`[g_window center]` の**後**に `setFrameAutosaveName:@"MiataMainWindow"` を呼ぶ。AppKit が、移動・リサイズのたびに `NSUserDefaults`（バンドルIDのドメインの `NSWindow Frame MiataMainWindow`）へ自動で保存し、この呼び出しの時点で、保存済みなら復元する(初回は保存が無いので `center` のまま)。終了時にまとめて保存する処理は無い(強制終了でも最後の状態が残る)。

- **`center` を先に呼ぶこと**：保存済みなら復元で上書きされ、無ければ中央のまま出る。順序を逆にすると、毎回中央に戻る。
- **画面外の保存値**：保存された位置がどの画面にも掛からない場合(モニタを外した後など)は、復元の時点でAppKitが画面内へ寄せる(実測。`-20000, 5000` を保存して復元すると画面内に収まった)ので、自前の補正は要らない。
- **復元はViewを作る前**：`pl_create_main_window()` は `InitializeImpl()` の最初に呼ばれ、`View` はその後にcontentViewのサイズを基準にレイアウトされるので、復元後のサイズで始まる。復元で `windowDidResize:` が呼ばれても、リサイズのハンドラ(`pl_set_resize_handler`)はまだ未設定なので何も起きない。
- **保存しているのはウィンドウだけ**：左右ペインの境界(`NSSplitView`)の位置と、各ペインのディレクトリは保存しない(`NSSplitView.autosaveName` は、覆い(プレビュー)の位置追従のデリゲートとの相互作用が未確認なので、入れていない)。
- **テスト**：実物の `pl_create_main_window` を、`makeKeyAndOrderFront:`/`activateIgnoringOtherApps:` を何もしないものに差し替えて(画面に出さず)呼べる。ハーネスの実行ファイル名のドメインに保存されるので、本物のアプリの設定には触れない。起動をまたぐ復元は、プロセスを分けて確かめる。

## アプリアイコン

**元画像は `assets/AppIcon.png`（1024×1024 の正方形の PNG）、変換後が `resources/AppIcon.icns`**。`rake icon`（`Rakefile` の `icon` タスク）が、前者から後者を作る。**両方をコミットする**：`.icns` はビルドに必要で（CMake は `.icns` を作らない）、元画像は次に直すときに必要。元画像を直したら `rake icon` で `.icns` も作り直して、一緒にコミットする（今コミットしてある `.icns` は、`assets/AppIcon.png` から `rake icon` を実行すると、バイト単位で同一のものができる）。

- **元画像を `resources/` に置かない理由**：`CMakeLists.txt` の `file(GLOB_RECURSE RESOURCES "resources/*")` が、`resources/` の全ファイルを `MACOSX_PACKAGE_LOCATION Resources`（= `Contents/Resources/` 直下に平らにコピー）で `.app` に入れるため。1024px の PNG が `.app` に同梱されてしまう。`assets/` は CMake が見ないので入らない。
- **`Info.plist` への登録**：`set_target_properties(app PROPERTIES MACOSX_BUNDLE_ICON_FILE "AppIcon")`（拡張子なしの `AppIcon`）。`resources/AppIcon.icns` は、他の `resources/` のファイルと同じ経路で `.app` に入る。
- **`rake icon` の変換元は固定**（`ICON_SOURCE`）で、引数を取らない。以前の `rake 'icon[x.png]'` の形で呼ばれたときに、引数を黙って無視すると、x.png を変換したつもりで別の画像から `.icns` ができてしまうので、`args.extras` が空でなければエラーにする（引数を宣言すると `rake -T` に擬似的な引数名が出てしまうので、宣言していない）。元画像が無い/正方形でない/画像として読めない場合は、書き込む前にエラーで止まる（既存の `.icns` は壊れない）。`sips` で10サイズ（16〜1024 とその `@2x`）の `.iconset` を作り、`iconutil -c icns` で `.icns` を作る。
- **新しい `resources/` のファイルは cmake の再生成が要る**：`GLOB_RECURSE` は構成時にしか評価されない。`rake build:debug`/`build:release` は cmake を再生成するので、ふつうは気にしなくてよい。直接 `make` するときだけ注意。
- **macOS のアイコンの作法**：角丸の形と余白（1024 の枠に 824 の角丸四角）は画像の側に含める。システムは形を整えない。
- **確認のしかた**：`NSWorkspace iconForFile:` に `.app` のパスを渡すと、システムが返すアイコンが分かる（ビルドしたバンドルで、仮のアイコンが返ることを確認済み）。Dock/Finder のキャッシュで古い絵が残る場合は、この方法で `.app` 側が正しいかを切り分けられる。

## 設定の読み込み（組み込みの既定の設定 + `~/.config/miata/init.lua`）

`Application::InitializeImpl()` → `InitializeScript()`（`Script::Initialize()` で `base.lua` → コマンド登録 → `Config::ScriptInitialize()` → `Script::PostInitialize()`）。`PostInitialize()` が、**組み込みの既定の設定（リソースの `test.lua`）→ ユーザーの設定（`Script::UserConfigFile()` = `pl_get_config_dir()/miata/init.lua`）** の順に、同じ Lua ステートで実行する。後から実行したものが、同じ設定を上書きする(`bind` は同じキーを無言で上書きし、`unbind` で外せる。`Miata.config.*` は後の代入が勝つ)。

- **読み込みは Viewを作る前**：`FileListView` の構築時に、フォントサイズ(ヘッダーの高さ)を読むため、設定はその前に済ませる必要がある。`View` を作った**後**に `ReportConfigErrors()` でエラーのダイアログを出す(`PostInitialize()` はエラーを返すだけで、画面には触らない)。
- **読み込みの直後に、ウィンドウ自体の背景色を反映する**：ウィンドウ(`pl_create_main_window`)は設定を読むより前に作るので、設定の背景色は、読み込んだ後に `pl_set_window_background_color(Config::Background())` で渡す（後述「色の設定」）。
- **`.app` への埋め込みは、既にCMakeの仕組みで成立している**：`resources/*` が `Contents/Resources/` にコピーされ、`pl_read_resource_file()` は `[NSBundle mainBundle] pathForResource:` で、そこだけを読む(ソースツリーを見ない。Release の実行ファイルにソースツリーのパスは残っていない)。`resources/test.lua` を直したら、ビルドし直さないと反映されない。ビルドし直さない設定変更は `init.lua` の役目。
- **エラーの扱い**：`DoFile()`/`DoResourceFile()`/`DoString()` は `std::expected<bool, std::string>` で返し、`PostInitialize()` が `Script::ConfigError { file, message }` の列にして返す。失敗しても起動は続け、エラーの行より前の設定は有効(後は実行されない)。既定の設定が壊れていても、ユーザーの設定は続けて読む。ダイアログは1つにまとめる(`ConfirmDialog`)。キー入力で呼ばれた関数のエラーは、従来どおり標準出力(`KeyDown`)だけ。
- **ファイルは `luaL_loadfile`、リソースは `luaL_loadbuffer(..., "@名前")` で読む**：エラーメッセージに「ファイル名:行番号:」が付く(`luaL_dostring` だと、`[string "…"]` になって行が分かりにくい)。`luaL_loadfile` は、先頭のBOMと `#!` の行を読み飛ばし、開けない/読めないファイル(ディレクトリ、権限なし)をエラーとして返す。ただし、Luaが長いパスを縮める(`...` で始める)ので、メッセージのパスは欠けることがある。`ReportConfigErrors()` は、メッセージにファイルのパスが含まれなければ足す。
- **ユーザーの設定が「無い」の判定は `symlink_status`**：`exists` だと、リンク切れのシンボリックリンク(dotfiles管理でありがち)が「無い」扱いで黙って無視される。`not_found` 以外は読みに行き、読めなければエラーにする。
- **罠：`error({})` のように、文字列でない値を投げる設定**：`lua_tostring` が NULL を返し、そのまま `std::string` にすると未定義動作で落ちる。エラーオブジェクトは `Script.cc` の `ErrorMessage()` を通す(`(error object is a table value)` 等にする)。`InvokeRefFunction`/`InvokeRefFunctionOnThread` のエラー経路も同じ。
- **罠：読み込み中に、Viewを操作するコマンドを呼ぶ**：`view_` はまだ null なので、`app.view_->…` で落ちる。`view_commands[]` と `privates[]` は、`Script::RegisterFunctions(..., wrapper)` の `wrapper` に `Application::lua_view_trampoline` を渡して**クロージャとして登録**し(本来の関数は upvalue(1))、トランポリンが「`view_` が無ければ `luaL_error`、あれば本来の関数へ中継」する。Luaの関数としては同じ名前・同じ引数のままなので、`Miata.command.X` を Lua で再定義して上書きする使い方も壊れない(登録の順序を変えて対処していない理由)。
- **`pl_get_config_dir()`**：`$XDG_CONFIG_HOME` が**絶対パス**のときだけ使い、空や相対パスは無視して `~/.config`(XDG の仕様どおり。相対パスだと、起動した場所で設定の場所が変わってしまう)。GUI(Dock/Finder)から起動したアプリには、シェルの環境変数は渡らないので、ふつうは `~/.config/miata/init.lua`。
- **テスト**：本物の `.app` と同じ構成の「模擬バンドル」(`Fake.app/Contents/{MacOS,Resources}` に、ハーネスの実行ファイルと、ビルド済みの `base.lua`/`test.lua` のコピー)の中から、実物の `Application::InitializeScript()` を呼び、`XDG_CONFIG_HOME` を切り替えて、`init.lua` のパターン(無し/上書き/`unbind`/構文エラー/実行時エラー/`error({})`/読み込み中のViewコマンド/ディレクトリ/権限なし/リンク切れ/BOM/`#!`/空/既定の設定が壊れている…)ごとにプロセスを分けて確かめる。クラッシュするケース(ミュータント)は、シグナルを自分で受けて静かに終了させる(macOS のクラッシュレポートのダイアログを出さないため)。

## 色の設定（`Miata.config.color.*`）

Lua からの代入先は `Config::Color`（`Config.h` の `CONFIG_COLOR_LIST` = `background`/`normal_text`/`normal_file`/`directory`/`search_match`/`search_current`）。**この一覧に足しただけでは何も起きない。描く側が `Config::Color().Get(...)` を読んで初めて効く。** `background` と `normal_text` は、最初のコミット（sokol/ImGui 時代）から一度も読まれておらず、設定しても効かなかった（OS が Light だと、背景は OS の純白のまま、白い `normal_file` の文字は白地に消えた）。色を足すときは、読む箇所まで作り、描画の実測（下の「テスト」）で確かめること。

| 設定 | 使っている所 |
|------|--------------|
| `background` | ペイン本体（`FileListView::Draw` の先頭で全面を塗る）、ヘッダー（`DrawHeader`）、検索バー（`_MiataSearchBarView`）、Quick Look の覆い（`_MiataQuickLookShield`）、ウィンドウ自体（`pl_set_window_background_color`。`InitializeImpl()` が `InitializeScript()` の後に一度呼ぶ） |
| `normal_text` | ヘッダーのパスの文字（`DrawHeader`）、検索バーの文字（「/」・入力欄・件数。件数は alpha 0.8）・キャレット・上端の線（alpha 0.35）（`SearchBar.mm`） |
| `normal_file` / `directory` | 行の文字（`Draw`） |
| `search_match` / `search_current` | 検索で一致した部分の背景（`Draw`。カーソルのある行・フォーカスのあるペインは `search_current`）。既定値は `resources/test.lua`（`Color4f` の既定が不透明な黒なので、`test.lua` に無いと、一致部分が黒く塗られて文字が沈む） |

- **背景の alpha は使わない（常に不透明）**：背景を塗るときは `Color().Get(Background)` を直接使わず、必ず `Config::Background()`（alpha を 1 にして返す）を使う。半透明で塗ると、後ろのOSのテーマの色（Light なら白）が混ざり、OS の設定しだいで見た目が変わる。`pl_set_window_background_color` も不透明に塗る。
- **背景と文字に、OS の色（`windowBackgroundColor`/`textColor` 等）を使わない**：OS のテーマに従い、Light では背景が純白（実測: Light `#ffffff` / Dark `#1e1e1e`）。ヘッダーの文字を OS の色にすると、背景を暗くしたとき黒文字が沈む（なので `normal_text` を読む）。ダイアログ（`Dialog.mm` ほか）は意図して OS の色のまま（`Miata.config.color` は効かない）。
- **ブラウザ領域のビューの `appearance` を、背景の明るさに合わせる**：`BrowserView` のコンテナに `AppearanceForBackground()`（`NSColorUtil.h`。輝度 < 0.5 なら Dark）を設定する。ペインの境目の線（`NSSplitView` のディバイダ）やスクロールバーは OS が描き、Light 用は「黒の薄い線」「明るい帯」なので、暗い背景だと線が消える（実測: OS が Light で背景が黒だと、境目が `#000000` に消えた。Dark 用の外観なら `#212121`）。
- **罠：ウィンドウやアプリ全体の `appearance` は切り替えない**：`pl_get_color`（`osx.mm`）は、描画の外（ダイアログの作成時など）では、`NSApp.appearance`/`window.appearance` を変えても OS の外観のままの色を返す（実測: `NSApp.appearance` を Dark にしても `windowBackgroundColor` は `#ffffff`）。全体を切り替えると、ダイアログの背景（`pl_get_color`）は白いまま、中のコントロールだけが Dark になって壊れる。切り替えるのは、`pl_get_color` を使わないビュー（ブラウザ領域のコンテナ）に限る。タイトルバーとダイアログは OS のテーマのまま。
- **ウィンドウ自体の背景は起動時に一度だけ**（設定は起動時にだけ読むため）。ペインとヘッダーは描画のたびに設定を読む。ウィンドウ自体の背景は、ペインの境目の隙間と、リサイズ中に広がった部分にだけ見える。
- **色の解釈**：`ToNSColor`（`colorWithRed:`）は sRGB（`colorWithSRGBRed:` と同じ。実測）。`"#rrggbb"`/`"#rrggbbaa"` は 16 進のとおりの色になる。
- **未確認（実機）**：従来型スクロールバー（マウス接続時などに常時表示されるタイプ）の見た目（ヘッドレスの描画では描かれない）、タイトルバーとダイアログの見た目。
- **テスト**：実物の `pl_create_main_window` と `views::View` を、画面に出さずに（`makeKeyAndOrderFront:`/`activateIgnoringOtherApps:` を何もしないものに差し替える）ウィンドウごとビットマップに描き（`bitmapImageRepForCachingDisplayInRect:` + `cacheDisplayInRect:toBitmapImageRep:`）、決めた位置の色を測る（`window.appearance` で OS の Light/Dark を模す。PNG にも保存して目で確かめられる）。ペインがウィンドウの背景に頼らず自分で塗れているかは、`pl_set_window_background_color` を呼ばずに測る。起動の配線（設定 → ウィンドウの背景）は、設定読み込みのテストと同じ模擬バンドルの中から、実物の `Application::Initialize()` を呼んで確かめる。**罠**：ウィンドウの描画を `colorAtX:y:` で読み戻すと、中間色の数値がずれる（`#112233` → `#1b2e41`。sRGB を明示した見本でも同じなので測定の癖で、黒と白はずれない）。中間色は許容誤差で比べる。

## 権限エラーの案内（macOS の保護）

ファイル操作・ディレクトリ移動が「権限が無い」で失敗したとき、エラーに加えて、許可のしかた（システム設定の「フルディスクアクセス」）を案内するダイアログを出す。流れ：失敗 → `FileError`（`FileError.h`。`message` + `permission_denied`）→ **`View::ReportFileError(what, error)`** → `permission_denied` なら `YesNoDialog`（OS の説明 + 案内文 `kPermissionGuide` + 「システム設定を開く」/「閉じる」。既定は「閉じる」）→ 「開く」で `pl_open_full_disk_access_settings()`。権限でない失敗は、従来どおりの `ConfirmDialog`（文面は「what: OSの説明」で変わらない）。

- **エラーのダイアログを自前で作らず、必ず `View::ReportFileError` に通す**：ゴミ箱（`DeleteMarked`）、コピー・移動の完了（`OnFileOperationCompleted`）、`make_directory`、`rename_execute`、`reload`、ディレクトリ移動（`View::JumpToOrReport`/`MoveToParentOrReport`）が通している。失敗し得る操作を足したら、ここに通すこと（通さないと、権限の失敗に案内が付かない）。
- **分類（実測）**：`std::filesystem` の権限の失敗は、`EACCES`（13。書き込めない・読めないディレクトリ）と `EPERM`（1。ロック(`uchg`)されたファイル、OS の保護）で、どちらも `permission_denied`（`FileError::From`）。`pl_trash_file`（`trashItemAtURL:`）は `NSCocoaErrorDomain` の 513（`NSFileWriteNoPermissionError`。下位は `NSOSStatusErrorDomain` の -5000 = `afpAccessDenied`）で、存在しないファイルは Cocoa の 4（権限ではない）。`ToFileError`（`osx.mm`）は、Cocoa の 257/513、下位の POSIX の EPERM/EACCES、OSStatus の -5000 のどれかで権限の失敗とみなす。
- **原因は区別できない**：`EPERM` は、OS の保護（プライバシーとセキュリティ）のほかに、SIP（`/System` など）、ロックされたファイル、App 管理でも返る。`EACCES`（アクセス権）と Cocoa の 513 は、OS の保護とアクセス権を区別できない。案内の文面は「止められた場合は…」と書き、アクセス権が原因のときの確認先（Finder の「情報を見る」）も添えている。SIP やロックの判別（`st_flags`）はしていない（README に、許可しても変更できない場所があると書いた）。
- **`FileErrorSummary`**：複数の失敗（コピー・移動、ゴミ箱）をまとめる。**権限の失敗を、後の別の失敗で隠さない**（`Add` の順序に依らず、権限の失敗があれば、その説明を残す）。件数は全部の失敗。
- **罠：`FileListModel::JumpTo` は、読めないと例外を投げる**（`std::filesystem::filesystem_error`）。コードベースに `catch` が一つも無いので、ユーザー操作の移動に使うと、プロセスが `std::terminate` で終了する（実測: `libc++abi: terminating due to uncaught exception ... Permission denied`。権限を拒否した「書類」フォルダなどに入っただけで落ちる）。ユーザー操作は **`TryJumpTo`/`NavigateToParent`**（失敗しても、パス・一覧・マークに触れず、通知もせず、`FileError` を返す）を使う。`JumpTo` は、起動時（ホーム）とテスト専用。`Reload` も、同じ理由でエラーコード版（`Scan`）。
- **`pl_open_full_disk_access_settings()`**：URL `x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles`（System Settings もこの形式を解釈する）を `-[NSWorkspace openURL:]` で開き、開けなければ `com.apple.systempreferences`（System Settings.app）そのものを開く。許可を与えるのはユーザーで、アプリからは変えられない。
- **実機で確認できたこと（ユーザーの報告とOSのログ）**：「システム設定を開く」でシステム設定が開き、Miata にフルディスクアクセスを許可できた（macOS 27.0）。許可はシステムの TCC.db に `kTCCServiceSystemPolicyAllFiles | com.fum1h1ro.miata | 許可` として残り、起動し直した Miata は `Allowed (System Set)` と判定された。許可の条件は `cdhash`（実行ファイルのハッシュ）。
- **未確認**：「システム設定を開く」のURLが、フルディスクアクセスの画面を直接開いたか(開いたことは確認、画面までは未確認)。
- **未解決（保留）：クラウドストレージ（File Provider。`~/Library/CloudStorage/*` の Dropbox など、iCloud Drive）の中のファイルは、普段の起動（Dock/Finder/`open`）の Miata からは、フルディスクアクセスを許可しても削除できない（実測。macOS 27.0、Dropbox。普通のフォルダは問題なし）。ユーザーの判断で保留にした（2026-10-01）。再開するときは、この項目を読んでから始めること。**`trashItemAtURL:` が `Cocoa 513`（権限がない）で失敗する。Dropbox の許可（`kTCCServiceFileProviderDomain`。システム設定では `full`）が許可済みでも、フルディスクアクセスのオン・オフでも変わらない。OS のログでは、書き込みの瞬間に `kernel (Sandbox) sandboxd rejected approval request from Miata for kTCCServiceFileProviderDomain (<パス>): would require prompt` が出る（Dropbox の管理外の `.DS_Store` でも、試験用に作ったファイルでも同じ）。読み取り（一覧、`stat`）は、同じ状態で `granted by TCC` で通る。
  - **差は「許可の対象になるアプリ（responsible process）」**（実測、同じ実行ファイル・同じ署名で比較）：Miata を**ターミナル（WezTerm）から直接起動**すると、アプリ自身のプロセスの中の `trashItemAtURL:` が**成功**する（`sandboxd` が確認するのは、起動元 WezTerm のフルディスクアクセスだけで、Dropbox 側の確認は発生しない）。普段の起動（LaunchServices。起動元は Miata 自身）だと、フルディスクアクセスの確認は `Allowed` なのに、**Dropbox 側の確認が先に出て「プロンプトが要る」として拒否**される。
  - **効かなかったこと**：(1) **別プロセスに任せる**（Miata が `/usr/bin/trash` を子として起動する実装を作ったが、**外した**）：普段の起動の Miata の子の `trash` も、同じ Dropbox 側の確認で拒否された（親が先に書き込みを試みていないまっさらな状態でも。承認の問い合わせは `from trash` として出る）。ターミナルから起動した `trash` が通るのは、起動元がターミナルだから。(2) Info.plist に `NSFileProviderDomainUsageDescription` などの**用途説明**を足す（`.app` のコピーで確認。実行ファイルの `cdhash` は変わらない）：変わらない。(3) Dropbox の許可の付け外し（`tccutil reset`、システム設定のスイッチ）、フルディスクアクセスのオン・オフ：変わらない。
  - **未検証の案（と、保留の理由）**：(a) **安定した署名**。差として確認できたのは「起動元が WezTerm（Developer ID 署名・チーム ID あり）か、Miata（ad-hoc・チーム ID なし）か」だけで、署名が原因とは限らない（hardened runtime、公証、他の許可の有無も違う）。有料の Apple Developer Program には個人では未加入なので、Developer ID は使えない。キーチェーンには、組織（会社）のチームの Apple Development 証明書（有効期限 2027-04）があるが、個人のアプリに使ってよいかはユーザーの判断で、試していない（チーム ID や証明書の名前は、コミットするファイルに書かないこと）。通れば、再ビルドで許可が外れる問題（下の「署名」）も一緒に解決する。(b) **Finder に Apple Events で削除を頼む**（`NSAppleScript`、`tell application "Finder" to delete`。Info.plist の `NSAppleEventsUsageDescription` と、オートメーションの許可が要る。Finder 自身の権限で行われるので、通る可能性がある。未確認）。
  - **当面の回避策（実測）**：Miata を、フルディスクアクセスのあるターミナルから起動する（`rake run:debug`、または `_build/debug/Miata.app/Contents/MacOS/Miata`）。README に書いた。
  - **案内ダイアログは、この場合は効かない**：`kPermissionGuide` はフルディスクアクセスを案内するが、クラウドストレージの中では、許可しても解消しない。文面には反映していない（README に注意を書いた）。リネーム、フォルダの作成、コピー・移動も、普段の起動では、同じ理由で失敗するはず（未確認）。
  - **本物の Miata の権限で、画面を出さずに試す方法（スクラッチで使った）**：試したい処理を入れた dylib を、`DYLD_INSERT_LIBRARIES` で Miata に読み込ませ、コンストラクタ（`main` より前）で実行して結果をファイルに書き、`_exit(0)` する。起動は `open -n -g -W --env DYLD_INSERT_LIBRARIES=... --env 名前=値 Miata.app`（LaunchServices 経由。普段の起動と同じ）、または実行ファイルを直接起動（起動元がターミナルになる）。Miata の実行ファイルは ad-hoc で hardened runtime ではないので、DYLD の環境変数が効く。ウィンドウもキー入力も発生しない。**危ない操作は、試験用に作った空のファイル（名前と大きさで確かめる）にだけ向けること。**
- **罠：許可の対象（responsible process）**：`rake run:*` は、実行ファイル（`Miata.app/Contents/MacOS/Miata`）を、ターミナルから**直接**起動する（`open` を経由しない）。このように起動したプロセスの TCC・サンドボックスの判定は、起動元のアプリ（ターミナル）のものとして行われる（実測：直接起動した Miata の書き込みでは、承認の問い合わせの `Resp:` がターミナル（WezTerm）で、そのフルディスクアクセスで通った）。ターミナルに許可が無いとき、Miata を許可しても効かないか（ターミナルに許可が要るか）は未確認。Miata 自身の許可を試すときは、`open _build/debug/Miata.app` で起動する（Dock/Finder 起動と同じ）。README に書いた。
- **署名**：いまのビルドは `adhoc,linker-signed`（チーム ID なし）。TCC の許可はアプリの署名（識別）に結び付くので、アドホック署名では、ソースを直して作り直すと許可が外れるはず（未確認）。安定した署名（開発者証明書など）で署名すれば、保たれるはず。ユーザーに「ビルドし直すと外れることがある」と README に書いた。
- **テスト**：権限の失敗は、スクラッチの書き込めない（`0555`）・読めない（`0000`）・走査だけできない（`0111`）ディレクトリと、`UF_IMMUTABLE`（`chflags uchg`）で、実際に起こす。**`pl_trash_file` は、必ず失敗するケースだけ**（書き込めないディレクトリの中、存在しないファイル）で呼ぶこと（成功すると、実際にユーザーのゴミ箱へ移る）。**システム設定を開かない**：`-[NSWorkspace openURL:]` を `class_replaceMethod` で、URL を記録するだけのものに差し替える（実物が呼ばれると、ユーザーの画面にシステム設定が出る）。ダイアログの回答は `OnButton(yes_id_)` + `CheckDialogState()`。権限を落としたディレクトリは、後始末で必ず戻す（テストが途中で落ちても消せるよう、実行前に `chmod -R 755` と `chflags -R nouchg` をかける）。

## C++ から Lua へ関数を登録する手順

1. `Application.h` の `Application` クラスに `static int lua_command_XXX(lua_State* L)`（`Miata.command.*` 用）または `static int lua_private_XXX(lua_State* L)`（`Miata._private.*` 用）を追加
2. `Application.cc` の `InitializeScript()` の `view_commands[]`（`Miata.command` 用）または `privates[]`（`Miata._private` 用）に `{ "name", lua_command_XXX }` を追加。Viewに触らず、設定ファイルの読み込み中にも使えるべきもの(`bind`/`unbind` のような)だけ `config_commands[]` に入れる（後述「設定の読み込み」のトランポリン）
3. OS 依存の実装が必要な場合は `platform.h` に `pl_*` 関数を宣言し、`platforms/osx.mm` に実装を追加（AppKit 型はここか `views/*.mm` にのみ閉じ込め、`.h` には持ち込まない）

**注意**：`Miata.command.*` と `Miata._private.*` は同じ実装が入り得る別の名前空間ではあるが、`Miata.command` テーブル自身の中で Lua 側（`base.lua`）の関数と C++ 側の関数に同じキー名を使ってはいけない。`Application::InitializeScript()` は `script.Initialize()`（`base.lua` 読み込み）の後に `RegisterFunctions("Miata.command", ...)` を実行するため、同名なら C++ 側が Lua 側を**無言で上書きする**（コンパイルエラーにも起動時エラーにもならず、該当キーを実際に呼び出した時だけ引数不一致などで失敗する）。Lua ラッパー＋その内部で使う生の C++ 実行関数、という組み合わせを作る場合は、`dialog_input`（`Miata.command`）/ `dialog_open`（`Miata._private`）や `make_folder`（`Miata.command`）/ `make_directory`（`Miata.command`だが別名）のように、公開する名前と内部実装の名前を必ず分ける（内部実装は `Miata._private.*` に置くのが基本）。

## ドキュメントの同期

Lua に公開するコマンドやダイアログ種別を追加・変更した場合は、その都度 `README.md` の「Lua スクリプト API」セクション（引数・戻り値・注意点）も更新すること。アーキテクチャに影響する変更（新しいダイアログ種別の追加、`pl_*` の新設、ディレクトリ構成の変更など）があれば、この CLAUDE.md 自体も合わせて更新する。
