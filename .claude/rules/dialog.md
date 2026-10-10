---
paths:
  - "src/views/Dialog.*"
  - "src/views/FilterListDialog.*"
  - "src/views/Mnemonic.*"
---

# ダイアログ（`DialogPanel`）・選択リスト・項目のショートカット

> 機能別の設計メモ。上の `paths:` のファイルを触ると自動で読み込まれる。`Application.cc` など複数の機能が同居するファイルでは載らないので、そのときは CLAUDE.md の「設計メモの索引」から、該当するファイルを自分で読むこと。
> テスト・実測・実機未確認の記録は `.claude/notes/dialog.md`（自動では読み込まれない。テストを書く・直す・検証するときに読む）。

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
| `dialog_yes_no(message, default_focus, yes_text, no_text)` | `"yesno"` | YES/NO ボタン。最初のカーソルと「既定」の強調は、`default_focus` が true（省略時）なら YES、false なら NO（`YesNoDialog::OnOpen` が `SetInitialFocus` で揃える）。Esc は常に false |
| `dialog_input(message, initial)` | `"inputtext"` | `NSTextField` 1つ。戻り値は文字列 or nil |
| `dialog_custom(spec)` | `"custom"` | チェックボックス・選択リスト（縦の行。行で Enter すると即確定）・複数ボタン。詳細は後述「選択リスト」 |
| `dialog_filter_list(spec)` | `"filterlist"` | 大量の文字列(`items`)から `fzf` 絞り込みで1件選択。戻り値は文字列 or nil。行の幅に収まらない文字列は先頭を「…」で省く（フォルダの履歴など、パスの一覧向け） |

`dialog_custom` の `spec` テーブル形式：
```lua
{
    title = "タイトル",
    message = "説明文",          -- optional
    checkboxes = { { label = "ラベル", checked = false } },
    select  = { options = {"A","B"}, selected = 1 },   -- optional。1ダイアログに1つ。selected は最初にカーソルがある行
    buttons = {"OK", "キャンセル"},                     -- optional。select があれば省略時はボタン無し、無ければ {"OK"}
}
-- ボタン・チェックボックスの label、select.options の `&x` は、ショートカット(x キーでその項目を選ぶ。後述「ダイアログ項目のショートカット」)
-- 戻り値: 行で Enter/クリックして閉じた → { select=2, checkboxes={false} }（button は無い）
--         ボタンで閉じた               → { button=1, checkboxes={false} }（select は無い）
--         Esc                          → nil
```

**注意（テキストフィールドとキー入力の関係）**：`NSTextField` がダイアログ内で first responder になっている間（`dialog_input`/`dialog_filter_list` 表示中など）、`MiataRootView::keyDown:`（`platforms/osx.mm`）は一切呼ばれない。つまり通常のキーバインド経路（`KeyBindingMap::Dialog` → `IDialog::Navigate()` → `DialogPanel::NavigateUp/Down/...`）はテキストフィールドには効かない。矢印キー/Enter/Escape をテキスト入力と共存させる必要がある場合は、`FilterListDialog.mm` のように `NSTextFieldDelegate` の `control:textView:doCommandBySelector:`（IME 変換中は呼ばれないため日本語入力と安全に共存できる）で個別に横取りする。

## 選択リスト（`dialog_custom` の `select`）

`dialog_custom` の `select = { options = {...}, selected = 1 }` は、選択肢を縦の行で並べ、カーソルを上下に動かして、行で Enter（またはクリック）すると、その場でダイアログを閉じて選択を確定する。ユーザー向けの仕様は README の `dialog_custom`。ここには、設計の決定と罠を書く。

- **構造**：`DialogPanel::AddSelectList`（`Dialog.mm`）が、リストを囲む細い箱（飾り。`focusables` に入れない）と、行（`_MiataSelectRow`。`NSControl` のサブクラス）を作る。行は `background` の**直接の子**で、他のコントロールと同じ `focusables` の1項目に登録する（カーソルのときの薄い塗りは、`UpdateFocusHighlight` が `isKindOfClass:` で行を見分けて付ける）。**行を箱の子にしない**：`NavigateDir` が、全 focusable の frame を `background` の座標系のまま比べるため。専用の部品にはせず（`FilterListDialog` の行リストとも共有せず）、`Dialog.mm` の中に閉じ込めた（十数行・1ダイアログ1リストで足りるため。スクロールや複数リストが要るようになったら、内部カーソルを持つ部品にする）
- **移動と Enter は、既存の仕組みをそのまま使う**（新しい移動のコードは無い）：行も、チェックボックスやボタンと同じ `focusables` の1項目で、`NavigateDir`（幾何学的な最近傍）が動かす。だから、リストの端を超えると、その先のチェックボックス・ボタンへ移る。折り返さない。リストへ戻ると、入ってきた側に近い行に着く（上から入れば先頭、下から入れば末尾。カーソルの記憶は無い）。行の高さは 22（`kSelectRowHeight`）、行間は 0（隙間があると、行の間のクリックが後ろの箱に当たる）。左右キーで隣の行へ動かないのは、行が同じ x・同じ幅で並び、左右の探索の帯が行の左端・右端に接するだけで重ならないため（`Overlaps` は厳密な不等号）。行の高さには依らない（22.3 や 22.7 でも、全行で動かないことを実測した）
- **Enter が届く先（実測）**：先頭ボタンの `keyEquivalent = "\r"`（「既定」の強調）は、Return を奪わない。`MiataRootView::keyDown:` が `super` を呼ばずに消費するので、AppKit の `performKeyEquivalent:` に回らない（`MiataRootView` が first responder の、confirm / yes_no / custom のダイアログで。合成した Return を `[window sendEvent:]` に送って確かめた）。先頭ボタンが既定でも、カーソルを Cancel に移して Return すると Cancel が押される（Lua の `<enter>` → `navigate_ok` → `NavigateOk` → カーソルの項目に `performClick:`）。`[window performKeyEquivalent:]` を直接呼ぶと既定のボタンが押されるが、通常のキー入力ではその経路に来ない。入力欄が first responder の `dialog_input` は、ヘッドレスではウィンドウがキーでないため、Return の挙動を再現できなかった（未確認）。矢印キーは、以前は Dialog モードに束縛が無く（ビープ）、`resources/test.lua` で `"nd"` にして Dialog モードにも束縛した（`bind` はモードごとに別々の参照を作るので、`unbind("n", "<down>")` で Normal の矢印だけを外しても、Dialog の矢印は残る。後述「キーバインド」）
- **確定の通知は次のランループ**：ボタンと同じ `_MiataControlTarget::fire:`（`MakeTarget`）が `dispatch_async` で逃がす。`NavigateOk` の中で同期的に閉じてはいけない（`Hide()` が `focusables` を clear する）。値（チェックボックス）は、`CloseDialog()` の前に読む（`Hide()` の後は `GetCheckbox` が false しか返さない）
- **閉じた後に届いた通知は捨てる**（`IDialog` のコンストラクタで `on_button_` に渡すラムダの `!is_opened_`。全ダイアログに効く）：`fire:` の block は `Hide()` で取り消されない。Enter の連打などで、確定が block の実行前に2回呼ばれると、2発目が閉じた後に届き、`OnButton` が `Hide()` で消えた値（チェックボックスは false、入力欄は ""）で結果を上書きしてしまう。`CustomDialog`（`double_fire`）と `InputTextDialog`（`double_fire_input`）で確かめた。行のダブルクリックでは起きにくい（1発目の block が走ると、行がビューから外れる）
- **結果は排他**：`CustomDialogResult` の `button_index` と `select_index` のどちらか一方（`std::optional`）。Lua では `result.button` か `result.select`。行の確定を `button = 1` にしない（ボタンが無いダイアログでも `button = 1` が返って紛らわしい）
- **`buttons` 省略時**：select があれば OK を足さない（閉じるのは行の Enter か Esc）。select があるとき、先頭ボタンの「既定」の強調も付けない（Enter の主な作用が行の確定で、強調された OK は「Enter で OK」と誤解させる。Enter はカーソルの項目に作用する）。強調されたボタンと、Enter が押す項目の食い違いは、`YesNoDialog` にもあった：`dialog_yes_no(msg, true, …)` は、強調が「はい」（ウィンドウの `defaultButtonCell`）でも、カーソルと Enter は先頭に登録した「いいえ」から始まった。`YesNoDialog::OnOpen` で、強調したボタンに `SetInitialFocus` して揃えた（`default_select_` が true なら YES）。`dialog_custom`（select の無いもの）は、最初の項目（チェックボックス、無ければ先頭のボタン）から始まり、強調された先頭ボタンとは別のまま
- **検証は厳格、`luaL_error` の前に**：`CheckCustomDialogSelect`（`Application.cc`）を、`lua_private_dialog_open` の `custom` の分岐の先頭、spec（`std::string` を持つ）を作る前に呼ぶ（`luaL_error` は longjmp。`type_string` は `std::string_view` にして、デストラクタを持つオブジェクトを、`luaL_error` の前に作らない）。Lua の C API と整数だけを使い、メッセージの書式は固定で、埋め込むのは整数だけ。`select` がテーブルでない・`options` がテーブルでない/空・文字列（数値も可）でない要素がある・`selected` が整数でない/範囲外はエラー（要素を黙って捨てると、行の番号がずれて、意図しない行が確定するため）。通った後の `ParseCustomDialogSpec` の select の読み取りは、エラーを投げない書き方で、`CustomDialog` は検証済みを前提にする（`selected` を丸めない）。`buttons` / `checkboxes` は寛容（型違いを黙って捨てる。捨てると添字がずれる同じ問題が残る。別件）
- **文字列の修復**：`dialog_custom` の Lua からの文字列（`title`・`message`・ボタン・チェックボックスのラベル・選択肢）は、`ParseCustomDialogSpec` で `RepairUtf8` に通す（不正なバイトのままだと NSString にできず、ラベルの作成（`labelWithString:nil`）で例外になる）。ほかの `dialog_*` の文字列は未対応（「名前が UTF-8 として不正なファイル」を参照）
- **見た目**：カーソル行は、既存の 2pt の枠（`UpdateFocusHighlight`）に加えて、`selectedContentBackgroundColor` の薄い塗り（`kCursorFillAlpha = 0.25`）。文字色は反転しない（不透明な選択色だと、Light で黒文字が青地に載ってコントラストが落ち、反転が要る）。リストは細い箱で囲む（箱が無いと、選べる行だと分からない）。カーソルがチェックボックスやボタンにあるときは、行はどれも塗られない（カーソルは1つ）。`FilterListDialog` の選択行（不透明な塗りだけ、枠なし、`pl_get_color`）とは見た目が違う：他のダイアログの項目の枠と揃えた。共通化するときは、揃えるかを決める。長い選択肢は、1行のまま末尾が「…」で省かれる（PNG で確認）
- **クリック**：`_MiataSelectRow` は `acceptsFirstResponder` が NO（キー入力は `MiataRootView` に届き続ける必要がある。`_MiataFileListNSView` と同じ）。`mouseDown:` は何もしない実装で上書きして `mouseUp:` を行に届け、`mouseUp:` が行の内側のときだけ確定する（押しただけ、外で離したときは何もしない）。セルを持たない `NSControl` なので、target/action は使わず、`activator`（`_MiataControlTarget`）を持つ。標準の `NSPopUpButton` が持っていたアクセシビリティの代わりに、ボタンとして読まれる自前の4メソッドを持つ（読み上げの内容は未確認。旧ポップアップが読んでいたグループ名・現在値は、読まれない）
- **名前**：行の確定も `on_button_` / `OnButton` / `button_id` に届く（ボタンだけだった頃の名前のまま）。3種類目の部品を足すときに、`on_activate_` / `OnActivate` などへ改名する
- **限界**：カーソルの記憶が無い。スクロールも高さの上限も無い（十数行まで。計算上、25 行前後で、既定のウィンドウより高くなる）。1ダイアログに1リスト。ホバー・押下中の表示・タイプしてジャンプは無い

## ダイアログ項目のショートカット（ラベルの `&x`）

ボタン・チェックボックス・選択リストの行のラベルに `&x` と書くと、ダイアログの表示中の `x` キーで、その項目を選んだことになる（カーソルをその項目に移して Enter を押したのと同じ作用）。ユーザー向けの仕様は README の「項目のショートカット」。ここには、設計の決定と罠を書く。

- **流れ**：`Application::KeyDown` の「割り当て無し」の枝（Dialog モード・キー列の長さ 1・修飾キー無し）→ `KeyBinding::KeyCodeToAscii(キーコード)` → `View::ActivateDialogMnemonic(char)` → `IDialog::ActivateMnemonic` → `DialogPanel::ActivateMnemonic`（`focusables` から探し、`focus_index` を移し、`UpdateFocusHighlight()`、`NavigateOk()`）。`NavigateOk` を使うので、「Enter と同じ」が構造で保証される（ボタン・行は `fire:` が次のランループへ逃がす。チェックボックスは同期で切り替わる）。`constants::Navigate` には足さない：Lua に `navigate_*` として公開される語彙で、文字を運べず、結果の bool も返せない。当たらなければ、これまでどおりビープする
- **キーバインド優先は、差し込み口の位置で成り立つ**：`KeyBinding::Has` は、完全一致なら割り当て（Lua の関数の参照）を返し（`KeyDown` が実行する）、より長い束縛の先頭なら `MaybeTooShort` を返し（`KeyDown` は何もせず、次のキーを待つ）、どちらでもないときだけ `NotFound` を返す。ショートカットは `NotFound` の枝（ビープの直前）でだけ試す。だから、ユーザーが Dialog モードに bind したキーは自動で優先され、`unbind` すればショートカットとして働く。Normal モードだけの束縛は、ダイアログの中では引かれないので、ショートカットに使える。**キー列の長さが 1 のときだけ試す**：`key_stroke_` は Normal と Dialog で共有され、タイムアウトも、ダイアログの開閉でのリセットも無い。Normal の `dd` の 1 打目が残ったままダイアログが開くと、次のキーは `[d, x]` として引かれて `NotFound` になる（既存の癖。Enter も 1 回食われる）。この 2 打目を、ショートカットとして試さない。複数キーの束縛が外れた 2 打目も同じ
- **キーは物理キーの位置**（キーバインドと同じ）：`KeyBinding::KeyCodeToAscii` は `ascii_to_keycode_` の逆引き。**表の値 0 は「割り当て無し」と `a` のキー（`kVK_ANSI_A` = 0）の両方**なので、0 を無効の印にしない。英数字（小文字と数字）の位置だけを調べる。`ParseKey` / `ParseTag` は `ascii_to_keycode_[(uint8_t)c]` で引くが、表は 128 個なので、128 以上のバイト（日本語など）をキーの文字列に書くと、範囲外を読む（既存の不具合。ラベルの文字は `ParseKey` に通さない）。Dvorak などでは、下線の文字とキートップがずれる（`j` `k` `h` `l` と同じ性質）。IME の状態には依らない（`MiataRootView` はキーコードしか受け取らない）。文字（`charactersIgnoringModifiers`）で比べる案は、`pl_set_key_down_handler` と `Application::KeyDown` の引数が変わり、キーバインドと流儀が割れるので採らなかった
- **解析は純関数**（`views/Mnemonic.h/.cc`、AppKit 非依存）：`ParseMnemonicLabel(raw)` → `{text, key, pos}`。`&&` = `&`。`&` + ASCII 英数字 = ショートカット（最初の 1 つだけ。2 つ目以降は `&` を取り除くだけ。`key` は小文字）。それ以外の `&`（`"Tom & Jerry"`、末尾の `&`、日本語の前の `&`）は、そのまま残す。`&`（0x26）は UTF-8 の連続バイトに現れないので、バイト単位で走査する（不正な UTF-8 でも範囲外を読まない）。`pos` はバイト位置で、下線の範囲には `UnderlineIndex`（`Dialog.mm`）で NSString の添字（UTF-16）に直す（絵文字などのサロゲートペアの後でもずれない）
- **解析する場所は、`DialogPanel::AddButton` / `AddCheckbox` / `AddSelectList` の入口だけ**：C++ のダイアログも Lua のダイアログも、ここを通る。`title` / `message` と、`dialog_filter_list` の `items` は解析しない（エラー文のパスや履歴のパスは、生のデータ）。`Impl::ParseLabel` が、同じ文字を先に登録した項目があれば、`key` を 0 にする（先勝ち。負けた項目は、`&` を取り除くだけで、下線もショートカットも無い。効かない下線を出さない）。登録順は、`CustomDialog` ならチェックボックス → 行 → ボタン、`YesNoDialog` なら「いいえ」→「はい」。台帳の `Focusable::mnemonic`（0 = 無し）に持つので、`Hide()` で `focusables` と一緒に消える。文字から項目を探すのは `Impl::IndexOfMnemonic` の 1 か所（重複判定と `ActivateMnemonic` が共用）で、**key が 0 のときは -1（`ActivateMnemonic(0)` は false）**：弾かないと、ショートカットを持たない全項目に当たる
- **下線**：ボタン・チェックボックスは、`attributedTitle` の複製に `NSUnderlineStyleAttributeName` を足す（新しい `NSAttributedString` を作ると、フォントなどを失う）。`title` はプレーンな文字列のまま（アクセシビリティもこちら）。**文字色の属性（`NSForegroundColorAttributeName`）は外す**：`attributedTitle` が持つ色は、読んだ時点の状態の値（実測: 窓に載る前の既定のボタンは `alternateSelectedControlTextColor`、載った後は `controlTextColor`）で、残すと、その時点に固定してしまう。色の属性が無い文字列は、セルが状態に応じた色で描く（実測: Light/Dark とも元のボタンと同じ色。赤などの具体的な色を明示すると、そのまま描かれる）。選択リストの行は自前描画なので、`_MiataSelectRow` の `underlineRange`（長さ 0 = 無し）を、`drawRect:` で属性つき文字列に足す（`drawInRect:` でも、長い選択肢の末尾の「…」は効く。PNG で確認）。AppKit の mnemonic の API（`setTitleWithMnemonic:` など）は使わない：SDK のヘッダに「Mnemonics, which are underlined characters in the button title that can be used as a keyboard shortcut, are not used on macOS」（`NSButton.h`）とあり、10.8 で deprecated（`NSCell.h`）。`&` を取り除いて `title` にするだけで、ショートカットにはならない
- **使えないもの**：既定の Dialog キー（`j` `k` `h` `l`、矢印、Enter、Esc）は、キーバインドが優先なので、`&j` は効かない（下線は付く。bind は実行時に変わるので、検出しない）。テンキーの数字、記号、日本語は、ショートカットにできない（`KeyCodeToAscii` が英数字だけ）。入力欄のあるダイアログは、入力欄が first responder の間 `MiataRootView::keyDown:` に来ないので、働かない（誤爆もしない）。キーリピートの `keyDown:` も `Application::KeyDown` に届く。リピートかどうか（`isARepeat`）は運んでいない（`pl_set_key_down_handler` の引数は、キーコードと修飾キーだけ）ので、通常の押下と区別できず、長押しで連続して発火する（開いたキーと同じ文字のショートカットを長押ししたとき、など）。閉じた直後（`current_dialog_` が残る最大 50ms）は、台帳が空なので当たらず、ビープになる
- **採らなかった案**：(a) `test.lua` で Dialog モードに a〜z・0〜9 を全部 `bind` して、新しい Lua コマンドを呼ぶ案：完全一致が先に勝つので、ユーザーが足した複数キーの束縛（`gg` など）の先頭を隠す（キーバインド優先を満たせない）。キーを押すたびに Lua のコルーチンも走る。(b) `NSButton.keyEquivalent`：`MiataRootView::keyDown:` が `super` を呼ばずに消費するので、`performKeyEquivalent:` に回らない。チェックボックスと行には使えない
- **既存の C++ の確認ダイアログ（上書き/スキップ、ゴミ箱へ/キャンセル、システム設定を開く/閉じる）には、付けていない**：うっかり押したキーで、削除などを確定する恐れがあるため。仕組みは `AddButton` の入口で全部に効くので、付けるときは、ラベルに `&x` を足すだけ。`resources/test.lua` では、ソート（`s`）と `<tab>` のデモに付けた
