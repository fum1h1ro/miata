//------------------------------------------------------------------------------
//  imgui-metal.c
//  Since this will only need to compile with clang we can
//  mix C99 designated initializers and C++ code.
//
//  NOTE: this demo is using the sokol_imgui.h utility header which
//  implements a renderer for Dear ImGui on top of sokol_gfx.h, but without
//  sokol_app.h (via the config define SOKOL_IMGUI_NO_SOKOL_APP).
//------------------------------------------------------------------------------
//#include <AppKit/AppKit.h>
//#include <Metal/Metal.h>
//#include <MetalKit/MetalKit.h>
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_time.h>
#include <sokol_log.h>
#include <sokol_glue.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <backends/imgui_impl_metal.h>
#define SOKOL_METAL
#define SOKOL_IMGUI_IMPL
//#define SOKOL_IMGUI_NO_SOKOL_APP
#include <sokol_imgui.h>
#include <Carbon/Carbon.h>

//#include <lua.h>

#include <string>
#include <vector>
#include <ranges>
#include <filesystem>

#include "platform.h"
#include "models/Model.h"
#include "views/View.h"
#include "Script.h"
#include "KeyBinding.h"
#include "Application.h"
#include "Config.h"
#include "misc.h"

ImFont* AddDefaultFont( float pixel_size )
{
    ImGuiIO &io = ImGui::GetIO();
    ImFontConfig config;
    config.SizePixels = pixel_size;
    config.OversampleH = config.OversampleV = 1;
    config.PixelSnapH = true;
    ImFont *font = io.Fonts->AddFontDefault(&config);
    return font;
}










static uint64_t last_time = 0;
static bool show_test_window = true;
static bool show_another_window = false;
static sg_pass_action pass_action;
static ImFont* font = nullptr;




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
        update_count_ = 0;
        key_binding_map_[(int)KeyBindingMap::Normal] = std::make_unique<KeyBinding>();
        key_binding_map_[(int)KeyBindingMap::Dialog] = std::make_unique<KeyBinding>();
    }

    Application::~Application()
    {
    }

    void Application::CheckUpdate()
    {
        if (update_count_ > 0) {
            pl_start_update();
            --update_count_;
        }
        else {
            pl_stop_update();
        }
    }

    void Application::RequestUpdate()
    {
        update_count_ = 100;
        pl_force_update();
    }

    void Application::CheckFileOperations()
    {
        file_operations_.Update();
    }

    void Application::Initialize()
    {
        Instance().InitializeImpl();
    }

    void Application::Frame()
    {
        Instance().FrameImpl();
    }

    void Application::Shutdown()
    {
        Instance().ShutdownImpl();
    }

    void Application::Event(const sapp_event* event)
    {
        Instance().EventImpl(event);
    }

    void Application::InitializeImpl()
    {
        // setup sokol_gfx and sokol_time
        const sg_desc desc = {
            .logger = {
                .func = slog_func,
            },
            .environment = sglue_environment(),
        };
        sg_setup(&desc);
        stm_setup();
        const simgui_desc_t simgui_desc = {
            .no_default_font = true,
            //.disable_set_mouse_cursor = true,
            .logger = {
                .func = slog_func,
            },
        };
        simgui_setup(&simgui_desc);
        ImGuiIO& io = ImGui::GetIO();
        //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        std::print("config_dir: {}\n", pl_get_config_dir().c_str());

        auto& script = miata::Script::Instance();
        script.Initialize();
        static luaL_Reg commands[] = {
            { "bind", lua_command_bind },
            { "unbind", lua_command_unbind },
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
            { "sort", lua_command_sort },
        };
        script.RegisterFunctions(
            "Miata.command",
            std::vector<luaL_Reg>(std::begin(commands), std::end(commands))
        );

        static luaL_Reg privates[] = {
            { "dialog_open", lua_private_dialog_open },
            { "dialog_is_open", lua_private_dialog_is_open },
            { "dialog_result", lua_private_dialog_result },
        };
        script.RegisterFunctions(
            "Miata._private",
            std::vector<luaL_Reg>(std::begin(privates), std::end(privates))
        );

        Config::ScriptInitialize();

        script.PostInitialize();

        auto home = pl_get_home_dir();
        printf("home: %s\n", home.c_str());
#if 1
        auto font_path = pl_find_font_filename("ヒラギノ丸ゴ ProN W4");
        //auto font_path = "/Users/k-ya/Downloads/Fonts/Kiwi_Maru/KiwiMaru-Regular.ttf";
        font = io.Fonts->AddFontFromFileTTF(font_path.c_str(), 20.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
        printf("font: %p\n", font);
#endif

        // setup the imgui environment
        //io.KeyMap[ImGuiKey_Tab] = kVK_Tab;
        //io.KeyMap[ImGuiKey_LeftArrow] = 0x7B;
        //io.KeyMap[ImGuiKey_RightArrow] = 0x7C;
        //io.KeyMap[ImGuiKey_DownArrow] = 0x7D;
        //io.KeyMap[ImGuiKey_UpArrow] = 0x7E;
        //io.KeyMap[ImGuiKey_Home] = 0x73;
        //io.KeyMap[ImGuiKey_End] = 0x77;
        //io.KeyMap[ImGuiKey_Delete] = 0x75;
        //io.KeyMap[ImGuiKey_Backspace] = 0x33;
        //io.KeyMap[ImGuiKey_Enter] = 0x24;
        //io.KeyMap[ImGuiKey_Escape] = 0x35;
        //io.KeyMap[ImGuiKey_A] = 0x00;
        //io.KeyMap[ImGuiKey_C] = 0x08;
        //io.KeyMap[ImGuiKey_V] = 0x09;
        //io.KeyMap[ImGuiKey_X] = 0x07;
        //io.KeyMap[ImGuiKey_Y] = 0x10;
        //io.KeyMap[ImGuiKey_Z] = 0x06;

#if 0
        // OSX => ImGui input forwarding
        osx_mouse_pos([] (float x, float y) { ImGui::GetIO().MousePos = ImVec2(x, y); osx_needs_redraw(); });
        osx_mouse_btn_down([] (int btn)     { ImGui::GetIO().MouseDown[btn] = true; osx_needs_redraw(); });
        osx_mouse_btn_up([] (int btn)       { ImGui::GetIO().MouseDown[btn] = false; osx_needs_redraw(); });
        osx_mouse_wheel([] (float v)        { ImGui::GetIO().MouseWheel = 0.25f * v; osx_needs_redraw(); });
        //osx_key_down([] (int key)           { if (key < 512) ImGui::GetIO().KeysDown[key] = true; osx_needs_redraw(); });
        //osx_key_up([] (int key)             { if (key < 512) ImGui::GetIO().KeysDown[key] = false; osx_needs_redraw(); });
        osx_char([] (wchar_t c)             { ImGui::GetIO().AddInputCharacter(c); osx_needs_redraw(); });

        osx_key_down(Application::KeyDown);
        osx_key_up(Application::KeyUp);
#endif

        // initial clear color
        pass_action = (sg_pass_action){
            .colors[0] = { .load_action = SG_LOADACTION_CLEAR, .clear_value = { 0.0f, 0.0f, 0.0f, 1.0f } }
        };


        //pl_osx_set_visual_effect_view();
        pl_set_fps((int)pl_get_default_fps());
        //pl_stop_update();
        //pl_force_update();

        auto& style = ImGui::GetStyle();

        style.ItemSpacing = ImVec2(3, 3);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.0f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.0f);

        view_ = std::make_unique<views::View>(sapp_width(), sapp_height());

        file_operations_.SetCallback([this](FileOperationCompleted& e) {
            OnFileOperationCompleted(e);
        });

        pl_app_post_initialize();

        key_binding_map_[(int)KeyBindingMap::Normal]->DumpAll();
        key_binding_map_[(int)KeyBindingMap::Dialog]->DumpAll();



#if 0
        subscriptions_.push_back(
            width_.Observe()
                .subscribe([](int v) {
                    printf("width2: %d\n", v);
                })
        );
        subscriptions_.push_back(
            height_.Observe()
                .subscribe([](int v) {
                    printf("height: %d\n", v);
                })
        );
        //subscriptions_.clear();


        auto handle = misc::MessageBroker::Subscribe<int>([](int& v) {
            std::print("message: {}\n", v);
        });
        struct temp {
            int a;
            int b;
        };
        struct temp2 {
            int a;
            int b;
        };
        auto handle2 = misc::MessageBroker::Subscribe<temp>([](temp& v) {
            std::print("message2: {} {}\n", v.a, v.b);
        });
        misc::MessageBroker::Publish(temp{ 1, 2 });

        misc::MessageBroker::Publish(42);
        misc::MessageBroker::Unsubscribe(handle);
        misc::MessageBroker::Publish(43);
        misc::MessageBroker::Publish(temp2{ 3, 4 });
        //misc::MessageBroker::Unsubscribe(handle2);
#endif
    }

    void Application::FrameImpl()
    {
        if (frame_running_) return;
        frame_running_ = true;
        struct FrameGuard { bool& f; ~FrameGuard() { f = false; } } guard{frame_running_};

        CheckUpdate();
        CheckFileOperations();
        if (Script::Instance().Update()) {
            RequestUpdate();
        }

        const int width = sapp_width();
        const int height = sapp_height();
        //printf("width: %d, height: %d\n", width, height);
        simgui_new_frame({ width, height, stm_sec(stm_laptime(&last_time)), 1.0f });
        pl_update_ime(ImGui::GetIO().WantTextInput);
#if 0
        // 1. Show a simple window
        // Tip: if we don't call ImGui::Begin()/ImGui::End() the widgets appears in a window automatically called "Debug"
        static float f = 0.0f;
        ImGui::Text("Hello, world!");
        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
        ImGui::ColorEdit3("clear color", &pass_action.colors[0].clear_value.r);
        if (ImGui::Button("Test Window")) show_test_window ^= 1;
        if (ImGui::Button("Another Window")) show_another_window ^= 1;
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

        // 2. Show another simple window, this time using an explicit Begin/End pair
        if (show_another_window) {
            ImGui::SetNextWindowSize(ImVec2(200,100), ImGuiCond_FirstUseEver);
            ImGui::Begin("Another Window", &show_another_window);
            ImGui::Text("Hello");
            ImGui::End();
        }

        // 3. Show the ImGui test window. Most of the sample code is in ImGui::ShowDemoWindow()
        if (show_test_window) {
            ImGui::SetNextWindowPos(ImVec2(460, 20), ImGuiCond_FirstUseEver);
            ImGui::ShowDemoWindow();
        }
#else
        view_->OnGui(width, height);
        if (view_->IsAnyDialogOpened()) {
            RequestUpdate();
        }
#endif
        // the sokol draw pass
        sg_begin_pass({
            .action = pass_action,
            .swapchain = sglue_swapchain()
        });
        simgui_render();
        sg_end_pass();
        sg_commit();


#ifndef NDEBUG
        auto title = std::format("Application average {:.3f} ms/frame ({:.1f} FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        sapp_set_window_title(title.c_str());
#endif

    }

    void Application::ShutdownImpl()
    {
        simgui_shutdown();
        sg_shutdown();
        std::print("shutdown\n");
    }

    void Application::EventImpl(const sapp_event* event)
    {
        //std::print("event\n");
        simgui_handle_event(event);
        RequestUpdate();

        switch (event->type) {
        case SAPP_EVENTTYPE_KEY_DOWN:
            if (!KeyBinding::IsModifierKey(event->key_code)) KeyDown(event->key_code, event->modifiers);
            break;
        case SAPP_EVENTTYPE_KEY_UP:
            if (!KeyBinding::IsModifierKey(event->key_code)) KeyUp(event->key_code, event->modifiers);
            break;
        case SAPP_EVENTTYPE_CHAR:
            //if (std::isprint((char)event->char_code)) {
            //    printf("char: %s%d %c\n", temp::modifier(event).c_str(), event->char_code, (char)event->char_code);
            //}
            //else {
            //    printf("char: %s%d\n", temp::modifier(event).c_str(), event->char_code);
            //}
            break;
        case SAPP_EVENTTYPE_RESIZED:
            //std::print("resized: {} {}\n", event->window_width, event->window_height);
            width_.Value(event->window_width);
            height_.Value(event->window_height);
            break;
        }

        //std::print("event: {}\n", (int)event->type);



    }

    void Application::KeyDown(sapp_keycode key_code, uint32_t mods)
    {
        if (ImGui::GetIO().WantTextInput) return;
        auto& script = Script::Instance();
        auto& kb = (view_->IsAnyDialogOpened())? key_binding_map_[(int)KeyBindingMap::Dialog] : key_binding_map_[(int)KeyBindingMap::Normal];

        auto r = key_stroke_.Add(KeyBinding::Key::MakeKey((uint16_t)mods, key_code));
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

    void Application::KeyUp(sapp_keycode key_code, uint32_t mods)
    {
        //printf("key_up: %d\n", key);
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
            event.dest_model->JumpTo(event.dest_dir);
        }
        if (event.src_model && event.src_model->Path() == event.src_dir) {
            if (event.type == FileOpType::Move) {
                event.src_model->JumpTo(event.src_dir); // 消えたファイルを一覧に反映（マークも自然に消える）
            }
            else {
                event.src_model->ClearMarks(); // 中身は変わらないのでマークだけ解除する
            }
        }

        auto message = event.success
            ? std::string("完了しました")
            : std::format("エラーが発生しました（{}件失敗）: {}", event.failed_count, event.error_message);
        view_->RequestDialog(std::make_shared<views::ConfirmDialog>(
            [](views::IDialog&) {},
            views::ConfirmDialog::arguments{
                .message_ = message,
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

                auto failed_count = 0;
                std::string last_error;
                for (auto& target : targets) {
                    auto result = pl_trash_file(target);
                    if (!result) {
                        ++failed_count;
                        last_error = result.error();
                    }
                }

                // 確認ダイアログはモーダルで開いている間ペイン移動できないため、
                // ここでは常に操作対象だったディレクトリのままのはずだが、念のため確認する。
                if (list.Path() == dir) {
                    list.JumpTo(dir);
                }

                if (failed_count > 0) {
                    view_->RequestDialog(std::make_shared<views::ConfirmDialog>(
                        [](views::IDialog&) {},
                        views::ConfirmDialog::arguments{
                            .message_ = std::format("エラーが発生しました（{}件失敗）: {}", failed_count, last_error),
                            .button_text_ = "OK",
                        }
                    ));
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


        app.view_->RequestDialog(std::make_shared<views::YesNoDialog>([](views::IDialog& dialog) {
            auto& yes_no_dialog = dynamic_cast<views::YesNoDialog&>(dialog);
            if (yes_no_dialog.Result()) {
                std::print("yes\n");
            }
            else {
                std::print("no\n");
            }
        }, views::YesNoDialog::arguments{ "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789", false }));




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
            list.JumpTo(list.Path());
            lua_pushboolean(L, true);
        }
        else {
            app.view_->RequestDialog(std::make_shared<views::ConfirmDialog>(
                [](views::IDialog&) {},
                views::ConfirmDialog::arguments{
                    .message_ = std::format("フォルダを作成できませんでした: {}", ec.message()),
                    .button_text_ = "OK",
                }
            ));
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
                [](views::IDialog& dialog) {
                    std::print("confirm\n");
                },
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
        return 0;
    }

}

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    return (sapp_desc){
        .init_cb = miata::Application::Initialize,
        .frame_cb = miata::Application::Frame,
        .cleanup_cb = miata::Application::Shutdown,
        .event_cb = miata::Application::Event,
        .width = 1024,
        .height = 768,
        .window_title = "Miata",
        .icon = {
            .sokol_default = true,
        },
        .logger = {
            .func = slog_func,
        },
    };
}

//int main()
//{
//    auto& script = miata::Script::Instance();
//    std::println("core: {:d}\n", std::thread::hardware_concurrency());
//    const int INITIAL_WIDTH = 1024;
//    const int INITIAL_HEIGHT = 768;
//    osx_start(INITIAL_WIDTH, INITIAL_HEIGHT, 1, SG_PIXELFORMAT_NONE, "Miata", miata::Application::Initialize, miata::Application::Frame, miata::Application::Shutdown);
//    std::println("exit\n");
//    return 0;
//}
