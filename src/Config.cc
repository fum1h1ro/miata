#include "Config.h"
#include "Script.h"
#include <charconv>



namespace miata {
    namespace {
        // 引数 index が min〜max の整数なら、n に入れて true。数値だけ(文字列の "3" は不可)。3.0 は整数として受け付け、
        // 3.5・NaN・無限大は受け付けない。Lua のエラーにはしない: luaL_error は longjmp なので、呼ぶ側が、C++ のオブジェクトを
        // 作る前に、これで検証して、エラーを投げる
        bool ReadIntegerArg(lua_State* L, int index, lua_Integer min, lua_Integer max, lua_Integer& n)
        {
            int is_integer = 0;
            n = lua_type(L, index) == LUA_TNUMBER ? lua_tointegerx(L, index, &is_integer) : 0;
            return is_integer && n >= min && n <= max;
        }
    }

    Config* Config::_instance = nullptr;

    const char* Config::Color::names_[] = {
#define X(name, var) #var,
        CONFIG_COLOR_LIST
#undef X
    };

    Config::Color::Color()
    {
        constexpr int sz = sizeof(names_) / sizeof(names_[0]);
        values_.resize(sz);

        for (std::underlying_type_t<Type> i = 0; i < sz; ++i) {
            table_[names_[i]] = (Type)i;
        }
    }
    std::string Config::Color::ToString(const Color4f& v)
    {
        auto r = (int)(v.r * 255);
        auto g = (int)(v.g * 255);
        auto b = (int)(v.b * 255);
        auto a = (int)(v.a * 255);
        return std::format("#{:02x}{:02x}{:02x}{:02x}", r, g, b, a);
    }
    std::expected<Color4f, std::string> Config::Color::FromString(const std::string_view& v)
    {
        Color4f result;
        const size_t sz = v.size();
        if (v[0] != '#') {
            return std::unexpected(std::format("unknown color format: {}", v));
        }
        auto p = v.data();
        if (sz == 7) {
            int r, g, b;
            auto result_r = std::from_chars(&p[1], &p[3], r, 16);
            auto result_g = std::from_chars(&p[3], &p[5], g, 16);
            auto result_b = std::from_chars(&p[5], &p[7], b, 16);
            if (result_r.ec != std::errc() || result_g.ec != std::errc() || result_b.ec != std::errc()) {
                return std::unexpected(std::format("unknown color format: {}", v));
            }
            result.r = (float)r / 255.0f;
            result.g = (float)g / 255.0f;
            result.b = (float)b / 255.0f;
            result.a = 1.0f;
        }
        else if (sz == 9) {
            int r, g, b, a;
            auto result_r = std::from_chars(&p[1], &p[3], r, 16);
            auto result_g = std::from_chars(&p[3], &p[5], g, 16);
            auto result_b = std::from_chars(&p[5], &p[7], b, 16);
            auto result_a = std::from_chars(&p[7], &p[9], a, 16);
            if (result_r.ec != std::errc() || result_g.ec != std::errc() || result_b.ec != std::errc() || result_a.ec != std::errc()) {
                return std::unexpected(std::format("unknown color format: {}", v));
            }
            result.r = (float)r / 255.0f;
            result.g = (float)g / 255.0f;
            result.b = (float)b / 255.0f;
            result.a = (float)a / 255.0f;
        }
        else {
            return std::unexpected(std::format("unknown color format: {}", v));
        }
        return result;
    }








    Config::Config()
    {
    }

    Config::~Config()
    {
    }

    void Config::ScriptInitialize()
    {
        //auto& config = Instance();
        auto& script = Script::Instance();
        script.StackUsing([&]() {
            script.GetGlobal("Miata.config.color");
            static luaL_Reg metas[] = {
                { "__index", lua_color_index },
                { "__newindex", lua_color_newindex },
            };
            script.RegisterFunctionsToMetaTable(std::vector<luaL_Reg>(std::begin(metas), std::end(metas)));
        });

        static luaL_Reg config_funcs[] = {
            { "set_font", lua_set_font },
            { "set_font_size", lua_set_font_size },
            { "set_statusbar_font_size", lua_set_statusbar_font_size },
            { "set_history_limit", lua_set_history_limit },
            { "set_console_max_lines", lua_set_console_max_lines },
            { "set_console_height", lua_set_console_height },
            { "set_show_console", lua_set_show_console },
            { "get_show_console", lua_get_show_console },
            { "set_show_icons", lua_set_show_icons },
            { "get_show_icons", lua_get_show_icons },
            { "set_show_hidden", lua_set_show_hidden },
            { "get_show_hidden", lua_get_show_hidden },
            { "set_statusbar", lua_set_statusbar },
            { "set_show_statusbar", lua_set_show_statusbar },
            { "get_show_statusbar", lua_get_show_statusbar },
        };
        script.RegisterFunctions("Miata.config", std::vector<luaL_Reg>(std::begin(config_funcs), std::end(config_funcs)));
    }

    int Config::lua_set_font_size(lua_State* L)
    {
        Script::CheckArgType(L, 1, LUA_TNUMBER);
        Instance().font_size_ = (float)lua_tonumber(L, 1);
        return 0;
    }

    // Miata.config.set_statusbar_font_size(n): ステータスバーの文字のサイズ(pt)。kStatusBarFontSizeMin〜Maxの数値だけ
    // 受け付ける(14.5 のような小数も可)。文字列 "14"・NaN・無限大・範囲外は、設定を変えずにエラーにする(設定の読み込みの
    // エラーとして、起動時のダイアログに出る)。set_font_size と違って範囲も見る(0 や負数は、バーの高さが壊れるため)。
    // luaL_errorはlongjmpなので、C++のオブジェクトを作る前に検証する。
    int Config::lua_set_statusbar_font_size(lua_State* L)
    {
        const bool is_number = lua_type(L, 1) == LUA_TNUMBER;
        const lua_Number size = is_number ? lua_tonumber(L, 1) : 0;
        // NaN は、どちらの比較も偽になるので、ここで外れる
        if (!is_number || !(size >= kStatusBarFontSizeMin && size <= kStatusBarFontSizeMax)) {
            return luaL_error(L, "set_statusbar_font_size: expected a number from %d to %d", kStatusBarFontSizeMin, kStatusBarFontSizeMax);
        }
        Instance().statusbar_font_size_ = static_cast<float>(size);
        return 0;
    }

    // Miata.config.set_history_limit(n): 0〜kHistoryLimitMaxの整数。外れた値は、設定を変えずにエラーにする
    // (設定の読み込みのエラーとして、起動時のダイアログに出る)。3.0は整数として受け付け、3.5・NaN・無限大・
    // 文字列は受け付けない(ReadIntegerArg)。
    int Config::lua_set_history_limit(lua_State* L)
    {
        lua_Integer n = 0;
        if (!ReadIntegerArg(L, 1, 0, kHistoryLimitMax, n)) {
            return luaL_error(L, "set_history_limit: expected an integer from 0 to %d", kHistoryLimitMax);
        }
        Instance().history_limit_ = static_cast<size_t>(n);
        return 0;
    }

    // Miata.config.set_console_max_lines(n): コンソールが持つ行数の上限。1〜kConsoleMaxLinesMaxの整数。外れた値は、
    // 設定を変えずにエラーにする(set_history_limitと同じ検証)。0 は受け付けない=「記録しない」とも「無制限」とも読めて
    // 曖昧。起動時にだけ読む。
    int Config::lua_set_console_max_lines(lua_State* L)
    {
        lua_Integer n = 0;
        if (!ReadIntegerArg(L, 1, 1, kConsoleMaxLinesMax, n)) {
            return luaL_error(L, "set_console_max_lines: expected an integer from 1 to %d", kConsoleMaxLinesMax);
        }
        Instance().console_max_lines_ = static_cast<size_t>(n);
        return 0;
    }

    // Miata.config.set_console_height(n): コンソールを開いたときの高さ(行数)。kConsoleHeightMin〜kConsoleHeightMaxの整数
    // (set_console_max_linesと同じ検証)。外れた値は、設定を変えずにエラー。起動時にだけ読む。
    int Config::lua_set_console_height(lua_State* L)
    {
        lua_Integer n = 0;
        if (!ReadIntegerArg(L, 1, kConsoleHeightMin, kConsoleHeightMax, n)) {
            return luaL_error(L, "set_console_height: expected an integer from %d to %d", kConsoleHeightMin, kConsoleHeightMax);
        }
        Instance().console_height_ = static_cast<int>(n);
        return 0;
    }

    // Miata.config.set_show_console(bool): コンソールを出すか(既定は出さない)。真偽値だけ受け付ける(set_show_statusbar と同じ。
    // 型の違いは、設定を変えずにエラー)。実行中に値が変わったら、すぐ反映する(show_console_observer_)。
    // ステータスバーの Lua の関数の中では、呼べない(画面を変えるので。set_show_statusbar と同じ)
    int Config::lua_set_show_console(lua_State* L)
    {
        if (lua_type(L, 1) != LUA_TBOOLEAN) {
            return luaL_error(L, "set_show_console: expected a boolean (true or false)");
        }
        auto& config = Instance();
        if (config.view_change_guard_ && !config.view_change_guard_()) {
            return luaL_error(L, "set_show_console: cannot be called from the status bar function (it must not change anything)");
        }
        const bool show = lua_toboolean(L, 1) != 0;
        const bool changed = config.show_console_ != show;
        config.show_console_ = show;
        // 実行中(ビューがある)に、値が変わったときだけ、出入りを直す。検証の後なので、エラーは起きない
        if (changed && config.show_console_observer_) config.show_console_observer_();
        return 0;
    }

    // Miata.config.get_show_console() -> boolean: コンソールを出している(出す設定になっている)か。いまの状態
    // (set_show_console の値と、実行中の切り替えを反映する)。読むだけで、状態は変えない。設定の読み込み中も呼べる
    int Config::lua_get_show_console(lua_State* L)
    {
        lua_pushboolean(L, Instance().show_console_);
        return 1;
    }

    // Miata.config.set_show_icons(bool): ファイル名の頭のアイコンを出すか。真偽値だけ受け付ける
    // (lua_tobooleanは何でも受けて、0 や "" を真にしてしまうので、型を見る)。違う型は、設定を変えずにエラーにする
    // (設定の読み込みのエラーとして、起動時のダイアログに出る)。実行中に値が変わったら、すぐ描き直す(show_icons_observer_)。
    int Config::lua_set_show_icons(lua_State* L)
    {
        if (lua_type(L, 1) != LUA_TBOOLEAN) {
            return luaL_error(L, "set_show_icons: expected a boolean (true or false)");
        }
        auto& config = Instance();
        if (config.view_change_guard_ && !config.view_change_guard_()) {
            return luaL_error(L, "set_show_icons: cannot be called from the status bar function (it must not change anything)");
        }
        const bool show = lua_toboolean(L, 1) != 0;
        const bool changed = config.show_icons_ != show;
        config.show_icons_ = show;
        // 実行中(ビューがある)に、値が変わったときだけ、両ペインを描き直す。検証の後なので、エラーは起きない
        if (changed && config.show_icons_observer_) config.show_icons_observer_();
        return 0;
    }

    // Miata.config.get_show_icons() -> boolean: ファイル名の頭のアイコンを出している(出す設定になっている)か。
    // set_show_icons で決めた値(起動時の設定も、実行中の呼び出しも)を反映した、いまの状態(Config::ShowIcons())。
    // 読むだけで、状態は変えない。画面を触らないので、
    // 設定の読み込み中(init.lua の最上位)からも呼べる。引数は見ない。
    int Config::lua_get_show_icons(lua_State* L)
    {
        lua_pushboolean(L, Instance().show_icons_);
        return 1;
    }

    // Miata.config.set_show_hidden(bool): 隠しファイルを一覧に出すか(起動したときの状態)。真偽値だけ受け付ける
    // (set_show_iconsと同じ。型の違いは、設定を変えずにエラーにする)。
    int Config::lua_set_show_hidden(lua_State* L)
    {
        if (lua_type(L, 1) != LUA_TBOOLEAN) {
            return luaL_error(L, "set_show_hidden: expected a boolean (true or false)");
        }
        auto& config = Instance();
        if (config.view_change_guard_ && !config.view_change_guard_()) {
            return luaL_error(L, "set_show_hidden: cannot be called from the status bar function (it must not change anything)");
        }
        const bool show = lua_toboolean(L, 1) != 0;
        const bool changed = config.show_hidden_ != show;
        config.show_hidden_ = show;
        // 実行中(ビューがある)に、値が変わったときだけ、両ペインの一覧を作り直す
        if (changed && config.show_hidden_observer_) config.show_hidden_observer_();
        return 0;
    }

    // Miata.config.get_show_hidden() -> boolean: 隠しファイルを一覧に出している(出す設定になっている)か。
    // set_show_hidden で決めた値(起動時の設定も、実行中の呼び出しも)を反映した、いまの状態(Config::ShowHidden())。
    // 読むだけで、状態は変えない。画面を触らないので、
    // 設定の読み込み中(init.lua の最上位)からも呼べる。引数は見ない。
    int Config::lua_get_show_hidden(lua_State* L)
    {
        lua_pushboolean(L, Instance().show_hidden_);
        return 1;
    }

    // Miata.config.set_show_statusbar(bool): ステータスバーを出すか(既定は出す)。真偽値だけ受け付ける(set_show_icons と同じ。
    // 型の違いは、設定を変えずにエラー)。実行中に値が変わったら、すぐ反映する(show_statusbar_observer_)。
    // ステータスバーの Lua の関数の中では、呼べない(画面を変えるので。set_show_icons など、ほかの set_show_* と同じ)
    int Config::lua_set_show_statusbar(lua_State* L)
    {
        if (lua_type(L, 1) != LUA_TBOOLEAN) {
            return luaL_error(L, "set_show_statusbar: expected a boolean (true or false)");
        }
        auto& config = Instance();
        if (config.view_change_guard_ && !config.view_change_guard_()) {
            return luaL_error(L, "set_show_statusbar: cannot be called from the status bar function (it must not change anything)");
        }
        const bool show = lua_toboolean(L, 1) != 0;
        const bool changed = config.show_statusbar_ != show;
        config.show_statusbar_ = show;
        // 実行中(ビューがある)に、値が変わったときだけ、バーの出入りを直す。検証の後なので、エラーは起きない
        if (changed && config.show_statusbar_observer_) config.show_statusbar_observer_();
        return 0;
    }

    // Miata.config.get_show_statusbar() -> boolean: ステータスバーを出している(出す設定になっている)か。いまの状態
    // (set_show_statusbar の値と、実行中の切り替えを反映する)。読むだけで、状態は変えない。設定の読み込み中も呼べる
    int Config::lua_get_show_statusbar(lua_State* L)
    {
        lua_pushboolean(L, Instance().show_statusbar_);
        return 1;
    }

    // Miata.config.set_statusbar(fn | nil): ステータスバーの内容を決める Lua の関数を登録する(nil で解除=C++ の既定の表示)。
    // 関数か nil だけ受け付ける(引数なしも不可)。違う型は、設定を変えずにエラーにする(設定の読み込みのエラーとして、起動時の
    // ダイアログに出る)。ほかの設定と違い、実行中に呼んでよい(関数が自分を差し替えてもよい: 呼び出し中の関数の値は、
    // Lua のスタックに残るので、参照を解放しても生きている)。世代が進み、次のティック(KeyDown の中なら、その出口)で反映される。
    // 設定の読み込み中(Viewが無い)も、参照を持つだけ。luaL_error は longjmp なので、検証の後に、何かを作る
    int Config::lua_set_statusbar(lua_State* L)
    {
        const int type = lua_type(L, 1);
        if (type != LUA_TFUNCTION && type != LUA_TNIL) {
            return luaL_error(L, "set_statusbar: expected a function or nil");
        }
        auto& config = Instance();
        if (config.statusbar_ref_ != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, config.statusbar_ref_);
            config.statusbar_ref_ = LUA_NOREF;
        }
        if (type == LUA_TFUNCTION) {
            lua_pushvalue(L, 1);
            config.statusbar_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
        }
        ++config.statusbar_generation_;
        return 0;
    }

    int Config::lua_set_font(lua_State* L)
    {
        Script::CheckArgType(L, 1, LUA_TSTRING);
        Instance().font_family_ = lua_tostring(L, 1);
        return 0;
    }

    int Config::lua_color_index(lua_State* L)
    {
        auto nargs = lua_gettop(L);
        if (nargs != 2) {
            luaL_error(L, "invalid number of arguments");
        }
        Script::CheckArgType(L, 1, LUA_TTABLE);
        Script::CheckArgType(L, 2, LUA_TSTRING);
        auto name = lua_tostring(L, 2);

        auto result = Config::Color().Get(name);
        if (!result) {
            luaL_error(L, "unknown color type");
        }
        auto str = Config::Color::ToString(result.value());
        lua_pushstring(L, str.c_str());
        return 1;
    }

    int Config::lua_color_newindex(lua_State* L)
    {
        auto nargs = lua_gettop(L);
        if (nargs != 3) {
            luaL_error(L, "invalid number of arguments");
        }
        Script::CheckArgType(L, 1, LUA_TTABLE);
        Script::CheckArgType(L, 2, LUA_TSTRING);
        Script::CheckArgType(L, 3, LUA_TSTRING);
        auto name = lua_tostring(L, 2);
        auto value = lua_tostring(L, 3);

        auto color = Config::Color::FromString(value);
        if (!color) {
            luaL_error(L, color.error().c_str());
        }
        auto r = Config::Color().Set(name, color.value());
        if (!r) {
            luaL_error(L, r.error().c_str());
        }
        return 0;
    }

}
