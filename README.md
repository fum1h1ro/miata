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
```

成果物: `_build/debug/Miata.app` / `_build/release/Miata.app`

## アーキテクチャ

```
src/
├── Application.cc/h      # 更新処理(0.05秒タイマー駆動)・イベント処理・コマンド登録（シングルトン）
├── Script.cc/h           # Lua VM 管理・C++ 関数登録・resources/ の読み込み
├── KeyBinding.h          # キーストローク解析・モード別キーマップ管理
├── misc.h                # Flags・ReactiveProperty・MessageBroker 等のユーティリティ
├── platform.h            # OS 依存処理の抽象化（`pl_*` 関数。色・フォント・ファイル操作・プロセス起動等）
├── models/               # データモデル（ファイルリスト・ブラウザ状態）
└── views/                # UI レイヤー（View・BrowserView・FileListView・Dialog）

platforms/
└── osx.mm                # platform.h の macOS 実装。AppKit 型はここと views/*.mm にのみ閉じ込める

resources/
├── base.lua              # コアユーティリティ・ダイアログヘルパー定義
└── test.lua              # キーバインド設定・コマンド定義（ユーザー設定）
```

## Lua スクリプト API

### キーバインド

```lua
-- モード: "n"=Normal, "d"=Dialog, "nd"=両方, ""=全モード
Miata.command.bind("n", "j", function()
    Miata.command.navigate_down()
end)
Miata.command.unbind("n", "j")
```

### ナビゲーション

```lua
Miata.command.navigate_down(n)   -- n 行下（省略時 1）
Miata.command.navigate_up(n)
Miata.command.navigate_left()
Miata.command.navigate_right()
Miata.command.navigate_ok()
Miata.command.navigate_cancel()
```

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
Miata.command.make_directory(name)  -- 現在のペインに新規フォルダを作成
Miata.command.make_folder()         -- 名前を入力ダイアログで聞いてから make_directory を呼ぶ
Miata.command.sort(key, reverse)    -- key: "name"/"size"/"mtime"/"ext"
```

#### `rename`

カーソル位置の単一エントリの名前を変更する。**マークが1件でもあれば何もしない**（複数選択時のリネームは未対応）。リネーム先の名前が既に存在する場合は上書きせず、衝突している旨のメッセージを添えて同じ入力ダイアログを開き直す。

```lua
Miata.command.rename()
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
