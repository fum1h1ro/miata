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
-- Miata.config.set_history_limit(n) でフォルダの履歴の件数(左右のペイン合わせて)を指定できる(0〜10000。未指定なら 100。0 なら記録しない)

local command <const> = Miata.command

-- 操作の対象: マーク済みのエントリ。無ければカーソル下の1件(copy_marked / move_marked と同じ)。一覧が空なら、空の配列
local function targets()
    local list = Miata.command.marked_entries()
    if #list == 0 then
        local e = Miata.command.cursor_entry()
        if e then list = { e } end
    end
    return list
end

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
-- 矢印キーは、ダイアログ(Dialog モード)でも効く(j/k/h/l と同じ)
Miata.command.bind("nd", "<down>", function()
    Miata.command.navigate_down()
end)
Miata.command.bind("nd", "<up>", function()
    Miata.command.navigate_up()
end)
Miata.command.bind("nd", "<left>", function()
    Miata.command.navigate_left()
end)
Miata.command.bind("nd", "<right>", function()
    Miata.command.navigate_right()
end)
-- Enter: ダイアログでは、カーソルの項目を実行する。一覧では、フォルダなら入り、ファイルなら既定のアプリで開く
-- (.app などのパッケージは、フォルダとして入る。起動するには o)
Miata.command.bind("d", "<enter>", function()
    Miata.command.navigate_ok()
end)
Miata.command.bind("n", "<enter>", function()
    local e = Miata.command.cursor_entry()
    if e and not e.is_dir then
        Miata.command.open(e)
    else
        Miata.command.navigate_ok()
    end
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
        -- ラベルの `&x` は、ショートカット: x キーを押すと、その項目を選んだことになる(Enter と同じ。x に下線が付く)。
        -- `&&` は文字としての `&`。j k h l や矢印、Enter、Esc のように、Dialog モードで bind したキーは、そちらが優先される
        buttons = {"&OK", "キャンセル(&C)"},
        checkboxes = {
            { label = "オプション&A", checked = true },
            { label = "オプション&B", checked = false },
        },
        -- 選択リスト(縦の行)。j/k で動かして、行で Enter するとその場で閉じる
        select = { options = {"高速(&F)", "標準(&N)", "低速(&S)"}, selected = 1 },
    })

    if result then
        print(result.button)          -- ボタンで閉じたときの番号(1=OK, 2=キャンセル)。行で閉じたときは nil
        print(result.select)          -- 行で閉じたときの 1-based の行番号。ボタンで閉じたときは nil
        print(result.checkboxes[1])   -- true/false
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
-- 開く(o)・Finder で表示(O)・パスをクリップボードへ(yy)。対象は、マーク済み。無ければカーソル下の1件。
-- o は、フォルダも .app も開く(.app は起動、フォルダは Finder)。10 件を超えると、確認する
Miata.command.bind("n", "o", function()
    Miata.command.open(targets())
end)
Miata.command.bind("n", "<S-o>", function()
    Miata.command.reveal(targets())
end)
Miata.command.bind("n", "yy", function()
    Miata.command.copy_path(targets())
end)
-- アプリを指定して開く例(アプリの名前は、自分の環境に合わせる。名前・Bundle ID・絶対パスのどれでもよい):
-- Miata.command.bind("n", "e", function() Miata.command.open_with("Visual Studio Code", targets()) end)
-- Miata.command.bind("n", "t", function() Miata.command.open_with("Terminal", Miata.command.pane_path()) end)
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
    -- 基準の行で Enter すると、その場で並べ替える(Esc で取り消し)。ダイアログは、今のソート(current_sort)から始まる:
    -- カーソルは今の基準の行にあり、降順ならチェックが入っている。降順を切り替えるには、k でチェックボックスへ上がって
    -- Enter で切り替えてから、行へ戻る。ラベルの `&x` はショートカットで、x キーを押すと、その項目を選んだことになる:
    -- 行なら、その基準で並べ替えて閉じる(s のあとに s で、サイズ順)。チェックボックスなら、d で降順を切り替える
    local keys = { "name", "size", "mtime", "ext" }
    local key, reverse = Miata.command.current_sort()
    local selected = 1
    for i, k in ipairs(keys) do
        if k == key then selected = i end
    end
    local result = Miata.command.dialog_custom({
        title = "ソート",
        checkboxes = { { label = "降順(&D)", checked = reverse } },
        select = { options = {"名前(&N)", "サイズ(&S)", "更新日時(&M)", "拡張子(&E)"}, selected = selected },
    })
    if result and result.select then
        Miata.command.sort(keys[result.select], result.checkboxes[1])
    end
end)

-- フォルダの履歴(左右のペインで共有する。新しい順)。fzf で絞り込んで選ぶと、カーソルのあるペインがそのフォルダへ移る
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
