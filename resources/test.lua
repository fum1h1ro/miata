Miata.config.color.background = "#11223344"
Miata.config.color.normal_text = "#aaff55ff"
Miata.config.color.normal_file = "#ffffffff"
Miata.config.color.directory = "#00ffaaff"
-- Miata.config.set_font("フォント名") / Miata.config.set_font_size(size) でファイル一覧のフォントを指定できる(未指定ならデフォルト)

local command <const> = Miata.command

Miata.util.pp("HOGE")
Miata.util.pp(Miata)

command.bind("nd", "j", function()
    Miata.command.navigate_down()
end)
Miata.command.bind("nd", "k", function()
    Miata.command.navigate_up()
end)
Miata.command.bind("nd", "h", function()
    Miata.command.navigate_left()
end)
Miata.command.bind("nd", "l", function()
    Miata.command.navigate_right()
end)
Miata.command.bind("n", "<C-d>", function()
    Miata.command.navigate_down(10)
end)
Miata.command.bind("n", "<C-u>", function()
    Miata.command.navigate_up(10)
end)
Miata.command.bind("n", "<down>", function()
    Miata.command.navigate_down()
end)
Miata.command.bind("N", "<up>", function()
    Miata.command.navigate_up()
end)
Miata.command.bind("n", "<left>", function()
    Miata.command.navigate_left()
end)
Miata.command.bind("n", "<right>", function()
    Miata.command.navigate_right()
end)
Miata.command.bind("nd", "<enter>", function()
    Miata.command.navigate_ok()
end)
Miata.command.bind("nd", "<esc>", function()
    Miata.command.navigate_cancel()
end)
Miata.command.bind("n", "<tab>", function()
    --Miata.command.toggle_focus()

    --Miata.command.dialog_confirm("マジですか？", "yatta-")

    --local result = Miata.command.dialog_yes_no("マジですか？", true, "はい", "いいえ")
    --print("result:"..tostring(result))

    local result = Miata.command.dialog_custom({
        title = "設定",
        message = "詳細を選択してください", -- optional
        buttons = {"OK", "キャンセル"},
        checkboxes = {
            { label = "オプションA", checked = true },
            { label = "オプションB", checked = false },
        },
        selects = {
            { label = "方法", options = {"高速", "標準", "低速"}, selected = 1 },
        },
    })

    if result then
        print(result.button)          -- 1=OK, 2=キャンセル
        print(result.checkboxes[1])   -- true/false
        print(result.selects[1])      -- 1-based 選択インデックス
    end

    local text = Miata.command.dialog_input("入力してください", "")
    print(text)

    --Miata.command.dialog_custom("マジですか？", "yatta-")
end)
Miata.command.bind("n", " ", function()
    Miata.command.toggle_mark()
    Miata.command.navigate_down()
end)
Miata.command.bind("n", "c", function()
    Miata.command.copy_marked()
end)
Miata.command.bind("n", "m", function()
    Miata.command.move_marked()
end)
Miata.command.bind("n", "<S-k>", function()
    Miata.command.make_folder()
end)
Miata.command.bind("n", "r", function()
    Miata.command.rename()
end)
Miata.command.bind("n", "dd", function()
    Miata.command.delete_marked()
end)
Miata.command.bind("n", "<C-r>", function()
    Miata.command.reload()
end)
Miata.command.bind("n", "s", function()
    local result = Miata.command.dialog_custom({
        title = "ソート",
        buttons = {"OK", "キャンセル"},
        checkboxes = { { label = "降順", checked = false } },
        selects = { { label = "基準", options = {"名前", "サイズ", "更新日時", "拡張子"}, selected = 1 } },
    })
    if result and result.button == 1 then
        local keys = { "name", "size", "mtime", "ext" }
        Miata.command.sort(keys[result.selects[1]], result.checkboxes[1])
    end
end)

Miata.command.bind("n", "H", function()
    local items = {}
    for i = 1, 3000 do
        items[i] = string.format("/path/to/some/file_%d.txt", i)
    end
    table.insert(items, "/Users/example/デスクトップ/日本語のファイル名.txt")
    local picked = Miata.command.dialog_filter_list({
        items = items, title = "テスト", message = "選択してください"
    })
    if picked then
        print("picked: " .. picked)
    else
        print("cancelled")
    end
end)





Miata.command.bind("", "<C-q>s", function()
    print("C-q s")
end)
Miata.command.bind("", "<S-u>", function()
    print("S-u")
end)
Miata.command.bind("", "<S-up>", function()
    print("S-up")
end)

--Miata.command.unbind("n", "<S-up>")





--return false
