#import <AppKit/AppKit.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include "QueryBar.h"
#include "../Config.h"
#include "NSColorUtil.h"
#include "ViewMetrics.h"

namespace {
    constexpr CGFloat kGap = 6; // 入力欄と件数の間

    CGFloat LineHeight(NSFont* font)
    {
        return std::ceil(font.ascender - font.descender + font.leading);
    }

    NSColor* TextColor()
    {
        return miata::views::ToNSColor(miata::Config::Color().Get(miata::Config::Color::Type::NormalText));
    }

    // フィールドエディタ(ウィンドウの全テキスト欄が共有する)が、IMEの変換中の文字を持っているか
    BOOL IsComposing(NSText* editor)
    {
        return [editor isKindOfClass:[NSTextView class]] && [(NSTextView*)editor hasMarkedText];
    }
}

// 入力欄のdelegate。Enter / Esc / ↑↓ / Tabだけを横取りし、通常の文字入力とIMEの変換中の操作は、AppKit標準の
// 経路(素通し)に任せる。doCommandBySelector:は変換中は呼ばれないので、変換候補の選択中のEnter / 矢印は
// 自然にIME側に渡る(FilterListDialog.mmと同じ作法)。
@interface _MiataQueryFieldDelegate : NSObject <NSTextFieldDelegate>
// QueryBar::Implが持つ。QueryBarが破棄されたらnullptrにする(次のランループに逃がしたブロックが、破棄後に
// 呼ぶのを防ぐ)
@property (nonatomic, assign) const miata::views::QueryBar::Callbacks* callbacks;
@end
@implementation _MiataQueryFieldDelegate
- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)commandSelector
{
    if (!self.callbacks) return NO;
    // 矢印の上下と、Ctrl-P / Ctrl-N(NSTextViewの標準のキー割り当てで、moveUp: / moveDown: になる)は、
    // 検索なら次・前のマッチへ、絞り込みなら一覧のカーソルを1行。同期で処理する(カーソルを動かすだけ)
    if (commandSelector == @selector(moveUp:)) {
        if (self.callbacks->on_step) self.callbacks->on_step(-1);
        return YES;
    }
    if (commandSelector == @selector(moveDown:)) {
        if (self.callbacks->on_step) self.callbacks->on_step(1);
        return YES;
    }
    // Enter / Escは、確定・取り消しで入力欄を手放す(first responderを移す)ので、その処理をdelegateの
    // 呼び出しの中で行わないよう、次のランループへ逃がす
    if (commandSelector == @selector(insertNewline:)) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (self.callbacks && self.callbacks->on_commit) self.callbacks->on_commit();
        });
        return YES;
    }
    if (commandSelector == @selector(cancelOperation:)) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (self.callbacks && self.callbacks->on_cancel) self.callbacks->on_cancel();
        });
        return YES;
    }
    // Tab / Shift-Tab: 既定ではキービューループで次のビューへ移って、入力欄を失ってしまうので握りつぶす
    if (commandSelector == @selector(insertTab:) || commandSelector == @selector(insertBacktab:)) {
        return YES;
    }
    return NO;
}
- (void)controlTextDidChange:(NSNotification*)notification
{
    // IMEの変換中の扱い(語にしない)は、文字を読む側(QueryBar::SettledText)が決める
    if (self.callbacks && self.callbacks->on_query_changed) self.callbacks->on_query_changed();
}
@end

// バー全体の背景。設定の背景色で塗り(OSのテーマの色は使わない)、上端に一覧との境目の線を引く。
@interface _MiataQueryBarView : NSView
@property (nonatomic, weak) NSTextField* prompt;
@property (nonatomic, weak) NSTextField* field;
@property (nonatomic, weak) NSTextField* count;
- (void)layoutBar;
@end
@implementation _MiataQueryBarView
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (void)drawRect:(NSRect)dirtyRect
{
    // dirtyRectは、自分の範囲(bounds)を超えて渡されることがある(親をまるごと描くとき。macOS 14以降、ビューは
    // 既定で自分の範囲に描画を切り詰めないので、そのまま塗ると、隣のビュー(一覧)を塗りつぶしてしまう)
    NSRect rect = NSIntersectionRect(dirtyRect, self.bounds);
    if (NSIsEmptyRect(rect)) return;
    [miata::views::ToNSColor(miata::Config::Background()) setFill];
    NSRectFill(rect);
    [[TextColor() colorWithAlphaComponent:0.35] setFill];
    NSRectFillUsingOperation(NSIntersectionRect(NSMakeRect(0, 0, self.bounds.size.width, 1), rect), NSCompositingOperationSourceOver);
}
// 入力中以外は、マウスを常にこのビューが受ける(何もしない)。入力欄はクリックでfirst responderを
// 奪えないので、確定後に、キー入力の窓口(MiataRootView)から外れない(_MiataQuickLookShieldと同じ考え方)。
- (NSView*)hitTest:(NSPoint)point
{
    if (self.field.editable) return [super hitTest:point];
    return [super hitTest:point] ? self : nil;
}
- (void)setFrameSize:(NSSize)newSize
{
    [super setFrameSize:newSize];
    [self layoutBar];
}
// プロンプト + 入力欄 + 右端の件数を、左右の余白で挟んで横に並べる。明示的に計算する(BrowserViewの子で、
// flippedなビューのautoresizingは意図どおりに効かないため。FileListViewのレイアウトコンテナと同じ)
- (void)layoutBar
{
    using miata::views::kListPadding;
    NSFont* font = self.field.font;
    CGFloat width = self.bounds.size.width;
    CGFloat height = self.bounds.size.height;
    CGFloat line = LineHeight(font) + 2;
    CGFloat y = std::floor((height - line) / 2);
    NSDictionary* attrs = @{NSFontAttributeName: font};

    CGFloat prompt_w = std::ceil([self.prompt.stringValue sizeWithAttributes:attrs].width) + 2;
    CGFloat count_w = std::ceil([self.count.stringValue sizeWithAttributes:attrs].width) + 4;
    CGFloat count_x = std::max(kListPadding + prompt_w, width - kListPadding - count_w);
    CGFloat field_x = kListPadding + prompt_w;

    self.prompt.frame = NSMakeRect(kListPadding, y, prompt_w, line);
    self.field.frame = NSMakeRect(field_x, y, std::max((CGFloat)0, count_x - kGap - field_x), line);
    self.count.frame = NSMakeRect(count_x, y, std::max((CGFloat)0, width - kListPadding - count_x), line);
}
@end

namespace miata::views {

struct QueryBar::Impl {
    _MiataQueryBarView* bar = nil;
    NSTextField* prompt = nil;
    NSTextField* field = nil;
    NSTextField* count = nil;
    _MiataQueryFieldDelegate* delegate = nil;
    Callbacks callbacks;
    // 入力中だけ変えるキャレットの色の、元の色。フィールドエディタはウィンドウの全テキスト欄(ダイアログの
    // 入力欄など)が共有していて、設定した色は次の欄にも残る(実測)ので、入力が終わったら戻す
    NSColor* saved_caret_color = nil;

    // 表示の差分更新用(同じ内容なら、AppKitに触らない)。入力欄の文字は、入力中にこちらの知らないところで
    // 変わる(打鍵、編集の終了で欄に取り込まれる)ので、覚えておかず、実際の中身と比べる
    bool shown_active = false;
    std::string shown_count;

    ~Impl()
    {
        delegate.callbacks = nullptr;
        field.delegate = nil;
    }

    void SetEditable(bool editable)
    {
        if (field.editable == editable && field.refusesFirstResponder == !editable) return; // 毎ティックの片付けから呼ばれる
        field.editable = editable;
        field.selectable = editable;
        // 確定後は、クリックで入力欄がfirst responderを奪えないようにする(奪われると、キー入力が
        // MiataRootViewに届かなくなる)
        field.refusesFirstResponder = !editable;
    }
};

QueryBar::QueryBar(const std::string& prompt) : impl_(std::make_unique<Impl>())
{
    NSFont* font = MakeFont(Config::FontSize());
    NSColor* text_color = TextColor();

    impl_->bar = [[_MiataQueryBarView alloc] initWithFrame:NSMakeRect(0, 0, 200, Height())];

    impl_->prompt = [NSTextField labelWithString:@(prompt.c_str())];
    impl_->prompt.font = font;
    impl_->prompt.textColor = text_color;
    impl_->prompt.hidden = YES;

    impl_->count = [NSTextField labelWithString:@""];
    impl_->count.font = font;
    impl_->count.textColor = [text_color colorWithAlphaComponent:0.8];
    impl_->count.alignment = NSTextAlignmentRight;

    impl_->field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 100, 20)];
    impl_->field.font = font;
    impl_->field.textColor = text_color;
    impl_->field.bordered = NO;
    impl_->field.bezeled = NO;
    impl_->field.drawsBackground = NO;
    impl_->field.focusRingType = NSFocusRingTypeNone;
    impl_->field.lineBreakMode = NSLineBreakByClipping;
    [impl_->field.cell setUsesSingleLineMode:YES];
    [impl_->field.cell setScrollable:YES];
    [impl_->field.cell setWraps:NO];

    impl_->delegate = [[_MiataQueryFieldDelegate alloc] init];
    impl_->delegate.callbacks = &impl_->callbacks;
    impl_->field.delegate = impl_->delegate;
    impl_->SetEditable(false);

    impl_->bar.prompt = impl_->prompt;
    impl_->bar.field = impl_->field;
    impl_->bar.count = impl_->count;
    [impl_->bar addSubview:impl_->prompt];
    [impl_->bar addSubview:impl_->field];
    [impl_->bar addSubview:impl_->count];
    [impl_->bar layoutBar];
}

QueryBar::~QueryBar() = default;

void* QueryBar::NativeView() const
{
    return (__bridge void*)impl_->bar;
}

double QueryBar::Height()
{
    return HeaderHeight();
}

void QueryBar::SetCallbacks(Callbacks callbacks)
{
    impl_->callbacks = std::move(callbacks);
}

void QueryBar::Update(const Display& display)
{
    bool relayout = false;
    if (display.active != impl_->shown_active) {
        impl_->shown_active = display.active;
        impl_->prompt.hidden = !display.active;
    }
    if (display.count != impl_->shown_count) {
        impl_->shown_count = display.count;
        impl_->count.stringValue = @(display.count.c_str());
        relayout = true;
    }
    // 入力中は、入力欄の文字に触らない(打っている途中の文字や、IMEの変換中の文字を壊さないため)。
    // それ以外は、実際の中身と比べる: 入力が終わると、打っていた文字が欄に取り込まれて残るので
    // (取り消した語が欄に残って、古い文字が見えてしまう)、覚えていた値とは比べない
    if (!IsEditing()) {
        NSString* want = @(display.query.c_str()) ?: @"";
        if (![impl_->field.stringValue isEqualToString:want]) impl_->field.stringValue = want;
    }
    if (relayout) [impl_->bar layoutBar];
}

bool QueryBar::BeginInput()
{
    NSWindow* window = impl_->field.window;
    if (!window) return false;

    impl_->field.stringValue = @"";
    impl_->SetEditable(true);
    if (![window makeFirstResponder:impl_->field] || !impl_->field.currentEditor) {
        impl_->SetEditable(false);
        return false;
    }
    // 暗い背景でもキャレットが見えるように、文字と同じ色にする(終わったら戻す。saved_caret_color参照)
    NSText* editor = impl_->field.currentEditor;
    if ([editor isKindOfClass:[NSTextView class]]) {
        impl_->saved_caret_color = ((NSTextView*)editor).insertionPointColor;
        ((NSTextView*)editor).insertionPointColor = TextColor();
    }
    return true;
}

void QueryBar::EndInput()
{
    NSWindow* window = impl_->field.window;
    NSText* editor = impl_->field.currentEditor;
    // 入力欄がfirst responderのときだけ、キー入力の窓口(MiataRootView = contentView)に戻す。ダイアログが
    // 閉じるとき(DialogPanel::Hide)などで、すでに他へ移っているときは、他の誰かのfirst responderを奪わない
    if (window && editor && window.firstResponder == editor) {
        [window makeFirstResponder:window.contentView];
    }
    impl_->SetEditable(false);

    // キャレットの色を戻す。入力欄がfirst responderを奪われた後でも、フィールドエディタ自体は残っている
    if (impl_->saved_caret_color) {
        NSText* shared = window ? [window fieldEditor:NO forObject:nil] : nil;
        if ([shared isKindOfClass:[NSTextView class]]) ((NSTextView*)shared).insertionPointColor = impl_->saved_caret_color;
        impl_->saved_caret_color = nil;
    }
}

bool QueryBar::IsEditing() const
{
    return impl_->field.currentEditor != nil;
}

std::optional<std::string> QueryBar::SettledText() const
{
    NSText* editor = impl_->field.currentEditor;
    if (!editor || IsComposing(editor)) return std::nullopt;
    const char* utf8 = editor.string.UTF8String;
    return std::string(utf8 ? utf8 : "");
}

} // namespace miata::views
