#include <lua.h>
#include <ffi.h>
#include <stdint.h>

static
int cast_lua_pointer(lua_State *L, int i, void **pp)
{
	int ltype;
	lua_CFunction fn;
	const char *name;
	struct cvar *var;
	struct cfunc *func;

	ltype = lua_type(L, i);
	switch (ltype) {
		case LUA_TNIL:
			*pp = 0;
			break;
		case LUA_TNUMBER:
			*pp = (void *) luaL_checkinteger(L, i);
			break;
		case LUA_TSTRING:
			/* FFI discards the `const' qualifier;
			 * the C function must not change the string. */
			*pp = (void *) lua_tostring(L, i);
			break;
		case LUA_TFUNCTION:
			fn = lua_tocfunction(L, i);
			if (!fn || lua_getupvalue(L, i, 1))
				return 0; /* don't support C closure yet */
			*pp = (void *) fn;
			break;
		case LUA_TLIGHTUSERDATA:
			*pp = lua_touserdata(L, i);
			break;
		case LUA_TUSERDATA:
			if (luaL_getmetafield(L, i, "__name") != LUA_TSTRING)
				goto fail;
			name = lua_tostring(L, -1);
			if (strcmp(name, "ffi_cvar") == 0) {
				var = lua_touserdata(L, i);
				if (var->arraysize > 0) {
					*pp = (void *) var->mem;
				} else if (var->type->type == FFI_TYPE_POINTER) {
					*pp = *(void **) var->mem;
				} else {
					goto fail;
				}
			} else if (strcmp(name, "ffi_cfunc") == 0) {
				func = lua_touserdata(L, i);
				*pp = (void *) func->fn;
			} else {
				goto fail;
			}
			lua_pop(L, 1); /* __name */
			break;
		default:
fail:
			return 0;
	}
	return 1;
}

static
int cast_int_c(lua_Integer n, void *addr, int type)
{
	switch (type) {
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
int cast_number_c(lua_Number n, void *addr, int type)
{
	switch (type) {
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
	struct cvar *var;
	int ltype;

	ltype = lua_type(L, i);
	switch (ltype) {
		case LUA_TBOOLEAN:
			return cast_int_c(lua_toboolean(L, i), addr, type);
		case LUA_TNUMBER:
			return (lua_isinteger(L, i))
				? cast_int_c(lua_tointeger(L, i), addr, type)
				: cast_number_c(lua_tonumber(L, i), addr, type);
		case LUA_TUSERDATA:
			var = luaL_checkudata(L, i, "ffi_cvar");
			if (!var || type != var->type->type)
				return 0;
			memcpy(addr, var->mem, var->type->size);
			return 1;
	}
	return 0;
}

static
int cast_c_lua(lua_State *L, void *addr, int type)
{
	lua_Integer i;
	lua_Number n;
	void *p;

	switch (type) {
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
