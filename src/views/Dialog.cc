#include "Dialog.h"
#include <Carbon/Carbon.h>

namespace miata::views {
    IDialog::IDialog(const char* id, std::function<void(IDialog&)> on_close) : id_(id), on_close_(on_close)
    {
    }
    void IDialog::Open()
    {
        is_opened_ = true;
        ImGui::OpenPopup(id_.c_str());
    }

    void IDialog::OnGui(int window_width, int window_height)
    {
        ImGui::SetNextWindowSize(GetIdealSize(), ImGuiCond_Always);
        if (ImGui::BeginPopupModal(Id().c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
            auto window_size = ImGui::GetWindowSize();
            auto avail_rect = ImVec2((float)window_width, (float)window_height);
            auto center_pos = ImVec2((avail_rect.x - window_size.x) / 2, (avail_rect.y - window_size.y) / 2);
            ImGui::SetWindowPos(center_pos);
            OnGuiImpl();
        }
        ImGui::EndPopup();
        if (is_opened_) {
            if (!ImGui::IsPopupOpen(id_.c_str())) {
                is_opened_ = false;
                if (on_close_) {
                    on_close_(*this);
                }
            }
        }
    }

    void IDialog::DrawFocus()
    {
        auto item_min = ImGui::GetItemRectMin();
        auto item_max = ImGui::GetItemRectMax();
        auto draw_list = ImGui::GetWindowDrawList();
        auto col = IM_COL32(255, 0, 127, 255);
        draw_list->AddRect(item_min, item_max, col);
    }




    YesNoDialog::YesNoDialog(std::function<void(IDialog&)> on_close, arguments args) : IDialog("YesNoDialog", on_close)
    {
        args_ = args;
    }
    void YesNoDialog::OnGuiImpl()
    {
        auto avail_width = ImGui::GetWindowSize().x;// * 0.8f;
        auto width = ImGui::CalcTextSize(args_.message_.c_str()).x;
        //ImGui::SetNextItemWidth(avail_width);
        if (avail_width < width) {
            ImGui::PushTextWrapPos(avail_width);
            ImGui::Text("%s", args_.message_.c_str());
            ImGui::PopTextWrapPos();
        }
        else {
            ImGui::Text("%s", args_.message_.c_str());
        }
        //ImGui::Text("%s", args_.message_.c_str());
        ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight()));

        //auto avail_width = ImGui::GetContentRegionAvail().x;
        auto padding = ImGui::GetStyle().FramePadding.x;
        auto max_width = 0.0f;
        //for (auto text : button_texts_) {
        //    auto bw = ImGui::CalcTextSize(text.c_str()).x + padding * 2;
        //    max_width = std::max(max_width, bw);
        //}
        //auto button_width = max_width * (float)button_texts_.size() + padding * 2 * (float)(button_texts_.size() - 1);
        //ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_width - button_width));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
        auto color = ImVec4(0, 0, 1, 1);//osx_get_color("controlAccentColor");
        ImGui::PushStyleColor(ImGuiCol_Button, color);
        if (ImGui::Button("NO")) {
            result_ = false;
            ImGui::CloseCurrentPopup();
        }
        DrawFocus();
        ImGui::SameLine();
        if (ImGui::Button("YES")) {
            result_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    ConfirmDialog::ConfirmDialog(const char* id, std::function<void(IDialog&)> on_close) : IDialog(id, on_close)
    {
    }
    void ConfirmDialog::OnGuiImpl()
    {
        if (ImGui::BeginPopupModal(Id().c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", message_.c_str());
            ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight()));

            auto avail_width = ImGui::GetContentRegionAvail().x;
            auto padding = ImGui::GetStyle().FramePadding.x;
            auto max_width = 0.0f;
            for (auto text : button_texts_) {
                auto bw = ImGui::CalcTextSize(text.c_str()).x + padding * 2;
                max_width = std::max(max_width, bw);
            }
            auto button_width = max_width * (float)button_texts_.size() + padding * 2 * (float)(button_texts_.size() - 1);
            //ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_width - button_width));
            //ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
            for (auto i = 0; i < button_texts_.size(); ++i) {
                auto& text = button_texts_[(size_t)i];
                ImGui::SetKeyboardFocusHere();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.5f, 0.5f, 0.5f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                if (ImGui::Button(text.c_str(), ImVec2(max_width, 0))) {
                    if (i == 1) ImGui::SetItemDefaultFocus();
                    result_ = i;
                }
                ImGui::PopStyleColor(3);
                if (result_ >= 0) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
            }
            //ImGui::PopStyleVar();
            ImGui::EndPopup();
        }
    }



}

