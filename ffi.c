#define LUA_LIB

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <ffi.h>
#include <stdlib.h>
#include <string.h>

struct cvar {
	ffi_type *type;
	size_t arraysize;
	char mem[0];
};

static
int callloadlib(lua_State *L)
{
	int ret = lua_gettop(L) - 2;

	lua_call(L, 2, LUA_MULTRET);
	return !lua_isnil(L, ret);
}

static
int openlib(lua_State *L)
{
	int ABI;

	lua_pushvalue(L, lua_upvalueindex(1)); /* loadlib() */
	lua_pushvalue(L, 1); /* libname */
	lua_pushliteral(L, "*");
	if (!callloadlib(L)) {
		lua_pop(L, 1); /* pop "open" */
		/* error message is on stack top */
		lua_error(L);
	}
	lua_pop(L, 1); /* pop true on success */

	/* XXX: does not check the ABI;
	 * but setting ABI is unportable anyway. */
	ABI = luaL_optinteger(L, 2, FFI_DEFAULT_ABI);
	lua_createtable(L, 0, 2);
	lua_pushvalue(L, 1);
	lua_setfield(L, -2, "__FFI_LIBNAME");
	lua_pushinteger(L, ABI);
	lua_setfield(L, -2, "__FFI_ABI");
	luaL_setmetatable(L, "ffi_library");
	return 1;
}

static
int funccall(lua_State *L);

static
int libindex(lua_State *L)
{
	lua_CFunction fn;

	lua_pushvalue(L, lua_upvalueindex(1)); /* loadlib() */
	lua_pushliteral(L, "__FFI_LIBNAME");
	lua_rawget(L, 1);
	lua_pushvalue(L, 2); /* funcname */

	lua_pushliteral(L, "__FFI_PREFIX");
	if (lua_rawget(L, 1) == LUA_TSTRING) {
		lua_insert(L, -2);
		lua_concat(L, 2);
	} else {
		lua_pop(L, 1);
	}

	callloadlib(L);
	fn = lua_tocfunction(L, -1); /* fun = loadlib(libname, funcname) */
	if (!fn) {
		lua_pushnil(L);
		return 1;
	}

	/* upvalues: (1) function handle (already on top of stack) */
	lua_pushliteral(L, "__FFI_ABI");
	lua_rawget(L, 1); /* (2) ABI */
	lua_pushlightuserdata(L, &ffi_type_sint); /* (3) return type */
	lua_pushnil(L); /* (4) arglist (default: empty) */
	lua_pushcclosure(L, funccall, 4);

	/* cache in library table */
	lua_pushvalue(L, 2); /* funcname */
	lua_pushvalue(L, -2); /* closure */
	lua_rawset(L, 1); /* lib[funcname] = closure */

	return 1;
}

struct arglist {
	unsigned int nargs;
	int variadic;
	ffi_type *atypes[0];
};

/* check ffi.h for correctness */
#define TYPE_IS_INT(T) ((1<<(T))&8162) /* 1, 5--12 */
#define TYPE_IS_FLOAT(T) ((1<<(T))&28) /* 2, 3, 4 */

static
int pusharg_cvar(ffi_raw *arg, struct cvar *var, ffi_type *ftype)
{
	ffi_type *vartype = var->type;

	if (ftype == vartype ||
		(TYPE_IS_INT(ftype->type) && TYPE_IS_INT(vartype->type)) ||
		(TYPE_IS_FLOAT(ftype->type) && TYPE_IS_FLOAT(vartype->type))) {
		if (ftype->size < vartype->size)
			return 0;
		if (var->type->size > sizeof (arg->data)) {
			arg->ptr = var->mem;
		} else {
			memcpy(arg->data, var->mem, var->type->size);
		}
		return 1;
	}
	return 0;
}

static const char *type_names[] = {
	"void", "int", "float", "double", "long double",
	"uint8", "int8", "uint16", "int16",
	"uint32", "int32", "uint64", "int64",
	"struct", "pointer", "complex",
};

#include "casts.c"

static
ffi_type *pusharg(lua_State *L, int i, ffi_raw *args, ffi_type *ftype)
{
	int ltype;
	const char *ltypename;
	ffi_raw *arg;
	struct cvar *var;

	arg = &args[i];
	ltype = lua_type(L, ++i); /* lua index = C index + 1 */
#define CHECK(T1, T2) if (!T1) T1 = T2; else if (T1 != T2) goto convert_fail;
	switch (ltype) {
		case LUA_TNIL: /* as NULL pointer */
			CHECK(ftype, &ffi_type_pointer);
			arg->ptr = NULL;
			break;
		case LUA_TBOOLEAN:
		case LUA_TNUMBER:
			if (!ftype) /* default as int */
				ftype = &ffi_type_sint;
			if (!cast_lua_c(L, i, arg->data, ftype->type))
				goto convert_fail;
			break;
		case LUA_TSTRING: /* as const char * */
			CHECK(ftype, &ffi_type_pointer);
			arg->ptr = (void *) lua_tostring(L, i);
			break;
		case LUA_TFUNCTION: /* as pointer */
			CHECK(ftype, &ffi_type_pointer);
			arg->ptr = (void *) lua_tocfunction(L, i);
			if (!arg->ptr || lua_getupvalue(L, i, 1))
				goto convert_fail;
			break;

		case LUA_TLIGHTUSERDATA:
			CHECK(ftype, &ffi_type_pointer);
			arg->ptr = lua_touserdata(L, i);
			break;

		case LUA_TUSERDATA:
			var = luaL_testudata(L, i, "ffi_cvar");
			if (var->arraysize > 0) {
				CHECK(ftype, &ffi_type_pointer);
				arg->ptr = var->mem;
			} else {
				if (!ftype)
					ftype = var->type;
				if (!pusharg_cvar(arg, var, ftype))
					goto convert_fail;
			}
			break;

		/* these cannot be converted */
		case LUA_TTABLE:
		case LUA_TTHREAD:
		default:
convert_fail:
			ltypename = lua_typename(L, ltype);
			if (ftype)
				lua_pushfstring(L, "%s expected, got %s\n",
						type_names[ftype->type],
						ltypename);
			else
				lua_pushfstring(L, "cannot pass a lua %s "
						"to FFI function",
						ltypename);
			luaL_argerror(L, i, lua_tostring(L, -1));
	}
#undef CHECK
	return ftype;
}

static
ffi_type *get_ffi_type(lua_State *L, int i)
{
	ffi_type *type = 0;

	if (lua_getupvalue(L, i, 1)) {
		/* assume an ffi_type* */
		type = lua_touserdata(L, -1);
	}
	return type;
}

static struct cvar *makecvar_(lua_State *, ffi_type *, size_t);

int funccall(lua_State *L)
{
	/* (n) means the n-th upvalue */
	lua_CFunction fn; /* (1) */
	ffi_cif cif;
	ffi_abi ABI; /* (2) */
	ffi_raw *args;
	ffi_type *rtype; /* (3) */
	struct arglist *argl; /* (4) */
	ffi_type **atypes;
	ffi_arg rvalue;
	ffi_status status;
	unsigned int i, nargs, nfixed;

	nargs = lua_gettop(L);
	args = alloca(sizeof (ffi_raw *) * nargs);

	fn = lua_tocfunction(L, lua_upvalueindex(1));
	ABI = (ffi_abi) lua_tointeger(L, lua_upvalueindex(2));
	rtype = lua_touserdata(L, lua_upvalueindex(3));
	argl = lua_touserdata(L, lua_upvalueindex(4));
	if (argl) {
		nfixed = argl->nargs;
		atypes = argl->atypes;
		if (nargs < nfixed || (!argl->variadic && nargs != nfixed))
			return luaL_error(L, "need %d arguments, %d given",
				(int) nfixed, (int) nargs);
	} else {
		nfixed = nargs;
		atypes = alloca(sizeof (ffi_type *) * nargs);
	}

	for (i = 0; i < nargs; i++) {
		if (argl && i < nfixed)
			pusharg(L, i, args, atypes[i]);
		else
			atypes[i] = pusharg(L, i, args, 0);
	}

	if (nfixed == nargs)
		status = ffi_prep_cif(&cif, ABI, nargs, rtype, atypes);
	else
		status = ffi_prep_cif_var(&cif, ABI, nfixed, nargs, rtype, atypes);
	if (status != FFI_OK) {
		const char *status_name = 0;

		switch (status) {
			case FFI_OK: /* to silent compiler warning */
			case FFI_BAD_TYPEDEF: status_name = "FFI_BAD_TYPEDEF"; break;
			case FFI_BAD_ABI: status_name = "FFI_BAD_ABI"; break;
		}

		return luaL_error(L, "libffi error: %s", status_name);
	}
	fprintf(stderr, "FFI: cif->size = %u, sizeof args = %u\n",
		(unsigned) cif.bytes, (unsigned) sizeof (ffi_raw *) * nargs);
	/* FIXME large return values? */
	if (rtype->size <= sizeof rvalue) {
		ffi_raw_call(&cif, FFI_FN(fn), &rvalue, args);
		return cast_c_lua(L, &rvalue, rtype->type);
	} else {
		struct cvar *var = makecvar_(L, rtype, 0);
		ffi_raw_call(&cif, FFI_FN(fn), var, args);
	}
	return 1;
}

static
int c_sizeof(lua_State *L)
{
	ffi_type *type;
	size_t size, arraysize;
	struct cvar *var;

	if ((var = luaL_testudata(L, 1, "ffi_cvar")) != 0) {
		type = var->type;
		arraysize = var->arraysize;
	} else {
		type = get_ffi_type(L, 1);
		lua_getupvalue(L, 1, 2);
		arraysize = lua_tonumber(L, -1);
	}

	if (!type) {
		luaL_error(L, "cannot apply sizeof to %s",
				lua_typename(L, lua_type(L, 1)));
	}

	size = type->size;
	if (arraysize > 0)
		size *= arraysize;
	lua_pushinteger(L, size);
	return 1;
}

static
int restype(lua_State *L)
{
	ffi_type *type;

	if (lua_isnil(L, 2)) {
		lua_pushlightuserdata(L, &ffi_type_void);
	} else {
		size_t arraysize;
		/* assume 1 is the ffi_function */
		type = get_ffi_type(L, 2);
		luaL_argcheck(L, type, 2, "need an ffi_type");
		lua_getupvalue(L, 2, 2);
		arraysize = (size_t) lua_tointeger(L, -1);
		luaL_argcheck(L, arraysize == 0, 2,
			"cannot use array as result type");
		lua_pop(L, 1); /* array size */
	}
	lua_setupvalue(L, 1, 3); /* (3) rtype */
	return 0;
}

static
int argtype(lua_State *L)
{
	int nargs;
	struct arglist *argl;
	int i;

	luaL_checktype(L, 1, LUA_TFUNCTION);
	nargs = lua_gettop(L) - 1;
	argl = lua_newuserdata(L, sizeof *argl + sizeof (ffi_type *) * nargs);
	argl->nargs = nargs;
	argl->variadic = 0;
	for (i = 0; i < nargs; i++) {
		if (lua_isstring(L, i+2) && strcmp(lua_tostring(L, i+2), "...") == 0) {
			argl->nargs = i;
			argl->variadic = 1;
			break;
		}
		if (!(argl->atypes[i] = get_ffi_type(L, i+2)))
			return luaL_error(L, "need a ffi_type");
		lua_pop(L, 1); /* ffi_type* */
	}
	/* (4) arglist */
	if (!lua_setupvalue(L, 1, 4))
		return luaL_error(L, "bad FFI function");

	return 0;
}

static
struct cvar *makecvar_(lua_State *L, ffi_type *type, size_t arraysize)
{
	size_t memsize;
	struct cvar *var;

	memsize = type->size;
	if (arraysize > 0)
		memsize *= arraysize;
	var = lua_newuserdata(L, sizeof (struct cvar) + memsize);
	luaL_setmetatable(L, "ffi_cvar");
	var->type = type;
	var->arraysize = arraysize;
	return var;
}

static
int makecvar(lua_State *L)
{
	ffi_type *type; /* (1) */
	size_t arraysize; /* (2) */
	struct cvar *var;

	type = lua_touserdata(L, lua_upvalueindex(1));
	arraysize = lua_tointeger(L, lua_upvalueindex(2));
	var = makecvar_(L, type, arraysize);
	if (!arraysize && lua_gettop(L) >= 1) {
		cast_lua_c(L, 1, var->mem, type->type);
	} else {
		memset(var->mem, 0, type->size * arraysize);
	}
	return 1;
}

static
void *index2addr(lua_State *L, int *type)
{
	struct cvar *var;
	size_t i;
	int rc;

	var = luaL_checkudata(L, 1, "ffi_cvar");
	i = lua_tointegerx(L, 2, &rc);
	if (!rc || var->arraysize == 0) {
		luaL_error(L, "attempt to index a value of type %s",
			type_names[var->type->type]);
	}
	if (i >= var->arraysize) {
		luaL_error(L, "index out of range");
	}
	*type = var->type->type;
	return var->mem + i * var->type->size;
}

static
int c_index(lua_State *L)
{
	void *addr;
	int type;

	addr = index2addr(L, &type);
	cast_c_lua(L, addr, type);
	return 1;
}

static
int c_newindex(lua_State *L)
{
	void *addr;
	int type;

	addr = index2addr(L, &type);
	if (!cast_lua_c(L, 3, addr, type))
		return luaL_error(L, "cannot convert %s to %s",
			lua_typename(L, lua_type(L, 3)),
			type_names[type]);
	return 1;
}

static
int c_addr(lua_State *L)
{
	struct cvar *var;

	var = luaL_checkudata(L, 1, "ffi_cvar");
	lua_pushlightuserdata(L, var->mem);
	return 1;
}

static
int c_tostr(lua_State *L)
{
	struct cvar *var;
	int type;

	var = luaL_checkudata(L, 1, "ffi_cvar");
	type = var->type->type;
	if (var->arraysize > 0) {
		if (type == FFI_TYPE_UINT8 || type == FFI_TYPE_SINT8) {
			lua_pushlstring(L, var->mem, var->arraysize);
		} else {
			lua_pushfstring(L, "array: %p", var->mem);
		}
	} else {
		if (cast_c_lua(L, var->mem, type)) {
			lua_tostring(L, -1);
		} else {
			lua_pushfstring(L, "%s: %p", type_names[type], var->mem);
		}
	}
	return 1;
}

#if 0
static
int c_ptrstr(lua_State *L)
{
	struct cvar *var;
	int type;
	void *base;

	if (lua_islightuserdata(L, 1)) {
		base = lua_touserdata(L, 1);
	} else {
		var = luaL_checkudata(L, 1, "ffi_cvar");
		type = var->type->type;
		typename = type_names[type];
		if (var->arraysize > 0) {
			base = var->mem;
		} else if (type == FFI_TYPE_POINTER) {
			void **pp = (void *) var->mem;
			base = *pp;
		} else {
			return luaL_error(L, "expect a pointer or array");
		}
	}
	lua_pushstring(L, base);
	return 1;
}
#endif

static
int c_tonum(lua_State *L)
{
	struct cvar *var;
	int type;

	var = luaL_checkudata(L, 1, "ffi_cvar");
	type = var->type->type;
	if (TYPE_IS_INT(type) || TYPE_IS_FLOAT(type)) {
		cast_c_lua(L, var->mem, type); /* must success */
	} else if (type == FFI_TYPE_POINTER) {
		void **pp = (void *) var->mem;
		lua_pushinteger(L, (lua_Integer) *pp);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static
int array(lua_State *L)
{
	ffi_type *type;
	lua_Integer size;

	type = get_ffi_type(L, 1);
	luaL_argcheck(L, type, 1, "need an ffi_type");
	size = luaL_checkinteger(L, 2);
	luaL_argcheck(L, size > 0, 2, "non-positive size");
	lua_pushlightuserdata(L, type);
	lua_pushvalue(L, 2);
	lua_pushcclosure(L, makecvar, 2);
	return 1;
}

static
luaL_Reg ffilib[] = {
	{"addr", c_addr},
	{"argtype", argtype},
	{"array", array},
#if 0
	{"ptrstr", c_ptrstr},
#endif
	{"restype", restype},
	{"sizeof", c_sizeof},
	{"tonumber", c_tonum},
	{"openlib", 0}, /* placeholder */
	{0, 0}
};

static
void define_type(lua_State *L, const char *name, ffi_type *type)
{
	int table;

	table = lua_gettop(L);

	lua_pushlightuserdata(L, type);
	lua_pushinteger(L, 0); /* non-array */
	lua_pushcclosure(L, makecvar, 2);
	/* store as int32 etc. */
	lua_pushvalue(L, -1);
	lua_setfield(L, table, type_names[type->type]);
	/* store as given name (int etc.) */
	lua_setfield(L, table, name);
}

static
void define_types(lua_State *L)
{
	define_type(L, "uint", &ffi_type_uint);
	define_type(L, "int", &ffi_type_sint);
	define_type(L, "short", &ffi_type_sshort);
	define_type(L, "ushort", &ffi_type_ushort);
	define_type(L, "long", &ffi_type_slong);
	define_type(L, "ulong", &ffi_type_ulong);
#if CHAR_MAX == SCHAR_MAX
	define_type(L, "char", &ffi_type_schar);
	define_type(L, "uchar", &ffi_type_uchar);
#else
	define_type(L, "char", &ffi_type_uchar);
	define_type(L, "schar", &ffi_type_schar);
#endif

	/* assume long long is 64 bit */
	define_type(L, "longlong", &ffi_type_sint64);
	define_type(L, "ulonglong", &ffi_type_uint64);

	define_type(L, "float", &ffi_type_float);
	define_type(L, "double", &ffi_type_double);
	define_type(L, "longdouble", &ffi_type_longdouble);
	define_type(L, "pointer", &ffi_type_pointer);
}

LUAMOD_API
int luaopen_ffi(lua_State *L)
{
	int loadfunc;

	luaL_requiref(L, "package", luaopen_package, 0);
	lua_getfield(L, -1, "loadlib");
	if (!lua_isfunction(L, -1))
		return luaL_error(L, "cannot find package.loadlib");
	loadfunc = lua_gettop(L); /* package.loadlib */

	luaL_newmetatable(L, "ffi_library");
	lua_pushvalue(L, loadfunc);
	lua_pushcclosure(L, libindex, 1);
	lua_setfield(L, -2, "__index");
	lua_pushliteral(L, "v"); /* weak value table */
	lua_setfield(L, -2, "__mode");
	
	luaL_newmetatable(L, "ffi_cvar");
	lua_pushcfunction(L, c_tostr);
	lua_setfield(L, -2, "__tostring");
	lua_pushcfunction(L, c_index);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, c_newindex);
	lua_setfield(L, -2, "__newindex");

	luaL_newlib(L, ffilib);
	lua_pushvalue(L, loadfunc);
	lua_pushcclosure(L, openlib, 1);
	lua_setfield(L, -2, "openlib");
	define_types(L);

	return 1;
}
