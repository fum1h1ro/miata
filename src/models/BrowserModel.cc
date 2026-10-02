#include "BrowserModel.h"
#include <array>
#include <utility>
#include "../platform.h"

namespace miata::models {
    namespace {
        // 履歴の保存先のキー(NSUserDefaults)。ペインごとに別のキーにする(片方が壊れても、もう片方に影響しない)。
        // ウィンドウ位置の自動保存("NSWindow Frame MiataMainWindow")とは別のキー
        constexpr const char* kHistoryKeyLeft = "MiataHistoryLeft";
        constexpr const char* kHistoryKeyRight = "MiataHistoryRight";

        // 左右の一覧と、その履歴の保存先のキー
        std::array<std::pair<FileListModel*, const char*>, 2> HistoryTargets(FileListModel& left, FileListModel& right)
        {
            return {{
                {&left, kHistoryKeyLeft},
                {&right, kHistoryKeyRight},
            }};
        }
    }

    BrowserModel::BrowserModel()
    {
    }

    BrowserModel::~BrowserModel()
    {
    }

    void BrowserModel::RestoreHistory(size_t limit)
    {
        for (auto& [list, key] : HistoryTargets(left_, right_)) {
            // 上限を先に決める(Restoreは上限で切る)
            list->History().SetLimit(limit);
            list->History().Restore(pl_load_string_list(key));
        }
    }

    void BrowserModel::SaveHistoryIfChanged()
    {
        for (auto& [list, key] : HistoryTargets(left_, right_)) {
            auto& history = list->History();
            if (!history.Dirty()) continue;
            pl_save_string_list(key, history.Entries());
            history.MarkSaved();
        }
    }
}
