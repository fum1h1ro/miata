#ifndef LINE_ASSEMBLER_H__
#define LINE_ASSEMBLER_H__

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace miata {
    // バイト列の流れ(起動したコマンドの出力など。どこで切れて届いてもよい)を、行に分ける(純ロジック)。
    //  - 行の区切りは、LF・CRLF・CR(単独)。CR だけで行を書き換える進捗の表示(ffmpeg・curl など)は、書き換えのたびに 1 行になる
    //    (端末のように、同じ行を上書きすることは、しない。途中経過が、出たときに見える)
    //  - 行頭の CR(何も貯まっていないときの CR)は、行にしない: 「CR + 新しい内容」の形の進捗が、空行を挟まないように。
    //    CRLF だけの行は、空行になる(CR の次の LF を見てから決める)。CR だけの空行(CR CR)は、出ない。
    //    行を出した CR に続く CR と LF は、同じ区切りの続き(CR CR LF は、1 つの区切り)
    //  - 1 行は max_line_bytes まで。超えた分は、次の区切りまで捨てて、truncated の印を付ける(メモリは、増え続けない)
    //  - 区切りの無い残りは、持ち越す。流れの終わり(Flush)で、1 行として出す
    // どう分けて渡しても、出る行は同じ(CRLF が、渡す境で割れてもよい)。
    class LineAssembler {
    public:
        // 完結した 1 行(区切りを含まない、生のバイト列。呼んでいる間だけ有効)。truncated は、長すぎて、残りを捨てたこと
        using Emit = std::function<void(std::string_view line, bool truncated)>;

        explicit LineAssembler(size_t max_line_bytes);

        void Feed(std::string_view bytes, const Emit& emit);
        // 流れの終わり(EOF・プロセスの終了)。持ち越している行があれば、1 行として出す
        void Flush(const Emit& emit);

    private:
        // 直前のバイトが CR だったときの、その CR の扱い(次が LF なら、CRLF として 1 つの区切りにするため)
        enum class AfterCr {
            None,      // 直前は CR ではない
            Committed, // 直前の CR で、行を出した(次の LF は、その区切りの続き。何も出さない)
            Empty,     // 直前の CR のとき、何も貯まっていなかった(次の LF は、空行を出す)
        };

        void Append(std::string_view text);
        void Commit(const Emit& emit);

        size_t max_line_bytes_;
        std::string line_;        // 区切りを待っている行(max_line_bytes_ まで)
        bool truncated_ = false;  // line_ の後ろを捨てた
        AfterCr after_cr_ = AfterCr::None;
    };
}

#endif // LINE_ASSEMBLER_H__
