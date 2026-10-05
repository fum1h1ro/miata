#import <Foundation/Foundation.h>
#include "NameMatcher.h"

namespace miata::views {

struct NameMatcher::Impl {
    NSString* needle = nil; // 語が空、または不正なUTF-8ならnil
    NSStringCompareOptions options = 0;
};

NameMatcher::NameMatcher(const std::string& query) : impl_(std::make_unique<Impl>())
{
    if (query.empty()) return;

    // バイト列のまま作る(不正なUTF-8ならnil。NULを含んでもよい: 名前にNULは無いので、どの名前にも一致しない)
    NSString* needle = [[NSString alloc] initWithBytes:query.data() length:query.size() encoding:NSUTF8StringEncoding];
    if (needle.length == 0) return;
    impl_->needle = needle;

    // スマートケース: 語に大文字(Unicodeの大文字を含む)があれば大文字小文字を区別し、無ければ区別しない。
    // NSLiteralSearchは付けない(付けると、合成済みの文字と分解された文字(「が」と「か」+濁点)が一致しなくなる。
    // Name()はNFCだが、語はNFDで来ることもある)。
    bool has_upper = [needle rangeOfCharacterFromSet:[NSCharacterSet uppercaseLetterCharacterSet]].location != NSNotFound;
    impl_->options = has_upper ? 0 : NSCaseInsensitiveSearch;
}

NameMatcher::~NameMatcher() = default;

bool NameMatcher::Empty() const
{
    return impl_->needle == nil;
}

std::optional<NameMatch> NameMatcher::Match(const std::string& name) const
{
    if (!impl_->needle) return std::nullopt;

    @autoreleasepool {
        NSString* text = @(name.c_str());
        if (!text) return std::nullopt;

        // textの中の語の出現箇所をすべて、左から重ならないように見つける。範囲はtextの添字(語の長さと同じとは
        // 限らない。合成済みと分解された文字は同じ文字として一致するため)
        NameMatch match;
        NSUInteger length = text.length;
        NSUInteger position = 0;
        while (position < length) {
            NSRange range = [text rangeOfString:impl_->needle options:impl_->options range:NSMakeRange(position, length - position)];
            if (range.location == NSNotFound || range.length == 0) break;
            match.ranges.push_back(MatchRange{range.location, range.length});
            position = NSMaxRange(range);
        }
        if (match.ranges.empty()) return std::nullopt;
        return match;
    }
}

} // namespace miata::views
