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
        make_folder = function()
            local name = Miata.command.dialog_input("新しいフォルダ名を入力してください", "")
            if name and name ~= "" then
                Miata.command.make_directory(name)
            end
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


