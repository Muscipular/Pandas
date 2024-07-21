#include "lua.hpp"
#include "script.hpp"

#include <atomic>
#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <stdlib.h> // atoi, strtol, strtoll, exit

#define LUA_FUNC_NAME2(K, fn) K##_##fn
#define LUA_FUNC2(K, fn) int LUA_FUNC_NAME2(K, fn)(lua_State* L)

LUA_FUNC2(INT64, add) {
	int64_t r;
	if (luaL_checkUserData<int64_t>(L, 1)) {
		r = luaL_toUserData<int64_t>(L, 1)->st;
	}
	else {
		r = lua_tointeger(L, 1);
	}
	if (luaL_checkUserData<int64_t>(L, 2)) {
		r += luaL_toUserData<int64_t>(L, 2)->st;
	}
	else {
		r += lua_tointeger(L, 2);
	}
	luaL_newUserData(L, r);
	return 1;
}

LUA_FUNC2(INT64, sub) {
	int64_t r;
	if (luaL_checkUserData<int64_t>(L, 1)) {
		r = luaL_toUserData<int64_t>(L, 1)->st;
	}
	else {
		r = lua_tointeger(L, 1);
	}
	if (luaL_checkUserData<int64_t>(L, 2)) {
		r -= luaL_toUserData<int64_t>(L, 2)->st;
	}
	else {
		r -= lua_tointeger(L, 2);
	}
	luaL_newUserData(L, r);
	return 1;
}

LUA_FUNC2(INT64, mul) {
	int64_t r;
	if (luaL_checkUserData<int64_t>(L, 1)) {
		r = luaL_toUserData<int64_t>(L, 1)->st;
	}
	else {
		r = lua_tointeger(L, 1);
	}
	if (luaL_checkUserData<int64_t>(L, 2)) {
		r *= luaL_toUserData<int64_t>(L, 2)->st;
	}
	else {
		r *= lua_tointeger(L, 2);
	}
	luaL_newUserData(L, r);
	return 1;
}

LUA_FUNC2(INT64, div) {
	int64_t r;
	if (luaL_checkUserData<int64_t>(L, 1)) {
		r = luaL_toUserData<int64_t>(L, 1)->st;
	}
	else {
		r = lua_tointeger(L, 1);
	}
	if (luaL_checkUserData<int64_t>(L, 2)) {
		r /= luaL_toUserData<int64_t>(L, 2)->st;
	}
	else {
		r /= lua_tointeger(L, 2);
	}
	luaL_newUserData(L, r);
	return 1;
}

LUA_FUNC2(INT64, mod) {
	int64_t r;
	if (luaL_checkUserData<int64_t>(L, 1)) {
		r = luaL_toUserData<int64_t>(L, 1)->st;
	}
	else {
		r = lua_tointeger(L, 1);
	}
	if (luaL_checkUserData<int64_t>(L, 2)) {
		r %= luaL_toUserData<int64_t>(L, 2)->st;
	}
	else {
		r %= lua_tointeger(L, 2);
	}
	luaL_newUserData(L, r);
	return 1;
}

LUA_FUNC2(INT64, pow) {
	int64_t r;
	if (luaL_checkUserData<int64_t>(L, 1)) {
		r = luaL_toUserData<int64_t>(L, 1)->st;
	}
	else {
		r = lua_tointeger(L, 1);
	}
	if (luaL_checkUserData<int64_t>(L, 2)) {
		r ^= luaL_toUserData<int64_t>(L, 2)->st;
	}
	else {
		r ^= lua_tointeger(L, 2);
	}
	luaL_newUserData(L, r);
	return 1;
}

LUA_FUNC2(INT64, unm) {
	int64_t r;
	if (luaL_checkUserData<int64_t>(L, 1)) {
		r = luaL_toUserData<int64_t>(L, 1)->st;
	}
	else {
		r = lua_tointeger(L, 1);
	}
	luaL_newUserData(L, -r);
	return 1;
}

LUA_FUNC2(INT64, eq) {
	int64_t r;
	if (luaL_checkUserData<int64_t>(L, 1)) {
		r = luaL_toUserData<int64_t>(L, 1)->st;
	}
	else {
		r = lua_tointeger(L, 1);
	}
	if (luaL_checkUserData<int64_t>(L, 2)) {
		lua_pushboolean(L, r == luaL_toUserData<int64_t>(L, 2)->st);
	}
	else {
		lua_pushboolean(L, r == lua_tointeger(L, 2));
	}
	return 1;
}

LUA_FUNC2(INT64, lt) {
	int64_t r;
	if (luaL_checkUserData<int64_t>(L, 1)) {
		r = luaL_toUserData<int64_t>(L, 1)->st;
	}
	else {
		r = lua_tointeger(L, 1);
	}
	if (luaL_checkUserData<int64_t>(L, 2)) {
		lua_pushboolean(L, r < luaL_toUserData<int64_t>(L, 2)->st);
	}
	else {
		lua_pushboolean(L, r < lua_tointeger(L, 2));
	}
	return 1;
}

LUA_FUNC2(INT64, le) {
	int64_t r;
	if (luaL_checkUserData<int64_t>(L, 1)) {
		r = luaL_toUserData<int64_t>(L, 1)->st;
	}
	else {
		r = lua_tointeger(L, 1);
	}
	if (luaL_checkUserData<int64_t>(L, 2)) {
		lua_pushboolean(L, r <= luaL_toUserData<int64_t>(L, 2)->st);
	}
	else {
		lua_pushboolean(L, r <= lua_tointeger(L, 2));
	}
	return 1;
}

LUA_FUNC2(INT64, tostring) {
	if (lua_gettop(L) != 1) {
		lua_pushnil(L);
		return 1;
	}
	if (!luaL_checkUserData<int64_t>(L, 1)) {
		lua_pushnil(L);
		return 1;
	}
	auto ud = luaL_toUserData<int64_t>(L, 1);
	char buff[256] = { 0 };
	sprintf(buff, "%" PRId64, ud->st);
	lua_pushstring(L, buff);
	return 1;
}

LUA_FUNC2(G, tonumber) {
	int64_t r;
	if (luaL_checkUserData<int64_t>(L, 1)) {
		r = luaL_toUserData<int64_t>(L, 1)->st;
	}
	else {
		r = lua_tointeger(L, 1);
	}
	if (r >= INT32_MIN || r <= INT32_MAX) {
		lua_pushinteger(L, r);
	}
	else {
		lua_pushnumber(L, r);
	}
	return 1;
}

LUA_FUNC2(G, newI64) {
	int64_t r;
	if (luaL_checkUserData<int64_t>(L, 1)) {
		r = luaL_toUserData<int64_t>(L, 1)->st;
	}
	else if (lua_type(L, 1) == LUA_TSTRING) {
		r = std::strtoll(lua_tostring(L, 1), nullptr, 0);
	}
	else {
		r = lua_tonumber(L, 1);
	}
	luaL_newUserData(L, r);
	return 1;
}


std::mt19937 mt(std::clock());
std::minstd_rand stdRand(time(nullptr));

LUA_FUNC2(G, mtRand) {
	int n = lua_gettop(L);
	if (n == 1) {
		int max = lua_tointeger(L, 1);
		if (max <= 1) {
			lua_pushinteger(L, 1);
			return 1;
		}
		std::uniform_int_distribution<int> dist(1, max);
		lua_pushinteger(L, dist(mt));
		return 1;
	}
	if (n == 0) {
		std::uniform_real_distribution<double> dist(0.0, 1.0);
		lua_pushnumber(L, dist(mt));
		return 1;
	}
	if (n == 2) {
		int min = lua_tointeger(L, 1);
		int max = lua_tointeger(L, 2);
		if (min > max) {
			std::swap(min, max);
		}
		if (min == max) {
			lua_pushinteger(L, min);
			return 1;
		}
		std::uniform_int_distribution<int> dist(min, max);
		lua_pushinteger(L, dist(mt));
		return 1;
	}
	else
	{
		std::uniform_int_distribution<int> dist(1, n);
		lua_pushvalue(L, dist(mt));
		return 1;
	}
	return luaL_error(L, "mtRand: invalid parameter");
}

LUA_FUNC2(G, stdRand) {
	int n = lua_gettop(L);
	if (n == 1) {
		int max = lua_tointeger(L, 1);
		if (max <= 1) {
			lua_pushinteger(L, 1);
			return 1;
		}
		std::uniform_int_distribution<int> dist(1, max);
		lua_pushinteger(L, dist(stdRand));
		return 1;
	}
	if (n == 0) {
		std::uniform_real_distribution<double> dist(0.0, 1.0);
		lua_pushnumber(L, dist(stdRand));
		return 1;
	}
	if (n == 2) {
		int min = lua_tointeger(L, 1);
		int max = lua_tointeger(L, 2);
		if (min > max) {
			std::swap(min, max);
		}
		if (min == max) {
			lua_pushinteger(L, min);
			return 1;
		}
		std::uniform_int_distribution<int> dist(min, max);
		lua_pushinteger(L, dist(stdRand));
		return 1;
	}
	else
	{
		std::uniform_int_distribution<int> dist(1, n);
		lua_pushvalue(L, dist(stdRand));
		return 1;
	}
	return luaL_error(L, "mtRand: invalid parameter");
}

#define SET_META_FN(K, N) { lua_pushstring(L, "__" #N);\
lua_pushcfunction(L, LUA_FUNC_NAME2(K, N));\
lua_rawset(L, -3);}

static void reg_int64(lua_State* L) {
	luaL_newmetatable(L, UserDataMetaTableFor<int64_t>());
	SET_META_FN(INT64, tostring);
	SET_META_FN(INT64, add);
	SET_META_FN(INT64, sub);
	SET_META_FN(INT64, mul);
	SET_META_FN(INT64, div);
	SET_META_FN(INT64, mod);
	SET_META_FN(INT64, pow);
	SET_META_FN(INT64, unm);
	SET_META_FN(INT64, eq);
	SET_META_FN(INT64, lt);
	SET_META_FN(INT64, le);
	lua_setglobal(L, UserDataMetaTableFor<int64_t>());


	lua_pushcfunction(L, LUA_FUNC_NAME2(G, tonumber));
	lua_setglobal(L, "I64ToNumber");
	lua_pushcfunction(L, LUA_FUNC_NAME2(G, newI64));
	lua_setglobal(L, "newI64");
	lua_pushcfunction(L, LUA_FUNC_NAME2(G, mtRand));
	lua_setglobal(L, "mtRand");
	lua_pushcfunction(L, LUA_FUNC_NAME2(G, stdRand));
	lua_setglobal(L, "stdRand");
}

void lua_reg_ctype(lua_State* L) {
	reg_int64(L);
}
