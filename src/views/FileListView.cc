#include "../platform.h"
#include "../misc.h"
#include "FileListView.h"

namespace miata::views {
    FileListView::FileListView(const char *id, int w, int h, models::FileListModel &list) : Pane(id, w, h), model_(list)
    {
        style_.child_border_size_ = 0.0f;

        cursorIndex_ = 0;

        subscriptions_.push_back(
            model_.ObservePath()
                .subscribe([&](const std::filesystem::path& path) {
                    std::print("view path: {}\n", path.c_str());
                    Fetch();
                    cursorIndex_ = 0;
                })
        );
    }

    void FileListView::OnGuiImpl(bool window_resized)
    {
        OnGuiHeader();
        OnGuiList();
    }

    void FileListView::OnGuiHeader()
    {
        auto bak = ImGui::GetFont()->Scale;
        ImGui::GetFont()->Scale = 4.0f;
        auto height = ImGui::CalcTextSize("").y;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, pl_get_color(pl_color_type::window_background_color));
        ImGui::PushID(1);
        ImGui::BeginChild(Name().c_str(), ImVec2(0, height), 0, 0);
        ImGui::SetCursorPos(ImVec2(8, 0));
        ImGui::Text("%s", model_.Path().c_str());
        ImGui::EndChild();
        ImGui::PopID();
        ImGui::PopStyleColor();
        ImGui::GetFont()->Scale = bak;
    }

    void FileListView::OnGuiList()
    {
        misc::ScopedImGuiStyle style(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
        style.AddStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
        style.AddStyleVar(ImGuiStyleVar_ScrollbarSize, 16);
        style.AddStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));

        ImGui::PushID(2);
        ImGui::BeginChild(Name().c_str(), ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, 0);
        auto size = (int)list_.size();

        if (cursorIndex_ < 0) cursorIndex_ = 0;
        if (cursorIndex_ >= size) cursorIndex_ = size - 1;

        //style.AddStyleColor(ImGuiCol_Text, Config::Color().Get(Config::Color::Type::NormalText));
        for (auto i = 0; i < size; i++) {
            auto entry_view = list_[(size_t)i];
            auto& entry_model = entry_view->Model();
            auto on_cursor = i == cursorIndex_;
            if (entry_model.IsMarked()) {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImVec2 rect = ImGui::GetContentRegionAvail();
                ImVec2 item_size = ImGui::CalcTextSize(entry_model.Name().c_str());
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                ImVec2 rect_min = ImVec2(pos.x, pos.y);
                ImVec2 rect_max = ImVec2(pos.x + rect.x, pos.y + item_size.y);
                ImU32 col = IM_COL32(0, 63, 127, 255);
                draw_list->AddRectFilled(rect_min, rect_max, col, 0.0f);
            }
            if (on_cursor && focus_) {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImVec2 rect = ImGui::GetContentRegionAvail();
                ImVec2 item_size = ImGui::CalcTextSize(entry_model.Name().c_str());
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                ImVec2 rect_min = ImVec2(pos.x, pos.y + item_size.y);
                ImVec2 rect_max = ImVec2(pos.x + rect.x, pos.y + item_size.y);
                ImU32 col = IM_COL32(255, 0, 0, 255);
                draw_list->AddLine(rect_min, rect_max, col, 4.0f);
            }

            if (entry_model.IsDirectory()) {
                ImGui::PushStyleColor(ImGuiCol_Text, Config::Color().Get(Config::Color::Type::Directory));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, Config::Color().Get(Config::Color::Type::NormalFile));
            }

            auto pos = ImGui::GetCursorScreenPos();
            auto rect = ImGui::GetContentRegionAvail();
            auto time_rect = ImGui::CalcTextSize(entry_model.ModifiedTime().c_str());
            ImGui::SetCursorScreenPos(ImVec2(pos.x + rect.x - time_rect.x, pos.y));
            ImGui::Text("%s", entry_model.ModifiedTime().c_str());
            if (entry_model.IsDirectory()) {
                auto dir_rect = ImGui::CalcTextSize("<DIR> ");
                ImGui::SetCursorScreenPos(ImVec2(pos.x + rect.x - time_rect.x - dir_rect.x, pos.y));
                ImGui::Text("<DIR> ");
            }
            ImGui::SetCursorScreenPos(pos);
            ImGui::Text("%s", entry_model.Name().c_str());
            if (on_cursor && focus_ && !IsFullyVisible()) ImGui::SetScrollHereY();
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopID();
    }

    bool FileListView::IsFullyVisible() const
    {
        // 要素の最小・最大座標を取得（スクリーン座標）
        ImVec2 item_min = ImGui::GetItemRectMin();
        ImVec2 item_max = ImGui::GetItemRectMax();

        // ウィンドウの位置を取得（スクリーン座標）
        ImVec2 window_pos = ImGui::GetWindowPos();
        // ウィンドウのスクロール量を取得
        float scroll = ImGui::GetScrollY();

        // ウィンドウのコンテンツ領域の最小・最大座標を取得（ウィンドウ座標）
        ImVec2 content_min = ImGui::GetWindowContentRegionMin();
        ImVec2 content_max = ImGui::GetWindowContentRegionMax();

        // コンテンツ領域の座標をスクリーン座標に変換
        ImVec2 content_min_screen = ImVec2(window_pos.x + content_min.x, window_pos.y + content_min.y + scroll);
        ImVec2 content_max_screen = ImVec2(window_pos.x + content_max.x, window_pos.y + content_max.y + scroll);

        // 要素がコンテンツ領域内に完全に収まっているかチェック
        return
            (item_min.x >= content_min_screen.x) &&
            (item_max.x <= content_max_screen.x) &&
            (item_min.y >= content_min_screen.y) &&
            (item_max.y <= content_max_screen.y);
    }

    void FileListView::Fetch()
    {
        list_.clear();
        entries_.clear();
        auto size = model_.Size();
        list_.reserve((size_t)size);
        entries_.reserve((size_t)size);

        for (auto i = 0; i < size; i++) {
            auto& entry = model_.GetEntry(i);
            entries_.push_back(FileEntryView(entry));
            list_.push_back(&entries_[(size_t)i]);
        }

        std::sort(list_.begin(), list_.end(), [](FileEntryView* a, FileEntryView* b) {
            if (a->Model().IsDirectory() != b->Model().IsDirectory()) return a->Model().IsDirectory();
            return a->Model().Name() < b->Model().Name();
        });
    }
} // namespace miata::views
