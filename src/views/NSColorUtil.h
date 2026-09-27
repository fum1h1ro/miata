#ifndef VIEWS_NS_COLOR_UTIL_H__
#define VIEWS_NS_COLOR_UTIL_H__

#import <AppKit/AppKit.h>
#include "../platform.h"

// AppKit依存のヘルパーなので.mmファイルからのみincludeする(Dialog.h/FileListView.h等の
// ビュー層ヘッダはAppKit非依存に保つ規約のため、ヘッダには置かない)。
namespace miata::views {
    inline NSColor* ToNSColor(const Color4f& c)
    {
        return [NSColor colorWithRed:c.r green:c.g blue:c.b alpha:c.a];
    }
}

#endif // VIEWS_NS_COLOR_UTIL_H__
