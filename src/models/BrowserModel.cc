#include "BrowserModel.h"
#include "../platform.h"

namespace miata::models {
    namespace {
        // 履歴の保存先のキー(NSUserDefaults)。ウィンドウ位置の自動保存("NSWindow Frame MiataMainWindow")とは別のキー
        constexpr const char* kHistoryKey = "MiataHistory";
    }

    BrowserModel::BrowserModel() : left_(&history_), right_(&history_)
    {
    }

    BrowserModel::~BrowserModel()
    {
    }

    void BrowserModel::RestoreHistory(size_t limit)
    {
        // 上限を先に決める(Restoreは上限で切る)
        history_.SetLimit(limit);
        history_.Restore(pl_load_string_list(kHistoryKey));
    }

    void BrowserModel::SaveHistoryIfChanged()
    {
        if (!history_.Dirty()) return;
        pl_save_string_list(kHistoryKey, history_.Entries());
        history_.MarkSaved();
    }
}
