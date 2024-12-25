#ifndef APPLICATION_H__
#define APPLICATION_H__

#include <array>
#include <memory>
#include <sokol_app.h>
#include <lua.h>
#include "misc.h"
#include "KeyBinding.h"
#include "views/View.h"

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
            if (instance_ == nullptr) {
                instance_ = new Application();
            }
            return *instance_;
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

        static int lua_color_index(lua_State* L);
        static int lua_color_newindex(lua_State* L);







        int update_count_;
        std::unique_ptr<views::View> view_;
        //KeyBinding key_binding_;
        std::array<std::unique_ptr<KeyBinding>, (size_t)KeyBindingMap::Max> key_binding_map_;
        KeyBinding::KeyStroke key_stroke_;


        misc::ReactiveProperty<int> width_;
        misc::ReactiveProperty<int> height_;
        std::vector<misc::SubscriptionGuard> subscriptions_;


        static Application* instance_;
    };
}





#endif // APPLICATION_H__
