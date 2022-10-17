#include "mysql_utils.h"
std::mutex _sql_mutex;

NvMap FnMysqlResult1(MYSQL_BIND* binds, MYSQL_FIELD* fields, size_t fieldNum) {
	NvMap map;
	for (size_t i = 0; i < fieldNum; i++)
	{
		switch (fields[i].type)
		{
		case MYSQL_TYPE_BIT:
			map[fields[i].name] = (int)(*(UCHAR*)binds[i].buffer & 1);
			break;
		case MYSQL_TYPE_TINY:
			if (binds[i].is_unsigned) {
				map[fields[i].name] = (int)(*(UCHAR*)binds[i].buffer);
			}
			else {
				map[fields[i].name] = (int)(*(CHAR*)binds[i].buffer);
			}
			break;
		default:
		case MYSQL_TYPE_NULL:
			break;
		case MYSQL_TYPE_INT24:
			if (binds[i].is_unsigned) {
				map[fields[i].name] = (int)(*(uint32_t*)binds[i].buffer);
			}
			else {
				map[fields[i].name] = (int)((*(int*)binds[i].buffer & 0x7fffff) | ((*(int*)binds[i].buffer & 0x800000) << 8));
			}
			break;
		case MYSQL_TYPE_SHORT:
			if (binds[i].is_unsigned) {
				map[fields[i].name] = (int)(*(USHORT*)binds[i].buffer);
			}
			else {
				map[fields[i].name] = (int)(*(SHORT*)binds[i].buffer);
			}
			break;
		case MYSQL_TYPE_LONG:
			if (binds[i].is_unsigned) {
				map[fields[i].name] = (int)(*(UINT32*)binds[i].buffer);
			}
			else {
				map[fields[i].name] = (int)(*(int*)binds[i].buffer);
			}
			break;
		case MYSQL_TYPE_FLOAT:
			map[fields[i].name] = (double)(*(float*)binds[i].buffer);
			break;
		case MYSQL_TYPE_DOUBLE:
			map[fields[i].name] = (*(double*)binds[i].buffer);
			break;
		case MYSQL_TYPE_LONGLONG:
			map[fields[i].name] = (int)(*(int64_t*)binds[i].buffer);
			break;
		case MYSQL_TYPE_VARCHAR:
		case MYSQL_TYPE_VAR_STRING:
		case MYSQL_TYPE_STRING:
		case MYSQL_TYPE_BLOB:
		case MYSQL_TYPE_TINY_BLOB:
		case MYSQL_TYPE_LONG_BLOB:
		case MYSQL_TYPE_MEDIUM_BLOB:
			map[fields[i].name] = (const char*)binds[i].buffer;
			break;
		}
	}
	return std::move(map);
}
char server[256]; char user[256]; char pwd[256]; char db[256]; char charset[256];

void setupMysql(const char* pserver, const char* puser, const char* ppwd, const char* pdb, const char* pcharset) {
	memcpy_s(server, sizeof(server), pserver, strlen(pserver));
	memcpy_s(user, sizeof(user), puser, strlen(puser));
	memcpy_s(pwd, sizeof(pwd), ppwd, strlen(ppwd));
	memcpy_s(db, sizeof(db), pdb, strlen(pdb));
	memcpy_s(charset, sizeof(charset), pcharset, strlen(pcharset));
}

MYSQL* getConnection() {
	static MYSQL* mysql;

	if (mysql) {
		return mysql;
	}
	auto mysql1 = mysql_init(NULL);
	if (!mysql1) {
		return NULL;
	}
	my_bool val = 1;
	mysql_options(mysql1, mysql_option::MYSQL_OPT_RECONNECT, &val);
	DWORD val2 = 3;
	mysql_options(mysql1, mysql_option::MYSQL_OPT_CONNECT_TIMEOUT, &val2);
	mysql_options(mysql1, mysql_option::MYSQL_OPT_READ_TIMEOUT, &val2);
	mysql_options(mysql1, mysql_option::MYSQL_OPT_WRITE_TIMEOUT, &val2);
	mysql_options(mysql1, mysql_option::MYSQL_INIT_COMMAND, "set interactive_timeout = 604800;");
	mysql_options(mysql1, mysql_option::MYSQL_INIT_COMMAND, "set wait_timeout = 300;");
	if (!mysql_real_connect(mysql1, server, user, pwd, db, 0, NULL, 0)) {
		auto err = mysql_error(mysql1);
		printf_s("\n[错误] Mysql连接失败 %s %s %s %s\n", server, user, db, err);
		mysql_close(mysql1);
		return NULL;
	}
	mysql_set_character_set(mysql1, charset);
	mysql_select_db(mysql1, db);
	return mysql = mysql1;
}

SqlResult runSql(const char* sql, std::vector<ArgValue>* args, int64_t* result) {
	return querySql<NvMap, FnMysqlResult1>(sql, args, NULL, result);
}

SqlResult runSql(const char* sql, std::initializer_list<ArgValue> args, int64_t* result) {
	return querySql<NvMap, FnMysqlResult1>(sql, args, NULL, result);
}

SqlResult runSql(const char* sql) {
	return runSql(sql, 0);
}

SqlResult runSql(const char* sql, int64_t* result) {
	std::lock_guard lg(_sql_mutex);
	auto mysql = getConnection();
	if (mysql_real_query(mysql, sql, strlen(sql) + 1) != 0) {
		return SqlResult::Error;
	}
	if (result) {
		*result = mysql_affected_rows(mysql);
	}
	auto res = mysql_use_result(mysql);
	if (res) {
		mysql_free_result(res);
	}
	return SqlResult::OK;
}
