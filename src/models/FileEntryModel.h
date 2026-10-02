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
            std::error_code ec;
            return raw_.exists(ec);
        }
        // 名前(NFC)。常にUTF-8として正しい: ファイルシステムの名前がUTF-8として不正なとき(ネットワークボリュームなど)は、
        // 不正なバイトをU+FFFDに置き換えてある。そのため、この名前でパスを作ると、そのファイルを指さない
        // (元のバイト列のパスは Path())。Basename()・Ext()も同じ
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
        // 破損したシンボリックリンクや特殊なシステムファイル(例: macOSルート直下の
        // .VolumeIcon.icns 等)はディレクトリ一覧には出てくるがstatに失敗することがあるため、
        // 例外を投げないerror_code版を使い、失敗時は0/falseにフォールバックする。
        inline uintmax_t Size() const
        {
            if (IsDirectory()) return 0;
            std::error_code ec;
            auto size = raw_.file_size(ec);
            return ec ? 0 : size;
        }
        inline const std::string& ModifiedTime() const
        {
            return mtime_;
        }
        inline bool IsDirectory() const
        {
            std::error_code ec;
            return raw_.is_directory(ec);
        }
        inline bool IsSymlink() const
        {
            std::error_code ec;
            return raw_.is_symlink(ec);
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
