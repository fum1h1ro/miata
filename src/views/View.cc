#include <algorithm>
#include <Carbon/Carbon.h>
#include <any>
#include <ranges>
#include "View.h"
#include <sokol_app.h>

namespace miata::views {
    View::View(int w, int h)
    {
        mode_ = Mode::Browser;
        printf("app_view: %d, %d\n", w, h);
        window_width_ = w;
        window_height_ = h;
        auto log_height = 100;//(int)((float)h * 0.3f);

        //panes_.test_ = std::make_unique<widgets::HorizontalLayouter>("test", w, h);
        panes_.test_ = std::make_unique<widgets::VerticalLayouter>("test", w, h);
        panes_.test_->AddChild(std::make_shared<widgets::Pane>("left", w*0.5, h));
        panes_.test_->AddChild(std::make_shared<widgets::Pane>("center", w*0.5, h));
        //panes_.test_->AddChild(std::make_shared<widgets::Pane>("right", w*0.5, h));

        panes_.main_ = std::make_unique<widgets::VerticalLayouter>("main", w*0, h*0);
        panes_.top_ = std::make_shared<widgets::Pane>("top", w, h);
        panes_.bottom_ = std::make_shared<widgets::Pane>("bottom", w, h);
        panes_.main_->AddChild(panes_.top_);
        panes_.main_->AddChild(panes_.bottom_);

        panes_.browser_ = std::make_shared<BrowserView>("browser", 0, 0);
        //panes_.browser_->ChildFlags().on(ImGuiChildFlags_ResizeY);
        //panes_.browser_->ChildFlags().off(ImGuiChildFlags_Borders);
        panes_.browser_->FocusLeft();
        panes_.top_->AddChild(panes_.browser_);

        panes_.log_ = std::make_unique<widgets::Pane>("log", w, log_height);

        panes_.viewer_ = std::make_unique<widgets::Pane>("viewer", w, h);





    }

    View::~View()
    {
    }

    void View::OnGui(int width, int height)
    {
        //sapp_set_mouse_cursor(SAPP_MOUSECURSOR_ARROW);
#if 1
        //ImGui::PushFont(font);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)width, (float)height), ImGuiCond_Always);
        //if (window_width_ <= 0 && window_height_ <= 0) {
        //    window_width_ = width;
        //    window_height_ = height;
        //}
        resized_ = (window_width_ != width || window_height_ != height);
        window_width_ = width;
        window_height_ = height;

        if (ImGui::Begin("WindowAll", nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            //ImGuiWindowFlags_NoBackground |
            0
        )) {
            //if (resized_) {
            //    auto top_ratio = pane_sizes_.top_.y / pane_sizes_.window_.y;
            //    auto log_height = panes_.bottom_->height();
            //    auto bottom_height = pane_sizes_.bottom_.y + 4; // @bug magic number
            //    panes_.top_->width(width);
            //    //panes_.top_->height((int)((float)height * top_ratio));
            //    panes_.top_->height(height - (int)bottom_height);
            //    panes_.top_->ForceResize();
            //    printf("bottom_height: %f\n", bottom_height);

            //    panes_.browser_->width(width);
            //    panes_.browser_->height(height - log_height);
            //    panes_.browser_->ForceResize();
            //}
            //else {
            //    pane_sizes_.window_ = ImVec2((float)width, (float)height);
            //    pane_sizes_.top_ = panes_.top_->size();
            //    pane_sizes_.bottom_ = panes_.bottom_->size();
            //    printf("bottom: %f, %f\n", pane_sizes_.bottom_.x, pane_sizes_.bottom_.y);
            //}

#if 1
            auto winsz = ImGui::GetWindowSize();
            //std::print("sz: {}, {}\n", width, height);
            //std::print("winsz: {}, {}\n", winsz.x, winsz.y);
            //panes_.test_->OnGui(width, height);
            panes_.main_->OnGui(width, height);
            //panes_.top_->on_gui(resized_, [&]() {
            //    ImGui::Text("Top");
            //});
            //panes_.bottom_->on_gui(resized_, [&]() {
            //    ImGui::Text("Bottom");
            //});
#else
            switch (mode_) {
            case Mode::Browser:
                {
                    panes_.browser_->on_gui(resized_);
                    //if (panes_.file_list_->OnGui(resized_, [&]() {
                    //    ImGui::Text("最下層Hello Child 3");
                    //})) {

                    //}
                    if (panes_.log_->on_gui(resized_, [&]() {
                        ImGui::Text("最下層Hello Child\n1\n2\n3");
                    })) {

                    }
                }
                break;
            case Mode::Viewer:
                {
                    panes_.viewer_->on_gui(resized_);
                }
                break;
            default:
                throw std::runtime_error("Unknown mode");
            }
#endif
            //OnGuiBrowser();
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        //ImGui::PopFont();
        //if (ImGui::IsItemHovered()) {
            //ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
            //sapp_set_mouse_cursor(SAPP_MOUSECURSOR_NOT_ALLOWED);
        //}
        //else {
        //    sapp_set_mouse_cursor(SAPP_MOUSECURSOR_ARROW);
        //}
#endif

#if 0
        auto rect = ImGui::GetContentRegionAvail();
        //printf("GetContentRegionAvail: %f, %f\n", rect.x, rect.y);
        auto io = ImGui::GetIO();
        //printf("io.DisplaySize: %f, %f\n", io.DisplaySize.x, io.DisplaySize.y);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        if (ImGui::Begin("Hello", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::Button("Hello2", ImVec2(64, 64));
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 rect_min = ImVec2(8, 8);
            ImVec2 rect_max = ImVec2(rect_min.x + 32, rect_min.y + 32);
            ImU32 col = IM_COL32(255, 0, 127, 255);
            draw_list->AddRectFilled(rect_min, rect_max, col, 0.0f);
        }
        ImGui::End();
        ImGui::PopStyleVar();
#endif

        OnGuiDialogs(width, height);

        //ImGui::ShowMetricsWindow();
    }

    void View::Navigate(constants::Navigate dir)
    {
        //std::println("Navigate: {}", (int)dir);
        if (IsAnyDialogOpened()) {
            auto& d = CurrentDialog();
            d->Navigate(dir);
            return;
        }
        switch (mode_) {
        case Mode::Browser:
            NavigateForBrowser(dir);
            break;
        }
    }

    void View::OnGuiDialogs(int width, int height)
    {
        if (current_dialog_ == nullptr && !dialog_requests_.empty()) {
            auto& req = dialog_requests_.front();
            printf("open dialog: %s\n", req->Id().c_str());
            current_dialog_ = req;
            dialog_requests_.pop();
            current_dialog_->Open();
        }

        if (current_dialog_ != nullptr) {
            current_dialog_->OnGui(width, height);
            if (!current_dialog_->IsOpened()) {
                current_dialog_.reset();
            }
        }
    }

    std::shared_ptr<views::IDialog>& View::CurrentDialog()
    {
        return current_dialog_;
    }

    void View::RequestDialog(std::shared_ptr<IDialog> dialog)
    {
        dialog_requests_.push(dialog);
    }


    void View::NavigateForBrowser(constants::Navigate dir)
    {
        auto& browser_view = panes_.browser_;
        auto& browser_model = models::BrowserModel::Instance();
        switch (dir) {
        case constants::Navigate::Left:
            if (browser_view->IsLeft()) {
                browser_model.Left().NavigateToParent();
            }
            else {
                browser_view->FocusLeft();
            }
            break;
        case constants::Navigate::Right:
            if (browser_view->IsRight()) {
                browser_model.Right().NavigateToParent();
            }
            else {
                browser_view->FocusRight();
            }
            break;
        case constants::Navigate::Down:
            browser_view->GetCurrentFileListView()->MoveCursor(1);
            break;
        case constants::Navigate::Up:
            browser_view->GetCurrentFileListView()->MoveCursor(-1);
            break;
        case constants::Navigate::Ok:
            {
                auto& entry_model = browser_view->CurrentFileEntryModel();
                if (entry_model.IsDirectory()) {
                    if (browser_view->IsLeft()) {
                        auto new_path = browser_model.Left().Path() / entry_model.Name();
                        browser_model.Left().JumpTo(new_path);
                    }
                    else {
                        auto new_path = browser_model.Right().Path() / entry_model.Name();
                        browser_model.Right().JumpTo(new_path);
                    }
                }
            }
            break;
        default:
            break;
        }
    }

    models::FileListModel& View::CurrentList()
    {
        auto& browser = panes_.browser_;
        auto& browser_model = models::BrowserModel::Instance();
        if (browser->IsLeft()) {
            return browser_model.Left();
        }
        else {
            return browser_model.Right();
        }
    }

    models::FileListModel& View::OtherList()
    {
        auto& browser = panes_.browser_;
        auto& browser_model = models::BrowserModel::Instance();
        if (browser->IsLeft()) {
            return browser_model.Right();
        }
        else {
            return browser_model.Left();
        }
    }

    models::FileEntryModel& View::CurrentEntry()
    {
        return panes_.browser_->CurrentFileEntryModel();
    }

    FileListView& View::CurrentFileListView()
    {
        return *panes_.browser_->GetCurrentFileListView();
    }

    void View::ToggleFocus()
    {
        auto& browser= panes_.browser_;
        browser->ToggleFocus();
    }

    void View::Mark()
    {
        auto& browser= panes_.browser_;
        auto& entry_model = browser->CurrentFileEntryModel();
        entry_model.Mark(true);
    }

    void View::Unmark()
    {
        auto& browser= panes_.browser_;
        auto& entry_model = browser->CurrentFileEntryModel();
        entry_model.Mark(false);
    }

    void View::ToggleMark()
    {
        auto& browser= panes_.browser_;
        auto& entry_model = browser->CurrentFileEntryModel();
        entry_model.Mark(!entry_model.IsMarked());
    }






}

