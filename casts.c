#include <lua.h>
#include <ffi.h>
#include <stdint.h>

static
int cast_int_c(lua_Integer n, void *addr, int type)
{
	switch (type) {
		case FFI_TYPE_INT: *(int *) addr = n; break;
		case FFI_TYPE_UINT8: *(uint8_t *) addr = n; break;
		case FFI_TYPE_UINT16: *(uint16_t *) addr = n; break;
		case FFI_TYPE_UINT32: *(uint32_t *) addr = n; break;
		case FFI_TYPE_UINT64: *(uint64_t *) addr = n; break;
		case FFI_TYPE_SINT8: *(int8_t *) addr = n; break;
		case FFI_TYPE_SINT16: *(int16_t *) addr = n; break;
		case FFI_TYPE_SINT32: *(int32_t *) addr = n; break;
		case FFI_TYPE_SINT64: *(int64_t *) addr = n; break;

		case FFI_TYPE_FLOAT: *(float *) addr = n; break;
		case FFI_TYPE_DOUBLE: *(double *) addr = n; break;
		case FFI_TYPE_LONGDOUBLE: *(long double *) addr = n; break;

		case FFI_TYPE_POINTER: *(void **) addr = (void *) n; break;

		default: return 0;
	}
	return 1;
}

static
int cast_number_c(lua_Number n, void *addr, int type)
{
	switch (type) {
		case FFI_TYPE_INT: *(int *) addr = n; break;
		case FFI_TYPE_UINT8: *(uint8_t *) addr = n; break;
		case FFI_TYPE_UINT16: *(uint16_t *) addr = n; break;
		case FFI_TYPE_UINT32: *(uint32_t *) addr = n; break;
		case FFI_TYPE_UINT64: *(uint64_t *) addr = n; break;
		case FFI_TYPE_SINT8: *(int8_t *) addr = n; break;
		case FFI_TYPE_SINT16: *(int16_t *) addr = n; break;
		case FFI_TYPE_SINT32: *(int32_t *) addr = n; break;
		case FFI_TYPE_SINT64: *(int64_t *) addr = n; break;

		case FFI_TYPE_FLOAT: *(float *) addr = n; break;
		case FFI_TYPE_DOUBLE: *(double *) addr = n; break;
		case FFI_TYPE_LONGDOUBLE: *(long double *) addr = n; break;

		default: return 0;
	}
	return 1;
}

static
int cast_lua_c(lua_State *L, int i, void *addr, int type)
{
	if (lua_isboolean(L, i))
		return cast_int_c(lua_toboolean(L, i), addr, type);
	return lua_isinteger(L, i)
		? cast_int_c(lua_tointeger(L, i), addr, type)
		: cast_number_c(lua_tonumber(L, i), addr, type);
}

static
int cast_c_lua(lua_State *L, void *addr, int type)
{
	lua_Integer i;
	lua_Number n;
	void *p;

	switch (type) {
		case FFI_TYPE_INT: i = *(int *) addr; goto cast_int;
		case FFI_TYPE_UINT8: i = *(uint8_t *) addr; goto cast_int;
		case FFI_TYPE_UINT16: i = *(uint16_t *) addr; goto cast_int;
		case FFI_TYPE_UINT32: i = *(uint32_t *) addr; goto cast_int;
		case FFI_TYPE_UINT64: i = *(uint64_t *) addr; goto cast_int;
		case FFI_TYPE_SINT8: i = *(int8_t *) addr; goto cast_int;
		case FFI_TYPE_SINT16: i = *(int16_t *) addr; goto cast_int;
		case FFI_TYPE_SINT32: i = *(int32_t *) addr; goto cast_int;
		case FFI_TYPE_SINT64: i = *(int64_t *) addr; goto cast_int;
cast_int:
			lua_pushinteger(L, i); break;

		case FFI_TYPE_FLOAT: n = *(float *) addr; goto cast_num;
		case FFI_TYPE_DOUBLE: n = *(double *) addr; goto cast_num;
		case FFI_TYPE_LONGDOUBLE: n = *(long double *) addr; goto cast_num;
cast_num:
			lua_pushnumber(L, n); break;

		case FFI_TYPE_POINTER:
			p = *(void **) addr;
			if (!p)
				lua_pushnil(L);
			else
				lua_pushlightuserdata(L, p);
			break;

		default: return 0;
	}
	return 1;
}
