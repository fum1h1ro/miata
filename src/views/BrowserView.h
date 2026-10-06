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
#include "QueryBar.h"

namespace miata::views {
    // 左右2ペインのファイルリストをNSSplitViewで並べ、その上にQuick Lookのプレビューを被せられる。
    // 各ペインの下端には、そのペインの入力バー(QueryBar。ファイル名の検索と、絞り込み)を出せる。
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
        // カーソルのペインの、カーソル下のエントリ。一覧が空(ファイルもフォルダも1つも無い、または絞り込みで0行)ならnullptr
        models::FileEntryModel* CurrentFileEntryModel();

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

        // --- ペインごとの入力(各ペインの下端の入力バー。ファイル名の検索と、絞り込み) ---
        // 入力の状態はペインごとにFileListViewが持つ(FileListView::BeginSearch / BeginFilter参照)。ここは、入力バー(入力欄と
        // 件数の表示)と、各ペインの状態との整合を取る。語の入力は、入力欄(本物のNSTextField)でしか受けられず、
        // 入力中は、そのペインの、その種類だけがTypingで、そのバーの入力欄がfirst responderを持つ(Normalの
        // キーバインドは効かない)。入力中の対象は、全体で常に1つ(first responderが1つなので)。
        // バーはペインごと・種類ごとに1つで、そのペインにその種類の入力(入力中・確定済み)があるあいだ、そのペインの
        // 下端に、ペインと同じ幅で出す(下端から、検索 → 絞り込みの順に積む)。一覧が縮むのも、そのペインだけ。
        // ペインの高さは、そのペイン自身の状態だけで決まるので、ペインを切り替えても変わらない。

        // カーソルのペインで検索語の入力を始める。どこかが入力中、または入力欄に文字を打てなければfalse。
        bool BeginSearch();
        // カーソルのペインの、次(dir > 0)・前(dir < 0)のマッチへ動く(FileListView::StepSearch参照)
        bool StepSearch(int dir);
        // カーソルのペインの検索を終える(ハイライトを消す)。検索していたらtrue。
        bool ClearSearch();
        // カーソルのペインで、絞り込みの語の入力を始める(入力欄に打つたびに、一覧が絞り込まれる)。始める条件はBeginSearchと同じ。
        // kindは、語の一致のしかた(部分一致か、あいまい一致か)。
        bool BeginFilter(MatchKind kind = MatchKind::Substring);
        // paneの絞り込みを解除する(入力中なら入力欄も手放す)。絞り込んでいたらtrue。
        bool ClearFilter(constants::Pane pane);
        // paneの絞り込みを、入力欄を使わずに、queryの確定済みにする(空なら解除)。入力中なら、先に終わらせる。
        // 戻り値は、結果の状態(見えている行数と全行数)。
        FilterStatus SetFilter(constants::Pane pane, const std::string& query, MatchKind kind = MatchKind::Substring);
        // 入力中なら、成り行きで終わらせて(語があれば確定する。絞り込みの語が空なら、解除ではなく取り消す=前の絞り込みに戻る。
        // 空で確定=解除は、ユーザーがEnterで押したときだけ)、入力欄のfirst responderを手放す。ダイアログを開く前と、
        // ペインを切り替える前に呼ぶ。ダイアログが閉じるときのDialogPanel::Hide()は、first responderを無条件に
        // MiataRootViewへ戻すので、入力欄を持ったままだと、バーは入力中のまま、打鍵がNormalのキーバインドへ流れてしまう。
        // また、ダイアログ向けのEnter / Escを、入力欄のdelegateが受けてしまう。
        void SettleQueryInput();
        // 毎ティック(Application::Update)から呼ぶ。入力欄とペインの状態を突き合わせて整える
        // (UpdateQuickLookと同じ、状態を見て判断する方式): 入力欄がfirst responderを失っていたら入力を終わらせ、
        // 入力欄の文字と語がずれていたら追いつかせ、バーの出入りと中身(件数)を更新する。
        void UpdateQueryBars();

    private:
        // 入力の対象: 入力バーの1つ(ペインと種類)
        struct QueryTarget {
            constants::Pane pane;
            constants::QueryKind kind;
            bool operator==(const QueryTarget&) const = default;
        };
        static size_t SlotIndex(QueryTarget target)
        {
            return static_cast<size_t>(target.pane) * constants::kQueryKindCount + static_cast<size_t>(target.kind);
        }

        // 入力中(Typing)の対象。無ければnullopt
        std::optional<QueryTarget> TypingTarget() const;
        // カーソルのペインで、kindの入力を始める
        // matchは、絞り込みの語の一致のしかた(検索では使わない)
        bool BeginInput(constants::QueryKind kind, MatchKind match = MatchKind::Substring);
        // 入力中のtargetを終わらせる(入力欄の最新の文字を取り込んでから、確定する)。explicit_enterは、ユーザーがEnterで
        // 確定した(true)か、成り行きで終わる(false。ダイアログが開く前・ペインの切り替え・入力欄がfirst responderを失った)か。
        // 絞り込みの語が空のとき、Enterなら解除、成り行きなら取り消す(前の絞り込みを、黙って失わないため)
        void FinishTyping(QueryTarget target, bool explicit_enter);
        // 入力欄の文字(IMEの変換中の未確定の文字を除く)を、入力中のtargetの語に取り込む。同じ文字なら何もしない
        void PullQuery(QueryTarget target);
        // 入力欄と各ペインの状態の食い違いを直す(UpdateQueryBarsの前半)
        void ReconcileInput();
        // 各ペインのバーの出入り(一覧を縮めて、バーを重ねる)と中身(語・件数)を、ペインの状態に合わせる
        // (UpdateQueryBarsの後半)。入力欄のfirst responderや、ペインの状態は変えない。入力を始める直前
        // (BeginInput)のように、入力欄がまだfirst responderでないときは、食い違いを直すReconcileInputを
        // 含むUpdateQueryBarsではなく、こちらだけを呼ぶ(入力欄が「first responderを失った」と誤って、確定してしまう)。
        void SyncQueryBars();

        const std::shared_ptr<FileListView>& ViewOf(constants::Pane pane) const
        {
            return pane == constants::Pane::Left ? left_ : right_;
        }
        QueryBar& BarOf(QueryTarget target) const
        {
            return *bars_[SlotIndex(target)];
        }

        std::shared_ptr<FileListView> left_;
        std::shared_ptr<FileListView> right_;
        int cursorIndex_ = 0;

        std::array<std::unique_ptr<QueryBar>, 2 * constants::kQueryKindCount> bars_; // 添字はSlotIndex()

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
