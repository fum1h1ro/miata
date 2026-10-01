# Miata

macOS 専用のキーボードドリブンなファイルブラウザ。
フルネイティブ AppKit で描画し、Lua スクリプトでキーバインドとコマンドを定義する。

## 技術スタック

- **言語**: C++23、Objective-C++
- **レンダリング**: フルネイティブ AppKit（`NSView.drawRect:` によるカスタム描画 + 標準コントロール。sokol/Dear ImGui は使用していない）
- **スクリプト**: Lua 5.x（コルーチンベースのダイアログ制御）
- **リアクティブ**: RxCpp（`ReactiveProperty` 等）
- **依存関係**: すべて `packages/` に Git submodule として存在（実際にビルドされるのは `lua` のみ）

## ビルド

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

成果物: `_build/debug/Miata.app` / `_build/release/Miata.app`

### アプリアイコン

`resources/AppIcon.icns` が `.app` のアイコンになる（`resources/` の中身は `Contents/Resources/` にコピーされ、`CMakeLists.txt` の `MACOSX_BUNDLE_ICON_FILE` で `Info.plist` に登録してある）。**今あるのは仮のアイコン**。差し替えるには、1024×1024 の正方形の PNG を用意して、次を実行する。

```bash
rake 'icon[path/to/icon.png]'   # resources/AppIcon.icns を作り直す(macOS 標準の sips と iconutil を使う)
rake build:debug                # ビルドし直す(rake build:release でもよい)
```

- 自分で作った `.icns`（Icon Composer や画像ツールの出力）を、`resources/AppIcon.icns` に直接置いてもよい
- macOS のアイコンは、角丸の形と余白（1024 の枠の中に 824 の角丸四角。macOS 11 以降の作法）を、画像の側に含める。システムは形を整えてくれない
- 正方形でない画像はエラーになる。1024 より小さい画像は、大きいサイズが引き伸ばされる（警告が出る）
- Dock や Finder に古いアイコンが出続けるときは、macOS のアイコンのキャッシュが残っていることがある（`.app` を `touch` して更新日時を変える、または Dock を再起動する）

## アーキテクチャ

```
src/
├── Application.cc/h      # 更新処理(0.05秒タイマー駆動)・イベント処理・コマンド登録（シングルトン）
├── Script.cc/h           # Lua VM 管理・C++ 関数登録・resources/ の読み込み
├── KeyBinding.h          # キーストローク解析・モード別キーマップ管理
├── misc.h                # Flags・ReactiveProperty・MessageBroker 等のユーティリティ
├── platform.h            # OS 依存処理の抽象化（`pl_*` 関数。色・フォント・ファイル操作・ディレクトリ監視・プロセス起動等）
├── models/               # データモデル（ファイルリスト・ブラウザ状態）
└── views/                # UI レイヤー（View・BrowserView・FileListView・QuickLookView・Dialog）

platforms/
└── osx.mm                # platform.h の macOS 実装。AppKit 型はここと views/*.mm にのみ閉じ込める

resources/
├── base.lua              # コアユーティリティ・ダイアログヘルパー定義
├── test.lua              # キーバインド設定・コマンド定義（ユーザー設定）
└── AppIcon.icns          # アプリアイコン（rake icon で作る）
```

## マウス操作

基本はキーボード操作で、マウスが使えるのは**マーク済みファイルのドラッグ&ドロップ**（Finder・ターミナル・メール・ブラウザ等、他アプリへの持ち出し）だけ。

- ファイル一覧上でマウスをドラッグすると、**そのペインのマーク済みファイル**をまとめてドラッグできる。どの行を押して始めても同じで、画面表示順に運ぶ。
- マークが1件も無いとき、およびダイアログ表示中は何も起きない。プレビュー（[Quick Look](#プレビューquick-look)）で覆っている範囲でも起きない。
- コピーになるか移動になるかはドロップ先と修飾キー（Option でコピー等）に従う。ドロップが受理されると、ファイルが移動されていれば一覧を再スキャンし（カーソルは維持され、移されなかったファイルのマークは残る）、そうでなければマークだけ解除する（実ファイルの状態を見て判断するため、反映は 0.3 秒ほど遅れる）。キャンセルされた場合はマークを維持する。
- 自アプリのもう一方のペインへのドロップには未対応（ペイン間のコピー/移動は `copy_marked` / `move_marked` を使う）。

## ディレクトリ監視（自動リロード）

各ペインは表示中のディレクトリを macOS の FSEvents で監視していて、外部（Finder・ターミナル・エディタ・ダウンロード等）で、直下のファイルが**追加・削除・改名・更新**されると、0.3 秒ほど待って自動で一覧を再読み込みする。カーソルとマークは `reload` と同じく維持される。サイズや更新日時の変化（追記、保存など）も自動で反映される。

- 短時間に続く変更は 1 回にまとめて反映する。変更が続く間も、再読み込みの間隔は最短 0.5 秒あける（走査が重いディレクトリでは、かかった時間の約 8 倍）
- ダイアログ表示中は反映を保留し、閉じた最初のティックで反映する（リネームの入力中にカーソルが動いて、別のファイルを改名してしまうのを避けるため）
- 見るのはディレクトリの**直下だけ**。サブディレクトリの中の変更では再読み込みしない
- 監視しているディレクトリ自体が消えた・移動された場合は、一覧はそのまま残る（再読み込みに失敗しても、何も表示しない）。同じ場所に作り直されると、自動で追従する
- ネットワークボリュームなど、他のマシンからの変更が通知されない場所では自動更新されない（`reload` で反映する）

## プレビュー（Quick Look）

カーソル下のファイルを、macOS 標準の Quick Look（Finder のクイックルックと同じ）で、ファイル一覧の上に被せて表示する。別ウィンドウは開かない。Lua の `quick_look` で出し入れする（`resources/test.lua` では `p` で両ペインに、`<S-p>` で反対側のペインだけに出す）。

- **プレビューするのは、カーソルのあるペインのカーソル下のファイル**（被せる範囲とは無関係）。カーソルを動かす・ペインを切り替える・ディレクトリを移動する・再読み込みでカーソルが動く、のどれでも、その時のファイルに追従する。動かし続けている間は切り替えず、止まって約 0.1 秒後に切り替える（高速に動かしたときに読み込みが空振りしないため）。一覧が空のときは空のプレビュー
- **被せる範囲**は、既定は両ペインにまたがる 1 枚。`"left"` / `"right"` を指定すると、そのペインだけに被せる（反対側のペインに出せば、カーソルのある一覧は見えたまま操作できる）
- **閉じる**のは、同じ指定でもう一度 `quick_look` を呼ぶか、`navigate_cancel`（`resources/test.lua` では Esc）。別の範囲を指定して呼ぶと、閉じずに範囲だけ切り替わる
- **キー入力を優先して、プレビューの中のマウス操作はできない**（PDF のスクロール、動画の再生ボタンなど）。プレビューはクリックでキーボードフォーカスを奪えてしまい、奪われるとキーバインドが効かなくなるため、クリックはプレビューに届かないようにしている。覆っている範囲では、マーク済みファイルのドラッグも効かない
- 見た目は macOS 標準で、`Miata.config.color` は効かない。表示は 1 件ずつ
- ファイルの中身だけが外部で更新されても、プレビューは自動では作り直さない（カーソル下のファイルが変わったときに作り直す）

## ウィンドウの位置とサイズ

ウィンドウを動かしたり大きさを変えたりするたびに位置とサイズを保存し、次回の起動で同じ位置・サイズで開く（Cmd+Q・ウィンドウを閉じる・強制終了のどれで終わっても、最後の状態が残る）。保存されたものが無いとき（初回）は、1024×768 で画面の中央に開く。

- 保存先は macOS の設定（`defaults`）で、`com.fum1h1ro.miata` の `NSWindow Frame MiataMainWindow`。デバッグ版とリリース版で共有する
- 保存した位置が今のどの画面にも無い場合（外付けモニタを外した後など）は、画面内に寄せて開く
- 初期状態（中央・1024×768）に戻すには、`defaults delete com.fum1h1ro.miata "NSWindow Frame MiataMainWindow"` を実行してから起動する
- 保存するのはウィンドウだけ。左右のペインの境界の位置と、各ペインのディレクトリは保存しない

## Lua スクリプト API

### キーバインド

```lua
Miata.command.bind(mode, keys, function() ... end)
Miata.command.unbind(mode, keys)

Miata.command.bind("n", "j", function()
    Miata.command.navigate_down()          -- j
end)
Miata.command.bind("n", "<C-d>", function()
    Miata.command.navigate_down(10)        -- Ctrl+d
end)
Miata.command.bind("n", "<S-k>", function()
    Miata.command.make_folder()            -- Shift+k
end)
Miata.command.bind("n", "dd", function()
    Miata.command.delete_marked()          -- d を 2 回
end)
Miata.command.bind("nd", "<enter>", function()
    Miata.command.navigate_ok()            -- Enter（一覧でもダイアログでも）
end)
```

#### モード（`mode`）

| 値 | 意味 |
|---|---|
| `"n"` | Normal（通常の一覧操作） |
| `"d"` | Dialog（ダイアログ表示中） |
| `"nd"` | 両方 |

- `"N"` `"D"` のように大文字で書いても同じ
- 空文字 `""` は何も登録しない（エラーにもならない）。`n` `d` 以外の文字を含めると `unknown map` のエラーになる
- テキスト入力欄にフォーカスがある間（`dialog_input`、`dialog_filter_list`）は、Dialog モードのキーバインドも効かない（テキスト入力が優先される）

#### キーの書き方（`keys`）

| 書き方 | 意味 |
|---|---|
| `j` `1` `;` など | 修飾キー無しのそのキー。**大文字は Shift ではない**（`"J"` は `"j"` と同じ） |
| `" "` | スペース（1 文字のスペース。`<space>` は使えない） |
| `<S-j>` | Shift+j |
| `<C-d>` | Ctrl+d |
| `<A-x>` | Alt（Option）+x |
| `<M-x>` | Command+x |
| `<C-S-k>` | 修飾キーは `-` でつないで複数指定できる。修飾キーは `S` `C` `A` `M`（小文字も可） |
| `<enter>` `<esc>` `<tab>` `<bs>` `<del>` `<up>` `<down>` `<left>` `<right>` `<f1>`〜`<f20>` | 特殊キー（`enter` は Return、`bs` は Delete（後退）、`del` は前方削除）。`<S-up>` のように修飾キーも付けられる |
| `dd` `gg` `<C-q>s` | 続けて押すキーの列（最大 4 キー）。途中までは次のキーを待ち、どの割り当てにも合わない列を押すとビープ音が鳴って最初からやり直しになる |

そのまま書ける記号は、スペース（`" "`）と `'` `,` `-` `.` `/` `;` `=` `[` `\` `]` とバッククォートだけ。キーは文字ではなく**物理キーの位置**（ANSI 配列のキーコード）で識別する。

**注意**

- 同じモードで同じキー列を二度 `bind` すると、後の方が**黙って上書き**する（エラーにならない）。大文字の `"H"` を Shift+h のつもりで書くと、`"h"` の割り当てを上書きしてしまう。Shift+h は `"<S-h>"` と書く
- Shift を押さないと出ない記号（`?` `!` `:` など）は、直接は書けない。表にない記号を直接書くと、**`a` のキーとして登録されてしまう**。ベースのキーに `<S-...>` を付けて書く（`?` は `<S-/>`）
- 特殊キーの表にない名前を `<...>` に書くと、先頭の 1 文字のキーとして扱われる（`<space>` は `s` のキーになる）
- 修飾キーの綴りを間違えた `<X-a>` や、中身が空の `<>` を書くと、Lua のエラーではなく**アプリが異常終了する**（既知の問題）

### ナビゲーション

```lua
Miata.command.navigate_down(n)   -- n 行下（省略時 1）
Miata.command.navigate_up(n)
Miata.command.navigate_left()
Miata.command.navigate_right()
Miata.command.navigate_ok()
Miata.command.navigate_cancel()
Miata.command.current_pane()     -- カーソルのあるペイン: "left" または "right"
```

ペインは `"left"` / `"right"` の文字列で表す。`current_pane()` の戻り値は、ペインを指定する引数（`reload` など）にそのまま渡せる。

### ダイアログ

ダイアログは `NSAlert` ではなく、非モーダルな `NSView` オーバーレイ（`DialogPanel`）で表示される。テキスト入力欄は本物の `NSTextField` を使うため macOS IME（日本語入力）にそのまま対応する。

#### `dialog_confirm`

```lua
Miata.command.dialog_confirm("メッセージ", "ボタンラベル")
```

#### `dialog_yes_no`

```lua
local result = Miata.command.dialog_yes_no("メッセージ", default, "はい", "いいえ")
-- result: true / false
```

#### `dialog_input`

```lua
local text = Miata.command.dialog_input("プロンプト", "初期値")
-- text: 入力文字列 または nil（キャンセル）
```

#### `dialog_custom`

チェックボックスとリスト選択を含むカスタムダイアログ。

```lua
local result = Miata.command.dialog_custom({
    title    = "タイトル",
    message  = "説明文",           -- optional
    buttons  = {"OK", "キャンセル"},
    checkboxes = {
        { label = "オプションA", checked = false },
        { label = "オプションB", checked = true  },
    },
    selects = {
        { label = "方法", options = {"高速", "標準", "低速"}, selected = 1 },
    },
})

-- result が nil ならキャンセル
-- result.button       -- 押されたボタンのインデックス（1始まり）
-- result.checkboxes   -- { true/false, ... }
-- result.selects      -- 選択インデックス（1始まり）の配列
```

#### `dialog_filter_list`

大量の文字列（`items`）から [fzf](https://github.com/junegunn/fzf) による絞り込みで1件選択する。フィルタ欄は常時表示され、上下矢印で選択移動、Enter で確定、Escape でキャンセルする。`fzf` が見つからない環境では大文字小文字を無視した部分一致に自動フォールバックする。

```lua
local picked = Miata.command.dialog_filter_list({
    items   = path_history,        -- 必須: 文字列の配列
    title   = "履歴",              -- optional
    message = "選択してください",   -- optional
})

-- picked: 選択した文字列 または nil（キャンセル）
```

### ファイル操作

```lua
Miata.command.mark()                -- カーソル位置のエントリをマーク
Miata.command.unmark()              -- マーク解除
Miata.command.toggle_mark()         -- マークのトグル
Miata.command.copy_marked()         -- マーク済み(無ければカーソル位置)を反対側のペインへコピー
Miata.command.move_marked()         -- 同、移動。コピー/移動とも、名前が衝突する場合は上書き確認ダイアログを出す
Miata.command.delete_marked()       -- マーク済みをゴミ箱へ移動(確認ダイアログあり)
Miata.command.reload(pane)          -- ペインのディレクトリを再読み込み(カーソルとマークは維持)。pane省略で現在のペイン
Miata.command.make_directory(name)  -- 現在のペインに新規フォルダを作成
Miata.command.make_folder()         -- 名前を入力ダイアログで聞いてから make_directory を呼ぶ
Miata.command.sort(key, reverse)    -- key: "name"/"size"/"mtime"/"ext"
```

ファイルを追加・削除・改名するこれらの操作（`copy_marked` / `move_marked` / `delete_marked` / `make_directory` / `rename`）の後も、一覧はカーソル位置とマークを維持したまま最新になる（`reload` と同じ仕組み）。カーソルのファイルが移動・削除で消えた場合は、次に残っているファイルへ寄る。`rename` のカーソルは新しい名前に付いていく。移動・削除に失敗したファイルはマークが残るので、そのまま再実行できる。`copy_marked` の後は、コピー元のマークだけが解除される。

#### `rename`

カーソル位置の単一エントリの名前を変更する。**マークが1件でもあれば何もしない**（複数選択時のリネームは未対応）。リネーム先の名前が既に存在する場合は上書きせず、衝突している旨のメッセージを添えて同じ入力ダイアログを開き直す。

```lua
Miata.command.rename()
```

#### `reload`

ペインのディレクトリを再スキャンして、一覧を最新にする。外部の変更は[ディレクトリ監視](#ディレクトリ監視自動リロード)で自動的に反映されるので、主に、監視できない場所（ネットワークボリュームなど）の変更を反映したいときや、自動更新を待たずに今すぐ更新したいときに使う。**カーソルとマークは、パスで同じファイルを引き継ぐ。**

- 引数 `pane` を省略（または `nil`）すると**現在のペイン**（カーソルのある方）が対象。`"left"` / `"right"` を渡すと、カーソルの位置に関係なくそのペインが対象になる。それ以外の値はエラー
- マーク済みのファイルが残っていれば、マークも残る。消えたファイルのマークは落ち、新しく現れたファイルは未マーク
- カーソルは同じファイルに追従する（並びが変わっても）。カーソルのファイルが消えていた場合は、再読み込み前の並びで次に残っているファイル（無ければその前）へ寄せる。対象が現在のペインでなくても同じ
- ディレクトリが消えた・読めない場合は、一覧を変えずに、失敗したディレクトリを示すエラーのダイアログを出す

成功なら `true`、失敗なら `false` を返す。`resources/test.lua` では `<C-r>`（現在のペイン）に割り当てている。

```lua
Miata.command.reload()          -- 現在のペイン
Miata.command.reload("left")    -- 左ペイン

-- 反対側のペインを再読み込みする
local other = Miata.command.current_pane() == "left" and "right" or "left"
Miata.command.reload(other)
```

### プレビュー

```lua
Miata.command.quick_look(pane)   -- カーソル下のファイルのプレビューを、一覧に被せて出す/閉じる
```

#### `quick_look`

カーソルのあるペインのカーソル下のファイルを、Quick Look のプレビューとして一覧の上に被せて表示する。動作の詳細は[プレビュー（Quick Look）](#プレビューquick-look)を参照。

- 引数 `pane` は**被せる範囲**。省略（または `nil`）と `"both"` は両ペインにまたがる 1 枚、`"left"` / `"right"` はそのペインだけ。それ以外の値はエラー。`"both"` 以外は `current_pane()` の戻り値と同じ表記なので、`current_pane()` と組み合わせて反対側のペインを指定できる
- 表示していないときに呼ぶと表示する。**表示中に同じ範囲で呼ぶと閉じる**。別の範囲で呼ぶと、閉じずに範囲だけ切り替わる
- 呼んだ後に表示中なら `true`、閉じたなら `false` を返す

```lua
Miata.command.quick_look()          -- 両ペインに被せる
Miata.command.quick_look("left")    -- 左ペインだけ
Miata.command.quick_look("right")   -- 右ペインだけ

-- 反対側のペインだけに被せる(カーソルのある一覧は見えたまま)
local other = Miata.command.current_pane() == "left" and "right" or "left"
Miata.command.quick_look(other)
```

### ユーティリティ

```lua
Miata.util.pp(value)          -- デバッグ出力（pretty print）
```

### 設定

```lua
Miata.config.color.background  = "#11223344"  -- RGBA hex
Miata.config.color.normal_text = "#aaff55ff"
Miata.config.color.normal_file = "#ffffffff"
Miata.config.color.directory   = "#00ffaaff"

Miata.config.set_font("フォント名")   -- 未指定時はシステムデフォルトフォント
Miata.config.set_font_size(14)        -- ファイル一覧の行の高さも連動して変わる
```
