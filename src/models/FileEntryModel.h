#ifndef MODELS_FILE_ENTRY_MODEL_H__
#define MODELS_FILE_ENTRY_MODEL_H__

#include <stdint.h>
#include <string>
#include <vector>
#include <filesystem>
#include "../misc.h"

namespace miata::models {
    class FileEntryModel {
        friend class FileListModel;
    public:
        enum class flags : uint8_t {
            none = 0,
            marked = 1<<1,
        };

        FileEntryModel()
        {
            flags_.clear();
        }
        inline bool IsValid() const
        {
            return raw_.exists();
        }
        inline const std::string& Name() const
        {
            return name_;
        }
        inline std::filesystem::path Path() const
        {
            return raw_.path();
        }
        inline const std::string& Basename() const
        {
            return basename_;
        }
        inline const std::string& Ext() const
        {
            return ext_;
        }
        inline uintmax_t Size() const
        {
            if (IsDirectory()) return 0;
            return raw_.file_size();
        }
        inline const std::string& ModifiedTime() const
        {
            return mtime_;
        }
        inline bool IsDirectory() const
        {
            return raw_.is_directory();
        }
        inline bool IsSymlink() const
        {
            return raw_.is_symlink();
        }
        inline bool IsMarked() const
        {
            return flags_.is(flags::marked);
        }
        inline void Mark(bool marked)
        {
            flags_.set(flags::marked, marked);
        }
    private:
        FileEntryModel(const std::filesystem::directory_entry& entry);
        std::string format_time(const std::filesystem::file_time_type& time);
        //
        std::filesystem::directory_entry raw_;
        std::string name_;
        std::string basename_;
        std::string ext_;
        std::string mtime_;
        misc::Flags<flags> flags_;
    };

}

#endif // MODELS_FILE_ENTRY_MODEL_H__
