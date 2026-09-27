#include "Widget.h"
#include <print>
#include <sokol_app.h>
#include "../platform.h"

namespace miata::widgets {

    Pane::Pane(const char* name, int w, int h) : name_(name)
    {
        size_ = ImVec2((float)w, (float)h);
        flags_.clear();
        window_flags_.on(ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);
        child_flags_.on(ImGuiChildFlags_Borders);

        style_.window_padding_ = ImVec2(0, 0);
        style_.child_border_size_ = 4.0f;
    }

    void Pane::AddChild(std::shared_ptr<Pane> child)
    {
        children_.push_back(child);
    }

    void Pane::RemoveChild(std::shared_ptr<Pane> child)
    {
        auto it = std::find(children_.begin(), children_.end(), child);
        if (it != children_.end()) {
            children_.erase(it);
        }
    }

    bool Pane::OnGui(int parent_width, int parent_height, std::function<void()> on_gui)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style_.window_padding_);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, style_.child_border_size_);

        bool parent_resized = (int)parent_size_.x != parent_width || (int)parent_size_.y != parent_height;
        parent_size_ = ImVec2((float)parent_width, (float)parent_height);
        //if (parent_resized) {
        //    printf("parent_size: %f, %f\n", parent_size_.x, parent_size_.y);
        //}

        bool window_resized = false;
        auto resize_x = child_flags_.is(ImGuiChildFlags_ResizeX);
        auto resize_y = child_flags_.is(ImGuiChildFlags_ResizeY);
        auto sz = size_;

        if (!resize_x) {
            sz.x = 0;
        }
        if (!resize_y) {
            sz.y = 0;
        }

        if (flags_.is(flags::force_resize)) {
            ImGui::SetNextWindowSize(size_, ImGuiCond_Always);
            flags_.off(flags::force_resize);
        }
        if ((sz.x <= 0 && sz.y <= 0)) {
            ImGui::SetNextWindowSize(sz, ImGuiCond_Always);
        }
        if (ImGui::BeginChild(
            name_.c_str(),
            sz,
            child_flags_.value(),
            window_flags_.value())
        ) {
            auto winsz = ImGui::GetWindowSize();
            window_resized = winsz.x != size_.x || winsz.y != size_.y;
            size_ = winsz;
            //if (window_resized) printf("name: %s, size: %f, %f\n", name_.c_str(), size_.x, size_.y);

            OnGuiImpl(parent_resized);
            if (on_gui != nullptr) on_gui();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(4);

        return window_resized;
    }

    void Pane::OnGuiImpl(bool parent_resized)
    {
        for (auto& child : children_) {
            child->OnGui((int)parent_size_.x, (int)parent_size_.y);
            if (flags_.is(flags::horizontal)) {
                ImGui::SameLine();
            }
        }
    }





    Layouter::Layouter(const char* name, int w, int h) : Pane(name, w, h)
    {
        splitter_size_ = 8;
    }
    Layouter::~Layouter()
    {
    }

    void Layouter::AddChild(std::shared_ptr<Pane> child)
    {
        Pane::AddChild(child);
        auto sz = sizes_.size();
        sizes_.emplace_back(
            Size(
                child,
                std::format("pane_{}-{}", Name(), sz),
                std::format("split_{}-{}", Name(), sz),
                std::format("button_{}-{}", Name(), sz),
                0.0f
            )
        );

        for (auto& s : sizes_) {
            s.ratio_ = 1.0f / (float)(sz + 1);
        }
    }

    void Layouter::RemoveChild(std::shared_ptr<Pane> child)
    {
        Pane::RemoveChild(child);
        auto it = std::find_if(sizes_.begin(), sizes_.end(), [&](const auto& s) { return s.pane_ == child; });
        if (it != sizes_.end()) {
            sizes_.erase(it);
        }

        auto sz = sizes_.size();
        for (auto& s : sizes_) {
            s.ratio_ = 1.0f / (float)sz;
        }
    }

    void HorizontalLayouter::OnGuiImpl(bool parent_resized)
    {
        auto sz = (int)Children().size();
        auto rect = ImGui::GetContentRegionAvail();
        auto actual_width = rect.x - (float)(splitter_size_ * (sz - 1));

        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
        for (auto i = 0; i < sz; ++i) {
            auto& size = sizes_[(size_t)i];
            auto w = actual_width * size.ratio_;

            //ImGui::SetNextWindowSize(ImVec2(100, 0), ImGuiCond_Always);
            if (ImGui::BeginChild(size.name_.c_str(), ImVec2(w, 0), 0, 0)) {
                auto pane_rect = ImGui::GetContentRegionAvail();
                auto& child = Children()[(size_t)i];
                child->OnGui((int)pane_rect.x, (int)pane_rect.y);
            }
            ImGui::EndChild();
            if (i < sz - 1) {
                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_ChildBg, pl_get_color(pl_color_type::control_background_color));
                if (ImGui::BeginChild(size.splitter_name_.c_str(), ImVec2((float)splitter_size_, 0), 0, 0)) {
                    auto rect = ImGui::GetWindowContentRegionMax();
                    ImGui::InvisibleButton(size.button_name_.c_str(), rect);
                    if (ImGui::IsItemHovered()) {
                        //ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                        sapp_set_mouse_cursor(SAPP_MOUSECURSOR_RESIZE_EW);
                    }
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
                    {
                        auto& next_size = sizes_[(size_t)i + 1];
                        auto next_w = actual_width * next_size.ratio_;
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        //std::print("delta: {}, {}\n", delta.x, delta.y);
                        w += delta.x;
                        next_w -= delta.x;
                        size.ratio_ = w / actual_width;
                        next_size.ratio_ = next_w / actual_width;
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();

                ImGui::SameLine();
            }
        }
        ImGui::PopStyleVar(5);
    }

    void VerticalLayouter::OnGuiImpl(bool parent_resized)
    {
        auto sz = (int)Children().size();
        auto rect = ImGui::GetContentRegionAvail();
        auto actual_height = rect.y - (float)(splitter_size_ * (sz - 1));

        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
        for (auto i = 0; i < sz; ++i) {
            auto& size = sizes_[(size_t)i];
            auto h = actual_height * size.ratio_;

            //ImGui::SetNextWindowSize(ImVec2(100, 0), ImGuiCond_Always);
            if (ImGui::BeginChild(size.name_.c_str(), ImVec2(0, h), 0, 0)) {
                auto pane_rect = ImGui::GetContentRegionAvail();
                auto& child = Children()[(size_t)i];
                child->OnGui((int)pane_rect.x, (int)pane_rect.y);
            }
            ImGui::EndChild();
            if (i < sz - 1) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, pl_get_color(pl_color_type::control_background_color));
                if (ImGui::BeginChild(size.splitter_name_.c_str(), ImVec2(0, (float)splitter_size_), 0, 0)) {
                    auto rect = ImGui::GetWindowContentRegionMax();
                    ImGui::InvisibleButton(size.button_name_.c_str(), rect);
                    if (ImGui::IsItemHovered()) {
                        //ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                        sapp_set_mouse_cursor(SAPP_MOUSECURSOR_RESIZE_NS);
                    }
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
                    {
                        auto& next_size = sizes_[(size_t)i + 1];
                        auto next_h = actual_height * next_size.ratio_;
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        //std::print("delta: {}, {}\n", delta.x, delta.y);
                        h += delta.y;
                        next_h -= delta.y;
                        size.ratio_ = h / actual_height;
                        next_size.ratio_ = next_h / actual_height;
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
        }
        ImGui::PopStyleVar(5);
    }


    ItemFocus::ItemFocus(int32_t index) : index_(-1), dir_(Dir::None)
    {
    }
    void ItemFocus::Begin()
    {
        items_.clear();
    }
    void ItemFocus::End()
    {
        int32_t max = (int32_t)items_.size();
        index_ = std::clamp(index_, 0, max - 1);

        if (dir_ != Dir::None && items_.size() > 0uz) {
            auto current = items_[(size_t)index_];
            auto current_center = current.GetCenter();
            ImRect hitbox = current;
            const auto width = current.GetSize().x;
            const auto height = current.GetSize().y;
            const int dist = 10;

            switch (dir_) {
            case Dir::Up:
                hitbox.Translate(ImVec2(0, height * -dist));
                hitbox.Max.y += height * (dist - 1);
                //hitbox.Expand(ImVec2(0, height * (dist - 1)));
                break;
            case Dir::Down:
                hitbox.Translate(ImVec2(0, height));
                hitbox.Max.y += height * (dist - 1);
                //hitbox.Expand(ImVec2(0, height * (dist - 1)));
                break;
            case Dir::Left:
                hitbox.Translate(ImVec2(width * -dist, 0));
                hitbox.Max.x += width * (dist - 1);
                //hitbox.Expand(ImVec2(width * (dist - 1), 0));
                break;
            case Dir::Right:
                hitbox.Translate(ImVec2(width, 0));
                hitbox.Max.x += width * (dist - 1);
                //hitbox.Expand(ImVec2(width * (dist - 1), 0));
                break;
            default:
                break;
            }

            float min_dist = FLT_MAX;
            int32_t next = -1;

            for (int32_t i = 0; i < max; ++i) {
                if (i == index_) continue;
                auto& item = items_[(size_t)i];

                if (hitbox.Overlaps(item)) {
                    auto center = item.GetCenter();
                    auto dist = center - current_center;
                    auto d = dist.x * dist.x + dist.y * dist.y;
                    if (d < min_dist) {
                        min_dist = d;
                        next = i;
                    }
                }
            }
            if (next >= 0) index_ = std::clamp(next, 0, max - 1);
        }
        dir_ = Dir::None;
    }
    bool ItemFocus::Focus()
    {
        auto min = ImGui::GetItemRectMin();
        auto max = ImGui::GetItemRectMax();
        int32_t idx = (int32_t)items_.size();
        items_.emplace_back(min, max);

        if (index_ == idx) {
            auto draw_list = ImGui::GetWindowDrawList();
            auto col = IM_COL32(0, 255, 255, 255);
            draw_list->AddRect(min, max, col, 0.0f, 0, 2.0f);
            if (IsOk()) {
                return true;
            }
        }
        return false;
    }
    void ItemFocus::NextItemIsDefault()
    {
        if (index_ < 0) {
            index_ = (int32_t)items_.size();
        }
    }
    void ItemFocus::NavigateUp()
    {
        dir_ = Dir::Up;
    }
    void ItemFocus::NavigateDown()
    {
        dir_ = Dir::Down;
    }
    void ItemFocus::NavigateLeft()
    {
        dir_ = Dir::Left;
    }
    void ItemFocus::NavigateRight()
    {
        dir_ = Dir::Right;
    }
    void ItemFocus::NavigateOk()
    {
        flags_.on(Flags::Ok);
    }
    void ItemFocus::NavigateCancel()
    {
        flags_.on(Flags::Cancel);
    }
    bool ItemFocus::Button(const char* label, const ImVec2& size)
    {
        return ImGui::Button(label, size) || Focus();
    }
    bool ItemFocus::Checkbox(const std::string& label, bool value)
    {
        ImGui::Checkbox(label.c_str(), &value);
        if (Focus()) {
            value = !value;
        }
        return value;
    }
    bool ItemFocus::Selectable(const std::string& label, bool selected)
    {
        return ImGui::Selectable(label.c_str(), &selected) || Focus();
    }









} // namespace miata::widgets
