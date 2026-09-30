#ifndef VIEWS_QUICK_LOOK_VIEW_H__
#define VIEWS_QUICK_LOOK_VIEW_H__

#include <filesystem>
#include <memory>
#include <optional>

namespace miata::views {
    // Quick Look(QLPreviewView)のプレビューを、ファイル一覧の上に被せて表示するビュー。
    // 実体は、マウスを受け止める覆いのNSViewと、その中のQLPreviewView。AppKit型はQuickLookView.mmに
    // 閉じ込める。表示する位置(frame)は親(BrowserView)が決める。
    //
    // 覆いは、クリックがQLPreviewViewに届かないようにするためのもの。QLPreviewViewはfirst responderに
    // なれ、クリックで奪われるとキー入力の窓口(MiataRootView)から外れてしまう。その代わり、プレビューの中の
    // マウス操作(PDFのスクロール、動画の再生ボタン等)はできない。
    class QuickLookView {
    public:
        QuickLookView();
        ~QuickLookView();

        // 親にaddSubviewするNSView*(覆い)を(__bridge void*)で返す。表示していない間は隠れている
        void* NativeView() const;

        // 表示を始める(QLPreviewViewを作る)。表示中なら何もしない。中身はSetFile()で設定する
        void Show();
        // 表示をやめる(QLPreviewViewをcloseして手放す)。表示していなければ何もしない
        void Hide();
        bool IsShown() const;

        // プレビューするファイルを切り替える。nulloptなら空にする。表示していなければ何もしない
        void SetFile(const std::optional<std::filesystem::path>& path);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif // VIEWS_QUICK_LOOK_VIEW_H__
