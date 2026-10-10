# 設定（読み込み・キーバインド・色） — テスト・実測・未確認の記録

> 自動では読み込まれない。テストを書く・直す・検証するとき、実機で確かめることを探すときに読む。
> 設計の決定と罠は `.claude/rules/config.md`。

## 設定の読み込み（組み込みの既定の設定 + `~/.config/miata/init.lua`）

- **テスト**：本物の `.app` と同じ構成の「模擬バンドル」(`Fake.app/Contents/{MacOS,Resources}` に、ハーネスの実行ファイルと、ビルド済みの `base.lua`/`test.lua` のコピー)の中から、実物の `Application::InitializeScript()` を呼び、`XDG_CONFIG_HOME` を切り替えて、`init.lua` のパターン(無し/上書き/`unbind`/構文エラー/実行時エラー/`error({})`/読み込み中のViewコマンド/ディレクトリ/権限なし/リンク切れ/BOM/`#!`/空/既定の設定が壊れている…)ごとにプロセスを分けて確かめる。クラッシュするケース(ミュータント)は、シグナルを自分で受けて静かに終了させる(macOS のクラッシュレポートのダイアログを出さないため)。

## キーバインド（`bind` / `unbind`）

- **テスト**（リポジトリ外のスクラッチ）：実物の `Application::Initialize()` を模擬 `.app` から起動し、`Has` で各モードの割り当てを読み、`lua_rawlen(LUA_REGISTRYINDEX)` で参照の増減を数える（`luaL_unref` は、解放した番号を空きリストに繋ぐだけで、配列の長さは縮まない。「増え続けない」「次の `bind` が再利用する」で確かめる）。11 シナリオ（旧実装では 8 つが失敗する）。実装を1か所ずつ壊す変異 8 件は、全て検出した。`resources/test.lua` の矢印を `"nd"` で1回 `bind` する形で、`unbind` した後の矢印が壊れないこと（選択リストのテストの `unbind_arrow`）で、元の問題が直ったことを確かめた

## 色の設定（`Miata.config.color.*`）

- **未確認（実機）**：従来型スクロールバー（マウス接続時などに常時表示されるタイプ）の見た目（ヘッドレスの描画では描かれない）、タイトルバーとダイアログの見た目。
- **テスト**：実物の `pl_create_main_window` と `views::View` を、画面に出さずに（`makeKeyAndOrderFront:`/`activateIgnoringOtherApps:` を何もしないものに差し替える）ウィンドウごとビットマップに描き（`bitmapImageRepForCachingDisplayInRect:` + `cacheDisplayInRect:toBitmapImageRep:`）、決めた位置の色を測る（`window.appearance` で OS の Light/Dark を模す。PNG にも保存して目で確かめられる）。ペインがウィンドウの背景に頼らず自分で塗れているかは、`pl_set_window_background_color` を呼ばずに測る。起動の配線（設定 → ウィンドウの背景）は、設定読み込みのテストと同じ模擬バンドルの中から、実物の `Application::Initialize()` を呼んで確かめる。**罠**：ウィンドウの描画を `colorAtX:y:` で読み戻すと、中間色の数値がずれる（`#112233` → `#1b2e41`。sRGB を明示した見本でも同じなので測定の癖で、黒と白はずれない）。中間色は許容誤差で比べる。
