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
                cr_ended_line_ = false;
            }
            if (end == std::string_view::npos) break;

            const char separator = bytes[end];
            bytes.remove_prefix(end + 1);
            if (separator == '\n') {
                // CR で出した行の、続きの LF(CRLF)は、何も出さない。それ以外は、行を出す(空でも: 空行)
                if (!cr_ended_line_) Commit(emit);
                cr_ended_line_ = false;
            }
            else if (!line_.empty()) {
                // CR の時点で、すぐ出す(次のバイトを待たない: 進捗が、出たときに見える)
                Commit(emit);
                cr_ended_line_ = true;
            }
            // 行頭の CR は、行にしない(行を出した CR の直後なら、その区切りの続きのまま)
        }
    }

    void LineAssembler::Flush(const Emit& emit)
    {
        if (!line_.empty()) Commit(emit);
        cr_ended_line_ = false;
    }
}
