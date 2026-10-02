#ifndef MODELS_BROWSER_MODEL_H__
#define MODELS_BROWSER_MODEL_H__

#include "FileEntryModel.h"
#include "FileListModel.h"

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

        // 左右それぞれの履歴(FileListModel::History)に、上限を渡して、前回までの記録を復元する。上限はConfig
        // (Lua設定)の値だが、モデルはConfigを知らないので、外(Application)から渡す。設定の読み込みの後、
        // 最初の移動より前に一度だけ呼ぶ(それまでの上限は0で、何も記録しない)。上限を縮めた、保存してあった
        // データが壊れていた、などで整えたときは、次のSaveHistoryIfChanged()で保存し直す。上限0は、保存してあった
        // 履歴も消す(履歴オフにしたのに、パスがディスクに残らないように)。
        void RestoreHistory(size_t limit);
        // 変更のあった履歴を保存する(NSUserDefaults)。毎ティック呼んでよい(変わっていなければ何もしない)。
        // 終了時にまとめて保存する処理は無い(強制終了でも残るように)ので、ティックごとに、変更をまとめて保存する。
        void SaveHistoryIfChanged();

    private:
        BrowserModel();
        virtual ~BrowserModel();

        FileListModel left_;
        FileListModel right_;

    };
}

#endif // MODELS_BROWSER_MODEL_H__
