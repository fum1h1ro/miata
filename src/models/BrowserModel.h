#ifndef MODELS_BROWSER_MODEL_H__
#define MODELS_BROWSER_MODEL_H__

#include "FileEntryModel.h"
#include "FileListModel.h"
#include "PathHistory.h"

namespace miata::models {
    class BrowserModel {
    public:
        static inline BrowserModel& Instance()
        {
            static BrowserModel instance;
            return instance;
        }

        inline FileListModel& Left()
        {
            return left_;
        }

        inline FileListModel& Right()
        {
            return right_;
        }

        // 左右のペインが移動したフォルダの履歴。ペインごとではなく、1つにまとめて持つ(どちらのペインで行ったフォルダも、
        // どちらのペインからでも選べる)。記録は、左右のFileListModel::TryJumpToの成功経路から、この履歴に行う。
        PathHistory& History()
        {
            return history_;
        }
        // 履歴に、上限を渡して、前回までの記録を復元する。上限はConfig(Lua設定)の値だが、モデルはConfigを知らない
        // ので、外(Application)から渡す。設定の読み込みの後、最初の移動より前に一度だけ呼ぶ(それまでの上限は0で、
        // 何も記録しない)。上限を縮めた、保存してあったデータが壊れていた、などで整えたときは、次の
        // SaveHistoryIfChanged()で保存し直す。上限0は、保存してあった履歴も消す(履歴オフにしたのに、パスが
        // ディスクに残らないように)。以前の版は、ペインごとに別のキーで保存していたので、あれば1つにまとめて引き継ぐ。
        void RestoreHistory(size_t limit);
        // 変更があれば、履歴を保存する(NSUserDefaults)。毎ティック呼んでよい(変わっていなければ何もしない)。
        // 終了時にまとめて保存する処理は無い(強制終了でも残るように)ので、ティックごとに、変更をまとめて保存する。
        void SaveHistoryIfChanged();

    private:
        BrowserModel();
        virtual ~BrowserModel();

        // left_ / right_ より前に宣言する(両方のコンストラクタにこの履歴を渡す。メンバーは宣言順に構築される)
        PathHistory history_;
        FileListModel left_;
        FileListModel right_;

    };
}

#endif // MODELS_BROWSER_MODEL_H__
