#ifndef VIEWS_MNEMONIC_H__
#define VIEWS_MNEMONIC_H__

#include <cstddef>
#include <string>
#include <string_view>

namespace miata::views {
    // ダイアログの項目(ボタン・チェックボックス・選択リストの行)のラベルを、`&` の記法で解析した結果。
    // AppKit型は持たない(DialogPanelが、これを使って、表示と、ショートカットの登録を行う)。
    struct MnemonicLabel {
        std::string text; // 画面に出す文字列(`&` の印を取り除いた後)
        char key = 0;     // ショートカットの文字(小文字のASCII英数字)。ショートカットが無ければ0
        size_t pos = 0;   // text の中の、下線を付ける文字のバイト位置(keyが0でないときだけ意味がある。text[pos] がその文字)
    };

    // ラベルの `&` を解釈する。`&x` と書くと、x がショートカットの文字になる(`名前(&N)` は、画面では「名前(N)」で、
    // Nに下線が付く)。
    //  - `&&` は、文字としての `&`
    //  - `&` の直後がASCIIの英数字なら、その文字がショートカット。大文字小文字は同じ(keyは小文字)。最初の1つだけ有効で、
    //    2つ目以降の `&x` は、`&` を取り除くだけ(ショートカットにしない)
    //  - それ以外の `&`(直後が空白・記号・ASCII以外・文字列の末尾)は、文字としての `&`。"Tom & Jerry" のような文字列を壊さない
    // `&`(0x26)はUTF-8の連続バイトに現れないので、バイト単位で走査する。UTF-8として不正な入力でも、範囲外を読まない
    // (textは、入力から `&` の印を取り除いただけ)。
    MnemonicLabel ParseMnemonicLabel(std::string_view raw);
}

#endif // VIEWS_MNEMONIC_H__
