#ifndef FILE_ERROR_H__
#define FILE_ERROR_H__

#include <string>
#include <system_error>

namespace miata {
    // ファイル操作の失敗。画面に見せる説明(message)に加えて、「権限が無いことによる失敗か」を持つ。
    // macOSでは、権限が無い失敗(EPERM / EACCES)の多くが、OSの保護(プライバシーとセキュリティ)によるもので、
    // システム設定で許可すれば解消できる。画面では、その許可のしかたを案内する(View::ReportFileError)。
    struct FileError {
        std::string message;
        bool permission_denied = false;

        // std::filesystemなどのエラーコードから作る。EPERM(Operation not permitted。OSの保護・ロックされた
        // ファイル)とEACCES(Permission denied。アクセス権)を、どちらも権限の失敗とみなす
        static FileError From(const std::error_code& ec)
        {
            return FileError{
                .message = ec.message(),
                .permission_denied = ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted,
            };
        }
    };

    // 複数のファイルの操作の失敗を、1つにまとめる(件数と、画面に見せる1件の説明)。
    // 権限による失敗があれば、その説明を残す(案内の対象になる失敗を、別の失敗で隠さないため)。
    struct FileErrorSummary {
        int count = 0;
        FileError shown;

        void Add(const FileError& error)
        {
            ++count;
            if (error.permission_denied || !shown.permission_denied) shown = error;
        }
    };
}

#endif // FILE_ERROR_H__
