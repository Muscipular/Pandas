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
const char* get_val2_str(struct script_state* st, int64 uid, struct reg_db* ref);
int64 get_val2_num(struct script_state* st, int64 uid, struct reg_db* ref);
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


#define LUA_FUNC(fn) int __cdecl fn(lua_State* L)
#define LUA_REGFUNC(s) luaL_Reg {#s, s}
#define LUA_REGFUNC2(s,ss) luaL_Reg {s, ss}

#define LUA_CHECK_ST(F) { if (!luaL_checkUserData<script_state*>(L, 1)) return luaL_error(L, "buildin_" #F " error: invalid st.");  }

#define LUA_YIELD_RET(N, ref) {	st->lua_state.refVar = ref;\
st->lua_state.lastCmd = elc_##N;\
st->lua_state.fn = RESUME_NAME(N);\
return lua_yield(L, 0); }

typedef struct { int code; } ERROR_RET;

typedef std::variant<const char*, int64_t, ERROR_RET> ARG_TYPE;

std::vector<Const>* constList = new std::vector<Const>();

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

std::map<std::string, int> buildin_fn_map;

bool checkSupport(const char* fn) {
	static std::vector<std::string> blockcmd = {
	"mes", "next", "close",  "close3", "close2", "menu", "select", "prompt", "input", "end",
	"sleep", "sleep2",  "progressbar", "progressbar_npc",
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

void free_stack(script_state* st, char* funcname0, int oStart, DBMap*& vars, DBMap*& arrays) {
	/*ShowDebug("torelease  start: %d end: %d sp: %d \n", st->start, st->end, st->stack->sp);*/
	pop_stack(st, st->start, st->stack->sp);
	st->start = oStart;/*
	st->end = end0;
	st->stack->sp = sp;*/
	st->funcname = funcname0;
	/*ShowDebug("release    start: %d end: %d sp: %d \n", st->start, st->end, st->stack->sp);*/
	arrays->destroy(arrays, script_free_array_db);
	arrays = nullptr;
	script_free_vars(vars);
	vars = nullptr;
}

/**
 * \brief
 * \param L
 * \return
 */
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
	auto it = buildin_fn_map.find(funcname);
	if (it != buildin_fn_map.end()) {
		fn = it->second;
	}
	if (fn < 0) {
		return luaL_error(L, "callScript error: buildin_func not find %s", funcname);
	}
	if (!checkSupport(funcname)) {
		return luaL_error(L, "callScript error: buildin_func %s not support", funcname);
	}
	//ShowDebug("begin      start: %d end: %d sp: %d %s\n", st->start, st->end, st->stack->sp, funcname);
	auto start0 = st->start;
	auto end0 = st->end;
	auto sp = st->stack->sp;
	auto funcname0 = st->funcname;
	push_val(st->stack, c_op::C_NAME, fn);
	//push_str(st->stack, c_op::C_STR, aStrdup(funcname));
	push_val(st->stack, c_op::C_ARG, 0);
	DBMap* map_arrays;
	DBMap* map_vars;
	reg_db scope;
	map_vars = idb_alloc(DB_OPT_RELEASE_DATA);
	map_arrays = idb_alloc(DB_OPT_BASE);
	scope.arrays = map_arrays;
	scope.vars = map_vars;
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
					idb_i64put(map_vars, uid, (int64)lua_tonumber(L, -1));
					//ShowDebug("in %d:%d %d %s %s\n", i, argN, uid, str, str_buf + str_data[uid].str);
					push_val2(st->stack, c_op::C_NAME, uid, &scope);
				}
				else if (lua_isstring(L, -1)) {
					sprintf(str, ".@lua_arg_%d$", i);
					int uid = add_str(str);
					idb_put(map_vars, uid, aStrdup(lua_tostring(L, -1)));
					//ShowDebug("in %d:%d %d %s %s\n", i, argN, uid, str, str_buf + str_data[uid].str);
					push_val2(st->stack, c_op::C_NAME, uid, &scope);
				}
				else {
					sprintf(str, ".@lua_arg_%d", i);
					int uid = add_str(str);
					idb_i64put(map_vars, uid, 0);
					//ShowDebug("in %d:%d %d %s %s\n", i, argN, uid, str, str_buf + str_data[uid].str);
					push_val2(st->stack, c_op::C_NAME, uid, &scope);
				}
				lua_pop(L, 1);
			}
			/*
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
			*/
		}
		else if (luaL_checkUserData<int64_t>(L, i)) {
			push_val(st->stack, c_op::C_INT, luaL_toUserData<int64_t>(L, i)->st);
		}
		else if (luaL_checkUserData<script_data>(L, i)) {
			auto sRef = luaL_toUserData<script_data>(L, i);
			push_val2(st->stack, sRef->st.type, sRef->st.u.num, sRef->st.ref);
		}
		else {
			push_val(st->stack, c_op::C_INT, 0);
		}
	}

	st->start = sp;
	st->end = st->stack->sp;
	st->stack->defsp = sp;
	auto osp = st->stack->sp;
	st->funcname = (char*)funcname;
	//ShowDebug("> stack sp %d:%d %d %d\n", st->start, st->end, st->end - st->start, st->stack->sp);
	int ret = buildin_func[fn].func(st);
	//ShowDebug("< stack sp %d:%d %d %d\n", st->start, st->end, st->end - st->start, st->stack->sp);
	int count = 0;
	if (st->stack->sp > osp) {
		count = 1;
		auto retData = script_getdatatop(st, -1);
		script_data data;
		memcpy(&data, retData, sizeof(script_data));
		auto retDataActual = get_val(st, retData);
		if (data_isstring(retDataActual)) {
			lua_pushstring(L, retData->u.str);
			//ShowDebug("ret val %s\n", retData->u.str);
		}
		else if (data_isint(retDataActual)) {
			lua_pushinteger(L, retData->u.num);
			//ShowDebug("ret val %" PRId64 "\n", retData->u.num);
		}
		else {
			lua_pushnil(L);
		}
	}
	if (count == 0) {
		count = 1;
		lua_pushnil(L);
	}
	//if (data_isreference(&data)) {
	//	lua_newtable(L);
	//	lua_pushlightuserdata(L, (void*)(int)data.type);
	//	lua_setfield(L, -2, "type");
	//	lua_pushlightuserdata(L, (void*)(data.u.num & 0xffffffff));
	//	lua_setfield(L, -2, "num1");
	//	lua_pushlightuserdata(L, (void*)((data.u.num >> 32) & 0xffffffff));
	//	lua_setfield(L, -2, "num2");
	//	lua_pushlightuserdata(L, data.ref);
	//	lua_setfield(L, -2, "ref");
	//	count++;
	//}



	if (ret != SCRIPT_CMD_SUCCESS) {
		lua_pop(L, count);
		free_stack(st, funcname0, start0, map_vars, map_arrays);
		return luaL_error(L, "callScript error: buildin_func %s exec failed", funcname);
	}

	for (int i = 3; i <= argN; i++) {
		if (lua_istable(L, i)) {
			if (lua_objlen(L, i) > 0) {
				auto d = script_getdata(st, i - 1);
				auto refName = reference_getname(d);
				//				ShowDebug("out %d:%d %d %s %s\n", i, argN, (int32_t)d->u.num, refName, str_buf + str_data[d->u.num].str);
				auto is_str = is_string_variable(refName);
				auto xLen = script_array_highest_key(st, nullptr, refName, d->ref);
				if (xLen > 1) {
					auto dId = reference_getid(d);
					for (int j = 0; j < xLen; ++j) {
						auto r = reference_uid(dId, j);
						if (is_str) {
							lua_pushstring(L, get_val2_str(st, r, d->ref));
						}
						else {
							lua_pushnumber(L, get_val2_num(st, r, d->ref));
						}
						//idb_remove(map_vars, r);
						lua_rawseti(L, i, j);
					}
				}
				else {
					if (is_str) {
						lua_pushstring(L, conv_str(st, d));
					}
					else {
						lua_pushnumber(L, conv_num64(st, d));
					}
					lua_rawseti(L, i, 1);
				}
			}
		}
	}
	free_stack(st, funcname0, start0, map_vars, map_arrays);

#undef freeStack
	return count;
}

RESUME_FUNC(sleep) {
	st->state = RUN;
	st->sleep.tick = 0;
	ret = lua_resume(L, 0);
	return SCRIPT_CMD_SUCCESS;
}

LUA_FUNC(sleep) {
	if (!lua_isuserdata(L, 1)) {
		return luaL_error(L, "sleep error: 1");
	}
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
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
	st->lua_state.lastCmd = elc_sleep;
	st->lua_state.fn = resume_sleep;
	return lua_yield(L, 0);
}

LUA_FUNC(sleep2) {
	if (!lua_isuserdata(L, 1)) {
		return luaL_error(L, "sleep error: 1");
	}
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	//if (st->sleep.tick == 0) {
	int ticks;

	ticks = luaL_optinteger(L, 2, 0);

	if (ticks <= 0) {
		ShowError("buildin_sleep: negative or zero amount('%d') of milli seconds is not supported\n", ticks);
		lua_pushboolean(L, false);
		return 1;
	}

	// sleep for the target amount of time
	st->state = RERUNLINE;
	st->sleep.tick = ticks;
	st->lua_state.lastCmd = elc_sleep2;
	st->lua_state.fn = resume_sleep;
	return lua_yield(L, 0);
}

LUA_FUNC(mes) {
	if (!luaL_checkUserData<script_state*>(L, 1)) {
		return luaL_error(L, "sleep error: 1");
	}
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	map_session_data* sd = map_id2sd(st->rid);
	if (!sd)
		return luaL_error(L, "player not attached.");
	auto n = lua_gettop(L);
	for (int i = 2; i <= n; ++i) {
		clif_scriptmes(*sd, st->oid, lua_tostring(L, i));
	}
	st->mes_active = 1; // Invoking character has a NPC dialog box open.
	return 0;
}

LUA_FUNC(next) {
	if (!luaL_checkUserData<script_state*>(L, 1)) {
		return luaL_error(L, "sleep error: 1");
	}
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	map_session_data* sd = map_id2sd(st->rid);

	if (!st->mes_active) {
		ShowWarning("buildin_next: There is no mes active.\n");
		return luaL_error(L, "buildin_next: There is no mes active.\n");
	}

	if (!sd)
		return 0;
#ifdef SECURE_NPCTIMEOUT
	sd->npc_idle_type = NPCT_WAIT;
#endif
	st->state = STOP;
	clif_scriptnext(*sd, st->oid);
	st->lua_state.lastCmd = elc_next;
	st->lua_state.fn = resume_sleep;
	return lua_yield(L, 0);
}

LUA_FUNC(clear) {
	if (!luaL_checkUserData<script_state*>(L, 1)) {
		return luaL_error(L, "sleep error: 1");
	}
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	map_session_data* sd = map_id2sd(st->rid);

	if (!st->mes_active) {
		ShowWarning("buildin_clear: There is no mes active.\n");
		return luaL_error(L, "buildin_clear: There is no mes active.\n");
	}

	if (!sd)
		return 0;

	clif_scriptclear(*sd, st->oid);
	return 0;
}

LUA_FUNC(close) {
	if (!luaL_checkUserData<script_state*>(L, 1)) {
		return luaL_error(L, "close error: 1");
	}
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	map_session_data* sd = map_id2sd(st->rid);

	if (!sd)
		return 0;

	npc_data* nd = map_id2nd(st->oid);

	if (nd != nullptr && nd->dynamicnpc.owner_char_id != 0) {
		nd->dynamicnpc.last_interaction = gettick();
	}

	const char* command = "close";

	if (!st->mes_active) {
		st->state = END; // Keep backwards compatibility.
		ShowWarning("buildin_close: Incorrect use of '%s' command!\n", command);
	}
	else {
		st->state = CLOSE;
		st->mes_active = 0;
	}

	if (!strcmp(command, "close3")) {
		st->clear_cutin = true;
	}

	clif_scriptclose(sd, st->oid);
	return 0;
}

LUA_FUNC(close2) {
	if (!luaL_checkUserData<script_state*>(L, 1)) {
		return luaL_error(L, "close2 error: 1");
	}
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	map_session_data* sd = map_id2sd(st->rid);

	if (!sd)
		return 0;

	st->state = STOP;

	if (st->mes_active)
		st->mes_active = 0;

	clif_scriptclose(sd, st->oid);
	return 0;
}

LUA_FUNC(close3) {
	if (!luaL_checkUserData<script_state*>(L, 1)) {
		return luaL_error(L, "close3 error: 1");
	}
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	map_session_data* sd = map_id2sd(st->rid);

	if (!sd)
		return 0;

	npc_data* nd = map_id2nd(st->oid);

	if (nd != nullptr && nd->dynamicnpc.owner_char_id != 0) {
		nd->dynamicnpc.last_interaction = gettick();
	}

	const char* command = "close";

	if (!st->mes_active) {
		st->state = END; // Keep backwards compatibility.
		ShowWarning("buildin_close: Incorrect use of '%s' command!\n", command);
	}
	else {
		st->state = CLOSE;
		st->mes_active = 0;
	}

	if (!strcmp(command, "close3")) {
		st->clear_cutin = true;
	}

	clif_scriptclose(sd, st->oid);
	return 0;
}

LUA_FUNC(readparam) {
	if (!luaL_checkUserData<script_state*>(L, 1)) {
		return luaL_error(L, "readparam error: 1");
	}
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	map_session_data* sd = nullptr;
	int arg3Type = lua_type(L, 3);
	if (lua_gettop(L) >= 3) {
		if (arg3Type == LUA_TSTRING)
			sd = map_nick2sd(lua_tostring(L, 3), false);
		else if (arg3Type == LUA_TNUMBER)
			sd = map_id2sd(lua_tointeger(L, 4));
		else
			sd = map_id2sd(st->rid);
	}
	else {
		sd = map_id2sd(st->rid);
	}

	if (!sd)
		return luaL_error(L, "buildin_readparam player not found. %p %d %d", sd, st->rid, arg3Type);

	auto val = pc_readparam(sd, lua_tointeger(L, 2));
	if (val < INT32_MIN || val > INT32_MAX) {
		luaL_newUserData(L, val);
	}
	else {
		lua_pushinteger(L, val);
	}
	return 1;
}

LUA_FUNC(setparam) {
	LUA_CHECK_ST(setparam);
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	map_session_data* sd = nullptr;
	int arg4Type = lua_type(L, 4);
	if (lua_gettop(L) >= 4) {
		if (arg4Type == LUA_TSTRING)
			sd = map_nick2sd(lua_tostring(L, 4), false);
		else if(arg4Type == LUA_TNUMBER)
			sd = map_id2sd(lua_tointeger(L, 4));
		else
			sd = map_id2sd(st->rid);
	}
	else {
		sd = map_id2sd(st->rid);
	}

	if (!sd)
		return luaL_error(L, "buildin_setparam player not found. %p %d %d", sd, st->rid, arg4Type);
	int64_t val = lua_tointeger(L, 3);
	auto ret = pc_setparam(sd, lua_tointeger(L, 2), val);
	lua_pushboolean(L, ret);
	return 1;
}

LUA_FUNC(checkcell) {
	LUA_CHECK_ST(checkcell);
	//auto st = luaL_toUserData<script_state*>(L, 1)->st;

	int16 m = map_mapname2mapid(lua_tostring(L, 2));
	int16 x = lua_tointeger(L, 3);
	int16 y = lua_tointeger(L, 4);
	cell_chk type = (cell_chk)lua_tointeger(L, 5);

	lua_pushinteger(L, map_getcell(m, x, y, type));

	return 1;
}

int menu_countoptions(const char* str, int max_count, int* total);

RESUME_FUNC(select) {
	map_session_data* sd = map_id2sd(st->rid);
	ret = LUA_OK;
	if (!sd)
		return SCRIPT_CMD_SUCCESS;
	if (sd->npc_menu == 0xff) {// Cancel was pressed
		sd->state.menu_or_input = 0;
		st->state = END;
		lua_pushinteger(L, -1);
		ret = lua_resume(L, 1);
		return SCRIPT_CMD_SUCCESS;
	}
	else {// return selected option
		int menu = sd->npc_menu;
		sd->npc_menu = 0;
		sd->state.menu_or_input = 0;
		lua_pushinteger(L, menu);
		ret = lua_resume(L, 1);
		return SCRIPT_CMD_SUCCESS;
	}
}

LUA_FUNC(select) {
	if (!luaL_checkUserData<script_state*>(L, 1)) {
		return luaL_error(L, "close3 error: 1");
	}
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	map_session_data* sd = map_id2sd(st->rid);

	int i;
	const char* text;

	if (!sd)
		return 0;

#ifdef SECURE_NPCTIMEOUT
	sd->npc_idle_type = NPCT_MENU;
#endif

	if (sd->state.menu_or_input == 0) {
		struct StringBuf buf;

		StringBuf_Init(&buf);
		sd->npc_menu = 0;
#ifdef Pandas_Fix_Prompt_Cancel_Combine_Close_Error
		sd->npc_menu_npcid = 0;
#endif // Pandas_Fix_Prompt_Cancel_Combine_Close_Error
		for (i = 2; i <= lua_gettop(L); ++i) {
			text = lua_tostring(L, i);

			if (sd->npc_menu > 0)
				StringBuf_AppendStr(&buf, ":");

			StringBuf_AppendStr(&buf, text);
			sd->npc_menu += menu_countoptions(text, 0, NULL);
		}

		st->state = RERUNLINE;
		sd->state.menu_or_input = 1;

		/**
		 * menus beyond this length crash the client (see bugreport:6402)
		 **/
		if (StringBuf_Length(&buf) >= 2047) {
			struct npc_data* nd = map_id2nd(st->oid);
			char* menu;
			CREATE(menu, char, 2048);
			safestrncpy(menu, StringBuf_Value(&buf), 2047);
			ShowWarning("buildin_select: NPC Menu too long! (source:%s / length:%d)\n", nd ? nd->name : "Unknown", StringBuf_Length(&buf));
			clif_scriptmenu(sd, st->oid, menu);
			aFree(menu);
		}
		else
			clif_scriptmenu(sd, st->oid, StringBuf_Value(&buf));
		StringBuf_Destroy(&buf);

		if (sd->npc_menu >= 0xff) {
			ShowWarning("buildin_select: Too many options specified (current=%d, max=254).\n", sd->npc_menu);
		}
		st->lua_state.lastCmd = elc_select;
		st->lua_state.fn = resume_select;
		return lua_yield(L, 0);
	}
	return 0;
}

RESUME_FUNC(input) {
	map_session_data* sd = map_id2sd(st->rid);
	ret = LUA_OK;
	if (!sd)
		return SCRIPT_CMD_SUCCESS;
	int type, min, max;
	sd->state.menu_or_input = 0;
	if (st->lua_state.refVar > 0) {
		luaL_ref_get(L, st->lua_state.refVar);
		luaL_unref_ngc(L, st->lua_state.refVar);
		st->lua_state.refVar = 0;
	}
	type = luaL_getTableInt(L, -1, 1);
	min = luaL_getTableInt(L, -1, 2);
	max = luaL_getTableInt(L, -1, 3);
	lua_pop(L, 1);
	if (type == 1)
	{
		int len = (int)strlen(sd->npc_str);
		lua_pushinteger(L, (len > max ? 1 : len < min ? -1 : 0));
		lua_pushstring(L, sd->npc_str);
	}
	else
	{
		int amount = sd->npc_amount;
		lua_pushinteger(L, (amount > max ? 1 : amount < min ? -1 : 0));
		lua_pushinteger(L, cap_value(amount, min, max));
	}
	st->state = RUN;
	ret = lua_resume(L, 2);
	return SCRIPT_CMD_SUCCESS;
}

LUA_FUNC(input) {
	LUA_CHECK_ST(input);
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	map_session_data* sd = map_id2sd(st->rid);
	int min = script_config.input_min_value;
	int max = script_config.input_max_value;
	int type = lua_tointeger(L, 2);
	if (!sd)
		return 0;

#ifdef SECURE_NPCTIMEOUT
	sd->npc_idle_type = NPCT_WAIT;
#endif

	if (!sd->state.menu_or_input)
	{	// first invocation, display npc input box
		sd->state.menu_or_input = 1;
		st->state = RERUNLINE;
		if (type == 1)
			clif_scriptinputstr(sd, st->oid);
		else
			clif_scriptinput(sd, st->oid);
		lua_newtable(L);
		luaL_setTableInt(L, -1, 1, type);
		luaL_setTableInt(L, -1, 2, min);
		luaL_setTableInt(L, -1, 3, max);
		LUA_YIELD_RET(input, luaL_ref(L));
	}
	return luaL_error(L, "buildin_input error state.");
}

RESUME_FUNC(progressbar) {
	ret = lua_resume(L, 0);
	return SCRIPT_CMD_SUCCESS;
}

LUA_FUNC(progressbar) {
	LUA_CHECK_ST(progressbar);
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	map_session_data* sd = map_id2sd(st->rid);
	if (!sd)
		return 0;

	st->state = STOP;

	auto color = lua_tostring(L, 2);
	auto second = lua_tointeger(L, 3);

	sd->progressbar.npc_id = st->oid;
	sd->progressbar.timeout = gettick() + second * 1000;
	sd->state.workinprogress = WIP_DISABLE_ALL;

	clif_progressbar(sd, strtol(color, (char**)NULL, 0), second);
	LUA_YIELD_RET(progressbar, 0);
}

RESUME_FUNC(progressbar_npc) {
	map_session_data* sd = map_id2sd(st->rid);

	luaL_ref_get(L, st->lua_state.refVar);
	luaL_unref_ngc(L, st->lua_state.refVar);
	st->lua_state.refVar = 0;
	auto nid = lua_tointeger(L, -1);
	lua_pop(L, 1);
	auto nd = map_id2nd(nid);
	// Continue the script
	if (sd) { // Player attached - remove restrictions
		sd->state.workinprogress = WIP_DISABLE_NONE;
		sd->state.block_action &= ~(PCBLOCK_MOVE | PCBLOCK_ATTACK | PCBLOCK_SKILL);
	}

	st->state = RUN;
	st->sleep.tick = 0;
	nd->progressbar.timeout = nd->progressbar.color = 0;
	ret = lua_resume(L, 0);
	return SCRIPT_CMD_SUCCESS;
}

LUA_FUNC(progressbar_npc) {
	LUA_CHECK_ST(progressbar_npc);
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	map_session_data* sd = map_id2sd(st->rid);
	struct npc_data* nd = NULL;

	if (lua_gettop(L) >= 4) {
		const char* name = lua_tostring(L, 4);

		nd = npc_name2id(name);

		if (!nd) {
			return luaL_error(L, "buildin_progressbar_npc: NPC \"%s\" was not found.\n", name);
		}
	}
	else {
		nd = map_id2nd(st->oid);
	}

	auto color = lua_tostring(L, 2);
	auto second = lua_tointeger(L, 3);

	if (!nd->progressbar.timeout) {
		if (second < 0) {
			return luaL_error(L, "buildin_progressbar_npc: negative amount('%d') of seconds is not supported\n", second);
		}

		if (sd) { // Player attached - keep them from doing other things
			sd->state.workinprogress = WIP_DISABLE_ALL;
			sd->state.block_action |= (PCBLOCK_MOVE | PCBLOCK_ATTACK | PCBLOCK_SKILL);
		}

		// sleep for the target amount of time
		st->state = RERUNLINE;
		st->sleep.tick = second * 1000;
		nd->progressbar.timeout = gettick() + second * 1000;
		nd->progressbar.color = strtol(color, nullptr, 0);

		clif_progressbar_npc_area(nd);
		// Second call(by timer after sleeping time is over)
	}
	else {
		return luaL_error(L, "buildin_progressbar_npc: invalid state.\n");
	}
	lua_pushinteger(L, nd->bl.id);
	LUA_YIELD_RET(progressbar_npc, luaL_ref(L));
}

template<typename T>
LUA_FUNC(ToString) {
	if (lua_gettop(L) != 1) {
		lua_pushnil(L);
		return 1;
	}
	if (!luaL_checkUserData<T>(L, 1)) {
		lua_pushnil(L);
		return 1;
	}
	char buff[256] = { 0 };
	sprintf(buff, "%s@0x%p", UserDataMetaTableFor<T>(), lua_touserdata(L, 1));
	lua_pushstring(L, buff);
	return 1;
}

LUA_FUNC(get_ref_value) {
	int argN = lua_gettop(L);
	if (argN < 2) {
		return luaL_error(L, "callScript error: 0");
	}
	if (!luaL_checkUserData<script_state*>(L, 1)) {
		return luaL_error(L, "callScript error: 1");
	}
	if (!luaL_checkUserData<script_data>(L, 2)) {
		return luaL_error(L, "callScript error: 2");
	}
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	auto data = luaL_toUserData<script_data>(L, 2);
	auto is_str = is_string_variable(reference_getname(&data->st));
	if (is_str) {
		lua_pushstring(L, conv_str(st, &data->st));
	}
	else {
		const auto v = conv_num64(st, &data->st);
		if (v > INT32_MAX) {
			luaL_newUserData<int64_t>(L, v);
		}
		else {
			lua_pushinteger(L, static_cast<int32_t>(v));
		}
	}
	return 1;
}

LUA_FUNC(get_ref_array) {
	int argN = lua_gettop(L);
	if (argN < 2) {
		return luaL_error(L, "callScript error: 0");
	}
	if (!luaL_checkUserData<script_state*>(L, 1)) {
		return luaL_error(L, "callScript error: 1");
	}
	if (!luaL_checkUserData<script_data>(L, 2)) {
		return luaL_error(L, "callScript error: 2");
	}
	int offset = 0;
	if (argN == 3) {
		offset = lua_tointeger(L, 3);
	}
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	auto data = luaL_toUserData<script_data>(L, 2);
	auto is_str = is_string_variable(reference_getname(&data->st));
	map_session_data* sd = map_id2sd(st->rid);
	auto xLen = script_array_highest_key(st, sd, reference_getname(&data->st), data->st.ref);
	lua_newtable(L);
	auto d = &data->st;
	auto dId = reference_getid(d);
	for (int j = 0; j < xLen; ++j) {
		if (is_str) {
			lua_pushstring(L, get_val2_str(st, reference_uid(dId, j), d->ref));
		}
		else {
			const auto v = get_val2_num(st, reference_uid(dId, j), d->ref);
			if (v > INT32_MAX) {
				luaL_newUserData<int64_t>(L, v);
			}
			else {
				lua_pushinteger(L, static_cast<int32_t>(v));
			}
		}
		lua_rawseti(L, -2, j + offset);
	}

	return 1;
}

LUA_FUNC(ref) {
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "error");
	}
	LUA_CHECK_ST(ref);
	if (!lua_isstring(L, 2)) {
		return luaL_error(L, "error");
	}
	auto buffer = lua_tostring(L, 2);
	char varname[256] = "";
	int elem = 0;
	if (sscanf(buffer, "%99[^[][%11d]", varname, &elem) < 2)
		elem = 0;
	trim(varname);
	reg_db* reg = nullptr;
	auto st = luaL_toUserData<script_state*>(L, 1)->st;

	if (varname[0] == '\'') {
		auto sd = map_id2sd(st->rid);
		if (sd) {
			int instance_id = -1;
			switch (sd->instance_mode)
			{
			case IM_NONE:
				break;
			case IM_CHAR:
				instance_id = sd->instance_id;
				break;
			case IM_PARTY:
			{
				auto pd = party_search(sd->status.party_id);
				if (pd && pd->party.party_id == sd->status.party_id) {
					instance_id = pd->instance_id;
				}
				break;
			}
			case IM_GUILD:
			{
				auto gd = guild_search(sd->status.guild_id);
				if (gd && gd->guild.guild_id == sd->status.guild_id) {
					instance_id = gd->instance_id;
				}
				break;
			}
			case IM_CLAN:
			{
				auto cd = clan_search(sd->status.clan_id);
				if (cd && cd->id == sd->status.clan_id) {
					instance_id = cd->instance_id;
				}
				break;
			}
			}

			std::shared_ptr<s_instance_data> im = rathena::util::umap_find(instances, instance_id);

			if (im && im->state == INSTANCE_BUSY) {
				if (!im->regs.vars) {
					im->regs.vars = i64db_alloc(DB_OPT_RELEASE_DATA);
				}
				reg = &im->regs;
			}

		}
	}
	auto s = luaL_newUserData<script_data>(L, {
		C_NAME,
		static_cast<int64>(reference_uid(add_str(varname), elem)),
		reg
		});
	return 1;
}

//instance_ref
LUA_FUNC(instance_ref) {
	if (lua_gettop(L) != 3) {
		return luaL_error(L, "error");
	}
	if (lua_type(L, 1) != LUA_TUSERDATA) {
		return luaL_error(L, "error");
	}
	if (!lua_isstring(L, 2)) {
		return luaL_error(L, "error");
	}
	if (!lua_isnumber(L, 3)) {
		return luaL_error(L, "error");
	}
	auto buffer = lua_tostring(L, 2);
	char varname[256] = "";
	int elem = 0;
	if (sscanf(buffer, "%99[^[][%11d]", varname, &elem) < 2)
		elem = 0;

	if (*varname != '\'') {
		return luaL_error(L, "instance_ref: Invalid scope. %s is not an instance variable.\n", varname);
	}

	int instance_id = lua_tointeger(L, 3);

	if (instance_id <= 0) {
		return luaL_error(L, "instance_ref: Invalid instance ID %d.\n", instance_id);
	}

	std::shared_ptr<s_instance_data> im = rathena::util::umap_find(instances, instance_id);


	if (!im || im->state != INSTANCE_BUSY) {
		ShowError("instance_ref: Unknown instance ID %d.\n", instance_id);
		return luaL_error(L, "error");
	}

	if (!im->regs.vars) {
		im->regs.vars = i64db_alloc(DB_OPT_RELEASE_DATA);
	}

	auto s = luaL_newUserData<script_data>(L, {
		C_NAME,
		static_cast<int64>(reference_uid(add_str(varname), elem)),
		&im->regs,
		});

	return 1;
}

map_session_data* mapid2sd_(script_state* st, lua_State* L, int n) {
	if (lua_gettop(L) >= n && lua_type(L, n) == LUA_TNUMBER) {
		return map_id2sd(lua_tointeger(L, n));
	}
	if (lua_gettop(L) < n) {
		return map_id2sd(st->rid);
	}
	return nullptr;
}

LUA_FUNC(getmapxy) {
	if (!luaL_checkUserData<script_state*>(L, 1)) {
		return luaL_error(L, "callScript error: 1");
	}
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	map_session_data* sd = nullptr;
	block_list* bl = nullptr;

	auto type = BL_PC;

	if (lua_isnumber(L, 2)) {
		type = static_cast<bl_type>(lua_tointeger(L, 2));
	}

	switch (type) {
	case BL_PC:	//Get Character Position
		if ((lua_type(L, 3) == LUA_TSTRING && (sd = map_nick2sd(lua_tostring(L, 3), false))) || (sd = mapid2sd_(st, L, 3)))
			bl = &sd->bl;
		break;
	case BL_NPC:	//Get NPC Position
		if (lua_gettop(L) >= 3) {
			struct npc_data* nd;

			if (lua_type(L, 3) == LUA_TSTRING)
				nd = npc_name2id(lua_tostring(L, 3));
			else
				nd = map_id2nd(lua_tointeger(L, 3));
			if (nd)
				bl = &nd->bl;
		}
		else //In case the origin is not an NPC?
			bl = map_id2bl(st->oid);
		break;
	case BL_PET:	//Get Pet Position
		if (((lua_type(L, 3) == LUA_TSTRING && (sd = map_nick2sd(lua_tostring(L, 3), false))) || (sd = mapid2sd_(st, L, 3))) && sd->pd)
			bl = &sd->pd->bl;
		break;
	case BL_HOM:	//Get Homun Position
		if (((lua_type(L, 3) == LUA_TSTRING && (sd = map_nick2sd(lua_tostring(L, 3), false))) || (sd = mapid2sd_(st, L, 3))) && sd->hd)
			bl = &sd->hd->bl;
		break;
	case BL_MER: //Get Mercenary Position
		if (((lua_type(L, 3) == LUA_TSTRING && (sd = map_nick2sd(lua_tostring(L, 3), false))) || (sd = mapid2sd_(st, L, 3))) && sd->md)
			bl = &sd->md->bl;
		break;
	case BL_ELEM: //Get Elemental Position
		if (((lua_type(L, 3) == LUA_TSTRING && (sd = map_nick2sd(lua_tostring(L, 3), false))) || (sd = mapid2sd_(st, L, 3))) && sd->ed)
			bl = &sd->ed->bl;
		break;
	default:
		ShowWarning("script: buildin_getmapxy: Invalid type %d.\n", type);
		return 0;
	}
	if (!bl) {
		return 0;
	}

	lua_pushstring(L, map_getmapdata(bl->m)->name);
	lua_pushinteger(L, bl->x);
	lua_pushinteger(L, bl->y);
	return 3;
}

LUA_FUNC(getpartymember) {
	LUA_CHECK_ST(getpartymember);
	auto st = luaL_toUserData<script_state*>(L, 1)->st;
	struct party_data* p;
	uint8 j = 0;

	int partyId = 0;
	int n = lua_gettop(L);
	if (n >= 2) {
		partyId = lua_tonumber(L, 2);
	}
	else {
		map_session_data* sd = map_id2sd(st->rid);
		if (sd && sd->status.party_id) {
			partyId = sd->status.party_id;
		}
	}

	p = party_search(partyId);
	lua_newtable(L);
	if (p != NULL) {
		uint8 i, type = 0;
		struct script_data* data = NULL;
		char* varname = NULL;

		if (n >= 3)
			type = lua_tointeger(L, 3);

		for (i = 0; i < MAX_PARTY; i++) {
			if (p->party.member[i].account_id) {
				switch (type) {
				case 2:
					lua_pushinteger(L, p->party.member[i].account_id);
					break;
				case 1:
					lua_pushinteger(L, p->party.member[i].char_id);
					break;
				default:
					lua_pushstring(L, p->party.member[i].name);
					break;
				}

				j++;
				lua_rawseti(L, -2, j);
			}
		}
	}

	return 1;
}


LUA_FUNC(getunitdata) {
	LUA_CHECK_ST(getunitdata);
	auto st = luaL_toUserData<script_state*>(L, 1)->st;

	TBL_PC* sd = st->rid ? map_id2sd(st->rid) : NULL;
	struct block_list* bl;
	TBL_MOB* md = NULL;
	TBL_HOM* hd = NULL;
	TBL_MER* mc = NULL;
	TBL_PET* pd = NULL;
	TBL_ELEM* ed = NULL;
	TBL_NPC* nd = NULL;
	char* name;
	
	if (!(bl = map_id2bl(lua_tointeger(L, 2)))) {
		return luaL_error(L, "buildin_getunitdata: Error in finding object %d!\n", lua_tointeger(L, 2));
	}

	switch (bl->type) {
	case BL_MOB:  md = map_id2md(bl->id); break;
	case BL_HOM:  hd = map_id2hd(bl->id); break;
	case BL_PET:  pd = map_id2pd(bl->id); break;
	case BL_MER:  mc = map_id2mc(bl->id); break;
	case BL_ELEM: ed = map_id2ed(bl->id); break;
	case BL_NPC:  nd = map_id2nd(bl->id); break;
	default:
		return luaL_error(L, "buildin_getunitdata: Invalid object type!\n");
	}
	lua_newtable(L);
#define getunitdata_sub(n, v) {if ((int64_t)(v) >= INT32_MIN && (int64_t)(v) <= INT32_MAX) lua_pushinteger(L, (int)(v)); else luaL_newUserData(L, (int64_t)(v)); lua_rawseti(L, -2, (n)); }

	switch (bl->type) {
	case BL_MOB:
		if (!md) {
			return luaL_error(L, "buildin_getunitdata: Error in finding object BL_MOB!\n");
		}
		getunitdata_sub(UMOB_SIZE, md->status.size);
		getunitdata_sub(UMOB_LEVEL, md->level);
		getunitdata_sub(UMOB_HP, md->status.hp);
		getunitdata_sub(UMOB_MAXHP, md->status.max_hp);
		getunitdata_sub(UMOB_MASTERAID, md->master_id);
		getunitdata_sub(UMOB_MAPID, md->bl.m);
		getunitdata_sub(UMOB_X, md->bl.x);
		getunitdata_sub(UMOB_Y, md->bl.y);
		getunitdata_sub(UMOB_SPEED, md->status.speed);
		getunitdata_sub(UMOB_MODE, md->status.mode);
		getunitdata_sub(UMOB_AI, md->special_state.ai);
		getunitdata_sub(UMOB_SCOPTION, md->sc.option);
		getunitdata_sub(UMOB_SEX, md->vd->sex);
		getunitdata_sub(UMOB_CLASS, md->vd->class_);
		getunitdata_sub(UMOB_HAIRSTYLE, md->vd->hair_style);
		getunitdata_sub(UMOB_HAIRCOLOR, md->vd->hair_color);
		getunitdata_sub(UMOB_HEADBOTTOM, md->vd->head_bottom);
		getunitdata_sub(UMOB_HEADMIDDLE, md->vd->head_mid);
		getunitdata_sub(UMOB_HEADTOP, md->vd->head_top);
		getunitdata_sub(UMOB_CLOTHCOLOR, md->vd->cloth_color);
		getunitdata_sub(UMOB_SHIELD, md->vd->shield);
		getunitdata_sub(UMOB_WEAPON, md->vd->weapon);
		getunitdata_sub(UMOB_LOOKDIR, md->ud.dir);
		getunitdata_sub(UMOB_CANMOVETICK, md->ud.canmove_tick);
		getunitdata_sub(UMOB_STR, md->status.str);
		getunitdata_sub(UMOB_AGI, md->status.agi);
		getunitdata_sub(UMOB_VIT, md->status.vit);
		getunitdata_sub(UMOB_INT, md->status.int_);
		getunitdata_sub(UMOB_DEX, md->status.dex);
		getunitdata_sub(UMOB_LUK, md->status.luk);
		//pow, sta, wis, spl, con, crt,
		getunitdata_sub(UMOB_POW, md->status.pow);
		getunitdata_sub(UMOB_STA, md->status.sta);
		getunitdata_sub(UMOB_WIS, md->status.wis);
		getunitdata_sub(UMOB_SPL, md->status.spl);
		getunitdata_sub(UMOB_CON, md->status.con);
		getunitdata_sub(UMOB_CRT, md->status.crt);
		getunitdata_sub(UMOB_CHASERANGE, md->min_chase);
		getunitdata_sub(UMOB_SLAVECPYMSTRMD, md->state.copy_master_mode);
		getunitdata_sub(UMOB_DMGIMMUNE, md->ud.immune_attack);
		getunitdata_sub(UMOB_ATKRANGE, md->status.rhw.range);
		getunitdata_sub(UMOB_ATKMIN, md->status.rhw.atk);
		getunitdata_sub(UMOB_ATKMAX, md->status.rhw.atk2);
		getunitdata_sub(UMOB_MATKMIN, md->status.matk_min);
		getunitdata_sub(UMOB_MATKMAX, md->status.matk_max);
		getunitdata_sub(UMOB_DEF, md->status.def);
		getunitdata_sub(UMOB_MDEF, md->status.mdef);
		getunitdata_sub(UMOB_HIT, md->status.hit);
		getunitdata_sub(UMOB_FLEE, md->status.flee);
		getunitdata_sub(UMOB_PDODGE, md->status.flee2);
		getunitdata_sub(UMOB_CRIT, md->status.cri);
		getunitdata_sub(UMOB_RACE, md->status.race);
		getunitdata_sub(UMOB_ELETYPE, md->status.def_ele);
		getunitdata_sub(UMOB_ELELEVEL, md->status.ele_lv);
		getunitdata_sub(UMOB_AMOTION, md->status.amotion);
		getunitdata_sub(UMOB_ADELAY, md->status.adelay);
		getunitdata_sub(UMOB_DMOTION, md->status.dmotion);
		getunitdata_sub(UMOB_TARGETID, md->target_id);
		getunitdata_sub(UMOB_ROBE, md->vd->robe);
		getunitdata_sub(UMOB_BODY2, md->vd->body_style);
		getunitdata_sub(UMOB_GROUP_ID, md->ud.group_id);
		getunitdata_sub(UMOB_IGNORE_CELL_STACK_LIMIT, md->ud.state.ignore_cell_stack_limit);
		getunitdata_sub(UMOB_RES, md->status.res);
		getunitdata_sub(UMOB_MRES, md->status.mres);
		getunitdata_sub(UMOB_DAMAGETAKEN, md->damagetaken);
#ifdef Pandas_Struct_Unit_CommonData_Aura
		getunitdata_sub(UMOB_AURA, md->ucd.aura.id);
#endif // Pandas_Struct_Unit_CommonData_Aura
#ifdef Pandas_ScriptParams_DamageTaken_From_Database
		getunitdata_sub(UMOB_DMGRATE, md->pandas.dmg_rate);
		getunitdata_sub(UMOB_DMGRATE2, md->pandas.dmg_rate2);
		getunitdata_sub(UMOB_DAMAGETAKEN_DB, md->db->damagetaken);
#endif // Pandas_ScriptParams_DamageTaken_From_Database
#ifdef Pandas_ScriptParams_UnitData_Experience
		getunitdata_sub(UMOB_MOBBASEEXP, md->pandas.base_exp);
		getunitdata_sub(UMOB_MOBBASEEXP_DB, md->db->base_exp);
		getunitdata_sub(UMOB_MOBJOBEXP, md->pandas.job_exp);
		getunitdata_sub(UMOB_MOBJOBEXP_DB, md->db->job_exp);
#endif // Pandas_ScriptParams_UnitData_Experience
		break;

	case BL_HOM:
		if (!hd) {
			return luaL_error(L, "buildin_getunitdata: Error in finding object BL_HOM!\n");
		}
		getunitdata_sub(UHOM_SIZE, hd->base_status.size);
		getunitdata_sub(UHOM_LEVEL, hd->homunculus.level);
		getunitdata_sub(UHOM_HP, hd->homunculus.hp);
		getunitdata_sub(UHOM_MAXHP, hd->homunculus.max_hp);
		getunitdata_sub(UHOM_SP, hd->homunculus.sp);
		getunitdata_sub(UHOM_MAXSP, hd->homunculus.max_sp);
		getunitdata_sub(UHOM_MASTERCID, hd->homunculus.char_id);
		getunitdata_sub(UHOM_MAPID, hd->bl.m);
		getunitdata_sub(UHOM_X, hd->bl.x);
		getunitdata_sub(UHOM_Y, hd->bl.y);
		getunitdata_sub(UHOM_HUNGER, hd->homunculus.hunger);
		getunitdata_sub(UHOM_INTIMACY, hd->homunculus.intimacy);
		getunitdata_sub(UHOM_SPEED, hd->base_status.speed);
		getunitdata_sub(UHOM_LOOKDIR, hd->ud.dir);
		getunitdata_sub(UHOM_CANMOVETICK, hd->ud.canmove_tick);
		getunitdata_sub(UHOM_STR, hd->base_status.str);
		getunitdata_sub(UHOM_AGI, hd->base_status.agi);
		getunitdata_sub(UHOM_VIT, hd->base_status.vit);
		getunitdata_sub(UHOM_INT, hd->base_status.int_);
		getunitdata_sub(UHOM_DEX, hd->base_status.dex);
		getunitdata_sub(UHOM_LUK, hd->base_status.luk);
		getunitdata_sub(UHOM_DMGIMMUNE, hd->ud.immune_attack);
		getunitdata_sub(UHOM_ATKRANGE, hd->battle_status.rhw.range);
		getunitdata_sub(UHOM_ATKMIN, hd->base_status.rhw.atk);
		getunitdata_sub(UHOM_ATKMAX, hd->base_status.rhw.atk2);
		getunitdata_sub(UHOM_MATKMIN, hd->base_status.matk_min);
		getunitdata_sub(UHOM_MATKMAX, hd->base_status.matk_max);
		getunitdata_sub(UHOM_DEF, hd->battle_status.def);
		getunitdata_sub(UHOM_MDEF, hd->battle_status.mdef);
		getunitdata_sub(UHOM_HIT, hd->battle_status.hit);
		getunitdata_sub(UHOM_FLEE, hd->battle_status.flee);
		getunitdata_sub(UHOM_PDODGE, hd->battle_status.flee2);
		getunitdata_sub(UHOM_CRIT, hd->battle_status.cri);
		getunitdata_sub(UHOM_RACE, hd->battle_status.race);
		getunitdata_sub(UHOM_ELETYPE, hd->battle_status.def_ele);
		getunitdata_sub(UHOM_ELELEVEL, hd->battle_status.ele_lv);
		getunitdata_sub(UHOM_AMOTION, hd->battle_status.amotion);
		getunitdata_sub(UHOM_ADELAY, hd->battle_status.adelay);
		getunitdata_sub(UHOM_DMOTION, hd->battle_status.dmotion);
		getunitdata_sub(UHOM_TARGETID, hd->ud.target);
		getunitdata_sub(UHOM_GROUP_ID, hd->ud.group_id);
#ifdef Pandas_Struct_Unit_CommonData_Aura
		getunitdata_sub(UHOM_AURA, hd->ucd.aura.id);
#endif // Pandas_Struct_Unit_CommonData_Aura
		break;

	case BL_PET:
		if (!pd) {
			return luaL_error(L, "buildin_getunitdata: Error in finding object BL_PET!\n");
		}
		getunitdata_sub(UPET_SIZE, pd->status.size);
		getunitdata_sub(UPET_LEVEL, pd->pet.level);
		getunitdata_sub(UPET_HP, pd->status.hp);
		getunitdata_sub(UPET_MAXHP, pd->status.max_hp);
		getunitdata_sub(UPET_MASTERAID, pd->pet.account_id);
		getunitdata_sub(UPET_MAPID, pd->bl.m);
		getunitdata_sub(UPET_X, pd->bl.x);
		getunitdata_sub(UPET_Y, pd->bl.y);
		getunitdata_sub(UPET_HUNGER, pd->pet.hungry);
		getunitdata_sub(UPET_INTIMACY, pd->pet.intimate);
		getunitdata_sub(UPET_SPEED, pd->status.speed);
		getunitdata_sub(UPET_LOOKDIR, pd->ud.dir);
		getunitdata_sub(UPET_CANMOVETICK, pd->ud.canmove_tick);
		getunitdata_sub(UPET_STR, pd->status.str);
		getunitdata_sub(UPET_AGI, pd->status.agi);
		getunitdata_sub(UPET_VIT, pd->status.vit);
		getunitdata_sub(UPET_INT, pd->status.int_);
		getunitdata_sub(UPET_DEX, pd->status.dex);
		getunitdata_sub(UPET_LUK, pd->status.luk);
		getunitdata_sub(UPET_DMGIMMUNE, pd->ud.immune_attack);
		getunitdata_sub(UPET_ATKRANGE, pd->status.rhw.range);
		getunitdata_sub(UPET_ATKMIN, pd->status.rhw.atk);
		getunitdata_sub(UPET_ATKMAX, pd->status.rhw.atk2);
		getunitdata_sub(UPET_MATKMIN, pd->status.matk_min);
		getunitdata_sub(UPET_MATKMAX, pd->status.matk_max);
		getunitdata_sub(UPET_DEF, pd->status.def);
		getunitdata_sub(UPET_MDEF, pd->status.mdef);
		getunitdata_sub(UPET_HIT, pd->status.hit);
		getunitdata_sub(UPET_FLEE, pd->status.flee);
		getunitdata_sub(UPET_PDODGE, pd->status.flee2);
		getunitdata_sub(UPET_CRIT, pd->status.cri);
		getunitdata_sub(UPET_RACE, pd->status.race);
		getunitdata_sub(UPET_ELETYPE, pd->status.def_ele);
		getunitdata_sub(UPET_ELELEVEL, pd->status.ele_lv);
		getunitdata_sub(UPET_AMOTION, pd->status.amotion);
		getunitdata_sub(UPET_ADELAY, pd->status.adelay);
		getunitdata_sub(UPET_DMOTION, pd->status.dmotion);
		getunitdata_sub(UPET_GROUP_ID, pd->ud.group_id);
#ifdef Pandas_Struct_Unit_CommonData_Aura
		getunitdata_sub(UPET_AURA, pd->ucd.aura.id);
#endif // Pandas_Struct_Unit_CommonData_Aura
		break;

	case BL_MER:
		if (!mc) {
			return luaL_error(L, "buildin_getunitdata: Error in finding object BL_MER!\n");
		}
		getunitdata_sub(UMER_SIZE, mc->base_status.size);
		getunitdata_sub(UMER_HP, mc->base_status.hp);
		getunitdata_sub(UMER_MAXHP, mc->base_status.max_hp);
		getunitdata_sub(UMER_MASTERCID, mc->mercenary.char_id);
		getunitdata_sub(UMER_MAPID, mc->bl.m);
		getunitdata_sub(UMER_X, mc->bl.x);
		getunitdata_sub(UMER_Y, mc->bl.y);
		getunitdata_sub(UMER_KILLCOUNT, mc->mercenary.kill_count);
		getunitdata_sub(UMER_LIFETIME, mc->mercenary.life_time);
		getunitdata_sub(UMER_SPEED, mc->base_status.speed);
		getunitdata_sub(UMER_LOOKDIR, mc->ud.dir);
		getunitdata_sub(UMER_CANMOVETICK, mc->ud.canmove_tick);
		getunitdata_sub(UMER_STR, mc->base_status.str);
		getunitdata_sub(UMER_AGI, mc->base_status.agi);
		getunitdata_sub(UMER_VIT, mc->base_status.vit);
		getunitdata_sub(UMER_INT, mc->base_status.int_);
		getunitdata_sub(UMER_DEX, mc->base_status.dex);
		getunitdata_sub(UMER_LUK, mc->base_status.luk);
		getunitdata_sub(UMER_DMGIMMUNE, mc->ud.immune_attack);
		getunitdata_sub(UMER_ATKRANGE, mc->base_status.rhw.range);
		getunitdata_sub(UMER_ATKMIN, mc->base_status.rhw.atk);
		getunitdata_sub(UMER_ATKMAX, mc->base_status.rhw.atk2);
		getunitdata_sub(UMER_MATKMIN, mc->base_status.matk_min);
		getunitdata_sub(UMER_MATKMAX, mc->base_status.matk_max);
		getunitdata_sub(UMER_DEF, mc->base_status.def);
		getunitdata_sub(UMER_MDEF, mc->base_status.mdef);
		getunitdata_sub(UMER_HIT, mc->base_status.hit);
		getunitdata_sub(UMER_FLEE, mc->base_status.flee);
		getunitdata_sub(UMER_PDODGE, mc->base_status.flee2);
		getunitdata_sub(UMER_CRIT, mc->base_status.cri);
		getunitdata_sub(UMER_RACE, mc->base_status.race);
		getunitdata_sub(UMER_ELETYPE, mc->base_status.def_ele);
		getunitdata_sub(UMER_ELELEVEL, mc->base_status.ele_lv);
		getunitdata_sub(UMER_AMOTION, mc->base_status.amotion);
		getunitdata_sub(UMER_ADELAY, mc->base_status.adelay);
		getunitdata_sub(UMER_DMOTION, mc->base_status.dmotion);
		getunitdata_sub(UMER_TARGETID, mc->ud.target);
		getunitdata_sub(UMER_GROUP_ID, mc->ud.group_id);
#ifdef Pandas_Struct_Unit_CommonData_Aura
		getunitdata_sub(UMER_AURA, mc->ucd.aura.id);
#endif // Pandas_Struct_Unit_CommonData_Aura
		break;

	case BL_ELEM:
		if (!ed) {
			return luaL_error(L, "buildin_getunitdata: Error in finding object BL_ELEM!\n");
		}
		getunitdata_sub(UELE_SIZE, ed->base_status.size);
		getunitdata_sub(UELE_HP, ed->elemental.hp);
		getunitdata_sub(UELE_MAXHP, ed->elemental.max_hp);
		getunitdata_sub(UELE_SP, ed->elemental.sp);
		getunitdata_sub(UELE_MAXSP, ed->elemental.max_sp);
		getunitdata_sub(UELE_MASTERCID, ed->elemental.char_id);
		getunitdata_sub(UELE_MAPID, ed->bl.m);
		getunitdata_sub(UELE_X, ed->bl.x);
		getunitdata_sub(UELE_Y, ed->bl.y);
		getunitdata_sub(UELE_LIFETIME, ed->elemental.life_time);
		getunitdata_sub(UELE_MODE, ed->elemental.mode);
		getunitdata_sub(UELE_SP, ed->base_status.speed);
		getunitdata_sub(UELE_LOOKDIR, ed->ud.dir);
		getunitdata_sub(UELE_CANMOVETICK, ed->ud.canmove_tick);
		getunitdata_sub(UELE_STR, ed->base_status.str);
		getunitdata_sub(UELE_AGI, ed->base_status.agi);
		getunitdata_sub(UELE_VIT, ed->base_status.vit);
		getunitdata_sub(UELE_INT, ed->base_status.int_);
		getunitdata_sub(UELE_DEX, ed->base_status.dex);
		getunitdata_sub(UELE_LUK, ed->base_status.luk);
		getunitdata_sub(UELE_DMGIMMUNE, ed->ud.immune_attack);
		getunitdata_sub(UELE_ATKRANGE, ed->base_status.rhw.range);
		getunitdata_sub(UELE_ATKMIN, ed->base_status.rhw.atk);
		getunitdata_sub(UELE_ATKMAX, ed->base_status.rhw.atk2);
		getunitdata_sub(UELE_MATKMIN, ed->base_status.matk_min);
		getunitdata_sub(UELE_MATKMAX, ed->base_status.matk_max);
		getunitdata_sub(UELE_DEF, ed->base_status.def);
		getunitdata_sub(UELE_MDEF, ed->base_status.mdef);
		getunitdata_sub(UELE_HIT, ed->base_status.hit);
		getunitdata_sub(UELE_FLEE, ed->base_status.flee);
		getunitdata_sub(UELE_PDODGE, ed->base_status.flee2);
		getunitdata_sub(UELE_CRIT, ed->base_status.cri);
		getunitdata_sub(UELE_RACE, ed->base_status.race);
		getunitdata_sub(UELE_ELETYPE, ed->base_status.def_ele);
		getunitdata_sub(UELE_ELELEVEL, ed->base_status.ele_lv);
		getunitdata_sub(UELE_AMOTION, ed->base_status.amotion);
		getunitdata_sub(UELE_ADELAY, ed->base_status.adelay);
		getunitdata_sub(UELE_DMOTION, ed->base_status.dmotion);
		getunitdata_sub(UELE_TARGETID, ed->ud.target);
		getunitdata_sub(UELE_GROUP_ID, ed->ud.group_id);
#ifdef Pandas_Struct_Unit_CommonData_Aura
		getunitdata_sub(UELE_AURA, ed->ucd.aura.id);
#endif // Pandas_Struct_Unit_CommonData_Aura
		break;

	case BL_NPC:
		if (!nd) {
			return luaL_error(L, "buildin_getunitdata: Error in finding object BL_NPC!\n");
		}
		getunitdata_sub(UNPC_LEVEL, nd->level);
		getunitdata_sub(UNPC_HP, nd->status.hp);
		getunitdata_sub(UNPC_MAXHP, nd->status.max_hp);
		getunitdata_sub(UNPC_MAPID, nd->bl.m);
		getunitdata_sub(UNPC_X, nd->bl.x);
		getunitdata_sub(UNPC_Y, nd->bl.y);
		getunitdata_sub(UNPC_LOOKDIR, nd->ud.dir);
		getunitdata_sub(UNPC_STR, nd->status.str);
		getunitdata_sub(UNPC_AGI, nd->status.agi);
		getunitdata_sub(UNPC_VIT, nd->status.vit);
		getunitdata_sub(UNPC_INT, nd->status.int_);
		getunitdata_sub(UNPC_DEX, nd->status.dex);
		getunitdata_sub(UNPC_LUK, nd->status.luk);
		getunitdata_sub(UNPC_PLUSALLSTAT, nd->stat_point);
		getunitdata_sub(UNPC_DMGIMMUNE, nd->ud.immune_attack);
		getunitdata_sub(UNPC_ATKRANGE, nd->status.rhw.range);
		getunitdata_sub(UNPC_ATKMIN, nd->status.rhw.atk);
		getunitdata_sub(UNPC_ATKMAX, nd->status.rhw.atk2);
		getunitdata_sub(UNPC_MATKMIN, nd->status.matk_min);
		getunitdata_sub(UNPC_MATKMAX, nd->status.matk_max);
		getunitdata_sub(UNPC_DEF, nd->status.def);
		getunitdata_sub(UNPC_MDEF, nd->status.mdef);
		getunitdata_sub(UNPC_HIT, nd->status.hit);
		getunitdata_sub(UNPC_FLEE, nd->status.flee);
		getunitdata_sub(UNPC_PDODGE, nd->status.flee2);
		getunitdata_sub(UNPC_CRIT, nd->status.cri);
		getunitdata_sub(UNPC_RACE, nd->status.race);
		getunitdata_sub(UNPC_ELETYPE, nd->status.def_ele);
		getunitdata_sub(UNPC_ELELEVEL, nd->status.ele_lv);
		getunitdata_sub(UNPC_AMOTION, nd->status.amotion);
		getunitdata_sub(UNPC_ADELAY, nd->status.adelay);
		getunitdata_sub(UNPC_DMOTION, nd->status.dmotion);
		getunitdata_sub(UNPC_SEX, nd->vd.sex);
		getunitdata_sub(UNPC_CLASS, nd->vd.class_);
		getunitdata_sub(UNPC_HAIRSTYLE, nd->vd.hair_style);
		getunitdata_sub(UNPC_HAIRCOLOR, nd->vd.hair_color);
		getunitdata_sub(UNPC_HEADBOTTOM, nd->vd.head_bottom);
		getunitdata_sub(UNPC_HEADMIDDLE, nd->vd.head_mid);
		getunitdata_sub(UNPC_HEADTOP, nd->vd.head_top);
		getunitdata_sub(UNPC_CLOTHCOLOR, nd->vd.cloth_color);
		getunitdata_sub(UNPC_SHIELD, nd->vd.shield);
		getunitdata_sub(UNPC_WEAPON, nd->vd.weapon);
		getunitdata_sub(UNPC_ROBE, nd->vd.robe);
		getunitdata_sub(UNPC_BODY2, nd->vd.body_style);
		getunitdata_sub(UNPC_DEADSIT, nd->vd.dead_sit);
		getunitdata_sub(UNPC_GROUP_ID, nd->ud.group_id);
#ifdef Pandas_Struct_Unit_CommonData_Aura
		getunitdata_sub(UNPC_AURA, nd->ucd.aura.id);
#endif // Pandas_Struct_Unit_CommonData_Aura
		break;

	default:
		return luaL_error(L, "buildin_getunitdata: Unknown object type!\n");
	}

#undef getunitdata_sub
	return 1;
}


//void script_set_constant_(const char* name, int64 value, const char* constant_name, bool isparameter, bool deprecated);
#define script_set_constant_x(v) {if((v)>INT32_MAX||(v)<INT32_MIN) luaL_newUserData<int64_t>(L, (v)); else lua_pushinteger(L, (int32_t)(v));}
#define script_set_constant_(n,v,c,p,d) {script_set_constant_x(v); lua_setfield(L, -2, c? c:n);}
#define script_set_constant(n,v,p,d) { script_set_constant_x(v); lua_setfield(L, -2, n);}
//#define script_set_constant_()

void lua_reg_ctype(lua_State* L);

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
	luaopen_bit(L);
	auto ret = true;
	lua_settop(L, 0);
	luaL_newmetatable(m_lua, UserDataMetaTableFor<script_state*>());
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
			buildin_fn_map[buildin_func[i].name] = (int)i;
			lua_pushstring(L, buildin_func[i].name); //3
			lua_pushvalue(L, 2);
			lua_pushstring(L, buildin_func[i].name); //4
			lua_call(L, 1, 1);
			lua_rawset(L, 1);
		}
	}
	lua_pop(L, 1);

#define ST_FUNC(N) {lua_pushstring(L, #N);lua_pushcfunction(L, N);	lua_rawset(L, 1);}
#define ST_FUNC2(K,N) {lua_pushstring(L, K);lua_pushcfunction(L, N);	lua_rawset(L, 1);}

	ST_FUNC(callScript);
	ST_FUNC(sleep);
	ST_FUNC(sleep2);
	ST_FUNC(ref);
	ST_FUNC(instance_ref);
	ST_FUNC(get_ref_value);
	ST_FUNC(get_ref_array);
	ST_FUNC(mes);
	ST_FUNC(next);
	ST_FUNC(clear);
	ST_FUNC(close);
	ST_FUNC(close2);
	ST_FUNC(close3);
	ST_FUNC(getmapxy);
	ST_FUNC(select);
	ST_FUNC(input);
	ST_FUNC(progressbar);
	ST_FUNC(progressbar_npc);
	ST_FUNC(readparam);
	ST_FUNC(setparam);
	ST_FUNC(getpartymember);
	ST_FUNC(checkcell);
	ST_FUNC(getunitdata);

#undef ST_FUNC
#undef ST_FUNC2
	lua_pushvalue(L, 1);
	lua_pushstring(L, "__index");
	lua_pushvalue(L, 1);
	lua_rawset(L, -3);
	lua_pushstring(L, "__tostring");
	lua_pushcfunction(L, ToString<script_state*>);
	lua_rawset(L, -3);
	lua_setglobal(L, UserDataMetaTableFor<script_state*>());
	luaL_newmetatable(m_lua, UserDataMetaTableFor<script_data>());
	lua_pushstring(L, "__tostring");
	lua_pushcfunction(L, ToString<script_data>);
	lua_rawset(L, -3);
	lua_setglobal(L, UserDataMetaTableFor<script_data>());

	lua_reg_ctype(L);

	lua_newtable(L);
	for (auto& n : *constList) {
		script_set_constant_x(n.val);
		lua_setfield(L, -2, n.name.c_str());
	}
	delete constList;
	constList = nullptr;
#include "script_constants.hpp"

	lua_setglobal(L, "CONST");
	luaL_dostring(L, "function __WARP__() local fn = coroutine.yield(); return fn(coroutine.yield()) end ");
	lua_settop(L, 0);

	ret = luaL_dostring(m_lua, "pcall(function() dofile('npc/init.lua') end);");
	if (ret) {
		return false;
	}
	return true;
}


script_cmd_result lua_run(script_state* st, lua_State* L0) {
	int n = script_lastdata(st);
	int ret = 0;
	if (st->state == e_script_state::RUN) {
		auto L = lua_newthread(L0);
		auto lRef = luaL_ref(L0);
		lua_getglobal(L, "__WARP__");
		lua_resume(L, 0);
		auto fnStr = script_getstr(st, 2);
		//ShowDebug("lua_run %s", fnStr);
		lua_getglobal(L, fnStr);
		lua_resume(L, 1);
		auto p = luaL_newUserData<script_state*>(L, st);
		for (int i = 3; i <= n; i++) {
			script_data* data = get_val(st, script_getdata(st, i));
			if (data_isint(data)) {
				lua_pushnumber(L, static_cast<lua_Number>(data->u.num));
			}
			else if (data_isstring(data)) {
				lua_pushstring(L, data->u.str);
			}
			else {
				lua_pushstring(L, conv_str(st, data));
			}
		}
		int ret = lua_resume(L, n - 2 + 1);
		if (ret == LUA_OK) {
			//ShowDebug("lua run ok\n");
			auto b = lua_toboolean(L, -2);
			if (b) {
				if (lua_isnumber(L, -1)) {
					script_pushint64(st, static_cast<int64>(lua_tonumber(L, -1)));
				}
				else if (lua_isstring(L, -1)) {
					script_pushstrcopy(st, lua_tostring(L, -1));
				}
			}
			else {
				printf("lua error: %s\n", lua_tostring(L, -1));
			}
			lua_settop(L, 0);
			luaL_unref(L0, lRef);
			return b ? SCRIPT_CMD_SUCCESS : SCRIPT_CMD_FAILURE;
		}
		if (ret == LUA_YIELD) {
			if (st->state == e_script_state::END || st->state == e_script_state::CLOSE) {
				luaL_unref(L0, lRef);
				return SCRIPT_CMD_SUCCESS;
			}
			st->lua_state.refId = lRef;
			st->lua_state.thread = L;
			st->state = RERUNLINE;
			return SCRIPT_CMD_SUCCESS;
		}
		auto ei = lua_gettop(L);
		printf("error ");
		for (int i = 1; i <= ei; i++) {
			printf("%d %s\n", i, lua_tostring(L, i));
		}
		printf("\n");
		return SCRIPT_CMD_FAILURE;
	}
	if (st->state == e_script_state::RERUNLINE) {
		if (st->lua_state.fn != nullptr) {
			int lRet = -1;
			auto fn = st->lua_state.fn;
			auto L = st->lua_state.thread;
			st->lua_state.fn = nullptr;
			auto sRet = fn(st, L, lRet);
			if (lRet >= LUA_ERRRUN) {
				auto ei = lua_gettop(L);
				printf("error ");
				for (int i = 1; i <= ei; i++) {
					printf("%d %s\n", i, lua_tostring(L, i));
				}
				printf("\n");
			}
			if (lRet != LUA_YIELD) {
				luaL_unref(L0, st->lua_state.refId);
				st->lua_state.thread = nullptr;
			}
			else if (st->state == e_script_state::END || st->state == e_script_state::CLOSE) {
				luaL_unref(L0, st->lua_state.refId);
				st->lua_state.thread = nullptr;
				return SCRIPT_CMD_SUCCESS;
			}
			if (lRet == LUA_YIELD) {
				st->state = RERUNLINE;
				return SCRIPT_CMD_SUCCESS;
			}
			return sRet;
		}
	}


	return SCRIPT_CMD_FAILURE;
}
