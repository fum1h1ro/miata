# Miata

macOS 専用のキーボードドリブンなファイルブラウザ。
sokol + Dear ImGui でレンダリングし、Lua スクリプトでキーバインドとコマンドを定義する。

## 技術スタック

- **言語**: C++23、Objective-C++
- **レンダリング**: [sokol](https://github.com/floooh/sokol)（Metal バックエンド）+ [Dear ImGui](https://github.com/ocornut/imgui)
- **スクリプト**: Lua 5.x（コルーチンベースのダイアログ制御）
- **リアクティブ**: RxCpp（`ReactiveProperty` 等）
- **依存関係**: すべて `packages/` に Git submodule として存在

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

成果物: `_build/debug/miata.app` / `_build/release/miata.app`

## アーキテクチャ

```
src/
├── Application.cc/h      # フレームループ・イベント処理・コマンド登録（シングルトン）
├── Script.cc/h           # Lua VM 管理・C++ 関数登録・resources/ の読み込み
├── KeyBinding.h          # キーストローク解析・モード別キーマップ管理
├── misc.h                # Flags・ReactiveProperty・MessageBroker 等のユーティリティ
├── platform.h            # macOS 固有 API の抽象化（フォント検索・ダイアログ等）
├── models/               # データモデル（ファイルリスト・ブラウザ状態）
├── views/                # UI レイヤー（View・BrowserView・Dialog）
└── widgets/              # 汎用 UI ウィジェット

platforms/
└── osx.mm                # macOS 固有実装（IME・ネイティブダイアログ・ウィンドウ操作）

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

すべてのダイアログはネイティブ NSAlert で表示される（macOS IME 対応）。

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

### ユーティリティ

```lua
Miata.util.pp(value)          -- デバッグ出力（pretty print）
Miata.command.toggle_mark()   -- 選択マークのトグル
```

### 設定

```lua
Miata.config.color.background  = "#11223344"  -- RGBA hex
Miata.config.color.normal_text = "#aaff55ff"
Miata.config.color.normal_file = "#ffffffff"
Miata.config.color.directory   = "#00ffaaff"
```
