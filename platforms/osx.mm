#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <Foundation/Foundation.h>
#import <Quartz/Quartz.h>
#include <sokol_app.h>
#include "../src/platform.h"
#include <fstream>
#include <format>

// NSView の座標系はデフォルトで下原点なので、上から順に配置するために flipped にする
@interface _MiataDialogView : NSView
@end
@implementation _MiataDialogView
- (BOOL)isFlipped { return YES; }
@end

@interface ImeInputHandler : NSView<NSTextInputClient>
@end

@implementation ImeInputHandler {
    NSString* _markedText;
    BOOL _insertedText;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    _markedText = @"";
    _insertedText = NO;
    return self;
}

- (void)clearInsertedText { _insertedText = NO; }
- (BOOL)didInsertText { return _insertedText; }

- (void)insertText:(id)aString replacementRange:(NSRange)replacementRange {
    NSString* str = [aString isKindOfClass:[NSAttributedString class]]
        ? [(NSAttributedString*)aString string] : (NSString*)aString;
    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharactersUTF8(str.UTF8String);
    _insertedText = YES;
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange {
    _markedText = [string isKindOfClass:[NSAttributedString class]]
        ? [(NSAttributedString*)string string] : (NSString*)string;
}
- (void)unmarkText { _markedText = @""; }
- (BOOL)hasMarkedText { return _markedText.length > 0; }
- (NSRange)markedRange { return _markedText.length > 0 ? NSMakeRange(0, _markedText.length) : NSMakeRange(NSNotFound, 0); }
- (NSRange)selectedRange { return NSMakeRange(0, 0); }
- (nullable NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange { return nil; }
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange { return NSZeroRect; }
- (NSUInteger)characterIndexForPoint:(NSPoint)point { return 0; }
- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText { return @[]; }
- (void)doCommandBySelector:(SEL)selector {}

@end

static ImeInputHandler* g_ime_handler = nil;
static NSTextInputContext* g_text_input_context = nil;

static NSWindow* ns_window()
{
    return (__bridge NSWindow*)sapp_macos_get_window();
}

static NSView* ns_content_view()
{
    return ns_window().contentView;
}

std::string pl_normalize_string(const std::string& input)
{
    @autoreleasepool {
        NSString* nsInput = [[NSString alloc] initWithBytes:input.data() length:input.size() encoding:NSUTF8StringEncoding];
        NSString* normalized;
        normalized = [nsInput precomposedStringWithCanonicalMapping]; // NFC
        return std::string([normalized UTF8String]);
    }
}

void pl_play_beep()
{
    @autoreleasepool {
        NSBeep();
    }
}

void pl_app_post_initialize()
{
    NSMenuItem* app_menu_item = [NSApp.mainMenu itemAtIndex:0];
    NSMenu* app_menu = [[NSMenu alloc] init];
    NSString* quit_title = @"Quit";
    NSMenuItem* quit_item = [[NSMenuItem alloc] initWithTitle:quit_title action:@selector(terminate:) keyEquivalent:@"q"];
    [app_menu addItem:quit_item];
    app_menu_item.submenu = app_menu;

    g_ime_handler = [[ImeInputHandler alloc] initWithFrame:NSZeroRect];
    [ns_content_view() addSubview:g_ime_handler];
    g_text_input_context = [[NSTextInputContext alloc] initWithClient:g_ime_handler];

    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                         handler:^NSEvent* _Nullable(NSEvent* event) {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput) {
            BOOL hadMarkedText = [g_ime_handler hasMarkedText];
            [g_ime_handler clearInsertedText];
            // activate により、システムIMEがこのコンテキストを通じて変換処理を行えるようにする
            [g_text_input_context activate];
            BOOL handled = [g_text_input_context handleEvent:event];
            BOOL nowHasMarkedText = [g_ime_handler hasMarkedText];
            // IMEがイベントを処理した、文字が確定した、または変換中ならキャンセル
            if (handled || [g_ime_handler didInsertText] || hadMarkedText || nowHasMarkedText) {
                return nil;
            }
        }
        return event;
    }];
}

void* pl_get_content_view()
{
    return (__bridge void*)ns_content_view();
}

void pl_update_ime(bool want_text_input)
{
    // WantTextInput が false になったら IME コンテキストを deactivate する
    // （候補ウィンドウを閉じ、変換中状態をキャンセルする）
    static bool prev_want_text_input = false;
    if (!want_text_input && prev_want_text_input) {
        [g_text_input_context deactivate];
    }
    prev_want_text_input = want_text_input;
}

void pl_set_fps(int fps)
{
    // sokol now uses CADisplayLink internally (no longer MTKView),
    // and defaults to the screen's max refresh rate.
    (void)fps;
}

float pl_get_default_fps()
{
    NSScreen* mainScreen = [NSScreen mainScreen];
    NSDictionary* deviceDescription = [mainScreen deviceDescription];
    NSNumber* screenNumber = [deviceDescription objectForKey:@"NSScreenNumber"];
    CGDirectDisplayID displayID = [screenNumber unsignedIntValue];
    CGDisplayModeRef displayMode = CGDisplayCopyDisplayMode(displayID);
    double refreshRate = CGDisplayModeGetRefreshRate(displayMode);
    CGDisplayModeRelease(displayMode);
    if (refreshRate == 0.0) {
        refreshRate = 60.0;
    }
    return (float)refreshRate;
}

void pl_start_update()
{
    // sokol's CADisplayLink runs continuously; pause/resume not exposed externally.
}

void pl_stop_update()
{
    // sokol's CADisplayLink runs continuously; pause/resume not exposed externally.
}

void pl_force_update()
{
    // sokol's CADisplayLink handles display scheduling internally.
}

std::filesystem::path pl_find_font_filename(const std::string& font_name)
{
    @autoreleasepool {
        NSString* font_name_ns = [NSString stringWithUTF8String:font_name.c_str()];
        CTFontRef fontRef = CTFontCreateWithName((CFStringRef)font_name_ns, 0.0, NULL);
        std::string result;
        if (fontRef) {
            CFURLRef urlRef = (CFURLRef)CTFontCopyAttribute(fontRef, kCTFontURLAttribute);
            if (urlRef) {
                NSString* fontPath = [(__bridge NSURL*)urlRef path];
                NSLog(@"font path: %@", fontPath);
                CFRelease(urlRef);
                result = [fontPath UTF8String];
            }
            CFRelease(fontRef);
        }
        return result;
    }
}

std::filesystem::path pl_get_home_dir()
{
    return [NSHomeDirectory() UTF8String];
}

std::filesystem::path pl_get_config_dir()
{
    @autoreleasepool {
        // 環境変数からXDG_CONFIG_HOMEを取得
        NSString* xdgConfigHome = [[[NSProcessInfo processInfo] environment] objectForKey:@"XDG_CONFIG_HOME"];

        // 環境変数が設定されていない場合はデフォルト値を使用
        if (!xdgConfigHome)
        {
            NSString* homeDir = NSHomeDirectory();
            xdgConfigHome = [homeDir stringByAppendingPathComponent:@".config"];
        }

        return [xdgConfigHome UTF8String];
    }
}

std::expected<void, std::string> pl_trash_file(const std::filesystem::path& path)
{
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        NSError* error = nil;
        BOOL ok = [[NSFileManager defaultManager] trashItemAtURL:url resultingItemURL:nil error:&error];
        if (!ok) {
            return std::unexpected(std::string(error.localizedDescription.UTF8String));
        }
        return {};
    }
}

std::string pl_read_file(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    file.unsetf(std::ios::skipws);
    file.seekg(0, std::ios::end);
    auto file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string data;
#if 1
    data.resize_and_overwrite((size_t)file_size + 1, [&](char* ptr, size_t sz)
    {
        file.read(ptr, file_size);
        ptr[file_size] = '\0';
        return (size_t)file_size;
    });
    return data;
#else
    data.resize((size_t)file_size);

    if (file.read((char*)&data[0], file_size)) {
        return data;
    }
#endif
    return std::string();
}

std::expected<std::string, std::string> pl_read_resource_file(const char* path)
{
    @autoreleasepool {
        NSString* res_path = [[NSBundle mainBundle] pathForResource:[NSString stringWithUTF8String:path] ofType:nil];
        if (!res_path) {
            return std::unexpected(std::format("{}: file not found: {}", __FUNCTION__, path));
        }
        return pl_read_file(res_path.UTF8String);
    }
}
void pl_osx_set_visual_effect_view()
{
    NSVisualEffectView* visualEffectView = [[NSVisualEffectView alloc] initWithFrame:ns_window().contentView.bounds];
    visualEffectView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    visualEffectView.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    visualEffectView.material = NSVisualEffectMaterialHUDWindow;
    visualEffectView.state = NSVisualEffectStateActive;
    auto mtkview = ns_content_view();
    ns_window().contentView = visualEffectView;
    [visualEffectView addSubview:mtkview positioned:NSWindowBelow relativeTo:nil];
    ns_window().opaque = NO;
    ns_window().backgroundColor = [NSColor clearColor];
    mtkview.layer.opaque = NO;
    mtkview.layer.backgroundColor = [NSColor clearColor].CGColor;
    mtkview.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
}

ImVec4 pl_get_color(pl_color_type type)
{
    @autoreleasepool {
        //NSAppearance* appearance = [NSAppearance currentDrawingAppearance];
        //NSLog(@"appearance: %@", appearance.name);
        NSScreen* screen = [NSScreen mainScreen];
        switch (type) {
        case pl_color_type::label_color:
            {
                auto c = [[NSColor labelColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::secondary_label_color:
            {
                auto c = [[NSColor secondaryLabelColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::tertiary_label_color:
            {
                auto c = [[NSColor tertiaryLabelColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::quaternary_label_color:
            {
                auto c = [[NSColor quaternaryLabelColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::text_color:
            {
                auto c = [[NSColor textColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::placeholder_text_color:
            {
                auto c = [[NSColor placeholderTextColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::selected_text_color:
            {
                auto c = [[NSColor selectedTextColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::text_background_color:
            {
                auto c = [[NSColor textBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::selected_text_background_color:
            {
                auto c = [[NSColor selectedTextBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::keyboard_focus_indicator_color:
            {
                auto c = [[NSColor keyboardFocusIndicatorColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::unemphasized_selected_text_color:
            {
                auto c = [[NSColor unemphasizedSelectedTextColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::unemphasized_selected_text_background_color:
            {
                auto c = [[NSColor unemphasizedSelectedTextBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::link_color:
            {
                auto c = [[NSColor linkColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::separator_color:
            {
                auto c = [[NSColor separatorColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::selected_content_background_color:
            {
                auto c = [[NSColor selectedContentBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::unemphasized_selected_content_background_color:
            {
                auto c = [[NSColor unemphasizedSelectedContentBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::selected_menu_item_text_color:
            {
                auto c = [[NSColor selectedMenuItemTextColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        //case pl_color_type::selected_menu_item_color:
        //    {
        //        auto c = [[NSColor selectedMenuItemColor] colorUsingColorSpace:screen.colorSpace];
        //        return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
        //    }
        case pl_color_type::header_text_color:
            {
                auto c = [[NSColor headerTextColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        //case pl_color_type::alternating_content_background_colors:
        //    {
        //        auto c = [[NSColor alternatingContentBackgroundColors] colorUsingColorSpace:screen.colorSpace];
        //        return ImVec4((float)c[0].redComponent, (float)c[0].greenComponent, (float)c[0].blueComponent, (float)c[0].alphaComponent);
        //    }
        case pl_color_type::control_accent_color:
            {
                auto c = [[NSColor controlAccentColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::control_color:
            {
                auto c = [[NSColor controlColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::control_background_color:
            {
                auto c = [[NSColor controlBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::control_text_color:
            {
                auto c = [[NSColor controlTextColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::disabled_control_text_color:
            {
                auto c = [[NSColor disabledControlTextColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        //case pl_color_type::current_control_tint:
        //    {
        //        auto c = [[NSColor currentControlTint] colorUsingColorSpace:screen.colorSpace];
        //        return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
        //    }
        case pl_color_type::selected_control_color:
            {
                auto c = [[NSColor selectedControlColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        //case pl_color_type::secondary_selected_control_color:
        //    {
        //        auto c = [[NSColor secondarySelectedControlColor] colorUsingColorSpace:screen.colorSpace];
        //        return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
        //    }
        //case pl_color_type::alternate_selected_control_color:
        //    {
        //        auto c = [[NSColor alternateSelectedControlColor] colorUsingColorSpace:screen.colorSpace];
        //        return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
        //    }
        case pl_color_type::selected_control_text_color:
            {
                auto c = [[NSColor selectedControlTextColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::alternate_selected_control_text_color:
            {
                auto c = [[NSColor alternateSelectedControlTextColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::scrubber_textured_background_color:
            {
                auto c = [[NSColor scrubberTexturedBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::window_background_color:
            {
                auto c = [[NSColor windowBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::window_frame_text_color:
            {
                auto c = [[NSColor windowFrameTextColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        //case pl_color_type::window_frame_color:
        //    {
        //        auto c = [[NSColor windowFrameColor] colorUsingColorSpace:screen.colorSpace];
        //        return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
        //    }
        case pl_color_type::under_page_background_color:
            {
                auto c = [[NSColor underPageBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::find_highlight_color:
            {
                auto c = [[NSColor findHighlightColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::highlight_color:
            {
                auto c = [[NSColor highlightColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::shadow_color:
            {
                auto c = [[NSColor shadowColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::quaternary_system_fill_color:
            {
                auto c = [[NSColor quaternarySystemFillColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::quinary_label_color:
            {
                auto c = [[NSColor quinaryLabelColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::quinary_system_fill_color:
            {
                auto c = [[NSColor quinarySystemFillColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::secondary_system_fill_color:
            {
                auto c = [[NSColor secondarySystemFillColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::system_fill_color:
            {
                auto c = [[NSColor systemFillColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::tertiary_system_fill_color:
            {
                auto c = [[NSColor tertiarySystemFillColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        case pl_color_type::text_insertion_point_color:
            {
                auto c = [[NSColor textInsertionPointColor] colorUsingColorSpace:screen.colorSpace];
                return ImVec4((float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent);
            }
        default:
            return ImVec4(1, 0, 1, 1); // Unity Purple
        }
    }
}




@interface QLPreviewPanelController : NSViewController <QLPreviewPanelDataSource, QLPreviewPanelDelegate>
@property (nonatomic, strong) NSArray* previewFilePathArray;
@end

@implementation QLPreviewPanelController
- (void)showPreviewForFileList:(NSArray*)filePathArray
{
    self.previewFilePathArray = filePathArray;
    QLPreviewPanel* panel = [QLPreviewPanel sharedPreviewPanel];
    [panel setDataSource:self];
    [panel setDelegate:self];
    [panel makeKeyAndOrderFront:nil];
}
- (NSInteger)numberOfPreviewItemsInPreviewPanel:(QLPreviewPanel*)panel
{
    return (NSInteger)self.previewFilePathArray.count;
}
- (id<QLPreviewItem>)previewPanel:(QLPreviewPanel*)panel previewItemAtIndex:(NSInteger)index
{
    return self.previewFilePathArray[(NSUInteger)index];
}
- (BOOL)acceptsPreviewPanelControl:(QLPreviewPanel*)panel
{
    return YES;
}
- (void)beginPreviewPanelControl:(QLPreviewPanel*)panel
{
    panel.delegate = self;
    panel.dataSource = self;
}
- (void)endPreviewPanelControl:(QLPreviewPanel*)panel
{
}
@end

void pl_quick_preview(const std::vector<std::string>& path_list)
{
    @autoreleasepool {
        NSMutableArray* items = [NSMutableArray arrayWithCapacity:path_list.size()];
        for (const auto& path : path_list) {
            [items addObject:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]]];
        }
        QLPreviewPanelController* controller = [[QLPreviewPanelController alloc] init];
        [controller showPreviewForFileList:items];
    }
}




