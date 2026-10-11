#ifndef FORMAT_H__
#define FORMAT_H__

#include <cstdint>
#include <ctime>
#include <string>

// 表示用の書式(AppKit非依存の純関数)。一覧の右の欄(FileListView)と、ステータスバーが共有する。
namespace miata {
    // ls -lh 風の簡易フォーマット("512B" "1.2K" "3.4M")。将来的に外部指定できるようにするまでの固定実装
    std::string FormatSize(uintmax_t bytes);
    // 3桁区切りのバイト数("12,345 B")
    std::string FormatGroupedBytes(uintmax_t bytes);
    // ローカルタイムの "%Y-%m-%d %H:%M:%S"(一覧の更新日時と同じ書式)
    std::string FormatLocalTime(std::time_t time);
    // ls -l 風の、種類と権限("-rw-r--r--" "drwxr-xr-x" "lrwxr-xr-x")。modeは st_mode(setuid/setgid/stickyは ls と同じ s S t T)
    std::string FormatMode(uint32_t mode);
}

#endif // FORMAT_H__
