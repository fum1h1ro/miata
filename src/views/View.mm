#import <AppKit/AppKit.h>
#include <format>
#include "View.h"
#include "../platform.h"

namespace {
    // 権限が無い失敗のときに添える、許可のしかたの案内。権限が無い失敗(EPERM / EACCES)のうち、OSの保護(プライバシーと
    // セキュリティ)で止められたものは、システム設定の「フルディスクアクセス」で許可すれば解消する。ファイル自体の権限
    // (所有者・アクセス権)が原因のものは、それでは解消しないので、その旨も添える(どちらも同じ「権限が無い」として届く
    // ので、原因は区別できない)。アプリ本体(.app)の削除・変更は、フルディスクアクセスとは別に「App管理」の許可が要る。
    constexpr const char* kPermissionGuide =
        "権限がないため、操作できませんでした。\n\n"
        "macOS の保護で止められた場合は、システム設定の「プライバシーとセキュリティ」→"
        "「フルディスクアクセス」で Miata を許可すると、操作できるようになります（アプリ本体の削除や変更は「App管理」）。"
        "許可したあと、反映されないときは Miata を起動し直してください。\n\n"
        "ファイル自体の権限が原因のときは、Finder の「情報を見る」で、所有者とアクセス権を確認してください。";
}

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

    void View::ReportFileError(const std::string& what, const FileError& error)
    {
        auto reason = std::format("{}: {}", what, error.message);
        if (!error.permission_denied) {
            RequestDialog(std::make_shared<ConfirmDialog>(
                [](IDialog&) {},
                ConfirmDialog::arguments{
                    .message_ = reason,
                    .button_text_ = "OK",
                }
            ));
            return;
        }

        // 権限が無い(OSの保護など)。許可のしかたを案内し、システム設定を開けるようにする。
        // 既定は「閉じる」(エラーを閉じようとしたEnterで、システム設定が開いてしまわないように)
        RequestDialog(std::make_shared<YesNoDialog>(
            [](IDialog& dialog) {
                if (dynamic_cast<YesNoDialog&>(dialog).Result()) pl_open_full_disk_access_settings();
            },
            YesNoDialog::arguments{
                .message_ = reason + "\n\n" + kPermissionGuide,
                .default_select_ = false,
                .yes_text_ = "システム設定を開く",
                .no_text_ = "閉じる",
            }
        ));
    }

    void View::MoveToParentOrReport(models::FileListModel& list)
    {
        auto parent = list.Path().parent_path();
        auto result = list.NavigateToParent();
        if (!result) ReportFileError(std::format("移動できませんでした ({})", parent.string()), result.error());
    }

    void View::JumpToOrReport(models::FileListModel& list, const std::filesystem::path& path)
    {
        auto result = list.TryJumpTo(path);
        if (!result) ReportFileError(std::format("移動できませんでした ({})", path.string()), result.error());
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
                MoveToParentOrReport(browser_model.Left());
            }
            else {
                browser_->FocusLeft();
            }
            break;
        case constants::Navigate::Right:
            if (browser_->IsRight()) {
                MoveToParentOrReport(browser_model.Right());
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
                        JumpToOrReport(browser_model.Left(), browser_model.Left().Path() / entry_model.Name());
                    }
                    else {
                        JumpToOrReport(browser_model.Right(), browser_model.Right().Path() / entry_model.Name());
                    }
                }
            }
            break;
        case constants::Navigate::Cancel:
            browser_->HideQuickLook();
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

    FileListView* View::FindFileListView(const models::FileListModel& list)
    {
        auto& browser_model = models::BrowserModel::Instance();
        if (&list == &browser_model.Left()) return &GetFileListView(constants::Pane::Left);
        if (&list == &browser_model.Right()) return &GetFileListView(constants::Pane::Right);
        return nullptr;
    }

    void View::ReloadList(const models::FileListModel& list, std::optional<std::filesystem::path> cursor_to)
    {
        if (auto* view = FindFileListView(list)) {
            (void)view->Reload(std::move(cursor_to));
        }
    }

    void View::ClearListMarks(const models::FileListModel& list)
    {
        if (auto* view = FindFileListView(list)) {
            view->ClearMarks();
        }
    }

    bool View::ToggleQuickLook(constants::QuickLookArea area)
    {
        return browser_->ToggleQuickLook(area);
    }

    void View::UpdateQuickLook()
    {
        browser_->UpdateQuickLook();
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
