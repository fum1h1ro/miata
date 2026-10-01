#include <format>
#include <print>
#include <ranges>
#include <system_error>
#include "macro.h"
#include "Script.h"
#include <stdio.h>
#include "platform.h"


namespace miata {
    namespace {
        // スタックトップのエラーオブジェクトを文字列にする。error({}) のように、文字列でない値が投げられても
        // 落ちない(lua_tostring はNULLを返すので、そのままstd::stringにすると未定義動作になる)。
        std::string ErrorMessage(lua_State* L)
        {
            if (lua_isstring(L, -1)) return lua_tostring(L, -1);
            return std::format("(error object is a {} value)", luaL_typename(L, -1));
        }
    }

    Script::Script()
    {
        L_ = lua_newstate(lua_Alloc, nullptr, 0);
        lua_gc(L_, LUA_GCINC, 150, 100, 13);
        luaL_openlibs(L_);
        auto version = lua_version(L_);
        printf("Lua Version: %f\n", version);
    }

    Script::~Script()
    {
        lua_close(L_);
    }

    void* Script::lua_Alloc(void* ud, void* ptr, size_t osize, size_t nsize)
    {
        if (nsize == 0) {
            ::free(ptr);
            return nullptr;
        }
        else {
            return ::realloc(ptr, nsize);
        }
    }

    void Script::Initialize()
    {
        auto r = DoResourceFile("base.lua");
        if (!r) {
            std::println("error: {}", r.error());
            return;
        }
        SetupCommands();
    }

    std::filesystem::path Script::UserConfigFile()
    {
        return pl_get_config_dir() / "miata" / "init.lua";
    }

    std::vector<Script::ConfigError> Script::PostInitialize()
    {
        std::vector<ConfigError> errors;

        // 組み込みの既定の設定
        if (auto r = DoResourceFile("test.lua"); !r) {
            errors.push_back({"test.lua", r.error()});
        }

        // ユーザーの設定。無いのはエラーではない(リンク切れのシンボリックリンクは、あるのに読めないので、エラーにする)
        auto user_file = UserConfigFile();
        std::error_code ec;
        if (std::filesystem::symlink_status(user_file, ec).type() != std::filesystem::file_type::not_found) {
            if (auto r = DoFile(user_file); !r) {
                errors.push_back({user_file.string(), r.error()});
            }
        }

        for (auto& e : errors) {
            std::println("error: {}: {}", e.file, e.message);
        }
        return errors;
    }

    bool Script::Update()
    {
        bool any = false;
        for (auto& [_, v] : threads_) {
            auto& param = v;
            if (param->status_ == LUA_YIELD) {
                param->status_ = lua_resume(param->L_, nullptr, 0, &param->nresult_);
                lua_pop(param->L_, param->nresult_);
                any = true;
            }
        }
        return any;
    }

    void Script::SetupCommands()
    {
    }

    std::expected<bool, std::string> Script::DoFile(std::filesystem::path path)
    {
        // luaL_loadfile は、先頭の文字コードの印(BOM)と#!行を読み飛ばし、開けない・読めないファイルは
        // エラーとして返し、エラーメッセージにファイル名と行番号を付ける
        auto orig = lua_gettop(L_);
        if (luaL_loadfile(L_, path.c_str()) != LUA_OK || lua_pcall(L_, 0, 0, 0) != LUA_OK) {
            auto error = ErrorMessage(L_);
            lua_settop(L_, orig);
            return std::unexpected(error);
        }
        lua_settop(L_, orig);
        return true;
    }

    std::expected<bool, std::string> Script::DoResourceFile(std::filesystem::path path)
    {
        auto file = pl_read_resource_file(path.c_str());
        if (!file) return std::unexpected(file.error());
        return DoBuffer(file.value(), "@" + path.string());
    }

    std::expected<bool, std::string> Script::DoString(const char* str)
    {
        return DoBuffer(str, str);
    }

    std::expected<bool, std::string> Script::DoBuffer(const std::string& code, const std::string& chunk_name)
    {
        auto orig = lua_gettop(L_);
        if (luaL_loadbuffer(L_, code.data(), code.size(), chunk_name.c_str()) != LUA_OK || lua_pcall(L_, 0, 0, 0) != LUA_OK) {
            auto error = ErrorMessage(L_);
            lua_settop(L_, orig);
            return std::unexpected(error);
        }
        lua_settop(L_, orig);
        return true;
    }

    void Script::StackUsing(std::function<void()> func)
    {
        auto orig = lua_gettop(L_);
        func();
        lua_settop(L_, orig);
    }

    void Script::GetGlobal(const std::string_view& name)
    {
        int count = 0;
        for (auto range : name | std::views::split('.')) {
            auto item = std::string(range.begin(), range.end());
            if (count++ == 0) {
                lua_getglobal(L_, item.c_str());
            }
            else {
                lua_getfield(L_, -1, item.c_str());
            }
        }
    }

    void Script::RegisterFunctions(const std::string_view& table, const std::vector<luaL_Reg>& funcs, lua_CFunction wrapper)
    {
        auto orig = lua_gettop(L_);
        int count = 0;
        for (auto range : table | std::views::split('.')) {
            auto item = std::string(range.begin(), range.end());
            if (count++ == 0) {
                lua_getglobal(L_, item.c_str());
            }
            else {
                lua_getfield(L_, -1, item.c_str());
            }
        }
        for (auto& f : funcs) {
            lua_pushcfunction(L_, f.func);
            if (wrapper) lua_pushcclosure(L_, wrapper, 1); // 本来の関数をupvalue(1)に持つクロージャにする
            lua_setfield(L_, -2, f.name);
        }
        lua_settop(L_, orig);
    }

    void Script::RegisterFunctionsToMetaTable(const std::vector<luaL_Reg>& funcs)
    {
        CheckArgType(L_, -1, LUA_TTABLE);
        if (lua_getmetatable(L_, -1) == 0) {
            lua_newtable(L_);
        }

        for (auto& f :funcs) {
            lua_pushcfunction(L_, f.func);
            lua_setfield(L_, -2, f.name);
        }
        lua_setmetatable(L_, -2);
    }

    std::expected<bool, std::string> Script::InvokeRefFunction(int ref)
    {
        auto orig = lua_gettop(L_);
        lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
        if (lua_pcall(L_, 0, 0, 0)) {
            auto error = ErrorMessage(L_);
            lua_pop(L_, 1);
            return std::unexpected(error);
        }
        lua_settop(L_, orig);
        return true;
    }

    std::expected<bool, std::string> Script::InvokeRefFunctionOnThread(std::string thread_key, int ref)
    {
        auto it = threads_.find(thread_key);

        if (it != threads_.end()) {
            auto& param = it->second;
            if (param->status_ == LUA_YIELD) {
                return std::unexpected("thread is already running");
            }
            if (param->status_ != LUA_OK) {
                if (lua_closethread(param->L_, nullptr)) {
                    auto error = ErrorMessage(param->L_);
                    lua_pop(param->L_, 1);
                    return std::unexpected(error);
                }
            }
        }
        else {
            auto thread = lua_newthread(L_);
            threads_[thread_key] = std::make_unique<ThreadParam>(thread);
            it = threads_.find(thread_key);
        }

        auto& param = it->second;
        auto orig = lua_gettop(L_);
        lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
        lua_xmove(L_, param->L_, 1);
        param->status_ = lua_resume(param->L_, nullptr, 0, &param->nresult_);
        if (param->status_ != LUA_OK && param->status_ != LUA_YIELD) {
            auto error = ErrorMessage(param->L_);
            lua_pop(param->L_, 1);
            return std::unexpected(error);
        }
        lua_settop(L_, orig);
        return true;
    }







    int Script::Command_Quit(lua_State* L)
    {
        std::println("quit");
        luaL_error(L, "error yo");
        return 0;
    }



}
