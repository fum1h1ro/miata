#include "Utf8.h"

namespace miata {
    namespace {
        // 先頭バイトごとの、2バイト目の範囲と、後ろに続く継続バイトの数(Unicodeの表3-7。正しい並びだけを受け付ける)
        struct Lead {
            unsigned char lo, hi; // 2バイト目の範囲
            int follow;           // 先頭の後ろに続くバイト数
        };

        // 先頭バイトとして使えなければ、follow = 0
        Lead LeadOf(unsigned char b)
        {
            if (b >= 0xC2 && b <= 0xDF) return {0x80, 0xBF, 1};
            if (b == 0xE0) return {0xA0, 0xBF, 2};                 // 2バイト目が 80〜9F だと、冗長な形(オーバーロング)
            if ((b >= 0xE1 && b <= 0xEC) || b == 0xEE || b == 0xEF) return {0x80, 0xBF, 2};
            if (b == 0xED) return {0x80, 0x9F, 2};                 // 2バイト目が A0〜BF だと、サロゲート
            if (b == 0xF0) return {0x90, 0xBF, 3};                 // 2バイト目が 80〜8F だと、冗長な形
            if (b >= 0xF1 && b <= 0xF3) return {0x80, 0xBF, 3};
            if (b == 0xF4) return {0x80, 0x8F, 3};                 // 2バイト目が 90〜BF だと、U+10FFFF を超える
            return {0, 0, 0};                                       // 継続バイト(80〜BF)、C0・C1、F5〜FF
        }

        inline bool IsContinuation(unsigned char b)
        {
            return b >= 0x80 && b <= 0xBF;
        }

        // sの先頭から、正しい並び(1文字分)の長さ。不正なら、不正な並びの長さ(最大の部分列。最低1)に負号をつけて返す
        int Scan(std::string_view s)
        {
            auto b0 = static_cast<unsigned char>(s[0]);
            if (b0 < 0x80) return 1;
            Lead lead = LeadOf(b0);
            if (lead.follow == 0) return -1;

            int length = 1;
            for (int k = 1; k <= lead.follow; ++k) {
                if (s.size() <= static_cast<size_t>(k)) return -length;   // 途中で切れている
                auto b = static_cast<unsigned char>(s[static_cast<size_t>(k)]);
                bool ok = (k == 1) ? (b >= lead.lo && b <= lead.hi) : IsContinuation(b);
                if (!ok) return -length;
                ++length;
            }
            return length;
        }
    }

    bool IsValidUtf8(std::string_view s)
    {
        while (!s.empty()) {
            int n = Scan(s);
            if (n < 0) return false;
            s.remove_prefix(static_cast<size_t>(n));
        }
        return true;
    }

    std::string RepairUtf8(std::string_view s)
    {
        std::string result;
        result.reserve(s.size());
        while (!s.empty()) {
            int n = Scan(s);
            if (n > 0) {
                result.append(s.substr(0, static_cast<size_t>(n)));
                s.remove_prefix(static_cast<size_t>(n));
            }
            else {
                result.append("\xEF\xBF\xBD"); // U+FFFD
                s.remove_prefix(static_cast<size_t>(-n));
            }
        }
        return result;
    }
}
