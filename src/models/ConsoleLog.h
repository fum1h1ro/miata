#ifndef MODELS_CONSOLE_LOG_H__
#define MODELS_CONSOLE_LOG_H__

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace miata::models {
    // 行の種類。標準出力に書かれたものは Normal、標準エラーに書かれたもの(エラー・警告。Log.h の LogError もここ)は Error
    // (コンソールには赤で出る)
    enum class LogKind : uint8_t { Normal, Error };

    struct LogLine {
        uint64_t seq;      // 通し番号(0 から。古い行を捨てても、Clear しても、振り直さない)
        LogKind kind;
        std::string text;  // 整形済み(SanitizeLine): UTF-8 として正しく、制御文字・改行を含まず、kMaxLineBytes 以内
    };

    // いま持っている行の範囲と、世代の写し(1 回のロックで読む)
    struct LogRange {
        uint64_t first_seq;  // 持っている最も古い行の通し番号(空なら next_seq と同じ)
        uint64_t next_seq;   // 次に確定する行の通し番号(= 持っている最も新しい行の次)
        uint64_t epoch;      // 世代(ConsoleLog::Epoch と同じ)
        size_t Count() const { return static_cast<size_t>(next_seq - first_seq); }
    };

    // 確定した 1 行の生のバイト列を、画面に出せる形にする(純関数)。
    //  - 末尾の CR を 1 つ除く(CRLF)。UTF-8 として不正なバイトは U+FFFD に直す(RepairUtf8。NSString にすると nil になるため)
    //  - NUL・ESC などの制御文字と DEL は空白に、タブは空白 4 個にする(1 行の表示を、文字数で揃えるため)
    //  - kMaxLineBytes を超える分は、文字の途中を避けて切り、末尾に「…」を付ける。truncated は、呼び出し側が、既に
    //    途中で切ってあること(行が長すぎて、残りを捨てた)を伝える印: 「…」を付ける。切った位置が文字の途中だと、欠けた文字は
    //    U+FFFD になるが、raw は上限いっぱいあるので、ふつうは、整形の後の切り詰めで落ちる
    std::string SanitizeLine(std::string_view raw, bool truncated = false);

    // コンソールに出す行を貯める受け口(純ロジック。AppKit・Lua・Config のどれも知らない。PathHistory と同じ作り)。
    //  - 書く側: Write(stdout / stderr を差し替えた FILE から。StdioCapture)。断片でよい(改行で行が確定する)。任意のスレッドから呼べる
    //  - 読む側: メインスレッドが、毎ティック Epoch() を比べ、変わっていれば Range / Lines で見える行だけを写す(ConsoleView)
    //  - 行の上限: SetLimit。古い行から捨てる。設定(Config)は知らない: 呼び出し側(Application)が、設定の読み込みの後に渡す。
    //    それまでの間(設定の読み込み中の print も、ここに入る)は kMaxLimit で持つので、後から SetLimit で詰めても、
    //    最初から同じ上限で持っていたのと同じ中身になる
    //  - 寿命: Instance() は new して解放しない(exit() が開いている FILE を flush するのは、静的破棄の後でもありうるため)
    class ConsoleLog {
    public:
        // 1 行の最大バイト数(整形した後)。超えた分は「…」にする
        static constexpr size_t kMaxLineBytes = 2048;
        // 持てる行数の最大(SetLimit の上限であり、SetLimit が呼ばれるまでの上限)。Config::kConsoleMaxLinesMax と同じ値
        static constexpr size_t kMaxLimit = 100000;

        static ConsoleLog& Instance();

        // bytes を kind の流れの続きとして書く。改行(\n)で行が確定する。改行の無い残りは、種類ごとに持ち越して、次の
        // 書き込みで続ける。他方の種類の持ち越しがあれば、先に 1 行として確定する(標準出力の途中の行に、標準エラーの行が割り込んだ
        // とき、書いた順に並ぶように)。任意のスレッドから呼べる。例外は投げない
        void Write(LogKind kind, std::string_view bytes) noexcept;

        // 持ち越している未完の行を、1 行として確定する(テスト用。Write は、他方の種類の分を自分で確定する)
        void CommitPending() noexcept;

        // 行の上限。1〜kMaxLimit に丸める。縮めたら、古い行から捨てる(広げても、捨てた行は戻らない)
        void SetLimit(size_t lines) noexcept;
        size_t Limit() const noexcept;

        // 行を全部捨てる(未完の行も。通し番号は続く)
        void Clear() noexcept;

        // 世代。見える内容(行)が変わるたびに進む: 行が確定した Write、中身のある Clear、行を捨てた SetLimit。
        // 未完の行だけを持ち越した Write や、変わらない SetLimit では進まない。ロックしない(毎ティックの比較が安い)
        uint64_t Epoch() const noexcept;

        LogRange Range() const noexcept;
        // 通し番号が from_seq 以降の行を、最大 max 件、古い方から返す(写し)。from_seq が持っている範囲より前なら先頭から、
        // 後ろなら空
        std::vector<LogLine> Lines(uint64_t from_seq, size_t max) const;
        // 全行を '\n' でつないだ文字列(末尾に改行は付けない。種類は失われる)。console_copy 用
        std::string Text() const;

    private:
        struct Partial {
            std::string raw;         // 改行までの、生のバイト列(上限まで。超えた分は捨てる)
            bool truncated = false;  // 捨てた(次の改行まで、残りは捨てる)
        };

        // どれも、ロックを取ったまま呼ぶ
        void CommitLocked(LogKind kind);                     // kind の未完の行を、(空でも)1 行にする。上限を超えた古い行は捨てる
        bool HasPendingLocked(LogKind kind) const;           // kind の未完の行に、中身があるか
        void AppendToPartialLocked(Partial& partial, std::string_view piece);

        mutable std::mutex mutex_;
        std::deque<LogLine> lines_;
        std::array<Partial, 2> partials_;
        uint64_t next_seq_ = 0;
        size_t limit_ = kMaxLimit;
        std::atomic<uint64_t> epoch_{0};
    };
}

#endif // MODELS_CONSOLE_LOG_H__
