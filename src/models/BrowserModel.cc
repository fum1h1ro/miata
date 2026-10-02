#include "BrowserModel.h"
#include "../platform.h"

namespace miata::models {
    namespace {
        // 履歴の保存先のキー(NSUserDefaults)。ウィンドウ位置の自動保存("NSWindow Frame MiataMainWindow")とは別のキー
        constexpr const char* kHistoryKey = "MiataHistory";
        // 以前の版が、ペインごとの履歴を保存していたキー。今は使わない(RestoreHistoryが、あれば引き継いで消す)
        constexpr const char* kLegacyHistoryKeyLeft = "MiataHistoryLeft";
        constexpr const char* kLegacyHistoryKeyRight = "MiataHistoryRight";
    }

    BrowserModel::BrowserModel() : left_(&history_), right_(&history_)
    {
    }

    BrowserModel::~BrowserModel()
    {
    }

    void BrowserModel::RestoreHistory(size_t limit)
    {
        auto saved = pl_load_string_list(kHistoryKey);

        // 以前の版の保存データ(ペインごと)。左右の新旧は記録に無いので、左、右の順につなぐ(重複はRestoreが捨てる)
        auto legacy = pl_load_string_list(kLegacyHistoryKeyLeft);
        auto legacy_right = pl_load_string_list(kLegacyHistoryKeyRight);
        legacy.insert(legacy.end(), legacy_right.begin(), legacy_right.end());
        bool had_legacy = !legacy.empty();
        if (saved.empty()) saved = std::move(legacy); // 新しいキーに保存したものが無ければ、引き継ぐ。あれば、古いデータは使わない

        // 上限を先に決める(Restoreは上限で切る)
        history_.SetLimit(limit);
        history_.Restore(saved);

        // 古いキーを消す。引き継いだ分は、新しいキーに保存しておく(新しいキーのデータが無いときは、ここで初めて書く)
        if (had_legacy) {
            pl_save_string_list(kHistoryKey, history_.Entries());
            pl_save_string_list(kLegacyHistoryKeyLeft, {});
            pl_save_string_list(kLegacyHistoryKeyRight, {});
            history_.MarkSaved();
        }
    }

    void BrowserModel::SaveHistoryIfChanged()
    {
        if (!history_.Dirty()) return;
        pl_save_string_list(kHistoryKey, history_.Entries());
        history_.MarkSaved();
    }
}
