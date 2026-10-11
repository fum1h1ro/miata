#include "StatusBarScript.h"
#include <cstring>
#include <sys/stat.h>
#include "Format.h"
#include "Script.h"

namespace miata {
    namespace {
        // 以下の関数は、Lua の C API だけを呼ぶ(Lua のエラーは longjmp。デストラクタを持つオブジェクトは、この中で作らない
        // ほうがよい。FormatXxx の戻り値は一時の std::string で、メモリ不足のときだけ、漏れる)
        void SetString(lua_State* L, const char* key, const std::string& value)
        {
            lua_pushlstring(L, value.data(), value.size());
            lua_setfield(L, -2, key);
        }

        void SetInteger(lua_State* L, const char* key, lua_Integer value)
        {
            lua_pushinteger(L, value);
            lua_setfield(L, -2, key);
        }

        void SetBoolean(lua_State* L, const char* key, bool value)
        {
            lua_pushboolean(L, value ? 1 : 0);
            lua_setfield(L, -2, key);
        }

        // ctx.entry: カーソル下のファイル
        void PushEntry(lua_State* L, const views::StatusEntry& entry)
        {
            lua_createtable(L, 0, 16);
            SetString(L, "name", entry.name);
            SetString(L, "path", entry.path);
            SetBoolean(L, "is_dir", entry.is_dir);
            SetBoolean(L, "is_link", entry.is_link);
            SetBoolean(L, "is_alias", entry.is_alias);
            if (entry.info) SetFileInfoFields(L, *entry.info);
            else SetString(L, "error", entry.error);
        }

        void PushMarks(lua_State* L, const views::MarkSummary& marks)
        {
            lua_createtable(L, 0, 5);
            SetInteger(L, "count", marks.count);
            SetInteger(L, "dirs", marks.dirs);
            SetInteger(L, "bytes", (lua_Integer)marks.bytes);
            SetString(L, "size_text", FormatSize(marks.bytes));
            SetInteger(L, "hidden", marks.hidden);
        }

        void PushPending(lua_State* L, const views::StatusPending& pending)
        {
            lua_createtable(L, 0, 2);
            SetString(L, "keys", pending.keys);
            lua_createtable(L, (int)pending.candidates.size(), 0);
            lua_Integer index = 1;
            for (const auto& candidate : pending.candidates) {
                lua_createtable(L, 0, 3);
                SetString(L, "key", candidate.key);
                SetString(L, "desc", candidate.desc);
                SetBoolean(L, "leaf", candidate.leaf);
                lua_rawseti(L, -2, index++);
            }
            lua_setfield(L, -2, "candidates");
        }

        // ctx(表)を積む
        void PushContext(lua_State* L, const views::StatusSnapshot& snapshot, const views::StatusText& defaults)
        {
            lua_createtable(L, 0, 10);
            SetString(L, "pane", snapshot.pane);
            SetString(L, "dir", snapshot.dir);
            SetBoolean(L, "dialog", snapshot.dialog);
            SetInteger(L, "shown", snapshot.shown);
            SetInteger(L, "total", snapshot.total);
            if (snapshot.entry) {
                PushEntry(L, *snapshot.entry);
                lua_setfield(L, -2, "entry");
            }
            PushMarks(L, snapshot.marks);
            lua_setfield(L, -2, "marks");
            if (snapshot.pending) {
                PushPending(L, *snapshot.pending);
                lua_setfield(L, -2, "pending");
            }
            lua_createtable(L, 0, 2);
            SetString(L, "left", defaults.left);
            SetString(L, "right", defaults.right);
            lua_setfield(L, -2, "default");
        }

        struct Call {
            int ref;
            const views::StatusSnapshot* snapshot;
            const views::StatusText* defaults;
        };

        // lua_pcall される C 関数(引数 1 つ: Call* の lightuserdata)。ctx を組み、関数を呼び、戻り値の型を検証して、
        // 戻り値を 1 つ残す(読み取りは、呼び出した後に、C++ の側で行う)。この中では、デストラクタを持つオブジェクトを作らない
        // (Lua のエラーは longjmp)。表の組み立てのメモリ不足も、関数のエラーも、外側の pcall が受ける
        int InvokeStatusFunction(lua_State* L)
        {
            const Call* call = static_cast<const Call*>(lua_touserdata(L, 1));
            lua_settop(L, 0);
            lua_rawgeti(L, LUA_REGISTRYINDEX, call->ref);
            PushContext(L, *call->snapshot, *call->defaults);
            lua_call(L, 1, 1);

            const int type = lua_type(L, 1);
            if (type == LUA_TNIL || type == LUA_TSTRING) return 1;
            if (type != LUA_TTABLE) {
                return luaL_error(L, "statusbar function: expected nil, a string or a table {left=, right=} to be returned (got %s)", luaL_typename(L, 1));
            }
            // 表: キーは left / right だけ(タイプミスを黙って捨てない)、値は文字列か数値
            lua_pushnil(L);
            while (lua_next(L, 1) != 0) {
                if (lua_type(L, -2) != LUA_TSTRING) {
                    return luaL_error(L, "statusbar function: the returned table may only have the keys left and right");
                }
                const char* key = lua_tostring(L, -2);
                if (std::strcmp(key, "left") != 0 && std::strcmp(key, "right") != 0) {
                    return luaL_error(L, "statusbar function: unknown key '%s' in the returned table (expected left or right)", key);
                }
                const int value_type = lua_type(L, -1);
                if (value_type != LUA_TSTRING && value_type != LUA_TNUMBER) {
                    return luaL_error(L, "statusbar function: %s must be a string (got %s)", key, luaL_typename(L, -1));
                }
                lua_pop(L, 1);
            }
            return 1;
        }
    }

    void SetFileInfoFields(lua_State* L, const pl_file_info& info)
    {
        SetString(L, "perm", FormatMode(info.mode));
        SetInteger(L, "mode", (lua_Integer)(info.mode & 07777));
        SetString(L, "owner", info.owner);
        SetString(L, "group", info.group);
        SetInteger(L, "uid", (lua_Integer)info.uid);
        SetInteger(L, "gid", (lua_Integer)info.gid);
        SetInteger(L, "mtime", (lua_Integer)info.mtime);
        SetString(L, "mtime_text", FormatLocalTime((std::time_t)info.mtime));
        SetBoolean(L, "dataless", info.dataless);
        // サイズは、通常のファイルだけ(フォルダの st_size は意味が無い。リンクは、リンク先の文字列の長さ)
        if ((info.mode & S_IFMT) == S_IFREG) {
            SetInteger(L, "size", (lua_Integer)info.size);
            SetString(L, "size_text", FormatSize(info.size));
        }
        if ((info.mode & S_IFMT) == S_IFLNK && !info.link.empty()) {
            SetString(L, "link", info.link);
        }
    }

    void PushFileInfoTable(lua_State* L, const pl_file_info& info)
    {
        lua_createtable(L, 0, 14);
        // 種類は、リンクそのもの(フォルダへのリンクは is_dir=false, is_link=true。ctx.entry の is_dir は、リンク先を見る)
        SetBoolean(L, "is_dir", (info.mode & S_IFMT) == S_IFDIR);
        SetBoolean(L, "is_link", (info.mode & S_IFMT) == S_IFLNK);
        SetFileInfoFields(L, info);
    }

    std::expected<views::StatusOverride, std::string> CallStatusBarFunction(
        int fn_ref,
        const views::StatusSnapshot& snapshot,
        const views::StatusText& defaults
    )
    {
        Call call{fn_ref, &snapshot, &defaults};
        views::StatusOverride result;

        auto read = [&result](lua_State* L) {
            // 戻り値(スタックの先頭)。Entry が、nil / 文字列 / {left, right}(値は文字列か数値)に検証済み
            auto read_string = [L](int index) {
                size_t length = 0;
                const char* text = lua_tolstring(L, index, &length); // 数値は、文字列にする
                return std::string(text ? text : "", length);
            };
            const int type = lua_type(L, -1);
            if (type == LUA_TSTRING) {
                result.left = read_string(-1);
            }
            else if (type == LUA_TTABLE) {
                // **rawget で読む**: lua_getfield は、メタテーブルの __index を起動する。ここは、保護(pcall)と命令数の上限の
                // 外なので、表がメタテーブルを持っていると、保護なしの Lua が走る(エラーは abort、ループは UI の停止)
                lua_pushstring(L, "left");
                lua_rawget(L, -2);
                if (!lua_isnil(L, -1)) result.left = read_string(-1);
                lua_pop(L, 1);
                lua_pushstring(L, "right");
                lua_rawget(L, -2);
                if (!lua_isnil(L, -1)) result.right = read_string(-1);
                lua_pop(L, 1);
            }
        };

        auto r = Script::Instance().CallProtected(InvokeStatusFunction, &call, kStatusBarInstructionLimit, read);
        if (!r) return std::unexpected(r.error());
        return result;
    }
}
