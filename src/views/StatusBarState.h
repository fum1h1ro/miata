#ifndef VIEWS_STATUS_BAR_STATE_H__
#define VIEWS_STATUS_BAR_STATE_H__

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "../platform.h"

// ステータスバー(ウィンドウの下端。カーソル下のファイルの情報・マークの要約・キー入力の途中の次のキー候補)に、何を、いつ出すかを
// 決める。AppKit にも Lua にも依存しない(ProgressState と同じ作り)ので、AppKit なしでテストできる。
// 画面は StatusBar(.mm)、Lua の関数との受け渡しは StatusBarScript、材料を集めて配線するのは Application。
namespace miata::views {
    // 見えているマークの要約(FileListView::GetMarkSummary)
    struct MarkSummary {
        int count = 0;        // 見えているマークの数(フォルダ・リンクも含む)
        int dirs = 0;         // そのうち、フォルダの数(IsDirectory。フォルダへのリンクも)
        uintmax_t bytes = 0;  // ファイルの合計サイズ(フォルダ・リンクは 0。フォルダの中身は再帰しない)
        int hidden = 0;       // 見えていないマークの数(絞り込み・隠しファイルで外れた行)

        bool operator==(const MarkSummary&) const = default;
    };

    // カーソル下のファイル
    struct StatusEntry {
        std::string name;                  // 名前(Name()。NFC・修復済み)
        std::string path;                  // パス(Path()。元のバイト列)
        bool is_dir = false;               // IsDirectory(フォルダへのリンクも)
        bool is_link = false;              // シンボリックリンク
        bool is_alias = false;             // Finder のエイリアス
        std::optional<pl_file_info> info;  // lstat の結果。取れなければ nullopt(errorに、OS の説明)
        std::string error;
    };

    // 次のキー候補 1 つ
    struct StatusKeyCandidate {
        std::string key;    // 表示名("d" "<C-q>")
        std::string desc;   // 説明(bind の4番目の引数。無ければ空)
        bool leaf = false;  // true: このキーで実行される。false: さらに続きがある

        bool operator==(const StatusKeyCandidate&) const = default;
    };

    // キー入力の途中(前置きが保留中)
    struct StatusPending {
        std::string keys;                            // 入力済みの前置き("d" "<C-q>")
        std::vector<StatusKeyCandidate> candidates;  // 次の 1 打の候補

        bool operator==(const StatusPending&) const = default;
    };

    // バーを組み立てる材料(Application が集める。Lua の関数にも渡す)
    struct StatusSnapshot {
        std::string pane;                   // カーソルのペイン("left" / "right")
        std::string dir;                    // そのペインのフォルダ
        bool dialog = false;                // ダイアログが開いている
        int shown = 0;                      // 見えている行数
        int total = 0;                      // 絞り込む前の、出してよい行数
        std::optional<StatusEntry> entry;   // カーソル下。一覧が空なら nullopt
        MarkSummary marks;
        std::optional<StatusPending> pending;
    };

    // 画面に出す内容。左(カーソル下のファイル。前置きの途中は次のキー候補)と、右(マークの要約)
    struct StatusText {
        std::string left;
        std::string right;

        bool operator==(const StatusText&) const = default;
    };

    // Lua の関数が返す置き換え。書いた側だけ置き換える(書かなかった側は、既定のまま)
    struct StatusOverride {
        std::optional<std::string> left;
        std::optional<std::string> right;
    };

    // 既定の表示(カーソル下のファイル・マークの要約・次のキー候補の書式の、唯一の実装)。
    //  左: 「-rw-r--r--  k-ya:staff  12,345 B (12.1K)  2026-10-10 14:03:21」(フォルダはサイズ無し。リンクは末尾に「  -> リンク先」。
    //      ダウンロード前は「  <CLOUD>」、エイリアスは「  <ALIAS>」。一覧が空なら空。属性が取れなければ「(情報を取得できません: …)」)
    //  右: 「マーク 3 件 (フォルダ 1)  2.4M  (隠れ 2)」(マークも隠れたマークも無ければ空)
    //  前置きの途中は、左を次のキー候補に置き換える: 「d …  d マークをゴミ箱へ」「g …  g 先頭の行へ  s…」(右は、そのまま)
    StatusText DefaultStatusText(const StatusSnapshot& snapshot);

    // 画面に出す文字列にする: UTF-8 として不正なバイトを U+FFFD にし(NSString にできる形)、制御文字(NUL・改行・タブなど)を
    // 空白にして、長さを制限する(1024 バイト。文字の途中では切らない)。バーに出す文字列の、唯一の関門
    std::string SanitizeBarText(std::string_view text);

    // 画面に出す内容が変わりうるかを、安価に判定するための鍵。AppKit も Lua も触らずに作れるものだけ
    struct StatusKey {
        int pane = 0;                       // カーソルのペイン(0 左 / 1 右)
        // 各ペイン(0 左 / 1 右)の、カーソル下のファイルのパス(一覧が空なら空)と、一覧の世代(FileListView::ListEpoch)。
        // カーソルのペインだけでなく、反対側も入れる: 反対側のペインを、読むだけのコマンド(pane_path("right") など)で
        // 読む Lua の関数が、反対側の変化で、呼び直されるように
        std::array<std::string, 2> cursor_paths;
        std::array<uint64_t, 2> list_epochs{};
        uint64_t mark_epoch = 0;            // マークの世代(FileEntryModel::MarkEpoch。どのペインのマークでも進む)
        std::string pending;                // 前置き(キー列の表示名。無ければ空)
        bool dialog = false;
        uint64_t function_generation = 0;   // Lua の関数の世代(Config::StatusBarGeneration。set_statusbar のたびに増える)

        bool operator==(const StatusKey&) const = default;
    };

    // 鍵が変わったときだけ、材料を集めて、既定と(あれば)Lua の関数から、画面に出す内容を作る。
    // 時刻にも、画面にも、Lua の状態にも触らない(関数を渡す)ので、偽の関数で、呼び出しの回数・ラッチ・鍵の取り直しを試せる
    class StatusBarState {
    public:
        // 材料を集める(鍵が変わったときだけ呼ばれる)
        using Collect = std::function<StatusSnapshot()>;
        // Lua の関数に、材料と既定の表示を渡して、置き換えを受け取る(失敗は、エラーの文面)
        using CallScript = std::function<std::expected<StatusOverride, std::string>(const StatusSnapshot&, const StatusText&)>;

        // 毎ティックと、KeyDown の出口から呼ぶ。鍵が前回と同じなら、何も呼ばずに nullopt(何も変わらない)。
        // 違えば、collect → 既定 → Lua(has_function が真で、エラーでラッチされていなければ script)→ 整形(SanitizeBarText)の順で、
        // 画面に出す内容を返す。
        //  - 関数がエラーを返したら、その関数の世代が変わるまで(set_statusbar で差し替えるまで)、script を呼ばない(ラッチ)。
        //    左に「Lua: <エラー>」を出し続ける(右は既定)。ただし前置きの途中は、候補を優先する(既定の左のまま)
        //  - make_key は、script を呼んだ後にもう一度呼んで、鍵を取り直す(関数が状態を変えても、同じ内容のまま
        //    次のティックで再実行しない)。ただし関数の世代だけは、評価に使った世代を残す(関数が自分を差し替えたら、
        //    次の Update で、新しい関数で組み直す)
        std::optional<StatusText> Update(
            const std::function<StatusKey()>& make_key,
            bool has_function,
            const Collect& collect,
            const CallScript& script
        );

        // 前回の鍵を忘れる(次の Update は、鍵が同じでも、必ず組み直す)。バーを隠している間は Update を呼ばない(材料の収集も
        // Lua の関数の呼び出しもしない)ので、出し直したとき、隠している間に変わったことを、すぐ反映するために使う。
        // エラーのラッチは、そのまま(関数の世代が変わるまで、関数は呼ばない)
        void Invalidate()
        {
            last_key_.reset();
        }

        // ラッチ中のエラー(無ければ nullopt)。テストと、診断用
        const std::optional<std::string>& LatchedError() const
        {
            return latched_error_;
        }

    private:
        std::optional<StatusKey> last_key_;
        std::optional<std::string> latched_error_;
        uint64_t latched_generation_ = 0;
    };
}

#endif // VIEWS_STATUS_BAR_STATE_H__
