#ifndef PLATFORM_H__
#define PLATFORM_H__

#include <optional>
#include <string>
#include <vector>
#include <filesystem>
#include <expected>
#include <functional>
#include <cstdint>

struct Color4f {
    float r = 0, g = 0, b = 0, a = 1;
};

enum class pl_color_type {
    label_color,
    secondary_label_color,
    tertiary_label_color,
    quaternary_label_color,
    text_color,
    placeholder_text_color,
    selected_text_color,
    text_background_color,
    selected_text_background_color,
    keyboard_focus_indicator_color,
    unemphasized_selected_text_color,
    unemphasized_selected_text_background_color,
    link_color,
    separator_color,
    selected_content_background_color,
    unemphasized_selected_content_background_color,
    selected_menu_item_text_color,
    //selected_menu_item_color,
    header_text_color,
    alternating_content_background_colors,
    control_accent_color,
    control_color,
    control_background_color,
    control_text_color,
    disabled_control_text_color,
    //current_control_tint,
    selected_control_color,
    //secondary_selected_control_color,
    //alternate_selected_control_color,
    selected_control_text_color,
    alternate_selected_control_text_color,
    scrubber_textured_background_color,
    window_background_color,
    window_frame_text_color,
    //window_frame_color,
    under_page_background_color,
    find_highlight_color,
    highlight_color,
    shadow_color,
    quaternary_system_fill_color,
    quinary_label_color,
    quinary_system_fill_color,
    secondary_system_fill_color,
    system_fill_color,
    tertiary_system_fill_color,
    text_insertion_point_color,
};

// KeyBinding::Key::mods_ で使うモディファイアのビット表現。NSEventModifierFlagsから
// osx.mm 側で変換して渡す(値そのものはAppKitのビット位置とは無関係な自前定義)。
namespace pl_modifier {
    constexpr uint16_t Shift = 1 << 0;
    constexpr uint16_t Ctrl  = 1 << 1;
    constexpr uint16_t Alt   = 1 << 2;
    constexpr uint16_t Super = 1 << 3;
}

std::string pl_normalize_string(const std::string& input);
void pl_play_beep();

// AppKitのcontentView(NSView*)を(__bridge void*)で返す。
// views/ 層はAppKit型に直接依存させたくないため、このvoid*橋渡し越しに使う。
void* pl_get_content_view();

// メインウィンドウを生成して表示する。
void pl_create_main_window(int width, int height, const char* title);

// キーコードはmacOSのvirtual keycode(NSEvent.keyCode)そのもの。
// modsはpl_modifier::* のビットOR。
void pl_set_key_down_handler(std::function<void(uint16_t keycode, uint16_t mods)> handler);
void pl_set_key_up_handler(std::function<void(uint16_t keycode, uint16_t mods)> handler);
void pl_set_resize_handler(std::function<void(int width, int height)> handler);
// 一定間隔(seconds)で呼ばれる更新タイマーを開始する。
void pl_start_timer(double interval_seconds, std::function<void()> callback);

struct CustomDialogCheckbox {
    std::string label;
    bool checked;
};
struct CustomDialogSelect {
    std::string label;
    std::vector<std::string> options;
    int selected; // 0-based
};
struct CustomDialogSpec {
    std::string title;
    std::string message; // informative text (optional)
    std::vector<std::string> buttons;
    std::vector<CustomDialogCheckbox> checkboxes;
    std::vector<CustomDialogSelect> selects;
};
struct CustomDialogResult {
    int button_index; // 0-based
    std::vector<bool> checkboxes;
    std::vector<int> selects; // 0-based selected index
};

std::expected<void, std::string> pl_trash_file(const std::filesystem::path& path);

std::filesystem::path pl_find_font_filename(const std::string& font_name);
std::filesystem::path pl_get_home_dir();
std::filesystem::path pl_get_config_dir();
std::string pl_read_file(const char* path);
std::expected<std::string, std::string> pl_read_resource_file(const char* path);

Color4f pl_get_color(pl_color_type type);

void pl_quick_preview(const std::vector<std::string>& path_list);

#endif // PLATFORM_H__
