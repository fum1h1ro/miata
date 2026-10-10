# 設定（読み込み・キーバインド・色・その他の設定） — テスト・実測・未確認の記録

> 自動では読み込まれない。テストを書く・直す・検証するとき、実機で確かめることを探すときに読む。
> 設計の決定と罠は `.claude/rules/config.md`。

## 設定の読み込み（組み込みの既定の設定 + `~/.config/miata/init.lua`）

- **テスト**：本物の `.app` と同じ構成の「模擬バンドル」(`Fake.app/Contents/{MacOS,Resources}` に、ハーネスの実行ファイルと、ビルド済みの `base.lua`/`test.lua` のコピー)の中から、実物の `Application::InitializeScript()` を呼び、`XDG_CONFIG_HOME` を切り替えて、`init.lua` のパターン(無し/上書き/`unbind`/構文エラー/実行時エラー/`error({})`/読み込み中のViewコマンド/ディレクトリ/権限なし/リンク切れ/BOM/`#!`/空/既定の設定が壊れている…)ごとにプロセスを分けて確かめる。クラッシュするケース(ミュータント)は、シグナルを自分で受けて静かに終了させる(macOS のクラッシュレポートのダイアログを出さないため)。

## キーバインド（`bind` / `unbind`）

- **テスト**（リポジトリ外のスクラッチ）：実物の `Application::Initialize()` を模擬 `.app` から起動し、`Has` で各モードの割り当てを読み、`lua_rawlen(LUA_REGISTRYINDEX)` で参照の増減を数える（`luaL_unref` は、解放した番号を空きリストに繋ぐだけで、配列の長さは縮まない。「増え続けない」「次の `bind` が再利用する」で確かめる）。11 シナリオ（旧実装では 8 つが失敗する）。実装を1か所ずつ壊す変異 8 件は、全て検出した。`resources/test.lua` の矢印を `"nd"` で1回 `bind` する形で、`unbind` した後の矢印が壊れないこと（選択リストのテストの `unbind_arrow`）で、元の問題が直ったことを確かめた

## 色の設定（`Miata.config.color.*`）

- **未確認（実機）**：従来型スクロールバー（マウス接続時などに常時表示されるタイプ）の見た目（ヘッドレスの描画では描かれない）、タイトルバーとダイアログの見た目。
- **テスト**：実物の `pl_create_main_window` と `views::View` を、画面に出さずに（`makeKeyAndOrderFront:`/`activateIgnoringOtherApps:` を何もしないものに差し替える）ウィンドウごとビットマップに描き（`bitmapImageRepForCachingDisplayInRect:` + `cacheDisplayInRect:toBitmapImageRep:`）、決めた位置の色を測る（`window.appearance` で OS の Light/Dark を模す。PNG にも保存して目で確かめられる）。ペインがウィンドウの背景に頼らず自分で塗れているかは、`pl_set_window_background_color` を呼ばずに測る。起動の配線（設定 → ウィンドウの背景）は、設定読み込みのテストと同じ模擬バンドルの中から、実物の `Application::Initialize()` を呼んで確かめる。**罠**：ウィンドウの描画を `colorAtX:y:` で読み戻すと、中間色の数値がずれる（`#112233` → `#1b2e41`。sRGB を明示した見本でも同じなので測定の癖で、黒と白はずれない）。中間色は許容誤差で比べる。

## その他の設定（`Miata.config.set_*`）と `toggle_icons`

- **未確認（実機）**：ユーザーの実機（`~/.config/miata/init.lua`・Osaka 20pt）での、`.` キーでの切り替えと、`set_show_icons(false)` の起動。`.` を、すでに別の割り当てに使っている `init.lua` では、後から読むユーザーの設定が勝つ（上書きされる）。
- **テスト**（リポジトリ外のスクラッチ。`icon_app_test`。模擬の `.app`（`Fake.app/Contents/{MacOS,Resources}` + `Info.plist`）から、1 回の起動で 1 シナリオ）：`defaults`（設定が無いと出す・エラーのダイアログが無い）、`config`（`init.lua` に `set_show_icons` を書いた結果。`false` / `true` は値が変わりエラーなし。`0` / `"true"` / `nil` / 引数なし / `{}` は、エラーの文面に `set_show_icons: expected a boolean` が含まれ、値は変わらない）、`toggle`（`Application::KeyDown(kVK_ANSI_Period)` と Lua の `Miata.command.toggle_icons()` の戻り値（切り替えた後の状態）・値が変わること・両ペインの一覧に `setNeedsDisplay:` が呼ばれること・描画でアイコンが出る/消えること。読み込み後の `Miata.config.set_show_icons(false)` と型の検査）、`toggle_load`（設定の読み込み中に `toggle_icons` を呼ぶと、落ちずに「画面がまだ無い」エラーになり、値は変わらない）、`render`（実ウィンドウを Light/Dark の PNG に描く。4 通り=system 12pt / Osaka 20pt × 入 / 切）。変異 8 件（型の検査を外す・再描画を呼ばない・戻り値を逆にする・既定を切にする・`.` の割り当てを消す・コマンドを登録しない・値を書き換えない・設定の関数を登録しない）は、全て検出した
- **罠**：(1) **`cacheDisplayInRect:` は、描き直しの要求（`needsDisplay`）の有無に関係なく、全部描く**ので、「切り替えた後に `Redraw()` を呼び忘れた」ことは、描画の結果では見えない（変異「再描画を呼ばない」が最初は生き残った）。(2) **画面に出していないウィンドウでは、`setNeedsDisplay:YES` を呼んでも `needsDisplay` が立たない**（実測: 呼んだ直後の `needsDisplay` が 0）ので、`-[NSView setNeedsDisplay:]` を `method_setImplementation` で差し替えて、呼び出し（ビューと引数）を記録し、両ペインの一覧のビュー（`NSScrollView` の `documentView`）に `YES` で呼ばれたことを見る。(3) 模擬のバンドルに `Info.plist`（`CFBundleIdentifier`）を置くと、`NSUserDefaults` のドメインはバンドル ID になる（保存されたペインの状態・ウィンドウの位置は、毎回 `defaults delete <バンドル ID>` で消す）。(4) `pl_create_main_window` が呼ぶ `makeKeyAndOrderFront:` と `activateIgnoringOtherApps:` は、何もしないものに差し替えて、画面を奪わない
