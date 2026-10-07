#ifndef FILE_OPERATION_PROGRESS_H__
#define FILE_OPERATION_PROGRESS_H__

#include <chrono>
#include <cstdint>
#include <string>

namespace miata {
    enum class FileOpType {
        Copy,
        Move,
    };

    // 画面に出す、操作の名前
    inline const char* FileOpLabel(FileOpType type)
    {
        return type == FileOpType::Copy ? "コピー" : "移動";
    }

    // コピー・移動の 1 回の操作を区別する番号(FileOperationManager::Start が付ける。プロセスの中で、使い回さない)。
    // Running() の写しと、完了のコールバック(FileOperationCompleted::id)が、同じ操作かどうかを、これで見分ける
    using FileOperationId = std::uint64_t;

    // 実行中の操作の進み具合(事実だけ)。裏スレッドが書き(FileOperationManager の Job が mutex で守る)、メインスレッドは写しを読む。
    // 画面に出す形(文言・割合・遅延・フェード)は、views/ProgressState が決める。
    struct FileOperationProgress {
        enum class Phase : std::uint8_t {
            Preparing, // 合計を調べている(合計が未定)
            Running,
        };
        Phase phase = Phase::Running;
        std::int64_t total_items = 0; // 項目 = ユーザーが選んだもの(フォルダの中身は数えない)
        std::int64_t done_items = 0;  // 終えた項目の数(いま処理している項目は含まない)
        std::int64_t total_bytes = 0; // 0 = 分からない(割合は、項目数から出す)
        std::int64_t done_bytes = 0;
        std::string current_name;     // いま処理している名前(ファイル名のバイト列のまま。UTF-8 として不正なことがある)
    };

    // FileOperationManager::Running() が返す、実行中の操作 1 つの写し
    struct FileOperationStatus {
        FileOperationId id = 0;
        FileOpType type = FileOpType::Copy;
        std::chrono::steady_clock::time_point started_at;
        FileOperationProgress progress;
    };
}

#endif // FILE_OPERATION_PROGRESS_H__
