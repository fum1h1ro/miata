---
paths:
  - "src/views/StatusBar.*"
  - "src/views/StatusBarState.*"
  - "src/StatusBarScript.*"
  - "src/Format.*"
---

# ステータスバー（カーソル下の属性・マークの要約・次のキー候補・Lua の関数）

> 機能別の設計メモ。上の `paths:` のファイルを触ると自動で読み込まれる。`Application.cc` など複数の機能が同居するファイルでは載らないので、そのときは CLAUDE.md の「設計メモの索引」から、該当するファイルを自分で読むこと。
> テスト・実測・実機未確認の記録は `.claude/notes/status-bar.md`（自動では読み込まれない。テストを書く・直す・検証するときに読む）。

ウィンドウの下端の、全幅の 1 行（2026-10-11。依頼は「汎用ステータスバーが欲しい。なんでも表示できる」。用途は ①カーソル下のファイルの追加情報 ②マークの件数・合計サイズ ③2 ストローク以上のキー入力の途中の、次のキー候補）。左にカーソル下のファイルの属性、右にマークの要約、キー列の入力の途中は左を次のキー候補に置き換える。ユーザー向けの仕様は README の「ステータスバー」と「ステータスバーの内容」。ここには、設計の決定と罠を書く。

## ユーザーの決定

- **内容の決め方は「C++ 既定 + Lua 上書き」**（ユーザーの選択。「Lua の関数だけ + 既定は test.lua」という案もあった）。書式の実装は C++ の `DefaultStatusText` が唯一で、Lua には、材料（`ctx`）と既定の文字列（`ctx.default`）を渡して、置き換えを受け取るだけ（二重実装にしない）。登録しなくても、そのまま動く
- ③の説明文は、`bind(mode, keys, fn, desc)` の 4 番目の引数（任意）。既定の複数ストローク（`gg` `dd` `yy` `zh`）に付ける
- ②は「見えているマーク」だけ（ファイル操作の対象と同じ）。ファイルの合計サイズ。フォルダは再帰しない（重い・クラウドで取得が起きる）。隠れたマークは `(隠れ N)`。フォルダには `(フォルダ N)` を添える（フォルダは 0B なので、合計を読み違えやすい）
- ①はカーソル下の 1 件だけ、変わったときに `lstat` 1 回の範囲（種類と権限・所有者/グループ・正確なサイズ・更新日時・リンク先）。`FileEntryModel`（全件）には足さない。xattr・Finder のタグ・UTI は読まない（ダウンロードを起こすかが未確認）
- `fn(ctx)` の戻り値は「書いた側だけ置換」（省いた側は既定のまま）。前置きの間は、左だけ候補に置き換え、右（マーク）は残す（`dd` で消える件数を見ながら 2 打目を選べる）
- Lua のエラーは、`set_statusbar` で差し替えるまで、バーの左に出し続ける（ラッチ。関数は呼び直さない。前置きの間は、候補を優先）。**ダイアログにしない**（キー入力を奪う）。ランタイムの Lua のエラーを画面に出す経路が、ほかに無い（コンソールは別件）ので、バーに出す
- **入り切りの設定を足した**（初回の報告の後の、ユーザーの依頼: 「バーの入り切りは確かにあったらいいと思ってました。デフォルト表示あり」）。`Miata.config.set_show_statusbar(bool)` / `get_show_statusbar()`、既定は**出す**、キーは `zs`（`resources/test.lua`）。仕組みは、後述「入り切り」
- **別の話（作っていない）**: キー入力のタイムアウト・前置き中の Esc の扱い・ダイアログを開いたときの前置きのリセット・ウィンドウの非アクティブ化・押し込み式の `set_status(text)`

## 構成

| 部品 | 役割 |
|---|---|
| `views/StatusBarState.h/.cc` | **AppKit にも Lua にも依存しない**（`ProgressState` と同じ作り）。材料 `StatusSnapshot`・既定の書式 `DefaultStatusText`・整形 `SanitizeBarText`・鍵 `StatusKey`・`StatusBarState::Update(make_key, has_function, collect, script)`（鍵の比較・ラッチ・鍵の取り直し）。関数を注入するので、偽の関数で、呼び出しの回数・ラッチを試せる |
| `views/StatusBar.h/.mm` | 渡された内容を描くだけ（pimpl）。自前 `drawRect:`、子に `NSTextField` を持たない。クリックは吸収、first responder にならない |
| `StatusBarScript.h/.cc` | `ctx` を組み、関数を呼び、戻り値を読む。Lua の C API だけ |
| `Format.h/.cc` | サイズ・権限・日時の書式（一覧の `FormatSize` を移した。`FileEntryModel` の更新日時と共有） |
| `views/NSColorUtil.h` の `DrawBarBackground` | 入力バーとステータスバーの背景と上端の線（共通。.mm 専用ヘッダ） |
| `Application::UpdateStatusBar()` | 配線。`make_key` / `collect` / `script` の 3 つのラムダを、`StatusBarState::Update` に渡す |
| `FileListView::ListEpoch()` / `GetMarkSummary()` | 一覧の世代と、マークの要約（世代でメモ化） |
| `FileEntryModel::MarkEpoch()` | マークの世代（static。`Mark()` が、値が変わるときだけ進める） |
| `pl_get_file_info`（`platform.h` / `osx.mm`） | `lstat` + `readlink` + 所有者/グループの名前（引いた結果を 256 件まで覚える） |
| `KeyBinding::Continuations` / `KeyName` / `StrokeName` / `descriptions_` | 次のキー候補・表示名・説明文 |
| `View::View()` の `_MiataMainLayout` | contentView の直下の縦の並び（ブラウザ領域 + バー）。flipped で、明示計算。`statusHidden` で、バーを隠す（ブラウザが全面） |
| `Config::ShowStatusBar()` / `SetShowStatusBarObserver` / `View::RefreshStatusBarVisibility()` / `StatusBarState::Invalidate()` | バーの入り切り（後述「入り切り」） |

## いつ作り直すか（鍵）

- 呼ぶのは、`Application::Update()`（`UpdateQuickLook` の後）・`KeyDown` の出口（本体 `KeyDownImpl` を包む。早期の return も通る）・起動時（`RestorePanes` の後に 1 回）だけ。**Lua も View の更新も走っていない時点**（Lua の関数が、再スキャンの途中などで、入れ子に呼ばれないように）。**`FileListView::Redraw()` から呼ばない**（リサイズやフォーカスでも呼ばれ、1 操作で複数回呼ばれる）
- 鍵 `StatusKey` = カーソルのペイン・**両ペインの**カーソルのパスと一覧の世代・マークの世代・前置き・ダイアログの有無・関数の世代。**反対側のペインも入れる**: 反対側のペインを、読むだけのコマンド（`pane_path("right")` など）で読む Lua の関数が、反対側の変化（カーソル・フォルダ・絞り込み）で、呼び直されるように（カーソルのペインだけだと、反対側が変わっても、バーが古いままになる。`lua_other_pane` のテスト）。同じ間は、`collect` も `script` も呼ばない（`lstat` も Lua も呼ばない。アイドルの間は、鍵の比較だけ）
- 一覧の世代 `ListEpoch()` は、`ApplyFilter()` で進める（`list_` を書く唯一の場所なので、再スキャン・ソート・絞り込み・隠しファイルの切り替え・移動を、全部拾う。同じパスのまま中身が変わる自動リロードも）。**カーソルの移動では進まない**（カーソルのパスが鍵に入る）
- マークの世代 `MarkEpoch()` は、`FileEntryModel::Mark()` で進める（マークを書く唯一の入口なので、取りこぼさない）。**値が変わらない書き込みでは進めない**。反対側のペインのマークが変わっても進む（作り直しは、安く済ませてある）。**`ApplyFilter()` を通らない**ので、一覧の世代だけでは、マークの変更を拾えない
- `GetMarkSummary()` は、一覧の世代とマークの世代でメモ化する（カーソルを動かすだけでは、マークの数だけ `stat` しない）。鍵の取り直し（`Update` が、関数を呼んだ後に、もう一度 `make_key` を呼ぶ）で、関数が状態を変えても、次のティックで、同じ内容のまま再実行しない。**ただし関数の世代だけは、評価に使った世代を残す**（関数が自分を差し替えたら、次の `Update` で、新しい関数で組み直す）

## キー列の入力の途中（③）

- **前置きの判定は、`KeyDownImpl` の `MaybeTooShort` の枝だけ**。`KeyBinding::Has` は、空のキー列でも `MaybeTooShort` を返す（割り当てが 1 つでもあれば）ので、ティックで `Has` を呼ばない。`key_stroke_` を、バーのために直接読まない: Lua の関数の実行中は、確定した完全なキー列が残っている。`pending_` を、キーごとに作り直して持つ（確定したか、外れたら空）
- `Continuations(prefix)` は、`bindings_` を全走査する（`routes_` は、`unbind` の後も 0 のまま残るので使えない）。次のキーごとに 1 つ。そのキーだけで確定する割り当てがあれば `leaf`（その先の、より長い割り当ては、完全一致が先に実行されるので、到達できない=出さない）。無ければ、続きのあるキー。並びは、修飾キー無し → 有り、表示名の昇順
- **説明は `descriptions_`（`KeyStroke` をキーにした別の表）**。`Key` にデータメンバーを足せない（`KeyStrokeHash` が、4 バイトの `Key` を `uint32_t` として読む型パンニング）。`Register` で置き換わる（説明なしなら消える）、`Unregister` で消える。`bind` の 4 番目: 文字列か `nil` 以外は、何も登録せずにエラー。`luaL_error` の前に `std::string` を作らない（`const char*` + 長さのまま渡す）
- **表示名 `KeyName`**: README の記法（`g` `<S-g>` `<C-d>` `<enter>`）。`ascii_to_keycode_` の値 0 は「割り当て無し」と `a` の両方なので、`a` だけ特別に引く（0 を無効の印にしない）。大文字は小文字と同じキー（Shift ではない）。スペースは `Space`（README に書いたとおり、`<space>` は `bind` では使えない。表示だけ）。**`KeyStroke::ToString()` は、スレッドの名前用で、区切りが無く曖昧**（`sd` と `q` が、どちらも "12"）なので、表示に使わない（同名のコルーチンが「thread is already running」になる既存の癖の、別の原因）
- ダイアログが開いても、前置きは残る（既知の挙動。バーで見えるようになるだけで、変えていない）。キー入力のタイムアウトは無い

## Lua の関数

- `set_statusbar(fn | nil)`（`Config.cc` の `config_funcs[]`）。参照（`luaL_ref`）と世代を `Config` が持つだけ（観測者は作らない: 世代の比較で足りる）。古い参照を解放してから、新しい参照を作る（呼び出し中の関数の値は、スタックに残るので、生きている）。実行中の差し替えを許す（README の「起動時にだけ読み込む」の例外に足した）
- **呼び方**: `Script::CallProtected(InvokeStatusFunction, &call, kStatusBarInstructionLimit, read)`。メインの state で `lua_pcall`。C 関数 `InvokeStatusFunction` が、ctx を組み（メモリ不足の longjmp も、外側の pcall が受ける）、`lua_call` し、戻り値の型を検証して、結果を 1 つ残す。**読み取りは、pcall の後**（`std::string` などを、longjmp の通る関数の中で作らない）。命令数の上限は `LUA_MASKCOUNT` のフック（呼び出しの後に外す。外し忘れると、ふつうの Lua が、上限を超えたところでエラーになる）。コルーチンの外なので、yield は「attempt to yield from outside a coroutine」のエラー
- **罠: 戻り値の表の読み取りは `lua_rawget`**（`lua_getfield` は、メタテーブルの `__index` を起動する）。読み取りは、pcall と命令数の上限の外なので、メタテーブルを持つ表を返されると、保護なしの Lua が走る（メイン state には errorJmp も panic 関数も無く、エラーは `abort()`、ループなら UI が固まる）。検証（`lua_next`）も raw。`lua_meta_return` のテストが、`__index` がエラーになる・止まらない関数でも、落ちない・固まらないことを見る
- **関数の中で、View を変えるコマンドは呼べない**: `lua_view_trampoline` が、`in_status_function_` のとき `luaL_error`（状態を変えると、鍵が変わって、毎ティック、関数が呼び直されて、振動する）。**読むだけのコマンドは `view_queries[]`（`lua_view_query_trampoline`）に登録してある**ので、呼べる。**新しい Lua のコマンドを足すときは、画面も状態も変えないなら `view_queries[]`、変えるなら `view_commands[]`**。`Miata.config.*` と `bind` / `unbind` は、トランポリンを通らない。画面を変える設定 `set_show_icons` / `set_show_hidden` だけは、`Config::SetViewChangeGuard`（`Application` が、`in_status_function_` の間は偽を返す）で止める（値を変えずに、Lua のエラー。止めないと、観測者が、関数の中で View を書き換え、`collect` 済みの内容が古いまま残る）。`set_statusbar` は、止めない（自分を差し替えられる）。`bind` / `unbind` も止めていない（関数は、鍵を変えないので、害は無い）
- 戻り値: `nil` / 文字列（左だけ）/ `{ left, right }`（書いた側だけ。値は文字列か数値。`left` `right` 以外のキーはエラー: タイプミスを黙って捨てない）。文字列は `SanitizeBarText` を通る（不正な UTF-8 を U+FFFD に、制御文字を空白に、1024 バイトまで）。**バーに出す文字列の、唯一の関門**
- エラーの表示は、バーの左（`Lua: <理由>`）。`StatusBarState` がラッチする（関数の世代が変わるまで `script` を呼ばない）。stdout にも 1 回（`Application::UpdateStatusBar`）
- **`Miata.command.file_info(path)`**（`view_queries[]`）: `pl_get_file_info` の結果を、`ctx.entry` と同じ項目（`SetFileInfoFields`。共用。`PushFileInfoTable` が、`is_dir` / `is_link` を足した表にする）で返す。取れなければ `nil, 理由`。`path` は絶対パスのみ（`CheckAbsolutePathArg`）。**`is_dir` は、リンクそのもの**（フォルダへのリンクは `false` で、`is_link` が `true`）で、`cursor_entry` の `is_dir`（リンク先を見る）とは違う。`size` / `size_text` は、通常のファイルだけ（フォルダとリンクには付かない）

## 描画・レイアウト

- `_MiataMainLayout`（`View.mm`）が、contentView の全面に広がり、子の位置を、`setFrameSize:` で明示的に計算する（ブラウザ領域 `(0, 0, w, h − バー)`、バー `(0, h − バー, w, バー)`）。**flipped のビューでは、autoresizing の Y マージンの意味が反転する癖があり、前例が無い**ので使わない（`_MiataFileListLayoutContainer` と同じ作り）。ブラウザ領域の中（`NSSplitView`・Quick Look の覆い・入力バー）は、分割ビュー由来の明示計算なので、ブラウザを縮めれば、追従する。進捗パネルのホストは contentView 全面のまま（右上なので無関係）。ダイアログは contentView の中央のまま（ブラウザ領域の中心より、バーの半分だけ下。害は無い）
- バーの高さは `HeaderHeight()`（ヘッダー・入力バーと同じ）。文字の縦位置は、ヘッダー（`DrawHeader`）と同じ式（文字列の高さで中央）。右を先に確保して（幅の半分まで）、左は、末尾を「…」で切る（`NSLineBreakByTruncatingTail`）
- **背景と上端の境目の線は、入力バー（`QueryBar`）と共通の `DrawBarBackground`**（`NSColorUtil.h`。`kBarBorderAlpha` = 0.35、補助の文字の `kBarSubTextAlpha` = 0.8 も共通）。共通にしたときの確認は、入力バーとステータスバーが並んだウィンドウの PNG が、前後でバイト単位で同一であること（`qbar_render`）
- **罠: `drawRect:` で `dirtyRect` をそのまま塗らない**（`DrawBarBackground` が `NSIntersectionRect(dirtyRect, bounds)` を取る）。macOS 14 以降、親をまるごと描くと、`dirtyRect` が子の範囲を超えて渡され、不透明に塗るバーが、ブラウザ領域を塗りつぶす（`render` のテストが検出する）
- 色は、設定のものだけ（背景は `Config::Background()`、文字は `normal_text`。右は alpha 0.8、上端の線は alpha 0.35）。新しい色名は足していない。ブラウザ領域のコンテナにだけ設定している `AppearanceForBackground()` に依存しないよう、自前描画（OS が描く部品が無い）

## 入り切り（`set_show_statusbar` / `get_show_statusbar` / `zs`）

- **設定は `show_icons` / `show_hidden` と同じ型**（`config.md`「その他の設定」）: `Config::show_statusbar_`（既定 `true`）・`ShowStatusBar()`・`SetShowStatusBarObserver`。Lua の `set_show_statusbar` が、**値が変わったときだけ**、値を書いた後に観測者を呼ぶ（同じ値では呼ばない=鍵を忘れさせないので、関数も呼び直されない）。設定の読み込み中は、観測者が未登録で、値だけが変わる（`View` は、その値で作られる）。真偽値以外は、値を変えずに Lua のエラー（`lua_toboolean` は `0` も `""` も真にするため、`lua_type` で見る）。**ステータスバーの関数の中では呼べない**（`SetViewChangeGuard`。画面を変えるため）
- **観測者がすること（`Application::InitializeImpl`）**: `View::RefreshStatusBarVisibility()`（`_MiataMainLayout.statusHidden` を直す=バーを `hidden`・高さ 0 にして、ブラウザ領域を全面に。出すときは、バーの高さを空ける）と、`StatusBarState::Invalidate()`（前回の鍵を忘れる）。**組み直しは、観測者ではしない**: `zs` などの Lua のキー関数の中から呼ばれるので、そこで、バーの Lua の関数を、入れ子に呼ばない。組み直しは、`KeyDown` の出口（キー入力の直後）と、毎ティックが行う
- **隠している間は、`UpdateStatusBar()` が、何もしない**（先頭で `Config::ShowStatusBar()` を見て return）。材料の収集（`lstat`・マークの集計）も、Lua の関数の呼び出しも、鍵の計算もしない。**前置きの判定（`pending_`）は、`KeyDownImpl` の仕事で、バーの有無に依らず動く**（隠していても、キー入力の仕組みは変わらない）
- **`Invalidate()` が要る理由**: 隠している間は `last_key_` が古いまま残る。出し直したとき、鍵が（偶然）同じでも、**鍵に現れない状態**（関数が読む Lua の変数・時刻など）は、変わっているかもしれない。忘れさせて、必ず組み直す。**エラーのラッチは解かない**（関数の世代が変わるまで、関数は呼ばない。隠して出し直しても、壊れた関数は、呼び直されない）。同じ値の `set_show_statusbar` は観測者を呼ばないので、鍵は忘れない
- **レイアウト**: `_MiataMainLayout` は、`statusHidden`（既定 `NO`=出す）を持つ。**「出す」ではなく「隠す」側の名前にした**のは、未設定（`NO`）が既定の「出す」になるため（並びの部品を直接作るテスト・将来の呼び出しで、バーが消えない）。`layoutChildren` が、隠すときは、バーの高さを 0 にして `hidden = YES`、ブラウザ `(0, 0, w, h)`。窓のリサイズ中も、同じ式で追従する。隠したバーは、ヒットテストにも当たらない
- **起動したときの状態**は、`init.lua` の `set_show_statusbar(false)`（`View` を作る前に読み終えているので、`View::View()` が、その値で並べる）。`zs` の切り替えは保存しない
- **限界・注意**: 隠している間は、バーに出すはずだった Lua のエラー（`Lua: …`）も見えない（stdout への 1 回の出力は、エラーが起きた時点でのみ。隠している間は関数を呼ばないので、新しいエラーも起きない）。ダイアログの `RequestDialog` などは、バーの有無とは無関係

## 限界

- **時間で変わる表示（時計）はできない**（鍵が変わらないと、関数を呼ばない）
- マークの合計は、マークが変わるたびに、見えているマークの数だけ `stat` する（3000 件で、マークの変更 1 回 4ms ほど。SMB など遅いボリュームでは遅い。増分化・上限は未実装）
- リンクの更新日時は、リンクそのもの（一覧は、リンク先）。フォルダの子の数・フォルダの再帰サイズ・xattr・Finder のタグ・UTI は出さない
- `os.execute` のような、C の側で待つものは、命令数の上限では止められない。サンドボックスは無い（キー関数と同じ）
- ダイアログの表示中、`dialog_*` を、関数から呼べない（View を変えるコマンド）
