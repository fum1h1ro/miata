---
paths:
  - "src/models/ConsoleLog.*"
  - "src/StdioCapture.*"
  - "src/Log.h"
  - "src/views/ConsoleView.*"
---

# コンソール（ログのペイン。標準出力・標準エラー・Lua のエラーの取り込み）

> 機能別の設計メモ。上の `paths:` のファイルを触ると自動で読み込まれる。`Application.cc` など複数の機能が同居するファイルでは載らないので、そのときは CLAUDE.md の「設計メモの索引」から、該当するファイルを自分で読むこと。
> テスト・実測・実機未確認の記録は `.claude/notes/console.md`（自動では読み込まれない。テストを書く・直す・検証するときに読む）。

ブラウザ領域とステータスバーの間の、全幅のログのペイン（2026-10-16。依頼は「コンソール（ログ）ペインを追加したい。lua の print とかもキャプチャできたらしたい。最大保持行数は設定で変更可」）。ユーザー向けの仕様は README の「コンソール」と「Lua スクリプト API」の「コンソール」。ここには、設計の決定と罠を書く。

## ユーザーの決定

- **取り込む範囲は「標準出力に出ているもの全部」**。意味のあるものだけに絞らなかった（`funopen` で C の `stdout` を差し替える方法は、普通の stdio と同程度に軽い。実測は notes）。標準エラーも、同じ仕組みで取り、赤い行にする
- 操作は最小: 高さは設定の行数で固定、スクロールはホイール・トラックパッド、キーは開閉の `zc` だけ。**フォーカスのあるコンソール（第 3 のキーモード）は不要**（ユーザーが明言）。ドラッグでの高さの変更は、作っていない（後から足せる作り: 高さは `_MiataMainLayout.consoleHeight` の 1 か所）
- 既定は閉じている（今の見た目を変えない）。表示の状態も、ログの中身も、保存しない（`show_icons` / `show_hidden` / `show_statusbar` と同じ）。エラーが出ても、自動では開かない
- 背景・文字・フォントは一覧と同じ設定。エラーの行だけ新しい色 `console_error`（既定は `#ff6b6bff`）。色とフォントの独立した設定（ステータスバーのような）は、作っていない（依頼が無い）
- **標準出力に、デバッグの出力を残さない**（CLAUDE.md の約束事）。残すと、コンソールを開いた瞬間に、無関係な出力で埋まる。起動時の情報（`build:` `config_dir:` `Lua 5.5.1` `home:`）だけは、意図して出している（コンソールの最初の行になる）

## 構成

| 部品 | 役割 |
|---|---|
| `models/ConsoleLog.h/.cc` | 行の受け口。AppKit・Lua・Config を知らない純ロジック（`PathHistory` と同じ作り）。`View` より前に存在する、**解放しない**シングルトン（`Instance()`）。mutex で守る（書き込みは任意のスレッド）。`Epoch()` は atomic |
| `StdioCapture.h/.cc` | C の `stdout` / `stderr` を `funopen` の FILE に差し替えて、書かれたものを `ConsoleLog` に流す。元の FILE にも書き戻す（tee） |
| `Log.h` | `LogError(msg)`: 診断（エラー・警告）を 1 行、標準エラーへ出す唯一の入口。受け口を知らない |
| `views/ConsoleView.h/.mm` | 行を描くだけ（pimpl。素の `NSScrollView` + 自前に描く documentView）。`NextScrollY`（追従の位置の計算）は AppKit 非依存の純関数（ヘッダーの inline） |
| `Script.h/.cc` | キー関数のエラーを**値で返す**（`ThreadError`）。ログを知らない。警告の受け取り（`SetWarningHandler`） |
| `Application.cc` | 配線（差し替えの呼び出し・上限の注入・エラーの文面を作って `LogError`・`UpdateConsole`・`console_*` コマンド） |
| `View.mm` の `_MiataMainLayout` | ブラウザ領域・コンソール・ステータスバーの縦の並び |

## 標準出力・標準エラーの取り込み（`StdioCapture`）

- **方法**: `funopen` で、書き込みコールバックだけを持つ FILE を 2 つ作り（`stdout` 用が Normal、`stderr` 用が Error）、`stdout = out; stderr = err;` と差し替える（`stdout` は `__stdoutp` という普通のグローバル変数）。コールバックは、`ConsoleLog::Write` に渡し、元の FILE（差し替える前に控えた）に書き戻して `fflush` する。`printf` / `puts` / `fwrite` / `std::print` / Lua の `print`・`io.write`・`io.stdout:write`・`io.stderr:write` が、全部、同じ口で取れる。スレッドもパイプも要らない（同期的）ので、明示の追記との順序も崩れない。`dup2` でパイプにして、読み取りスレッドで拾う方法は、採らなかった（パイプが詰まると書き手が止まる・順序が崩れる・スレッドが要る）
- **罠: Lua を開く（`Script` の構築）より前に差し替える**。Lua の `io.stdout` / `io.stderr` は、開いた時点の `FILE*` を握り続けるので、後だと `io.write` が拾えない（`print` は、呼ぶたびに `stdout` を見るので、後でも拾える）。`Application::InitializeImpl()` の**先頭**で入れる。`Script` が最初に作られるのは `InitializeScript()`
- **罠: stderr も行バッファ（`_IOLBF`）にする**。通常の stderr はバッファなし（`_IONBF`）だが、差し替えた FILE で `_IONBF` にすると、`fprintf(stderr, ...)` が、書式の部品ごとに、別々のコールバックに分かれて届く。別スレッドの標準出力の行が、その間に割り込むと、`ConsoleLog` の「他方の種類の未完の行を先に確定する」規則が働いて、書きかけの行が壊れる。行バッファなら、1 回の出力が、FILE のロックの下で 1 つにまとまって届く（ただし、バッファに収まる範囲=macOS の `BUFSIZ` は 1024 バイト。それを超える出力は、複数のコールバックに分かれて届く。書くスレッドがメインの 1 本だけの間は、問題ない。複数のスレッドが、標準出力と標準エラーに、長い出力を同時に書くようになったら、行が割れうる）
- **罠: コールバックの中で、差し替えた `stdout` / `stderr` を使う関数（`printf` など）を呼ばない**（同じ FILE のロックで再入する）。触ってよいのは、受け口と、元の FILE だけ。書き戻しは、`ConsoleLog` のロックの外で行う
- **書き戻しは、毎回 `fflush` する**: `exit()` の flush は、FILE の並びの順に走るので、差し替えた FILE の最後の未完の行（改行の無い出力）は、元の FILE が先に flush された後に届きうる。flush しないと、その分が失われる。出力先がパイプやファイルのときは、1 行ごとに `write(2)` が 1 回
- 差し替えた FILE と cookie は、**解放しない**（プロセスの終わりまで。`exit()` が flush するのは、静的破棄の後でもありうる）。`ConsoleLog::Instance()` も、同じ理由で、`new` して解放しない
- `funopen` が失敗したら、何も変えずに `false`（コンソールは空のまま。端末への出力は、これまでどおり）。差し替えた FILE の `fileno()` は -1（コードベースに `fileno` / `isatty` の使用は無い）
- **拾えないもの**: 子プロセスの出力（fd を継承したもの。`os.execute`・fzf・エディタ）、`NSLog`（fd 2 へ直接）、`write(1, ...)` の直叩き、`std::cout` / `std::cerr`（libc++ は、構築時の FILE* を握る。コードベースに使用は無い）
- **行バッファ**なので、改行の無い出力は、改行か `fflush` まで、コールバックに届かない。届いた分は、`ConsoleLog` が未完の行として持ち越す（出さない）

## 行の受け口（`ConsoleLog`）

- **1 行 = `{seq, kind, text}`**。`seq` は通し番号（0 から。古い行を捨てても、`Clear` しても、振り直さない）。欠番が「捨てた」印になる（`ConsoleView` が、スクロール位置の補正に使う）。`kind` は、標準出力が Normal、標準エラーが Error。容器は `std::deque`
- **世代 `Epoch()`**（atomic。ロックしない）: 見える内容が変わるたびに進む（行が確定した `Write`・中身のある `Clear`・行を捨てた `SetLimit`）。未完の行だけの書き込み・空の `Clear`・変わらない `SetLimit` では進まない。`ConsoleView::Sync` は、毎ティック、まずこれだけを比べる（ロックしない）。変わっていたときだけ、`Range` / `Lines` で（ロックして）読む
- **整形は、確定のとき 1 回だけ**（`SanitizeLine`。純関数）: 末尾の CR を除く・不正な UTF-8 を U+FFFD にする（`RepairUtf8`。NSString にすると nil になって落ちるため）・NUL と C0 制御文字と DEL を空白にする・タブを空白 4 個にする・C1 制御文字（NEL を含む）と行区切り・段落区切り（U+2028 / 2029）を空白にする（画面に出すと、行を壊す）・2048 バイトを超えたら、文字の途中を避けて切って「…」を付ける。書き込みの境で、UTF-8 の文字が割れても、行が揃ってから直すので壊れない。`SanitizeBarText`（ステータスバー用。1024 バイトで切り、タブを 1 空白にする）は流用しない
- **未完の行**（改行を待つ間）は、種類ごとに持つ。上限は `kMaxRawBytes` = `kMaxLineBytes + 1`（末尾の CR の分: 足さないと、ちょうど 2048 バイトの行が CRLF で終わるとき、CR のせいで「長すぎて捨てた」ことになり、要らない「…」が付く）。超えた分は捨てて、次の改行まで何も足さない（印 `truncated` は、確定のとき「…」になる。切った位置が文字の途中だと、欠けた文字は U+FFFD になるが、raw は上限いっぱいあるので、整形の後の切り詰めで落ちる。極端に縮む行=C1 制御文字や U+2028 だらけの行でだけ、「…」の前に U+FFFD が残りうる。許容している）
- **他方の種類の未完の行は、先に 1 行として確定する**（標準出力の途中の行に、標準エラーの行が割り込んだとき、書いた順に並ぶように）。空の書き込みは、何もしない。`print('head', 壊れた __tostring)` は、標準の `print` が、先頭の引数だけを先に書いてしまうため、`head` が、次の標準出力の行の頭にくっつく（既知の限界。`print` は差し替えていない）
- **上限 `SetLimit`**: 1〜`kMaxLimit`（100000）に丸める。古い行から捨てる（広げても、捨てた行は戻らない）。**呼ばれるまで（設定の読み込み中）は `kMaxLimit` で持つ**ので、後から詰めても、最初から同じ上限で持っていたのと同じ中身になる（`PathHistory` のように、注入されるまで 0 にしてはいけない: `init.lua` の最上位の `print` が、設定を読む前に来る）。`Application` が、`InitializeScript()` の直後に `SetLimit(Config::ConsoleMaxLines())` を渡す（`ConsoleLog` は Config を知らない）。設定の読み込みがエラーでも、既定か、エラーの前に決めた値のまま
- `Config::kConsoleMaxLinesMax` と `ConsoleLog::kMaxLimit` は同じ値（`Application.cc` の `static_assert`。`Config.h` は `ConsoleLog.h` を include しない）。最悪のメモリは、上限 × 2KB 程度

## エラーの集約（`Script` と `LogError`）

- **`Script` はログを知らない**。キー関数（コルーチン）の失敗を値（`ThreadError{label, message}`）で返し、`Application` が文面を作る: `error: key <キー>: <メッセージ>`（`ReportThreadError`）。設定の読み込みのエラーは `error: <ConfigError::Describe()>`（ダイアログと同じ文面。Lua は長いパスを `...` で縮めるので、メッセージに完全なパスが含まれないときは、ファイル名を足す）、ステータスバーの関数は `statusbar: <理由>`（ラッチは残す）、警告は `warning: <テキスト>`、ファイル操作などの失敗は `View::ReportFileError` の `reason` そのまま（ダイアログに加えて `LogError`。権限の案内は、ダイアログだけ）。**ダイアログにはしない**（キー入力を奪う。ステータスバーの Lua のエラーと同じ決め）
- **`Script::Settle(name, param, status)` が、`lua_resume` の結果の扱いを 1 か所にまとめる**（最初の区間も、`Script::Update` が進めた後続の区間も同じ。確認のダイアログの後のコードは後続の区間なので、結果を必ず `Settle` に通す: 見ないと、エラーはどこにも出ない）。成功・待ち（`LUA_OK` / `LUA_YIELD`）: 積まれた戻り値を `lua_pop(nresult_)` で捨てる（積んだままにすると、同じコルーチンを使い回すたびに積み上がり、いつか `stack overflow` になる）。失敗: **スタックが生きているうちに**エラーを読み（`lua_resume` は巻き戻さず、エラーの値を先頭に残す）、`lua_closethread` で閉じ（`__close` の変数があれば、ここで走る）、`lua_settop(0)`・`status_ = LUA_OK` に戻す。**`status_` を戻さないと、後続の区間で失敗したコルーチンを、毎ティック再開し続ける**
- **罠: 新しいコルーチンは、レジストリに固定する（`luaL_ref`）。メインスレッドのスタックに積んだままにしない**（`InvokeRefFunctionOnThread`）。`lua_newthread` も `lua_rawgeti` も、スタックの空きを確かめない（`api_incr_top` は、`LUA_USE_APICHECK` の時だけ検査する）。ホストの C++ から使えるのは、`LUA_MINSTACK`（20）個まで。新しい名前のキー列のたびに 1 つ積み上げると、20 個を超えたところで契約違反になり、スタックの配列（初期 40 + 5）を超えると、ヒープを壊す。コルーチンの寿命は、`threads_`（C++ の map）ではなく、レジストリの参照が保つ
- **罠: 失敗したコルーチンを閉じ直さないと、同じキーを押し直したとき、関数が実行されない**（`lua_closethread` は、閉じる前の古いエラー状態をそのまま返す。実行されずに、古いエラーが返り、次の押下でやっと動く）。`Settle` が毎回閉じるので、押すたびに関数が実行され、エラーも毎回出る
- **コルーチンの名前は `KeyBinding::StrokeName`**（表示名: `dd` `<C-d>`）。キーコードの数字をつなぐだけの名前は、区切りが無く曖昧（`sd`（s=1, d=2）と `q`（12）が、どちらも "12"）で、片方が待っていると、もう片方が「thread is already running」になる。表示名は、ログの「どのキーか」にも使う
- **警告**: `Script::Script()` が、`lua_newstate`（3 引数。`luaL_newstate` ではない）を直接呼ぶので、警告の関数は自動では入らない。`lua_setwarnf` で、自前の関数を入れる（`warn()` と、VM が出す `__gc` のエラーの警告が、これで届く。`__close` の失敗は警告にならず、エラーとして伝わる）。規則は lauxlib の標準の関数と同じ: 続きでない `@on` / `@off` は制御メッセージ、断片は最後まで貯めて 1 件にする。違い: 出さない間も、断片の続きかを追う。**コアの中（GC の最中を含む）から呼ばれる**ので、Lua の API を呼ばず、C のフレームに例外を通さない。`luaL_newstate` に替えなかったのは、ハッシュの種（seed）が 0 から変わると、`pairs` の順が起動ごとに変わるため
- **作らなかったもの**（後から足せる）: panic の関数（保護外のエラーは、いまも、メッセージ無しで `abort()`）、トレースバック、`Miata.command.log`（`print` と `io.stderr:write` / `warn` で足りる）、エラーでの自動オープン

## 画面（`ConsoleView` と `_MiataMainLayout`）

- **縦の並び**: ブラウザ領域 / コンソール / ステータスバー。`_MiataMainLayout`（`View.mm`）が、`layoutChildren` で明示的に計算する（autoresizing の Y マージンは使わない）。コンソールの高さ = `min(consoleHeight, max(0, 残り − kMinBrowserHeight(120pt)))`。1 行（`ConsoleView::Height(1)`）に満たなければ 0（隠す）。窓が低いと、コンソールが先に縮む。3 つの高さの和は、いつも全体に等しく、負の高さは作らない。**`consoleShown`（既定 NO=閉じている）という「出す」側の名前にした**（未設定が既定になる。ステータスバーの `statusHidden` とは逆向き）
- **描画**: `FileListView` と同じ作り（素の `NSScrollView` + flipped な documentView。見えている行だけ描く）。行の高さは `ListRowHeight()`（`ViewMetrics.h`。一覧と同じ式。一覧の `RowHeight` を移した。**名前を `RowHeight` にしない**: 無名名前空間の同名の関数に隠される罠が、`BarHeight` で起きた）。縦位置は一覧の行と同じ式。折り返さず、`NSLineBreakByTruncatingTail`。行の位置は通し番号から決める（同期の後にログが進んでも、行と内容がずれない）。上端の境目の線と背景は、`DrawBarBackground`（入力バー・ステータスバーと共通）
- **first responder にならず、`hitTest:` も横取りしない**: ホイール・トラックパッドのスクロールが、中の `NSScrollView` に届く。`NSTextView` は使わない（`acceptsFirstResponder` を YES にしない規約と、ダイアログを閉じるたびに first responder が `contentView` へ戻される作りに合わない）。テキストの選択は無い
- **スクローラの外観**: コンテナに `AppearanceForBackground()` を設定する（`BrowserView` のコンテナの外にあるので、ここで要る）
- **追従は、状態を持たず、位置から決める**（`NextScrollY`）: 更新の前にいちばん下を見ていれば（誤差 1pt）、更新の後のいちばん下。そうでなければ、先頭から捨てられた行数ぶん、位置を戻す（同じ行が、同じ場所に見え続ける）。範囲に収める。`stale`（最初と、出し直したとき）は、必ずいちばん下。窓の大きさが変わるとき（`setFrameSize:`）は、変わる**前**の位置・大きさ（`BeforeResize`）で判定する
- **同期**: `Application::UpdateConsole`（→ `View::UpdateConsole` → `ConsoleView::Sync`）を、**毎ティック（`UpdateStatusBar` の後）・`KeyDown` の出口・起動時**に呼ぶ。隠している間（`Config::ShowConsole()` が偽）は何もしない（ログは貯まり続ける）。世代（`Epoch`）が同じなら、AppKit に触らない。**`ConsoleLog` への書き込みの中では、ビューに触らない**（`print` は、Lua の中・設定の読み込み中・描画の途中からも呼ばれる）。見えていない（クリップの高さが 0。窓が低くて出せない）ときは、同期せず、世代も消費しない（見えるようになった次の呼び出しで直す）
- **出入り（`set_show_console` → `Config::SetShowConsoleObserver` → `View::RefreshConsoleVisibility`）**: 並べ直し、出すときは `Invalidate()`（次の同期で、いちばん下から）。**観測者の中で同期しない**（Lua のキー関数の中から呼ばれる。同期は、`KeyDown` の出口と毎ティックが行う）。同じ値の `set_show_console` は、観測者を呼ばない（出し直しにならない）
- 読み込み中（`init.lua` の最上位）の `set_show_console` は、値だけが変わる（`View` は、その値で作られる）。画面を変える設定なので、ステータスバーの関数の中では、エラー（`Config::SetViewChangeGuard`）

## 設定・コマンド

- 設定（`Config.cc` の `config_funcs[]`）: `set_console_max_lines(n)`（1〜100000 の整数。`set_history_limit` と同じ検証。0 は「記録しない」とも「無制限」とも読めて曖昧なので受け付けない）、`set_console_height(n)`（1〜100 の整数）、`set_show_console(bool)` / `get_show_console()`、色 `console_error`（**`resources/test.lua` の既定値と同時に**）。行数と高さは、起動時にだけ読む（実行中に呼ぶと、値だけが変わる）
- コマンド（`view_commands[]`。画面か状態を変えるので、設定の読み込み中と、ステータスバーの関数の中では、エラー）: `console_clear()`、`console_copy()`（全行を `pl_set_clipboard_text`。コピーする文字列が空（行が無い・空行だけ）なら、何もせず false）、`console_scroll(n)`（整数だけ。巨大な値でもよい=端で止まる。隠しているときは何もしない）。既定のキーは、開閉の `zc` だけ（`test.lua`。説明つき）。ほかは、README の例を、自分で `bind` する

## 限界・既知

- 子プロセスの出力・`NSLog` は取れない。改行の無い出力は、改行か flush まで出ない。`print` の `__tostring` のエラーで、先頭の引数が次の行にくっつく
- 保護外の Lua のエラー（panic）は、いまも、メッセージ無しの `abort()`
- 窓の高さがとても低いと、コンソールは出ない（ブラウザ領域に 120pt を残す）。高さをドラッグでは変えられない
- 実行中に `set_console_max_lines` を縮める経路は無い（起動時だけ）。足すなら、観測者で `ConsoleLog::SetLimit` を呼ぶだけ
