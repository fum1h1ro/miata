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







} // namespace miata::widgets
