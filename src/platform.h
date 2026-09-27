#ifndef PLATFORM_H__
#define PLATFORM_H__

#include <optional>
#include <string>
#include <vector>
#include <filesystem>
#include <expected>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

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

std::string pl_normalize_string(const std::string& input);
void pl_play_beep();
void pl_app_post_initialize();
void pl_update_ime(bool want_text_input);
// AppKitのcontentView(NSView*)を(__bridge void*)で返す。
// views/ 層はAppKit型に直接依存させたくないため、このvoid*橋渡し越しに使う。
void* pl_get_content_view();

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

void pl_set_fps(int fps);
float pl_get_default_fps();
void pl_start_update();
void pl_stop_update();
void pl_force_update();
std::filesystem::path pl_find_font_filename(const std::string& font_name);
std::filesystem::path pl_get_home_dir();
std::filesystem::path pl_get_config_dir();
std::string pl_read_file(const char* path);
std::expected<std::string, std::string> pl_read_resource_file(const char* path);
void pl_osx_set_visual_effect_view();

ImVec4 pl_get_color(pl_color_type type);

void pl_quick_preview(const std::vector<std::string>& path_list);

#endif // PLATFORM_H__
