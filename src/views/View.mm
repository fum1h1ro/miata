#import <AppKit/AppKit.h>
#include <format>
#include "View.h"
#include "../Config.h"
#include "../Log.h"
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

    // コンソールを出すとき、ブラウザ領域に残す最小の高さ(pt)。窓が低いときは、コンソールのほうが縮む
    constexpr CGFloat kMinBrowserHeight = 120;
}

// ウィンドウの内容(contentView)の直下に置く、縦の並び: 上がブラウザ領域、その下がコンソール(全幅。出しているときだけ)、
// 下端がステータスバー(全幅の 1 行)。
// 子の位置は、明示的に計算する(isFlipped=YES なビューでは、autoresizingMask の Y 軸のマージンの意味が反転する癖があり、
// このコードベースは、FileListView.mm の _MiataFileListLayoutContainer のように、フレーム変更時に計算し直す)。
// ブラウザ領域の中(NSSplitView・Quick Look の覆い・入力バー)は、ブラウザの大きさに追従する
@interface _MiataMainLayout : NSView
@property (nonatomic, weak) NSView* browserView;
@property (nonatomic, weak) NSView* consoleView;
@property (nonatomic, weak) NSView* statusView;
@property (nonatomic, assign) CGFloat statusHeight;
// バーを隠すか。真なら、バーは隠れ(高さ 0)、ブラウザ領域が全面になる(設定 show_statusbar)。変えると、並べ直す。
// 「隠す」側を持つのは、既定(未設定=NO)が「出す」になるため(並びの部品を直接作る場合も、バーが出る)
@property (nonatomic, assign) BOOL statusHidden;
// コンソールを出すか(設定 show_console)。既定(未設定=NO)が「出さない」なので、「出す」側を持つ。変えると、並べ直す
@property (nonatomic, assign) BOOL consoleShown;
// コンソールを開いたときの高さ(pt)と、これより低いなら出さない高さ(1 行ぶん)
@property (nonatomic, assign) CGFloat consoleHeight;
@property (nonatomic, assign) CGFloat consoleMinHeight;
- (void)layoutChildren;
@end
@implementation _MiataMainLayout
- (BOOL)isFlipped { return YES; }
- (void)setStatusHidden:(BOOL)hidden
{
    _statusHidden = hidden;
    [self layoutChildren];
}
- (void)setConsoleShown:(BOOL)shown
{
    _consoleShown = shown;
    [self layoutChildren];
}
- (void)layoutChildren
{
    CGFloat w = self.bounds.size.width;
    CGFloat h = self.bounds.size.height;
    CGFloat bar = self.statusHidden ? 0 : MIN(self.statusHeight, h);
    CGFloat available = h - bar;
    // コンソールは、ブラウザ領域に最小の高さを残して、収まるだけ。窓が低くて、1 行も出せないなら、出さない(負の高さも作らない)
    CGFloat console = 0;
    if (self.consoleShown) {
        console = MIN(self.consoleHeight, MAX((CGFloat)0, available - kMinBrowserHeight));
        if (console < self.consoleMinHeight) console = 0;
    }
    self.statusView.hidden = self.statusHidden;
    self.consoleView.hidden = console <= 0;
    self.browserView.frame = NSMakeRect(0, 0, w, available - console);
    self.consoleView.frame = NSMakeRect(0, available - console, w, console);
    self.statusView.frame = NSMakeRect(0, h - bar, w, bar);
}
- (void)setFrameSize:(NSSize)newSize
{
    [super setFrameSize:newSize];
    [self layoutChildren];
}
@end

namespace miata::views {
    View::View() : browser_(std::make_unique<BrowserView>())
    {
        // ダイアログは非モーダルなオーバーレイなので、その裏のファイル一覧にもマウスは届く。
        // ダイアログ表示中はファイルのドラッグ開始を受け付けない。
        browser_->SetDragGuard([this] { return !IsAnyDialogOpened(); });

        // ブラウザ領域・コンソール・ステータスバーの縦の並び。contentView の全面に、窓に合わせて広がる
        // (中の位置は、_MiataMainLayout が、フレームが変わるたびに、明示的に計算する)
        NSView* content = (__bridge NSView*)pl_get_content_view();
        NSView* browser_view = (__bridge NSView*)browser_->NativeView();
        console_view_ = std::make_unique<ConsoleView>(models::ConsoleLog::Instance());
        NSView* console_view = (__bridge NSView*)console_view_->NativeView();
        status_bar_ = std::make_unique<StatusBar>();
        NSView* status_view = (__bridge NSView*)status_bar_->NativeView();

        browser_view.autoresizingMask = NSViewNotSizable; // 大きさは、_MiataMainLayout が決める
        console_view.autoresizingMask = NSViewNotSizable;
        _MiataMainLayout* layout = [[_MiataMainLayout alloc] initWithFrame:content.bounds];
        layout.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        layout.browserView = browser_view;
        layout.consoleView = console_view;
        layout.statusView = status_view;
        layout.statusHeight = (CGFloat)StatusBar::Height();
        layout.consoleHeight = (CGFloat)ConsoleView::Height(Config::ConsoleHeight());
        layout.consoleMinHeight = (CGFloat)ConsoleView::Height(1);
        layout.statusHidden = !Config::ShowStatusBar(); // 設定は、View を作る前に読み終えている
        layout.consoleShown = Config::ShowConsole();
        [layout addSubview:browser_view];
        [layout addSubview:console_view];
        [layout addSubview:status_view];
        [layout layoutChildren];
        [content addSubview:layout];
        main_layout_ = (__bridge void*)layout; // 保持は、contentView が持つ(アプリの間、生きている)

        // 進捗パネルの、全面の素通しのホスト。ブラウザの上に足す(Quick Look の覆いは、ブラウザの中にあるので、その上になる)。
        // ダイアログは、開くたびに contentView の末尾に足されるので、常にこれより手前になる
        progress_overlay_ = std::make_unique<ProgressOverlay>();
        NSView* progress_view = (__bridge NSView*)progress_overlay_->NativeView();
        progress_view.frame = content.bounds;
        progress_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [content addSubview:progress_view];
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
        // ログ(コンソールの赤い行)にも残す: ダイアログを閉じた後も読める。ファイル操作・ジャンプ・開く・edit の失敗は、
        // 全部ここを通る。権限の案内の文面は、画面のダイアログだけ
        LogError(reason);
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

    void View::MoveToParentOrReport(constants::Pane pane)
    {
        auto& list = GetList(pane);
        const auto from = list.Path();
        auto result = list.NavigateToParent();
        if (!result) {
            ReportFileError(std::format("移動できませんでした ({})", from.parent_path().string()), result.error());
            return;
        }
        // 親の一覧では、カーソルは先頭にある。出てきたフォルダ(from)の行へ合わせる。移動で検索と絞り込みは消えて、入力バーも
        // 消え、一覧が下へ伸びるので、先にバーを整える(バーが出たままの高さで、行を画面の中央に出すと、バーが消えた後に、
        // バーの高さの半分だけ、中央からずれる)
        browser_->UpdateQueryBars();
        GetFileListView(pane).MoveCursorToChild(from);
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
        // 移動で検索と絞り込みは消えるので、次のティックを待たずに入力バーを整える(NavigateForBrowserの末尾と同じ)
        browser_->UpdateQueryBars();
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
        // 入力バーの入力中なら、先に終わらせて、入力欄のfirst responderを手放す(理由は
        // BrowserView::SettleQueryInput。ファイル操作の失敗を知らせるダイアログなどが、入力中に割り込むことがある)
        browser_->SettleQueryInput();
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
                MoveToParentOrReport(constants::Pane::Left);
            }
            else {
                browser_->FocusLeft();
            }
            break;
        case constants::Navigate::Right:
            if (browser_->IsRight()) {
                MoveToParentOrReport(constants::Pane::Right);
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
        case constants::Navigate::Top:
            browser_->GetCurrentFileListView()->MoveCursorToTop();
            break;
        case constants::Navigate::Bottom:
            browser_->GetCurrentFileListView()->MoveCursorToBottom();
            break;
        case constants::Navigate::Ok:
            {
                // 一覧が空(ファイルもフォルダも1つも無い、または絞り込みで0行)なら、入るものが無い
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
            // Escは、プレビューも、カーソルのペインの検索(ハイライトと、下端のバー)も閉じる。絞り込みは解除しない
            // (解除は、絞り込みをもう一度始めて、空のままEnter。Luaのfilter_clear()でも解除できる)
            browser_->HideQuickLook();
            browser_->ClearSearch();
            break;
        default:
            break;
        }
        // カーソルの移動・ペインの切り替え・ディレクトリの移動で、入力バーの件数やバーの出入りが変わるので、
        // 次のティックを待たずに更新する
        browser_->UpdateQueryBars();
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

    void View::ReloadDirectory(
        const std::filesystem::path& dir,
        std::optional<std::pair<std::filesystem::path, std::filesystem::path>> renamed
    )
    {
        for (auto pane : {constants::Pane::Left, constants::Pane::Right}) {
            if (GetList(pane).Path() != dir) continue;
            auto& view = GetFileListView(pane);
            std::optional<std::filesystem::path> cursor_to;
            if (renamed && view.CurrentPath() == renamed->first) cursor_to = renamed->second;
            (void)view.Reload(std::move(cursor_to));
        }
    }

    void View::UnmarkPathsIn(const std::filesystem::path& dir, const std::vector<std::filesystem::path>& paths)
    {
        for (auto pane : {constants::Pane::Left, constants::Pane::Right}) {
            if (GetList(pane).Path() == dir) GetFileListView(pane).UnmarkPaths(paths);
        }
    }

    void View::RefreshShowIcons()
    {
        for (auto pane : {constants::Pane::Left, constants::Pane::Right}) {
            GetFileListView(pane).Redraw();
        }
    }

    void View::RefreshHiddenFiles()
    {
        for (auto pane : {constants::Pane::Left, constants::Pane::Right}) {
            GetFileListView(pane).RefreshHidden();
        }
        // 行数が変わるので、絞り込みバーの件数と、隠れたマークの数を、次のティックを待たずに更新する
        browser_->UpdateQueryBars();
    }

    bool View::ToggleQuickLook(constants::QuickLookArea area)
    {
        return browser_->ToggleQuickLook(area);
    }

    void View::UpdateQuickLook()
    {
        browser_->UpdateQuickLook();
    }

    std::optional<double> View::QuickLookZoom() const
    {
        return browser_->QuickLookZoom();
    }

    std::optional<double> View::SetQuickLookZoom(double factor)
    {
        return browser_->SetQuickLookZoom(factor);
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

    bool View::BeginFilter(MatchKind kind)
    {
        if (IsAnyDialogOpened()) return false;
        return browser_->BeginFilter(kind);
    }

    bool View::ClearFilter(constants::Pane pane)
    {
        return browser_->ClearFilter(pane);
    }

    FilterStatus View::SetFilter(constants::Pane pane, const std::string& query, MatchKind kind)
    {
        return browser_->SetFilter(pane, query, kind);
    }

    void View::UpdateQueryBars()
    {
        browser_->UpdateQueryBars();
    }

    void View::UpdateProgress(ProgressClock::time_point now, const std::vector<FileOperationStatus>& running)
    {
        progress_state_.Update(now, running);
        progress_overlay_->Update(progress_state_.Panels(now));
    }

    void View::FinishProgress(FileOperationId id, bool succeeded, ProgressClock::time_point now)
    {
        progress_state_.Finish(id, succeeded, now);
    }

    void View::SetStatusText(const StatusText& text)
    {
        status_bar_->SetText(text);
    }

    void View::RefreshStatusBarVisibility()
    {
        // バーの出入りに合わせて、ブラウザ領域の高さを直す(並べ直す)。バーの内容は、出し直したときの、次の組み立てが作る
        _MiataMainLayout* layout = (__bridge _MiataMainLayout*)main_layout_;
        layout.statusHidden = !Config::ShowStatusBar();
    }

    void View::UpdateConsole()
    {
        // 隠している間は、何もしない(ログは貯まり続ける)。出し直すとき(RefreshConsoleVisibility)に、作り直す
        if (!Config::ShowConsole()) return;
        console_view_->Sync();
    }

    void View::RefreshConsoleVisibility()
    {
        // 出入りに合わせて、ブラウザ領域の高さを直す(並べ直す)。出すときは、内容を作り直して、いちばん下を見せる(次の UpdateConsole で)。
        // ここで同期しない: 呼ばれるのは、Lua のキー関数の中(zc など)で、同期は、KeyDown の出口と、毎ティックが行う
        console_view_->Invalidate(); // 隠すときに立てても、出すときにまた立つ。出し直したときは、必ず作り直す
        _MiataMainLayout* layout = (__bridge _MiataMainLayout*)main_layout_;
        layout.consoleShown = Config::ShowConsole();
    }

    void View::ScrollConsole(int64_t lines)
    {
        if (!Config::ShowConsole()) return;
        console_view_->ScrollLines(lines);
    }

    ConsoleView& View::GetConsoleView()
    {
        return *console_view_;
    }

    const StatusText& View::GetStatusText() const
    {
        return status_bar_->Text();
    }

    void View::ToggleFocus()
    {
        browser_->ToggleFocus();
    }

    // マークの操作は、一覧が空(ファイルもフォルダも1つも無い、または絞り込みで0行)なら、何もしない(マークする対象が無い)
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
