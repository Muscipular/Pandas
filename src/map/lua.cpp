#include "lua.hpp"
#include "script.hpp"

#include <atomic>
#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <stdlib.h> // atoi, strtol, strtoll, exit

#ifdef Pandas_ScriptEngine_Express
#include <cctype>	// toupper, tolower
#include <algorithm>	// transform
#endif // Pandas_ScriptEngine_Express

#ifdef Pandas_ScriptCommand_Preg_Search
#include <regex>
#endif // Pandas_ScriptCommand_Preg_Search

#ifdef PCRE_SUPPORT
#include <pcre.h> // preg_match
#endif

#include <common/cbasetypes.hpp>
#include <common/ers.hpp>  // ers_destroy
#include <common/malloc.hpp>
#include <common/md5calc.hpp>
#include <common/nullpo.hpp>
#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>
#include <common/utilities.hpp>
#include <common/utils.hpp>

#include "achievement.hpp"
#include "atcommand.hpp"
#include "battle.hpp"
#include "battleground.hpp"
#include "cashshop.hpp"
#include "channel.hpp"
#include "chat.hpp"
#include "chrif.hpp"
#include "clan.hpp"
#include "clif.hpp"
#include "date.hpp" // date type enum, date_get()
#include "elemental.hpp"
#include "guild.hpp"
#include "homunculus.hpp"
#include "instance.hpp"
#include "intif.hpp"
#include "itemdb.hpp"
#include "log.hpp"
#include "mail.hpp"
#include "map.hpp"
#include "mapreg.hpp"
#include "mercenary.hpp"
#include "mob.hpp"
#include "npc.hpp"
#include "party.hpp"
#include "path.hpp"
#include "pc.hpp"
#include "pc_groups.hpp"
#include "pet.hpp"
#include "quest.hpp"
#include "storage.hpp"
#include "asyncquery.hpp"

#ifdef Pandas_Aura_Mechanism
#include "aura.hpp"
#endif // Pandas_Aura_Mechanism

#ifdef Pandas_Item_Properties
#include "itemprops.hpp"
#endif // Pandas_Item_Properties

#ifdef Pandas_Database_MobItem_FixedRatio
#include "mobdrop.hpp"
#endif // Pandas_Database_MobItem_FixedRatio


#include <variant>
/// Pushes a value into the stack
#define push_val(stack,type,val) push_val2(stack, type, val, NULL)


DBMap* get_userfunc_db();

#define userfunc_db get_userfunc_db()

struct str_data_struct {
	enum c_op type;
	int str;
	int backpatch;
	int label;
	int (*func)(struct script_state* st);
	int64 val;
	int next;
	const char* name;
	bool deprecated;
};

str_data_struct* get_str_data();
int get_str_data_size();
#define str_data get_str_data()
char* get_str_buf();
#define str_buf get_str_buf()

struct script_data* push_val2(struct script_stack* stack, enum c_op type, int64 val, struct reg_db* ref);
struct script_data* push_str(struct script_stack* stack, enum c_op type, char* str);
struct script_data* push_retinfo(struct script_stack* stack, struct script_retinfo* ri, struct reg_db* ref);
struct script_data* get_val(struct script_state* st, struct script_data* data);

typedef struct script_function {
	int (*func)(struct script_state* st);
	const char* name;
	const char* arg;
	const char* deprecated;
} script_function;

extern script_function buildin_func[];


#define LUA_FUNC(fn) int fn(lua_State* L)
#define LUA_REGFUNC(s) luaL_Reg {#s, s}
#define LUA_REGFUNC2(s,ss) luaL_Reg {s, ss}

typedef struct { int code; } ERROR_RET;

typedef std::variant<const char*, int64_t, ERROR_RET> ARG_TYPE;


ARG_TYPE callScriptFn(script_state* st, const char* fn, std::vector<ARG_TYPE> n) {
	int i, j;
	struct script_retinfo* ri;
	struct script_code* scr;
	const char* str = fn;
	struct reg_db* ref = NULL;
	ARG_TYPE ret = ERROR_RET({ 0 });

	int stEnd = st->end;
	scr = (struct script_code*)strdb_get(userfunc_db, str);
	if (!scr) {
		ShowError("callScriptFn: Function not found! [%s]\n", str);
		st->state = END;
		return ret;
	}

	parse_script("{callfunc .@fn$;}", "tmp", 0, 0);
	auto subSt = script_alloc_state(scr, 0, st->rid, st->oid);
	subSt->refCount++;
	script_pushnil(subSt);
	//push_val(st->stack, c_op::C_NAME, fn);
	//push_str(st->stack, c_op::C_STR, aStrdup(funcname));
	push_val(subSt->stack, c_op::C_ARG, 0);

	for (i = 0, j = 0; i < n.size(); i++, j++) {
		if (n[i].index() == 0) {
			script_pushint64(subSt, std::get<int64_t>(n[i]));
		}
		else {
			script_pushconststr(subSt, std::get<const char*>(n[i]));
		}
	}


	CREATE(ri, struct script_retinfo, 1);
	ri->script = nullptr;              // script code
	ri->scope.vars = st->stack->scope.vars;   // scope variables
	ri->scope.arrays = st->stack->scope.arrays; // scope arrays
	ri->pos = st->pos;                 // script location
	ri->nargs = j;                       // argument count
	ri->defsp = st->stack->defsp;        // default stack pointer
	push_retinfo(st->stack, ri, ref);

	st->pos = 0;
	st->script = scr;
	st->stack->defsp = st->stack->sp;
	st->state = RUN;
	st->stack->scope.vars = i64db_alloc(DB_OPT_RELEASE_DATA);
	st->stack->scope.arrays = idb_alloc(DB_OPT_BASE);

	if (!st->script->local.vars)
		st->script->local.vars = i64db_alloc(DB_OPT_RELEASE_DATA);
	st->start = 0;
	run_script_main(st);
	if (st->end > stEnd) {
		auto retData = script_getdatatop(st, -1);
		if (data_isstring(retData)) {
			ret = conv_str(st, retData);
		}
		else if (data_isint(retData)) {
			ret = conv_num64(st, retData);
		}
		pop_stack(st, stEnd + 1, st->end);
	}
	else {
		return 0;
	}
	return ret;
}


bool checkSupport(const char* fn) {
	static std::vector<std::string> blockcmd = {
	"mes", "next", "close", "close2", "menu", "select", "prompt", "input",
	"openstorage", "guildopenstorage", "produce", "cooking", "birthpet",
	"callshop", "sleep", "sleep2", "openmail", "openauction", "progressbar",
	"buyingstore", "makerune", "opendressroom", "openstorage2"
	};

	std::vector<std::string>::iterator iter;
	std::string funcname = std::string(fn);
	std::transform(
		funcname.begin(), funcname.end(), funcname.begin(),
		static_cast<int(*)(int)>(std::tolower)
	);
	iter = std::find(blockcmd.begin(), blockcmd.end(), funcname);

	if (iter != blockcmd.end()) {
		return false;
	}
	return true;
}

LUA_FUNC(callScript) {
	int argN = lua_gettop(L);
	if (argN < 2) {
		return luaL_error(L, "callScript error: 0");
	}
	if (!luaL_checkUserData<script_state*>(L, 1)) {
		return luaL_error(L, "callScript error: 1");
	}
	if (!lua_isstring(L, 2)) {
		return luaL_error(L, "callScript error: 2");
	}
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	auto funcname = lua_tostring(L, 2);
	int fn = -1;
	for (int i = 0; i < 2000; i++) {
		if (buildin_func[i].name == NULL) {
			break;
		}
		if (strcmp(buildin_func[i].name, funcname) == 0) {
			fn = i;
			break;
		}
	}
	if (fn < 0) {
		return luaL_error(L, "callScript error: buildin_func not find %s", funcname);
	}
	if (!checkSupport(funcname)) {
		return luaL_error(L, "callScript error: buildin_func %s not support", funcname);
	}
	auto start0 = st->start;
	auto end0 = st->end;
	auto sp = st->stack->sp;
	auto funcname0 = st->funcname;
	push_val(st->stack, c_op::C_NAME, fn);
	//push_str(st->stack, c_op::C_STR, aStrdup(funcname));
	push_val(st->stack, c_op::C_ARG, 0);
	DBMap* m;
	DBMap* m2;
	reg_db scope;
	m2 = idb_alloc(DB_OPT_BASE);
	m = idb_alloc(DB_OPT_BASE);
	scope.arrays = m;
	scope.vars = m2;
	char str[256];
	for (int i = 3; i <= argN; i++) {
		if (lua_isnumber(L, i)) {
			push_val(st->stack, c_op::C_INT, lua_tonumber(L, i));
		}
		else if (lua_isstring(L, i)) {
			push_str(st->stack, c_op::C_STR, aStrdup(lua_tostring(L, i)));
		}
		else if (lua_istable(L, i)) {
			if (lua_objlen(L, i) > 0) {
				lua_rawgeti(L, i, 1);
				if (lua_isnumber(L, -1)) {
					sprintf(str, ".@lua_arg_%d", i);
					int uid = add_str(str);
					idb_i64put(m2, uid, (int64)lua_tonumber(L, -1));
					push_val2(st->stack, c_op::C_NAME, uid, &scope);
				}
				else if (lua_isstring(L, -1)) {
					sprintf(str, ".@lua_arg_%d$", i);
					int uid = add_str(str);
					idb_put(m2, uid, aStrdup(lua_tostring(L, -1)));
					push_val2(st->stack, c_op::C_NAME, uid, &scope);
				}
				else {
					sprintf(str, ".@lua_arg_%d", i);
					int uid = add_str(str);
					idb_i64put(m2, uid, 0);
					push_val2(st->stack, c_op::C_NAME, uid, &scope);
				}
				lua_pop(L, 1);
			}
			else {
				lua_getfield(L, i, "type");
				c_op type = (c_op)(uint64)lua_touserdata(L, -1);
				lua_pop(L, 1);
				lua_getfield(L, i, "num1");
				int64 uid = (int)(uint64)lua_touserdata(L, -1);
				lua_pop(L, 1);
				lua_getfield(L, i, "num2");
				uid |= ((int64)(uint64)lua_touserdata(L, -1) << 32);
				lua_pop(L, 1);
				lua_getfield(L, i, "ref");
				reg_db* ref = (reg_db*)lua_touserdata(L, -1);
				lua_pop(L, 1);
				push_val2(st->stack, type, uid, ref);
			}
		}
		else if(luaL_checkUserData<int64_t>(L, i)) {
			push_val(st->stack, c_op::C_INT, luaL_toUserData<int64_t>(L, i)->st);
		}
		else {
			push_val(st->stack, c_op::C_INT, 0);
		}
	}

	st->start = end0;
	st->end = st->stack->sp;
	int ret = buildin_func[fn].func(st);
	auto retData = script_getdatatop(st, -1);
	script_data data;
	memcpy(&data, retData, sizeof(script_data));
	auto retDataActual = get_val(st, retData);
	if (data_isstring(retDataActual)) {
		lua_pushstring(L, retData->u.str);
	}
	else if (data_isint(retDataActual)) {
		lua_pushinteger(L, retData->u.num);
	}
	else {
		lua_pushnil(L);
	}
	int count = 1;
	if (data_isreference(&data)) {
		lua_newtable(L);
		lua_pushlightuserdata(L, (void*)(int)data.type);
		lua_setfield(L, -2, "type");
		lua_pushlightuserdata(L, (void*)(data.u.num & 0xffffffff));
		lua_setfield(L, -2, "num1");
		lua_pushlightuserdata(L, (void*)((data.u.num >> 32) & 0xffffffff));
		lua_setfield(L, -2, "num2");
		lua_pushlightuserdata(L, data.ref);
		lua_setfield(L, -2, "ref");
		count++;
	}
	if (ret != SCRIPT_CMD_SUCCESS) {
		lua_pop(L, count);
		pop_stack(st, end0 - 1, st->end);
		st->start = start0;
		st->end = end0;
		st->funcname = funcname0;
		db_destroy(m);
		db_destroy(m2);
		return luaL_error(L, "callScript error: buildin_func %s exec failed", funcname);
	}
	for (int i = 3; i <= argN; i++) {
		if (lua_istable(L, i)) {
			if (lua_objlen(L, i) > 0) {
				auto d = script_getdata(st, i - 1);
				if (is_string_variable(reference_getname(d))) {
					lua_pushstring(L, conv_str(st, d));
				}
				else {
					lua_pushnumber(L, conv_num64(st, d));
				}
				lua_rawseti(L, i, 1);
			}
		}
	}
	pop_stack(st, end0 - 1, st->end);
	st->start = start0;
	st->end = end0;
	st->stack->sp = sp;
	st->funcname = funcname0;
	db_destroy(m);
	db_destroy(m2);
	return count;
}

LUA_FUNC(sleep) {
	if (!lua_isuserdata(L, 1)) {
		return luaL_error(L, "sleep error: 1");
	}
	auto st = ((UserData<script_state*>*)lua_touserdata(L, 1))->st;
	//if (st->sleep.tick == 0) {
	int ticks;

	ticks = luaL_optinteger(L, 2, 0);

	if (ticks <= 0) {
		ShowError("buildin_sleep: negative or zero amount('%d') of milli seconds is not supported\n", ticks);
		lua_pushboolean(L, false);
		return 1;
	}

	// detach the player
	script_detach_rid(st);

	// sleep for the target amount of time
	st->state = RERUNLINE;
	st->sleep.tick = ticks;
	int n = lua_yield(L, 0);
	if (n > 0) {
		lua_pop(L, n);
	}
	lua_pushboolean(L, true);
	return 1;
	// Second call(by timer after sleeping time is over)
	//}
}

LUA_FUNC(ToString) {
	if (lua_gettop(L) != 1) {
		lua_pushnil(L);
		return 1;
	}
	if (lua_type(L, 1) != LUA_TUSERDATA) {
		lua_pushnil(L);
		return 1;
	}
	char buff[256] = { 0 };
	sprintf(buff, "ScriptState@0x%p", lua_touserdata(L, 1));
	lua_pushstring(L, buff);
	return 1;
}

LUA_FUNC(INT64ToString) {
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

//void script_set_constant_(const char* name, int64 value, const char* constant_name, bool isparameter, bool deprecated);
#define script_set_constant_(n,v,c,p,d) {luaL_newUserData<int64_t>(L, v); lua_setfield(L, -2, c? c:n);}
#define script_set_constant(n,v,p,d) {luaL_newUserData<int64_t>(L, v); lua_setfield(L, -2, n);}
//#define script_set_constant_()

bool init_lua() {
	if (m_lua) {
		return false;
	}
	m_lua = luaL_newstate();
	if (!m_lua) {
		return false;
	}
	auto L = m_lua;
	luaL_openlibs(m_lua);
	auto ret = true;
	lua_settop(L, 0);
	lua_newtable(L);
	lua_pushstring(L, "callScript");
	lua_pushcfunction(L, callScript);
	lua_rawset(L, -3);
	lua_pushstring(L, "sleep");
	lua_pushcfunction(L, sleep);
	lua_rawset(L, -3);
	// 1
	static char cmd[2048] = "return function(fn) return function(st, ...) return st:callScript(fn, ...) end end";
	luaL_loadstring(L, cmd); //2
	lua_call(L, 0, 1);
	for (size_t i = 0; i < 4096; i++) {
		if (buildin_func[i].name == nullptr) {
			break;
		}
		if (buildin_func[i].name && strlen(buildin_func[i].name) > 0) {
			if (strcmp(buildin_func[i].name, "return") == 0) {
				continue;
			}
			lua_pushstring(L, buildin_func[i].name); //3
			lua_pushvalue(L, 2);
			lua_pushstring(L, buildin_func[i].name); //4
			lua_call(L, 1, 1);
			lua_rawset(L, 1);
		}
	}

	luaL_newmetatable(m_lua, "ScriptState");
	lua_pushstring(L, "__index");
	lua_pushvalue(L, 1);
	lua_rawset(L, -3);
	lua_pushstring(L, "__tostring");
	lua_pushcfunction(L, ToString);
	lua_rawset(L, -3);
	luaL_newmetatable(m_lua, "INT64");
	lua_pushstring(L, "__tostring");
	lua_pushcfunction(L, INT64ToString);
	lua_rawset(L, -3);
	lua_newtable(L);	
#include "script_constants.hpp"

	lua_setglobal(L, "CONST");
	lua_settop(L, 0);
	
	ret = luaL_dostring(m_lua, "pcall(function() dofile('npc/init.lua') end);");
	if (ret) {
		return false;
	}
	return true;
}

int resume_lua(script_state* st, lua_State* L, int n) {
	int ret = lua_resume(L, n);
	if (ret == LUA_YIELD) {
		st->lua_state.thread = L;
	}
	else {
		if (ret != 0) {
			printf("lua error: %s\n", lua_tostring(m_lua, -1));
		}
		if (L == st->lua_state.thread) {
			st->lua_state.thread = nullptr;
		}
		lua_close(L);
	}
	return ret;
}
