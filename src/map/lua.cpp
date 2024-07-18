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


#define LUA_FUNC(fn) int fn(lua_State* L)
#define LUA_REGFUNC(s) luaL_Reg {#s, s}
#define LUA_REGFUNC2(s,ss) luaL_Reg {s, ss}

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
				auto is_str = is_string_variable(reference_getname(d));
				auto xLen = script_array_highest_key(st, nullptr, reference_getname(d), reference_getref(d));
				if (xLen > 1) {
					auto dId = reference_getid(d);
					for (int j = 0; j < xLen; ++j) {
						if (is_str) {
							lua_pushstring(L, get_val2_str(st, reference_uid(dId, j), d->ref));
						}
						else {
							lua_pushnumber(L, get_val2_num(st, reference_uid(dId, j), d->ref));
						}
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
	pop_stack(st, end0 - 1, st->end);
	st->start = start0;
	st->end = end0;
	st->stack->sp = sp;
	st->funcname = funcname0;
	db_destroy(m);
	db_destroy(m2);
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
	if (lua_gettop(L) >= 3) {
		if (lua_type(L, 3) == LUA_TSTRING)
			sd = map_nick2sd(lua_tostring(L, 3), false);
		else
			sd = map_id2sd(lua_tointeger(L, 3));
	}
	else {
		sd = map_id2sd(st->rid);
	}

	if (!sd)
		return luaL_error(L, "player not found.");

	auto val = pc_readparam(sd, lua_tointeger(L, 2));
	if (val < INT32_MIN || val > INT32_MAX) {
		luaL_newUserData(L, val);
	}
	else {
		lua_pushinteger(L, val);
	}
	return 1;
}

int menu_countoptions(const char* str, int max_count, int* total);

RESUME_FUNC(select) {
	map_session_data* sd = map_id2sd(st->rid);
	int i;
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

LUA_FUNC(ToStringUserData) {
	if (lua_gettop(L) != 1) {
		lua_pushnil(L);
		return 1;
	}
	if (lua_type(L, 1) != LUA_TUSERDATA) {
		lua_pushnil(L);
		return 1;
	}
	char buff[256] = { 0 };
	sprintf(buff, "ScriptData@0x%p", lua_touserdata(L, 1));
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
	if (lua_type(L, 1) != LUA_TUSERDATA) {
		return luaL_error(L, "error");
	}
	if (!lua_isstring(L, 2)) {
		return luaL_error(L, "error");
	}
	auto buffer = lua_tostring(L, 2);
	char varname[256] = "";
	int elem = 0;
	if (sscanf(buffer, "%99[^[][%11d]", varname, &elem) < 2)
		elem = 0;
	trim(varname);
	auto s = luaL_newUserData<script_data>(L, {
		C_NAME,
		static_cast<int64>(reference_uid(add_str(varname), elem)),
		nullptr
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
		ShowError("instance_ref: Invalid scope. %s is not an instance variable.\n", varname);
		return luaL_error(L, "error");
	}

	int instance_id = lua_tointeger(L, 3);

	if (instance_id <= 0) {
		ShowError("instance_ref: Invalid instance ID %d.\n", instance_id);
		return luaL_error(L, "error");
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
#define script_set_constant_x(v) {if((v)>INT32_MAX||(v)<INT32_MIN) luaL_newUserData<int64_t>(L, (v)); else lua_pushinteger(L, (int32_t)(v));}
#define script_set_constant_(n,v,c,p,d) {script_set_constant_x(v); lua_setfield(L, -2, c? c:n);}
#define script_set_constant(n,v,p,d) { script_set_constant_x(v); lua_setfield(L, -2, n);}
//#define script_set_constant_()
extern ConstantDatabase constant_db;

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

	ST_FUNC(callScript)
		ST_FUNC(sleep)
		ST_FUNC(sleep2)
		ST_FUNC(ref)
		ST_FUNC(instance_ref)
		ST_FUNC(get_ref_value)
		ST_FUNC(get_ref_array)
		ST_FUNC(mes)
		ST_FUNC(next)
		ST_FUNC(clear)
		ST_FUNC(close)
		ST_FUNC(close2)
		ST_FUNC(close3)
		ST_FUNC(getmapxy)
		ST_FUNC(select)
		ST_FUNC(readparam)

#undef ST_FUNC
#undef ST_FUNC2

		luaL_newmetatable(m_lua, UserDataMetaTableFor<script_state*>());
	lua_pushstring(L, "__index");
	lua_pushvalue(L, 1);
	lua_rawset(L, -3);
	lua_pushstring(L, "__tostring");
	lua_pushcfunction(L, ToString);
	lua_rawset(L, -3);
	luaL_newmetatable(m_lua, UserDataMetaTableFor<int64_t>());
	lua_pushstring(L, "__tostring");
	lua_pushcfunction(L, INT64ToString);
	lua_rawset(L, -3);
	luaL_newmetatable(m_lua, UserDataMetaTableFor<script_data>());
	lua_pushstring(L, "__tostring");
	lua_pushcfunction(L, ToStringUserData);
	lua_rawset(L, -3);
	lua_newtable(L);
	for (auto& n : *constList) {
		luaL_newUserData<int64_t>(L, n.val);
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
		lua_getglobal(L, script_getstr(st, 2));
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
			luaL_unref(L0, lRef);
			lua_gc(L, LUA_GCCOLLECT, 0);
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
