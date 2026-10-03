#import <AppKit/AppKit.h>
#include <format>
#include "View.h"
#include "../platform.h"
#include "../Utf8.h"

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

    // pathが、フォルダとして存在しないと確かめられたか(無い、またはフォルダでなくなった)。権限や一時的なI/Oエラー、
    // シンボリックリンクの循環などで調べられなかったとき(typeがnone)は、有無が分からないのでfalse。権限が無い失敗でも、
    // フォルダ自体は調べられる(存在する)か、調べられない(false)かのどちらかなので、これだけで「権限が無いだけなら
    // 外さない」になる。(FileListView.mmにも同じ名前の別の判定があるので、名前を変えている)
    bool IsKnownNotDirectory(const std::filesystem::path& path)
    {
        std::error_code ec;
        auto type = std::filesystem::status(path, ec).type();
        return type != std::filesystem::file_type::none && type != std::filesystem::file_type::directory;
    }

    // ペインの状態の保存先のキー(NSUserDefaults)。ペインごとに別のキーにするのは、片方のペインが動いたときに、
    // もう一方の保存してある値(入れなくて、ホームのままだった場所)を上書きしないため。履歴("MiataHistory")や
    // ウィンドウ位置("NSWindow Frame MiataMainWindow")とは別のキー
    constexpr const char* kPaneKeyLeft = "MiataPaneLeft";
    constexpr const char* kPaneKeyRight = "MiataPaneRight";
    // フォーカスしているペイン("focus" = "left" / "right")。ペインごとのキーに入れない(ペインを切り替えるたびに、
    // 両方のペインのキーを書き直すことになり、保存してある値を上書きするため)
    constexpr const char* kSessionKey = "MiataSession";

    const char* PaneKey(miata::views::constants::Pane pane)
    {
        return pane == miata::views::constants::Pane::Left ? kPaneKeyLeft : kPaneKeyRight;
    }
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

    bool View::ActivateDialogMnemonic(char key)
    {
        return IsAnyDialogOpened() && current_dialog_->ActivateMnemonic(key);
    }

    void View::RequestDialog(std::shared_ptr<IDialog> dialog)
    {
        dialog_requests_.push(dialog);
        OpenNextDialogIfNeeded();
    }

    void View::ReportFileError(const std::string& what, const FileError& error)
    {
        // 文面にはパスが入る(whatに埋め込まれる)。UTF-8として不正なバイトがあると(Luaのjump_toに渡したパスなど)、
        // NSStringにできず、ダイアログのラベル(labelWithString:)が例外で落ちるので、U+FFFDに置き換える
        auto reason = RepairUtf8(std::format("{}: {}", what, error.message));
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

    bool View::JumpToOrReport(models::FileListModel& list, const std::filesystem::path& path)
    {
        auto result = list.TryJumpTo(path);
        if (!result) ReportFileError(std::format("移動できませんでした ({})", path.string()), result.error());
        return result.has_value();
    }

    bool View::JumpToPath(constants::Pane pane, const std::filesystem::path& path)
    {
        auto& list = GetList(pane);
        // ヘッダーに末尾の/が出ないよう、履歴と同じ形(末尾の/なし)にそろえて移る
        std::filesystem::path target(models::PathHistory::TrimTrailingSlash(path.string()));
        bool jumped = JumpToOrReport(list, target);
        if (!jumped && IsKnownNotDirectory(target)) models::BrowserModel::Instance().History().Remove(target);
        // 移動で検索は消えるので、次のティックを待たずに検索バーを整える(NavigateForBrowserの末尾と同じ)
        browser_->UpdateSearchBar();
        return jumped;
    }

    models::PaneState View::GetPaneState(constants::Pane pane)
    {
        auto& view = GetFileListView(pane);
        return models::PaneState{
            .path = GetList(pane).Path().string(),
            .sort_key = FileListView::SortKeyName(view.GetSortKey()),
            .sort_reverse = view.GetSortReverse(),
        };
    }

    void View::RestorePanes()
    {
        for (auto pane : {constants::Pane::Left, constants::Pane::Right}) {
            auto saved = models::PaneState::FromMap(pl_load_string_map(PaneKey(pane)));
            // ソート: 基準の名前が分からなければ(壊れた値)、"name" にする
            auto sort_key = FileListView::ParseSortKey(saved.sort_key).value_or(FileListView::SortKey::Name);
            GetFileListView(pane).SetSort(sort_key, saved.sort_reverse);
            // フォルダ: 入れなければ(エラーは捨てる)、一覧もパスも変わらず、ホームのまま。いまのフォルダなら、走査し直さない
            auto& list = GetList(pane);
            if (!saved.path.empty() && std::filesystem::path(saved.path) != list.Path()) {
                (void)list.TryRestoreTo(saved.path);
            }
            saved_panes_[static_cast<size_t>(pane)] = GetPaneState(pane);
        }

        auto session = pl_load_string_map(kSessionKey);
        if (auto focus = session.find("focus"); focus != session.end() && focus->second == "right") {
            browser_->FocusRight(); // 起動時のフォーカスは左(BrowserViewのコンストラクタ)
        }
        saved_focus_ = CurrentPane();
    }

    void View::SavePanesIfChanged()
    {
        for (auto pane : {constants::Pane::Left, constants::Pane::Right}) {
            auto now = GetPaneState(pane);
            auto& saved = saved_panes_[static_cast<size_t>(pane)];
            if (now == saved) continue;
            pl_save_string_map(PaneKey(pane), now.ToMap());
            saved = std::move(now);
        }

        auto focus = CurrentPane();
        if (focus != saved_focus_) {
            pl_save_string_map(kSessionKey, {{"focus", focus == constants::Pane::Left ? "left" : "right"}});
            saved_focus_ = focus;
        }
    }

    void View::OpenNextDialogIfNeeded()
    {
        if (current_dialog_ != nullptr) return;
        if (dialog_requests_.empty()) return;
        // 検索の入力中なら、先に確定して、入力欄のfirst responderを手放す(理由は
        // BrowserView::CommitSearchInput。ファイル操作の完了を知らせるダイアログなどが、入力中に割り込むことがある)
        browser_->CommitSearchInput();
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
                // 一覧が空(ファイルもフォルダも1つも無い)なら、入るものが無い
                auto* entry_model = browser_->CurrentFileEntryModel();
                if (entry_model && entry_model->IsDirectory()) {
                    if (browser_->IsLeft()) {
                        JumpToOrReport(browser_model.Left(), browser_model.Left().Path() / entry_model->Name());
                    }
                    else {
                        JumpToOrReport(browser_model.Right(), browser_model.Right().Path() / entry_model->Name());
                    }
                }
            }
            break;
        case constants::Navigate::Cancel:
            // Escは、プレビューも、カーソルのペインの検索(ハイライトと、下端のバー)も閉じる
            browser_->HideQuickLook();
            browser_->ClearSearch();
            break;
        default:
            break;
        }
        // カーソルの移動・ペインの切り替え・ディレクトリの移動で、検索バーの件数やバーの出入りが変わるので、
        // 次のティックを待たずに更新する
        browser_->UpdateSearchBar();
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

    models::FileEntryModel* View::CurrentEntry()
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

    bool View::BeginSearch()
    {
        if (IsAnyDialogOpened()) return false;
        return browser_->BeginSearch();
    }

    bool View::StepSearch(int dir)
    {
        if (IsAnyDialogOpened()) return false;
        return browser_->StepSearch(dir);
    }

    bool View::ClearSearch()
    {
        return browser_->ClearSearch();
    }

    void View::UpdateSearchBar()
    {
        browser_->UpdateSearchBar();
    }

    void View::ToggleFocus()
    {
        browser_->ToggleFocus();
    }

    // マークの操作は、一覧が空(ファイルもフォルダも1つも無い)なら、何もしない(マークする対象が無い)
    void View::Mark()
    {
        auto* entry_model = browser_->CurrentFileEntryModel();
        if (!entry_model) return;
        entry_model->Mark(true);
        CurrentFileListView().Redraw();
    }

    void View::Unmark()
    {
        auto* entry_model = browser_->CurrentFileEntryModel();
        if (!entry_model) return;
        entry_model->Mark(false);
        CurrentFileListView().Redraw();
    }

    void View::ToggleMark()
    {
        auto* entry_model = browser_->CurrentFileEntryModel();
        if (!entry_model) return;
        entry_model->Mark(!entry_model->IsMarked());
        CurrentFileListView().Redraw();
    }
}
