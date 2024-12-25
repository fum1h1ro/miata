#include <print>
#include <ranges>
#include "macro.h"
#include "Script.h"
#include <stdio.h>
#include "platform.h"


namespace miata {
    Script* Script::instance_ = nullptr;

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
        SOKOL_ASSERT(instance_ != nullptr);
        instance_ = nullptr;
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

    void Script::PostInitialize()
    {
        auto r = DoResourceFile("test.lua");
        if (!r) {
            std::println("error: {}", r.error());
        }
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
        auto file = pl_read_file(path.c_str());
        return DoString(file.c_str());
    }

    std::expected<bool, std::string> Script::DoResourceFile(std::filesystem::path path)
    {
        auto file = pl_read_resource_file(path.c_str());
        if (!file) return std::unexpected(file.error());
        return DoString(file.value().c_str());
    }

    std::expected<bool, std::string> Script::DoString(const char* str)
    {
        auto orig = lua_gettop(L_);
        if (luaL_dostring(L_, str)) {
            auto error = std::string(lua_tostring(L_, -1));
            lua_pop(L_, 1);
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

    void Script::RegisterFunctions(const std::string_view& table, const std::vector<luaL_Reg>& funcs)
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
            auto error = std::string(lua_tostring(L_, -1));
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
                    auto error = std::string(lua_tostring(param->L_, -1));
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
            auto error = std::string(lua_tostring(param->L_, -1));
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
