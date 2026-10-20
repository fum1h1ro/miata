#include "ConsoleLog.h"
#include <algorithm>
#include "../Utf8.h"

namespace miata::models {
    namespace {
        constexpr std::string_view kEllipsis = "\xE2\x80\xA6"; // …(3 バイト)
        constexpr size_t kTabWidth = 4;                         // タブを置き換える空白の数

        // 改行を待つ間(未完の行)に貯める、生のバイト数の上限。整形した後の上限(kMaxLineBytes)に、末尾の CR(CRLF の CR。
        // 確定のときに除く)の 1 バイトを足す: 足さないと、ちょうど kMaxLineBytes の行が CRLF で終わるとき、CR のせいで
        // 「長すぎて捨てた」ことになり、要らない「…」が付く
        constexpr size_t kMaxRawBytes = ConsoleLog::kMaxLineBytes + 1;

        // s(UTF-8 として正しいこと)の、limit バイト以下で、文字の境目になる、いちばん大きい位置
        size_t CharBoundaryAtMost(std::string_view s, size_t limit)
        {
            if (limit >= s.size()) return s.size();
            size_t p = limit;
            while (p > 0 && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80) --p;
            return p;
        }

        // 画面に出すと、行を壊す文字(UTF-8 として正しい文字列の、i の位置から始まる並び): C1 制御文字(U+0080〜009F。
        // NEL=U+0085 は、改行として扱われうる)と、行区切り・段落区切り(U+2028・2029)。長さ(バイト数)を返す。壊さなければ 0
        size_t BreakingSequenceLength(const std::string& s, size_t i)
        {
            const auto c0 = static_cast<unsigned char>(s[i]);
            if (c0 == 0xC2 && i + 1 < s.size()) {
                const auto c1 = static_cast<unsigned char>(s[i + 1]);
                if (c1 >= 0x80 && c1 <= 0x9F) return 2;
            }
            if (c0 == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80) {
                const auto c2 = static_cast<unsigned char>(s[i + 2]);
                if (c2 == 0xA8 || c2 == 0xA9) return 3;
            }
            return 0;
        }
    }

    std::string SanitizeLine(std::string_view raw, bool truncated)
    {
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);

        // 速い道: 印字できる ASCII だけ(ほとんどの行)。修復も置き換えも要らない
        bool plain = true;
        for (const char ch : raw) {
            const auto c = static_cast<unsigned char>(ch);
            if (c < 0x20 || c > 0x7E) {
                plain = false;
                break;
            }
        }

        std::string out;
        if (plain) {
            out.assign(raw);
        }
        else {
            const std::string fixed = RepairUtf8(raw);
            out.reserve(fixed.size());
            for (size_t i = 0; i < fixed.size();) {
                const auto c = static_cast<unsigned char>(fixed[i]);
                if (c == '\t') {
                    out.append(kTabWidth, ' ');
                }
                else if (c < 0x20 || c == 0x7F) {
                    out.push_back(' ');
                }
                else if (const size_t breaking = BreakingSequenceLength(fixed, i); breaking > 0) {
                    out.push_back(' ');
                    i += breaking;
                    continue;
                }
                else {
                    out.push_back(fixed[i]);
                }
                ++i;
            }
        }

        if (truncated || out.size() > ConsoleLog::kMaxLineBytes) {
            const size_t room = ConsoleLog::kMaxLineBytes - kEllipsis.size();
            out.resize(CharBoundaryAtMost(out, room));
            out.append(kEllipsis);
        }
        return out;
    }

    ConsoleLog& ConsoleLog::Instance()
    {
        // 解放しない(exit() が開いている FILE を flush するのは、静的破棄の後でもありうる。StdioCapture)
        static ConsoleLog* instance = new ConsoleLog();
        return *instance;
    }

    void ConsoleLog::AppendToPartialLocked(Partial& partial, std::string_view piece)
    {
        // 上限まで貯める。超えた分は捨てて、truncated の印を立てる。印が立った後は、room が 0 なので、次の改行まで、
        // 何も足さない(印は、確定のときに「…」になる)
        const size_t room = kMaxRawBytes - partial.raw.size();
        if (piece.size() <= room) {
            partial.raw.append(piece);
            return;
        }
        partial.raw.append(piece.substr(0, room));
        partial.truncated = true;
    }

    void ConsoleLog::CommitLocked(LogKind kind)
    {
        Partial& partial = partials_[static_cast<size_t>(kind)];
        LogLine line{next_seq_, kind, SanitizeLine(partial.raw, partial.truncated)};
        partial.raw.clear();
        partial.truncated = false;
        lines_.push_back(std::move(line));
        ++next_seq_;
        // 1 行ずつ捨てる(1 回の Write が巨大でも、上限を超えて膨らまない)
        while (lines_.size() > limit_) lines_.pop_front();
    }

    bool ConsoleLog::HasPendingLocked(LogKind kind) const
    {
        // truncated が立つのは、raw が上限まで貯まった後なので、raw が空なら、truncated も立っていない
        return !partials_[static_cast<size_t>(kind)].raw.empty();
    }

    void ConsoleLog::Write(LogKind kind, std::string_view bytes) noexcept
    {
        if (bytes.empty()) return;
        // 呼び出しは、libc の FILE の書き込みコールバックの中(C のフレーム)なので、例外を通さない。メモリ不足なら、その分を捨てる
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            bool committed = false;
            const LogKind other = kind == LogKind::Normal ? LogKind::Error : LogKind::Normal;
            if (HasPendingLocked(other)) {
                CommitLocked(other);
                committed = true;
            }
            Partial& partial = partials_[static_cast<size_t>(kind)];
            while (!bytes.empty()) {
                const size_t newline = bytes.find('\n');
                if (newline == std::string_view::npos) {
                    AppendToPartialLocked(partial, bytes);
                    break;
                }
                AppendToPartialLocked(partial, bytes.substr(0, newline));
                CommitLocked(kind);
                committed = true;
                bytes.remove_prefix(newline + 1);
            }
            if (committed) epoch_.fetch_add(1, std::memory_order_release);
        }
        catch (...) {
        }
    }

    void ConsoleLog::CommitPending() noexcept
    {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            bool committed = false;
            for (const LogKind kind : {LogKind::Normal, LogKind::Error}) {
                if (HasPendingLocked(kind)) {
                    CommitLocked(kind);
                    committed = true;
                }
            }
            if (committed) epoch_.fetch_add(1, std::memory_order_release);
        }
        catch (...) {
        }
    }

    void ConsoleLog::SetLimit(size_t lines) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        limit_ = std::clamp<size_t>(lines, 1, kMaxLimit);
        bool trimmed = false;
        while (lines_.size() > limit_) {
            lines_.pop_front();
            trimmed = true;
        }
        if (trimmed) epoch_.fetch_add(1, std::memory_order_release);
    }

    size_t ConsoleLog::Limit() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return limit_;
    }

    void ConsoleLog::Clear() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool had_lines = !lines_.empty();
        lines_.clear();
        for (auto& partial : partials_) {
            partial.raw.clear();
            partial.truncated = false;
        }
        if (had_lines) epoch_.fetch_add(1, std::memory_order_release);
    }

    uint64_t ConsoleLog::Epoch() const noexcept
    {
        return epoch_.load(std::memory_order_acquire);
    }

    LogRange ConsoleLog::Range() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return LogRange{
            .first_seq = lines_.empty() ? next_seq_ : lines_.front().seq,
            .next_seq = next_seq_,
            .epoch = epoch_.load(std::memory_order_acquire),
        };
    }

    std::vector<LogLine> ConsoleLog::Lines(uint64_t from_seq, size_t max) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lines_.empty() || max == 0) return {};
        const uint64_t first = lines_.front().seq;
        if (from_seq < first) from_seq = first;
        if (from_seq >= next_seq_) return {};
        const size_t start = static_cast<size_t>(from_seq - first);
        const size_t count = std::min(max, lines_.size() - start);
        return std::vector<LogLine>(
            lines_.begin() + static_cast<std::ptrdiff_t>(start),
            lines_.begin() + static_cast<std::ptrdiff_t>(start + count)
        );
    }

    std::string ConsoleLog::Text() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string text;
        bool first = true;
        for (const auto& line : lines_) {
            if (!first) text.push_back('\n');
            first = false;
            text += line.text;
        }
        return text;
    }
}
