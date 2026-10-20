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
        // lua_newstate を直接呼ぶと、警告の関数が入らず、warn() と、VM が出す __gc のエラーの警告が、黙って捨てられる
        lua_setwarnf(L_, &Script::WarnFunction, this);
        lua_gc(L_, LUA_GCINC, 150, 100, 13);
        luaL_openlibs(L_);
        // コンソールの最初の方の行になる("Lua 5.5.1")
        std::println("{}", LUA_RELEASE);
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

    std::optional<std::string> Script::Initialize()
    {
        auto r = DoResourceFile("base.lua");
        if (!r) return r.error();
        SetupCommands();
        return std::nullopt;
    }

    void Script::SetWarningHandler(std::function<void(const std::string&)> handler)
    {
        warning_handler_ = std::move(handler);
    }

    void Script::WarnFunction(void* ud, const char* message, int to_continue)
    {
        static_cast<Script*>(ud)->OnWarning(message, to_continue != 0);
    }

    void Script::OnWarning(const char* message, bool to_continue)
    {
        // コアの中(GC の最中を含む)から呼ばれる: Lua の API を呼ばず、C のフレームに例外を通さない
        // (メモリ不足などで投げたら、その警告を捨てる)
        try {
            // 続きでなく、'@' で始まる(最後の)断片だけが、制御メッセージ("@on" / "@off"。lauxlib の標準の warn 関数と同じ)
            if (!warning_continuing_ && !to_continue && message[0] == '@') {
                if (std::strcmp(message, "@off") == 0) warning_enabled_ = false;
                else if (std::strcmp(message, "@on") == 0) warning_enabled_ = true;
                return;
            }
            // 出さない間も、断片の続きかどうかは追う(続きの断片が '@' で始まっても、制御メッセージにしない)
            warning_continuing_ = to_continue;
            warning_text_ += message;
            if (to_continue) return;
            if (warning_enabled_ && warning_handler_) warning_handler_(warning_text_);
        }
        catch (...) {
        }
        if (!warning_continuing_) warning_text_.clear();
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

        // エラーを出すのは、呼び出し側(Application)の仕事(コンソールのログと、画面のダイアログ)
        return errors;
    }

    std::vector<Script::ThreadError> Script::Update()
    {
        std::vector<ThreadError> errors;
        for (auto& [name, param] : threads_) {
            if (param->status_ != LUA_YIELD) continue;
            const int status = lua_resume(param->L_, nullptr, 0, &param->nresult_);
            if (auto error = Settle(name, *param, status)) errors.push_back(std::move(*error));
        }
        return errors;
    }

    std::optional<Script::ThreadError> Script::Settle(const std::string& name, ThreadParam& param, int status)
    {
        if (status == LUA_OK || status == LUA_YIELD) {
            lua_pop(param.L_, param.nresult_);
            param.nresult_ = 0;
            param.status_ = status;
            return std::nullopt;
        }

        // 失敗: lua_resume は、スタックを巻き戻さず、エラーの値を先頭に残す。閉じる前に読む
        ThreadError error{name, ErrorMessage(param.L_)};
        // コルーチンを閉じて(__close の変数があれば、ここで走る。その失敗は、元のエラーを報告したので、見ない)、
        // 再利用できる状態に戻す
        lua_closethread(param.L_, nullptr);
        lua_settop(param.L_, 0);
        param.nresult_ = 0;
        param.status_ = LUA_OK;
        return error;
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

    namespace {
        // 命令数の上限を超えたときに呼ばれる(LUA_MASKCOUNT)。Lua のエラーにして、pcall に戻す
        void InstructionLimitHook(lua_State* L, lua_Debug* /*ar*/)
        {
            luaL_error(L, "instruction limit exceeded (the function ran too long)");
        }
    }

    std::expected<void, std::string> Script::CallProtected(
        lua_CFunction func,
        void* ud,
        int max_instructions,
        const std::function<void(lua_State*)>& read
    )
    {
        const auto orig = lua_gettop(L_);
        lua_pushcfunction(L_, func);
        lua_pushlightuserdata(L_, ud);
        if (max_instructions > 0) lua_sethook(L_, InstructionLimitHook, LUA_MASKCOUNT, max_instructions);
        const int status = lua_pcall(L_, 1, 1, 0);
        if (max_instructions > 0) lua_sethook(L_, nullptr, 0, 0);
        if (status != LUA_OK) {
            auto error = ErrorMessage(L_);
            lua_settop(L_, orig);
            return std::unexpected(error);
        }
        if (read) read(L_);
        lua_settop(L_, orig);
        return {};
    }

    std::optional<Script::ThreadError> Script::InvokeRefFunctionOnThread(const std::string& name, int ref)
    {
        auto it = threads_.find(name);
        if (it == threads_.end()) {
            // 新しいコルーチンは、レジストリに固定して(GC に回収されないように)、メインのスタックには残さない。残すと、新しい
            // 名前のキー列のたびに 1 つ積み上がり、20 個(LUA_MINSTACK)を超えると、スタックの容量の契約違反になる
            // (lua_newthread も lua_rawgeti も、空きを確かめない。配列の外へ書く)。参照は、プロセスの終わりまで解放しない
            auto thread = lua_newthread(L_);
            const int thread_ref = luaL_ref(L_, LUA_REGISTRYINDEX); // 値を取り出して、レジストリに固定する
            it = threads_.emplace(name, std::make_unique<ThreadParam>(thread, thread_ref)).first;
        }

        auto& param = *it->second;
        if (param.status_ == LUA_YIELD) {
            return ThreadError{name, "thread is already running"};
        }

        // ここに来るコルーチンは、新しいか、前の実行が終わった(LUA_OK)もの。失敗したものは、Settle が後始末をしてあるので、
        // 同じキーを押し直しても、関数は実行される
        lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
        lua_xmove(L_, param.L_, 1);
        const int status = lua_resume(param.L_, nullptr, 0, &param.nresult_);
        return Settle(name, param, status);
    }







    int Script::Command_Quit(lua_State* L)
    {
        std::println("quit");
        luaL_error(L, "error yo");
        return 0;
    }



}
