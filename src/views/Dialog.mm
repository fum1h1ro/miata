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

namespace miata::views {

namespace {
    constexpr CGFloat kPanelWidth = 400;
    constexpr CGFloat kPadding = 16;
    constexpr CGFloat kRowHeight = 24;
    constexpr CGFloat kRowSpacing = 8;
    constexpr CGFloat kButtonHeight = 28;
    constexpr CGFloat kButtonSpacing = 8;

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

    void UpdateFocusHighlight()
    {
        for (size_t i = 0; i < focusables.size(); ++i) {
            auto& f = focusables[i];
            if ((int)i == focus_index) {
                f.view.layer.borderWidth = 2.0;
                f.view.layer.cornerRadius = 4.0;
                f.view.layer.borderColor = [NSColor keyboardFocusIndicatorColor].CGColor;
            } else {
                f.view.layer.borderWidth = 0.0;
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
        [impl_->background removeFromSuperview];
        impl_->background = nil;
    }
    impl_->focusables.clear();
    impl_->buttons = nil;
    impl_->targets = nil;
    impl_->text_field = nil;
    impl_->focus_index = -1;
    impl_->cursor_y = 0;
}

int DialogPanel::AddButton(const std::string& label, bool is_default)
{
    impl_->EnsureBackground();
    NSButton* btn = [NSButton buttonWithTitle:@(label.c_str()) target:nil action:nil];
    btn.bezelStyle = NSBezelStyleRounded;
    if (is_default) btn.keyEquivalent = @"\r";

    int btn_id = impl_->next_id++;
    _MiataControlTarget* target = [[_MiataControlTarget alloc] init];
    DialogPanel* self_ptr = this;
    target.action = ^(id sender) {
        if (self_ptr->on_button_) self_ptr->on_button_(btn_id);
    };
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

int DialogPanel::AddPopup(const std::string& label, const std::vector<std::string>& options, int initial)
{
    impl_->EnsureBackground();
    CGFloat avail = impl_->panel_width - kPadding * 2;
    CGFloat label_width = label.empty() ? 0 : avail * 0.4f;

    if (!label.empty()) {
        NSTextField* lbl = [NSTextField labelWithString:@(label.c_str())];
        lbl.alignment = NSTextAlignmentRight;
        lbl.frame = NSMakeRect(kPadding, impl_->cursor_y + 3, label_width - 8, kRowHeight - 4);
        [impl_->background addSubview:lbl];
    }

    NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(kPadding + label_width, impl_->cursor_y, avail - label_width, kRowHeight) pullsDown:NO];
    for (auto& opt : options) {
        [popup addItemWithTitle:@(opt.c_str())];
    }
    if (initial >= 0 && initial < (int)options.size()) {
        [popup selectItemAtIndex:initial];
    }
    [impl_->background addSubview:popup];
    impl_->cursor_y += kRowHeight + kRowSpacing;

    int id = impl_->next_id++;
    impl_->RegisterFocusable(popup, id);
    return id;
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

int DialogPanel::GetPopupSelection(int id) const
{
    for (auto& f : impl_->focusables) {
        if (f.id == id) {
            NSPopUpButton* p = (NSPopUpButton*)f.view;
            return (int)p.indexOfSelectedItem;
        }
    }
    return -1;
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
        impl_->focus_index = 0;
        impl_->UpdateFocusHighlight();
    }
}

// ---- IDialog ----

IDialog::IDialog(const char* id, std::function<void(IDialog&)> on_close) : id_(id), on_close_(std::move(on_close))
{
    panel_.on_button_ = [this](int button_id) {
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
    for (auto& sel : spec_.selects) {
        select_ids_.push_back(panel_.AddPopup(sel.label, sel.options, sel.selected));
    }
    auto buttons = spec_.buttons.empty() ? std::vector<std::string>{"OK"} : spec_.buttons;
    for (size_t i = 0; i < buttons.size(); ++i) {
        button_ids_.push_back(panel_.AddButton(buttons[i], i == 0)); // 先頭ボタン(通常OK)をEnterのデフォルトにする
    }
}
void CustomDialog::OnButton(int button_id)
{
    auto it = std::find(button_ids_.begin(), button_ids_.end(), button_id);
    if (it == button_ids_.end()) return;
    int index = (int)std::distance(button_ids_.begin(), it);

    CustomDialogResult r;
    r.button_index = index;
    for (auto id : checkbox_ids_) r.checkboxes.push_back(panel_.GetCheckbox(id));
    for (auto id : select_ids_) r.selects.push_back(panel_.GetPopupSelection(id));
    result_ = r;
    CloseDialog();
}

}
