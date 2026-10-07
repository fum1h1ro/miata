#ifndef VIEWS_PROGRESS_STATE_H__
#define VIEWS_PROGRESS_STATE_H__

#include <chrono>
#include <optional>
#include <string>
#include <vector>
#include "../FileOperationProgress.h"

namespace miata::views {
    using ProgressClock = std::chrono::steady_clock;

    // 進捗パネルの時間。テストが参照する
    inline constexpr std::chrono::milliseconds kProgressShowDelay{300};    // 操作を始めてから、パネルを出すまで(それより早く終われば、何も出さない)
    inline constexpr std::chrono::milliseconds kProgressFadeIn{150};       // 出始めの、フェードイン
    inline constexpr std::chrono::milliseconds kProgressDoneHold{1000};    // 完了を、そのまま見せる時間
    inline constexpr std::chrono::milliseconds kProgressFadeOut{400};      // 完了を見せた後の、フェードアウト
    inline constexpr std::chrono::milliseconds kProgressPulsePeriod{1200}; // 割合が不定のときの、バーの区間の往復の周期
    // 周期を、この数の段に量子化する(ティック 0.05 秒 × 24 = 1.2 秒。同じ段の間は、同じ値になるので、再描画を起こさない)
    inline constexpr int kProgressPulseSteps = 24;

    // 画面に出すパネル 1 枚の内容(AppKit 非依存)。ProgressOverlay が描く。同じ内容なら、画面に触らない(operator==)
    struct ProgressPanel {
        FileOperationId id = 0;
        std::string title;              // 1 行目。「コピー  3/12 · 45%」
        std::optional<double> fraction; // 0〜1。nullopt = 不定(バーの区間を動かす)
        double pulse = 0;               // 不定のときの、区間の位置(0〜1 を kProgressPulseSteps 段に量子化)。確定のときは 0
        std::string detail;             // 3 行目。いま処理している名前(UTF-8 として正しい形)。完了のときは件数
        double alpha = 1;               // 0 より大きく 1 以下(フェード)

        bool operator==(const ProgressPanel&) const = default;
    };

    // 進捗パネルに「何を、いつ、どう出すか」を決める。実行中の操作(FileOperationManager::Running())と、完了の通知
    // (FileOperationManager のコールバック)を受けて、パネルの並びを作る。時刻は引数で受ける(テストが、実時間を待たない)。
    // 完了した操作は、FileOperationManager の側からは消える(Update が jobs_ から外す)ので、「完了」を見せる間の状態は、ここが持つ。
    class ProgressState {
    public:
        // 毎ティック。running は、実行中の操作の写し(開始順)。初めて見た操作は、末尾に足す。running にも無く、完了の通知も
        // 受けていない操作(通知なしに居なくなったもの)と、完了の表示の期限が切れたものは、捨てる
        void Update(ProgressClock::time_point now, const std::vector<FileOperationStatus>& running);

        // 操作が終わった(FileOperationManager のコールバックから)。成功なら、「完了」を見せて、消える。失敗はダイアログで知らせるので、
        // パネルは、その場で消す。パネルを出す前(kProgressShowDelay の前)に終わった操作も、何も出さない。知らない id は無視する
        // (開始してから、最初の Update までに終わった操作)
        void Finish(FileOperationId id, bool succeeded, ProgressClock::time_point now);

        // いま出すパネルの並び(初出の順。完了したものも、期限まで同じ位置に留まる)。純粋(状態を変えない)
        std::vector<ProgressPanel> Panels(ProgressClock::time_point now) const;

    private:
        struct Entry {
            FileOperationId id = 0;
            FileOpType type = FileOpType::Copy;
            ProgressClock::time_point started_at;
            FileOperationProgress progress; // 最後に見た値
            std::optional<ProgressClock::time_point> finished_at;
        };
        std::vector<Entry> entries_;
    };

    // 割合(0〜1)。準備中は不定。バイトの合計が分かればバイト、そうでなく項目が 2 つ以上なら項目数、それ以外(項目が 1 つで
    // バイトが不明)は不定(nullopt)。1 を超えない。(Panels の部品。テストが、直接呼ぶ)
    std::optional<double> ProgressFraction(const FileOperationProgress& progress);

    // 1 行目の文言。「コピー  3/12 · 45%」(項目が 1 つなら n/N は出さない。割合が不定なら % は出さない)。実行中の % は、
    // 99 を上限にする(100 は、完了のときだけ)。準備中は「コピー  準備中」、完了は「コピー  完了」。(Panels の部品。テストが、直接呼ぶ)
    std::string ProgressTitleText(FileOpType type, const FileOperationProgress& progress, std::optional<double> fraction, bool finished);
}

#endif // VIEWS_PROGRESS_STATE_H__
