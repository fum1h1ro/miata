#ifndef APPLICATION_H__
#define APPLICATION_H__

#include <array>
#include <memory>
#include <sokol_app.h>
#include <lua.h>
#include "misc.h"
#include "KeyBinding.h"
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
        static void Frame();
        static void Shutdown();
        static void Event(const sapp_event* event);




    private:
        Application();
        ~Application();
        void InitializeImpl();
        void FrameImpl();
        void ShutdownImpl();
        void EventImpl(const sapp_event* event);
        void KeyDown(sapp_keycode key_code, uint32_t mods);
        void KeyUp(sapp_keycode key_code, uint32_t mods);
        void CheckUpdate();
        void RequestUpdate();
        std::expected<KeyBindingMap, std::string> GetKeyBinding(const char map_c);
        void StartFileOperation(FileOpType type);
        void DeleteMarked();
        void OnFileOperationCompleted(FileOperationCompleted& event);
        void CheckFileOperations();


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
        static int lua_command_sort(lua_State* L);

        static int lua_private_dialog_open(lua_State* L);
        static int lua_private_dialog_is_open(lua_State* L);
        static int lua_private_dialog_result(lua_State* L);
        static int lua_private_show_input_dialog(lua_State* L);
        static int lua_private_show_custom_dialog(lua_State* L);

        static int lua_color_index(lua_State* L);
        static int lua_color_newindex(lua_State* L);







        bool frame_running_ = false;
        int update_count_;
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
