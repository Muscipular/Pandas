#pragma once
#include <cstdint>
#include <type_traits>
#include <sys/stat.h>

extern "C" {
#include "../../3rdparty/lua/lua.h"
#include "../../3rdparty/lua/lualib.h"
#include "../../3rdparty/lua/lauxlib.h"
}
extern lua_State* m_lua;
bool init_lua();
enum e_user_data
{
	ut_none,
	ut_script_state,
	ut_script_data,
	ut_int64,
};

enum e_lua_cmd
{
	elc_sleep,
	elc_close,
	elc_next,
	elc_select,
};

#define RESUME_NAME(N) resume_##N
#define RESUME_FUNC(N) script_cmd_result RESUME_NAME(N)(script_state* st, int& ret)
//typedef enum script_cmd_result (lua_Resume_Func*)(script_state* st, int& ret);
template<typename T = void>
struct UserData {
	e_user_data type;
	T st;
};
struct script_data;
struct script_state;

template<typename T>
inline e_user_data UserDataTypeFor() {
	if constexpr (std::is_same_v<T, script_state*>) {
		return ut_script_state;
	}
	else if constexpr (std::is_same_v<T, script_data>) {
		return ut_script_state;
	}
	else if constexpr (std::is_same_v<T, int64_t>) {
		return  ut_int64;
	}
	else {
		return T();
	}
}

int inline luaL_ref(lua_State* L) { return luaL_ref(L, LUA_REGISTRYINDEX); }
void inline luaL_unref(lua_State* L, int ref) {
	luaL_unref(L, LUA_REGISTRYINDEX, ref);
	lua_gc(L, LUA_GCCOLLECT, 0);
}

template<typename T>
inline const char* UserDataMetaTableFor() {
	if constexpr (std::is_same_v<T, script_state*>) {
		return "ScriptState";
	}
	else if constexpr (std::is_same_v<T, script_data>) {
		return "ScriptData";
	}
	else if constexpr (std::is_same_v<T, int64_t>) {
		return  "INT64";
	}
	else {
		return T();
	}
}

template<typename T>
inline UserData<T>* luaL_newUserData(lua_State* L, T v) {
	auto d = static_cast<UserData<T>*>(lua_newuserdata(L, sizeof(UserData<T>)));
	d->type = UserDataTypeFor<T>();
	d->st = v;
	luaL_setmetatable(L, UserDataMetaTableFor<T>());
	return d;
}

template<typename T>
inline bool luaL_checkUserData(lua_State* L, int n) {
	if (!lua_isuserdata(L, n)) {
		return false;
	}
	auto d = static_cast<UserData<T>*>(lua_touserdata(L, n));
	e_user_data type = UserDataTypeFor<T>();
	return d->type == type;
}

template<typename T>
inline UserData<T>* luaL_toUserData(lua_State* L, int n) {
	if (!lua_isuserdata(L, n)) {
		return nullptr;
	}
	auto d = static_cast<UserData<T>*>(lua_touserdata(L, n));
	e_user_data type = UserDataTypeFor<T>();
	if (d->type == type) {
		return d;
	}
	return nullptr;
}
