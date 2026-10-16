# コンソール: テスト・実測・実機未確認の記録

> 機能別のテスト・実測・実機未確認の記録。自動では読み込まれない（テストを書く・直す・検証するときに読む）。設計の決定と罠は `.claude/rules/console.md`。

## テスト（リポジトリ外のスクラッチ。2026-10-16）

テストはリポジトリに入れていない（このコードベースの方針）。すべて、作業ツリーのソースを直接コンパイルするか、ビルド成果物（`main.mm.o` 以外のオブジェクトと `liblua.a`）にリンクする。

- **純ロジック `console_log_test.cc`**（`ConsoleLog.cc` / `Utf8.cc` を、AppKit・Lua 抜きで直接コンパイル。**6523 項目**を、通常・ASan+UBSan・TSan の 3 通りで）: 行の確定（改行の有無・空行・CRLF・複数行を 1 回の書き込みで）、種類（Normal / Error）の混在と、他方の未完の行の先行確定、`SanitizeLine`（C0 制御文字・DEL・C1・NEL・U+2028 / 2029・タブ・不正な UTF-8）、2048 バイトの境（`Write` の経路を含む。CRLF でちょうど 2048 バイト・2〜4 バイトの文字が、バッファの境のどこで割れても、U+FFFD が残らず上限以内で「…」で終わる・整形で縮む文字（C1 制御文字・U+2028）を含む長い行にも「…」が付く）、上限（`SetLimit` の丸め・縮める・広げる・通し番号の連続）、`Range` / `Lines` / `Text`（境目・空・範囲外・件数の丸め）、世代 `Epoch()` が進む条件と進まない条件、`CommitPending`、**どんな分け方で書いても行は同じ**（`TestSplitInvariance`: 1 バイトずつを含む分割）、**ランダムなバイト列 3000 通り**（`TestFuzz`: 出した行が守るべき性質を満たし、分け方に依らない）、並行（`TestConcurrency`: 書き込み 4 スレッド × 5000 行 + 読み取り 2 スレッド。各スレッドの行の順序が保たれる・世代が戻らない・通し番号が連続する。TSan で競合なし）
- **標準出力の差し替え `capture_test.cc`**（`StdioCapture.cc` を `liblua.a` にリンクして直接コンパイル。**33 項目**を、通常・ASan+UBSan・TSan の 3 通りで + 元の出力先の検査）: C / C++ の `printf` / `puts` / `fputs` / `fwrite` / `putchar` / `std::print` / `std::println`、標準エラー（`fprintf(stderr, …)` / `LogError`（複数行・末尾の改行・空）/ `std::println(stderr, …)` / `fputs(…, stderr)`）が、順序どおりに、種類つき（stdout = Normal、stderr = Error）で届く、**差し替えた後に開いた Lua**（`print`・`io.write`・`io.stdout:write`・`io.stderr:write`・`__tostring`・複数行・NUL・10 万文字・日本語・不正な UTF-8）、**差し替える前に開いた Lua**（`print` は届くが、`io.write` は届かない=既知の限界）、未完の行（`fflush` しても届かない・標準出力の行が来ると標準エラーの未完を先に確定する）、二重の `Install` は何もしない、別スレッド 4 本（stdout と stderr が混ざる）で行が壊れない・順序が保たれる、元の FILE への書き戻し（tee。`run_capture.sh` が、リダイレクト先のファイルで確かめる）、**終了時の flush**（改行なしのまま `exit()` しても、元の stdout に届く。出力先がファイルでもパイプでも）。**TSan・ASan+UBSan とも、報告 0 件**（2026-10-16 に、レビューの前に回し直した）
- **`Script` のハーネス `repress_test2.cc`**（`#define private public` で `Script.h` を取り込み、ビルド成果物（`main.mm.o` 以外のオブジェクト）にリンクする。`threads_` のスタックを直接見る。**321 項目**）: A 最初の区間のエラー（押すたびに関数が実行され、エラーが 1 回ずつ出る=master は 1, 1, 2, 2 回）・B yield の後のエラー（押す → 待ち → `Update` で失敗が返る → また押せる）・C 待った後の正常終了・D 戻り値の積み上がり（戻り値の多い関数を何万回押しても、stack overflow しない・スタックが 0 に戻る）・E エラーオブジェクトが文字列でない（`error({})` / `error(nil)`）・F `<close>` の変数があるコルーチンが失敗しても `__close` が走る（`__close` の中のエラーも）・G 警告（`warn()` の断片を 1 件にまとめる・`@off` / `@on`・続きの断片が `@` で始まっても制御にしない）・H 設定のエラーの文面（`Describe`）・I 異なる名前のキーを 100 個呼んでも、メインスレッドのスタックが積み上がらない（積み上がりは 2 以下）・全部のコルーチンが GC を 2 回通しても生きていて、また動く
- **Release のオブジェクトでの実行**（`rake build:release` の成果物にリンクしたハーネス）: 全 35 シナリオが通る。Release のビルドの警告は、Debug と同じ既存の 2 件だけ
- **`LUA_USE_APICHECK` 付きの Lua での実行**（スクラッチで `packages/lua` から作る `build_lua_apicheck.sh`。プロジェクトの Lua と同じ作り（`-g -arch arm64`）に、`LUA_USE_APICHECK` だけを足す。ハーネスの `LIBLUA` を差し替えてリンク）: Script のハーネス（321 項目）と、全 35 シナリオが、**assert 0 件で通る**。通常の Lua は、API の契約違反（スタックの空きを確かめずに積む、など）を黙って通すので、ASan にも見えない（`liblua.a` を ASan で作り直さない限り）。**この方法で、master 由来の潜在バグを 1 つ見つけた**（下の「master との比較」）
- **`NextScrollY` の単体テスト `next_scroll_test.cc`**（`views/ConsoleView.h` を直接 include。AppKit 不要。**561375 項目**。うち 20 万組はランダム）: 追従（誤差 0.5pt・ちょうど 1pt・1pt を超える・捨てながら増える・`force_bottom`・行が無い）、読んでいる間（動かない・捨てた行ぶん戻る・0 で止まる・窓が高くなって下端が上がる・追従の判定が更新前の高さで行われる・行の高さが 20pt でない）、性質（結果が常に 0〜下端・捨てる行が増えれば位置は戻る・何も変わらなければ位置も変わらない）
- **アプリ全体 `app_test.mm`**（実物の `Application::Initialize()` を、模擬 `.app`（シナリオごとに固有のバンドル ID と `XDG_CONFIG_HOME`・`TMPDIR`）から起動し、1 回の起動で 1 シナリオ。**35 シナリオ・1578 項目**。`Application::KeyDown` で本物のキー入力を送り、`Tick()` = `Update()`、ダイアログは Return の後に `Pump()`）:
  - 取り込み: `boot`（起動の行が、ちょうど 3 行: `config_dir:`・`Lua 5.5.1`・`home:`。模擬 .app には版が無いので `build:` は出ない。デバッグの `print` の残骸が混ざらない）・`init_print`（`init.lua` の最上位の `print` が、設定の読み込み中でも貯まる）・`runtime_capture`（C の `printf`・`io.write`・`io.stderr:write`・`print` が全部届く。元の stdout / stderr のファイルにも書き戻される・stderr が stdout に漏れない）
  - 上限: `limit_small`（`set_console_max_lines(5)`）・`limit_default`（1000）・`limit_validate`（`0` `-1` `100001` `3.5` `0/0` `math.huge` `-math.huge` `'10'` `nil` `true` `{}` `2^63` `1e300` と引数なしは、値を変えずにエラー。`1` `100000` `3.0` は通る。起動後の呼び出しは `Config` の値だけが変わり、`ConsoleLog` の上限は起動時のまま。`set_history_limit` も、検証を共有している（`ReadIntegerArg`）ので同じ表で調べる: `-1` `10001` `3.5` `0/0` `math.huge` `'10'` `nil` `true` `{}` `2^63` は値を変えずにエラー、`0` `10000` `3.0` は通る）・`limit_then_error`（設定の途中でエラーになっても、エラーの前に決めた値）
  - エラー: `key_error_first`・`key_error_after_dialog`（`dd` の確認の後のエラーが赤い行に出る）・`strokes_distinct`（`sd` と `q` が別のコルーチン）・`statusbar_error`・`config_error`・`warn`・`warn_in_init`・`file_error`（`ReportFileError` が `LogError` する）・`edit_failure`・`key_ok`（成功するキーは、何も出さない）
  - 画面: `console_toggle`（`zc`・`set_show_console`・`get_show_console`）・`console_shown_at_start`（`init.lua` の `set_show_console(true)`）・`console_height_validate`・`console_layout`（3 つの高さの和・最小の高さ・窓の大きさを変える）・`console_hidden_no_sync`（隠している間は同期しない）・`console_follow`（追従・上へ戻ると動かない・上限で先頭を捨てても同じ行が同じ場所に・`Clear`・出し直し）・`console_resize_follow`（窓のリサイズ中の追従）・`console_low_window`（低い窓では出さない・戻すと出る）・`console_commands`（`console_clear` / `console_copy` / `console_scroll`。巨大な値（`2^40`・`math.maxinteger`・`math.mininteger`）が、int にあふれず、端まで動く。貼り付け板は差し替えた偽物）・`console_load_guard`（設定の読み込み中に画面を変えるコマンドはエラー）・`console_status_guard` / `console_status_command_guard`（ステータスバーの関数の中ではエラー）・`console_input`（コンソールの中の点の `hitTest:` が、横取りされず、中の documentView（`_MiataConsoleDoc`）に届く・マウスダウン / アップを `sendEvent:` で送っても first responder が `MiataRootView` のまま動かない・`acceptsFirstResponder` が NO・`zc` がそのまま届く。**ホイールの実イベントは送っていない**）・`console_idle`（何も変わらない 100 ティックで、documentView の `setNeedsDisplay:` が 0 回。変われば 1 回以上、その後はまた 0 回）・`console_stress`（不正な UTF-8・制御文字・NUL・10 万文字の行・日本語 1.5 万文字・絵文字（ZWJ）・結合文字・右から左への上書き（U+202E）・CRLF・長さの違う 2000 行を書いて、実際に全面を描いても落ちない。どの行も 2048 バイト以内）・`console_perf`
  - 描画: `render_default` / `render_osaka`（ウィンドウ全体を PNG に描いて、ふつうの行（文字色）とエラーの行（`console_error`）の画素を数える。Osaka 20pt の `init.lua` でも）。PNG は Light / Dark の外観と、先頭を見たものの 3 枚ずつ
- **変異テスト**（実装を 1 か所ずつ壊す。ソースは開始時に複製（走行中に作業ツリーを編集しても影響しない）、変異ごとに専用ディレクトリと専用のバンドル ID、3 並列）:
  - 段階①（`ConsoleLog` / `StdioCapture` / `Log.h`。純ロジックと取り込みのハーネス）: **48 件のうち 47 件を検出**。残り 1 件は同値: `M33`（`Instance()` を解放する。終了時の挙動だけが変わる）。最初の実行では 7 件が生き残り、テストを足すか、冗長なコードを消して、全部に決着をつけた（下）。レビュー後の整理で `DropIncompleteUtf8Tail` を消したとき、`M26`（その呼び出しを外す）は無くなり、代わりに `M03`（`truncated` の印を立てない）が生き残った=印が効くのは「整形で縮む長い行」だけになったため。テストの穴だったので、その行のテストを足して検出した
  - 段階②（`Script` / `Application` の配線 / `Config` の上限 / `ReportFileError` / `test.lua`。`Script` のハーネスとアプリ全体）: **32 件のうち 30 件を検出**。残り 2 件: `C06`（`set_console_max_lines` の「整数か」の検査を外す。次の `n < 1 || n > max` が、小数・文字列・NaN をすべて断るので、その時点では同値）と `R01`（`test.lua` に `Miata.util.pp("HOGE")` を戻す）= `boot` の期待を「ちょうど 3 行」に厳しくして検出した（直後に再実行して検出を確認）。`C06` は、その後、整数引数の検証を `ReadIntegerArg` に共有したとき（`set_history_limit` は 0 を許すので、「整数か」の検査を外すと `3.5` が 0 として通る）、同値でなくなった。`limit_validate` に `set_history_limit` の表を足して検出
  - 段階③（`NextScrollY` / `ConsoleView` / `_MiataMainLayout` / 設定の `show` と `height` と整数引数の検証 / 観測者 / コマンド / `zc` / 色の既定値 / `Sync` の世代の高速経路。アプリ全体）: レビューの後の整理を済ませてから回し直した **66 件のうち、アプリ全体で 62 件、`NextScrollY` の単体テストで 2 件（`N01`・`N05`）を検出**。残り 2 件は**同値**: `U13`（`Draw` の「描く範囲を超えた行で止める」を外す。範囲外の行は、描き直す範囲の外に描かれて、切り取られるだけ=性能の守り）と `W07`（`View::UpdateConsole` の「隠している間は何もしない」を外す。隠している間は高さが 0 で、`Sync` が見えていないとして止まる。明示の判定は、`UpdateStatusBar` と同じ作法）。回し直しで見つかった 2 件も解決した: `Y01`（スクロールビューが上端の線に重なる）は、`console_layout` に、スクロールビューの枠（線の下から下端まで）の検査を足して検出。`W05`（起動時の表示の設定を反映しない）は、整理で同じ文が 2 か所になり、変異の対象が一意でなくなっていた（変異の定義を直して検出）
  - `NextScrollY` だけは、純関数の単体テストで、10 件を直接調べた（`mutate_next_scroll.py`）: **10 件すべてを検出**。アプリ全体の変異テストで生き残った `N05`（下端で丸めない）と、同値の疑いだった `N01`（誤差を 0 にする）も、ここで検出できた（アプリ全体のシナリオには、窓が高くなって下端が上がる状況も、0.5pt の位置も無かった）
  - レビューの後に足した変異: `E01`–`E03`（`ConsoleView::Sync` の世代の高速経路。毎ティック描き直す・stale を見ない・比較が逆）、`T14` / `T15`（警告の後始末）、`S1` / `S2`（新しいコルーチンを、メインのスタックに積んだままにする / 降ろすだけで固定しない）: **7 件すべてを検出**（`T09`–`T12`・`A05`・`T01`–`T06` も回し直して、すべて検出）。`T16`（`@off` の間も文を貯める）は同値だったので、守りの `if` を外して単純にした
- **変異テストが見つけた、コードとテストの穴**: (1) 冗長な早期 return（`if (partial.truncated) return;`）→ コードを消した。(2) `HasPendingLocked` が複雑すぎた → 単純にした。(3) ちょうど 2048 バイトの行が CRLF で終わると、CR のせいで「長すぎ」と判定されて、要らない「…」が付いた → `kMaxRawBytes = kMaxLineBytes + 1`（コードの修正）。(4) テストの穴: C0 制御文字の全部・`Write` の経路でのちょうど 2048 バイト・空の `Write` が何もしないこと・`CommitPending` の Error・1 文字の未完の行 → テストを足した。(5) `test.lua` のデバッグ出力の復活（`R01`）→ `boot` を厳しくした。(6) 失敗の後始末で `status_` を戻さないと、後続の区間で失敗したコルーチンを毎ティック再開し続ける（`T03`）→ `key_error_after_dialog` が検出
- **master との比較**（`Script` の既存の不具合は、master を同じハーネスにリンクして再現した。master の `Script.h` を `-I master_hdr` で指して作った）: 失敗したキー関数の押し直しで、関数の実行回数が 1, 1, 2, 2（押すたびに 2 回に 1 回しか動かない）・戻り値が 20 個ある関数を 5 万回押すと `stack overflow`・確認の後の区間のエラーは、画面にも標準出力にも出ない・**新しい名前のキー列を初めて押すたびに、コルーチンをメインスレッドのスタックに積んだままにしていた**（`lua_newthread` の値。`lua_newthread` も `lua_rawgeti` も空きを確かめず、ホストから使えるのは 20 個=`LUA_MINSTACK` まで。`LUA_USE_APICHECK` の Lua では、21 個目の付近で `Assertion failed: (L->top.p <= L->ci->top.p)`。通常の Lua では、スタックの配列（初期 40 + 5）を超えたところで、ヒープを壊す。実際には、起動時に伸びた分と GC の縮小の兼ね合いで、いつ起きるかは分からない）。レジストリに固定して、メインのスタックには残さない形に直した（`InvokeRefFunctionOnThread`）

## 実測

- **標準出力の差し替えの費用**（`funopen` の FILE を、書き込みコールバックだけで作った場合）: 20 万行で 34.6 ms（**0.17 µs / 行**。普通の stdio と同じ）、別スレッド 4 本でも行が混ざらない。元の FILE への書き戻し（tee。毎回 `fflush`）を足すと、出力先がファイルのとき **約 2.1 µs / 行**（`capture_test` の 20 万行で 417〜429 ms。ASan+UBSan で 547 ms、TSan で 768 ms）。これが、ユーザーが聞いた「そんなに重くなる？」への答え: 実行時の費用は、`print` 1 回につき数 µs
- **アプリ全体**（`console_perf`。Lua の `print` を 10 万行、コンソールを開いた状態）: 10 万行の書き込み **281 ms**（2.8 µs / 行。Lua の `print` と tee を含む）・次のティックの同期 **0.2 ms**（行は 1 件も作り直さない。見えている分だけ）・ウィンドウ全体の描画 **24.7 ms**・アイドル 100 ティック **0.45 ms**（AppKit に 0 回）。上限（10 万行）を超えて貯めても、行数は上限のまま
- **メモリ**: 最悪で、上限（既定 1000 行、最大 10 万行）× 1 行 2 KB（超えたら「…」）。最大の設定で約 200 MB。既定では約 2 MB

## ハーネスの罠（テストの組み立て）

- **ハーネスは、`_build/debug` のオブジェクトにリンクする。ソースを直したら、先に `rake build:debug`**（古いオブジェクトのまま、前と同じ「118 / 0」が出て、直したコードを試していなかった）。変異テストは `save_objs.sh` で複製したオブジェクトを使うので、直した後に取り直す
- **`Config.h` を変えたら、ハーネスの `app_test.o` を作り直す**（`FORCE_HARNESS=1 bash build_app.sh`）。古い `Config` のレイアウトのまま、リンクされる
- **変異テストの前に、`_build` のオブジェクトを複製して、それを指す**（`OBJ_DIR=…/snap_objs`）。走行中に `rake build` しても、変異テストに影響しない。走行中に `app_test.mm` を編集しない（各変異が `app_test.o` を作り直す）
- **確認のダイアログは、`dispatch_async` で閉じる**。Return を送った後に `Pump()` で実行ループを回さないと、次のキーが「ダイアログの中」のままになる
- **子の ObjC のオブジェクトを、`CHECK_EQ` の引数で一時変数にすると、参照が dangling になる**（`CHECK_EQ(Foo().bar, …)`。ローカルに取ってから比べる）
- **レンダリングのテストで、最後の行を「最後のエラーの行」と決めつけない**（最後の行はふつうの行だった。内容で行を探す）
- Bash のガードは、複合コマンド（`for`・heredoc の Python・`$(…)` つきの引数）を断る。スクリプトをファイルに書いて、`bash x.sh` / `python3 -I x.py` で走らせる
- `NSBitmapImageRepFileTypePNG` は無い（`NSBitmapImageFileTypePNG`）
- **走らせた後の後片付けは、作った名前だけを消す**（`miata_probe_*` のようなワイルドカードで、ゴミ箱やクラウドのフォルダを触らない）

## 挙動の変更（ユーザーに見える）

- 起動時の `Lua Version: 505.000000`（`LUA_VERSION_NUM` を浮動小数で出していた）が、`Lua 5.5.1`（`LUA_RELEASE`）に変わった。コンソールを開くと、最初の行になる
- 設定の読み込みのエラーと、ステータスバーの Lua のエラー（以前は標準出力）は、**標準エラー**（`LogError`）に出るようになった。端末から起動して `2>/dev/null` すると見えなくなる
- 消した標準出力: 全キーバインドの一覧（`DumpAll`。起動のたびに約 60 行）・色名の表・ダイアログのハンドルが GC されるたびの "free shared userdata"・`yes` / `no` の結果・`test.lua` の `Miata.util.pp(...)` 2 行。端末から起動して見ていた人には、静かになる
- `Script::Update` が、確認の後の区間のエラーを捨てなくなった（`dd` `c` `m` `K` `r` `e` など）。エラーがあれば、コンソールに赤い行が出る

## 実機未確認

- 実機の Retina（PNG は 1x のオフスクリーンの描画）。行の高さ・上端の線 1pt・文字の縦位置が、2x でも揃うか
- 実機のホイール・トラックパッドのスクロール（ハーネスは `ScrollLines` / `console_scroll` で代えた。実イベントは送っていない）。慣性スクロールと、追従の相互作用（慣性の最中にログが増えたとき）。ホイールの 1 ノッチの行数
- Finder / Dock から起動したときの、標準出力・標準エラーが `/dev/null` の場合（差し替えた FILE から `/dev/null` への書き戻しは通る前提。ハーネスは、ファイルとパイプだけ）
- スクローラの見た目（常時表示の旧式のスクローラ・暗い背景の Dark 用）。コンテナの `appearance` は設定したが、実機で目では見ていない
- IME がオンのときの `zc`（`z` の後の `c`。コードは物理位置のはず）。JIS / ANSI の配列
- 窓を縦に伸び縮みしたときの、ライブリサイズ中の追従のちらつき
- 実際に長時間使ったときの、メモリ（既定 1000 行）と描画の費用。10 万行を超える出力を、実際のファイル操作（コピーの進捗など）と同時に出したとき
- `os.execute` / fzf / エディタなど、子プロセスの出力（取れないのが仕様）。`NSLog`（取れない）
