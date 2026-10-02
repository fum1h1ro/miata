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

namespace miata {
    // tidx にあるテーブル(title/message/buttons/checkboxes/selects)から CustomDialogSpec を組み立てる。
    static CustomDialogSpec ParseCustomDialogSpec(lua_State* L, int tidx)
    {
        auto get_str = [&](int idx, const char* key) -> std::string {
            lua_getfield(L, idx, key);
            std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
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
                if (lua_isstring(L, -1)) spec.buttons.push_back(lua_tostring(L, -1));
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

        // selects
        lua_getfield(L, tidx, "selects");
        if (lua_istable(L, -1)) {
            int n = (int)lua_rawlen(L, -1);
            for (int i = 1; i <= n; i++) {
                lua_rawgeti(L, -1, i);
                if (lua_istable(L, -1)) {
                    CustomDialogSelect sel;
                    sel.label = get_str(lua_gettop(L), "label");
                    lua_getfield(L, -1, "selected");
                    sel.selected = lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) - 1 : 0; // 1-based → 0-based
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "options");
                    if (lua_istable(L, -1)) {
                        int m = (int)lua_rawlen(L, -1);
                        for (int j = 1; j <= m; j++) {
                            lua_rawgeti(L, -1, j);
                            if (lua_isstring(L, -1)) sel.options.push_back(lua_tostring(L, -1));
                            lua_pop(L, 1);
                        }
                    }
                    lua_pop(L, 1);
                    spec.selects.push_back(std::move(sel));
                }
                lua_pop(L, 1);
            }
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
    }

    void Application::KeyDown(uint16_t key_code, uint16_t mods)
    {
        auto& script = Script::Instance();
        auto& kb = (view_->IsAnyDialogOpened())? key_binding_map_[(int)KeyBindingMap::Dialog] : key_binding_map_[(int)KeyBindingMap::Normal];

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
            pl_play_beep();
            key_stroke_.Clear();
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
        if (sources.empty() && src_model.Size() > 0) {
            sources.push_back(view_->CurrentEntry().Path());
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

    int Application::lua_command_bind(lua_State* L)
    {
        //auto& script = miata::Script::Instance();
        auto& app = Application::Instance();
        //int n = lua_gettop(L);
        Script::CheckArgType(L, 1, LUA_TSTRING);
        Script::CheckArgType(L, 2, LUA_TSTRING);
        Script::CheckArgType(L, 3, LUA_TFUNCTION);

        const std::string map_string = lua_tostring(L, 1);
        const char* key_string = lua_tostring(L, 2);
        //std::println("key_stroke: {}\n", key_string);
        lua_pushvalue(L, 3);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        //std::println("lua_command_bind {}\n", ref);

        for (size_t i = 0; i < map_string.size(); ++i) {
            auto map_c = map_string[i];
            auto map_index = app.GetKeyBinding((char)map_c);
            if (map_index) {
                auto& kb = app.key_binding_map_[(size_t)map_index.value()];
                auto r = kb->Register(key_string, ref);
                if (!r) {
                    luaL_unref(L, LUA_REGISTRYINDEX, ref);
                    luaL_error(L, "key binding error: %d", r.error());
                }
            }
            else {
                luaL_unref(L, LUA_REGISTRYINDEX, ref);
                luaL_error(L, "unknown map");
            }
        }
        return 0;
    }

    int Application::lua_command_unbind(lua_State* L)
    {
        //auto& script = miata::Script::Instance();
        auto& app = Application::Instance();
        //int n = lua_gettop(L);
        Script::CheckArgType(L, 1, LUA_TSTRING);
        Script::CheckArgType(L, 2, LUA_TSTRING);

        const std::string map_string = lua_tostring(L, 1);
        const char* key_string = lua_tostring(L, 2);

        for (size_t i = 0; i < map_string.size(); ++i) {
            auto map_c = map_string[i];
            auto map_index = app.GetKeyBinding((char)map_c);
            if (map_index) {
                auto& kb = app.key_binding_map_[(size_t)map_index.value()];
                auto r = kb->Unregister(key_string);
                if (r) {
                    luaL_unref(L, LUA_REGISTRYINDEX, r.value());
                }
                else {
                    luaL_error(L, "key binding error: %d", r.error());
                }
            }
            else {
                luaL_error(L, "unknown map");
            }
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

        if (list.Size() == 0 || AnyMarked(list)) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushstring(L, app.view_->CurrentEntry().Name().c_str());
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
        if (list.Size() == 0 || AnyMarked(list)) {
            lua_pushboolean(L, false);
            return 1;
        }

        auto& entry = app.view_->CurrentEntry();
        auto dest = list.Path() / new_name;

        std::error_code ec;
        std::filesystem::rename(entry.Path(), dest, ec);

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

    // jump_toに渡せる形か: 絶対パスで、各要素が空・"."・".." のどれでもない(末尾の/は許す)。".." などを含むと、画面の
    // 「親へ戻る」(パスの字句上の親)や履歴が、実際に開いた場所と食い違う(例: "/a/b/.." を開いた後のhが、"/a" ではなく
    // "/a/b" へ移る)。luaL_errorの前に呼ぶので、デストラクタを持つオブジェクトは作らない(string_viewだけで判定する)。
    static bool IsPlainAbsolutePath(std::string_view path)
    {
        if (path.empty() || path[0] != '/') return false;
        while (path.size() > 1 && path.back() == '/') path.remove_suffix(1);
        if (path.size() == 1) return true; // "/"
        path.remove_prefix(1);
        while (true) {
            auto slash = path.find('/');
            auto element = path.substr(0, slash);
            if (element.empty() || element == "." || element == "..") return false;
            if (slash == std::string_view::npos) return true;
            path.remove_prefix(slash + 1);
        }
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
        const std::string key_string = lua_tostring(L, 1);

        auto reverse = false;
        if (lua_gettop(L) >= 2) {
            Script::CheckArgType(L, 2, LUA_TBOOLEAN);
            reverse = lua_toboolean(L, 2);
        }

        views::FileListView::SortKey key;
        if (key_string == "name") key = views::FileListView::SortKey::Name;
        else if (key_string == "size") key = views::FileListView::SortKey::Size;
        else if (key_string == "mtime") key = views::FileListView::SortKey::ModifiedTime;
        else if (key_string == "ext") key = views::FileListView::SortKey::Extension;
        else {
            luaL_error(L, "unknown sort key");
            return 0;
        }

        app.view_->CurrentFileListView().SetSort(key, reverse);
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
    // paneのペイン(省略またはnilなら現在のペイン。"left" / "right")が移動したフォルダの履歴を、新しい順の
    // フルパスの配列で返す。今いるフォルダは含まない。履歴が無ければ空の配列。
    // 履歴を選んで移るのは、Miata.command.history()(これと dialog_filter_list と jump_to を組み合わせたもの)。
    int Application::lua_command_history_list(lua_State* L)
    {
        auto& app = Application::Instance();
        auto pane = OptionalPaneArg(L, 1, app.view_->CurrentPane());

        auto& list = app.view_->GetList(pane);
        auto items = list.History().List(list.Path());
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
    // 許可の案内が付く)。そのフォルダがもう無ければ(消えた・フォルダでなくなった)、そのペインの履歴からも外す。
    int Application::lua_command_jump_to(lua_State* L)
    {
        auto& app = Application::Instance();

        Script::CheckArgType(L, 1, LUA_TSTRING);
        size_t length = 0;
        const char* raw = lua_tolstring(L, 1, &length);
        if (std::strlen(raw) != length || !IsPlainAbsolutePath(std::string_view(raw, length))) { // NULを含む場合も弾く
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

        const std::string type_string = lua_tostring(L, 1);
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

            lua_pushinteger(L, result->button_index + 1); // 0-based → 1-based
            lua_setfield(L, -2, "button");

            lua_newtable(L);
            for (int i = 0; i < (int)result->checkboxes.size(); i++) {
                lua_pushboolean(L, result->checkboxes[i]);
                lua_rawseti(L, -2, i + 1);
            }
            lua_setfield(L, -2, "checkboxes");

            lua_newtable(L);
            for (int i = 0; i < (int)result->selects.size(); i++) {
                lua_pushinteger(L, result->selects[i] + 1); // 0-based → 1-based
                lua_rawseti(L, -2, i + 1);
            }
            lua_setfield(L, -2, "selects");

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
