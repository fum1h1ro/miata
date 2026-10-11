#ifndef APPLICATION_H__
#define APPLICATION_H__

#include <array>
#include <memory>
#include <optional>
#include <lua.h>
#include "misc.h"
#include "KeyBinding.h"
#include "Script.h"
#include "views/View.h"
#include "views/StatusBarState.h"
#include "FileOperation.h"

namespace miata {
    class Application {
    public:
        enum class KeyBindingMap {
            Normal,
            Dialog,
            Max,
        };
        static inline Application& Instance()
        {
            static Application instance;
            return instance;
        }

        static void Initialize();


    private:
        Application();
        ~Application();
        void InitializeImpl();
        // Luaの準備(base.lua、コマンドの登録、設定の読み込み)。Viewを作る前に行う。
        // 設定の読み込みで起きたエラーを返す(Viewを作った後にReportConfigErrorsで画面に出す)
        std::vector<Script::ConfigError> InitializeScript();
        void ReportConfigErrors(const std::vector<Script::ConfigError>& errors);
        void Update(); // タイマーから定期的に呼ばれる(旧FrameImpl相当)
        // キー入力。本体は KeyDownImpl で、ここは、どの出口(早期の return を含む)の後でも、ステータスバーを合わせる
        void KeyDown(uint16_t key_code, uint16_t mods);
        void KeyDownImpl(uint16_t key_code, uint16_t mods);
        // ステータスバー(ウィンドウの下端)を、いまの状態に合わせる。毎ティック(Update)と、KeyDown の出口から呼ぶ。
        // 鍵(StatusBarState)が前回と同じなら、何もしない(Lua も lstat も呼ばない)。呼ぶのは、Lua も View の更新も走って
        // いない時点だけ(Lua の関数が、View の更新の途中に、入れ子で呼ばれないように)
        void UpdateStatusBar();
        void KeyUp(uint16_t key_code, uint16_t mods);
        void Resize(int width, int height);
        std::expected<KeyBindingMap, std::string> GetKeyBinding(const char map_c);
        // sourcesを、dest_dirへコピー・移動する(裏スレッド。確認は出さない)。始める前の確認(CheckTransfer)で断るなら、
        // ダイアログで知らせてfalse。始めたらtrue(sourcesが空ならfalse)。overwriteは、先に同名があるとき、上書きするか、
        // スキップするか(操作全体で1つ)
        bool StartTransfer(FileOpType type, std::vector<std::filesystem::path> sources, const std::filesystem::path& dest_dir, bool overwrite);
        // pathsを、ゴミ箱へ移す(同期。確認は出さない)。移した後に、そのフォルダを表示しているペインを再スキャンして、
        // 失敗は、まとめて1回のダイアログで知らせる。全部移せたらtrue(pathsが空ならfalse)
        bool TrashPaths(const std::vector<std::filesystem::path>& paths);
        // fromを、toへ移す(名前の変更。同じボリュームの中)。toに既に何かあれば(壊れたリンクも)、上書きせずに断る。
        // 失敗は、ダイアログで知らせてfalse。成功したら、関係するフォルダを表示しているペインを再スキャンして、true
        bool RenamePath(const std::filesystem::path& from, const std::filesystem::path& to);
        void OnFileOperationCompleted(FileOperationCompleted& event);


        static int lua_command_bind(lua_State* L);
        static int lua_command_unbind(lua_State* L);
        static int lua_command_navigate_up(lua_State* L);
        static int lua_command_navigate_down(lua_State* L);
        static int lua_command_navigate_left(lua_State* L);
        static int lua_command_navigate_right(lua_State* L);
        static int lua_command_navigate_ok(lua_State* L);
        static int lua_command_navigate_cancel(lua_State* L);
        static int lua_command_navigate_top(lua_State* L);
        static int lua_command_navigate_bottom(lua_State* L);
        static int lua_command_toggle_focus(lua_State* L);
        static int lua_command_mark(lua_State* L);
        static int lua_command_unmark(lua_State* L);
        static int lua_command_toggle_mark(lua_State* L);
        static int lua_command_mark_all(lua_State* L);
        static int lua_command_unmark_all(lua_State* L);
        static int lua_command_invert_marks(lua_State* L);
        static int lua_command_mark_range(lua_State* L);
        static int lua_command_mark_search_hits(lua_State* L);
        static int lua_command_next_mark(lua_State* L);
        static int lua_command_prev_mark(lua_State* L);
        // copy_to と move_to の共通の本体(kind だけが違う)
        static int TransferCommand(lua_State* L, FileOpType type);
        static int lua_command_check_transfer(lua_State* L);
        static int lua_command_copy_to(lua_State* L);
        static int lua_command_move_to(lua_State* L);
        static int lua_command_make_directory(lua_State* L);
        static int lua_command_rename_to(lua_State* L);
        static int lua_command_exists(lua_State* L);
        static int lua_command_trash(lua_State* L);
        static int lua_command_current_pane(lua_State* L);
        static int lua_command_pane_path(lua_State* L);
        static int lua_command_cursor_entry(lua_State* L);
        static int lua_command_marked_entries(lua_State* L);
        static int lua_command_hidden_mark_count(lua_State* L);
        static int lua_command_current_sort(lua_State* L);
        static int lua_command_reload(lua_State* L);
        static int lua_command_quick_look(lua_State* L);
        static int lua_command_quick_look_zoom(lua_State* L);
        static int lua_command_sort(lua_State* L);
        static int lua_command_search(lua_State* L);
        static int lua_command_search_next(lua_State* L);
        static int lua_command_search_prev(lua_State* L);
        static int lua_command_search_clear(lua_State* L);
        static int lua_command_filter(lua_State* L);
        static int lua_command_filter_set(lua_State* L);
        static int lua_command_filter_clear(lua_State* L);
        static int lua_command_history_list(lua_State* L);
        static int lua_command_jump_to(lua_State* L);
        static int lua_command_set_clipboard(lua_State* L);
        static int lua_command_file_info(lua_State* L);

        // Viewを操作するコマンドの入口(Script::RegisterFunctionsのwrapper)。upvalue(1)に本来のC関数を持つ。
        // Viewは設定ファイルの読み込みより後に作るので、読み込み中に呼ばれたらLuaのエラーにする。
        // ステータスバーの Lua の関数の中(in_status_function_)では、呼べない(関数は、何も変えない。状態を変えると、
        // 鍵が変わって、毎ティック、関数が呼び直される)
        static int lua_view_trampoline(lua_State* L);
        // 読むだけのコマンド(current_pane / cursor_entry など。状態を変えない)の入口。読み込み中は呼べないが、
        // ステータスバーの関数の中では呼べる(ctx に無い情報を、関数が読めるように)
        static int lua_view_query_trampoline(lua_State* L);

        static int lua_private_dialog_open(lua_State* L);
        static int lua_private_dialog_is_open(lua_State* L);
        static int lua_private_dialog_result(lua_State* L);
        static int lua_private_paths_of(lua_State* L);
        static int lua_private_open_paths(lua_State* L);
        static int lua_private_reveal_paths(lua_State* L);

        static int lua_color_index(lua_State* L);
        static int lua_color_newindex(lua_State* L);







        std::unique_ptr<views::View> view_;
        //KeyBinding key_binding_;
        std::array<std::unique_ptr<KeyBinding>, (size_t)KeyBindingMap::Max> key_binding_map_;
        KeyBinding::KeyStroke key_stroke_;
        // キー入力の途中(前置きが保留中)の、次のキー候補。KeyDown の出口の状態: キーを押すたびに作り直し、
        // 割り当てが確定したか、外れたら空になる(KeyBinding::Has が MaybeTooShort を返したときだけ入る)。
        // key_stroke_ を、ステータスバーのために直接読まない: Lua の関数の実行中は、確定した完全なキー列が残っている
        std::optional<views::StatusPending> pending_;
        // ステータスバーに何を出すか(鍵の比較・Lua のエラーのラッチ)
        views::StatusBarState status_state_;
        // ステータスバーの Lua の関数を実行している間だけ真
        bool in_status_function_ = false;


        misc::ReactiveProperty<int> width_;
        misc::ReactiveProperty<int> height_;
        std::vector<misc::SubscriptionGuard> subscriptions_;

        FileOperationManager file_operations_;


    };
}





#endif // APPLICATION_H__
