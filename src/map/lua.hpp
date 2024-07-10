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
	ut_int64,
};

template<typename T = void>
struct UserData {
	e_user_data type;
	T st;
};

struct script_state;

template<typename T = void>
inline e_user_data UserDataTypeFor()
{
	if (std::is_same<T, script_state*>()) {
		return ut_script_state;
	}
	if (std::is_same<T, int64_t>()) {
		return  ut_int64;
	}
	return ut_none;
}

template<typename T = void>
inline const char* UserDataMetaTableFor() {
	if (std::is_same<T, script_state*>()) {
		return "ScriptState";
	}
	if (std::is_same<T, int64_t>()) {
		return  "INT64";
	}
	return "";
}

template<typename T = void>
inline UserData<T>* luaL_newUserData(lua_State* L, T v) {
	auto d = static_cast<UserData<T>*>(lua_newuserdata(L, sizeof(UserData<T>)));
	d->type = UserDataTypeFor<T>();
	d->st = v;
	luaL_setmetatable(L, UserDataMetaTableFor<T>());
	return d;
}

template<typename T = void>
inline bool luaL_checkUserData(lua_State* L, int n) {
	if (!lua_isuserdata(L, n)) {
		return false;
	}
	auto d = static_cast<UserData<T>*>(lua_touserdata(L, n));
	e_user_data type = UserDataTypeFor<T>();
	return d->type == type;
}

template<typename T = void>
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
