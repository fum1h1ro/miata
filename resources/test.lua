-- 組み込みの既定の設定(.app の Contents/Resources に入る。直したらビルドし直す)。
-- 自分用の設定は ~/.config/miata/init.lua に書く。この後に読み込まれて、同じ設定を上書きする。

Miata.config.color.background = "#11223344"
Miata.config.color.normal_text = "#aaff55ff"
Miata.config.color.normal_file = "#ffffffff"
Miata.config.color.directory = "#00ffaaff"
-- 検索で一致した部分の背景色。search_current は、カーソルのある行(今いるマッチ)の一致部分
Miata.config.color.search_match = "#7a5c00ff"
Miata.config.color.search_current = "#c06000ff"
-- 絞り込みで一致した部分の背景色(検索の色と同時に出るので、別の色)
Miata.config.color.filter_match = "#5a2d82ff"
-- 一覧の右側の札の文字色: <LNK>(シンボリックリンク)と <ALIAS>(Finder のエイリアス)と <CLOUD>(クラウドストレージのダウンロード前のファイル)の、
-- 札の部分だけ(名前と更新日時は行の色のまま。<DIR> とサイズも)
Miata.config.color.symlink = "#ffd24dff"
Miata.config.color.alias = "#ff8ad8ff"
Miata.config.color.cloud = "#7ec8ffff"
-- ステータスバー(ウィンドウの下端)専用の色。背景は、alpha を使わず不透明に塗る(background と同じ)。右側のマークの要約と上端の線は、
-- 文字色(statusbar_text)の薄い色。既定は background / normal_text と同じ値(独立した設定なので、background などを変えても、
-- これは動かない。合わせたいときは、こちらも書き換える)
Miata.config.color.statusbar_background = "#112233ff"
Miata.config.color.statusbar_text = "#aaff55ff"
-- コンソール(ブラウザ領域とステータスバーの間のログのペイン)の、エラーの行(赤い行: Lua のエラー・警告・ファイル操作の失敗・標準エラー)の
-- 文字色。背景とふつうの行の文字は、background / normal_text と同じ(フォントも、一覧と同じ)
Miata.config.color.console_error = "#ff6b6bff"
-- Miata.config.set_font("フォント名") / Miata.config.set_font_size(size) でファイル一覧のフォントを指定できる(未指定ならデフォルト)
-- Miata.config.set_statusbar_font_size(size) でステータスバーの文字のサイズ(pt。1〜200。未指定なら 12。set_font_size とは独立。バーの高さも連動する)を指定できる(ファミリーは set_font と共通)
-- Miata.config.set_history_limit(n) でフォルダの履歴の件数(左右のペイン合わせて)を指定できる(0〜10000。未指定なら 100。0 なら記録しない)
-- Miata.config.set_console_max_lines(n) でコンソール(標準出力・標準エラー・print・エラーのログ)が持つ行数の上限を指定できる(1〜100000。未指定なら 1000。超えたら古い行から捨てる)
-- Miata.config.set_console_height(n) でコンソールを開いたときの高さ(行数。1〜100。未指定なら 10。窓が低いときは縮む)を指定できる
-- Miata.config.set_show_console(true/false) でコンソールを出すかを指定できる(未指定なら出さない。zc で実行中に切り替えられる。
-- いまの状態は Miata.config.get_show_console() で読める)。console_clear() / console_copy() / console_scroll(n) は、Miata.command の中。
-- キーで動かすなら、例えば: Miata.command.bind("n", "zj", function() Miata.command.console_scroll(5) end)
-- Miata.config.set_show_icons(true/false) でファイル名の頭のアイコン(Finder と同じ)を出すかを指定できる(未指定なら出す。. で実行中に切り替えられる。いまの状態は Miata.config.get_show_icons() で読める。
-- 実行中に set_show_icons を呼んでも、値が変わったら、すぐ描き直す)
-- Miata.config.set_show_hidden(true/false) で隠しファイル(名前の頭が . のものと、Finder の「隠す」フラグが付いたもの。~/Library など)を
-- 出すかを指定できる(未指定なら隠す。zh で実行中に切り替えられる。いまの状態は Miata.config.get_show_hidden() で読める。
-- 実行中に set_show_hidden を呼んでも、値が変わったら、すぐ一覧を作り直す)
-- Miata.config.set_editor('コマンド') で、e(edit)が使うエディタを指定できる。%f を含む一行のコマンドで、%f が、対象のファイルに
-- 置き換わる(複数のときは、1 件ずつ別の引数になる。名前の中の $() や引用符は、コマンドとして解釈されない。%f は引用符の外に書く
-- ("%f" や '%f' はエラー)。%f が無ければ末尾に足す。%% は %)。
-- 未指定なら 'open -t %f'(システムの既定のテキストエディタ)。いまの値は Miata.config.get_editor() で読める。例:
--   Miata.config.set_editor('open -a "Visual Studio Code" %f')
--   Miata.config.set_editor('/opt/homebrew/bin/zed %f')

local command <const> = Miata.command

-- 操作の対象: マーク済みのエントリ。無ければカーソル下の1件(コピー・移動の c / m と、開く o ・Finder で表示 O ・パスのコピー yy が使う)。
-- 一覧が空なら、空の配列
local function targets()
    local list = Miata.command.marked_entries()
    if #list == 0 then
        local e = Miata.command.cursor_entry()
        if e then list = { e } end
    end
    return list
end

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
-- gg: 先頭の行へ / G(Shift+g): 末尾の行へ(vim と同じ)。絞り込み中は、見えている行の先頭・末尾。一覧だけ(ダイアログでは効かない)
-- bind の4番目の引数は、割り当ての説明(任意)。複数のキーを打つ割り当ての途中で、ステータスバーの「次のキー候補」に出る
Miata.command.bind("n", "gg", function()
    Miata.command.navigate_top()
end, "先頭の行へ")
Miata.command.bind("n", "<S-g>", function()
    Miata.command.navigate_bottom()
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
-- c: コピー / m: 移動。対象は、マーク済み(見えている行のマークだけ)。無ければカーソル下の1件。宛先は、反対側のペインのフォルダ。
-- 1. 始める前に断る組み合わせ(同じフォルダ・フォルダを自分の中へ・移動で、先の同名のフォルダが元の祖先)は、check_transfer が
--    調べて、理由を知らせて終わる。上書きの確認より前にやる(答えても進められない質問を、しないため)
-- 2. 先に同名があれば、上書き(はい)かスキップ(いいえ。Esc も)かを聞く(操作全体で1つ。既定はスキップ)
-- 3. copy_to / move_to が、裏スレッドで始める(進捗パネルが出る。完了で、一覧の更新と、コピーしたファイルのマーク解除)
local function transfer(kind, run)
    local list = targets()
    if #list == 0 then return end
    local other = Miata.command.current_pane() == "left" and "right" or "left"
    local dest = Miata.command.pane_path(other)
    local ok, info = Miata.command.check_transfer(kind, list, dest)
    if not ok then
        Miata.command.dialog_confirm(info) -- 断る理由
        return
    end
    local overwrite = false
    if #info > 0 then -- info は、先に同名があるものの、先のパスの配列
        overwrite = Miata.command.dialog_yes_no(#info .. "個のファイルが既に存在します。上書きしますか？", false, "上書き", "スキップ")
    end
    run(list, dest, { overwrite = overwrite })
end
Miata.command.bind("n", "c", function()
    transfer("copy", Miata.command.copy_to)
end)
Miata.command.bind("n", "m", function()
    transfer("move", Miata.command.move_to)
end)
-- K: 新しいフォルダを作る。名前を聞いて、カーソルのあるペインのフォルダの中に作る(名前に絶対パスを入れれば、その場所に作る)。
-- 場所(ペインのフォルダ)は、ダイアログを開く前に決める
Miata.command.bind("n", "<S-k>", function()
    local dir = Miata.command.pane_path()
    local name = Miata.command.dialog_input("新しいフォルダ名を入力してください", "")
    if name and name ~= "" then
        Miata.command.make_directory(Miata.util.path_join(dir, name))
    end
end)
-- r: カーソルのファイルの名前を変える(入力欄の初めの値は、今の名前)。見えているマークがあれば何もしない(単一のファイルだけ。
-- 隠れているマーク(絞り込みで外れた行・隠しファイル)は数えない)。同名のファイル/フォルダが既にあれば、上書きせず、同じ入力ダイアログを開き直す。
-- 名前に絶対パスを入れれば、その場所へ移す(同じボリュームの中)
Miata.command.bind("n", "r", function()
    if #Miata.command.marked_entries() > 0 then return end
    local entry = Miata.command.cursor_entry()
    if not entry then return end
    local dir = Miata.command.pane_path()
    local message = "リネーム"
    local new_name = entry.name
    local new_path
    while true do
        new_name = Miata.command.dialog_input(message, new_name)
        if not new_name or new_name == "" or new_name == entry.name then return end
        new_path = Miata.util.path_join(dir, new_name)
        if not Miata.command.exists(new_path) then break end
        message = "リネーム（同名のファイル/フォルダが既に存在します）"
    end
    Miata.command.rename_to(entry, new_path)
end)
-- dd: マーク済み(見えている行のマークだけ)をゴミ箱へ。確認する。マークが無ければ何もしない(カーソル下の1件にはしない)。
-- 確認の前に対象を決める(marked_entries)。確認の後の trash は、そのパスに対して動く(ダイアログの間に一覧が変わっても、
-- 確認したファイルが対象)。trash は配列を渡せる(1回で、再読み込みも失敗のダイアログも1回にまとまる)
Miata.command.bind("n", "dd", function()
    local list = Miata.command.marked_entries()
    if #list == 0 then return end
    local message = #list .. "件をゴミ箱に移動しますか？"
    -- 隠れているマーク(絞り込みで外れた行・隠しファイルを隠しているときの隠しファイル)は、対象にならない
    -- (見えていないものを、うっかり消さないため)。件数を知らせる
    local hidden = Miata.command.hidden_mark_count()
    if hidden > 0 then
        message = message .. "\n(隠れているマーク " .. hidden .. " 件は対象外です)"
    end
    if Miata.command.dialog_yes_no(message, false, "ゴミ箱へ", "キャンセル") then
        Miata.command.trash(list)
    end
end, "マークをゴミ箱へ")
Miata.command.bind("n", "<C-r>", function()
    Miata.command.reload()
end)
-- ファイル名の頭のアイコンの表示を入り切りする(AFXW の . と同じ)。set_show_icons は、実行中に呼ぶと、値が変わったら
-- すぐ描き直す。切り替えは保存しない(起動時の状態は、上の set_show_icons が決める)
Miata.command.bind("n", ".", function()
    Miata.config.set_show_icons(not Miata.config.get_show_icons())
end)
-- 隠しファイル(名前の頭が . のもの・Finder の「隠す」フラグが付いたもの。~/Library など)の表示を入り切りする(ranger / lf と同じ zh)。
-- 隠しているあいだは、検索・絞り込み・まとめてマーク・コピー・移動・ゴミ箱の対象からも外れる(絞り込みで隠れた行と同じ)。
-- set_show_hidden は、実行中に呼ぶと、値が変わったらすぐ一覧を作り直す(カーソルは同じファイルに留まる)。
-- 切り替えは保存しない(起動時の状態は、上の set_show_hidden が決める)
Miata.command.bind("n", "zh", function()
    Miata.config.set_show_hidden(not Miata.config.get_show_hidden())
end, "隠しファイルの表示を切り替え")
-- ステータスバー(ウィンドウの下端)の表示を入り切りする(zs)。既定は出す(起動時の状態は、set_show_statusbar が決める)。
-- set_show_statusbar は、実行中に呼ぶと、値が変わったらすぐ反映される(バーの高さは、一覧に戻る)。切り替えは保存しない
Miata.command.bind("n", "zs", function()
    Miata.config.set_show_statusbar(not Miata.config.get_show_statusbar())
end, "ステータスバーの表示を切り替え")
-- コンソール(ブラウザ領域とステータスバーの間の、ログのペイン)の表示を入り切りする(zc)。既定は出さない(起動時の状態は、
-- set_show_console が決める)。標準出力・標準エラー・print・Lua のエラー・ファイル操作の失敗が、ここに流れる。
-- set_show_console は、実行中に呼ぶと、値が変わったらすぐ反映される(一覧の高さは、コンソールの分だけ縮む)。切り替えは保存しない
Miata.command.bind("n", "zc", function()
    Miata.config.set_show_console(not Miata.config.get_show_console())
end, "コンソールの表示を切り替え")
-- プレビュー(Quick Look)。カーソル下のファイルを、引数なしなら両ペインに被せて表示する。もう一度押すか Esc で閉じる
Miata.command.bind("n", "p", function()
    Miata.command.quick_look()
end)
-- 反対側のペインだけに被せる(カーソルのある一覧は見えたまま)
Miata.command.bind("n", "<S-p>", function()
    local other = Miata.command.current_pane() == "left" and "right" or "left"
    Miata.command.quick_look(other)
end)
-- プレビューの倍率は、トラックパッドのピンチでも変えられる(xlsx・docx・csv・html・svg・txt・md・rtf など、ズームできる種類だけ。
-- 画像・PDF・json は、できない)。キーは付けていないので、使うなら ~/.config/miata/init.lua に、例えば次のように書く:
-- Miata.command.bind("n", "=", function() Miata.command.quick_look_zoom("in") end)
-- Miata.command.bind("n", "-", function() Miata.command.quick_look_zoom("out") end)
-- Miata.command.bind("n", "0", function() Miata.command.quick_look_zoom("reset") end)
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
end, "パスをクリップボードへ")
-- e: 対象(マーク済み。無ければカーソル下の1件)を、エディタで開く。エディタは Miata.config.set_editor で決める
-- (%f を含む一行のコマンド。未設定なら、システムの既定のテキストエディタ)。10 件を超えると、確認する
Miata.command.bind("n", "e", function()
    Miata.command.edit(targets())
end)
-- アプリを指定して開く例(アプリの名前は、自分の環境に合わせる。名前・Bundle ID・絶対パスのどれでもよい):
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
-- 絞り込み(名前に語を含む行だけを一覧に出す)。f でそのペインの下(検索バーの上)に入力欄が出て、打つたびに一覧が絞られる。
-- Enter で確定(別のフォルダへ移動するまで続く。通常時の Esc では解除されない)、入力中の Esc で取り消し(前の絞り込みに戻る)。
-- 解除は、もう一度 f を押して語を空のまま Enter。絞り込んだ上で mark_all などを使うと、見えている行だけが対象になる。
-- 入力欄を使わずに絞り込むには Miata.command.filter_set("語", [pane], [mode])、解除には Miata.command.filter_clear([pane])
Miata.command.bind("n", "f", function()
    Miata.command.filter()
end)
-- あいまい一致で絞り込む(F)。外部の fzf を使う(無ければ部分一致になる)。一覧は fzf の得点順で、一致した部分は強調しない。
-- 語には fzf の拡張検索の構文が使える(空白区切りで AND、^先頭、末尾$、'完全一致、!否定、|OR)。それ以外は f と同じ
Miata.command.bind("n", "<S-f>", function()
    Miata.command.filter("fuzzy")
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
