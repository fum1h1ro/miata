#import <AppKit/AppKit.h>
#include <algorithm>
#include <cmath>
#include "Dialog.h"
#include "../platform.h"

// NSView の座標系はデフォルトで下原点なので、上から積むレイアウトのために flipped にする
@interface _MiataDialogBackground : NSView
@end
@implementation _MiataDialogBackground
- (BOOL)isFlipped { return YES; }
@end

// NSButton の target は weak 参照のため、生きているのは _MiataControlTarget を
// retain し続ける DialogPanel::Impl::targets_ 配列のみ。
// fire: の実行中に呼び出し元(ボタン押下)経由でダイアログ自体が閉じられ、
// この target 自身が回収されることがあるため、実際の呼び出しは次のランループへ逃がす。
// ボタンと、選択リストの行(_MiataSelectRow)が共有する。同期的に閉じると、NavigateOk が持っている focusables への参照も壊れる。
@interface _MiataControlTarget : NSObject
@property (nonatomic, copy) void (^action)(id sender);
- (void)fire:(id)sender;
@end
@implementation _MiataControlTarget
- (void)fire:(id)sender
{
    auto block = self.action;
    if (!block) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        block(sender);
    });
}
@end

// 選択リストの行の、文字の左右の余白。ObjC のクラスは C++ の名前空間の外に置くので、定数もここに置く
static const CGFloat kSelectRowTextInset = 8;

// 選択リストの行(DialogPanel::AddSelectList)。background の直接の子として、他のコントロールと同じ focusable に登録する。
// NavigateOk からは performClick: だけで作用し、通知は _MiataControlTarget::fire: が次のランループへ逃がす(ボタンと同じ)。
// first responder にならない(キー入力は MiataRootView に届き続ける必要がある。_MiataFileListNSView と同じ)。
// クリックは、行の上で押して行の上で離したときだけ確定する。セルを持たない NSControl なので、target/action は使わず、
// activator を持つ。
@interface _MiataSelectRow : NSControl
@property (nonatomic, copy) NSString* rowTitle;
@property (nonatomic, strong) _MiataControlTarget* activator;
@end
@implementation _MiataSelectRow
- (BOOL)acceptsFirstResponder { return NO; }
- (void)drawRect:(NSRect)dirtyRect
{
    // 背景(カーソル行の塗り)は layer が持つ(DialogPanel::Impl::UpdateFocusHighlight)。ここでは文字だけを描く
    if (self.rowTitle.length == 0) return;
    // 段落スタイルを付けないと、長い文字列は折り返されて1行目だけが見える(FilterListDialog と同じ)。
    // 選択肢は先頭が大事なので、末尾を省く
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.lineBreakMode = NSLineBreakByTruncatingTail;
    NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:13],
        NSForegroundColorAttributeName: [NSColor labelColor],
        NSParagraphStyleAttributeName: style,
    };
    NSSize size = [self.rowTitle sizeWithAttributes:attrs];
    NSRect bounds = self.bounds;
    NSRect text_rect = NSMakeRect(kSelectRowTextInset, floor((bounds.size.height - size.height) / 2),
                                  bounds.size.width - kSelectRowTextInset * 2, size.height);
    [self.rowTitle drawInRect:text_rect withAttributes:attrs];
}
- (void)mouseDown:(NSEvent*)event
{
    // 押しただけでは確定しない。何もしない実装で上書きして、mouseUp: をこのビューに届ける
}
- (void)mouseUp:(NSEvent*)event
{
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (NSPointInRect(p, self.bounds)) [self performClick:nil];
}
- (void)performClick:(id)sender
{
    [self.activator fire:self];
}
// 標準の NSPopUpButton が持っていたアクセシビリティの代わり(ボタンとして読まれ、押せる)
- (BOOL)isAccessibilityElement { return YES; }
- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityButtonRole; }
- (NSString*)accessibilityLabel { return self.rowTitle; }
- (BOOL)accessibilityPerformPress
{
    [self performClick:nil];
    return YES;
}
@end

namespace miata::views {

namespace {
    constexpr CGFloat kPanelWidth = 400;
    constexpr CGFloat kPadding = 16;
    constexpr CGFloat kRowHeight = 24;
    constexpr CGFloat kRowSpacing = 8;
    constexpr CGFloat kButtonHeight = 28;
    constexpr CGFloat kButtonSpacing = 8;
    constexpr CGFloat kSelectRowHeight = 22;    // 選択リストの行の高さ。行どうしは隙間なく接する(行の間のクリックが、後ろの箱に当たらないように)
    constexpr CGFloat kSelectBoxInset = 4;      // リストの箱と、行の間
    constexpr CGFloat kCursorFillAlpha = 0.25;  // カーソル行の塗り(selectedContentBackgroundColor に掛ける。文字色は反転しない)

    struct Rect { CGFloat x, y, w, h; };
    Rect FrameOf(NSView* v)
    {
        NSRect f = v.frame;
        return { f.origin.x, f.origin.y, f.size.width, f.size.height };
    }
    bool Overlaps(const Rect& a, const Rect& b)
    {
        return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
    }

    // ボタンや選択リストの行が確定されたときの通知先。DialogPanel::on_button_ を id つきで呼ぶ(呼ぶのは次のランループ)。
    // panel は生ポインタで、呼ぶ時点で生きている前提(既存のボタンと同じ)。引数名を id にすると、ブロックの (id sender) の型を隠す
    _MiataControlTarget* MakeTarget(DialogPanel* panel, int control_id)
    {
        _MiataControlTarget* target = [[_MiataControlTarget alloc] init];
        target.action = ^(id sender) {
            if (panel->on_button_) panel->on_button_(control_id);
        };
        return target;
    }
}

struct DialogPanel::Impl {
    _MiataDialogBackground* background = nil;
    NSMutableArray<NSButton*>* buttons = nil;
    NSMutableArray<_MiataControlTarget*>* targets = nil; // ARCでretainさせるための保持
    NSTextField* text_field = nil;

    struct Focusable {
        NSView* view;
        int id;
    };
    std::vector<Focusable> focusables;
    int next_id = 1;
    int focus_index = -1;
    int initial_focus_id = -1; // Layout() で最初にカーソルを置く項目。-1 なら先頭
    CGFloat cursor_y = 0;
    CGFloat panel_width = kPanelWidth;

    void EnsureBackground()
    {
        if (background) return;
        background = [[_MiataDialogBackground alloc] initWithFrame:NSMakeRect(0, 0, panel_width, 0)];
        background.wantsLayer = YES;
        background.layer.cornerRadius = 8.0;
        auto bg = pl_get_color(pl_color_type::window_background_color);
        background.layer.backgroundColor = [NSColor colorWithRed:bg.r green:bg.g blue:bg.b alpha:0.98].CGColor;
        background.layer.borderWidth = 1.0;
        background.layer.borderColor = [NSColor separatorColor].CGColor;
        buttons = [NSMutableArray array];
        targets = [NSMutableArray array];
        cursor_y = kPadding;
    }

    NSTextField* MakeLabel(const std::string& text, bool bold)
    {
        NSTextField* label = [NSTextField labelWithString:@(text.c_str())];
        label.font = bold ? [NSFont boldSystemFontOfSize:13] : [NSFont systemFontOfSize:13];
        label.lineBreakMode = NSLineBreakByWordWrapping;
        label.maximumNumberOfLines = 0;
        CGFloat avail = panel_width - kPadding * 2;
        NSRect bounds = [label.attributedStringValue boundingRectWithSize:NSMakeSize(avail, CGFLOAT_MAX)
                                                                    options:NSStringDrawingUsesLineFragmentOrigin];
        CGFloat height = std::max((CGFloat)18.0, std::ceil(bounds.size.height) + 4);
        label.frame = NSMakeRect(kPadding, cursor_y, avail, height);
        [background addSubview:label];
        cursor_y += height + kRowSpacing;
        return label;
    }

    void RegisterFocusable(NSView* view, int id)
    {
        view.wantsLayer = YES;
        focusables.push_back({view, id});
    }

    // idの項目の添字。見つからなければ0(先頭)
    int FocusIndexOf(int id) const
    {
        for (size_t i = 0; i < focusables.size(); ++i) {
            if (focusables[i].id == id) return (int)i;
        }
        return 0;
    }

    void UpdateFocusHighlight()
    {
        for (size_t i = 0; i < focusables.size(); ++i) {
            auto& f = focusables[i];
            if ((int)i == focus_index) {
                f.view.layer.borderWidth = 2.0;
                f.view.layer.cornerRadius = 4.0;
                f.view.layer.borderColor = [NSColor keyboardFocusIndicatorColor].CGColor;
                // 選択リストの行は、枠に加えて薄く塗る(リストの他の行と区別がつくように。文字は反転しないので、濃くしない)
                if ([f.view isKindOfClass:[_MiataSelectRow class]]) {
                    f.view.layer.backgroundColor = [[NSColor selectedContentBackgroundColor] colorWithAlphaComponent:kCursorFillAlpha].CGColor;
                }
            } else {
                f.view.layer.borderWidth = 0.0;
                if ([f.view isKindOfClass:[_MiataSelectRow class]]) f.view.layer.backgroundColor = nil;
            }
        }
    }

    void NavigateDir(int dir) // 0=Up,1=Down,2=Left,3=Right (flippedなのでUp=y減少)
    {
        if (focusables.empty()) return;
        if (focus_index < 0) {
            focus_index = 0;
            UpdateFocusHighlight();
            return;
        }

        auto cur = FrameOf(focusables[(size_t)focus_index].view);
        CGFloat cur_cx = cur.x + cur.w / 2;
        CGFloat cur_cy = cur.y + cur.h / 2;

        Rect hitbox = cur;
        const CGFloat dist = 10;
        switch (dir) {
        case 0: hitbox.y -= cur.h * dist; hitbox.h += cur.h * (dist - 1); break;
        case 1: hitbox.y += cur.h;        hitbox.h += cur.h * (dist - 1); break;
        case 2: hitbox.x -= cur.w * dist; hitbox.w += cur.w * (dist - 1); break;
        case 3: hitbox.x += cur.w;        hitbox.w += cur.w * (dist - 1); break;
        }

        CGFloat min_dist = CGFLOAT_MAX;
        int next = -1;
        for (size_t i = 0; i < focusables.size(); ++i) {
            if ((int)i == focus_index) continue;
            auto r = FrameOf(focusables[i].view);
            if (Overlaps(hitbox, r)) {
                CGFloat cx = r.x + r.w / 2, cy = r.y + r.h / 2;
                CGFloat dx = cx - cur_cx, dy = cy - cur_cy;
                CGFloat d = dx * dx + dy * dy;
                if (d < min_dist) { min_dist = d; next = (int)i; }
            }
        }
        if (next >= 0) focus_index = next;
        UpdateFocusHighlight();
    }

    void LayoutButtonRow()
    {
        if (buttons.count == 0) return;
        CGFloat total_width = 0;
        for (NSButton* b in buttons) {
            [b sizeToFit];
            NSRect f = b.frame;
            f.size.width = std::max((CGFloat)80.0, f.size.width + 20);
            b.frame = f;
            total_width += f.size.width;
        }
        total_width += kButtonSpacing * (CGFloat)(buttons.count - 1);
        CGFloat x = panel_width - kPadding - total_width;
        for (NSButton* b in buttons) {
            NSRect f = b.frame;
            f.origin.x = x;
            f.origin.y = cursor_y;
            b.frame = f;
            x += f.size.width + kButtonSpacing;
        }
        cursor_y += kButtonHeight + kPadding;
    }
};

DialogPanel::DialogPanel() : impl_(std::make_unique<Impl>()) {}
DialogPanel::~DialogPanel() { Hide(); }

void DialogPanel::Show(const std::string& title, const std::string& message, Size2D ideal_size)
{
    impl_->panel_width = ideal_size.width > 0 ? ideal_size.width : kPanelWidth;
    impl_->EnsureBackground();
    if (!title.empty()) {
        impl_->MakeLabel(title, true);
    }
    if (!message.empty()) {
        impl_->MakeLabel(message, false);
    }
}

void DialogPanel::Hide()
{
    if (impl_->background) {
        NSWindow* window = impl_->background.window;
        [impl_->background removeFromSuperview];
        impl_->background = nil;
        // テキストフィールド等firstResponderだったビューが消えると、
        // ウィンドウのfirstResponderがnilになりキー入力がNSBeepされてしまうため、
        // メインビューへ明示的に戻す。
        [window makeFirstResponder:window.contentView];
    }
    impl_->focusables.clear();
    impl_->buttons = nil;
    impl_->targets = nil;
    impl_->text_field = nil;
    impl_->focus_index = -1;
    impl_->initial_focus_id = -1;
    impl_->cursor_y = 0;
}

int DialogPanel::AddButton(const std::string& label, bool is_default)
{
    impl_->EnsureBackground();
    NSButton* btn = [NSButton buttonWithTitle:@(label.c_str()) target:nil action:nil];
    btn.bezelStyle = NSBezelStyleRounded;
    if (is_default) btn.keyEquivalent = @"\r";

    int btn_id = impl_->next_id++;
    _MiataControlTarget* target = MakeTarget(this, btn_id);
    btn.target = target;
    btn.action = @selector(fire:);
    [impl_->targets addObject:target];

    [impl_->background addSubview:btn];
    [impl_->buttons addObject:btn];
    impl_->RegisterFocusable(btn, btn_id);
    return btn_id;
}

int DialogPanel::AddCheckbox(const std::string& label, bool initial)
{
    impl_->EnsureBackground();
    NSButton* cb = [NSButton checkboxWithTitle:@(label.c_str()) target:nil action:nil];
    cb.state = initial ? NSControlStateValueOn : NSControlStateValueOff;
    CGFloat avail = impl_->panel_width - kPadding * 2;
    cb.frame = NSMakeRect(kPadding, impl_->cursor_y, avail, kRowHeight);
    [impl_->background addSubview:cb];
    impl_->cursor_y += kRowHeight + kRowSpacing;

    int id = impl_->next_id++;
    impl_->RegisterFocusable(cb, id);
    return id;
}

std::vector<int> DialogPanel::AddSelectList(const std::vector<std::string>& options)
{
    impl_->EnsureBackground();
    const CGFloat avail = impl_->panel_width - kPadding * 2;
    const CGFloat box_height = kSelectRowHeight * (CGFloat)options.size() + kSelectBoxInset * 2;

    // リスト全体を囲む細い箱(飾り)。これが無いと、チェックボックスの下の選択肢が地の文に見えて、選べる行だと分からない。
    // focusables には入れない。行はこの箱の子にせず、background の直接の子にする(NavigateDir が、全 focusable の frame を
    // background の座標系のまま比べるため)
    NSView* box = [[NSView alloc] initWithFrame:NSMakeRect(kPadding, impl_->cursor_y, avail, box_height)];
    box.wantsLayer = YES;
    box.layer.borderWidth = 1.0;
    box.layer.cornerRadius = 6.0;
    box.layer.borderColor = [NSColor separatorColor].CGColor;
    [impl_->background addSubview:box];

    std::vector<int> ids;
    ids.reserve(options.size());
    for (size_t i = 0; i < options.size(); ++i) {
        const int row_id = impl_->next_id++;
        _MiataSelectRow* row = [[_MiataSelectRow alloc] initWithFrame:NSMakeRect(
            kPadding + kSelectBoxInset, impl_->cursor_y + kSelectBoxInset + kSelectRowHeight * (CGFloat)i,
            avail - kSelectBoxInset * 2, kSelectRowHeight)];
        row.rowTitle = @(options[i].c_str()); // UTF-8 として正しい前提(Lua からの文字列は Application.cc が修復する)
        row.activator = MakeTarget(this, row_id);
        [impl_->background addSubview:row];
        impl_->RegisterFocusable(row, row_id);
        ids.push_back(row_id);
    }
    impl_->cursor_y += box_height + kRowSpacing;
    return ids;
}

void DialogPanel::SetInitialFocus(int id)
{
    impl_->initial_focus_id = id;
}

void DialogPanel::AddTextField(const std::string& initial)
{
    impl_->EnsureBackground();
    CGFloat avail = impl_->panel_width - kPadding * 2;
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(kPadding, impl_->cursor_y, avail, kRowHeight)];
    field.stringValue = @(initial.c_str());
    [impl_->background addSubview:field];
    impl_->cursor_y += kRowHeight + kRowSpacing;
    impl_->text_field = field;
}

void DialogPanel::SetTextFieldDelegate(void* delegate)
{
    if (!impl_->text_field) return;
    impl_->text_field.delegate = (__bridge id<NSTextFieldDelegate>)delegate;
}

void DialogPanel::AddCustomView(void* native_view, float height)
{
    impl_->EnsureBackground();
    NSView* view = (__bridge NSView*)native_view;
    CGFloat avail = impl_->panel_width - kPadding * 2;
    view.frame = NSMakeRect(kPadding, impl_->cursor_y, avail, (CGFloat)height);
    [impl_->background addSubview:view];
    impl_->cursor_y += (CGFloat)height + kRowSpacing;
}

bool DialogPanel::GetCheckbox(int id) const
{
    for (auto& f : impl_->focusables) {
        if (f.id == id) {
            NSButton* b = (NSButton*)f.view;
            return b.state == NSControlStateValueOn;
        }
    }
    return false;
}

std::string DialogPanel::GetTextFieldValue() const
{
    if (!impl_->text_field) return "";
    [impl_->text_field validateEditing];
    return std::string(impl_->text_field.stringValue.UTF8String);
}

void DialogPanel::NavigateUp() { impl_->NavigateDir(0); }
void DialogPanel::NavigateDown() { impl_->NavigateDir(1); }
void DialogPanel::NavigateLeft() { impl_->NavigateDir(2); }
void DialogPanel::NavigateRight() { impl_->NavigateDir(3); }

// フォーカス中の NSControl には performClick: で作用する(ボタン=押下、チェックボックス=トグル、選択リストの行=確定)。
// ここで同期的に閉じてはいけない(Hide() が focusables を clear するので、下の参照 f が壊れる)。
// 通知は _MiataControlTarget::fire: が次のランループへ逃がす
void DialogPanel::NavigateOk()
{
    if (impl_->focus_index < 0 || impl_->focus_index >= (int)impl_->focusables.size()) return;
    auto& f = impl_->focusables[(size_t)impl_->focus_index];
    if ([f.view isKindOfClass:[NSControl class]]) {
        [(NSControl*)f.view performClick:nil];
    }
}

void DialogPanel::Layout()
{
    impl_->LayoutButtonRow();

    CGFloat total_height = impl_->cursor_y + kPadding;
    NSRect bg_frame = impl_->background.frame;
    bg_frame.size.height = total_height;
    impl_->background.frame = bg_frame;

    NSView* content = (__bridge NSView*)pl_get_content_view();
    NSRect content_bounds = content.bounds;
    NSRect final_frame = bg_frame;
    final_frame.origin.x = (content_bounds.size.width - bg_frame.size.width) / 2;
    final_frame.origin.y = (content_bounds.size.height - bg_frame.size.height) / 2;
    impl_->background.frame = final_frame;
    [content addSubview:impl_->background];

    if (impl_->text_field) {
        [content.window makeFirstResponder:impl_->text_field];
    } else if (!impl_->focusables.empty()) {
        // 指定が無ければ(initial_focus_id が -1)先頭
        impl_->focus_index = impl_->FocusIndexOf(impl_->initial_focus_id);
        impl_->UpdateFocusHighlight();
    }
}

// ---- IDialog ----

IDialog::IDialog(const char* id, std::function<void(IDialog&)> on_close) : id_(id), on_close_(std::move(on_close))
{
    panel_.on_button_ = [this](int button_id) {
        // 閉じた後に届いた通知は捨てる。fire: の dispatch_async のブロックは Hide() で取り消されないので、Enter の連打などで
        // 2発目が届くと、Hide() で消えた入力欄・チェックボックスの値(空・false)で、OnButton が結果を上書きしてしまう
        if (!is_opened_) return;
        OnButton(button_id);
    };
}

IDialog::~IDialog() = default;

void IDialog::Open()
{
    is_opened_ = true;
    OnOpen();
    panel_.Layout();
}

void IDialog::CloseDialog()
{
    if (!is_opened_) return;
    is_opened_ = false;
    panel_.Hide();
    OnClose();
    if (on_close_) on_close_(*this);
}

void IDialog::Navigate(constants::Navigate dir)
{
    switch (dir) {
    case constants::Navigate::Up: panel_.NavigateUp(); break;
    case constants::Navigate::Down: panel_.NavigateDown(); break;
    case constants::Navigate::Left: panel_.NavigateLeft(); break;
    case constants::Navigate::Right: panel_.NavigateRight(); break;
    case constants::Navigate::Ok: panel_.NavigateOk(); break;
    case constants::Navigate::Cancel: OnCancel(); break;
    }
}

// ---- ConfirmDialog ----

ConfirmDialog::ConfirmDialog(std::function<void(IDialog&)> on_close, const arguments& args)
    : IDialog(typeid(this).name(), on_close), args_(args)
{
}
void ConfirmDialog::OnOpen()
{
    panel_.Show("", args_.message_, GetIdealSize());
    ok_id_ = panel_.AddButton(args_.button_text_, true);
}
void ConfirmDialog::OnButton(int button_id)
{
    if (button_id == ok_id_) CloseDialog();
}

// ---- YesNoDialog ----

YesNoDialog::YesNoDialog(std::function<void(IDialog&)> on_close, arguments args)
    : IDialog(typeid(this).name(), on_close), args_(std::move(args))
{
}
void YesNoDialog::OnOpen()
{
    panel_.Show("", args_.message_, GetIdealSize());
    no_id_ = panel_.AddButton(args_.no_text_, !args_.default_select_);
    yes_id_ = panel_.AddButton(args_.yes_text_, args_.default_select_);
    // 最初のカーソルは、「既定」の強調を付けたボタンにする。Enter は(強調ではなく)カーソルの項目に作用するので、
    // 先頭に登録した「いいえ」のままだと、強調された「はい」と食い違って、Enter が「いいえ」を押してしまう
    panel_.SetInitialFocus(args_.default_select_ ? yes_id_ : no_id_);
}
void YesNoDialog::OnButton(int button_id)
{
    if (button_id == yes_id_) { result_ = true; CloseDialog(); }
    else if (button_id == no_id_) { result_ = false; CloseDialog(); }
}
void YesNoDialog::OnCancel()
{
    result_ = false;
    CloseDialog();
}

// ---- InputTextDialog ----

InputTextDialog::InputTextDialog(std::function<void(IDialog&)> on_close, const arguments& args)
    : IDialog(typeid(this).name(), on_close), args_(args)
{
}
void InputTextDialog::OnOpen()
{
    panel_.Show("", args_.message_, GetIdealSize());
    panel_.AddTextField(args_.initial_text_);
    ok_id_ = panel_.AddButton("OK", true);
}
void InputTextDialog::OnButton(int button_id)
{
    if (button_id == ok_id_) {
        result_ = panel_.GetTextFieldValue();
        CloseDialog();
    }
}
void InputTextDialog::OnCancel()
{
    result_ = std::nullopt;
    CloseDialog();
}

// ---- CustomDialog ----

CustomDialog::CustomDialog(std::function<void(IDialog&)> on_close, const CustomDialogSpec& spec)
    : IDialog(typeid(this).name(), on_close), spec_(spec)
{
}
void CustomDialog::OnOpen()
{
    panel_.Show(spec_.title, spec_.message, GetIdealSize());
    for (auto& cb : spec_.checkboxes) {
        checkbox_ids_.push_back(panel_.AddCheckbox(cb.label, cb.checked));
    }
    const bool has_list = spec_.select.has_value();
    if (has_list) {
        select_ids_ = panel_.AddSelectList(spec_.select->options);
        // options は1件以上、selected は範囲内(Application.cc が Lua からの入力を検証してから渡す)
        panel_.SetInitialFocus(select_ids_[(size_t)spec_.select->selected]);
    }
    // buttons を省略したとき: 選択リストがあれば、閉じるのは行のEnterかEscだけ。リストが無ければ、OKを1つ置く
    auto buttons = spec_.buttons;
    if (buttons.empty() && !has_list) buttons = {"OK"};
    for (size_t i = 0; i < buttons.size(); ++i) {
        // 先頭ボタン(通常OK)の「既定」の強調は、リストが無いときだけ。Enterはカーソルの項目に作用するので(既定のボタンの
        // keyEquivalent は効かない)、リストがあるときの強調されたOKは「EnterでOK」と誤解させる
        button_ids_.push_back(panel_.AddButton(buttons[i], !has_list && i == 0));
    }
}
void CustomDialog::OnButton(int button_id)
{
    CustomDialogResult r;
    if (auto row = std::find(select_ids_.begin(), select_ids_.end(), button_id); row != select_ids_.end()) {
        r.select_index = (int)std::distance(select_ids_.begin(), row);
    }
    else if (auto it = std::find(button_ids_.begin(), button_ids_.end(), button_id); it != button_ids_.end()) {
        r.button_index = (int)std::distance(button_ids_.begin(), it);
    }
    else {
        return;
    }
    for (auto id : checkbox_ids_) r.checkboxes.push_back(panel_.GetCheckbox(id)); // CloseDialog() の前に読む(Hide() の後は false しか返らない)
    result_ = r;
    CloseDialog();
}

}
