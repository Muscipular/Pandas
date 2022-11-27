cmake_minimum_required(VERSION 2.8)

set (LUA_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}"
    CACHE INTERNAL "lua include dir" )
find_library( LUA_LIBRARIES
	NAMES luajit
	PATHS "${CMAKE_CURRENT_SOURCE_DIR}"
	NO_DEFAULT_PATH )
mark_as_advanced( LUA_LIBRARIES )
mark_as_advanced( LUA_INCLUDE_DIRS )

message( STATUS "luajit - ${LUA_LIBRARIES} ${LUA_INCLUDE_DIRS}" )