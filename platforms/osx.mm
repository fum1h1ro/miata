#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <Quartz/Quartz.h>
#import <CoreServices/CoreServices.h>
#include "../src/platform.h"
#include <unistd.h>
#include <fstream>
#include <format>
#include <functional>

static uint16_t ConvertModifierFlags(NSEventModifierFlags flags)
{
    uint16_t mods = 0;
    if (flags & NSEventModifierFlagShift) mods |= pl_modifier::Shift;
    if (flags & NSEventModifierFlagControl) mods |= pl_modifier::Ctrl;
    if (flags & NSEventModifierFlagOption) mods |= pl_modifier::Alt;
    if (flags & NSEventModifierFlagCommand) mods |= pl_modifier::Super;
    return mods;
}

// メインウィンドウのcontentView。キーイベントをApplication側のハンドラへ橋渡しする。
// テキストフィールド編集中はそちらがfirstResponderになるため、このビューにkeyDown:は来ず、
// IMEは常にAppKit標準の経路で処理される。
@interface MiataRootView : NSView
@property (nonatomic, assign) std::function<void(uint16_t, uint16_t)>* keyDownHandler;
@property (nonatomic, assign) std::function<void(uint16_t, uint16_t)>* keyUpHandler;
@end
@implementation MiataRootView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return YES; }
- (void)keyDown:(NSEvent*)event
{
    if (self.keyDownHandler && *self.keyDownHandler) (*self.keyDownHandler)(event.keyCode, ConvertModifierFlags(event.modifierFlags));
}
- (void)keyUp:(NSEvent*)event
{
    if (self.keyUpHandler && *self.keyUpHandler) (*self.keyUpHandler)(event.keyCode, ConvertModifierFlags(event.modifierFlags));
}
@end

@interface MiataWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) std::function<void(int, int)>* resizeHandler;
@end
@implementation MiataWindowDelegate
- (void)windowDidResize:(NSNotification*)notification
{
    NSWindow* window = notification.object;
    NSSize size = window.contentView.frame.size;
    if (self.resizeHandler && *self.resizeHandler) (*self.resizeHandler)((int)size.width, (int)size.height);
}
@end

static NSWindow* g_window = nil;
static MiataRootView* g_root_view = nil;
static MiataWindowDelegate* g_window_delegate = nil;
static std::function<void(int, int)> g_resize_handler;

static NSView* ns_content_view()
{
    return g_window.contentView;
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

void pl_create_main_window(int width, int height, const char* title)
{
    if (!NSApp.mainMenu) {
        NSMenu* main_menu = [[NSMenu alloc] init];
        NSMenuItem* app_menu_item = [[NSMenuItem alloc] init];
        [main_menu addItem:app_menu_item];
        NSApp.mainMenu = main_menu;
    }
    NSMenuItem* app_menu_item = [NSApp.mainMenu itemAtIndex:0];
    NSMenu* app_menu = [[NSMenu alloc] init];
    NSMenuItem* quit_item = [[NSMenuItem alloc] initWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
    [app_menu addItem:quit_item];
    app_menu_item.submenu = app_menu;

    NSRect frame = NSMakeRect(0, 0, (CGFloat)width, (CGFloat)height);
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    g_window = [[NSWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
    g_window.title = [NSString stringWithUTF8String:title];
    g_window.releasedWhenClosed = NO;

    g_root_view = [[MiataRootView alloc] initWithFrame:frame];
    g_window.contentView = g_root_view;

    g_window_delegate = [[MiataWindowDelegate alloc] init];
    g_window_delegate.resizeHandler = &g_resize_handler;
    g_window.delegate = g_window_delegate;

    [g_window center];
    [g_window makeKeyAndOrderFront:nil];
    [g_window makeFirstResponder:g_root_view];
    [NSApp activateIgnoringOtherApps:YES];
}

void* pl_get_content_view()
{
    return (__bridge void*)ns_content_view();
}

void pl_set_key_down_handler(std::function<void(uint16_t, uint16_t)> handler)
{
    static std::function<void(uint16_t, uint16_t)> holder;
    holder = std::move(handler);
    g_root_view.keyDownHandler = &holder;
}

void pl_set_key_up_handler(std::function<void(uint16_t, uint16_t)> handler)
{
    static std::function<void(uint16_t, uint16_t)> holder;
    holder = std::move(handler);
    g_root_view.keyUpHandler = &holder;
}

void pl_set_resize_handler(std::function<void(int, int)> handler)
{
    g_resize_handler = std::move(handler);
}

void pl_start_timer(double interval_seconds, std::function<void()> callback)
{
    // アプリ終了まで生存し続ける想定のタイマーコールバックなので、意図的に解放しない。
    auto* cb = new std::function<void()>(std::move(callback));
    [NSTimer scheduledTimerWithTimeInterval:interval_seconds repeats:YES block:^(NSTimer* timer) {
        (*cb)();
    }];
}

// pl_watch_directory の状態。FSEventsのコンテキストがretain/releaseするので、ストリームが解放される
// まで(=キューに積まれたコールバックが残っていても)生きている。
@interface MiataDirWatchState : NSObject {
@public
    std::function<void()> on_change;
    std::filesystem::path watched; // FSEventsが報告する形(実パス)にそろえた監視先
    BOOL alive;
}
@end
@implementation MiataDirWatchState
@end

namespace {
    // このイベントは、監視先ディレクトリの一覧に影響しうるか
    bool IsRelevantDirEvent(const std::filesystem::path& watched, const char* path, FSEventStreamEventFlags flags)
    {
        // 何が変わったか分からない場合(イベントの取りこぼし・監視先自身の移動/削除・マウント)は、変化ありとみなす
        constexpr FSEventStreamEventFlags kUnknown =
            kFSEventStreamEventFlagMustScanSubDirs | kFSEventStreamEventFlagUserDropped | kFSEventStreamEventFlagKernelDropped |
            kFSEventStreamEventFlagRootChanged | kFSEventStreamEventFlagMount | kFSEventStreamEventFlagUnmount;
        if (flags & kUnknown) return true;

        // FSEventsは配下すべての変更を報告する(再帰的)ので、監視先の直下の子だけを拾う
        auto rel = std::filesystem::path(path).lexically_relative(watched);
        if (rel.empty() || *rel.begin() == "..") return true; // 想定外の形で報告された。見逃すよりは変化ありとみなす
        if (rel == ".") return false;                          // 監視先自身(移動/削除はRootChangedで拾っている)
        return std::distance(rel.begin(), rel.end()) == 1;    // 直下の子か(それより深い階層は無視)
    }

    void DirWatchCallback(ConstFSEventStreamRef, void* info, size_t count, void* event_paths,
                          const FSEventStreamEventFlags flags[], const FSEventStreamEventId[])
    {
        MiataDirWatchState* state = (__bridge MiataDirWatchState*)info;
        if (!state->alive) return; // 破棄された後に、キューに残っていたコールバック
        NSArray* paths = (__bridge NSArray*)event_paths; // kFSEventStreamCreateFlagUseCFTypes
        for (size_t i = 0; i < count; ++i) {
            if (IsRelevantDirEvent(state->watched, [paths[i] UTF8String], flags[i])) {
                auto on_change = state->on_change; // コールバックの中で破棄されても、呼び出しが終わるまで生きるようにコピー
                on_change();
                return; // 1回のコールバックでは1度だけ通知する
            }
        }
    }

    // FSEventsのストリームを1つ持ち、破棄で止める
    class DirWatchImpl : public pl_dir_watch {
    public:
        DirWatchImpl(FSEventStreamRef stream, MiataDirWatchState* state) : stream_(stream), state_(state) {}
        ~DirWatchImpl() override
        {
            // 念のため、キューに積まれたまま残ったコールバックがあっても呼び出し側へ届かないようにする
            // (Stop/Invalidateの後にFSEventsがコールバックを呼ばないことは実測で確認しているが、
            // 呼び出し側のon_changeはthisなどを参照するため、寿命切れの参照になる余地を残さない)
            state_->alive = NO;
            FSEventStreamStop(stream_);
            FSEventStreamInvalidate(stream_);
            FSEventStreamRelease(stream_); // コンテキストがretainしていたstateも手放す
        }
    private:
        FSEventStreamRef stream_;
        MiataDirWatchState* state_;
    };
}

std::unique_ptr<pl_dir_watch> pl_watch_directory(const std::filesystem::path& dir, std::function<void()> on_change)
{
    // FSEventsは実パスで報告する(/tmp -> /private/tmp など)ので、監視先も実パスにそろえる。
    // 存在しない・読めないディレクトリは監視しない。
    std::error_code ec;
    auto real = std::filesystem::canonical(dir, ec);
    if (ec || access(real.c_str(), R_OK) != 0) return nullptr;
    NSString* real_ns = [NSString stringWithUTF8String:real.c_str()];
    if (!real_ns) return nullptr;

    MiataDirWatchState* state = [[MiataDirWatchState alloc] init];
    state->on_change = std::move(on_change);
    state->watched = real;
    state->alive = YES;

    FSEventStreamContext context = {0, (__bridge void*)state, CFRetain, CFRelease, nullptr};
    FSEventStreamRef stream = FSEventStreamCreate(
        nullptr, &DirWatchCallback, &context, (__bridge CFArrayRef)@[real_ns],
        kFSEventStreamEventIdSinceNow,
        0.1, // 短時間の変化をまとめる幅(秒)。NoDeferなので最初の変化はすぐ届く
        kFSEventStreamCreateFlagFileEvents |   // ファイル単位のイベント(既存ファイルの中身の更新も届く)
        kFSEventStreamCreateFlagUseCFTypes |
        kFSEventStreamCreateFlagNoDefer |
        kFSEventStreamCreateFlagWatchRoot      // 監視先自身の移動/削除も届く
    );
    if (!stream) return nullptr;
    FSEventStreamSetDispatchQueue(stream, dispatch_get_main_queue());
    if (!FSEventStreamStart(stream)) {
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);
        return nullptr;
    }
    return std::make_unique<DirWatchImpl>(stream, state);
}

std::optional<std::filesystem::path> pl_find_executable(const std::string& name)
{
    @autoreleasepool {
        NSFileManager* fm = [NSFileManager defaultManager];
        auto is_executable = [&](const std::filesystem::path& candidate) {
            return (bool)[fm isExecutableFileAtPath:@(candidate.c_str())];
        };

        NSString* path_env = [[NSProcessInfo processInfo].environment objectForKey:@"PATH"];
        if (path_env) {
            for (NSString* dir in [path_env componentsSeparatedByString:@":"]) {
                if (dir.length == 0) continue;
                std::filesystem::path candidate = std::filesystem::path(dir.UTF8String) / name;
                if (is_executable(candidate)) return candidate;
            }
        }

        // GUIアプリ起動時はログインシェルのPATH(Homebrew等)を継承しないことが多いため、
        // 主要なインストール先を直接確認する。
        for (const char* dir : {"/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"}) {
            std::filesystem::path candidate = std::filesystem::path(dir) / name;
            if (is_executable(candidate)) return candidate;
        }
        return std::nullopt;
    }
}

std::expected<ProcessRunResult, std::string> pl_run_process(
    const std::filesystem::path& executable,
    const std::vector<std::string>& args,
    const std::filesystem::path& stdin_file)
{
    @autoreleasepool {
        NSFileHandle* input = [NSFileHandle fileHandleForReadingAtPath:@(stdin_file.c_str())];
        if (!input) {
            return std::unexpected(std::format("stdin file not readable: {}", stdin_file.string()));
        }

        NSTask* task = [[NSTask alloc] init];
        task.executableURL = [NSURL fileURLWithPath:@(executable.c_str())];
        NSMutableArray<NSString*>* ns_args = [NSMutableArray arrayWithCapacity:args.size()];
        for (auto& a : args) [ns_args addObject:@(a.c_str())];
        task.arguments = ns_args;
        task.standardInput = input;
        NSPipe* output_pipe = [NSPipe pipe];
        task.standardOutput = output_pipe;
        task.standardError = [NSFileHandle fileHandleWithNullDevice];

        NSError* error = nil;
        if (![task launchAndReturnError:&error]) {
            return std::unexpected(std::string(error.localizedDescription.UTF8String));
        }

        // 標準出力を先に読み切ってから waitUntilExit する。
        // 先に待ってしまうと、出力がパイプのバッファを超えた場合に
        // 子プロセスと親プロセスが互いを待ち続けて止まる(デッドロック)。
        NSData* output_data = [output_pipe.fileHandleForReading readDataToEndOfFile];
        [task waitUntilExit];

        ProcessRunResult result;
        result.exit_code = task.terminationStatus;
        result.stdout_text = std::string((const char*)output_data.bytes, output_data.length);
        return result;
    }
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
Color4f pl_get_color(pl_color_type type)
{
    @autoreleasepool {
        //NSAppearance* appearance = [NSAppearance currentDrawingAppearance];
        //NSLog(@"appearance: %@", appearance.name);
        NSScreen* screen = [NSScreen mainScreen];
        switch (type) {
        case pl_color_type::label_color:
            {
                auto c = [[NSColor labelColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::secondary_label_color:
            {
                auto c = [[NSColor secondaryLabelColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::tertiary_label_color:
            {
                auto c = [[NSColor tertiaryLabelColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::quaternary_label_color:
            {
                auto c = [[NSColor quaternaryLabelColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::text_color:
            {
                auto c = [[NSColor textColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::placeholder_text_color:
            {
                auto c = [[NSColor placeholderTextColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::selected_text_color:
            {
                auto c = [[NSColor selectedTextColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::text_background_color:
            {
                auto c = [[NSColor textBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::selected_text_background_color:
            {
                auto c = [[NSColor selectedTextBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::keyboard_focus_indicator_color:
            {
                auto c = [[NSColor keyboardFocusIndicatorColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::unemphasized_selected_text_color:
            {
                auto c = [[NSColor unemphasizedSelectedTextColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::unemphasized_selected_text_background_color:
            {
                auto c = [[NSColor unemphasizedSelectedTextBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::link_color:
            {
                auto c = [[NSColor linkColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::separator_color:
            {
                auto c = [[NSColor separatorColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::selected_content_background_color:
            {
                auto c = [[NSColor selectedContentBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::unemphasized_selected_content_background_color:
            {
                auto c = [[NSColor unemphasizedSelectedContentBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::selected_menu_item_text_color:
            {
                auto c = [[NSColor selectedMenuItemTextColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        //case pl_color_type::selected_menu_item_color:
        //    {
        //        auto c = [[NSColor selectedMenuItemColor] colorUsingColorSpace:screen.colorSpace];
        //        return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
        //    }
        case pl_color_type::header_text_color:
            {
                auto c = [[NSColor headerTextColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        //case pl_color_type::alternating_content_background_colors:
        //    {
        //        auto c = [[NSColor alternatingContentBackgroundColors] colorUsingColorSpace:screen.colorSpace];
        //        return Color4f{(float)c[0].redComponent, (float)c[0].greenComponent, (float)c[0].blueComponent, (float)c[0].alphaComponent};
        //    }
        case pl_color_type::control_accent_color:
            {
                auto c = [[NSColor controlAccentColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::control_color:
            {
                auto c = [[NSColor controlColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::control_background_color:
            {
                auto c = [[NSColor controlBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::control_text_color:
            {
                auto c = [[NSColor controlTextColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::disabled_control_text_color:
            {
                auto c = [[NSColor disabledControlTextColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        //case pl_color_type::current_control_tint:
        //    {
        //        auto c = [[NSColor currentControlTint] colorUsingColorSpace:screen.colorSpace];
        //        return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
        //    }
        case pl_color_type::selected_control_color:
            {
                auto c = [[NSColor selectedControlColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        //case pl_color_type::secondary_selected_control_color:
        //    {
        //        auto c = [[NSColor secondarySelectedControlColor] colorUsingColorSpace:screen.colorSpace];
        //        return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
        //    }
        //case pl_color_type::alternate_selected_control_color:
        //    {
        //        auto c = [[NSColor alternateSelectedControlColor] colorUsingColorSpace:screen.colorSpace];
        //        return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
        //    }
        case pl_color_type::selected_control_text_color:
            {
                auto c = [[NSColor selectedControlTextColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::alternate_selected_control_text_color:
            {
                auto c = [[NSColor alternateSelectedControlTextColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::scrubber_textured_background_color:
            {
                auto c = [[NSColor scrubberTexturedBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::window_background_color:
            {
                auto c = [[NSColor windowBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::window_frame_text_color:
            {
                auto c = [[NSColor windowFrameTextColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        //case pl_color_type::window_frame_color:
        //    {
        //        auto c = [[NSColor windowFrameColor] colorUsingColorSpace:screen.colorSpace];
        //        return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
        //    }
        case pl_color_type::under_page_background_color:
            {
                auto c = [[NSColor underPageBackgroundColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::find_highlight_color:
            {
                auto c = [[NSColor findHighlightColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::highlight_color:
            {
                auto c = [[NSColor highlightColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::shadow_color:
            {
                auto c = [[NSColor shadowColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::quaternary_system_fill_color:
            {
                auto c = [[NSColor quaternarySystemFillColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::quinary_label_color:
            {
                auto c = [[NSColor quinaryLabelColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::quinary_system_fill_color:
            {
                auto c = [[NSColor quinarySystemFillColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::secondary_system_fill_color:
            {
                auto c = [[NSColor secondarySystemFillColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::system_fill_color:
            {
                auto c = [[NSColor systemFillColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::tertiary_system_fill_color:
            {
                auto c = [[NSColor tertiarySystemFillColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        case pl_color_type::text_insertion_point_color:
            {
                auto c = [[NSColor textInsertionPointColor] colorUsingColorSpace:screen.colorSpace];
                return Color4f{(float)c.redComponent, (float)c.greenComponent, (float)c.blueComponent, (float)c.alphaComponent};
            }
        default:
            return Color4f{1, 0, 1, 1}; // Unity Purple
        }
    }
}




@interface MiataQuickLookController : NSViewController <QLPreviewPanelDataSource, QLPreviewPanelDelegate>
@property (nonatomic, strong) NSArray* previewFilePathArray;
@end

@implementation MiataQuickLookController
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
        MiataQuickLookController* controller = [[MiataQuickLookController alloc] init];
        [controller showPreviewForFileList:items];
    }
}




