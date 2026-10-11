#include "StatusBarState.h"
#include <format>
#include <sys/stat.h>
#include "../Format.h"
#include "../Utf8.h"

namespace miata::views {
    namespace {
        // 左: カーソル下のファイルの属性
        std::string EntryText(const std::optional<StatusEntry>& entry)
        {
            if (!entry) return "";
            if (!entry->info) {
                return entry->error.empty() ? "(情報を取得できません)" : "(情報を取得できません: " + entry->error + ")";
            }
            const pl_file_info& info = *entry->info;

            std::string s = FormatMode(info.mode);
            s += "  " + info.owner + ":" + info.group;
            // サイズは、通常のファイルだけ(フォルダのサイズは意味が無い。リンクの st_size は、リンク先の文字列の長さ)。
            // 正確なバイト数に、1024 以上なら、一覧と同じ書式を添える
            if ((info.mode & S_IFMT) == S_IFREG) {
                s += "  " + FormatGroupedBytes(info.size);
                if (info.size >= 1024) s += " (" + FormatSize(info.size) + ")";
            }
            s += "  " + FormatLocalTime((std::time_t)info.mtime);
            if (entry->is_link && !info.link.empty()) s += "  -> " + info.link;
            // 一覧の右の欄の札と同じ呼び名。ダウンロード前は、リンク・エイリアス・フォルダを除く通常のファイルだけ(一覧と同じ)
            if (entry->is_alias) s += "  <ALIAS>";
            else if (!entry->is_dir && !entry->is_link && info.dataless) s += "  <CLOUD>";
            return s;
        }

        // 右: マークの要約
        std::string MarksText(const MarkSummary& marks)
        {
            if (marks.count == 0 && marks.hidden == 0) return "";
            std::string s = std::format("マーク {} 件", marks.count);
            if (marks.dirs > 0) s += std::format(" (フォルダ {})", marks.dirs);
            // フォルダしか無いとき(合計が 0B になるだけのとき)は、サイズを出さない
            if (marks.count - marks.dirs > 0) s += "  " + FormatSize(marks.bytes);
            if (marks.hidden > 0) s += std::format("  (隠れ {})", marks.hidden);
            return s;
        }

        // 前置きの途中: 「d …  d マークをゴミ箱へ」。続きのあるキーは「s…」。説明が無ければ、キー名だけ
        std::string PendingText(const StatusPending& pending)
        {
            std::string s = pending.keys + " …";
            for (const auto& candidate : pending.candidates) {
                s += "  " + candidate.key;
                if (!candidate.leaf) s += "…";
                if (!candidate.desc.empty()) s += " " + candidate.desc;
            }
            return s;
        }
    }

    StatusText DefaultStatusText(const StatusSnapshot& snapshot)
    {
        StatusText text;
        text.left = snapshot.pending ? PendingText(*snapshot.pending) : EntryText(snapshot.entry);
        text.right = MarksText(snapshot.marks);
        return text;
    }

    std::string SanitizeBarText(std::string_view text)
    {
        std::string s = RepairUtf8(text);
        for (char& c : s) {
            if ((unsigned char)c < 0x20 || c == 0x7F) c = ' ';
        }
        constexpr size_t kMaxBytes = 1024;
        if (s.size() > kMaxBytes) {
            size_t n = kMaxBytes;
            // UTF-8 の文字の途中(継続バイト)で切らない
            while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) --n;
            s.resize(n);
        }
        return s;
    }

    std::optional<StatusText> StatusBarState::Update(
        const std::function<StatusKey()>& make_key,
        bool has_function,
        const Collect& collect,
        const CallScript& script
    )
    {
        const StatusKey key = make_key();
        if (last_key_ && *last_key_ == key) return std::nullopt;

        // 関数が差し替えられた(世代が変わった)ら、ラッチを解く
        if (latched_error_ && latched_generation_ != key.function_generation) latched_error_.reset();

        const StatusSnapshot snapshot = collect();
        const StatusText defaults = DefaultStatusText(snapshot);
        StatusText text = defaults;

        if (has_function) {
            if (!latched_error_) {
                auto result = script(snapshot, defaults);
                if (result) {
                    if (result->left) text.left = *result->left;
                    if (result->right) text.right = *result->right;
                }
                else {
                    latched_error_ = result.error();
                    latched_generation_ = key.function_generation;
                }
            }
            // ラッチ中(今回のエラーも含む)は、エラーを左に出し続ける。前置きの途中は、候補を優先する
            if (latched_error_ && !snapshot.pending) text.left = "Lua: " + *latched_error_;
        }

        text.left = SanitizeBarText(text.left);
        text.right = SanitizeBarText(text.right);
        // 鍵を取り直す: 関数が(ctx 以外の読み取りで)状態を変えても、次のティックで、同じ内容のまま再実行しない。
        // ただし、関数の世代は、評価に使った世代のまま残す(関数が自分を差し替えたときは、次の Update で、新しい関数で組み直す)
        StatusKey retaken = make_key();
        retaken.function_generation = key.function_generation;
        last_key_ = std::move(retaken);
        return text;
    }
}
