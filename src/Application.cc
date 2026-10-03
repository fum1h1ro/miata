#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>

#include "platform.h"
#include "models/Model.h"
#include "views/View.h"
#include "views/FilterListDialog.h"
#include "Script.h"
#include "KeyBinding.h"
#include "Application.h"
#include "Config.h"
#include "misc.h"
#include "Utf8.h"

namespace miata {
    // tidx にあるテーブルの select(選択リスト)を検証する。不正なら luaL_error(Lua のエラー。ダイアログは開かない)。
    // luaL_error は longjmp なので、スタックにあるデストラクタ付きのオブジェクト(std::string など)のデストラクタが走らない。
    // だから、C++ のオブジェクトを作る前に呼ぶこと。この関数自身も、Lua の C API と整数だけを使い、メッセージの書式は固定で、
    // 埋め込むのは整数だけにする。
    // 添字は戻り値の select と1対1なので、要素を黙って捨てたり、範囲外の selected を丸めたりしない(ずれた行が確定してしまう)。
    // buttons / checkboxes は、従来どおり寛容なパース(型違いを黙って捨てる)のまま。
    static void CheckCustomDialogSelect(lua_State* L, int tidx)
    {
        tidx = lua_absindex(L, tidx);

        // 旧 API。黙って無視すると、リストの無いダイアログが開いて、戻り値の result.selects[1] の nil 参照という遠いエラーになる
        lua_getfield(L, tidx, "selects");
        if (!lua_isnil(L, -1)) {
            luaL_error(L, "dialog_custom: 'selects' was replaced by 'select' (one list per dialog): select = { options = {...}, selected = 1 }");
        }
        lua_pop(L, 1);

        lua_getfield(L, tidx, "select");
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            return;
        }
        if (!lua_istable(L, -1)) {
            luaL_error(L, "dialog_custom: select must be a table: { options = {...}, selected = 1 }");
        }
        const int sel = lua_gettop(L);

        lua_getfield(L, sel, "options");
        if (!lua_istable(L, -1)) {
            luaL_error(L, "dialog_custom: select.options must be a table of strings");
        }
        const lua_Integer n = (lua_Integer)lua_rawlen(L, -1);
        if (n == 0) {
            luaL_error(L, "dialog_custom: select.options must not be empty");
        }
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L, -1, i);
            if (!lua_isstring(L, -1)) { // 文字列と数値(dialog_filter_list の items と同じ)
                luaL_error(L, "dialog_custom: select.options[%d] must be a string", (int)i);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1); // options

        lua_getfield(L, sel, "selected");
        if (!lua_isnil(L, -1)) {
            // 数値だけ(文字列の "2" は不可)。3.0 は整数として受け付け、3.5 と NaN は受け付けない(set_history_limit と同じ)
            int is_integer = 0;
            const lua_Integer v = lua_type(L, -1) == LUA_TNUMBER ? lua_tointegerx(L, -1, &is_integer) : 0;
            if (!is_integer || v < 1 || v > n) {
                luaL_error(L, "dialog_custom: select.selected must be an integer from 1 to %d", (int)n);
            }
        }
        lua_pop(L, 2); // selected, select
    }

    // tidx にあるテーブル(title/message/buttons/checkboxes/select)から CustomDialogSpec を組み立てる。
    // select は CheckCustomDialogSelect で検証済みの前提で、エラーを投げない読み取りにしてある。
    // 文字列は、UTF-8 として不正なバイトを U+FFFD に置き換える(そのままだと NSString にできず、ラベルの作成で例外になる)。
    static CustomDialogSpec ParseCustomDialogSpec(lua_State* L, int tidx)
    {
        auto get_str = [&](int idx, const char* key) -> std::string {
            lua_getfield(L, idx, key);
            std::string v = lua_isstring(L, -1) ? RepairUtf8(lua_tostring(L, -1)) : "";
            lua_pop(L, 1);
            return v;
        };

        CustomDialogSpec spec;
        spec.title   = get_str(tidx, "title");
        spec.message = get_str(tidx, "message");

        // buttons
        lua_getfield(L, tidx, "buttons");
        if (lua_istable(L, -1)) {
            int n = (int)lua_rawlen(L, -1);
            for (int i = 1; i <= n; i++) {
                lua_rawgeti(L, -1, i);
                if (lua_isstring(L, -1)) spec.buttons.push_back(RepairUtf8(lua_tostring(L, -1)));
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        // checkboxes
        lua_getfield(L, tidx, "checkboxes");
        if (lua_istable(L, -1)) {
            int n = (int)lua_rawlen(L, -1);
            for (int i = 1; i <= n; i++) {
                lua_rawgeti(L, -1, i);
                if (lua_istable(L, -1)) {
                    CustomDialogCheckbox cb;
                    cb.label   = get_str(lua_gettop(L), "label");
                    lua_getfield(L, -1, "checked");
                    cb.checked = lua_isboolean(L, -1) && lua_toboolean(L, -1);
                    lua_pop(L, 1);
                    spec.checkboxes.push_back(std::move(cb));
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        // select(検証済み。select.label など、使わないキーは読まない)
        lua_getfield(L, tidx, "select");
        if (lua_istable(L, -1)) {
            CustomDialogSelect sel;
            lua_getfield(L, -1, "options");
            int m = (int)lua_rawlen(L, -1);
            for (int j = 1; j <= m; j++) {
                lua_rawgeti(L, -1, j);
                sel.options.push_back(RepairUtf8(lua_tostring(L, -1)));
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            lua_getfield(L, -1, "selected");
            sel.selected = lua_isnil(L, -1) ? 0 : (int)lua_tointeger(L, -1) - 1; // 1-based → 0-based
            lua_pop(L, 1);
            spec.select = std::move(sel);
        }
        lua_pop(L, 1);

        return spec;
    }

    Application::Application()
    {
        key_binding_map_[(int)KeyBindingMap::Normal] = std::make_unique<KeyBinding>();
        key_binding_map_[(int)KeyBindingMap::Dialog] = std::make_unique<KeyBinding>();
    }

    Application::~Application()
    {
    }

    void Application::Initialize()
    {
        Instance().InitializeImpl();
    }

    void Application::InitializeImpl()
    {
        pl_create_main_window(1024, 768, "Miata");

        std::print("config_dir: {}\n", pl_get_config_dir().c_str());

        auto config_errors = InitializeScript();

        // 設定を読み終えたので、ウィンドウ自体の背景色を決める(ペインやヘッダーは、自分で塗る)。
        // ウィンドウを作った時点では設定が読まれていないので、ここで設定する
        pl_set_window_background_color(Config::Background());

        auto home = pl_get_home_dir();
        printf("home: %s\n", home.c_str());

        view_ = std::make_unique<views::View>();

        // フォルダの履歴に、設定の上限を渡して、前回までの記録を復元する。設定の読み込みの後で、最初の移動より前
        // (タイマーが動き出す前)に済ませる。設定の読み込みでエラーが起きたときは、ユーザーが指定した上限が分からない
        // (エラーの行より後の設定は実行されない)ので、保存してある履歴を削らないよう、最大値で復元する。正しい上限は、
        // 設定を直して起動し直したときに適用される
        auto history_limit = config_errors.empty() ? Config::HistoryLimit() : static_cast<size_t>(Config::kHistoryLimitMax);
        models::BrowserModel::Instance().RestoreHistory(history_limit);

        // 前回の終了時のペインの状態(いるフォルダ・ソート・フォーカス)を戻す。タイマーが動き出す前に済ませる(最初の
        // ティックの SavePanesIfChanged が、基準の無いまま比べないように)。履歴に記録しない移動なので、履歴の復元との順序は問わない
        view_->RestorePanes();

        file_operations_.SetCallback([this](FileOperationCompleted& e) {
            OnFileOperationCompleted(e);
        });

        pl_set_key_down_handler([this](uint16_t key_code, uint16_t mods) {
            if (!KeyBinding::IsModifierKey(key_code)) KeyDown(key_code, mods);
        });
        pl_set_key_up_handler([this](uint16_t key_code, uint16_t mods) {
            if (!KeyBinding::IsModifierKey(key_code)) KeyUp(key_code, mods);
        });
        pl_set_resize_handler([this](int width, int height) {
            Resize(width, height);
        });
        pl_start_timer(0.05, [this]() {
            Update();
        });

        key_binding_map_[(int)KeyBindingMap::Normal]->DumpAll();
        key_binding_map_[(int)KeyBindingMap::Dialog]->DumpAll();

        // 設定の読み込みで起きたエラーは、Viewとタイマーが動き出してから画面に出す
        ReportConfigErrors(config_errors);
    }

    std::vector<Script::ConfigError> Application::InitializeScript()
    {
        auto& script = miata::Script::Instance();
        script.Initialize();

        // 設定ファイルの読み込み中(Viewを作る前)にも使えるコマンド
        static luaL_Reg config_commands[] = {
            { "bind", lua_command_bind },
            { "unbind", lua_command_unbind },
        };
        script.RegisterFunctions(
            "Miata.command",
            std::vector<luaL_Reg>(std::begin(config_commands), std::end(config_commands))
        );

        // Viewを操作するコマンド。Viewは設定ファイルの読み込みより後に作るので、読み込み中に呼ばれても
        // (設定ファイルの最上位でうっかり呼んでも)nullのViewに触って落ちないよう、lua_view_trampolineを
        // 介して登録する(読み込み中に呼ぶとLuaのエラーになる)。新しいコマンドは、基本的にここに足す。
        static luaL_Reg view_commands[] = {
            { "navigate_up", lua_command_navigate_up },
            { "navigate_down", lua_command_navigate_down },
            { "navigate_left", lua_command_navigate_left },
            { "navigate_right", lua_command_navigate_right },
            { "navigate_ok", lua_command_navigate_ok },
            { "navigate_cancel", lua_command_navigate_cancel },
            { "toggle_focus", lua_command_toggle_focus },
            { "mark", lua_command_mark },
            { "unmark", lua_command_unmark },
            { "toggle_mark", lua_command_toggle_mark },
            { "copy_marked", lua_command_copy_marked },
            { "move_marked", lua_command_move_marked },
            { "make_directory", lua_command_make_directory },
            { "delete_marked", lua_command_delete_marked },
            { "current_pane", lua_command_current_pane },
            { "reload", lua_command_reload },
            { "quick_look", lua_command_quick_look },
            { "sort", lua_command_sort },
            { "search", lua_command_search },
            { "search_next", lua_command_search_next },
            { "search_prev", lua_command_search_prev },
            { "search_clear", lua_command_search_clear },
            { "history_list", lua_command_history_list },
            { "jump_to", lua_command_jump_to },
        };
        script.RegisterFunctions(
            "Miata.command",
            std::vector<luaL_Reg>(std::begin(view_commands), std::end(view_commands)),
            lua_view_trampoline
        );

        static luaL_Reg privates[] = {
            { "dialog_open", lua_private_dialog_open },
            { "dialog_is_open", lua_private_dialog_is_open },
            { "dialog_result", lua_private_dialog_result },
            { "rename_target", lua_private_rename_target },
            { "rename_conflict", lua_private_rename_conflict },
            { "rename_execute", lua_private_rename_execute },
        };
        script.RegisterFunctions(
            "Miata._private",
            std::vector<luaL_Reg>(std::begin(privates), std::end(privates)),
            lua_view_trampoline
        );

        Config::ScriptInitialize();

        return script.PostInitialize();
    }

    void Application::ReportConfigErrors(const std::vector<Script::ConfigError>& errors)
    {
        if (errors.empty()) return;

        std::string message = "設定ファイルの読み込みでエラーが起きました。エラーが起きた行より前の設定は有効です。";
        for (auto& e : errors) {
            // Luaのエラーメッセージは、ふつうファイル名を含む("ファイル名:行番号: ...")。含まないもの
            // (error({}) など)は、どのファイルか分かるように足す
            message += std::format("\n\n{}{}", e.message.find(e.file) == std::string::npos ? e.file + ": " : "", e.message);
        }
        view_->RequestDialog(std::make_shared<views::ConfirmDialog>(
            [](views::IDialog&) {},
            views::ConfirmDialog::arguments{
                .message_ = message,
                .button_text_ = "OK",
            }
        ));
    }

    int Application::lua_view_trampoline(lua_State* L)
    {
        if (!Instance().view_) {
            return luaL_error(L, "this command is not available while the config is loading; call it from a function bound to a key");
        }
        return lua_tocfunction(L, lua_upvalueindex(1))(L);
    }

    void Application::Update()
    {
        file_operations_.Update();
        Script::Instance().Update();
        view_->CheckDialogState();
        // Luaのコルーチン(Script::Update)とダイアログの後始末(CheckDialogState)の後に行う。
        // リネームなど、ダイアログの結果を受けて一覧のカーソルに作用する処理が終わってから反映するため。
        view_->UpdateAutoReload();
        // 検索バーの入力欄とペインの検索の状態を整える。自動リロードの後に行う(リロードで検索の件数が変わる)。
        // バーの出入りで一覧が縮むとき、Quick Lookの覆いが追従するのはレイアウトの側で行うので、UpdateQuickLookとの順序は問わない
        view_->UpdateSearchBar();
        // 自動リロードの後に行う(リロードで動いたカーソルに、同じティックで追従を始められるように)
        view_->UpdateQuickLook();
        // フォルダの履歴の変更を保存する。記録は移動の成功で済んでいて、ここは保存だけ(連続した移動は、ティックごとに
        // 1回にまとまる)。終了時にまとめて保存する処理は無いので、強制終了で失うのは、最後のティック1回分だけ
        models::BrowserModel::Instance().SaveHistoryIfChanged();
        // ペインの状態(いるフォルダ・ソート・フォーカス)の変更を保存する。履歴と同じく、ティックごとにまとめて保存する
        view_->SavePanesIfChanged();
    }

    void Application::KeyDown(uint16_t key_code, uint16_t mods)
    {
        auto& script = Script::Instance();
        const bool in_dialog = view_->IsAnyDialogOpened();
        auto& kb = in_dialog ? key_binding_map_[(int)KeyBindingMap::Dialog] : key_binding_map_[(int)KeyBindingMap::Normal];

        auto r = key_stroke_.Add(KeyBinding::Key::MakeKey(mods, key_code));
        if (!r) {
            pl_play_beep();
            key_stroke_.Clear();
            return;
        }
        auto key_setting = kb->Has(key_stroke_);
        if (key_setting) {
            //printf("match key stroke: %d\n", key_setting.value());
            auto result = script.InvokeRefFunctionOnThread(key_stroke_.ToString(), key_setting.value());
            key_stroke_.Clear();
            if (!result) {
                printf("error: %s\n", result.error().c_str());
            }
        }
        else if (key_setting.error() != KeyBinding::ErrorReason::MaybeTooShort) {
            // 割り当てが無い。ダイアログの表示中なら、項目のショートカット(ラベルの `&x`)を試す。ここに来るのは、キーバインドに
            // 完全一致も途中一致も無いキーだけなので、キーバインドが優先される。試すのは、修飾キー無しの単独のキーだけ
            // (キー列の2打目以降は試さない。Normal の `dd` の1打目が残ったまま、ダイアログが開くことがある)
            bool handled = false;
            if (in_dialog && key_stroke_.Size() == 1 && mods == 0) {
                if (const auto ch = KeyBinding::KeyCodeToAscii(key_code)) handled = view_->ActivateDialogMnemonic(*ch);
            }
            key_stroke_.Clear();
            if (!handled) pl_play_beep();
        }
    }

    void Application::KeyUp(uint16_t key_code, uint16_t mods)
    {
        //printf("key_up: %d\n", key);
    }

    void Application::Resize(int width, int height)
    {
        width_.Value(width);
        height_.Value(height);
    }

    std::expected<Application::KeyBindingMap, std::string> Application::GetKeyBinding(const char map_c)
    {
        switch (std::tolower(map_c)) {
        case 'n':
            return KeyBindingMap::Normal;
        case 'd':
            return KeyBindingMap::Dialog;
        default:
            return std::unexpected("unknown map");
        }
    }

    void Application::StartFileOperation(FileOpType type)
    {
        auto& src_model = view_->CurrentList();
        auto& dest_model = view_->OtherList();

        std::vector<std::filesystem::path> sources;
        for (auto i = 0; i < src_model.Size(); ++i) {
            auto& entry = src_model.GetEntry(i);
            if (entry.IsMarked()) sources.push_back(entry.Path());
        }
        if (sources.empty()) {
            // マークが無ければ、カーソル下の1件(一覧が空なら、対象が無い)
            if (auto* current = view_->CurrentEntry()) sources.push_back(current->Path());
        }
        if (sources.empty()) return;

        auto src_dir = src_model.Path();
        auto dest_dir = dest_model.Path();

        auto conflicts = 0;
        for (auto& src : sources) {
            std::error_code ec;
            if (std::filesystem::exists(dest_dir / src.filename(), ec)) ++conflicts;
        }

        auto start = [this, type, sources, src_dir, dest_dir, &src_model, &dest_model](bool overwrite) {
            file_operations_.Start(type, sources, src_dir, dest_dir, overwrite, &src_model, &dest_model);
        };

        if (conflicts > 0) {
            auto message = std::format("{}個のファイルが既に存在します。上書きしますか？", conflicts);
            view_->RequestDialog(std::make_shared<views::YesNoDialog>(
                [start](views::IDialog& dialog) {
                    auto& yes_no_dialog = dynamic_cast<views::YesNoDialog&>(dialog);
                    start(yes_no_dialog.Result());
                },
                views::YesNoDialog::arguments{
                    .message_ = message,
                    .default_select_ = false,
                    .yes_text_ = "上書き",
                    .no_text_ = "スキップ",
                }
            ));
        }
        else {
            start(false);
        }
    }

    void Application::OnFileOperationCompleted(FileOperationCompleted& event)
    {
        // バックグラウンド実行中にペインが別ディレクトリへ移動されている場合があるため、
        // 操作開始時点のパスを今も表示している場合に限って再スキャンする。
        if (event.dest_model && event.dest_model->Path() == event.dest_dir) {
            view_->ReloadList(*event.dest_model);
        }
        if (event.src_model && event.src_model->Path() == event.src_dir) {
            if (event.type == FileOpType::Move) {
                view_->ReloadList(*event.src_model); // 消えたファイルを一覧に反映（移せなかったファイルのマークは残る）
            }
            else {
                view_->ClearListMarks(*event.src_model); // 中身は変わらないのでマークだけ解除する(画面にも反映する)
            }
        }

        if (!event.success) {
            view_->ReportFileError(
                std::format("エラーが発生しました（{}件失敗）", event.failed_count),
                FileError{.message = event.error_message, .permission_denied = event.permission_denied}
            );
            return;
        }
        view_->RequestDialog(std::make_shared<views::ConfirmDialog>(
            [](views::IDialog&) {},
            views::ConfirmDialog::arguments{
                .message_ = "完了しました",
                .button_text_ = "OK",
            }
        ));
    }

    void Application::DeleteMarked()
    {
        auto& list = view_->CurrentList();

        std::vector<std::filesystem::path> targets;
        for (auto i = 0; i < list.Size(); ++i) {
            auto& entry = list.GetEntry(i);
            if (entry.IsMarked()) targets.push_back(entry.Path());
        }
        if (targets.empty()) return; // マークが無ければ何もしない

        auto dir = list.Path();
        auto message = std::format("{}件をゴミ箱に移動しますか？", targets.size());

        view_->RequestDialog(std::make_shared<views::YesNoDialog>(
            [this, targets, dir, &list](views::IDialog& dialog) {
                auto& yes_no_dialog = dynamic_cast<views::YesNoDialog&>(dialog);
                if (!yes_no_dialog.Result()) return;

                FileErrorSummary failures;
                for (auto& target : targets) {
                    auto result = pl_trash_file(target);
                    if (!result) failures.Add(result.error());
                }

                // 確認ダイアログはモーダルで開いている間ペイン移動できないため、
                // ここでは常に操作対象だったディレクトリのままのはずだが、念のため確認する。
                if (list.Path() == dir) {
                    view_->ReloadList(list); // ゴミ箱に移せなかったファイルのマークは残る
                }

                if (failures.count > 0) {
                    view_->ReportFileError(std::format("エラーが発生しました（{}件失敗）", failures.count), failures.shown);
                }
            },
            views::YesNoDialog::arguments{
                .message_ = message,
                .default_select_ = false,
                .yes_text_ = "ゴミ箱へ",
                .no_text_ = "キャンセル",
            }
        ));
    }

    // Miata.command.bind(mode, keys, fn)
    // モードごとに、別々の Lua の参照を作る。1つの参照を複数のモード("nd")で共有すると、unbind("n", ...) が、Dialog 側が
    // まだ使っている参照を解放し、その参照を再利用した別の関数が、Dialog 側のキーで呼ばれてしまう(二重に解放もする)。
    // luaL_error は longjmp なので、デストラクタを持つオブジェクト(std::string など)は、luaL_error の前に作らない。
    int Application::lua_command_bind(lua_State* L)
    {
        auto& app = Application::Instance();
        Script::CheckArgType(L, 1, LUA_TSTRING);
        Script::CheckArgType(L, 2, LUA_TSTRING);
        Script::CheckArgType(L, 3, LUA_TFUNCTION);

        const char* modes = lua_tostring(L, 1);
        const char* key_string = lua_tostring(L, 2);

        // 登録の前に、モードの文字を全部確かめる(途中で失敗して、一部のモードにだけ登録されるのを避ける)
        for (const char* m = modes; *m; ++m) {
            const bool known = app.GetKeyBinding(*m).has_value();
            if (!known) luaL_error(L, "unknown map");
        }

        for (const char* m = modes; *m; ++m) {
            auto& kb = app.key_binding_map_[(size_t)app.GetKeyBinding(*m).value()];

            // 同じキーが既に割り当てられていたら、外して、その関数の参照を解放する(上書きしても、参照が漏れない。
            // 外すときに、キー列の経路の数も戻るので、あとで unbind したキーが「続きを待つ」状態にならない)。
            // "nn" のように同じモードを2回書いても、2回目が1回目を置き換えるだけ
            if (auto old = kb->Has(key_string)) {
                kb->Unregister(key_string);
                luaL_unref(L, LUA_REGISTRYINDEX, old.value());
            }

            lua_pushvalue(L, 3);
            const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
            auto r = kb->Register(key_string, ref);
            if (!r) {
                // キーの書き方の誤りは、どのモードでも同じなので、最初のモードで失敗する(他のモードには登録していない)
                luaL_unref(L, LUA_REGISTRYINDEX, ref);
                luaL_error(L, "key binding error: %d", (int)r.error());
            }
        }
        return 0;
    }

    int Application::lua_command_unbind(lua_State* L)
    {
        auto& app = Application::Instance();
        Script::CheckArgType(L, 1, LUA_TSTRING);
        Script::CheckArgType(L, 2, LUA_TSTRING);

        const char* modes = lua_tostring(L, 1);
        const char* key_string = lua_tostring(L, 2);

        for (const char* m = modes; *m; ++m) {
            const bool known = app.GetKeyBinding(*m).has_value();
            if (!known) luaL_error(L, "unknown map");
        }

        std::array<bool, (size_t)KeyBindingMap::Max> done{}; // "nn" のように同じモードを2回書いても、1回だけ外す(2回目は割り当てが無くてエラーになる)
        for (const char* m = modes; *m; ++m) {
            const auto index = (size_t)app.GetKeyBinding(*m).value();
            if (done[index]) continue;
            done[index] = true;

            // そのモードの参照だけを解放する(モードごとに別々の参照なので、他のモードの割り当ては残る)
            auto r = app.key_binding_map_[index]->Unregister(key_string);
            if (!r) luaL_error(L, "key binding error: %d", (int)r.error());
            luaL_unref(L, LUA_REGISTRYINDEX, r.value());
        }
        return 0;
    }

    int Application::lua_command_navigate_up(lua_State* L)
    {
        auto& app = Application::Instance();
        int nargs = lua_gettop(L);
        if (nargs >= 1) {
            Script::CheckArgType(L, 1, LUA_TNUMBER);
            int n = (int)lua_tointeger(L, 1);
            for (int i = 0; i < n; ++i) {
                app.view_->Navigate(views::constants::Navigate::Up);
            }
        }
        else {
            app.view_->Navigate(views::constants::Navigate::Up);
        }
        return 0;
    }

    int Application::lua_command_navigate_down(lua_State* L)
    {
        auto& app = Application::Instance();
        int nargs = lua_gettop(L);
        if (nargs >= 1) {
            Script::CheckArgType(L, 1, LUA_TNUMBER);
            int n = (int)lua_tointeger(L, 1);
            for (int i = 0; i < n; ++i) {
                app.view_->Navigate(views::constants::Navigate::Down);
            }
        }
        else {
            app.view_->Navigate(views::constants::Navigate::Down);
        }
        return 0;
    }

    int Application::lua_command_navigate_left(lua_State* L)
    {
        auto& app = Application::Instance();
        app.view_->Navigate(views::constants::Navigate::Left);
        return 0;
    }

    int Application::lua_command_navigate_right(lua_State* L)
    {
        auto& app = Application::Instance();
        app.view_->Navigate(views::constants::Navigate::Right);
        return 0;
    }

    int Application::lua_command_navigate_ok(lua_State* L)
    {
        auto& app = Application::Instance();
        app.view_->Navigate(views::constants::Navigate::Ok);
        return 0;
    }
    int Application::lua_command_navigate_cancel(lua_State* L)
    {
        auto& app = Application::Instance();
        app.view_->Navigate(views::constants::Navigate::Cancel);
        return 0;
    }

    int Application::lua_command_toggle_focus(lua_State* L)
    {
        auto& app = Application::Instance();
        app.view_->ToggleFocus();
        return 0;
    }

    int Application::lua_command_mark(lua_State* L)
    {
        auto& app = Application::Instance();
        app.view_->Mark();
        return 0;
    }

    int Application::lua_command_unmark(lua_State* L)
    {
        auto& app = Application::Instance();
        app.view_->Unmark();
        return 0;
    }

    int Application::lua_command_toggle_mark(lua_State* L)
    {
        auto& app = Application::Instance();
        app.view_->ToggleMark();
        return 0;
    }

    int Application::lua_command_copy_marked(lua_State* L)
    {
        auto& app = Application::Instance();
        app.StartFileOperation(FileOpType::Copy);
        return 0;
    }

    int Application::lua_command_move_marked(lua_State* L)
    {
        auto& app = Application::Instance();
        app.StartFileOperation(FileOpType::Move);
        return 0;
    }

    int Application::lua_command_make_directory(lua_State* L)
    {
        auto& app = Application::Instance();
        Script::CheckArgType(L, 1, LUA_TSTRING);
        const std::string name = lua_tostring(L, 1);

        auto& list = app.view_->CurrentList();
        auto dest = list.Path() / name;

        std::error_code ec;
        std::filesystem::create_directory(dest, ec);

        if (!ec) {
            app.view_->ReloadList(list);
            lua_pushboolean(L, true);
        }
        else {
            app.view_->ReportFileError("フォルダを作成できませんでした", FileError::From(ec));
            lua_pushboolean(L, false);
        }
        return 1;
    }

    // list内にマークが1件でもあれば true(rename系コマンドは単一ファイルのみ対応のため無効化する)
    static bool AnyMarked(models::FileListModel& list)
    {
        for (auto i = 0; i < list.Size(); ++i) {
            if (list.GetEntry(i).IsMarked()) return true;
        }
        return false;
    }

    // マークがある、またはリストが空ならnil。それ以外はカーソル位置のエントリ名を返す。
    int Application::lua_private_rename_target(lua_State* L)
    {
        auto& app = Application::Instance();
        auto& list = app.view_->CurrentList();

        auto* current = app.view_->CurrentEntry();
        if (!current || AnyMarked(list)) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushstring(L, current->Name().c_str());
        return 1;
    }

    // カレントディレクトリ内にnameと同名のエントリが既に存在するか
    int Application::lua_private_rename_conflict(lua_State* L)
    {
        auto& app = Application::Instance();
        Script::CheckArgType(L, 1, LUA_TSTRING);
        const std::string name = lua_tostring(L, 1);

        auto& list = app.view_->CurrentList();
        std::error_code ec;
        lua_pushboolean(L, std::filesystem::exists(list.Path() / name, ec));
        return 1;
    }

    int Application::lua_private_rename_execute(lua_State* L)
    {
        auto& app = Application::Instance();
        Script::CheckArgType(L, 1, LUA_TSTRING);
        const std::string new_name = lua_tostring(L, 1);

        auto& list = app.view_->CurrentList();
        auto* entry = app.view_->CurrentEntry();
        if (!entry || AnyMarked(list)) {
            lua_pushboolean(L, false);
            return 1;
        }

        auto dest = list.Path() / new_name;

        std::error_code ec;
        std::filesystem::rename(entry->Path(), dest, ec);

        if (!ec) {
            app.view_->ReloadList(list, dest); // 旧名は消えるので、カーソルは新しい名前に合わせる
            lua_pushboolean(L, true);
        }
        else {
            app.view_->ReportFileError("リネームできませんでした", FileError::From(ec));
            lua_pushboolean(L, false);
        }
        return 1;
    }

    int Application::lua_command_delete_marked(lua_State* L)
    {
        auto& app = Application::Instance();
        app.DeleteMarked();
        return 0;
    }

    // Luaに公開するペイン名。current_pane()の戻り値とreload(pane)の引数で同じ表記を使うよう、
    // 文字列との変換はここに集約する。
    static const char* PaneName(views::constants::Pane pane)
    {
        return pane == views::constants::Pane::Left ? "left" : "right";
    }

    static std::optional<views::constants::Pane> ParsePane(const char* name)
    {
        if (std::strcmp(name, "left") == 0) return views::constants::Pane::Left;
        if (std::strcmp(name, "right") == 0) return views::constants::Pane::Right;
        return std::nullopt;
    }

    // 省略可のペイン引数(Luaの引数のindex番目)を読む。省略またはnilならfallback。"left" / "right" 以外は、Luaのエラー
    // (luaL_errorはlongjmpなので、呼ぶ前にC++のオブジェクトを作らないこと)。
    static views::constants::Pane OptionalPaneArg(lua_State* L, int index, views::constants::Pane fallback)
    {
        if (lua_gettop(L) < index || lua_isnil(L, index)) return fallback;
        Script::CheckArgType(L, index, LUA_TSTRING);
        auto parsed = ParsePane(lua_tostring(L, index));
        if (!parsed) {
            luaL_error(L, "unknown pane: %s (expected \"left\" or \"right\")", lua_tostring(L, index));
            return fallback; // luaL_errorは戻らない
        }
        return *parsed;
    }

    // Quick Lookを被せる範囲。ペイン名に、両ペインを表す "both" を足したもの
    static std::optional<views::constants::QuickLookArea> ParseQuickLookArea(const char* name)
    {
        if (std::strcmp(name, "both") == 0) return views::constants::QuickLookArea::Both;
        auto pane = ParsePane(name);
        if (!pane) return std::nullopt;
        return *pane == views::constants::Pane::Left ? views::constants::QuickLookArea::Left : views::constants::QuickLookArea::Right;
    }

    // Miata.command.current_pane() -> "left" | "right"  (カーソルのあるペイン)
    int Application::lua_command_current_pane(lua_State* L)
    {
        auto& app = Application::Instance();
        lua_pushstring(L, PaneName(app.view_->CurrentPane()));
        return 1;
    }

    // Miata.command.reload([pane]) -> boolean
    // paneは "left" / "right"。省略またはnilなら現在のペイン。
    int Application::lua_command_reload(lua_State* L)
    {
        auto& app = Application::Instance();

        auto pane = OptionalPaneArg(L, 1, app.view_->CurrentPane());

        auto result = app.view_->GetFileListView(pane).Reload();
        if (!result) {
            // 反対側のペインも対象にできるので、どのディレクトリで失敗したかも示す
            app.view_->ReportFileError(
                std::format("再読み込みできませんでした ({})", app.view_->GetList(pane).Path().string()),
                result.error()
            );
        }
        lua_pushboolean(L, result.has_value());
        return 1;
    }

    // Miata.command.quick_look([pane]) -> boolean
    // カーソルのあるペインのカーソル下のファイルを、Quick Lookのプレビューとして一覧の上に被せて表示する。
    // paneは被せる範囲で、"left" / "right" はそのペインだけ、省略・nil・"both" は両ペインにまたがる1枚。
    // 表示中に同じ指定で呼ぶと閉じる(別の指定なら範囲だけ切り替える)。呼んだ後に表示中ならtrueを返す。
    int Application::lua_command_quick_look(lua_State* L)
    {
        auto& app = Application::Instance();

        auto area = views::constants::QuickLookArea::Both;
        if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
            Script::CheckArgType(L, 1, LUA_TSTRING);
            auto parsed = ParseQuickLookArea(lua_tostring(L, 1));
            if (!parsed) {
                luaL_error(L, "unknown pane: %s (expected \"both\", \"left\" or \"right\")", lua_tostring(L, 1));
                return 0;
            }
            area = *parsed;
        }

        lua_pushboolean(L, app.view_->ToggleQuickLook(area));
        return 1;
    }

    int Application::lua_command_sort(lua_State* L)
    {
        auto& app = Application::Instance();
        Script::CheckArgType(L, 1, LUA_TSTRING);

        auto reverse = false;
        if (lua_gettop(L) >= 2) {
            Script::CheckArgType(L, 2, LUA_TBOOLEAN);
            reverse = lua_toboolean(L, 2);
        }

        // luaL_errorはlongjmpなので、デストラクタを持つオブジェクト(std::stringなど)は、luaL_errorの前に作らない
        // (std::optional<SortKey>はデストラクタを持たないので、使ってよい)
        const auto key = views::FileListView::ParseSortKey(lua_tostring(L, 1));
        if (!key) {
            luaL_error(L, "unknown sort key");
            return 0;
        }

        app.view_->CurrentFileListView().SetSort(*key, reverse);
        return 0;
    }

    // Miata.command.search() -> boolean
    // カーソルのペインで、ファイル名の検索を始める(vimの / )。そのペインの下に検索バーが出て、入力欄に文字を打てる
    // (入力中はNormalのキーバインドは効かない。Enterで確定、Escで取り消し)。始められたらtrue。
    // ダイアログの表示中や、すでに入力中のときはfalse。
    int Application::lua_command_search(lua_State* L)
    {
        auto& app = Application::Instance();
        lua_pushboolean(L, app.view_->BeginSearch());
        return 1;
    }

    // Miata.command.search_next() / search_prev() -> boolean
    // カーソルのペインの、次・前のマッチのファイルへカーソルを動かす(vimの n / N。端でラップ)。
    // 動けたらtrue。検索していない、またはマッチが無くて動けなければ、beepを鳴らしてfalse。
    int Application::lua_command_search_next(lua_State* L)
    {
        auto& app = Application::Instance();
        bool moved = app.view_->StepSearch(1);
        if (!moved) pl_play_beep();
        lua_pushboolean(L, moved);
        return 1;
    }

    int Application::lua_command_search_prev(lua_State* L)
    {
        auto& app = Application::Instance();
        bool moved = app.view_->StepSearch(-1);
        if (!moved) pl_play_beep();
        lua_pushboolean(L, moved);
        return 1;
    }

    // Miata.command.search_clear() -> boolean
    // カーソルのペインの検索を終える(ハイライトと検索バーを消す。カーソルは動かさない)。
    // 検索していたらtrue。通常時のEsc(navigate_cancel)も、同じことをする。
    int Application::lua_command_search_clear(lua_State* L)
    {
        auto& app = Application::Instance();
        lua_pushboolean(L, app.view_->ClearSearch());
        return 1;
    }

    // Miata.command.history_list([pane]) -> string[]
    // フォルダの履歴(左右のペインで共有する)を、新しい順のフルパスの配列で返す。paneのペイン(省略またはnilなら
    // 現在のペイン。"left" / "right")の今いるフォルダは含まない(そのペインの移動先に選ぶ前提なので)。反対側の
    // ペインの今いるフォルダは含む。履歴が無ければ空の配列。
    // 履歴を選んで移るのは、Miata.command.history()(これと dialog_filter_list と jump_to を組み合わせたもの)。
    int Application::lua_command_history_list(lua_State* L)
    {
        auto& app = Application::Instance();
        auto pane = OptionalPaneArg(L, 1, app.view_->CurrentPane());

        auto items = models::BrowserModel::Instance().History().List(app.view_->GetList(pane).Path());
        lua_createtable(L, (int)items.size(), 0);
        for (size_t i = 0; i < items.size(); ++i) {
            lua_pushlstring(L, items[i].data(), items[i].size());
            lua_rawseti(L, -2, (lua_Integer)i + 1);
        }
        return 1;
    }

    // Miata.command.jump_to(path, [pane]) -> boolean
    // paneのペイン(省略またはnilなら現在のペイン)を、pathのフォルダへ移す(Enterでフォルダに入るのと同じ移動。
    // カーソルは先頭、マークと検索は消える。フォーカスは動かさない)。pathは絶対パスのみ("~" は展開しない。
    // 空の要素・"."・".." を含むパスはエラー)。移れたらtrue。移れなければ、ダイアログで知らせてfalse(権限が無い失敗には、
    // 許可の案内が付く)。そのフォルダがもう無ければ(消えた・フォルダでなくなった)、履歴からも外す。
    int Application::lua_command_jump_to(lua_State* L)
    {
        auto& app = Application::Instance();

        Script::CheckArgType(L, 1, LUA_TSTRING);
        size_t length = 0;
        const char* raw = lua_tolstring(L, 1, &length);
        // luaL_errorの前なので、string_viewだけで判定する(NULも弾く。理由はPaneState::IsPlainAbsolutePath)
        if (!models::PaneState::IsPlainAbsolutePath(std::string_view(raw, length))) {
            luaL_error(L, "jump_to: expected an absolute path such as \"/Users/me/dir\" (no \"~\", and no empty, \".\" or \"..\" elements)");
            return 0;
        }
        auto pane = OptionalPaneArg(L, 2, app.view_->CurrentPane());

        // ここから先はluaL_errorを呼ばない(std::stringなどを作るため)
        lua_pushboolean(L, app.view_->JumpToPath(pane, std::filesystem::path(std::string(raw, length))));
        return 1;
    }

    // Miata._private.dialog_open(type, options)
    int Application::lua_private_dialog_open(lua_State* L)
    {
        auto& app = Application::Instance();
        Script::CheckArgType(L, 1, LUA_TSTRING);
        Script::CheckArgType(L, 2, LUA_TTABLE);

        // string_view なのは、デストラクタを持つオブジェクトを、luaL_error(longjmp)の前に作らないため
        const std::string_view type_string = lua_tostring(L, 1);
        if (type_string == "confirm") {
            auto message = Script::GetTableField<std::string, LUA_TSTRING>(L, 2, "message", "Are you sure?");
            auto button_text = Script::GetTableField<std::string, LUA_TSTRING>(L, 2, "button_text", "OK");

            auto dialog = std::make_shared<views::ConfirmDialog>(
                [](views::IDialog&) {},
                views::ConfirmDialog::arguments{
                    .message_ = message,
                    .button_text_ = button_text,
                }
            );
            auto dialog_ptr = Script::PushSharedUserdata<views::ConfirmDialog>(L, dialog);
            app.view_->RequestDialog(dialog_ptr);
            return 1;
        }
        else if (type_string == "yesno") {
            auto message = Script::GetTableField<std::string, LUA_TSTRING>(L, 2, "message", "Are you sure?");
            auto default_focus = Script::GetTableField<bool, LUA_TBOOLEAN>(L, 2, "default_focus", true);
            auto yes_text = Script::GetTableField<std::string, LUA_TSTRING>(L, 2, "yes_text", "YES");
            auto no_text = Script::GetTableField<std::string, LUA_TSTRING>(L, 2, "no_text", "NO");

            auto dialog = std::make_shared<views::YesNoDialog>(
                [](views::IDialog& dialog) {
                    auto& yes_no_dialog = dynamic_cast<views::YesNoDialog&>(dialog);
                    if (yes_no_dialog.Result()) {
                        std::print("yes\n");
                    }
                    else {
                        std::print("no\n");
                    }
                },
                views::YesNoDialog::arguments{
                    .message_ = message,
                    .default_select_ = default_focus,
                    .yes_text_ = yes_text,
                    .no_text_ = no_text,
                }
            );
            auto dialog_ptr = Script::PushSharedUserdata<views::YesNoDialog>(L, dialog);
            app.view_->RequestDialog(dialog_ptr);
            return 1;
        }
        else if (type_string == "inputtext") {
            auto message = Script::GetTableField<std::string, LUA_TSTRING>(L, 2, "message", "");
            auto initial_text = Script::GetTableField<std::string, LUA_TSTRING>(L, 2, "initial_text", "");

            auto dialog = std::make_shared<views::InputTextDialog>(
                [](views::IDialog&) {},
                views::InputTextDialog::arguments{
                    .message_ = message,
                    .initial_text_ = initial_text,
                }
            );
            auto dialog_ptr = Script::PushSharedUserdata<views::InputTextDialog>(L, dialog);
            app.view_->RequestDialog(dialog_ptr);
            return 1;
        }
        else if (type_string == "custom") {
            CheckCustomDialogSelect(L, 2); // 検証してから、spec(std::string を持つ)を作る
            auto spec = ParseCustomDialogSpec(L, 2);

            auto dialog = std::make_shared<views::CustomDialog>(
                [](views::IDialog&) {},
                spec
            );
            auto dialog_ptr = Script::PushSharedUserdata<views::CustomDialog>(L, dialog);
            app.view_->RequestDialog(dialog_ptr);
            return 1;
        }
        else if (type_string == "filterlist") {
            auto title = Script::GetTableField<std::string, LUA_TSTRING>(L, 2, "title", "");
            auto message = Script::GetTableField<std::string, LUA_TSTRING>(L, 2, "message", "");

            std::vector<std::string> items;
            lua_getfield(L, 2, "items");
            if (lua_istable(L, -1)) {
                int n = (int)lua_rawlen(L, -1);
                items.reserve((size_t)n);
                for (int i = 1; i <= n; i++) {
                    lua_rawgeti(L, -1, i);
                    if (lua_isstring(L, -1)) items.push_back(lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);

            auto dialog = std::make_shared<views::FilterListDialog>(
                [](views::IDialog&) {},
                views::FilterListDialog::arguments{
                    .title_ = title,
                    .message_ = message,
                    .items_ = std::move(items),
                }
            );
            auto dialog_ptr = Script::PushSharedUserdata<views::FilterListDialog>(L, dialog);
            app.view_->RequestDialog(dialog_ptr);
            return 1;
        }

        luaL_error(L, "unknown dialog type");
        return 0;
    }

    int Application::lua_private_dialog_is_open(lua_State* L)
    {
        Script::CheckArgType(L, 1, LUA_TUSERDATA);
        auto ud = static_cast<std::shared_ptr<views::IDialog>*>(lua_touserdata(L, 1));
        lua_pushboolean(L, (*ud)->IsOpened());
        return 1;
    }

    int Application::lua_private_dialog_result(lua_State* L)
    {
        Script::CheckArgType(L, 1, LUA_TUSERDATA);
        Script::CheckArgType(L, 2, LUA_TSTRING);

        const std::string type_string = lua_tostring(L, 2);
        if (type_string == "confirm") {
            lua_pushboolean(L, true);
            return 1;
        }
        else if (type_string == "yesno") {
            auto ud = static_cast<std::shared_ptr<views::YesNoDialog>*>(lua_touserdata(L, 1));
            lua_pushboolean(L, (*ud)->Result());
            return 1;
        }
        else if (type_string == "inputtext") {
            auto ud = static_cast<std::shared_ptr<views::InputTextDialog>*>(lua_touserdata(L, 1));
            auto& result = (*ud)->Result();
            if (result) {
                lua_pushstring(L, result->c_str());
            } else {
                lua_pushnil(L);
            }
            return 1;
        }
        else if (type_string == "custom") {
            auto ud = static_cast<std::shared_ptr<views::CustomDialog>*>(lua_touserdata(L, 1));
            auto& result = (*ud)->Result();
            if (!result) {
                lua_pushnil(L);
                return 1;
            }

            lua_newtable(L);

            // 閉じた理由は、ボタン(button)か、選択リストの行(select)のどちらか一方だけ。どちらも 0-based → 1-based
            if (result->button_index) {
                lua_pushinteger(L, *result->button_index + 1);
                lua_setfield(L, -2, "button");
            }
            if (result->select_index) {
                lua_pushinteger(L, *result->select_index + 1);
                lua_setfield(L, -2, "select");
            }

            lua_newtable(L);
            for (int i = 0; i < (int)result->checkboxes.size(); i++) {
                lua_pushboolean(L, result->checkboxes[i]);
                lua_rawseti(L, -2, i + 1);
            }
            lua_setfield(L, -2, "checkboxes");

            return 1;
        }
        else if (type_string == "filterlist") {
            auto ud = static_cast<std::shared_ptr<views::FilterListDialog>*>(lua_touserdata(L, 1));
            auto& result = (*ud)->Result();
            if (result) {
                lua_pushstring(L, result->c_str());
            } else {
                lua_pushnil(L);
            }
            return 1;
        }
        return 0;
    }

}
