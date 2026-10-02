-- 組み込みの既定の設定(.app の Contents/Resources に入る。直したらビルドし直す)。
-- 自分用の設定は ~/.config/miata/init.lua に書く。この後に読み込まれて、同じ設定を上書きする。

Miata.config.color.background = "#11223344"
Miata.config.color.normal_text = "#aaff55ff"
Miata.config.color.normal_file = "#ffffffff"
Miata.config.color.directory = "#00ffaaff"
-- 検索で一致した部分の背景色。search_current は、カーソルのある行(今いるマッチ)の一致部分
Miata.config.color.search_match = "#7a5c00ff"
Miata.config.color.search_current = "#c06000ff"
-- Miata.config.set_font("フォント名") / Miata.config.set_font_size(size) でファイル一覧のフォントを指定できる(未指定ならデフォルト)
-- Miata.config.set_history_limit(n) でフォルダの履歴の件数(ペインごと)を指定できる(0〜10000。未指定なら 100。0 なら記録しない)

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
-- プレビュー(Quick Look)。カーソル下のファイルを、引数なしなら両ペインに被せて表示する。もう一度押すか Esc で閉じる
Miata.command.bind("n", "p", function()
    Miata.command.quick_look()
end)
-- 反対側のペインだけに被せる(カーソルのある一覧は見えたまま)
Miata.command.bind("n", "<S-p>", function()
    local other = Miata.command.current_pane() == "left" and "right" or "left"
    Miata.command.quick_look(other)
end)
-- ファイル名の検索(vim の / n N)。/ でそのペインの下に入力欄が出て、打つたびにカーソルがマッチへ飛ぶ。
-- Enter で確定(n / N で次・前のマッチへ)、入力中の Esc で取り消し、通常時の Esc で検索を消す
Miata.command.bind("n", "/", function()
    Miata.command.search()
end)
Miata.command.bind("n", "n", function()
    Miata.command.search_next()
end)
Miata.command.bind("n", "<S-n>", function()
    Miata.command.search_prev()
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

-- フォルダの履歴(ペインごと。新しい順)。fzf で絞り込んで選ぶと、そのペインがそのフォルダへ移る
Miata.command.bind("n", "<S-h>", function()
    Miata.command.history()
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
