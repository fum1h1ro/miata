-- 多数のファイルを一度に開くとき(全マークなどで、誤って何百ものウィンドウを開かないように)の確認。続けてよいならtrue。
-- OPEN_CONFIRM_LIMIT 件までは、確認しない。ダイアログを待つので、キーに割り当てた関数の中(コルーチン)から呼ぶこと
local OPEN_CONFIRM_LIMIT = 10
local function confirm_open(count)
    if count <= OPEN_CONFIRM_LIMIT then return true end
    return Miata.command.dialog_yes_no(count .. " 件を開きますか？", false)
end

-- エディタのコマンド(edit が使う)。%f を含む一行のコマンドで、Miata.config.set_editor で決める。
-- 既定は、システムの既定のテキストエディタで開く(open -t。通常は TextEdit)
local DEFAULT_EDITOR = "open -t %f"
local editor_command = DEFAULT_EDITOR

-- エディタのコマンドの検査。正しければ nil、正しくなければ理由(文字列)を返す。
-- 空白だけでない文字列で、NUL を含まず、% の後ろが f か % だけ(%f = 対象のパス、%% = % そのもの)。
-- **%f は、シェルの引用符('…' と "…")の外に書く**(引用符の中と、バックスラッシュの直後は、エラー)。
-- %f は "$@"(位置パラメータ)に置き換わり、引用符の中に置くと、意図と違う語の分かれ方になる
local function editor_command_problem(command)
    if type(command) ~= "string" or command:find("^%s*$") then
        return "expected a command line such as 'open -a \"Visual Studio Code\" %f' (a non-empty string)"
    end
    if command:find("\0", 1, true) then
        return "the command line must not contain NUL characters"
    end
    -- 左から 1 文字ずつ読む。シェルの引用符の状態(quote: nil=外 / "'" / '"')を追い、% と次の 1 文字を組にして調べる
    -- (%%x は、% そのものと x。%x ではない)
    local quote = nil
    local i, n = 1, #command
    while i <= n do
        local c = command:sub(i, i)
        if c == "\\" and quote ~= "'" then
            -- バックスラッシュは、次の 1 文字を、ただの文字にする(シングルクォートの中を除く)。% の組は、次の周で読む
            local d = command:sub(i + 1, i + 1)
            if d == "%" then
                if command:sub(i + 1, i + 2) == "%f" then
                    return "%f must not directly follow a backslash"
                end
                i = i + 1
            else
                i = i + 2
            end
        elseif c == "'" and quote ~= '"' then
            if quote == "'" then quote = nil else quote = "'" end
            i = i + 1
        elseif c == '"' and quote ~= "'" then
            if quote == '"' then quote = nil else quote = '"' end
            i = i + 1
        elseif c == "%" then
            local d = command:sub(i + 1, i + 1)
            if d == "f" then
                if quote then
                    return "%f must be written outside quotes (write code %f, not code \"%f\" or code '%f')"
                end
            elseif d ~= "%" then
                if d == "" then
                    return "the command line must not end with a single '%' (write '%%' for a literal %)"
                end
                return "unknown placeholder '%" .. d .. "' (use %f for the target files, %% for a literal %)"
            end
            i = i + 2
        else
            i = i + 1
        end
    end
    return nil
end

-- command の %f を "$@"(シェルの位置パラメータの全部)に置き換えた、シェルのコマンドラインを返す。
-- **パスは、コマンドラインに埋め込まない**: spawn_shell の別の引数として渡り、シェルの構文としては解釈されない
-- (名前の中の $()・バッククォート・引用符・; は、ただの文字。空白・改行を含む名前も、そのまま 1 つの引数になる)。
-- %% は % になる。%f が無ければ、末尾に ' "$@"' を足す。command は、editor_command_problem を通ったもの
local function expand_editor_command(command)
    local has_files = false
    local script = command:gsub("%%(.)", function(c)
        if c == "f" then
            has_files = true
            return '"$@"'
        end
        return "%"
    end)
    if not has_files then script = script .. ' "$@"' end
    return script
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
        -- エディタで開く。targetは open と同じ(パスの文字列・エントリ・それらの配列。いつも明示する)。
        -- commandは、%f を含む一行のコマンド(省略したら、Miata.config.set_editor で決めたもの。未設定なら "open -t %f" =
        -- システムの既定のテキストエディタ)。/bin/sh -c で、待たずに起動する。%f は、対象のパス(1件ずつ別の引数)に
        -- なる("$@" に置き換わり、パスは位置パラメータとして渡る。シェルの構文としては解釈されない)。%f は
        -- 引用符の外に書く。%f が無ければ、末尾に足す。%% は %。
        -- 10件を超えるときは、確認する(ダイアログ)。対象が空なら、何もせずfalse。ほかは、起動の要求を出せたらtrue
        -- (起動できなかったとき・エディタが非0で終わったときは、あとでダイアログで知らせる)。
        -- commandが正しくない(文字列でない・空・NULを含む・%f と %% 以外の %x・引用符の中やバックスラッシュの直後の %f)と、
        -- Luaのエラー(確認の前に起きる)
        edit = function(target, command)
            if command == nil then
                command = editor_command
            else
                local problem = editor_command_problem(command)
                if problem then error("edit: " .. problem, 2) end
            end
            local paths = Miata._private.paths_of(target)
            if #paths == 0 then return false end
            if not confirm_open(#paths) then return false end
            return Miata._private.spawn_shell(expand_editor_command(command), paths, "エディタを起動できませんでした")
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
        -- エディタ(edit)のコマンドを決める。%f を含む一行のコマンドで、%f が、対象のファイルに置き換わる(引用符の外に書く)。
        -- 例: set_editor('open -a "Visual Studio Code" %f') / set_editor('/opt/homebrew/bin/zed %f')。
        -- 正しくない値(文字列でない・空・NULを含む・%f と %% 以外の %x・引用符の中やバックスラッシュの直後の %f)は、
        -- 設定を変えずに、エラーにする
        -- (設定の読み込みのエラーとして、起動時のダイアログに出る)。実行中に呼んでもよい(次の edit から使う)
        set_editor = function(command)
            local problem = editor_command_problem(command)
            if problem then error("set_editor: " .. problem, 2) end
            editor_command = command
        end,
        -- いまのエディタのコマンド(未設定なら、既定の "open -t %f")
        get_editor = function()
            return editor_command
        end,
    },
}


