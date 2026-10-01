#ifndef VIEWS_BROWSER_VIEW_H__
#define VIEWS_BROWSER_VIEW_H__

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include "Constants.h"
#include "FileListView.h"
#include "QuickLookView.h"

namespace miata::views {
    // 左右2ペインのファイルリストをNSSplitViewで並べ、その上にQuick Lookのプレビューを被せられる。
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

    private:
        std::shared_ptr<FileListView> left_;
        std::shared_ptr<FileListView> right_;
        int cursorIndex_ = 0;

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
