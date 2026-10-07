#ifndef VIEWS_PROGRESS_OVERLAY_H__
#define VIEWS_PROGRESS_OVERLAY_H__

#include <memory>
#include <vector>
#include "ProgressState.h"

namespace miata::views {
    // ファイル操作(コピー・移動)の進捗を、ウィンドウの右上に重ねて出す、半透明のパネル(1 操作 = 1 枚。縦に積む)。
    // モードレス: クリックも、キー入力も受けない(下の一覧のクリック・ドラッグ・スクロールが、そのまま使える)。
    // 何を、いつ出すかは ProgressState が決める。ここは、渡された内容を描くだけ。
    // AppKit の型は、ProgressOverlay.mm にだけ閉じ込める(QueryBar・QuickLookView と同じ pimpl)。
    class ProgressOverlay {
    public:
        ProgressOverlay();
        ~ProgressOverlay();

        // 全面の、素通しのホストビュー。親(contentView)の、ブラウザの上に足す(幅・高さは autoresizing で追従する)。
        // ダイアログは、開くたびに contentView の末尾に足されるので、常にこれより手前になる
        void* NativeView() const;

        // 毎ティック呼ぶ。同じ内容なら、AppKit に触らない。変わったパネルだけを、描き直す
        void Update(const std::vector<ProgressPanel>& panels);

        // パネル 1 枚の高さ(pt)。フォントの設定で決まる。配置とテストが共有する
        static double PanelHeight();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif // VIEWS_PROGRESS_OVERLAY_H__
