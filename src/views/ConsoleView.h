#ifndef VIEWS_CONSOLE_VIEW_H__
#define VIEWS_CONSOLE_VIEW_H__

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include "../models/ConsoleLog.h"

namespace miata::views {
    // いちばん下を見ているか(誤差 1pt まで)。追従の判定(NextScrollY)と、状態の写し(GetState)が使う
    inline bool IsAtBottom(double y, double clip_height, double doc_height)
    {
        constexpr double kBottomTolerance = 1.0; // いちばん下とみなす誤差(pt)
        return y + clip_height >= doc_height - kBottomTolerance;
    }

    // 更新の後の、スクロール位置(documentView を見ているクリップビューの原点の y)。AppKit に依存しない純関数。
    //  - 追従: 更新の前に、いちばん下を見ていた(old_y + old_clip_height が old_doc_height の 1pt 以内)か、force_bottom なら、
    //    更新の後のいちばん下。ログが増えても、最新の行が見え続ける
    //  - それ以外(上へ戻って読んでいる): 古い行を dropped_rows 行捨てたぶん、位置を戻す。読んでいる行が、同じ場所に見え続ける。
    //    範囲(0〜いちばん下)に収める
    // 追従の状態は持たず、いつも位置から決める(ユーザーのスクロール・窓の大きさの変更・ログの増減のどれの後でも、同じ規則)
    inline double NextScrollY(
        double old_y,
        double old_clip_height,
        double old_doc_height,
        double new_clip_height,
        double new_doc_height,
        double row_height,
        uint64_t dropped_rows,
        bool force_bottom
    )
    {
        const double max_y = std::max(0.0, new_doc_height - new_clip_height);
        if (force_bottom || IsAtBottom(old_y, old_clip_height, old_doc_height)) return max_y;
        return std::clamp(old_y - static_cast<double>(dropped_rows) * row_height, 0.0, max_y);
    }

    // コンソール(ブラウザ領域とステータスバーの間の、全幅のログのペイン)。ConsoleLog に貯まった行を描く。AppKit 型は .mm に
    // 閉じ込める(pimpl)。FileListView と同じ作り: 素の NSScrollView + 自前に描く documentView(見えている行だけ描く)。
    //  - 色は、背景 background・文字 normal_text・エラー行 console_error(設定)。フォントは一覧と同じ(set_font / set_font_size)。
    //    行の高さも一覧と同じ(ListRowHeight)。折り返さず、長い行は末尾を「…」にする。テキスト選択は無い
    //  - first responder にならず、hitTest も横取りしない(キー入力が MiataRootView に届き続ける。ホイール・トラックパッドの
    //    スクロールは、素の NSScrollView が処理する)。NSTextView は使わない
    //  - 更新は、メインのティックが Sync を呼ぶ(世代が変わったときだけ AppKit に触る)。ConsoleLog への書き込みの中では、
    //    ビューに触らない(print は Lua の中・設定の読み込み中・描画の途中からも呼ばれる)
    class ConsoleView {
    public:
        explicit ConsoleView(const models::ConsoleLog& log);
        ~ConsoleView();

        // 親(View)にaddSubviewするためのNSView*を(__bridge void*)で返す。位置と大きさは、親が決める
        void* NativeView() const;
        // rows 行を出すときのビューの高さ(pt)。行の高さは一覧と同じ(ListRowHeight)+ 上端の境目の線 1pt
        static double Height(int rows);

        // 受け口の世代が変わっていれば、行数・スクロール位置・描画を直す。変わっていなければ AppKit に触らない。
        // 見えていない(高さ 0)ときは、何もせず、世代も消費しない(見えるようになった次の呼び出しで直す)。
        // 追従: いちばん下を見ていれば、最新の行が見え続ける。上へ戻って読んでいる間は、動かさない
        // (古い行を捨てても、同じ行が同じ場所に見える)。呼ぶのは、メインのティックと KeyDown の出口(Application::UpdateConsole)
        void Sync();
        // 出し直したとき: 次の Sync で、必ず作り直して、いちばん下を見せる
        void Invalidate();
        // lines 行ぶん、スクロールする(正で下=新しい方、負で上)。端で止まる(巨大な値でもよい)
        void ScrollLines(int64_t lines);

        // 状態(テスト用)
        struct State {
            double scroll_y;     // クリップビューの原点の y
            double clip_height;  // 見えている高さ
            double doc_height;   // documentView の高さ
            size_t rows;         // 同期した行数
            bool at_bottom;      // いちばん下を見ているか
        };
        State GetState() const;

        // 以下は、このクラスの AppKit のビューが呼ぶ(外からは呼ばない)
        void Draw(double min_y, double max_y);
        void BeforeResize();
        void AfterResize();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif // VIEWS_CONSOLE_VIEW_H__
