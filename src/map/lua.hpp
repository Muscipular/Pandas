#pragma once
extern "C" {
#include "../../3rdparty/lua/lua.h"
#include "../../3rdparty/lua/lualib.h"
#include "../../3rdparty/lua/lauxlib.h"
}
extern lua_State* m_lua;
bool init_lua();
