#include "Format.h"
#include <format>
#include <sys/stat.h>

namespace miata {
    std::string FormatSize(uintmax_t bytes)
    {
        static const char* units[] = {"B", "K", "M", "G", "T"};
        double size = (double)bytes;
        int unit = 0;
        while (size >= 1024.0 && unit < 4) {
            size /= 1024.0;
            ++unit;
        }
        if (unit == 0) {
            return std::format("{}{}", (uintmax_t)size, units[unit]);
        }
        return std::format("{:.1f}{}", size, units[unit]);
    }

    std::string FormatGroupedBytes(uintmax_t bytes)
    {
        const std::string digits = std::to_string(bytes);
        std::string grouped;
        grouped.reserve(digits.size() + digits.size() / 3 + 2);
        for (size_t i = 0; i < digits.size(); ++i) {
            if (i > 0 && (digits.size() - i) % 3 == 0) grouped += ',';
            grouped += digits[i];
        }
        grouped += " B";
        return grouped;
    }

    std::string FormatLocalTime(std::time_t time)
    {
        std::tm tm{};
        // 範囲外の時刻は localtime_r が失敗して、tm が 0 のまま(strftime に null を渡さない)
        localtime_r(&time, &tm);
        char buf[64];
        const size_t n = std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        return std::string(buf, n);
    }

    std::string FormatMode(uint32_t mode)
    {
        char type = '?';
        switch (mode & S_IFMT) {
        case S_IFREG: type = '-'; break;
        case S_IFDIR: type = 'd'; break;
        case S_IFLNK: type = 'l'; break;
        case S_IFCHR: type = 'c'; break;
        case S_IFBLK: type = 'b'; break;
        case S_IFIFO: type = 'p'; break;
        case S_IFSOCK: type = 's'; break;
        default: break;
        }

        std::string s(10, '-');
        s[0] = type;
        s[1] = (mode & S_IRUSR) ? 'r' : '-';
        s[2] = (mode & S_IWUSR) ? 'w' : '-';
        s[3] = (mode & S_ISUID) ? ((mode & S_IXUSR) ? 's' : 'S') : ((mode & S_IXUSR) ? 'x' : '-');
        s[4] = (mode & S_IRGRP) ? 'r' : '-';
        s[5] = (mode & S_IWGRP) ? 'w' : '-';
        s[6] = (mode & S_ISGID) ? ((mode & S_IXGRP) ? 's' : 'S') : ((mode & S_IXGRP) ? 'x' : '-');
        s[7] = (mode & S_IROTH) ? 'r' : '-';
        s[8] = (mode & S_IWOTH) ? 'w' : '-';
        s[9] = (mode & S_ISVTX) ? ((mode & S_IXOTH) ? 't' : 'T') : ((mode & S_IXOTH) ? 'x' : '-');
        return s;
    }
}
