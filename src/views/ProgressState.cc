#include "ProgressState.h"
#include <algorithm>
#include <cmath>
#include <format>
#include "../Utf8.h"

namespace miata::views {
    namespace {
        using Seconds = std::chrono::duration<double>;

        // 不定のバーの区間の位置(0〜1)。周期を kProgressPulseSteps 段に量子化する。整数(ミリ秒)で計算する
        // (浮動小数点だと、段の境目で 1 段ずれることがある)
        double PulseAt(ProgressClock::duration elapsed)
        {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            auto period = kProgressPulsePeriod.count();
            auto step = (ms * kProgressPulseSteps / period) % kProgressPulseSteps;
            return static_cast<double>(step) / kProgressPulseSteps;
        }
    }

    std::optional<double> ProgressFraction(const FileOperationProgress& progress)
    {
        if (progress.phase == FileOperationProgress::Phase::Preparing) return std::nullopt;
        if (progress.total_bytes > 0) {
            return std::clamp(static_cast<double>(progress.done_bytes) / static_cast<double>(progress.total_bytes), 0.0, 1.0);
        }
        if (progress.total_items > 1) {
            return std::clamp(static_cast<double>(progress.done_items) / static_cast<double>(progress.total_items), 0.0, 1.0);
        }
        return std::nullopt;
    }

    std::string ProgressTitleText(FileOpType type, const FileOperationProgress& progress, std::optional<double> fraction, bool finished)
    {
        const char* label = FileOpLabel(type);
        if (finished) return std::format("{}  完了", label);
        if (progress.phase == FileOperationProgress::Phase::Preparing) return std::format("{}  準備中", label);

        std::string parts;
        if (progress.total_items > 1) {
            // 何件目か。終えた数 + 1(いま処理している項目)。全部終えた後の写しでも、項目数を超えない
            auto n = std::min(progress.done_items + 1, progress.total_items);
            parts = std::format("{}/{}", n, progress.total_items);
        }
        if (fraction) {
            // 100 は、完了のときだけ(処理が終わる直前に 100% と出して、続きがあるように見えるのを避ける)
            auto percent = std::min(99, static_cast<int>(std::floor(*fraction * 100.0)));
            if (!parts.empty()) parts += " · ";
            parts += std::format("{}%", percent);
        }
        return parts.empty() ? std::string(label) : std::format("{}  {}", label, parts);
    }

    void ProgressState::Update(ProgressClock::time_point now, const std::vector<FileOperationStatus>& running)
    {
        for (auto& status : running) {
            auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.id == status.id; });
            if (it == entries_.end()) {
                entries_.push_back(Entry{.id = status.id, .type = status.type, .started_at = status.started_at, .progress = status.progress, .finished_at = std::nullopt});
            }
            else if (!it->finished_at) {
                it->progress = status.progress; // 完了の通知を受けた後の、古い進捗で、完了の表示を壊さない
            }
        }
        std::erase_if(entries_, [&](const Entry& e) {
            if (e.finished_at) return now - *e.finished_at >= kProgressDoneHold + kProgressFadeOut; // 完了の表示の期限切れ
            // 実行中でも、完了の通知を受けたのでもない(通知なしに居なくなった)
            return std::none_of(running.begin(), running.end(), [&](const FileOperationStatus& s) { return s.id == e.id; });
        });
    }

    void ProgressState::Finish(FileOperationId id, bool succeeded, ProgressClock::time_point now)
    {
        auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.id == id; });
        if (it == entries_.end()) return;
        if (!succeeded || now - it->started_at < kProgressShowDelay) {
            entries_.erase(it); // 失敗はダイアログで知らせる。出す前に終わったものも、何も出さない
            return;
        }
        it->finished_at = now;
    }

    std::vector<ProgressPanel> ProgressState::Panels(ProgressClock::time_point now) const
    {
        std::vector<ProgressPanel> panels;
        for (auto& e : entries_) {
            // 時刻が戻っても(開始より前の時刻を渡されても)、下の alpha が 0 以下になるので、何も出ない
            auto elapsed = now - e.started_at;
            // 出始めのフェードイン(遅延が明けてから)。完了のパネルにも掛ける(0.3 秒の直後に終わっても、突然現れない)
            double alpha = std::clamp(Seconds(elapsed - kProgressShowDelay).count() / Seconds(kProgressFadeIn).count(), 0.0, 1.0);
            if (e.finished_at) {
                auto since = now - *e.finished_at;
                // 期限を過ぎると、alpha が 0 以下になって、下で飛ばされる
                if (since > kProgressDoneHold) alpha *= 1.0 - Seconds(since - kProgressDoneHold).count() / Seconds(kProgressFadeOut).count();
            }
            if (alpha <= 0) continue;

            ProgressPanel panel;
            panel.id = e.id;
            panel.alpha = alpha;
            if (e.finished_at) {
                panel.title = ProgressTitleText(e.type, e.progress, std::nullopt, true);
                panel.fraction = 1.0;
                panel.detail = std::format("{}件", e.progress.total_items);
            }
            else {
                panel.fraction = ProgressFraction(e.progress);
                panel.title = ProgressTitleText(e.type, e.progress, panel.fraction, false);
                panel.pulse = panel.fraction ? 0.0 : PulseAt(elapsed);
                panel.detail = RepairUtf8(e.progress.current_name); // NSString にできない名前で、ラベルの作成が落ちないように
            }
            panels.push_back(std::move(panel));
        }
        return panels;
    }
}
