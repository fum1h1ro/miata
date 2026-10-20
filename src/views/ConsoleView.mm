#import <AppKit/AppKit.h>
#include <algorithm>
#include <cmath>
#include "ConsoleView.h"
#include "NSColorUtil.h"
#include "ViewMetrics.h"
#include "../Config.h"

// 行を描く documentView(見えている行だけ描く)。first responder にならない(acceptsFirstResponder は既定の NO のまま:
// クリックしても、キー入力が MiataRootView に届き続ける)。マウスのイベントは扱わない
@interface _MiataConsoleDoc : NSView
@property (nonatomic, assign) miata::views::ConsoleView* owner;
@end
@implementation _MiataConsoleDoc
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect
{
    if (self.owner) self.owner->Draw(NSMinY(dirtyRect), NSMaxY(dirtyRect));
}
@end

// 背景と、上端の境目の線(入力バー・ステータスバーと共通の DrawBarBackground)を描き、その下に、スクロールビューを置く。
// 親の大きさが変わるたびに、スクロールビューの位置を直し、追従(いちばん下を見ているか)を保つ。
// hitTest: は横取りしない(ホイール・トラックパッドのスクロールが、中の NSScrollView に届くように)
@interface _MiataConsoleContainer : NSView
@property (nonatomic, assign) miata::views::ConsoleView* owner;
@end
@implementation _MiataConsoleContainer
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (void)drawRect:(NSRect)dirtyRect
{
    miata::views::DrawBarBackground(self, dirtyRect, miata::views::ToNSColor(miata::Config::Background()), miata::views::NormalTextColor());
}
- (void)setFrameSize:(NSSize)newSize
{
    // 追従の判定には、変わる前の位置・大きさが要る
    if (self.owner) self.owner->BeforeResize();
    [super setFrameSize:newSize];
    if (self.owner) self.owner->AfterResize();
}
@end

namespace miata::views {

namespace {
    // 上端の境目の線の太さ(pt)。DrawBarBackground の線と同じ。スクロールビューは、この線の下に置く
    constexpr CGFloat kTopBorder = 1;

    // コンテナの大きさから、スクロールビューの枠
    NSRect ScrollFrame(NSSize container_size)
    {
        return NSMakeRect(0, kTopBorder, container_size.width, std::max<CGFloat>(0, container_size.height - kTopBorder));
    }
}

struct ConsoleView::Impl {
    explicit Impl(const models::ConsoleLog& l) : log(l) {}

    const models::ConsoleLog& log;
    _MiataConsoleContainer* container = nil;
    NSScrollView* scroll = nil;
    _MiataConsoleDoc* doc = nil;

    // 最後に同期したときの、ログの世代・先頭の通し番号・行数。描画は、この範囲だけを描く(同期の後にログが進んでも、
    // 次の同期までは、documentView の高さと一致した範囲を描く)
    uint64_t synced_epoch = 0;
    uint64_t synced_first_seq = 0;
    size_t synced_rows = 0;
    // 次の同期で、必ず作り直して、いちばん下を見せる(最初と、出し直したとき)
    bool stale = true;
    // 大きさが変わる前の、位置と大きさ(BeforeResize が控える)
    double pre_y = 0;
    double pre_clip_height = 0;
    double pre_doc_height = 0;

    NSClipView* Clip() const { return scroll.contentView; }

    // documentView を rows 行の高さ(と、見えている高さの大きい方。背景を全面に塗るため)にして、スクロール位置を決める
    void Apply(size_t rows, bool force_bottom, uint64_t dropped, double old_y, double old_clip_height, double old_doc_height)
    {
        NSClipView* clip = Clip();
        const CGFloat clip_height = clip.bounds.size.height;
        const CGFloat clip_width = clip.bounds.size.width;
        const double row_height = ListRowHeight();
        const double doc_height = std::max(static_cast<double>(rows) * row_height, static_cast<double>(clip_height));
        doc.frame = NSMakeRect(0, 0, clip_width, doc_height);
        const double y = NextScrollY(old_y, old_clip_height, old_doc_height, clip_height, doc_height, row_height, dropped, force_bottom);
        [clip scrollToPoint:NSMakePoint(0, y)];
        [scroll reflectScrolledClipView:clip];
        doc.needsDisplay = YES;
    }
};

ConsoleView::ConsoleView(const models::ConsoleLog& log) : impl_(std::make_unique<Impl>(log))
{
    auto& i = *impl_;
    const CGFloat height = static_cast<CGFloat>(Height(Config::ConsoleHeight()));
    i.container = [[_MiataConsoleContainer alloc] initWithFrame:NSMakeRect(0, 0, 200, height)];
    // スクロールバーなど、OS が描く部品の見た目を、背景の明るさに合わせる(暗い背景の上に Light 用の部品が載らないように)。
    // ブラウザ領域のコンテナ(BrowserView)の外にあるので、ここで設定する
    i.container.appearance = AppearanceForBackground();

    i.doc = [[_MiataConsoleDoc alloc] initWithFrame:NSMakeRect(0, 0, 200, 0)];
    i.scroll = [[NSScrollView alloc] initWithFrame:ScrollFrame(NSMakeSize(200, height))];
    i.scroll.hasVerticalScroller = YES;
    i.scroll.hasHorizontalScroller = NO;
    i.scroll.drawsBackground = NO;
    i.scroll.documentView = i.doc;
    [i.container addSubview:i.scroll];

    // 作り終えてから、結ぶ(作る途中の setFrameSize: などで、未完成の自分を呼ばない)
    i.container.owner = this;
    i.doc.owner = this;
}

ConsoleView::~ConsoleView()
{
    // ビューは、親(contentView)が保持している。自分が消えた後に、ビューが自分を呼ばないようにする
    impl_->container.owner = nullptr;
    impl_->doc.owner = nullptr;
}

void* ConsoleView::NativeView() const
{
    return (__bridge void*)impl_->container;
}

double ConsoleView::Height(int rows)
{
    return static_cast<double>(rows) * ListRowHeight() + kTopBorder;
}

void ConsoleView::Invalidate()
{
    impl_->stale = true;
}

void ConsoleView::Sync()
{
    auto& i = *impl_;
    // 世代の比較だけで済ませる(ロックしない)。毎ティックは、ここで終わる
    if (!i.stale && i.log.Epoch() == i.synced_epoch) return;
    // 変わっていた。行の範囲と世代を、ロックして、まとめて読む(読んだ世代が、いま同期する中身の世代)
    const auto range = i.log.Range();

    NSClipView* clip = i.Clip();
    const CGFloat clip_height = clip.bounds.size.height;
    // 見えていない(隠れている・高さ 0)ときは、何もしない。世代も消費しない(見えるようになった次の呼び出しで直す)
    if (clip_height <= 0) return;

    // 先頭から捨てられた行数。上へ戻って読んでいるとき、同じ行が見え続けるように、位置を戻す。Clear の後は、全部の行が
    // 捨てられたことになり、先頭に戻る。first_seq は増える一方なので、引き算はあふれない。大きすぎても、NextScrollY が
    // 範囲(0〜いちばん下)に収める
    const uint64_t dropped = range.first_seq - i.synced_first_seq;
    i.Apply(range.Count(), i.stale, dropped, clip.bounds.origin.y, clip_height, i.doc.frame.size.height);

    i.synced_epoch = range.epoch;
    i.synced_first_seq = range.first_seq;
    i.synced_rows = range.Count();
    i.stale = false;
}

void ConsoleView::ScrollLines(int64_t lines)
{
    auto& i = *impl_;
    NSClipView* clip = i.Clip();
    const double max_y = std::max(0.0, static_cast<double>(i.doc.frame.size.height - clip.bounds.size.height));
    const double y = std::clamp(static_cast<double>(clip.bounds.origin.y) + static_cast<double>(lines) * ListRowHeight(), 0.0, max_y);
    [clip scrollToPoint:NSMakePoint(0, y)];
    [i.scroll reflectScrolledClipView:clip];
}

ConsoleView::State ConsoleView::GetState() const
{
    const auto& i = *impl_;
    NSClipView* clip = i.Clip();
    const double y = clip.bounds.origin.y;
    const double clip_height = clip.bounds.size.height;
    const double doc_height = i.doc.frame.size.height;
    return State{y, clip_height, doc_height, i.synced_rows, IsAtBottom(y, clip_height, doc_height)};
}

void ConsoleView::BeforeResize()
{
    auto& i = *impl_;
    NSClipView* clip = i.Clip();
    i.pre_y = clip.bounds.origin.y;
    i.pre_clip_height = clip.bounds.size.height;
    i.pre_doc_height = i.doc.frame.size.height;
}

void ConsoleView::AfterResize()
{
    auto& i = *impl_;
    i.scroll.frame = ScrollFrame(i.container.bounds.size);
    if (i.Clip().bounds.size.height <= 0) return;
    // 行は変わらない(大きさだけが変わった)。いちばん下を見ていれば、いちばん下を保つ
    i.Apply(i.synced_rows, false, 0, i.pre_y, i.pre_clip_height, i.pre_doc_height);
}

void ConsoleView::Draw(double min_y, double max_y)
{
    auto& i = *impl_;
    // 背景。行が足りない部分まで含めて、documentView の全面を塗る(塗らないと、後ろのウィンドウの背景が見える)
    [ToNSColor(Config::Background()) set];
    NSRectFill(i.doc.bounds);

    const CGFloat row_height = ListRowHeight();
    if (row_height <= 0) return;
    // 描き直す範囲にかかる行だけ描く(数万行あっても、見えている行の分だけ)。行が無ければ、last が first より小さくなる
    const int first = std::max(0, static_cast<int>(std::floor(min_y / row_height)));
    const int last = std::min(static_cast<int>(i.synced_rows) - 1, static_cast<int>(std::ceil(max_y / row_height)) - 1);
    if (last < first) return;

    NSFont* font = MakeFont(Config::FontSize());
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.lineBreakMode = NSLineBreakByTruncatingTail; // 折り返さず、長い行は末尾を「…」にする
    NSDictionary* normal_attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: NormalTextColor(),
        NSParagraphStyleAttributeName: style,
    };
    NSDictionary* error_attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: ToNSColor(Config::Color().Get(Config::Color::Type::ConsoleError)),
        NSParagraphStyleAttributeName: style,
    };

    const CGFloat width = i.doc.bounds.size.width;
    const auto lines = i.log.Lines(i.synced_first_seq + static_cast<uint64_t>(first), static_cast<size_t>(last - first + 1));
    for (const auto& line : lines) {
        // 行の位置は、通し番号から決める。同期の後にログが進んでいて、先頭が捨てられていても、行と内容がずれない。
        // Lines は from(= 同期した先頭 + first)以降の行だけを返すので、row は first 以上。同期の後に先頭が捨てられていると、
        // 返る行が後ろへずれて、描く範囲を超える
        const uint64_t row = line.seq - i.synced_first_seq;
        if (row > static_cast<uint64_t>(last)) break;

        // 整形済みなので UTF-8 として正しい(nil にならない)が、念のため。nil を描くと例外になる
        NSString* text = [[NSString alloc] initWithBytes:line.text.data() length:line.text.size() encoding:NSUTF8StringEncoding];
        if (!text) continue;
        NSDictionary* attrs = line.kind == models::LogKind::Error ? error_attrs : normal_attrs;
        const NSSize size = [text sizeWithAttributes:attrs];
        // 縦位置は、一覧の行と同じ式(文字列の高さで、行の中央)
        const NSRect rect = NSMakeRect(
            kListPadding,
            static_cast<CGFloat>(row) * row_height + (row_height - size.height) / 2,
            std::max<CGFloat>(0, width - kListPadding * 2),
            size.height
        );
        [text drawInRect:rect withAttributes:attrs];
    }
}

}
