Miata = {
    command = {},
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


