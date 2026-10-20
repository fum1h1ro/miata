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
#include "lauxlib.h"
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
    X(StatusBarBackground, statusbar_background) \
    X(StatusBarText, statusbar_text) \
    X(ConsoleError, console_error) \



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

        // ステータスバーの文字のサイズ(pt)。Miata.config.set_statusbar_font_size(n)で指定する(既定は12)。
        // ファイル一覧の set_font_size とは独立している(フォントのファミリー set_font は共通)。バーの高さも、
        // これに連動する(ViewMetrics.h の SingleLineBarHeight)。ほかの設定と同じく、起動時にだけ読み込む(Viewの構築時に高さを決める)。
        static inline float StatusBarFontSize()
        {
            return Instance().statusbar_font_size_;
        }
        // set_statusbar_font_sizeに指定できる範囲(ptの数値)
        static constexpr int kStatusBarFontSizeMin = 1;
        static constexpr int kStatusBarFontSizeMax = 200;

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

        // コンソールが持つ行数の上限。Miata.config.set_console_max_lines(n)で指定する(既定は1000。起動時にだけ読む)。
        // 超えたら、古い行から捨てる。設定の読み込みの後に、Application が ConsoleLog::SetLimit に渡す(ConsoleLog は Config を知らない)
        static inline size_t ConsoleMaxLines()
        {
            return Instance().console_max_lines_;
        }
        // set_console_max_linesに指定できる最大(1〜この値の整数。0は「記録しない」とも「無制限」とも読めて曖昧なので受け付けない)。
        // ConsoleLog::kMaxLimit と同じ値(Application.cc の static_assert が確かめる。Config.h は ConsoleLog.h を include しない)
        static constexpr int kConsoleMaxLinesMax = 100000;

        // コンソールを開いたときの高さ(行数)。Miata.config.set_console_height(n)で指定する(既定は10。起動時にだけ読む:
        // View の構築時に、高さ(pt)を決める)。行の高さは一覧と同じ。窓が低いときは、ブラウザ領域の最小の高さを残して、縮む
        static inline int ConsoleHeight()
        {
            return Instance().console_height_;
        }
        // set_console_heightに指定できる範囲(行数の整数)
        static constexpr int kConsoleHeightMin = 1;
        static constexpr int kConsoleHeightMax = 100;

        // ファイル名の頭に、ファイルのアイコン(Finderと同じ)を出すか。既定は出す。Miata.config.set_show_icons(bool)で
        // 指定する。ほかの設定と違い、実行中にも変えられる(Miata.config.set_show_icons を、実行中に呼ぶ)ので、描くたびに読むこと
        // (構築時にキャッシュしない)。値を書くのは、Luaの set_show_icons だけ(下の通知が、ビューを描き直す)。
        static inline bool ShowIcons()
        {
            return Instance().show_icons_;
        }

        // 実行中に、Luaの Miata.config.set_show_*(icons / hidden / statusbar / console)が、値を**変えた**ときに呼ぶ通知(同じ値を指定したときは、
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
        static void SetShowStatusBarObserver(std::function<void()> observer)
        {
            Instance().show_statusbar_observer_ = std::move(observer);
        }
        static void SetShowConsoleObserver(std::function<void()> observer)
        {
            Instance().show_console_observer_ = std::move(observer);
        }

        // コンソール(ブラウザ領域とステータスバーの間のログのペイン)を出すか。既定は出さない。Miata.config.set_show_console(bool)で
        // 指定する。show_statusbar と同じく、実行中にも変えられる(Miata.config.set_show_console を、実行中に呼ぶ。既定のキー zc)。
        // 値を書くのは、Luaの set_show_console だけ(上の通知が、出入りと、ブラウザ領域の高さを、すぐ直す)。
        // 隠している間は、View::UpdateConsole が、何もしない(ログは貯まり続ける)ので、毎ティックに読むこと(構築時にキャッシュしない)
        static inline bool ShowConsole()
        {
            return Instance().show_console_;
        }

        // ステータスバー(ウィンドウの下端)を出すか。既定は出す。Miata.config.set_show_statusbar(bool)で指定する。
        // show_icons / show_hidden と同じく、実行中にも変えられる(Miata.config.set_show_statusbar を、実行中に呼ぶ)。
        // 値を書くのは、Luaの set_show_statusbar だけ(上の通知が、バーの出入りと、ブラウザ領域の高さを、すぐ直す)。
        // 隠しているあいだは、Application::UpdateStatusBar が、何もしない(材料の収集も、Lua の関数の呼び出しもしない)ので、
        // 描くたび・毎ティックに読むこと(構築時にキャッシュしない)
        static inline bool ShowStatusBar()
        {
            return Instance().show_statusbar_;
        }

        // 画面を変える設定(set_show_icons / set_show_hidden / set_show_statusbar / set_show_console)を、いま Lua から呼んでよいかを
        // 返す関数(Application が登録する)。
        // 偽を返す間は、これらの設定は、値を変えずに Lua のエラーになる(ステータスバーの Lua の関数は、何も変えない)。
        // 未登録(設定の読み込み中)なら、いつでも呼べる
        static void SetViewChangeGuard(std::function<bool()> allowed)
        {
            Instance().view_change_guard_ = std::move(allowed);
        }

        // 隠しファイル(名前の頭が "." のものと、Finderの「隠す」フラグが付いたもの。FileEntryModel::IsHidden)を一覧に出すか。
        // 既定は隠す(Finderと同じ)。Miata.config.set_show_hidden(bool)で指定する。実行中にも変えられる
        // (Miata.config.set_show_hidden を、実行中に呼ぶ)ので、一覧を作るたびに読むこと(構築時にキャッシュしない)。
        // 値を書くのは、Luaの set_show_hidden だけ(上の通知が、一覧を作り直す)。
        static inline bool ShowHidden()
        {
            return Instance().show_hidden_;
        }

        // ステータスバーの Lua の関数(Miata.config.set_statusbar)。luaL_ref の参照(無ければ LUA_NOREF)。
        // Config は、参照を持つだけで、呼ばない(呼ぶのは Application。StatusBarScript)。set_statusbar を呼ぶたびに、
        // StatusBarGeneration が進む(関数の差し替え・解除の検出と、エラーでラッチした関数の差し替えに使う)。
        // ほかの設定と違い、実行中に差し替えてよい(観測者は作らない: 世代の比較で足りる)
        static inline int StatusBarRef()
        {
            return Instance().statusbar_ref_;
        }
        static inline uint64_t StatusBarGeneration()
        {
            return Instance().statusbar_generation_;
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

        // ステータスバーの背景色(Miata.config.color.statusbar_background)。Background()と同じ理由で、alphaは使わず、
        // 常に不透明にして返す。ステータスバーの背景を塗る箇所は、Color().Get(...)ではなく必ずこれを使う。
        static inline Color4f StatusBarBackground()
        {
            auto c = Color().Get(Color::Type::StatusBarBackground);
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
        static int lua_set_statusbar_font_size(lua_State* L);
        static int lua_set_history_limit(lua_State* L);
        static int lua_set_console_max_lines(lua_State* L);
        static int lua_set_console_height(lua_State* L);
        static int lua_set_show_console(lua_State* L);
        static int lua_get_show_console(lua_State* L);
        static int lua_set_show_icons(lua_State* L);
        static int lua_get_show_icons(lua_State* L);
        static int lua_set_show_hidden(lua_State* L);
        static int lua_get_show_hidden(lua_State* L);
        static int lua_set_statusbar(lua_State* L);
        static int lua_set_show_statusbar(lua_State* L);
        static int lua_get_show_statusbar(lua_State* L);

        static Config* _instance;
        class Color color_;
        std::string font_family_;
        float font_size_ = 12.0f;
        float statusbar_font_size_ = 12.0f;
        size_t history_limit_ = 100;
        size_t console_max_lines_ = 1000;
        int console_height_ = 10;
        bool show_console_ = false;
        std::function<void()> show_console_observer_;
        bool show_icons_ = true;
        bool show_hidden_ = false;
        std::function<void()> show_icons_observer_;
        std::function<void()> show_hidden_observer_;
        bool show_statusbar_ = true;
        std::function<void()> show_statusbar_observer_;
        std::function<bool()> view_change_guard_;
        int statusbar_ref_ = LUA_NOREF;
        uint64_t statusbar_generation_ = 0;
    };
}





#endif // CONFIG_H__
