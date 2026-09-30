#import <AppKit/AppKit.h>
#include "View.h"
#include "../platform.h"

namespace miata::views {
    View::View() : browser_(std::make_unique<BrowserView>())
    {
        // ダイアログは非モーダルなオーバーレイなので、その裏のファイル一覧にもマウスは届く。
        // ダイアログ表示中はファイルのドラッグ開始を受け付けない。
        browser_->SetDragGuard([this] { return !IsAnyDialogOpened(); });

        NSView* content = (__bridge NSView*)pl_get_content_view();
        NSView* browser_view = (__bridge NSView*)browser_->NativeView();
        browser_view.frame = content.bounds;
        browser_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [content addSubview:browser_view];
    }

    View::~View() = default;

    void View::Navigate(constants::Navigate dir)
    {
        if (IsAnyDialogOpened()) {
            current_dialog_->Navigate(dir);
            return;
        }
        NavigateForBrowser(dir);
    }

    void View::RequestDialog(std::shared_ptr<IDialog> dialog)
    {
        dialog_requests_.push(dialog);
        OpenNextDialogIfNeeded();
    }

    void View::OpenNextDialogIfNeeded()
    {
        if (current_dialog_ != nullptr) return;
        if (dialog_requests_.empty()) return;
        current_dialog_ = dialog_requests_.front();
        dialog_requests_.pop();
        current_dialog_->Open();
    }

    void View::CheckDialogState()
    {
        if (current_dialog_ != nullptr && !current_dialog_->IsOpened()) {
            current_dialog_.reset();
        }
        OpenNextDialogIfNeeded();
    }

    void View::UpdateAutoReload()
    {
        browser_->UpdateAutoReload(!IsAnyDialogOpened());
    }

    void View::NavigateForBrowser(constants::Navigate dir)
    {
        auto& browser_model = models::BrowserModel::Instance();
        switch (dir) {
        case constants::Navigate::Left:
            if (browser_->IsLeft()) {
                browser_model.Left().NavigateToParent();
            }
            else {
                browser_->FocusLeft();
            }
            break;
        case constants::Navigate::Right:
            if (browser_->IsRight()) {
                browser_model.Right().NavigateToParent();
            }
            else {
                browser_->FocusRight();
            }
            break;
        case constants::Navigate::Down:
            browser_->GetCurrentFileListView()->MoveCursor(1);
            break;
        case constants::Navigate::Up:
            browser_->GetCurrentFileListView()->MoveCursor(-1);
            break;
        case constants::Navigate::Ok:
            {
                auto& entry_model = browser_->CurrentFileEntryModel();
                if (entry_model.IsDirectory()) {
                    if (browser_->IsLeft()) {
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
        auto& browser_model = models::BrowserModel::Instance();
        return browser_->IsLeft() ? browser_model.Left() : browser_model.Right();
    }

    models::FileListModel& View::OtherList()
    {
        auto& browser_model = models::BrowserModel::Instance();
        return browser_->IsLeft() ? browser_model.Right() : browser_model.Left();
    }

    models::FileEntryModel& View::CurrentEntry()
    {
        return browser_->CurrentFileEntryModel();
    }

    FileListView& View::CurrentFileListView()
    {
        return *browser_->GetCurrentFileListView();
    }

    constants::Pane View::CurrentPane() const
    {
        return browser_->CurrentPane();
    }

    FileListView& View::GetFileListView(constants::Pane pane)
    {
        return *browser_->GetFileListView(pane);
    }

    models::FileListModel& View::GetList(constants::Pane pane)
    {
        auto& browser_model = models::BrowserModel::Instance();
        return pane == constants::Pane::Left ? browser_model.Left() : browser_model.Right();
    }

    void View::ReloadList(const models::FileListModel& list, std::optional<std::filesystem::path> cursor_to)
    {
        auto& browser_model = models::BrowserModel::Instance();
        if (&list == &browser_model.Left()) {
            (void)GetFileListView(constants::Pane::Left).Reload(std::move(cursor_to));
        }
        else if (&list == &browser_model.Right()) {
            (void)GetFileListView(constants::Pane::Right).Reload(std::move(cursor_to));
        }
    }

    void View::ToggleFocus()
    {
        browser_->ToggleFocus();
    }

    void View::Mark()
    {
        browser_->CurrentFileEntryModel().Mark(true);
        CurrentFileListView().Redraw();
    }

    void View::Unmark()
    {
        browser_->CurrentFileEntryModel().Mark(false);
        CurrentFileListView().Redraw();
    }

    void View::ToggleMark()
    {
        auto& entry_model = browser_->CurrentFileEntryModel();
        entry_model.Mark(!entry_model.IsMarked());
        CurrentFileListView().Redraw();
    }
}
