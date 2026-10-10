#ifndef CONFIG_H__
#define CONFIG_H__

#include "platform.h"
#include <format>
#include <functional>
#include <map>
#include <vector>
#include <expected>
extern "C" {
#include "lua.h"
}

#define CONFIG_COLOR_LIST \
    X(Background, background) \
    X(NormalText, normal_text) \
    X(NormalFile, normal_file) \
    X(Directory, directory) \
    X(SearchMatch, search_match) \
    X(SearchCurrent, search_current) \
    X(FilterMatch, filter_match) \
    X(Symlink, symlink) \
    X(Alias, alias) \
    X(Cloud, cloud) \



namespace miata {
    class Config {
    public:
        class Color {
            friend class Config;
        public:
            enum class Type : int32_t {
#define X(name, var) name,
                CONFIG_COLOR_LIST
#undef X
            };

            const Color4f& Get(Type type) const
            {
                return values_[(size_t)type];
            }
            std::expected<Color4f, std::string> Get(std::string type) const
            {
                auto idx = Find(type);
                if (!idx) return std::unexpected(idx.error());
                return values_[(size_t)idx.value()];
            }
            void Set(Type type, const Color4f& value)
            {
                values_[(size_t)type] = value;
            }
            std::expected<bool, std::string> Set(std::string type, const Color4f& value)
            {
                auto idx = Find(type);
                if (!idx) return std::unexpected(idx.error());
                values_[(size_t)idx.value()] = value;
                return true;
            }
            static std::string ToString(const Color4f& v);
            static std::expected<Color4f, std::string> FromString(const std::string_view& v);

        private:
            Color();
            std::expected<Type, std::string> Find(std::string type) const
            {
                auto t = table_.find(type);
                if (t == table_.end()) return std::unexpected(std::format("type not found: {}", type));
                return (*t).second;
            }

            static const char* names_[];
            std::vector<Color4f> values_;
            std::map<std::string, Type> table_;
        };



        static void ScriptInitialize();
        static inline Color& Color()
        {
            return Instance().color_;
        }

        // フォントファミリー名。未指定(空文字列)の場合はシステムデフォルトフォントを使う。
        static inline const std::string& FontFamily()
        {
            return Instance().font_family_;
        }

        // ファイル一覧本体の基準フォントサイズ(pt)。ヘッダーはこれより少し大きいサイズを使う。
        static inline float FontSize()
        {
            return Instance().font_size_;
        }

        // フォルダの履歴の上限(件数。左右のペインで1つの履歴を共有するので、合わせて)。0なら記録しない。
        // Miata.config.set_history_limit(n)で指定する。上限を大きくするほど、移動のたびに保存する量と、履歴を
        // 開いたときのfzfに渡す量が増える。今いるフォルダも履歴に数えるので、履歴を開いたときの一覧に出るのは、
        // 最大でn-1件(移動先のペインの今いるフォルダが履歴にあれば、それを除くため)。
        static inline size_t HistoryLimit()
        {
            return Instance().history_limit_;
        }
        // set_history_limitに指定できる最大(0〜この値の整数)
        static constexpr int kHistoryLimitMax = 10000;

        // ファイル名の頭に、ファイルのアイコン(Finderと同じ)を出すか。既定は出す。Miata.config.set_show_icons(bool)で
        // 指定する。ほかの設定と違い、実行中にも変えられる(Miata.config.set_show_icons を、実行中に呼ぶ)ので、描くたびに読むこと
        // (構築時にキャッシュしない)。値を書くのは、Luaの set_show_icons だけ(下の通知が、ビューを描き直す)。
        static inline bool ShowIcons()
        {
            return Instance().show_icons_;
        }

        // 実行中に、Luaの Miata.config.set_show_icons / set_show_hidden が、値を**変えた**ときに呼ぶ通知(同じ値を指定したときは、
        // 呼ばない)。値を書いた後に呼ぶ。Configはビューを知らないので、Applicationが、Viewを作った後に登録する。
        // 設定の読み込み中(登録前)の set_* は、値を書くだけ(ビューが無い。起動時の一覧は、その値で作られる)。
        static void SetShowIconsObserver(std::function<void()> observer)
        {
            Instance().show_icons_observer_ = std::move(observer);
        }
        static void SetShowHiddenObserver(std::function<void()> observer)
        {
            Instance().show_hidden_observer_ = std::move(observer);
        }

        // 隠しファイル(名前の頭が "." のものと、Finderの「隠す」フラグが付いたもの。FileEntryModel::IsHidden)を一覧に出すか。
        // 既定は隠す(Finderと同じ)。Miata.config.set_show_hidden(bool)で指定する。実行中にも変えられる
        // (Miata.config.set_show_hidden を、実行中に呼ぶ)ので、一覧を作るたびに読むこと(構築時にキャッシュしない)。
        // 値を書くのは、Luaの set_show_hidden だけ(上の通知が、一覧を作り直す)。
        static inline bool ShowHidden()
        {
            return Instance().show_hidden_;
        }

        // 背景色(Miata.config.color.background)。alphaは使わず、常に不透明にして返す。半透明で塗ると、
        // 後ろのOSのテーマの色(Lightなら白)が混ざって、OSの設定しだいで見た目が変わってしまうため。
        // 背景を塗る箇所は、Color().Get(...)ではなく必ずこれを使う(alphaの扱いをここに揃える)。
        static inline Color4f Background()
        {
            auto c = Color().Get(Color::Type::Background);
            c.a = 1.0f;
            return c;
        }

    private:
        static inline Config& Instance()
        {
            if (_instance == nullptr) {
                _instance = new Config();
            }
            return *_instance;
        }

        Config();
        ~Config();
        static int lua_color_index(lua_State* L);
        static int lua_color_newindex(lua_State* L);
        static int lua_set_font(lua_State* L);
        static int lua_set_font_size(lua_State* L);
        static int lua_set_history_limit(lua_State* L);
        static int lua_set_show_icons(lua_State* L);
        static int lua_get_show_icons(lua_State* L);
        static int lua_set_show_hidden(lua_State* L);
        static int lua_get_show_hidden(lua_State* L);

        static Config* _instance;
        class Color color_;
        std::string font_family_;
        float font_size_ = 12.0f;
        size_t history_limit_ = 100;
        bool show_icons_ = true;
        bool show_hidden_ = false;
        std::function<void()> show_icons_observer_;
        std::function<void()> show_hidden_observer_;
    };
}





#endif // CONFIG_H__
