#ifndef VIEWS_STATUS_BAR_H__
#define VIEWS_STATUS_BAR_H__

#include <memory>
#include "StatusBarState.h"

namespace miata::views {
    // ウィンドウの下端の、全幅のステータスバー(1 行)。左(カーソル下のファイルの情報・次のキー候補)と、右(マークの要約)を出す。
    // 渡された内容を描くだけ(何を出すかは StatusBarState。ProgressOverlay と同じ分担)。AppKit 型は .mm に閉じ込める(pimpl)。
    //  - 子に NSTextField を持たず、drawRect: で描く(色は設定のものだけ=statusbar_text / statusbar_background。
    //    OS の外観に依存しない。文字のサイズは set_statusbar_font_size)
    //  - クリックは吸収する(下にはウィンドウの背景しか無い)。first responder にはならない(キー入力が MiataRootView に届き続ける)
    //  - 右を先に確保し(幅の半分まで)、左は、残りに収まらなければ末尾を「…」にする
    class StatusBar {
    public:
        StatusBar();
        ~StatusBar();

        // 親(View)にaddSubviewするためのNSView*を(__bridge void*)で返す。位置と大きさは、親が決める
        void* NativeView() const;
        // バーの高さ(pt)。バーの文字のサイズ(Config::StatusBarFontSize)から決まる(ヘッダーと同じ式。同じサイズなら同じ高さ)
        static double Height();
        // 内容を更新する。前回と同じ内容なら、AppKit に触らない。文字列は、UTF-8 として正しい形(SanitizeBarText 済み)であること
        void SetText(const StatusText& text);
        // いま出している内容(テスト用)
        const StatusText& Text() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif // VIEWS_STATUS_BAR_H__
