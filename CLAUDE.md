# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## ビルドと実行

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

成果物は `_build/debug/miata.app` および `_build/release/miata.app` に生成される。

## アーキテクチャ概要

macOS 専用のファイルブラウザアプリケーション「Miata」。sokol + Dear ImGui でレンダリングし、Lua スクリプトでキーバインドとコマンドを定義する。

**技術スタック:**
- C++23、Objective-C++（macOS バインディング）
- レンダリング: sokol（Metal バックエンド）+ Dear ImGui
- スクリプト: Lua（コルーチンベースのダイアログ制御）
- リアクティブ: RxCpp（`ReactiveProperty` 等のユーティリティ）
- 外部依存はすべて `packages/` に Git submodule として存在

**MVC 構成:**
- `src/models/` — データモデル（ファイルリスト、ブラウザ状態）
- `src/views/` — UI レイヤー（`View`、`BrowserView`、`Dialog`）
- `src/widgets/` — 汎用 UI ウィジェット

**主要クラスの役割:**

| ファイル | 役割 |
|---------|------|
| `Application.cc/h` | フレームループ、イベント処理、コマンド登録（シングルトン） |
| `Script.cc/h` | Lua VM 管理、C++ 関数登録、`resources/` の読み込み |
| `KeyBinding.h` | キーストローク解析、モード別（Normal/Dialog）キーマップ管理 |
| `views/View.h` | ビューコンテナ、ダイアログスタック管理 |
| `views/Dialog.cc/h` | ダイアログ基底クラス（confirm/yes_no/input/custom） |
| `misc.h` | `Flags`、`ReactiveProperty`、`MessageBroker` などのユーティリティ |
| `platform.h` | macOS 固有のフォント検索・ファイル読み込み |

**Lua スクリプト:**
- `resources/base.lua` — コアユーティリティとダイアログヘルパー定義
- `resources/test.lua` — キーバインド設定とコマンド定義

C++ 側は `Miata.command.*`、`Miata.keymap.*` 等の名前空間で Lua 関数を登録し、スクリプト側から呼び出す。ダイアログはコルーチンで非同期制御される。

## sokol アップデート時の注意点

sokol を更新すると破壊的変更が入る場合がある。確認済みの変更点：

- **MTKView の廃止（Metal バックエンド）**：`contentView` は `NSView` + `CADisplayLink` になった。`mtk_view()` は削除済み。`ns_content_view()` を使う。
- **フォント API の変更**：`simgui_destroy_fonts_texture` / `simgui_create_fonts_texture` は廃止。`simgui_setup` で `.no_default_font = true` を指定し、`AddFontFromFileTTF()` を呼ぶだけでよい。
- **FPS / 一時停止制御**：`pl_start_update` / `pl_stop_update` / `pl_force_update` は sokol 内部の CADisplayLink にアクセスできないため現在 no-op。

## ネイティブダイアログ（NSAlert ベース）

ImGui の InputText は macOS IME（日本語入力）が正常動作しないため、テキスト入力・カスタムダイアログは `NSAlert` ネイティブ実装に移行済み。

| 関数（Lua） | 実装 | 説明 |
|------------|------|------|
| `Miata.command.dialog_input(message, initial)` | `pl_show_input_dialog()` | NSAlert + NSTextField。コルーチン不要 |
| `Miata.command.dialog_custom(spec)` | `pl_show_custom_dialog()` | NSAlert + チェックボックス・ポップアップ |

`dialog_custom` の `spec` テーブル形式：
```lua
{
    title = "タイトル",
    message = "説明文",          -- optional
    buttons = {"OK", "キャンセル"},
    checkboxes = { { label = "ラベル", checked = false } },
    selects   = { { label = "ラベル", options = {"A","B"}, selected = 1 } },
}
-- 戻り値: { button=1, checkboxes={false}, selects={2} } または nil
```

**注意**：`[NSAlert runModal]` はネストされたイベントループを起動するため、`Application::FrameImpl()` に再入ガード（`frame_running_`）が入っている。

## C++ から Lua へ関数を登録する手順

1. `Application.h` に `static int lua_XXX(lua_State* L)` を追加
2. `Application.cc` の `privates[]` または `commands[]` に `{ "name", lua_XXX }` を追加
3. `platform.h` / `osx.mm` にプラットフォーム実装を追加（Objective-C++ は `.mm`）
