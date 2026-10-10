---
paths:
  - "src/Config.*"
  - "src/Script.*"
  - "src/KeyBinding.*"
  - "resources/test.lua"
---

# 設定（読み込み・キーバインド・色・その他の設定）

> 機能別の設計メモ。上の `paths:` のファイルを触ると自動で読み込まれる。`Application.cc` など複数の機能が同居するファイルでは載らないので、そのときは CLAUDE.md の「設計メモの索引」から、該当するファイルを自分で読むこと。
> テスト・実測・実機未確認の記録は `.claude/notes/config.md`（自動では読み込まれない。テストを書く・直す・検証するときに読む）。

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

## キーバインド（`bind` / `unbind`）

`Miata.command.bind(mode, keys, fn)` / `unbind(mode, keys)`（`Application.cc` の `lua_command_bind` / `lua_command_unbind`）。ユーザー向けの仕様は README の「キーバインド」。ここには、設計の決定と罠を書く。

- **モードごとに別々の Lua の参照を作る**（`"nd"` なら2つ）：`fn` は `luaL_ref` で参照にして、キーマップ（Normal / Dialog）に登録する。以前は1つの参照を両モードで共有していたので、`unbind("n", "<down>")` が、Dialog 側がまだ使っている参照を解放し、その参照を `luaL_ref` が再利用した別の関数が、Dialog の `<down>` で呼ばれた。`unbind("nd", …)` は同じ参照を2回解放し、Lua の空きリストが壊れて、続く2回の `bind` が同じ参照になった（先に `bind` したキーが、後の関数を実行する）。`resources/test.lua` の `j` `k` `h` `l` `<enter>` `<esc>` と矢印の `"nd"` が、この影響を受けていた
- **同じキーの上書きは、先に外す**：`Has` で既存の割り当てを調べ、あれば `Unregister` して、その参照を解放してから `Register` する。以前は、上書きされた関数の参照が漏れ（200回の `bind` で参照が200増えた）、キー列の経路の数（`routes_`）も二重に数えて、あとで `unbind` したキーが「続きを待つ」状態（`MaybeTooShort`。押してもビープも鳴らない）になった。`KeyBinding::Register` 自身は、上書きを黙って行うだけ（古い参照を返さない）なので、呼ぶ側（`bind`）で外す
- **登録の前に、モードの文字を全部確かめる**：未知の文字があると、何も登録・解放せずにエラー（以前は、`bind("nx", …)` が `n` にだけ登録されて、その参照が解放されて残った。`unbind("nx", …)` は `n` だけを外してからエラーになった）。空文字列のモードは、何もしない（参照も作らない）
- **`luaL_error` は longjmp**：`std::string` を作らず、`const char*` で扱う。`GetKeyBinding`（`std::expected<…, std::string>`）の結果は、`luaL_error` を呼ぶ前に `bool` に直す
- **`unbind` で同じモードを2回書いた（`"nn"`）**：1回だけ外す（2回目は割り当てが無くて、エラーになるため）。`bind` は、2回目が1回目を置き換えるだけなので、特別扱いは要らない。割り当てが無いキーの `unbind` は、今までどおりエラー（`"nd"` で片方のモードにしか割り当てが無いと、先に処理したモードを外してから、もう片方でエラーになる）
- **罠：同じキーを Normal と Dialog の両方に bind すると、`thread is already running` になりうる**（既存の癖）：Lua のコルーチンは、キー列の文字列（修飾キー + キーコード。**モードに依らない**）で名前が付く。Normal の関数がダイアログを待っている間に、Dialog に bind した同じキーの関数を呼ぶと、同じ名前のコルーチンが走っているので、失敗する。ダイアログを開くキーと、ダイアログの中で使うキーは、別にする。

## 色の設定（`Miata.config.color.*`）

Lua からの代入先は `Config::Color`（`Config.h` の `CONFIG_COLOR_LIST` = `background`/`normal_text`/`normal_file`/`directory`/`search_match`/`search_current`/`filter_match`/`symlink`/`alias`/`cloud`）。**この一覧に足しただけでは何も起きない。描く側が `Config::Color().Get(...)` を読んで初めて効く。** `background` と `normal_text` は、最初のコミット（sokol/ImGui 時代）から一度も読まれておらず、設定しても効かなかった（OS が Light だと、背景は OS の純白のまま、白い `normal_file` の文字は白地に消えた）。色を足すときは、読む箇所まで作り、描画の実測（下の「テスト」）で確かめること。

| 設定 | 使っている所 |
|------|--------------|
| `background` | ペイン本体（`FileListView::Draw` の先頭で全面を塗る）、ヘッダー（`DrawHeader`）、入力バー（`_MiataQueryBarView`）、Quick Look の覆い（`_MiataQuickLookShield`）、進捗パネル（`_MiataProgressPanelView`。RGB だけ使い、透過度は 0.85 で固定）、ウィンドウ自体（`pl_set_window_background_color`。`InitializeImpl()` が `InitializeScript()` の後に一度呼ぶ） |
| `normal_text` | ヘッダーのパスの文字（`DrawHeader`）、入力バーの文字（プロンプト・入力欄・件数。件数は alpha 0.8）・キャレット・上端の線（alpha 0.35）（`QueryBar.mm`）、進捗パネルの文字（名前の行は alpha 0.8）と枠（alpha 0.25）（`ProgressOverlay.mm`）。入力バーと進捗パネルは、読み出しを `NormalTextColor()`（`NSColorUtil.h`）で共有する |
| `normal_file` / `directory` | 行の文字（`Draw`）。`directory` は、進捗パネルのバーの色も（溝は alpha 0.25）（`ProgressOverlay.mm`） |
| `filter_match` | 絞り込みで一致した部分の背景（`Draw`。検索の色より先に塗るので、同じ文字に検索も当たれば検索の色になる。カーソルのある行の特別な色は無い）。既定値は `resources/test.lua`（無いと、不透明な黒で塗られて文字が沈む） |
| `symlink` / `alias` / `cloud` | 一覧の右側の札の文字色（`Draw`。`<LNK>` が `symlink`、`<ALIAS>` が `alias`、`<CLOUD>`（ダウンロード前のファイル）が `cloud`。札の部分だけ。名前・更新日時は行の色のまま。`<DIR>` とサイズ（`<CLOUD>` の後ろのサイズも）も行の色）。既定値は `resources/test.lua`（無いと、不透明な黒で描かれて、暗い背景に沈む）。後述「一覧の右側の札」 |
| `search_match` / `search_current` | 検索で一致した部分の背景（`Draw`。カーソルのある行・フォーカスのあるペインは `search_current`）。既定値は `resources/test.lua`（`Color4f` の既定が不透明な黒なので、`test.lua` に無いと、一致部分が黒く塗られて文字が沈む） |

- **背景の alpha は使わない（常に不透明）**：背景を塗るときは `Color().Get(Background)` を直接使わず、必ず `Config::Background()`（alpha を 1 にして返す）を使う。半透明で塗ると、後ろのOSのテーマの色（Light なら白）が混ざり、OS の設定しだいで見た目が変わる。`pl_set_window_background_color` も不透明に塗る。**例外は進捗パネルだけ**：背景の RGB に、固定の透過 0.85（`kBackgroundAlpha`）を掛けて、下の一覧を透かす。後ろは、自前の一覧（不透明）なので、OS のテーマの色は混ざらない（`Config::Background()` の alpha ではなく、定数。上の理由は、そのまま成り立つ）。
- **背景と文字に、OS の色（`windowBackgroundColor`/`textColor` 等）を使わない**：OS のテーマに従い、Light では背景が純白（実測: Light `#ffffff` / Dark `#1e1e1e`）。ヘッダーの文字を OS の色にすると、背景を暗くしたとき黒文字が沈む（なので `normal_text` を読む）。ダイアログ（`Dialog.mm` ほか）は意図して OS の色のまま（`Miata.config.color` は効かない）。
- **ブラウザ領域のビューの `appearance` を、背景の明るさに合わせる**：`BrowserView` のコンテナに `AppearanceForBackground()`（`NSColorUtil.h`。輝度 < 0.5 なら Dark）を設定する。ペインの境目の線（`NSSplitView` のディバイダ）やスクロールバーは OS が描き、Light 用は「黒の薄い線」「明るい帯」なので、暗い背景だと線が消える（実測: OS が Light で背景が黒だと、境目が `#000000` に消えた。Dark 用の外観なら `#212121`）。
- **罠：ウィンドウやアプリ全体の `appearance` は切り替えない**：`pl_get_color`（`osx.mm`）は、描画の外（ダイアログの作成時など）では、`NSApp.appearance`/`window.appearance` を変えても OS の外観のままの色を返す（実測: `NSApp.appearance` を Dark にしても `windowBackgroundColor` は `#ffffff`）。全体を切り替えると、ダイアログの背景（`pl_get_color`）は白いまま、中のコントロールだけが Dark になって壊れる。切り替えるのは、`pl_get_color` を使わないビュー（ブラウザ領域のコンテナ）に限る。タイトルバーとダイアログは OS のテーマのまま。
- **ウィンドウ自体の背景は起動時に一度だけ**（設定は起動時にだけ読むため）。ペインとヘッダーは描画のたびに設定を読む。ウィンドウ自体の背景は、ペインの境目の隙間と、リサイズ中に広がった部分にだけ見える。
- **色の解釈**：`ToNSColor`（`colorWithRed:`）は sRGB（`colorWithSRGBRed:` と同じ。実測）。`"#rrggbb"`/`"#rrggbbaa"` は 16 進のとおりの色になる。

## その他の設定（`Miata.config.set_*`）

色以外の設定は、関数形（`set_font` / `set_font_size` / `set_history_limit` / `set_show_icons` / `set_show_hidden`）。**読み出し（`get_*`）は `get_show_icons` / `get_show_hidden` の 2 つだけ**（実行中に切り替えられる設定の、いまの状態を、切り替えずに読む。`Config::ShowIcons()` / `ShowHidden()` をそのまま返す。画面に触らないので、設定の読み込み中も呼べる。ほかの設定には、無い）。`Config::ScriptInitialize()` が、`config_funcs[]`（`Config.cc`）を `Miata.config` に登録する（`Application::InitializeScript()` が、`base.lua` とコマンドの登録の後、`test.lua` / `init.lua` の前に呼ぶ）。**`Application.cc` の `config_commands[]` / `view_commands[]` ではない**（それは `Miata.command.*`）。値は `Config` の private メンバー + 静的な getter（`Config::FontSize()` など）。

- **関数形にした理由**：`Miata.config.show_icons = true` のようなフィールドの代入は、タイプミスも黙って通り、何も起きない（`Miata.config` にメタテーブルは無く、あるのは `color` だけ）。関数なら、タイプミスは「nil を呼んだ」エラーになり、起動時のダイアログに出る
- **検証**：引数の型・範囲を、設定を変える前に確かめ、外れたらエラーにする（値は変えない）。`luaL_error` は longjmp なので、`std::string` などを作る前に。真偽値は `lua_type == LUA_TBOOLEAN` を見る（`lua_toboolean` は `0` も `""` も真にする）。`Script::CheckArgType` のエラーメッセージは、型コードを個数として書く（`expected 1 arguments, got 1`）ので紛らわしい。説明のあるメッセージを自前で出す（`set_history_limit` と `set_show_icons` が手本）。`set_font_size` は、型（数値）は見るが、範囲は見ない（0・負数・NaN を通す。既知。直していない）
- **反映のタイミング**：設定は、`View` を作る前に全部読み終わる（`InitializeImpl`）。値を読む側は、(a) 構築時に 1 回（`FileListView` のコンストラクタの `HeaderHeight()`、`QueryBar` のフォント。実行中の変更は反映されない）と、(b) 描くたびに（`FileListView::Draw` の色・フォント・行の高さ・`ShowIcons()`）の 2 通り。**`Config` から `View` への通知は、実行中に切り替えられる 2 つの設定（`show_icons` / `show_hidden`）だけにある**（`Config::SetShowIconsObserver` / `SetShowHiddenObserver`。`Application::InitializeImpl()` が、View を作った直後に登録する。Lua の `set_show_*` が、値を**変えたときだけ**（同じ値では呼ばない）、値を書いた後に呼ぶ。C++ の `SetShowIcons` / `SetShowHidden` は通知しない=`toggle_*` が、自分で反映する。設定の読み込み中は、登録前なので、`set_*` は値を書くだけ）。それ以外の設定で、実行中に変えるものは、(b) の読み方にして、変えた側が `Redraw()` を呼ぶ。レイアウトの値（アイコンの列の幅など）は、`Draw` の中で計算する（構築時にキャッシュしない）
- **実行中に切り替えられる設定は `show_icons` と `show_hidden` だけ**（`Miata.config.set_show_icons(...)` / `set_show_hidden(...)` を、実行中に呼ぶ。`resources/test.lua` で `.` キーと `zh` が、`set(not get)`。**専用の `toggle` コマンドは無い**=以前は `toggle_icons` / `toggle_hidden` があったが、実行中の `set_*` がすぐ反映されるようになったので、2026-10-10 に、ユーザーの判断で削除した。C++ の `SetShowIcons` / `SetShowHidden` も削除。値を書くのは、Lua の `set_*` だけ）。反映は、`Config` の通知（上記）が、`View::RefreshShowIcons()`（両ペインの `Redraw()`）/ `RefreshHiddenFiles()`（一覧の作り直し）を呼ぶ。値の読み方が違う: アイコンは描くたび、隠しファイルは `ApplyFilter()` のたび。保存はしない（起動時は設定が決める）。**ほかの設定は、実行中に切り替える手段を用意しておらず、反映も保証しない**（README の契約は「起動時にだけ読み込む」。色・フォント・行の高さは描くたびに読むので、書き換えると次の描画で変わりうるが、ヘッダーの高さなど構築時に読む値は変わらない）。設定の読み込み中（`view_` が無い）の `set_*` は、落ちずに、値だけが変わる（通知は、View を作った後に登録する）。ユーザーの依頼: 「set_* で再描画は難しい？」→「toggle は消しましたか？」→ 消す
