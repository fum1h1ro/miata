#ifndef UTF8_H__
#define UTF8_H__

#include <string>
#include <string_view>

namespace miata {
    // UTF-8として正しいか(最短の形・サロゲート(U+D800〜DFFF)なし・U+10FFFF以下)。空文字列は正しい。
    // ファイル名は、ファイルシステムによっては(ネットワークボリューム、FUSEなど。APFSは不正な名前を作らせない)、
    // UTF-8として不正な任意のバイト列になり得る。一方、画面に出す・並べ替える・検索するためにNSStringにするには、
    // UTF-8として正しい必要がある(不正だとnilになり、nilを引数にしたcompare:は例外になる)。
    bool IsValidUtf8(std::string_view s);

    // UTF-8として不正なバイトを、U+FFFD(�)に置き換えた文字列を返す。不正な並び1つ(Unicodeが勧める「最大の部分列」。
    // 途中で切れた3バイトの並びは、2バイトでも1つ)を、U+FFFD 1つにする。正しい文字列は、そのまま(同じ内容)返す。
    // 結果は常にUTF-8として正しい。
    std::string RepairUtf8(std::string_view s);
}

#endif // UTF8_H__
