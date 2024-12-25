#include <iostream>
#include <filesystem>
#include <chrono>
#include <format>
#include <ctime>
#include "FileEntryModel.h"
#include "../platform.h"

namespace miata::models {
    FileEntryModel::FileEntryModel(const std::filesystem::directory_entry& entry)
    {
        //printf("entry: %s\n", entry.path().string().c_str());
        raw_ = entry;
        flags_.clear();
        auto filename = entry.path().filename();
        name_ = pl_normalize_string(filename.string());
        basename_ = filename.stem().string();
        ext_ = filename.extension().string();
        if (raw_.exists()) {
            auto t = std::filesystem::last_write_time(entry);
            mtime_ = format_time(t);
        }
    }

    std::string FileEntryModel::format_time(const std::filesystem::file_time_type& time)
    {
        auto sys_time = std::chrono::file_clock::to_sys(time);
        auto sctp_system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(sys_time);
        std::time_t cftime = std::chrono::system_clock::to_time_t(sctp_system);
        size_t sz = 32;
        std::string buf(sz, '\0');
        // 時刻をフォーマットして出力
        while (0 == std::strftime(&buf[0], sz, "%Y-%m-%d %H:%M:%S", std::localtime(&cftime))) {
            sz *= 2;
            buf.resize(sz);
        }
        return buf;
    }








}
