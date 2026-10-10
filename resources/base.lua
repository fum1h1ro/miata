-- 多数のファイルを一度に開くとき(全マークなどで、誤って何百ものウィンドウを開かないように)の確認。続けてよいならtrue。
-- OPEN_CONFIRM_LIMIT 件までは、確認しない。ダイアログを待つので、キーに割り当てた関数の中(コルーチン)から呼ぶこと
local OPEN_CONFIRM_LIMIT = 10
local function confirm_open(count)
    if count <= OPEN_CONFIRM_LIMIT then return true end
    return Miata.command.dialog_yes_no(count .. " 件を開きますか？", false)
end

Miata = {
    command = {
        dialog_confirm = function(message, button_text)
            button_text = button_text or "OK"
            local handle = Miata._private.dialog_open("confirm", {
                message = message, button_text = button_text
            })
            coroutine.yield()
            coroutine.yield()
            while Miata._private.dialog_is_open(handle) do
                coroutine.yield()
            end
            return Miata._private.dialog_result(handle, "confirm")
        end,
        dialog_yes_no = function(message, default_focus, yes_text, no_text)
            yes_text = yes_text or "YES"
            no_text = no_text or "NO"
            local handle = Miata._private.dialog_open("yesno", {
                message = (message or "Are you sure?"), default_focus = default_focus, yes_text = yes_text, no_text = no_text
            })
            coroutine.yield()
            coroutine.yield()
            while Miata._private.dialog_is_open(handle) do
                coroutine.yield()
            end
            return Miata._private.dialog_result(handle, "yesno")
        end,
        dialog_input = function(message, initial_text)
            local handle = Miata._private.dialog_open("inputtext", {
                message = message, initial_text = initial_text or ""
            })
            coroutine.yield()
            coroutine.yield()
            while Miata._private.dialog_is_open(handle) do
                coroutine.yield()
            end
            return Miata._private.dialog_result(handle, "inputtext")
        end,
        dialog_custom = function(spec)
            local handle = Miata._private.dialog_open("custom", spec)
            coroutine.yield()
            coroutine.yield()
            while Miata._private.dialog_is_open(handle) do
                coroutine.yield()
            end
            return Miata._private.dialog_result(handle, "custom")
        end,
        -- items: 文字列の配列(必須)。title/message: 省略可。
        -- 例: Miata.command.dialog_filter_list({ items = history, title = "履歴" })
        -- 戻り値は選択した文字列、キャンセル時はnil。
        dialog_filter_list = function(spec)
            local handle = Miata._private.dialog_open("filterlist", spec)
            coroutine.yield()
            coroutine.yield()
            while Miata._private.dialog_is_open(handle) do
                coroutine.yield()
            end
            return Miata._private.dialog_result(handle, "filterlist")
        end,
        -- フォルダの履歴(左右のペインで共有する。新しい順)から、fzfで絞り込んで1つ選び、paneのペインをそのフォルダへ移す。
        -- paneは "left" / "right"(省略したら、いまカーソルのあるペイン)。履歴が空なら、案内を出すだけ。
        -- 履歴の中身は Miata.command.history_list(pane)(paneの今いるフォルダを除く)、移動は Miata.command.jump_to(path, pane)。
        -- ペインは最初に決めて、両方に渡す(ダイアログを開いている間も、同じペインを対象にする)。
        history = function(pane)
            pane = pane or Miata.command.current_pane()
            local items = Miata.command.history_list(pane)
            if #items == 0 then
                Miata.command.dialog_confirm("履歴がありません")
                return
            end
            local picked = Miata.command.dialog_filter_list({
                items = items,
                title = "履歴",
                message = pane == "left" and "左のペインの移動先を選択してください" or "右のペインの移動先を選択してください",
            })
            if picked then
                Miata.command.jump_to(picked, pane)
            end
        end,
        -- ファイルを開く・Finderで表示する・パスをクリップボードへ。targetは、対象のパスの文字列、エントリ({ path = ... }。
        -- cursor_entry の戻り値など)、またはそれらの配列(marked_entries の戻り値など)。対象は、いつも明示する
        -- (マーク済み、無ければカーソル下、にしたいときは、呼ぶ側で marked_entries と cursor_entry を組み合わせる)。
        -- 対象が空なら、何もせずfalse。ほかは、要求を出せたらtrue(開けたかは、あとでダイアログで知らされる)。
        -- 既定のアプリで開く(Finderのダブルクリックと同じ。フォルダはFinder、.appは起動)。
        -- 10件を超えるときは、確認する(ダイアログ)。
        open = function(target)
            local paths = Miata._private.paths_of(target)
            if #paths == 0 then return false end
            if not confirm_open(#paths) then return false end
            return Miata._private.open_paths(paths)
        end,
        -- appで開く。appは、アプリの名前("Visual Studio Code"。大文字小文字は区別しない)、Bundle ID("com.apple.TextEdit")、
        -- 絶対パス("/Applications/Foo.app")のどれか。名前は、標準の場所(~/Applications、/Applications、
        -- /Applications/Utilities、/System/Applications、/System/Applications/Utilities、/System/Library/CoreServices)から探す。
        -- 見つからなければ、ダイアログで知らせてfalse。10件を超えるときは、確認する。
        open_with = function(app, target)
            if type(app) ~= "string" or app == "" then
                error("open_with: expected an application (name, bundle identifier or absolute path)", 2)
            end
            local paths = Miata._private.paths_of(target)
            if #paths == 0 then return false end
            if not confirm_open(#paths) then return false end
            return Miata._private.open_paths(paths, app)
        end,
        -- Finderで、対象を選択した状態で表示する
        reveal = function(target)
            local paths = Miata._private.paths_of(target)
            if #paths == 0 then return false end
            return Miata._private.reveal_paths(paths)
        end,
        -- 対象のパスをクリップボードへ(複数は改行区切り)。名前だけ・フォルダのパスだけ、などは set_clipboard で自分で組む
        copy_path = function(target)
            local paths = Miata._private.paths_of(target)
            if #paths == 0 then return false end
            return Miata.command.set_clipboard(table.concat(paths, "\n"))
        end,
    },
    _private = {},
    util = {
        -- フォルダ dir の中の、名前 name のパス(make_directory / rename_to / exists などの、絶対パスの引数を組むとき)。
        -- name が "/" で始まる絶対パスなら、それ(dir は無視する。名前を入力させるダイアログに、絶対パスを入れたときの扱い)。
        -- dir がルート("/")のときは、"/" を重ねない。名前の正当性(空・"/" を含む、など)は見ない
        path_join = function(dir, name)
            if name:sub(1, 1) == "/" then return name end
            if dir:sub(-1) == "/" then return dir .. name end
            return dir .. "/" .. name
        end,
        inspect = function(val, indent)
            -- インデントの初期化
            indent = indent or 0
            local indent_str = string.rep("  ", indent)

            -- 結果を格納するテーブル
            local result = {}

            if (type(val) ~= "table") then
                -- テーブル以外の場合はそのまま文字列として返す
                return indent_str .. tostring(val)
            end

            -- テーブルの内容を走査
            for k, v in pairs(val) do
                local key_str = tostring(k)
                if type(v) == "table" then
                    -- 再帰的にテーブルの中身をダンプ
                    table.insert(result, string.format("%s[%s] = {", indent_str, key_str))
                    table.insert(result, Miata.util.inspect(v, indent + 1))
                    table.insert(result, string.format("%s}", indent_str))
                else
                    -- 値を文字列としてフォーマット
                    local value_str = type(v) == "string" and string.format("'%s'", v) or tostring(v)
                    table.insert(result, string.format("%s[%s] = %s,", indent_str, key_str, value_str))
                end
            end

            -- 結果を文字列として結合
            return table.concat(result, "\n")
        end,
        pp = function(val)
            print(Miata.util.inspect(val))
        end,
    },
    config = {
        color = {},
    },
}


