#ifndef VIEWS_BROWSER_VIEW_H__
#define VIEWS_BROWSER_VIEW_H__

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include "Constants.h"
#include "FileListView.h"
#include "QuickLookView.h"
#include "SearchBar.h"

namespace miata::views {
    // 左右2ペインのファイルリストをNSSplitViewで並べ、その上にQuick Lookのプレビューを被せられる。
    // 各ペインの下端には、そのペインの検索バー(SearchBar)を出せる。
    class BrowserView {
    public:
        BrowserView();
        ~BrowserView();

        void* NativeView() const;

        inline bool IsLeft() const
        {
            return cursorIndex_ == 0;
        }
        inline bool IsRight() const
        {
            return cursorIndex_ == 1;
        }
        void FocusLeft();
        void FocusRight();
        void ToggleFocus();
        models::FileEntryModel& CurrentFileEntryModel();

        inline std::shared_ptr<FileListView>& GetCurrentFileListView()
        {
            return cursorIndex_ == 0 ? left_ : right_;
        }

        // カーソル(フォーカス)のあるペイン
        inline constants::Pane CurrentPane() const
        {
            return IsLeft() ? constants::Pane::Left : constants::Pane::Right;
        }
        inline std::shared_ptr<FileListView>& GetFileListView(constants::Pane pane)
        {
            return pane == constants::Pane::Left ? left_ : right_;
        }

        // 両ペインの自動リロードを進める(FileListView::UpdateAutoReload参照)
        inline void UpdateAutoReload(bool allow)
        {
            left_->UpdateAutoReload(allow);
            right_->UpdateAutoReload(allow);
        }

        // 左右ペイン共通のドラッグ開始可否コールバックを設定する(FileListView::SetDragGuard参照)
        inline void SetDragGuard(const std::function<bool()>& guard)
        {
            left_->SetDragGuard(guard);
            right_->SetDragGuard(guard);
        }

        // --- Quick Look(プレビューを一覧の上に被せる) ---
        // プレビューするのは、カーソルのあるペインのカーソル下のファイル。被せる範囲(areaは両ペイン/左/右)は
        // それとは無関係に選べる(反対側のペインだけに被せれば、カーソルのある一覧は見えたまま操作できる)。
        // すでに同じ範囲に出していれば閉じる(トグル)。別の範囲に出していれば範囲だけ切り替える。
        // 呼んだ後に表示中ならtrueを返す。
        bool ToggleQuickLook(constants::QuickLookArea area);
        void HideQuickLook();
        inline bool IsQuickLookShown() const
        {
            return quick_look_area_.has_value();
        }
        // 毎ティック(Application::Update)から呼ぶ。カーソル下のファイルが変わったら、カーソルの動きが
        // 落ち着くのを少し待ってからプレビューを切り替える(動かし続けている間は切り替えない)。
        void UpdateQuickLook();

        // --- 検索(各ペインの下端の検索バー) ---
        // 検索の状態はペインごとにFileListViewが持つ(FileListView::BeginSearch参照)。ここは、検索バー(入力欄と
        // 件数の表示)と、各ペインの検索の状態との整合を取る。検索語の入力は、入力欄(本物のNSTextField)で
        // しか受けられず、入力中は、そのペインだけがTypingで、そのペインのバーの入力欄がfirst responderを
        // 持つ(Normalのキーバインドは効かない)。
        // バーはペインごとに1つで、そのペインに検索(入力中・確定済み)があるあいだ、そのペインの下端に、ペインと
        // 同じ幅で出す。一覧が縮むのも、そのペインだけ。ペインの高さは、そのペイン自身の検索の状態だけで決まる
        // ので、ペインを切り替えても変わらない。

        // カーソルのペインで検索語の入力を始める。どこかのペインが入力中、または入力欄に文字を打てなければfalse。
        bool BeginSearch();
        // カーソルのペインの、次(dir > 0)・前(dir < 0)のマッチへ動く(FileListView::StepSearch参照)
        bool StepSearch(int dir);
        // カーソルのペインの検索を終える(ハイライトを消す)。検索していたらtrue。
        bool ClearSearch();
        // 入力中なら確定して、入力欄のfirst responderを手放す。ダイアログを開く前と、ペインを切り替える前に呼ぶ。
        // ダイアログが閉じるときのDialogPanel::Hide()は、first responderを無条件にMiataRootViewへ戻すので、
        // 入力欄を持ったままだと、バーは入力中のまま、打鍵がNormalのキーバインドへ流れてしまう。また、ダイアログ向けの
        // Enter / Escを、入力欄のdelegateが受けてしまう。
        void CommitSearchInput();
        // 毎ティック(Application::Update)から呼ぶ。入力欄とペインの検索の状態を突き合わせて整える
        // (UpdateQuickLookと同じ、状態を見て判断する方式): 入力欄がfirst responderを失っていたら入力を確定し、
        // 入力欄の文字と検索語がずれていたら追いつかせ、バーの出入りと中身(件数)を更新する。
        void UpdateSearchBar();

    private:
        // 入力中(Typing)のペイン。無ければnullopt
        std::optional<constants::Pane> TypingPane() const;
        // 入力欄の文字(IMEの変換中の未確定の文字を除く)を、入力中のペインの検索語に取り込む。同じ文字なら何もしない
        void PullSearchQuery(constants::Pane pane);
        // 入力欄とペインの状態の食い違いを直す(UpdateSearchBarの前半)
        void ReconcileSearchInput();
        // 各ペインのバーの出入り(一覧を縮めて、バーを重ねる)と中身(検索語・件数)を、ペインの状態に合わせる
        // (UpdateSearchBarの後半)。入力欄のfirst responderや、ペインの検索の状態は変えない。入力を始める直前
        // (BeginSearch)のように、入力欄がまだfirst responderでないときは、食い違いを直すReconcileSearchInputを
        // 含むUpdateSearchBarではなく、こちらだけを呼ぶ(入力欄が「first responderを失った」と誤って、確定してしまう)。
        void SyncSearchBarView();

        const std::shared_ptr<FileListView>& ViewOf(constants::Pane pane) const
        {
            return pane == constants::Pane::Left ? left_ : right_;
        }
        SearchBar& BarOf(constants::Pane pane) const
        {
            return *search_bars_[static_cast<size_t>(pane)];
        }

        std::shared_ptr<FileListView> left_;
        std::shared_ptr<FileListView> right_;
        int cursorIndex_ = 0;

        std::array<std::unique_ptr<SearchBar>, 2> search_bars_; // 添字はconstants::Pane

        std::unique_ptr<QuickLookView> quick_look_;
        std::optional<constants::QuickLookArea> quick_look_area_; // 表示中の範囲。表示していなければnullopt
        // カーソル下のファイル(nulloptは一覧が空)。shown_はプレビューに渡した分、target_は直近に見た分で、
        // 食い違っている間が「切り替え待ち」(target_since_からの経過で、落ち着いたかを見る)
        std::optional<std::filesystem::path> quick_look_shown_;
        std::optional<std::filesystem::path> quick_look_target_;
        std::chrono::steady_clock::time_point quick_look_target_since_;

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif // VIEWS_BROWSER_VIEW_H__
