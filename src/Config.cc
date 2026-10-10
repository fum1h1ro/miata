#include "Config.h"
#include "Script.h"
#include <charconv>



namespace miata {
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
        for (auto t : table_) {
            std::printf("%s: %d\n", t.first.c_str(), (int)t.second);
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
            { "set_history_limit", lua_set_history_limit },
            { "set_show_icons", lua_set_show_icons },
            { "get_show_icons", lua_get_show_icons },
            { "set_show_hidden", lua_set_show_hidden },
            { "get_show_hidden", lua_get_show_hidden },
        };
        script.RegisterFunctions("Miata.config", std::vector<luaL_Reg>(std::begin(config_funcs), std::end(config_funcs)));
    }

    int Config::lua_set_font_size(lua_State* L)
    {
        Script::CheckArgType(L, 1, LUA_TNUMBER);
        Instance().font_size_ = (float)lua_tonumber(L, 1);
        return 0;
    }

    // Miata.config.set_history_limit(n): 0〜kHistoryLimitMaxの整数。外れた値は、設定を変えずにエラーにする
    // (設定の読み込みのエラーとして、起動時のダイアログに出る)。3.0は整数として受け付け、3.5・NaN・無限大・
    // 文字列は受け付けない。luaL_errorはlongjmpなので、C++のオブジェクトを作る前に検証する。
    int Config::lua_set_history_limit(lua_State* L)
    {
        int is_integer = 0;
        lua_Integer n = lua_type(L, 1) == LUA_TNUMBER ? lua_tointegerx(L, 1, &is_integer) : 0;
        if (!is_integer || n < 0 || n > kHistoryLimitMax) {
            return luaL_error(L, "set_history_limit: expected an integer from 0 to %d", kHistoryLimitMax);
        }
        Instance().history_limit_ = static_cast<size_t>(n);
        return 0;
    }

    // Miata.config.set_show_icons(bool): ファイル名の頭のアイコンを出すか。真偽値だけ受け付ける
    // (lua_tobooleanは何でも受けて、0 や "" を真にしてしまうので、型を見る)。違う型は、設定を変えずにエラーにする
    // (設定の読み込みのエラーとして、起動時のダイアログに出る)。
    int Config::lua_set_show_icons(lua_State* L)
    {
        if (lua_type(L, 1) != LUA_TBOOLEAN) {
            return luaL_error(L, "set_show_icons: expected a boolean (true or false)");
        }
        Instance().show_icons_ = lua_toboolean(L, 1) != 0;
        return 0;
    }

    // Miata.config.get_show_icons() -> boolean: ファイル名の頭のアイコンを出している(出す設定になっている)か。
    // set_show_icons で決めた値と、実行中の toggle_icons での切り替えの、どちらも反映した、いまの状態
    // (どちらも同じ値 Config::ShowIcons() を書き換える)。読むだけで、状態は変えない。画面を触らないので、
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
        Instance().show_hidden_ = lua_toboolean(L, 1) != 0;
        return 0;
    }

    // Miata.config.get_show_hidden() -> boolean: 隠しファイルを一覧に出している(出す設定になっている)か。
    // set_show_hidden で決めた値と、実行中の toggle_hidden での切り替えの、どちらも反映した、いまの状態
    // (どちらも同じ値 Config::ShowHidden() を書き換える)。読むだけで、状態は変えない。画面を触らないので、
    // 設定の読み込み中(init.lua の最上位)からも呼べる。引数は見ない。
    int Config::lua_get_show_hidden(lua_State* L)
    {
        lua_pushboolean(L, Instance().show_hidden_);
        return 1;
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
