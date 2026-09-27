#include "Dialog.h"
#include <Carbon/Carbon.h>
#include "../platform.h"
#include "../misc.h"

namespace miata::views {
    IDialog::IDialog(const char* id, std::function<void(IDialog&)> on_close) : focus_(0), id_(id), on_close_(on_close)
    {
    }
    void IDialog::Open()
    {
        is_opened_ = true;
        ImGui::OpenPopup(id_.c_str());
        OnOpen();
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
                OnClose();
            }
        }
    }
    void IDialog::Navigate(constants::Navigate dir)
    {
        switch (dir) {
        case constants::Navigate::Up:
            focus_.NavigateUp();
            break;
        case constants::Navigate::Down:
            focus_.NavigateDown();
            break;
        case constants::Navigate::Left:
            focus_.NavigateLeft();
            break;
        case constants::Navigate::Right:
            focus_.NavigateRight();
            break;
        case constants::Navigate::Ok:
            focus_.NavigateOk();
            break;
        case constants::Navigate::Cancel:
            focus_.NavigateCancel();
            break;
        }
    }

    ConfirmDialog::ConfirmDialog(std::function<void(IDialog&)> on_close, const arguments& args) : IDialog(typeid(this).name(), on_close)
    {
        message_ = args.message_;
        button_text_ = args.button_text_;
    }
    void ConfirmDialog::OnGuiImpl()
    {
        focus_.Begin();
        ImGui::Text("%s", message_.c_str());
        ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight()));
        focus_.NextItemIsDefault();
        if (focus_.Button(button_text_.c_str())) {
            ImGui::CloseCurrentPopup();
        }
        focus_.End();
    }

    YesNoDialog::YesNoDialog(std::function<void(IDialog&)> on_close, arguments args) : IDialog(typeid(this).name(), on_close), args_(args), result_(false)
    {
    }
    void YesNoDialog::OnGuiImpl()
    {
        focus_.Begin();
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
        auto color = pl_get_color(pl_color_type::control_accent_color);

        ImGui::PushStyleColor(ImGuiCol_Button, color);
        if (!args_.default_select_) focus_.NextItemIsDefault();
        if (focus_.Button(args_.no_text_.c_str()) || focus_.IsCancel()) {
            result_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (args_.default_select_) focus_.NextItemIsDefault();
        if (focus_.Button(args_.yes_text_.c_str())) {
            result_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        focus_.End();
    }

    InputTextDialog::InputTextDialog(std::function<void(IDialog&)> on_close, const arguments& args) : IDialog(typeid(this).name(), on_close)
    {
        message_ = args.message_;
        text_ = args.initial_text_;
        text_.reserve(std::max(256uz, text_.capacity()));
    }
    void InputTextDialog::OnGuiImpl()
    {
        focus_.Begin();
        if (!message_.empty()) {
            ImGui::Text("%s", message_.c_str());
            ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight()));
        }
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##input", text_.data(), text_.capacity())) {
            text_.resize(std::strlen(text_.data()));
        }
        if (text_.capacity() - text_.size() < 16) {
            text_.reserve(text_.capacity() * 2);
        }
        ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight()));
        if (focus_.IsOk()) {
            std::print("result: {}\n", Result());
            ImGui::CloseCurrentPopup();
        }
        if (focus_.IsCancel()) {
            ImGui::CloseCurrentPopup();
        }
        focus_.End();
    }





    CustomDialog::CustomDialog(std::function<void(IDialog&)> on_close, const arguments& args) : IDialog(typeid(this).name(), on_close)
    {
    }
    void CustomDialog::OnGuiImpl()
    {
        focus_.Begin();

        if (!title_.empty()) {
            ImGui::Text("%s", title_.c_str());
            ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight()));
        }

        for (auto i = 0; i < items_.size(); ++i) {
            auto& item = items_[(size_t)i];
            if (item.value_.type() == typeid(std::string) && item.result_.type() == typeid(bool)) {
                const auto& value = std::any_cast<std::string>(item.value_);
                auto result = std::any_cast<bool>(item.result_);
                item.result_ = focus_.Checkbox(value, result);
            }
            else if (item.value_.type() == typeid(std::vector<std::string>) && item.result_.type() == typeid(int)) {
                const auto& value = std::any_cast<std::vector<std::string>>(item.value_);
                auto result = std::any_cast<int>(item.result_);

                ImGui::PushID(i);
                auto s = misc::ScopedImGuiStyle(ImGuiStyleVar_ChildRounding, 5.0f);

                auto height = ImGui::GetTextLineHeightWithSpacing() * (float)value.size() * 1.2f;
                if (ImGui::BeginChild("child", ImVec2(0, height), ImGuiChildFlags_Borders)) {
                    for (auto j = 0; j < value.size(); ++j) {
                        auto& v = value[(size_t)j];
                        //if (j > 0) ImGui::SameLine();
                        if (focus_.Selectable(v, false)) {
                            item.result_ = j;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::PopID();
            }
        }








        focus_.End();
    }
    void CustomDialog::SetTitle(const std::string& title)
    {
        title_ = title;
    }
    void CustomDialog::AddCheckbox(const std::string& label, bool initial_value)
    {
        items_.emplace_back(Item{ label, initial_value });
    }
    void CustomDialog::AddSelectables(const std::vector<std::string>& items, int initial_value)
    {
        items_.emplace_back(Item{ items, initial_value });
    }



}

