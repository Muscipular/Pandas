#pragma once
#include "windows.h"
#include <initializer_list>
#include <string>
#include <variant>
#include <vector>
#include <map>
#include <mutex>
#include "../../3rdparty/mysql/include/mysql.h"
#include "./sql.hpp"

enum class SqlResult {
	OK,
	Error,
};
enum class ArgValueType {
	Int,
	UInt,
	//Double,
	Float,
	String,
};

union ArgValueData {
	int IntValue;
	DWORD DWORD;
	float Float;
	const char* String;
};

typedef struct ArgValue {
	ArgValueType type;
	ArgValueData data;
	ArgValue(int value) {
		type = ArgValueType::Int;
		data.IntValue = value;
	}
	ArgValue(DWORD value) {
		type = ArgValueType::UInt;
		data.DWORD = value;
	}
	ArgValue(float value) {
		type = ArgValueType::Float;
		data.Float = value;
	}
	ArgValue(const char* value) {
		type = ArgValueType::String;
		data.String = value;
	}
} ArgValue;
typedef std::variant<std::string, int, double> _MapValue;
typedef std::map<std::string, _MapValue> NvMap;

template <typename TR>
using FnMysqlResult = TR(__cdecl*)(MYSQL_BIND* binds, MYSQL_FIELD* fields, size_t fieldNum);

NvMap FnMysqlResult1(MYSQL_BIND* binds, MYSQL_FIELD* fields, size_t fieldNum);

void setupMysql(Sql* sql);
//void setupMysql(const char* server, const char* user, const char* pwd, const char* db, const char* charset);

extern MYSQL* getConnection();
extern std::mutex _sql_mutex;

template<typename TR, FnMysqlResult<TR> TC>
SqlResult querySql(const char* sql, std::vector<ArgValue>* args, std::vector<TR>* result, int64_t* effectRows) {
	std::lock_guard lg(_sql_mutex);
	auto mysql = getConnection();
	MYSQL_STMT* stmt;
	if (!mysql) {
		return SqlResult::Error;
	}
	SqlResult ret = SqlResult::OK;
	do
	{
		stmt = mysql_stmt_init(mysql);
		if (!stmt) {
			ret = SqlResult::Error;
			break;
		}
		if (mysql_stmt_prepare(stmt, sql, strlen(sql) + 1) != 0) {
			ret = SqlResult::Error;
			break;
		}
		MYSQL_BIND params[80];
		memset(params, 0, sizeof(params));
		int i = 0;
		for (auto arg = args->begin(); arg < args->end();arg++) {
			switch (arg->type) {
			case ArgValueType::Int:
				params[i].buffer = (void*)&arg->data.IntValue;
				params[i].buffer_type = enum_field_types::MYSQL_TYPE_LONG;
				break;
			case ArgValueType::Float:
				params[i].buffer = (void*)&arg->data.Float;
				params[i].buffer_type = enum_field_types::MYSQL_TYPE_FLOAT;
				break;
			case ArgValueType::UInt:
				params[i].buffer = (void*)&arg->data.DWORD;
				params[i].is_unsigned = TRUE;
				params[i].buffer_type = enum_field_types::MYSQL_TYPE_LONG;
				break;
			case ArgValueType::String:
				params[i].buffer = (void*)arg->data.String;
				params[i].buffer_length = strlen(arg->data.String);
				params[i].buffer_type = enum_field_types::MYSQL_TYPE_STRING;
				break;
			}
			i++;
		}
		if (mysql_stmt_bind_param(stmt, params) != 0) {
			ret = SqlResult::Error;
			printf_s("[ERR] MYSQL Query %s, sql: %s\n", mysql_error(mysql), sql);
			//printStack();
			break;
		}
		if (mysql_stmt_execute(stmt) != 0) {
			ret = SqlResult::Error;
			printf_s("[ERR] MYSQL Query %s, sql: %s\n", mysql_error(mysql), sql);
			//printStack();
			break;
		}
		if (effectRows) {
			*effectRows = mysql_stmt_affected_rows(stmt);
		}
		size_t num_fields = mysql_stmt_field_count(stmt);
		if (num_fields <= 0 || !result) {
			break;
		}
		auto rs_metadata = mysql_stmt_result_metadata(stmt);
		if (!rs_metadata) {
			ret = SqlResult::Error;
			break;
		}
		auto fields = mysql_fetch_fields(rs_metadata);
		if (!fields) {
			ret = SqlResult::Error;
			mysql_free_result(rs_metadata);
			break;
		}
		std::vector<void*> buffers;
		MYSQL_BIND* rs_bind = (MYSQL_BIND*)malloc(sizeof(MYSQL_BIND) * num_fields);
		buffers.push_back(rs_bind);
		memset(rs_bind, 0, sizeof(MYSQL_BIND) * num_fields);
		int* ptr = 0;
		int* ptrEnd = 0;
		for (size_t i = 0; i < num_fields; i++) {
			rs_bind[i].buffer_type = fields[i].type;
			//rs_bind[i].is_null = &is_null[i];

			switch (fields[i].type)
			{
			case MYSQL_TYPE_BIT:
			case MYSQL_TYPE_TINY:
			case MYSQL_TYPE_NULL:
			case MYSQL_TYPE_INT24:
			case MYSQL_TYPE_SHORT:
			case MYSQL_TYPE_LONG:
			case MYSQL_TYPE_FLOAT:
				if (ptr == ptrEnd) {
					ptr = (int*)malloc(1024);
					if (!ptr) {
						ret = SqlResult::Error;
						break;
					}
					memset(ptr, 0, 1024);
					ptrEnd = ptr + (1024 / 4);
					buffers.push_back(ptr);
				}
				rs_bind[i].buffer = ptr;
				rs_bind[i].buffer_length = sizeof(int);
				ptr += 1;
				break;
			case MYSQL_TYPE_DOUBLE:
			case MYSQL_TYPE_LONGLONG:
				if (ptr + 1 >= ptrEnd) {
					ptr = (int*)malloc(1024);
					if (!ptr) {
						ret = SqlResult::Error;
						break;
					}
					memset(ptr, 0, 1024);
					ptrEnd = ptr + (1024 / 4);
					buffers.push_back(ptr);
				}
				rs_bind[i].buffer = ptr;
				rs_bind[i].buffer_length = sizeof(int64_t);
				ptr += 2;
				break;
			case MYSQL_TYPE_VARCHAR:
			case MYSQL_TYPE_VAR_STRING:
			case MYSQL_TYPE_STRING:
			case MYSQL_TYPE_BLOB:
			case MYSQL_TYPE_TINY_BLOB:
			case MYSQL_TYPE_LONG_BLOB:
			case MYSQL_TYPE_MEDIUM_BLOB:
				if (((DWORD)ptrEnd - (DWORD)ptr) > fields[i].length) {
					rs_bind[i].buffer = ptr;
					rs_bind[i].buffer_length = fields[i].length;
					ptr += fields[i].length / 4 + 1;
				}
				else {
					auto len = min(1u << 23, fields[i].length) + 1;
					auto buf = malloc(len);
					if (!buf) {
						ret = SqlResult::Error;
						break;
					}
					memset(buf, 0, len);
					buffers.push_back(buf);
					rs_bind[i].buffer = buf;
					rs_bind[i].buffer_length = len;
				}
				break;
			default:
				ret = SqlResult::Error;
				break;
			}
			if (ret != SqlResult::OK) {
				break;
			}
		}
		if (mysql_stmt_bind_result(stmt, rs_bind) != 0) {
			ret = SqlResult::Error;
			printf_s("[ERR] MYSQL Query %s, sql: %s\n", mysql_error(mysql), sql);
			//printStack();
		}
		else {
			do {
				auto status = mysql_stmt_fetch(stmt);
				if (status == MYSQL_NO_DATA) {
					break;
				}
				if (status) {
					ret = SqlResult::Error;
					break;
				}
				result->push_back(TC(rs_bind, fields, num_fields));
			} while (true);
		}
		for (auto c : buffers) {
			free(c);
		}
		mysql_free_result(rs_metadata);
	} while (false);
	if (stmt) {
		mysql_stmt_close(stmt);
	}
	return ret;
}

template<typename TR, FnMysqlResult<TR> TC>
SqlResult querySql(const char* sql, std::initializer_list<ArgValue> args, std::vector<TR>* result, int64_t* effectRows) {
	std::vector<ArgValue> arg(args);
	return querySql<TR, TC>(sql, &arg, result, effectRows);
}

template<typename TR, FnMysqlResult<TR> TC>
SqlResult inline querySql(const char* sql, std::initializer_list<ArgValue> args, std::vector<TR>* result) {
	return querySql<TR, TC>(sql, args, result, NULL);
}

template<typename TR, FnMysqlResult<TR> TC>
SqlResult inline querySql(const char* sql, std::vector<ArgValue>* args, std::vector<TR>* result) {
	return querySql<TR, TC>(sql, args, result, NULL);
}

SqlResult runSql(const char* sql, std::vector<ArgValue>* args, int64_t* result);
SqlResult runSql(const char* sql, std::initializer_list<ArgValue> args, int64_t* result);
SqlResult runSql(const char* sql);
SqlResult runSql(const char* sql, int64_t* result);
