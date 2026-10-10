#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <CoreServices/CoreServices.h>
#include "../src/platform.h"
#include "../src/Utf8.h"
#include <unistd.h>
#include <copyfile.h>
#include <crt_externs.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

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
        // UTF-8として不正なバイトは、先にU+FFFDに置き換える。不正なままだと initWithBytes: がnilを返し、nilの
        // UTF8String(NULL)からstd::stringを作って落ちていた。ファイル名は、ファイルシステムによっては(ネットワーク
        // ボリューム、FUSEなど)任意のバイト列になり得る
        std::string valid = miata::RepairUtf8(input);
        NSString* nsInput = [[NSString alloc] initWithBytes:valid.data() length:valid.size() encoding:NSUTF8StringEncoding];
        NSString* normalized = [nsInput precomposedStringWithCanonicalMapping]; // NFC
        // 修復した後なのでnilにはならないはずだが、nilなら(NULLからstd::stringを作って落ちないよう)修復した名前を返す
        const char* utf8 = normalized.UTF8String;
        return utf8 ? std::string(utf8) : valid;
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

    [g_window center]; // 保存済みの位置が無い(初回)ときは、このまま中央に出る
    // 位置とサイズを自動で保存し、保存済みなら(centerよりも優先して)復元する。保存先はNSUserDefaults
    // (バンドルIDのドメインの "NSWindow Frame MiataMainWindow")。移動・リサイズのたびに保存されるので、
    // 終了のしかた(Cmd+Q、ウィンドウを閉じる、強制終了)によらず、最後の状態が残る。
    // 保存された位置が今のどの画面にも無い場合(外付けモニタを外した後など)は、復元の時点でAppKitが
    // 画面内へ寄せる。
    [g_window setFrameAutosaveName:@"MiataMainWindow"];
    [g_window makeKeyAndOrderFront:nil];
    [g_window makeFirstResponder:g_root_view];
    [NSApp activateIgnoringOtherApps:YES];
}

void* pl_get_content_view()
{
    return (__bridge void*)ns_content_view();
}

void pl_set_window_background_color(const Color4f& color)
{
    // 既定(windowBackgroundColor)はOSのテーマに従う(Lightだと純白)ので、設定の色で置き換える。
    // alphaは使わない(不透明)。
    g_window.backgroundColor = [NSColor colorWithRed:color.r green:color.g blue:color.b alpha:1.0];
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
    const std::filesystem::path& stdin_file,
    double timeout_seconds)
{
    // NSTaskは使わない(waitUntilExitが実行ループを回すため。宣言のコメント参照)。posix_spawn + 自前の待ち。
    int in_fd = open(stdin_file.c_str(), O_RDONLY | O_CLOEXEC);
    if (in_fd < 0) {
        return std::unexpected(std::format("stdin file not readable: {}", stdin_file.string()));
    }
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        int error = errno;
        close(in_fd);
        return std::unexpected(std::string(std::strerror(error)));
    }
    fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in_fd, STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    // 0 / 1 / 2 以外のファイル記述子は、子に渡さない。シグナルのマスクと、無視の設定は、既定に戻す
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t no_signals, all_signals;
    sigemptyset(&no_signals);
    sigfillset(&all_signals);
    posix_spawnattr_setsigmask(&attr, &no_signals);
    posix_spawnattr_setsigdefault(&attr, &all_signals);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

    std::string executable_string = executable.string();
    std::vector<std::string> argv_strings;
    argv_strings.reserve(args.size() + 1);
    argv_strings.push_back(executable_string);
    for (auto& a : args) argv_strings.push_back(a);
    std::vector<char*> argv;
    argv.reserve(argv_strings.size() + 1);
    for (auto& a : argv_strings) argv.push_back(a.data());
    argv.push_back(nullptr);

    pid_t pid = 0;
    int spawn_result = posix_spawn(&pid, executable_string.c_str(), &actions, &attr, argv.data(), *_NSGetEnviron());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(in_fd);
    close(pipe_fds[1]);
    if (spawn_result != 0) {
        close(pipe_fds[0]);
        return std::unexpected(std::string(std::strerror(spawn_result)));
    }

    // 標準出力を、終わり(EOF)まで読む。先に子の終了を待つと、出力がパイプのバッファを超えたとき、子と親が互いを
    // 待ち続けて止まる(デッドロック)。期限を過ぎたら、子を強制終了する
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(timeout_seconds));
    auto remaining_ms = [&]() -> int {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        return left <= 0 ? 0 : (int)std::min<long long>(left + 1, 1000000);
    };
    ProcessRunResult result;
    bool timed_out = false;
    char buffer[65536];
    for (;;) {
        int wait_ms = remaining_ms();
        if (wait_ms == 0) { timed_out = true; break; }
        pollfd pfd{pipe_fds[0], POLLIN, 0};
        int ready = poll(&pfd, 1, wait_ms);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) { timed_out = true; break; }
        ssize_t n = read(pipe_fds[0], buffer, sizeof buffer);
        if (n > 0) { result.stdout_text.append(buffer, (size_t)n); continue; }
        if (n < 0 && errno == EINTR) continue;
        break; // EOF(子が標準出力を閉じた=ふつうは終了した)、またはエラー
    }
    close(pipe_fds[0]);

    // 子の終了を待つ(標準出力を閉じても終わらない子のために、期限つき。waitpidのブロックはしない)
    int status = 0;
    bool reaped = false;
    if (!timed_out) {
        for (;;) {
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid) { reaped = true; break; }
            if (r < 0 && errno != EINTR) break;
            if (remaining_ms() == 0) { timed_out = true; break; }
            usleep(200);
        }
    }
    if (timed_out || !reaped) {
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
    if (timed_out) return std::unexpected(std::string("timed out"));

    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    return result;
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

        // 環境変数が設定されていない場合はデフォルト値を使用。XDG Base Directoryの仕様どおり、
        // 空や相対パスは設定されていないものとして扱う(相対パスだと、起動した場所によって設定の場所が変わってしまう)
        if (xdgConfigHome.length == 0 || ![xdgConfigHome hasPrefix:@"/"])
        {
            NSString* homeDir = NSHomeDirectory();
            xdgConfigHome = [homeDir stringByAppendingPathComponent:@".config"];
        }

        return [xdgConfigHome UTF8String];
    }
}

void pl_save_string_list(const std::string& key, const std::vector<std::string>& values)
{
    @autoreleasepool {
        NSString* ns_key = [NSString stringWithUTF8String:key.c_str()];
        NSMutableArray<NSString*>* array = [NSMutableArray arrayWithCapacity:values.size()];
        for (auto& value : values) {
            // UTF-8として不正な文字列はnilになる。配列にnilは入れられない(例外になる)ので捨てる
            NSString* s = [NSString stringWithUTF8String:value.c_str()];
            if (s) [array addObject:s];
        }
        NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
        if (array.count == 0) {
            [defaults removeObjectForKey:ns_key];
        }
        else {
            [defaults setObject:array forKey:ns_key];
        }
    }
}

std::vector<std::string> pl_load_string_list(const std::string& key)
{
    @autoreleasepool {
        std::vector<std::string> result;
        // 配列でないもの(手で書き換えられた、など)はnilで返る
        NSArray* array = [[NSUserDefaults standardUserDefaults] arrayForKey:[NSString stringWithUTF8String:key.c_str()]];
        for (id item in array) {
            if (![item isKindOfClass:[NSString class]]) continue;
            const char* utf8 = [(NSString*)item UTF8String]; // 孤立したサロゲートを含む文字列などはNULL
            if (utf8) result.emplace_back(utf8);
        }
        return result;
    }
}

void pl_save_string_map(const std::string& key, const std::map<std::string, std::string>& values)
{
    @autoreleasepool {
        NSString* ns_key = [NSString stringWithUTF8String:key.c_str()];
        NSMutableDictionary<NSString*, NSString*>* dict = [NSMutableDictionary dictionaryWithCapacity:values.size()];
        for (auto& [name, value] : values) {
            // UTF-8として不正な文字列はnilになる。辞書にnilは入れられない(例外になる)ので、その項目を捨てる
            NSString* ns_name = [NSString stringWithUTF8String:name.c_str()];
            NSString* ns_value = [NSString stringWithUTF8String:value.c_str()];
            if (ns_name && ns_value) dict[ns_name] = ns_value;
        }
        NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
        if (dict.count == 0) {
            [defaults removeObjectForKey:ns_key];
        }
        else {
            [defaults setObject:dict forKey:ns_key];
        }
    }
}

std::map<std::string, std::string> pl_load_string_map(const std::string& key)
{
    @autoreleasepool {
        std::map<std::string, std::string> result;
        // 辞書でないもの(手で書き換えられた、など)はnilで返る
        NSDictionary* dict = [[NSUserDefaults standardUserDefaults] dictionaryForKey:[NSString stringWithUTF8String:key.c_str()]];
        for (id name in dict) {
            id value = dict[name];
            if (![name isKindOfClass:[NSString class]] || ![value isKindOfClass:[NSString class]]) continue;
            const char* utf8_name = [(NSString*)name UTF8String];   // 孤立したサロゲートを含む文字列などはNULL
            const char* utf8_value = [(NSString*)value UTF8String];
            if (utf8_name && utf8_value) result.emplace(utf8_name, utf8_value);
        }
        return result;
    }
}

// NSErrorから、FileErrorを作る。権限が無い失敗(trashItemAtURL:では、書き込めない・OSの保護で止められた場合に
// NSCocoaErrorDomainの513が返る。実測)を、権限の失敗として区別する。Cocoaのコードに加えて、元になったエラー
// (NSUnderlyingErrorKey)のPOSIXのEPERM/EACCES、OSStatusのafpAccessDenied(-5000)も見る。
static miata::FileError ToFileError(NSError* error)
{
    miata::FileError result;
    result.message = error.localizedDescription.length > 0 ? error.localizedDescription.UTF8String : "unknown error";
    for (NSError* e = error; e; e = e.userInfo[NSUnderlyingErrorKey]) {
        if ([e.domain isEqualToString:NSCocoaErrorDomain]) {
            if (e.code == NSFileReadNoPermissionError || e.code == NSFileWriteNoPermissionError) result.permission_denied = true;
        }
        else if ([e.domain isEqualToString:NSPOSIXErrorDomain]) {
            if (e.code == EPERM || e.code == EACCES) result.permission_denied = true;
        }
        else if ([e.domain isEqualToString:NSOSStatusErrorDomain]) {
            if (e.code == -5000) result.permission_denied = true; // afpAccessDenied
        }
    }
    return result;
}

// ゴミ箱へ移すファイルのURL。UTF-8として不正な名前(ネットワークボリュームなど)は、NSStringにできない(nilを渡すと
// 例外になる)ので、バイト列のまま渡せる fileSystemRepresentation 版を使う。正しい名前は fileURLWithPath:
static NSURL* TrashTargetURL(const std::filesystem::path& path)
{
    NSString* ns_path = [NSString stringWithUTF8String:path.c_str()];
    return ns_path
        ? [NSURL fileURLWithPath:ns_path]
        : [NSURL fileURLWithFileSystemRepresentation:path.c_str() isDirectory:NO relativeToURL:nil];
}

// ---- クラウドストレージ(ファイルプロバイダ)の中のゴミ箱 ----
//
// サードパーティのファイルプロバイダ(Dropbox・Google Drive・OneDrive など)の領域は、~/Library/CloudStorage/ の下に
// 置かれる(macOS 12.3 以降)。その中のファイルは、普段の起動(Dock・Finder・open)のアプリからは、書き込みの許可があっても、
// ゴミ箱への移動だけが拒否される(実測、macOS 27.0、Dropbox。trashItemAtURL: も NSWorkspace の recycleURLs: も、
// Cocoa 513 で、sandboxd が「kTCCServiceFileProviderDomain … would require prompt」と断る。作成・リネーム・unlink は通る)。
// Finder は、自分の権限で移せるので、Apple Events で Finder に頼む。実測の詳細は .claude/rules/permissions-signing.md の「権限エラーの案内」。

// parent が、ファイルプロバイダの領域の中か。領域は root(~/Library/CloudStorage の実パス)の直下のフォルダなので、
// parent が root の下で(root と同じではない)あれば、その領域の中身。root 直下の項目(parent == root)は、領域の外
static bool IsInsideFileProviderDomain(const std::string& root, const std::string& parent)
{
    return !root.empty() && parent.size() > root.size() + 1 &&
           parent.compare(0, root.size(), root) == 0 && parent[root.size()] == '/';
}

// 実パス(シンボリックリンクを解決したもの)。解決できなければ nullopt
static std::optional<std::string> RealPathOf(const std::filesystem::path& path)
{
    char resolved[PATH_MAX];
    if (!realpath(path.c_str(), resolved)) return std::nullopt;
    return std::string(resolved);
}

// home をホームとして、path が領域の中か(テストで、偽のホームを使うために、分けてある)
static bool IsInFileProviderDomainOf(const std::filesystem::path& home, const std::filesystem::path& path)
{
    auto root = RealPathOf(home / "Library" / "CloudStorage");
    if (!root) return false; // CloudStorage が無い(ファイルプロバイダを使っていない)
    // 親だけを実パスにする。path 自身がシンボリックリンクなら、ゴミ箱の対象はリンクそのもので、リンク先ではない。
    // ~/Dropbox のような、領域へのシンボリックリンク経由のパスも、親を解決すれば、領域の中と分かる
    auto parent = RealPathOf(path.parent_path());
    if (!parent) return false;
    return IsInsideFileProviderDomain(*root, *parent);
}

bool pl_is_in_file_provider_domain(const std::filesystem::path& path)
{
    @autoreleasepool {
        NSString* home = NSHomeDirectory();
        if (home.length == 0) return false;
        return IsInFileProviderDomainOf(home.fileSystemRepresentation, path);
    }
}

// path が root と同じか、root の下か(root は実パス)
static bool IsUnderOrEqual(const std::string& root, const std::string& path)
{
    if (root.empty()) return false;
    if (path == root) return true;
    return path.size() > root.size() && path.compare(0, root.size(), root) == 0 && path[root.size()] == '/';
}

// home をホームとして、path が、クラウドストレージ(~/Library/CloudStorage か ~/Library/Mobile Documents)の中か
static bool IsInCloudStorageOf(const std::filesystem::path& home, const std::filesystem::path& path)
{
    std::optional<std::string> parent;
    std::optional<std::string> self;
    bool resolved = false;
    for (const char* sub : {"Library/CloudStorage", "Library/Mobile Documents"}) {
        auto root = RealPathOf(home / sub);
        if (!root) continue; // 無い(使っていない)
        if (!resolved) {
            // 親の実パスと、path 自身の実パス(リンクなら、リンク先)を、1 回だけ引く。どちらかが下にあれば、中
            parent = RealPathOf(path.parent_path());
            self = RealPathOf(path);
            resolved = true;
        }
        if ((parent && IsUnderOrEqual(*root, *parent)) || (self && IsUnderOrEqual(*root, *self))) return true;
    }
    return false;
}

bool pl_is_in_cloud_storage(const std::filesystem::path& path)
{
    @autoreleasepool {
        NSString* home = NSHomeDirectory();
        if (home.length == 0) return false;
        return IsInCloudStorageOf(home.fileSystemRepresentation, path);
    }
}

// Finder に頼んだ失敗の説明。status は OSStatus(Apple Events の送信の失敗か、Finder の返事の errn)、
// finder_message は Finder の返事の errs(あれば)。permission_denied は false のまま(フルディスクアクセスの案内は当てはまらない)
static miata::FileError FinderTrashError(OSStatus status, const std::string& finder_message)
{
    miata::FileError result;
    switch (status) {
    case errAEEventNotPermitted: // -1743。オートメーションの許可を拒否した(または、あとから外した)
        result.message = "Finderを操作する許可がありません。システム設定の「プライバシーとセキュリティ」→「オートメーション」で、"
                         "Miata の「Finder」をオンにしてください";
        break;
    case errAETimeout: // -1712
        result.message = "Finderの応答がありません(ゴミ箱への移動は、続いている可能性があります)";
        break;
    case procNotFound: // -600。Finder が起動していない
        result.message = "Finderが起動していないため、ゴミ箱へ移せません";
        break;
    default:
        result.message = std::format("Finderがゴミ箱への移動を断りました (OSStatus {}{})", static_cast<int>(status),
                                     finder_message.empty() ? std::string() : ": " + finder_message);
        break;
    }
    return result;
}

// Finder の返事を待つ秒数の上限。ゴミ箱への移動は、フォルダが大きいと時間がかかる(Finder は続けるので、超えても、移動は
// 続いている可能性がある)。許可のダイアログの待ちは含まない(送信の前に、別に確かめる)
static constexpr NSTimeInterval kFinderTrashTimeoutSeconds = 20;

// オートメーションの許可を確かめる関数。テストで差し替える(本物は、初回にダイアログを出す)
static OSStatus (*g_determine_automation_permission)(const AEAddressDesc*, AEEventClass, AEEventID, Boolean) =
    AEDeterminePermissionToAutomateTarget;

std::expected<void, miata::FileError> pl_trash_file_via_finder(const std::filesystem::path& path)
{
    // シンボリックリンクは頼まない: ファイルの URL で頼むと、Finder の返事が来ない(実測: 絶対パスのファイルへのリンクで、
    // 20 秒の上限まで待っても。2 回とも)ので、Miata がその間止まってしまう。ファイルプロバイダの領域には、ふつう
    // シンボリックリンクは無い(Dropbox などは、同期しない)ので、これで足りる
    struct stat link_status;
    if (lstat(path.c_str(), &link_status) == 0 && S_ISLNK(link_status.st_mode)) {
        return std::unexpected(miata::FileError{.message = "シンボリックリンクは、Finderに頼んでも、ゴミ箱へ移せません"});
    }

    // 呼んだスレッドで、そのまま行う。AEDeterminePermissionToAutomateTarget のヘッダーには「メインスレッドで呼ばない
    // (ユーザーの答えを待つので、いくらでも長くなりうる)」とあるが、別のスレッドに任せて、呼んだスレッドをセマフォで待たせる案は、
    // 実際の Apple Events では確かめられなかった(確かめようとしたとき、画面がロックされていて、許可のダイアログに答えられなかった)
    // ので、採っていない。メインスレッドから呼ぶ版は、許可済みの状態で、ひととおり通った(.claude/rules/permissions-signing.md の「権限エラーの案内」)。
    // 初回の許可のダイアログに答えるまで、呼んだスレッドが止まる(ダイアログは別のプロセスが出す。メインスレッドなら Miata 全体が止まる)。
    // 待つ間も、実行ループは回らない(実測: 4.7 秒待つ間、5 ms のタイマーが 1 回も発火しなかった)ので、Application::Update は再入しない
    @autoreleasepool {
        NSAppleEventDescriptor* finder = [NSAppleEventDescriptor descriptorWithBundleIdentifier:@"com.apple.finder"];

        // 許可を、送信とは別に確かめる。初回は、オートメーションの許可のダイアログが出て、答えるまで返らない
        // (送信の待ち時間 kFinderTrashTimeoutSeconds に、ユーザーが答えるまでの時間を含めないため。
        // 拒否されたときの失敗も、ここで分かる)
        OSStatus permission = g_determine_automation_permission(finder.aeDesc, typeWildCard, typeWildCard, true);
        if (permission != noErr) return std::unexpected(FinderTrashError(permission, ""));

        // Apple Events のコア(core)の delete。対象はファイルの URL。AppleScript の文字列を組まないので、
        // 名前に " や \ や UTF-8 として不正なバイトがあっても壊れない
        NSAppleEventDescriptor* event = [NSAppleEventDescriptor appleEventWithEventClass:kAECoreSuite
                                                                                 eventID:kAEDelete
                                                                        targetDescriptor:finder
                                                                                returnID:kAutoGenerateReturnID
                                                                           transactionID:kAnyTransactionID];
        NSData* url_data = [TrashTargetURL(path).absoluteString dataUsingEncoding:NSUTF8StringEncoding];
        [event setParamDescriptor:[NSAppleEventDescriptor descriptorWithDescriptorType:typeFileURL data:url_data]
                       forKeyword:keyDirectObject];

        // 返事を待つ。Finder にダイアログ(「すぐに削除しますか」など)を出させない(NeverInteract): 出させると、Miata が
        // 見えない Finder のダイアログを待つことになり、うっかり完全に削除される恐れもある
        NSError* error = nil;
        NSAppleEventDescriptor* reply = [event sendEventWithOptions:NSAppleEventSendWaitForReply | NSAppleEventSendNeverInteract
                                                            timeout:kFinderTrashTimeoutSeconds
                                                              error:&error];
        if (!reply) {
            OSStatus status = (error && [error.domain isEqualToString:NSOSStatusErrorDomain]) ? static_cast<OSStatus>(error.code) : -1;
            return std::unexpected(FinderTrashError(status, ""));
        }
        // Finder が断ったときは、送信は成功して、返事に errn(番号)と errs(説明)が入る
        NSAppleEventDescriptor* error_number = [reply paramDescriptorForKeyword:keyErrorNumber];
        if (error_number && error_number.int32Value != 0) {
            NSString* error_string = [reply paramDescriptorForKeyword:keyErrorString].stringValue;
            return std::unexpected(FinderTrashError(static_cast<OSStatus>(error_number.int32Value),
                                                    error_string.length > 0 ? error_string.UTF8String : ""));
        }
        return {};
    }
}

std::expected<void, miata::FileError> pl_trash_file(const std::filesystem::path& path)
{
    @autoreleasepool {
        NSError* error = nil;
        BOOL ok = [[NSFileManager defaultManager] trashItemAtURL:TrashTargetURL(path) resultingItemURL:nil error:&error];
        if (!ok) {
            miata::FileError failure = ToFileError(error);
            // クラウドストレージの中は、書き込みの許可があっても、ゴミ箱への移動だけが拒否される(上の説明)。Finder に頼む。
            // ターミナルから起動した Miata は、この拒否が起きず、ここへは来ない
            if (failure.permission_denied && pl_is_in_file_provider_domain(path)) {
                return pl_trash_file_via_finder(path);
            }
            return std::unexpected(failure);
        }
        return {};
    }
}

bool pl_is_alias_file(const std::filesystem::path& path)
{
    // Finderのエイリアスは、通常のファイルで、Finder情報(32バイト)の finderFlags(オフセット8のビッグエンディアン16ビット)に
    // kIsAlias(0x8000)が立っている。FSOPT_NOFOLLOWで、シンボリックリンクはリンクそのもの(印は無い)を見る。
    // パスは c_str() のバイト列のまま渡す(NSStringにすると、UTF-8として不正な名前でnilになる)
    struct attrlist wanted = {};
    wanted.bitmapcount = ATTR_BIT_MAP_COUNT;
    wanted.commonattr = ATTR_CMN_FNDRINFO;
    struct { uint32_t length; uint8_t finder_info[32]; } buffer = {};
    if (getattrlist(path.c_str(), &wanted, &buffer, sizeof(buffer), FSOPT_NOFOLLOW) != 0) return false;
    return (buffer.finder_info[8] & 0x80) != 0;
}

bool pl_is_dataless_file(const std::filesystem::path& path)
{
    // ダウンロード前のファイルには、OSが SF_DATALESS(st_flags の 0x40000000。書き込めない合成フラグ)を立てる。lstat は
    // メタデータだけを読むので、ダウンロードは起きない(実測)。シンボリックリンクは、リンクそのもの(ローカルのリンクに、この印は無い)を見る。
    // パスは c_str() のバイト列のまま渡す(NSStringにすると、UTF-8として不正な名前でnilになる)
    struct stat status;
    if (lstat(path.c_str(), &status) != 0) return false;
    return (status.st_flags & SF_DATALESS) != 0;
}

// copyfile(3) の進捗のコールバック: 中身をコピーしている間、1 MiB ごとに来る。COPYFILE_STATE_COPIED は、このファイルの累計。
// **エラー(COPYFILE_ERR)のときに、CONTINUE を返してはいけない**: man page のとおり、「同じデータの書き込みをやり直す」ことになり、
// 容量が足りない・書き込み中に先が外れた、のような書き込みの失敗で、終わらなくなる(実測: ENOSPC で、再試行が続いた)。
// QUIT なら、コールバックが無いときと同じく、copyfile が、そのエラー(errno はそのまま)で失敗する
static int CopyStatusCallback(int what, int stage, copyfile_state_t state, const char* /*src*/, const char* /*dst*/, void* context)
{
    if (stage == COPYFILE_ERR) return COPYFILE_QUIT;
    if (what == COPYFILE_COPY_DATA && stage == COPYFILE_PROGRESS) {
        off_t copied = 0;
        copyfile_state_get(state, COPYFILE_STATE_COPIED, &copied);
        (*static_cast<const std::function<void(std::int64_t)>*>(context))(static_cast<std::int64_t>(copied));
    }
    return COPYFILE_CONTINUE;
}

// copyfile(3) を1回呼ぶ。失敗は errno の error_code にする。on_progress が空でなければ、進捗のコールバックを付ける
static std::error_code CopyFileWithFlags(
    const std::filesystem::path& src,
    const std::filesystem::path& dst,
    copyfile_flags_t flags,
    const std::function<void(std::int64_t)>& on_progress = nullptr
)
{
    copyfile_state_t state = copyfile_state_alloc();
    if (on_progress) {
        copyfile_state_set(state, COPYFILE_STATE_STATUS_CB, reinterpret_cast<const void*>(&CopyStatusCallback));
        copyfile_state_set(state, COPYFILE_STATE_STATUS_CTX, &on_progress);
    }
    int result = ::copyfile(src.c_str(), dst.c_str(), state, flags);
    // copyfile_state_free が errno を書き換えることがあるので、先に退避する
    int saved_errno = errno;
    copyfile_state_free(state);
    // (-1 なのに errno が 0 なら、成功に見えてしまうので、EIO にする)
    return result == 0 ? std::error_code() : std::error_code(saved_errno != 0 ? saved_errno : EIO, std::generic_category());
}

// コピー中の一時ファイルの名前(dst と同じフォルダ)。dst の名前は使わない(名前の長さの上限を超えないため)。同じ名前が既にあれば
// (前の異常終了の残りなど)、COPYFILE_EXCL が EEXIST にするので、attempt を変えて、やり直す
static std::filesystem::path TemporaryCopyPath(const std::filesystem::path& dst, int attempt)
{
    static std::atomic<unsigned> counter{0};
    return dst.parent_path() / std::format(".miata-copy-{}-{}-{}", ::getpid(), counter.fetch_add(1), attempt);
}

std::error_code pl_copy_file(
    const std::filesystem::path& src,
    const std::filesystem::path& dst,
    bool replace,
    const std::function<void(std::int64_t copied)>& on_progress
)
{
    // ALL: 中身・更新日時・権限・拡張属性・ACL。NOFOLLOW_SRC: 元がリンクなら、リンクそのものを写す。
    // CLONE: APFSの同じボリュームでは、クローン(別のボリュームやAPFS以外では、普通のコピーに戻る)。
    // EXCL: 先に同名があれば、EEXIST
    constexpr copyfile_flags_t kFlags = COPYFILE_ALL | COPYFILE_NOFOLLOW_SRC | COPYFILE_CLONE | COPYFILE_EXCL;
    if (!replace) return CopyFileWithFlags(src, dst, kFlags, on_progress);

    // 置き換え: COPYFILE_UNLINK(先を消してから置く)は使わない。置く途中で失敗すると、先だけが失われる:クローンのときは、元を調べる前に
    // 先を消すので、元が読めないだけで、先が消える(実測)。容量が足りないときも同じ。同じフォルダの一時ファイルへ置いて、全部成功したときだけ、
    // rename で置き換える(rename は、先がリンクならリンクそのものを置き換え、別のハードリンクの中身は変えず、フォルダには EISDIR で失敗する)
    for (int attempt = 0; attempt < 8; ++attempt) {
        auto temporary = TemporaryCopyPath(dst, attempt);
        auto ec = CopyFileWithFlags(src, temporary, kFlags, on_progress);
        if (ec == std::errc::file_exists) continue; // 一時ファイルの名前が、使われていた(消さない: 自分のものではない)
        if (ec) {
            ::unlink(temporary.c_str()); // 作りかけは copyfile が消すが、念のため
            return ec;
        }
        if (::rename(temporary.c_str(), dst.c_str()) != 0) {
            int saved_errno = errno;
            ::unlink(temporary.c_str());
            return std::error_code(saved_errno, std::generic_category());
        }
        return {};
    }
    return std::make_error_code(std::errc::file_exists);
}

std::error_code pl_copy_directory_attributes(const std::filesystem::path& src, const std::filesystem::path& dst)
{
    // METADATA = 権限・更新日時・BSDフラグ(STAT)、拡張属性(XATTR)、ACL。DATA は含まないので、中身は写らない
    return CopyFileWithFlags(src, dst, COPYFILE_METADATA);
}

bool pl_open_full_disk_access_settings()
{
    @autoreleasepool {
        NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
        // 「プライバシーとセキュリティ > フルディスクアクセス」を直接開く(System Settingsも、この形式のURLを解釈する)
        NSURL* pane = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles"];
        if ([workspace openURL:pane]) return true;
        // 開けなかった(OSの版で、画面のURLが変わった場合など)ときは、システム設定そのものを開く
        // (案内の文面に、「プライバシーとセキュリティ」→「フルディスクアクセス」とたどる手順がある)
        NSURL* settings = [workspace URLForApplicationWithBundleIdentifier:@"com.apple.systempreferences"];
        return settings && [workspace openURL:settings];
    }
}

// パスから、ファイルのURLを作る。UTF-8として不正な名前(ネットワークボリュームなど)でも例外にならないよう、
// バイト列のまま(fileSystemRepresentation)渡す。パスが空だとnil
static NSURL* FileURLFromPath(const std::filesystem::path& path)
{
    return [NSURL fileURLWithFileSystemRepresentation:path.c_str() isDirectory:NO relativeToURL:nil];
}

std::optional<std::filesystem::path> pl_find_application(const std::string& spec)
{
    @autoreleasepool {
        if (spec.empty()) return std::nullopt;
        NSFileManager* file_manager = [NSFileManager defaultManager];

        // 絶対パス: .appバンドルはフォルダなので、フォルダとして存在すればよい
        if (spec[0] == '/') {
            NSString* ns_path = [NSString stringWithUTF8String:spec.c_str()];
            BOOL is_directory = NO;
            if (ns_path && [file_manager fileExistsAtPath:ns_path isDirectory:&is_directory] && is_directory) return std::filesystem::path(spec);
            return std::nullopt;
        }

        NSString* name = [NSString stringWithUTF8String:spec.c_str()]; // 不正なUTF-8だとnil
        if (!name) return std::nullopt;

        // Bundle ID(ドットを含む)。見つからなければ、名前として探す("Foo.app" などもドットを含むため)
        if ([name containsString:@"."]) {
            NSURL* url = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:name];
            if (url) return std::filesystem::path(url.fileSystemRepresentation);
        }

        // 名前: 標準の場所を、この順に探す(大文字小文字は区別しない。".app" は付けても省いてもよい)
        NSString* wanted = name.lowercaseString;
        if (![wanted hasSuffix:@".app"]) wanted = [wanted stringByAppendingString:@".app"];
        NSArray<NSString*>* directories = @[
            [NSHomeDirectory() stringByAppendingPathComponent:@"Applications"],
            @"/Applications",
            @"/Applications/Utilities",
            @"/System/Applications",
            @"/System/Applications/Utilities",
            @"/System/Library/CoreServices",
        ];
        for (NSString* directory in directories) {
            NSArray<NSString*>* entries = [file_manager contentsOfDirectoryAtPath:directory error:nil];
            for (NSString* entry in entries) {
                if ([entry.lowercaseString isEqualToString:wanted]) {
                    return std::filesystem::path(directory.UTF8String) / entry.UTF8String;
                }
            }
        }
        return std::nullopt;
    }
}

namespace {
    // 既定のアプリで1件ずつ開くときの、結果の集計(完了ハンドラは別のキューで呼ばれるので、ロックで守る)
    struct OpenTally {
        std::mutex mutex;
        int failed = 0;
        std::string first_message;
    };

    std::string ErrorDescription(NSError* error)
    {
        return error.localizedDescription.length > 0 ? error.localizedDescription.UTF8String : "unknown error";
    }
}

std::expected<void, std::string> pl_open_paths(
    const std::vector<std::filesystem::path>& paths,
    const std::string& app,
    std::function<void(int failed, int total, const std::string& message)> on_failure)
{
    @autoreleasepool {
        NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
        NSWorkspaceOpenConfiguration* configuration = [NSWorkspaceOpenConfiguration configuration];
        const int total = (int)paths.size();

        if (!app.empty()) {
            // アプリを指定: 全部をまとめて、そのアプリに渡す
            auto app_path = pl_find_application(app);
            if (!app_path) return std::unexpected(std::format("アプリが見つかりません ({})", app));
            NSMutableArray<NSURL*>* urls = [NSMutableArray arrayWithCapacity:paths.size()];
            for (const auto& path : paths) {
                if (NSURL* url = FileURLFromPath(path)) [urls addObject:url];
            }
            if (urls.count == 0) return std::unexpected("invalid path");
            [workspace openURLs:urls
                withApplicationAtURL:FileURLFromPath(*app_path)
                configuration:configuration
                completionHandler:^(NSRunningApplication*, NSError* error) {
                    if (!error) return;
                    std::string message = ErrorDescription(error);
                    // 完了ハンドラは別のキューで呼ばれるので、画面に出す側(メインスレッド)へ戻す
                    // (まとめて1回の要求なので、失敗したときは、全部が開けなかったことになる)
                    dispatch_async(dispatch_get_main_queue(), ^{ if (on_failure) on_failure(total, total, message); });
                }];
            return {};
        }

        // 既定のアプリ: ファイルごとに、そのファイルの既定のアプリで開く。結果は全部そろってから、1回でまとめて知らせる
        auto tally = std::make_shared<OpenTally>();
        dispatch_group_t group = dispatch_group_create();
        for (const auto& path : paths) {
            NSURL* url = FileURLFromPath(path);
            if (!url) {
                std::lock_guard<std::mutex> lock(tally->mutex);
                if (tally->failed++ == 0) tally->first_message = "invalid path";
                continue;
            }
            dispatch_group_enter(group);
            [workspace openURL:url
                configuration:configuration
                completionHandler:^(NSRunningApplication*, NSError* error) {
                    if (error) {
                        std::lock_guard<std::mutex> lock(tally->mutex);
                        if (tally->failed++ == 0) tally->first_message = ErrorDescription(error);
                    }
                    dispatch_group_leave(group);
                }];
        }
        dispatch_group_notify(group, dispatch_get_main_queue(), ^{
            std::lock_guard<std::mutex> lock(tally->mutex);
            if (tally->failed > 0 && on_failure) on_failure(tally->failed, total, tally->first_message);
        });
        return {};
    }
}

void pl_reveal_paths(const std::vector<std::filesystem::path>& paths)
{
    @autoreleasepool {
        NSMutableArray<NSURL*>* urls = [NSMutableArray arrayWithCapacity:paths.size()];
        for (const auto& path : paths) {
            if (NSURL* url = FileURLFromPath(path)) [urls addObject:url];
        }
        if (urls.count > 0) [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:urls];
    }
}

bool pl_set_clipboard_text(const std::string& text)
{
    @autoreleasepool {
        // UTF-8として不正なバイトはU+FFFDにする(NSStringにできないため)。NULを含んでもよいので、c_str()ではなく長さを渡す
        const std::string valid = miata::RepairUtf8(text);
        NSString* string = [[NSString alloc] initWithBytes:valid.data() length:valid.size() encoding:NSUTF8StringEncoding];
        if (!string) return false;
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];
        return [pasteboard setString:string forType:NSPasteboardTypeString];
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
