#ifndef APPLICATION_H__
#define APPLICATION_H__

#include <array>
#include <memory>
#include <lua.h>
#include "misc.h"
#include "KeyBinding.h"
#include "Script.h"
#include "views/View.h"
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
        void KeyDown(uint16_t key_code, uint16_t mods);
        void KeyUp(uint16_t key_code, uint16_t mods);
        void Resize(int width, int height);
        std::expected<KeyBindingMap, std::string> GetKeyBinding(const char map_c);
        void StartFileOperation(FileOpType type);
        void DeleteMarked();
        void OnFileOperationCompleted(FileOperationCompleted& event);


        static int lua_command_bind(lua_State* L);
        static int lua_command_unbind(lua_State* L);
        static int lua_command_navigate_up(lua_State* L);
        static int lua_command_navigate_down(lua_State* L);
        static int lua_command_navigate_left(lua_State* L);
        static int lua_command_navigate_right(lua_State* L);
        static int lua_command_navigate_ok(lua_State* L);
        static int lua_command_navigate_cancel(lua_State* L);
        static int lua_command_toggle_focus(lua_State* L);
        static int lua_command_mark(lua_State* L);
        static int lua_command_unmark(lua_State* L);
        static int lua_command_toggle_mark(lua_State* L);
        static int lua_command_copy_marked(lua_State* L);
        static int lua_command_move_marked(lua_State* L);
        static int lua_command_make_directory(lua_State* L);
        static int lua_command_delete_marked(lua_State* L);
        static int lua_command_current_pane(lua_State* L);
        static int lua_command_pane_path(lua_State* L);
        static int lua_command_cursor_entry(lua_State* L);
        static int lua_command_marked_entries(lua_State* L);
        static int lua_command_current_sort(lua_State* L);
        static int lua_command_reload(lua_State* L);
        static int lua_command_quick_look(lua_State* L);
        static int lua_command_sort(lua_State* L);
        static int lua_command_search(lua_State* L);
        static int lua_command_search_next(lua_State* L);
        static int lua_command_search_prev(lua_State* L);
        static int lua_command_search_clear(lua_State* L);
        static int lua_command_history_list(lua_State* L);
        static int lua_command_jump_to(lua_State* L);

        // Viewを操作するコマンドの入口(Script::RegisterFunctionsのwrapper)。upvalue(1)に本来のC関数を持つ。
        // Viewは設定ファイルの読み込みより後に作るので、読み込み中に呼ばれたらLuaのエラーにする
        static int lua_view_trampoline(lua_State* L);

        static int lua_private_dialog_open(lua_State* L);
        static int lua_private_dialog_is_open(lua_State* L);
        static int lua_private_dialog_result(lua_State* L);
        static int lua_private_rename_target(lua_State* L);
        static int lua_private_rename_conflict(lua_State* L);
        static int lua_private_rename_execute(lua_State* L);

        static int lua_color_index(lua_State* L);
        static int lua_color_newindex(lua_State* L);







        std::unique_ptr<views::View> view_;
        //KeyBinding key_binding_;
        std::array<std::unique_ptr<KeyBinding>, (size_t)KeyBindingMap::Max> key_binding_map_;
        KeyBinding::KeyStroke key_stroke_;


        misc::ReactiveProperty<int> width_;
        misc::ReactiveProperty<int> height_;
        std::vector<misc::SubscriptionGuard> subscriptions_;

        FileOperationManager file_operations_;


    };
}





#endif // APPLICATION_H__
