#include "LineAssembler.h"

namespace miata {
    LineAssembler::LineAssembler(size_t max_line_bytes) : max_line_bytes_(max_line_bytes)
    {
    }

    void LineAssembler::Append(std::string_view text)
    {
        // 上限まで貯める。超えた分は捨てて、印を立てる(印が立った後は、room が 0 なので、次の区切りまで、何も足さない)
        const size_t room = max_line_bytes_ - line_.size();
        if (text.size() <= room) {
            line_.append(text);
            return;
        }
        line_.append(text.substr(0, room));
        truncated_ = true;
    }

    void LineAssembler::Commit(const Emit& emit)
    {
        emit(line_, truncated_);
        line_.clear();
        truncated_ = false;
    }

    void LineAssembler::Feed(std::string_view bytes, const Emit& emit)
    {
        while (!bytes.empty()) {
            const size_t end = bytes.find_first_of("\r\n");
            const std::string_view text = bytes.substr(0, end); // 区切りが無ければ、残り全部
            if (!text.empty()) {
                Append(text);
                after_cr_ = AfterCr::None;
            }
            if (end == std::string_view::npos) break;

            const char separator = bytes[end];
            bytes.remove_prefix(end + 1);
            if (separator == '\n') {
                // CR で出した行の、続きの LF(CRLF)は、何も出さない。それ以外(ふつうの LF・空の CRLF)は、行を出す
                if (after_cr_ != AfterCr::Committed) Commit(emit);
                after_cr_ = AfterCr::None;
            }
            else if (line_.empty()) {
                // 行頭の CR。行にしない(次が LF なら空行、そうでなければ、何も出さない)。CR で行を出した直後なら、
                // その区切りの続きのまま(CR CR LF は、1 つの区切り)
                if (after_cr_ != AfterCr::Committed) after_cr_ = AfterCr::Empty;
            }
            else {
                Commit(emit);
                after_cr_ = AfterCr::Committed;
            }
        }
    }

    void LineAssembler::Flush(const Emit& emit)
    {
        if (!line_.empty()) Commit(emit);
        after_cr_ = AfterCr::None;
    }
}
