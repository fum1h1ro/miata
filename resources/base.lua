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
        -- フォルダの履歴(ペインごと。新しい順)から、fzfで絞り込んで1つ選び、そのペインをそのフォルダへ移す。
        -- paneは "left" / "right"(省略したら、いまカーソルのあるペイン)。履歴が空なら、案内を出すだけ。
        -- 履歴の中身は Miata.command.history_list(pane)、移動は Miata.command.jump_to(path, pane)。
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
                title = pane == "left" and "履歴（左のペイン）" or "履歴（右のペイン）",
                message = "移動先のフォルダを選択してください",
            })
            if picked then
                Miata.command.jump_to(picked, pane)
            end
        end,
        make_folder = function()
            local name = Miata.command.dialog_input("新しいフォルダ名を入力してください", "")
            if name and name ~= "" then
                Miata.command.make_directory(name)
            end
        end,
        -- マークがあれば何もしない(単一ファイルのリネームのみ対応)。
        -- 同名のファイル/フォルダが既に存在する場合は、上書きせず同じ入力ダイアログを開き直す。
        rename = function()
            local current = Miata._private.rename_target()
            if not current then return end

            local message = "リネーム"
            local new_name = current
            while true do
                new_name = Miata.command.dialog_input(message, new_name)
                if not new_name or new_name == "" or new_name == current then
                    return
                end
                if not Miata._private.rename_conflict(new_name) then
                    break
                end
                message = "リネーム（同名のファイル/フォルダが既に存在します）"
            end
            Miata._private.rename_execute(new_name)
        end,
    },
    _private = {},
    util = {
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


