#import <AppKit/AppKit.h>
#include "../src/Application.h"

@interface MiataAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation MiataAppDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    miata::Application::Initialize();
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app
{
    return YES;
}
@end

int main(int argc, const char* argv[])
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        MiataAppDelegate* delegate = [[MiataAppDelegate alloc] init];
        NSApp.delegate = delegate;
        return NSApplicationMain(argc, argv);
    }
}
