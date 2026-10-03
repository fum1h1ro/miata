#include "Mnemonic.h"

namespace miata::views {
    namespace {
        bool IsAsciiAlnum(char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }

        char ToLowerAscii(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
    }

    MnemonicLabel ParseMnemonicLabel(std::string_view raw)
    {
        MnemonicLabel label;
        label.text.reserve(raw.size());
        for (size_t i = 0; i < raw.size(); ++i) {
            const char c = raw[i];
            if (c != '&') {
                label.text += c;
                continue;
            }
            const bool has_next = i + 1 < raw.size();
            if (has_next && raw[i + 1] == '&') {
                label.text += '&';
                ++i;
            }
            else if (has_next && IsAsciiAlnum(raw[i + 1])) {
                if (label.key == 0) {
                    label.key = ToLowerAscii(raw[i + 1]);
                    label.pos = label.text.size(); // 次の周回で足される文字の位置
                }
                // `&` の印は取り除く。直後の文字は、次の周回で、ふつうの文字として足される
            }
            else {
                label.text += '&';
            }
        }
        return label;
    }
}
