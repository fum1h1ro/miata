#ifndef MODELS_PATH_HISTORY_H__
#define MODELS_PATH_HISTORY_H__

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace miata::models {
    // ペインが移動したフォルダの履歴。新しい順で、同じフォルダは1回だけ(また行ったら先頭へ移す)、件数に上限がある。
    // AppKit・Lua・Configのどれも知らない純粋なロジックで、左右のペインで1つを共有する(BrowserModelが持つ。SearchStateと
    // 同じ作り)。
    // 記録はFileListModel::TryJumpToの成功経路でだけ行う。保存(永続化)はここではせず、保存していない変更があるか
    // (Dirty)だけを持つ。
    class PathHistory {
    public:
        // 履歴に入れる形に整える。絶対パスで、改行・CR・NULを含まないものだけが対象(FzfFilterは候補を改行で区切るので、
        // 含まれると候補が割れて壊れる)。末尾の/は除く(std::filesystem::pathは "/tmp/" と "/tmp" を別物として比べる)。
        // 対象外はnullopt。整えるのはこれだけ: lexically_normalは、シンボリックリンクを経由した ".." を壊すので使わない。
        // NFCにもしない(画面の操作で作られるパスは、一覧の名前=NFCで組み立てるので、同じフォルダの綴りは揺れない)。
        static std::optional<std::string> Key(const std::filesystem::path& dir);
        // 末尾の/を除く(ルートの "/" は残す)。移動先のパスを、履歴と同じ形にそろえるのにも使う
        static std::string TrimTrailingSlash(std::string path);

        // 上限(件数)。0なら何も記録しない。縮めたときは古い方から捨てて、広げても捨てたものは戻らない。
        // 注入されるまでは0(ここはConfigを知らないので、設定の値は外から渡す)。
        void SetLimit(size_t limit);
        size_t Limit() const { return limit_; }

        // dirを先頭にする(すでにあれば移す)。先頭と同じなら何もしない。上限が0のときや、Key()が無いとき
        // (相対パスなど)も何もしない。
        void Record(const std::filesystem::path& dir);
        // dirを履歴から外す。あったらtrue。
        bool Remove(const std::filesystem::path& dir);
        // 表示用: 新しい順で、currentのフォルダを除いたもの。
        std::vector<std::string> List(const std::filesystem::path& current) const;

        // 保存してあったもの(新しい順)で置き換える。Key()が無い項目と重複(先に出た方を残す)を捨て、上限で切る。
        // 整えた結果が保存してあったものと違えば、保存し直すべきなのでDirtyになる(上限を縮めた、手で編集された、など)。
        void Restore(const std::vector<std::string>& saved);
        // 保存用: 新しい順。今いるフォルダも含む。
        const std::vector<std::string>& Entries() const { return entries_; }

        // 保存していない変更があるか。保存したらMarkSaved()を呼ぶ。
        bool Dirty() const { return dirty_; }
        void MarkSaved() { dirty_ = false; }

    private:
        // 上限を超えた分(古い方)を捨てる。捨てたらtrue
        bool Trim();

        std::vector<std::string> entries_; // 先頭が最新
        size_t limit_ = 0;
        bool dirty_ = false;
    };
}

#endif // MODELS_PATH_HISTORY_H__
