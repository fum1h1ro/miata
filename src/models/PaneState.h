#ifndef MODELS_PANE_STATE_H__
#define MODELS_PANE_STATE_H__

#include <map>
#include <string>
#include <string_view>

namespace miata::models {
    // 1つのペインの、起動をまたいで覚えておく状態(いるフォルダと、ソート)。AppKit・Lua・Configのどれも知らない純粋な
    // ロジックで、保存用の辞書との変換と、保存してあった値の検査だけを持つ(PathHistoryと同じ作り)。保存(NSUserDefaults)と
    // 復元は、Viewが行う。
    struct PaneState {
        std::string path;              // いるフォルダ(平らな絶対パスで、末尾の/なし)。空なら、復元しない(ホームのまま)
        std::string sort_key = "name"; // "name" / "size" / "mtime" / "ext"(Luaのsortと同じ名前。意味はFileListViewが知っている)
        bool sort_reverse = false;

        bool operator==(const PaneState&) const = default;

        // 保存用の辞書(名前は path / sort / reverse)。
        std::map<std::string, std::string> ToMap() const;
        // 保存してあった辞書から作る。欠けた・壊れた値は、既定にする: pathは、IsPlainAbsolutePathに合うものだけ(末尾の/は
        // 除く。合わなければ空)。sortは、空でなければそのまま(名前が正しいかは、意味を知っている呼ぶ側が調べる)。
        // reverseは、"1" のときだけtrue。
        static PaneState FromMap(const std::map<std::string, std::string>& saved);

        // 平らな絶対パスか: 先頭が/で、"."・".."・空の要素(//)が無い(末尾の/は許す)。NULを含むと偽。
        // NavigateToParentはparent_path()(字句上の親)を使うので、/a/b/.. のようなパスに入ると、hが /a ではなく /a/b
        // へ移ってしまう。そのようなパスは、ペインのいる場所にしない(Luaのjump_toの入口と、保存してあった値の検査で使う)。
        static bool IsPlainAbsolutePath(std::string_view path);
    };
}

#endif // MODELS_PANE_STATE_H__
