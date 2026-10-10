#include <cmath>
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
    // buttons / checkboxes は、寛容なパース(型違いを黙って捨てる)のまま。
    static void CheckCustomDialogSelect(lua_State* L, int tidx)
    {
        tidx = lua_absindex(L, tidx);

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
            { "mark_all", lua_command_mark_all },
            { "unmark_all", lua_command_unmark_all },
            { "invert_marks", lua_command_invert_marks },
            { "mark_range", lua_command_mark_range },
            { "mark_search_hits", lua_command_mark_search_hits },
            { "next_mark", lua_command_next_mark },
            { "prev_mark", lua_command_prev_mark },
            { "copy_marked", lua_command_copy_marked },
            { "move_marked", lua_command_move_marked },
            { "make_directory", lua_command_make_directory },
            { "delete_marked", lua_command_delete_marked },
            { "current_pane", lua_command_current_pane },
            { "pane_path", lua_command_pane_path },
            { "cursor_entry", lua_command_cursor_entry },
            { "marked_entries", lua_command_marked_entries },
            { "current_sort", lua_command_current_sort },
            { "reload", lua_command_reload },
            { "quick_look", lua_command_quick_look },
            { "quick_look_zoom", lua_command_quick_look_zoom },
            { "sort", lua_command_sort },
            { "search", lua_command_search },
            { "search_next", lua_command_search_next },
            { "search_prev", lua_command_search_prev },
            { "search_clear", lua_command_search_clear },
            { "filter", lua_command_filter },
            { "filter_set", lua_command_filter_set },
            { "filter_clear", lua_command_filter_clear },
            { "history_list", lua_command_history_list },
            { "jump_to", lua_command_jump_to },
            { "set_clipboard", lua_command_set_clipboard },
            { "toggle_icons", lua_command_toggle_icons },
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
            { "paths_of", lua_private_paths_of },
            { "open_paths", lua_private_open_paths },
            { "reveal_paths", lua_private_reveal_paths },
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
        // 入力バー(検索・絞り込み)の入力欄とペインの状態を整える。自動リロードの後に行う(リロードで検索や絞り込みの件数が変わる)。
        // バーの出入りで一覧が縮むとき、Quick Lookの覆いが追従するのはレイアウトの側で行うので、UpdateQuickLookとの順序は問わない
        view_->UpdateQueryBars();
        // 自動リロードの後に行う(リロードで動いたカーソルに、同じティックで追従を始められるように)
        view_->UpdateQuickLook();
        // 進捗パネル(右上)を、実行中の操作に合わせる。完了は、上の file_operations_.Update() のコールバック
        // (OnFileOperationCompleted)が、View::FinishProgress で伝えている(失敗のダイアログと同じティックで、パネルが消える)
        view_->UpdateProgress(std::chrono::steady_clock::now(), file_operations_.Running());
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

    // paneのビューの、見えている(画面に出ている行の)マーク済みのファイルのパス(画面の並び順)。絞り込みで隠れている行の
    // マークは含まない。コピー・移動・ゴミ箱・リネームの「操作の対象」は、すべてこれで決める
    static std::vector<std::filesystem::path> MarkedPaths(views::FileListView& view)
    {
        std::vector<std::filesystem::path> paths;
        for (auto* entry : view.MarkedEntries()) paths.push_back(entry->Path());
        return paths;
    }

    void Application::StartFileOperation(FileOpType type)
    {
        auto& src_model = view_->CurrentList();
        auto& dest_model = view_->OtherList();

        auto sources = MarkedPaths(view_->CurrentFileListView());
        if (sources.empty()) {
            // マークが無ければ、カーソル下の1件(一覧が空なら、対象が無い)
            if (auto* current = view_->CurrentEntry()) sources.push_back(current->Path());
        }
        if (sources.empty()) return;

        auto src_dir = src_model.Path();
        auto dest_dir = dest_model.Path();

        // 元と先の関係が、データを失う・暴走する組み合わせなら、始める前に断る(同じフォルダ・フォルダを自分の中へ・
        // 先の同名のフォルダが元の祖先)。上書きの確認より前に行う(確認に答えさせても、どちらを選んでも進められないため)。
        // 調べられなかったとき(元が外部で消えた、権限が無いなど)は断らず、項目の失敗として知らせる
        {
            FileOperationGuard guard(dest_dir);
            for (auto& src : sources) {
                bool unknown = false;
                if (auto reason = guard.Check(type, src, unknown)) {
                    view_->ReportFileError(
                        std::format("{}できませんでした ({})", FileOpLabel(type), src.filename().string()),
                        FileError{.message = *reason}
                    );
                    return;
                }
            }
        }

        // 壊れたシンボリックリンクも、同名として数える(コピー・移動が、リンクをたどらずに、同名があるかを見るのと合わせる。
        // 数えないと、確認が出ないまま、黙ってスキップされる)
        auto conflicts = 0;
        for (auto& src : sources) {
            if (ExistsNoFollow(dest_dir / src.filename())) ++conflicts;
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
        // 進捗パネルに伝える。成功なら「完了」を見せてから消え、失敗なら、その場で消える(下で、ダイアログで知らせる)
        view_->FinishProgress(event.id, event.success, std::chrono::steady_clock::now());

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
                // 中身は変わらないので、操作したファイルのマークだけ解除する(画面にも反映する)
                view_->UnmarkPaths(*event.src_model, event.sources);
            }
        }

        if (!event.success) {
            view_->ReportFileError(
                std::format("エラーが発生しました（{}件失敗）", event.failed_count),
                FileError{.message = event.error_message, .permission_denied = event.permission_denied}
            );
        }
        // 成功したときは、何も出さない(進捗パネルが「完了」を見せる。0.3 秒より早く終わった操作は、一覧の更新が、完了の合図になる)
    }

    void Application::DeleteMarked()
    {
        auto& list = view_->CurrentList();

        auto targets = MarkedPaths(view_->CurrentFileListView());
        if (targets.empty()) return; // 見えているマークが無ければ何もしない

        auto dir = list.Path();
        auto message = std::format("{}件をゴミ箱に移動しますか？", targets.size());
        // 絞り込みで隠れているマークは、対象にならない(見えていないものを、うっかり消さないため)。件数を知らせる
        if (auto hidden = view_->CurrentFileListView().HiddenMarkCount(); hidden > 0) {
            message += std::format("\n(絞り込みで隠れているマーク {} 件は対象外です)", hidden);
        }

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

    // Miata.command.toggle_icons() -> boolean
    // ファイル名の頭のアイコンの表示を、入り切りする(AFXWの . と同じ)。切り替えた後の状態(出していればtrue)を返す。
    // Miata.config.set_show_icons で決めた値を書き換えるだけで、保存はしない(起動し直すと、設定が決める)。
    // 両ペインを描き直す(フォーカスのあるペインは、カーソルの行へスクロールする。マークの変更などと同じ)。
    int Application::lua_command_toggle_icons(lua_State* L)
    {
        auto& app = Application::Instance();
        const bool show = !Config::ShowIcons();
        Config::SetShowIcons(show);
        app.view_->GetFileListView(views::constants::Pane::Left).Redraw();
        app.view_->GetFileListView(views::constants::Pane::Right).Redraw();
        lua_pushboolean(L, show);
        return 1;
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

    // 見えている行に、マークが1件でもあれば true(rename系コマンドは単一ファイルのみ対応のため無効化する)。
    // 絞り込みで隠れている行のマークは数えない(見えないマークのせいで、リネームできなくならないように)
    static bool AnyVisibleMarked(views::View& view)
    {
        return !view.CurrentFileListView().MarkedEntries().empty();
    }

    // 見えているマークがある、またはリストが空ならnil。それ以外はカーソル位置のエントリ名を返す。
    int Application::lua_private_rename_target(lua_State* L)
    {
        auto& app = Application::Instance();

        auto* current = app.view_->CurrentEntry();
        if (!current || AnyVisibleMarked(*app.view_)) {
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
        if (!entry || AnyVisibleMarked(*app.view_)) {
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

    // 省略可の一致のしかた引数(Luaの引数のindex番目)を読む。省略またはnilなら部分一致。"substring" / "fuzzy" 以外は、
    // Luaのエラー(luaL_errorはlongjmpなので、呼ぶ前にC++のオブジェクトを作らないこと)。
    static views::MatchKind OptionalMatchKindArg(lua_State* L, int index)
    {
        if (lua_gettop(L) < index || lua_isnil(L, index)) return views::MatchKind::Substring;
        Script::CheckArgType(L, index, LUA_TSTRING);
        size_t length = 0;
        const char* raw = lua_tolstring(L, index, &length);
        auto parsed = views::ParseMatchKind(std::string_view(raw, length));
        if (!parsed) {
            luaL_error(L, "unknown match mode: %s (expected \"substring\" or \"fuzzy\")", raw);
            return views::MatchKind::Substring; // luaL_errorは戻らない
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

    // エントリを { name, path, is_dir } のテーブルにして積む(Luaに公開する形)。
    // name: 画面に出る名前(NFC。UTF-8として不正なバイトはU+FFFDに置き換え済みなので、この名前でパスを作ると
    // そのファイルを指さないことがある)。path: そのファイルを指す絶対パス(元のバイト列のまま)。
    // is_dir: フォルダか(シンボリックリンクはたどる。Enterでフォルダに入れるか、と同じ)。
    static void PushEntryTable(lua_State* L, const models::FileEntryModel& entry)
    {
        lua_createtable(L, 0, 3);
        const std::string& name = entry.Name();
        lua_pushlstring(L, name.data(), name.size());
        lua_setfield(L, -2, "name");
        const std::string path = entry.Path().string();
        lua_pushlstring(L, path.data(), path.size());
        lua_setfield(L, -2, "path");
        lua_pushboolean(L, entry.IsDirectory());
        lua_setfield(L, -2, "is_dir");
    }

    // Miata.command.pane_path([pane]) -> string
    // paneのペイン("left" / "right"。省略またはnilなら現在のペイン)の、いまのフォルダの絶対パス。
    // 末尾に "/" は付かない(ルートだけ "/")。状態は変えない。
    int Application::lua_command_pane_path(lua_State* L)
    {
        auto& app = Application::Instance();
        auto pane = OptionalPaneArg(L, 1, app.view_->CurrentPane());

        // ここから先はluaL_errorを呼ばない(std::stringを作るため)
        const std::string path = app.view_->GetList(pane).Path().string();
        lua_pushlstring(L, path.data(), path.size());
        return 1;
    }

    // Miata.command.cursor_entry([pane]) -> { name, path, is_dir } | nil
    // paneのペインの、カーソル下のエントリ。一覧が空(ファイルもフォルダも1つも無い、または絞り込みで0行)ならnil。状態は変えない。
    int Application::lua_command_cursor_entry(lua_State* L)
    {
        auto& app = Application::Instance();
        auto pane = OptionalPaneArg(L, 1, app.view_->CurrentPane());

        auto* entry = app.view_->GetFileListView(pane).CurrentOrNull();
        if (!entry) {
            lua_pushnil(L);
            return 1;
        }
        PushEntryTable(L, entry->Model());
        return 1;
    }

    // Miata.command.marked_entries([pane]) -> { {name, path, is_dir}, ... }
    // paneのペインの、マーク済みのエントリ(画面の並び順。絞り込み中は、見えている行のマークだけ)。マークが無ければ空の配列(カーソル下の1件には
    // 置き換えない: copy_marked / move_marked の「マークが無ければカーソル下」は、呼ぶ側で cursor_entry と組み合わせる)。
    // 状態は変えない。
    int Application::lua_command_marked_entries(lua_State* L)
    {
        auto& app = Application::Instance();
        auto pane = OptionalPaneArg(L, 1, app.view_->CurrentPane());

        // ここから先はluaL_errorを呼ばない(vectorを作るため)
        const auto marked = app.view_->GetFileListView(pane).MarkedEntries();
        lua_createtable(L, (int)marked.size(), 0);
        for (size_t i = 0; i < marked.size(); ++i) {
            PushEntryTable(L, *marked[i]);
            lua_rawseti(L, -2, (lua_Integer)i + 1);
        }
        return 1;
    }

    // Miata.command.current_sort([pane]) -> key, reverse
    // paneのペインのソートの状態。keyは "name" / "size" / "mtime" / "ext"、reverseは降順ならtrue。
    // sort(key, reverse) に、そのまま渡せる形(ただしsortはカーソルのあるペインだけに効く)。状態は変えない。
    int Application::lua_command_current_sort(lua_State* L)
    {
        auto& app = Application::Instance();
        auto pane = OptionalPaneArg(L, 1, app.view_->CurrentPane());

        auto& list_view = app.view_->GetFileListView(pane);
        lua_pushstring(L, views::FileListView::SortKeyName(list_view.GetSortKey()));
        lua_pushboolean(L, list_view.GetSortReverse());
        return 2;
    }

    // --- マークの一括操作(mark_all / unmark_all / invert_marks / mark_range / mark_search_hits / next_mark / prev_mark) ---
    // どれも、最後の引数に省略できる opts(表)を取る。項目は pane / kind / mode / from のうち、コマンドごとに許すものだけ。
    // 許さない項目や知らない項目は、エラーにする(つづりを間違えた kind が黙って無視されると、意図しない種類がマークされて、
    // そのまま移動や削除に使われてしまうため)。
    using MarkMode = views::FileListView::MarkMode;
    using MarkKind = views::FileListView::MarkKind;
    using MarkRangeFrom = views::FileListView::MarkRangeFrom;

    static std::optional<MarkKind> ParseMarkKind(const char* name)
    {
        if (std::strcmp(name, "all") == 0) return MarkKind::All;
        if (std::strcmp(name, "files") == 0) return MarkKind::Files;
        if (std::strcmp(name, "dirs") == 0) return MarkKind::Dirs;
        return std::nullopt;
    }

    static std::optional<MarkMode> ParseMarkMode(const char* name)
    {
        if (std::strcmp(name, "mark") == 0) return MarkMode::Mark;
        if (std::strcmp(name, "unmark") == 0) return MarkMode::Unmark;
        if (std::strcmp(name, "toggle") == 0) return MarkMode::Toggle;
        return std::nullopt;
    }

    static std::optional<MarkRangeFrom> ParseMarkRangeFrom(const char* name)
    {
        if (std::strcmp(name, "above") == 0) return MarkRangeFrom::Above;
        if (std::strcmp(name, "below") == 0) return MarkRangeFrom::Below;
        return std::nullopt;
    }

    // optsの項目の値(lua_nextで走査している、スタックの一番上)を、parseで読む。文字列でない値や、parseできない値は、
    // Luaのエラー(luaL_errorはlongjmpなので、デストラクタを持つオブジェクトを作らない。std::optional<列挙型>は
    // デストラクタを持たないので使ってよい)
    template <typename T>
    static T ReadMarkOptValue(lua_State* L, const char* name, std::optional<T> (*parse)(const char*), const char* expected)
    {
        if (lua_type(L, -1) != LUA_TSTRING) {
            luaL_error(L, "option '%s' must be a string (%s)", name, expected);
            return T{}; // luaL_errorは戻らない
        }
        auto parsed = parse(lua_tostring(L, -1));
        if (!parsed) {
            luaL_error(L, "unknown value for option '%s': %s (expected %s)", name, lua_tostring(L, -1), expected);
            return T{};
        }
        return *parsed;
    }

    // optsから読む項目の置き場。置き場の初期値が、省略したときの値。paneは、全てのコマンドが使える。ほかは、nullptrの項目を、
    // このコマンドが許さない(指定されたらエラー)
    struct MarkOptSlots {
        views::constants::Pane& pane;
        MarkKind* kind = nullptr;
        MarkMode* mode = nullptr;
        MarkRangeFrom* from = nullptr;
    };

    // Luaの引数のindex番目のopts(省略・nilなら、何もしない)を、slotsに読む。走査はraw(メタテーブルのコードは走らない)。
    // 文字列でない名前は、lua_tostringが数値のキーを文字列に変えて、lua_nextを壊すので、型を先に見る
    static void ReadMarkOpts(lua_State* L, int index, const MarkOptSlots& slots)
    {
        if (lua_gettop(L) < index || lua_isnil(L, index)) return;
        if (!lua_istable(L, index)) {
            luaL_error(L, "options must be a table");
            return;
        }
        lua_pushnil(L);
        while (lua_next(L, index) != 0) {
            if (lua_type(L, -2) != LUA_TSTRING) {
                luaL_error(L, "option names must be strings");
                return;
            }
            const char* name = lua_tostring(L, -2);
            if (std::strcmp(name, "pane") == 0) {
                slots.pane = ReadMarkOptValue<views::constants::Pane>(L, name, ParsePane, "\"left\" or \"right\"");
            }
            else if (slots.kind && std::strcmp(name, "kind") == 0) {
                *slots.kind = ReadMarkOptValue<MarkKind>(L, name, ParseMarkKind, "\"all\", \"files\" or \"dirs\"");
            }
            else if (slots.mode && std::strcmp(name, "mode") == 0) {
                *slots.mode = ReadMarkOptValue<MarkMode>(L, name, ParseMarkMode, "\"mark\", \"unmark\" or \"toggle\"");
            }
            else if (slots.from && std::strcmp(name, "from") == 0) {
                *slots.from = ReadMarkOptValue<MarkRangeFrom>(L, name, ParseMarkRangeFrom, "\"above\" or \"below\"");
            }
            else {
                luaL_error(L, "unknown option: %s", name);
                return;
            }
            lua_pop(L, 1); // 値だけ捨てる(名前は、次のlua_nextに要る)
        }
    }

    // 一括操作の結果(対象になった数、実際に変わった数)を、Luaの戻り値として積む
    static int PushMarkResult(lua_State* L, const views::FileListView::MarkResult& result)
    {
        lua_pushinteger(L, result.matched);
        lua_pushinteger(L, result.changed);
        return 2;
    }

    // mark_all / unmark_all / invert_marks の共通部分。default_kindは、kindを省略したときの対象
    static int MarkAllCommand(lua_State* L, views::View& view, MarkMode mode, MarkKind default_kind)
    {
        auto pane = view.CurrentPane();
        auto kind = default_kind;
        ReadMarkOpts(L, 1, { .pane = pane, .kind = &kind });
        return PushMarkResult(L, view.GetFileListView(pane).MarkAll(mode, kind));
    }

    // next_mark / prev_mark の共通部分
    static int StepMarkCommand(lua_State* L, views::View& view, int dir)
    {
        auto pane = view.CurrentPane();
        ReadMarkOpts(L, 1, { .pane = pane });
        lua_pushboolean(L, view.GetFileListView(pane).StepMark(dir));
        return 1;
    }

    // Miata.command.mark_all([opts]) -> matched, changed
    // ペインの全ての行をマークする。opts: pane("left" / "right"。省略は現在のペイン)、kind("files" / "dirs" / "all"。
    // 省略は "files"=フォルダ以外)。戻り値は、対象になった行の数と、そのうち、マークの状態が実際に変わった数。
    int Application::lua_command_mark_all(lua_State* L)
    {
        return MarkAllCommand(L, *Application::Instance().view_, MarkMode::Mark, MarkKind::Files);
    }

    // Miata.command.unmark_all([opts]) -> matched, changed
    // ペインの、見えている行のマークを外す(絞り込み中は、隠れた行のマークは外さない)。opts と戻り値は mark_all と同じ。ただし、kind の省略は "all"(全解除は、フォルダのマークも
    // 残さない)。
    int Application::lua_command_unmark_all(lua_State* L)
    {
        return MarkAllCommand(L, *Application::Instance().view_, MarkMode::Unmark, MarkKind::All);
    }

    // Miata.command.invert_marks([opts]) -> matched, changed
    // ペインの全ての行のマークを反転する。opts と戻り値は mark_all と同じ(kind の省略は "files")。
    int Application::lua_command_invert_marks(lua_State* L)
    {
        return MarkAllCommand(L, *Application::Instance().view_, MarkMode::Toggle, MarkKind::Files);
    }

    // Miata.command.mark_range([opts]) -> matched, changed
    // カーソルより上(from = "above"。省略)または下("below")で、いちばん近いマークから、カーソルの行までを、全てマークする。
    // opts: pane、from。起点のマークが無ければ、何もしない(0, 0)。戻り値は、範囲の行数(端の2行を含む)と、新しくマークした数。
    int Application::lua_command_mark_range(lua_State* L)
    {
        auto& view = *Application::Instance().view_;
        auto pane = view.CurrentPane();
        auto from = MarkRangeFrom::Above;
        ReadMarkOpts(L, 1, { .pane = pane, .from = &from });
        return PushMarkResult(L, view.GetFileListView(pane).MarkRange(from));
    }

    // Miata.command.mark_search_hits([opts]) -> matched, changed
    // ペインの、いまの検索(入力中も確定後も)のヒットを、マークする。opts: pane、kind(省略は "all"=強調されている行を全て)、
    // mode("mark"(省略) / "unmark" / "toggle")。検索していなければ、何もしない(0, 0)。検索は終わらせない。
    int Application::lua_command_mark_search_hits(lua_State* L)
    {
        auto& view = *Application::Instance().view_;
        auto pane = view.CurrentPane();
        auto kind = MarkKind::All;
        auto mode = MarkMode::Mark;
        ReadMarkOpts(L, 1, { .pane = pane, .kind = &kind, .mode = &mode });
        return PushMarkResult(L, view.GetFileListView(pane).MarkSearchHits(mode, kind));
    }

    // Miata.command.next_mark([opts]) / prev_mark([opts]) -> boolean
    // ペインのカーソルを、次・前のマーク済みの行へ動かす(端でラップ。カーソルの行そのものは含まない)。opts: pane。
    // 動けたらtrue。マーク済みの行が(カーソルの行以外に)無くて動けなければfalse(beepは鳴らさない)。
    int Application::lua_command_next_mark(lua_State* L)
    {
        return StepMarkCommand(L, *Application::Instance().view_, 1);
    }

    int Application::lua_command_prev_mark(lua_State* L)
    {
        return StepMarkCommand(L, *Application::Instance().view_, -1);
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

    // "in" / "out" の1回あたりの倍率(今の倍率に掛ける・割る)
    static constexpr double kQuickLookZoomStep = 1.25;

    // Miata.command.quick_look_zoom([zoom]) -> number | nil
    // Quick Lookのプレビューの倍率(1.0 = 100%)を指定して、指定した後の倍率を返す。zoomは、数値(その倍率。0.25〜4.0に丸める)、
    // "in"(今の倍率の1.25倍) / "out"(1.25で割る) / "reset"(100%)。省略・nilなら、変えずに今の倍率を返す。
    // 表示していない・読み込み中・ズームできない種類(画像・PDF・json)のときは、何もせずnilを返す
    // (ズームできる種類は、QuickLookView::Zoomを参照)。ファイルを切り替えると、100%に戻る。
    int Application::lua_command_quick_look_zoom(lua_State* L)
    {
        auto& app = Application::Instance();

        // 引数を、倍率の指定に直す。luaL_error(longjmp)の前に、デストラクタを持つオブジェクトを作らない
        // (doubleとstd::string_viewは、持たないので使ってよい)
        enum class Op { Get, Set, In, Out, Reset };
        auto op = Op::Get;
        double factor = 1.0;
        if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
            if (lua_type(L, 1) == LUA_TNUMBER) {
                factor = lua_tonumber(L, 1);
                if (!std::isfinite(factor) || factor <= 0) {
                    luaL_error(L, "quick_look_zoom: the zoom must be a finite positive number");
                    return 0;
                }
                op = Op::Set;
            }
            else if (lua_type(L, 1) == LUA_TSTRING) {
                size_t len = 0;
                const char* s = lua_tolstring(L, 1, &len);
                const std::string_view name(s, len);
                if (name == "in") {
                    op = Op::In;
                }
                else if (name == "out") {
                    op = Op::Out;
                }
                else if (name == "reset") {
                    op = Op::Reset;
                }
                else {
                    luaL_error(L, "quick_look_zoom: unknown zoom: %s (expected a number, \"in\", \"out\" or \"reset\")", s);
                    return 0;
                }
            }
            else {
                luaL_error(L, "quick_look_zoom: expected a number, \"in\", \"out\" or \"reset\"");
                return 0;
            }
        }

        auto zoom = app.view_->QuickLookZoom();
        if (zoom && op != Op::Get) {
            switch (op) {
            case Op::In: factor = *zoom * kQuickLookZoomStep; break;
            case Op::Out: factor = *zoom / kQuickLookZoomStep; break;
            case Op::Reset: factor = 1.0; break;
            default: break; // Set: 引数の倍率のまま
            }
            zoom = app.view_->SetQuickLookZoom(factor);
        }
        if (zoom) {
            lua_pushnumber(L, *zoom);
        }
        else {
            lua_pushnil(L);
        }
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

    // Miata.command.filter([mode]) -> boolean
    // カーソルのペインで、絞り込みを始める。そのペインの下(検索バーの上)に入力欄が出て、打つたびに、名前に語を含む行だけが
    // 一覧に残る(入力中はNormalのキーバインドは効かない。↑↓でカーソルを動かし、Enterで確定、Escで取り消し)。
    // modeは、語の一致のしかた: "substring"(部分一致。省略・nilも同じ。一致した部分を強調する)、"fuzzy"(あいまい一致。外部の
    // fzfに任せる。一覧は得点順で、強調しない)。それ以外はエラー。
    // 確定済みの絞り込みがあるときに呼ぶと、全行に戻って、語を空から入力し直す(Escで前の絞り込みに戻る)。語を空のまま
    // Enterすると、絞り込みを解除する。始められたらtrue。ダイアログの表示中や、すでに入力中のときはfalse。
    int Application::lua_command_filter(lua_State* L)
    {
        auto& app = Application::Instance();
        auto kind = OptionalMatchKindArg(L, 1);
        lua_pushboolean(L, app.view_->BeginFilter(kind));
        return 1;
    }

    // Miata.command.filter_set(query, [pane], [mode]) -> shown, total
    // paneのペイン(省略またはnilなら現在のペイン)の絞り込みを、入力欄を使わずに、queryの確定済みにする。空の文字列なら解除する。
    // modeは一致のしかた("substring" / "fuzzy"。filterと同じ)。
    // カーソルは、今のファイルに留まる(隠れたら、次に見えるファイルへ。あいまい一致では、先頭の行へ)。入力中なら、先に終わらせる。戻り値は、
    // 絞り込み後の行数と、全行数。UTF-8として不正なバイトは、U+FFFDにする。NULを含む語はエラー。
    // ダイアログの表示中でも動く(閉じた直後のティックでは、閉じたはずのダイアログがまだ「開いている」扱いなので。jump_toと同じ)。
    int Application::lua_command_filter_set(lua_State* L)
    {
        auto& app = Application::Instance();

        Script::CheckArgType(L, 1, LUA_TSTRING);
        size_t length = 0;
        const char* raw = lua_tolstring(L, 1, &length);
        // luaL_errorの前なので、デストラクタを持つオブジェクトを作らない
        if (std::memchr(raw, 0, length) != nullptr) {
            luaL_error(L, "filter_set: the query must not contain NUL");
            return 0;
        }
        auto pane = OptionalPaneArg(L, 2, app.view_->CurrentPane());
        auto kind = OptionalMatchKindArg(L, 3);

        // ここから先はluaL_errorを呼ばない(std::stringなどを作るため)
        auto status = app.view_->SetFilter(pane, RepairUtf8(std::string(raw, length)), kind);
        lua_pushinteger(L, status.shown);
        lua_pushinteger(L, status.total);
        return 2;
    }

    // Miata.command.filter_clear([pane]) -> boolean
    // paneのペイン(省略またはnilなら現在のペイン)の絞り込みを解除する。カーソルは、今のファイルに留まる。絞り込んでいたらtrue。
    // 通常時のEsc(navigate_cancel)は、絞り込みを解除しない。
    int Application::lua_command_filter_clear(lua_State* L)
    {
        auto& app = Application::Instance();
        auto pane = OptionalPaneArg(L, 1, app.view_->CurrentPane());
        lua_pushboolean(L, app.view_->ClearFilter(pane));
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
    // カーソルは先頭、マーク・検索・絞り込みは消える。フォーカスは動かさない)。pathは絶対パスのみ("~" は展開しない。
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

    // Miata.command.set_clipboard(text) -> boolean
    // textをクリップボードに置く。UTF-8として不正なバイトは、U+FFFDにして置く。置けたらtrue。
    int Application::lua_command_set_clipboard(lua_State* L)
    {
        Script::CheckArgType(L, 1, LUA_TSTRING);
        size_t length = 0;
        const char* text = lua_tolstring(L, 1, &length);
        lua_pushboolean(L, pl_set_clipboard_text(std::string(text, length)));
        return 1;
    }

    // 開く・Finderで表示するコマンドの対象のパスとして使えるか(Luaの文字列のバイト列で判定する): 空でなく、"/" で始まる
    // 絶対パスで、NULを含まない。相対パスは起動した場所で意味が変わるので受け付けない("~" も展開しない)。
    // ".." や "//" は、開くだけなので、そのまま通す(jump_to と違い、ペインの場所にはしない)
    static bool IsOpenablePath(const char* s, size_t length)
    {
        return length > 0 && s[0] == '/' && std::memchr(s, '\0', length) == nullptr;
    }

    // スタックのidxの値が、1件の対象(パスの文字列、またはパスを持つエントリ { path = 文字列 })なら、そのパスの文字列を
    // スタックに積んでtrue。そうでなければ、何も積まずにfalse。Luaの C API だけを使う(luaL_errorの前に呼んでよい)
    static bool PushTargetPath(lua_State* L, int idx)
    {
        idx = lua_absindex(L, idx);
        if (lua_type(L, idx) == LUA_TSTRING) {
            lua_pushvalue(L, idx);
            return true;
        }
        if (lua_type(L, idx) == LUA_TTABLE) {
            lua_getfield(L, idx, "path");
            if (lua_type(L, -1) == LUA_TSTRING) return true;
            lua_pop(L, 1);
        }
        return false;
    }

    // Miata._private.paths_of(target) -> string[]
    // open などのコマンドの対象を、絶対パスの文字列の配列にする。targetは、パスの文字列、エントリ({ path = 文字列 }。
    // cursor_entry の戻り値など)、またはそれらの配列(marked_entries の戻り値など)。空の配列は、空の配列になる。
    // それ以外(nil・数値・入れ子の配列・path が文字列でないエントリ・絶対パスでない文字列・NULを含む文字列)はエラー。
    // Luaの C API だけを使うので、デストラクタを持つオブジェクトは無い(エラーは longjmp)。
    int Application::lua_private_paths_of(lua_State* L)
    {
        if (lua_gettop(L) < 1 || lua_isnil(L, 1)) {
            luaL_error(L, "expected a path (string), an entry ({ path = ... }) or an array of them, got nil");
            return 0;
        }
        lua_settop(L, 1); // 余分な引数は無視する

        // pathを持つ表はエントリ(1件)、持たない表は配列
        bool single = false;
        if (lua_type(L, 1) == LUA_TSTRING) {
            single = true;
        }
        else if (lua_type(L, 1) == LUA_TTABLE) {
            lua_getfield(L, 1, "path");
            single = !lua_isnil(L, -1);
            lua_pop(L, 1);
        }
        else {
            luaL_error(L, "expected a path (string), an entry ({ path = ... }) or an array of them, got %s", luaL_typename(L, 1));
            return 0;
        }

        lua_newtable(L); // 結果(index 2)
        if (single) {
            if (!PushTargetPath(L, 1)) {
                luaL_error(L, "entry.path must be a string");
                return 0;
            }
            size_t length = 0;
            const char* s = lua_tolstring(L, -1, &length);
            if (!IsOpenablePath(s, length)) {
                luaL_error(L, "the path must be an absolute path (starting with \"/\") and must not contain NUL");
                return 0;
            }
            lua_rawseti(L, 2, 1);
        }
        else {
            const lua_Integer count = (lua_Integer)lua_rawlen(L, 1);
            for (lua_Integer i = 1; i <= count; ++i) {
                lua_rawgeti(L, 1, i);
                if (!PushTargetPath(L, -1)) {
                    luaL_error(L, "element %d: expected a path (string) or an entry ({ path = string })", (int)i);
                    return 0;
                }
                size_t length = 0;
                const char* s = lua_tolstring(L, -1, &length);
                if (!IsOpenablePath(s, length)) {
                    luaL_error(L, "element %d: the path must be an absolute path (starting with \"/\") and must not contain NUL", (int)i);
                    return 0;
                }
                lua_rawseti(L, 2, i); // パスの文字列を積み込む(積んだ分は減る)
                lua_pop(L, 1);        // 取り出した要素
            }
        }
        return 1; // 結果の表が、スタックの一番上
    }

    // Luaの値(idx)が、絶対パスの文字列の配列(paths_of の結果)かを調べる。違えばLuaのエラー(luaL_errorの前に、
    // デストラクタを持つオブジェクトを作らない)。空でもよい
    static void CheckPathArray(lua_State* L, int idx, const char* function)
    {
        Script::CheckArgType(L, idx, LUA_TTABLE);
        const lua_Integer count = (lua_Integer)lua_rawlen(L, idx);
        for (lua_Integer i = 1; i <= count; ++i) {
            lua_rawgeti(L, idx, i);
            size_t length = 0;
            const char* s = lua_type(L, -1) == LUA_TSTRING ? lua_tolstring(L, -1, &length) : nullptr;
            if (!s || !IsOpenablePath(s, length)) {
                luaL_error(L, "%s: element %d is not an absolute path string", function, (int)i);
                return;
            }
            lua_pop(L, 1);
        }
    }

    // CheckPathArray を通った配列から、パスを取り出す(エラーを起こさない)
    static std::vector<std::filesystem::path> ReadPathArray(lua_State* L, int idx)
    {
        std::vector<std::filesystem::path> paths;
        const lua_Integer count = (lua_Integer)lua_rawlen(L, idx);
        paths.reserve((size_t)count);
        for (lua_Integer i = 1; i <= count; ++i) {
            lua_rawgeti(L, idx, i);
            size_t length = 0;
            const char* s = lua_tolstring(L, -1, &length);
            paths.emplace_back(std::string(s, length));
            lua_pop(L, 1);
        }
        return paths;
    }

    // Miata._private.open_paths(paths, [app]) -> boolean
    // pathsは、絶対パスの文字列の配列(paths_of の結果。空でないこと)。appは、アプリの指定(名前・Bundle ID・絶対パス)で、
    // 省略・nil なら、ファイルごとの既定のアプリで開く。開く要求を出せたらtrue(開けたかは、あとで分かる)。
    // アプリが見つからないときは、ダイアログで知らせてfalse。要求のあとで分かる失敗(開くアプリが無い・ファイルが無い等)は、
    // あとで(メインスレッドで)ダイアログで知らせる。
    int Application::lua_private_open_paths(lua_State* L)
    {
        auto& app = Application::Instance();
        CheckPathArray(L, 1, "open_paths");
        if (lua_rawlen(L, 1) == 0) {
            luaL_error(L, "open_paths: paths is empty");
            return 0;
        }
        if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) Script::CheckArgType(L, 2, LUA_TSTRING);

        // ここから先はluaL_errorを呼ばない(std::stringなどを作るため)
        const auto paths = ReadPathArray(L, 1);
        std::string app_spec;
        if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
            size_t length = 0;
            const char* s = lua_tolstring(L, 2, &length);
            app_spec.assign(s, length);
        }

        auto result = pl_open_paths(paths, app_spec, [](int failed, int total, const std::string& message) {
            // メインスレッドで呼ばれる。要求のあとで分かった失敗
            const auto what = (failed == 1 && total == 1) ? std::string("開けませんでした") : std::format("{}件が開けませんでした ({}件中)", failed, total);
            Application::Instance().view_->ReportFileError(what, FileError{.message = message});
        });
        if (!result) {
            app.view_->ReportFileError("開けませんでした", FileError{.message = result.error()});
            lua_pushboolean(L, false);
            return 1;
        }
        lua_pushboolean(L, true);
        return 1;
    }

    // Miata._private.reveal_paths(paths) -> boolean
    // Finderで、pathsを選択した状態で表示する。pathsは、絶対パスの文字列の配列(paths_of の結果。空でないこと:
    // 空のときに何もしないのは、呼ぶ側の Lua の reveal)。
    int Application::lua_private_reveal_paths(lua_State* L)
    {
        CheckPathArray(L, 1, "reveal_paths");
        if (lua_rawlen(L, 1) == 0) {
            luaL_error(L, "reveal_paths: paths is empty");
            return 0;
        }

        // ここから先はluaL_errorを呼ばない(vectorを作るため)
        pl_reveal_paths(ReadPathArray(L, 1));
        lua_pushboolean(L, true);
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
