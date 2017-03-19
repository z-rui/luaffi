#define LUA_LIB

#include <assert.h>
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
#define TYPE_IS_INT(T) ((1<<(T))&8160) /* 5--12 */
#define TYPE_IS_FLOAT(T) ((1<<(T))&28) /* 2, 3, 4 */

static const char *type_names[] = {
	"void", "int", "float", "double", "long double",
	"uint8", "int8", "uint16", "int16",
	"uint32", "int32", "uint64", "int64",
	"struct", "pointer", "complex",
};

#include "casts.c"

static
ffi_raw *pusharg(lua_State *L, int i, ffi_raw *arg, ffi_type *ftype)
{
	int ltype;
	const char *ltypename;
	struct cvar *var;
	void *fn;

	ltype = lua_type(L, i);
	ltypename = lua_typename(L, ltype);
#define CHECK(T1, T2) if (T1 != T2) goto convert_fail;
	switch (ltype) {
		case LUA_TNIL: /* as NULL pointer */
			CHECK(ftype, &ffi_type_pointer);
			arg->ptr = NULL;
			break;
		case LUA_TBOOLEAN:
		case LUA_TNUMBER:
			if (!cast_lua_c(L, i, arg->data, ftype->type))
				goto convert_fail;
			break;
		case LUA_TSTRING: /* as const char * */
			CHECK(ftype, &ffi_type_pointer);
			arg->ptr = (void *) lua_tostring(L, i);
			break;
		case LUA_TFUNCTION: /* as pointer */
			CHECK(ftype, &ffi_type_pointer);
			fn = (void *) lua_tocfunction(L, i);
			if (!fn || lua_getupvalue(L, i, 1))
				goto convert_fail;
			arg->ptr = fn;
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
				/* XXX: C -> C cast is not supported */
				if (ftype == var->type) {
					if (ftype->type == FFI_TYPE_STRUCT) {
						/* TODO struct is not supported yet */
						/* the raw API expects an pointer to
						 * the struct */
						arg->ptr = var->mem;
						return arg + 1;
					}
					memcpy(arg->data, var->mem, ftype->size);
				} else {
					ltypename = type_names[var->type->type];
					goto convert_fail;
				}
			}
			break;
#undef CHECK
		/* these cannot be converted */
		case LUA_TTABLE:
		case LUA_TTHREAD:
		default:
convert_fail:
			lua_pushfstring(L, "expected %s, got %s\n",
					type_names[ftype->type],
					ltypename);
			luaL_argerror(L, i, lua_tostring(L, -1));
	}
	{
		size_t size = ftype->size;
		arg++;
		while (size > sizeof (ffi_raw)) {
			arg++;
			size -= sizeof (ffi_raw);
		}
	}
	return arg;
}

static
ffi_type *type_info(lua_State *L, int i, size_t *arraysize)
{
	int rc;

	if (!lua_getupvalue(L, i, 2))
		return 0;
	*arraysize = (size_t) lua_tonumberx(L, -1, &rc);
	lua_pop(L, 1); /* pop arraysize */
	if (!rc) return 0;
	if (lua_getupvalue(L, i, 1)) {
		/* assume an ffi_type* */
		/* cannot pop from stack as this
		 * may not be a lightuserdata */
		return lua_touserdata(L, -1);
	}
	return 0;
}

static
size_t populate_atypes(lua_State *L, ffi_type **atypes, int begin, int end)
{
	struct cvar *var;
	int i;
	int ltype;
	size_t size = 0, size1;

	for (i = 0; i < end; i++) {
		if (i < begin) goto inc_size; /* type is given */
		ltype = lua_type(L, i+1);
		switch (ltype) {
			case LUA_TNIL: /* as NULL pointer */
			case LUA_TSTRING: /* as const char * */
			case LUA_TLIGHTUSERDATA:
			case LUA_TFUNCTION: /* as pointer */
				atypes[i] = &ffi_type_pointer;
				break;
			case LUA_TBOOLEAN:
			case LUA_TNUMBER: /* as int */
				atypes[i] = &ffi_type_sint;
				break;
			case LUA_TUSERDATA:
				var = luaL_testudata(L, i+1, "ffi_cvar");
				if (var) {
					atypes[i] =  (var->arraysize == 0)
						? var->type
						: &ffi_type_pointer;
					break;
				}
				/* fallthrough */
			/* cannot be passed to C function */
			case LUA_TTABLE:
			case LUA_TTHREAD:
			default:
				lua_pushfstring(L, "cannot pass a lua %s to FFI function",
						lua_typename(L, ltype));
				luaL_argerror(L, i, lua_tostring(L, -1));
		}
inc_size:
		size1 = atypes[i]->size;
		size++;
		while (size1 > sizeof (ffi_raw)) {
			size++; size1 -= sizeof (ffi_raw);
		}
	}
	return size;
}

static
void check_status(lua_State *L, int status)
{
	if (status != FFI_OK) {
		const char *status_name = 0;

		switch (status) {
			case FFI_OK: /* to silent compiler warning */
			case FFI_BAD_TYPEDEF: status_name = "FFI_BAD_TYPEDEF"; break;
			case FFI_BAD_ABI: status_name = "FFI_BAD_ABI"; break;
		}

		luaL_error(L, "libffi error: %s", status_name);
	}
}

static struct cvar *makecvar_(lua_State *, ffi_type *, size_t);

int funccall(lua_State *L)
{
	/* (n) means the n-th upvalue */
	lua_CFunction fn; /* (1) */
	ffi_cif cif;
	ffi_abi ABI; /* (2) */
	ffi_raw *args, *parg;
	size_t nraw;
	ffi_type *rtype; /* (3) */
	struct arglist *argl; /* (4) */
	ffi_type **atypes;
	ffi_arg rvalue;
	ffi_status status;
	unsigned int i, nargs, nfixed;

	nargs = lua_gettop(L);

	fn = lua_tocfunction(L, lua_upvalueindex(1));
	ABI = (ffi_abi) lua_tointeger(L, lua_upvalueindex(2));
	rtype = lua_touserdata(L, lua_upvalueindex(3));
	argl = lua_touserdata(L, lua_upvalueindex(4));
	if (argl) {
		nfixed = argl->nargs;
		if (!argl->variadic) {
			if (nargs != nfixed) {
arg_error:
				return luaL_error(L, "need %d arguments, %d given",
					(int) nfixed, (int) nargs);
			}
			atypes = argl->atypes;
		} else {
			if (nargs < nfixed)
				goto arg_error;
			atypes = alloca(sizeof (ffi_type *) * nargs);
			memcpy(atypes, argl->atypes, sizeof (ffi_type *) * nfixed);
			nraw = populate_atypes(L, atypes, nfixed, nargs);
		}
	} else { /* no argument info is given */
		nfixed = nargs;
		atypes = alloca(sizeof (ffi_type *) * nargs);
		nraw = populate_atypes(L, atypes, 0, nargs);
	}

	if (nfixed == nargs)
		status = ffi_prep_cif(&cif, ABI, nargs, rtype, atypes);
	else
		status = ffi_prep_cif_var(&cif, ABI, nfixed, nargs, rtype, atypes);

	check_status(L, status);

	fprintf(stderr, "FFI: nraw = %u, nargs = %u\n", (unsigned) nraw, (unsigned) nargs);
	assert(nraw >= nargs);
	parg = args = alloca(nraw * sizeof (ffi_raw));
	for (i = 0; i < nargs; i++) {
		parg = pusharg(L, i+1, parg, atypes[i]);
	}

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
		type = type_info(L, 1, &arraysize);
	}

	luaL_argcheck(L, type, 1, "expect type or FFI object");

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
		type = type_info(L, 2, &arraysize);
		luaL_argcheck(L, type && arraysize == 0, 2,
			"expect a non-array FFI type");
	}
	lua_setupvalue(L, 1, 3); /* (3) rtype */
	return 0;
}

static
int argtype(lua_State *L)
{
	int nargs;
	struct arglist *argl;
	ffi_type *type;
	size_t arraysize;
	int i;

	luaL_checktype(L, 1, LUA_TFUNCTION);
	nargs = lua_gettop(L) - 1;
	argl = lua_newuserdata(L, sizeof *argl + sizeof (ffi_type *) * nargs);
	argl->nargs = nargs;
	argl->variadic = 0;
	/* XXX limitation of this implementation:
	 * type has to be lightuserdata,
	 * otherwise it might get collected.
	 * need to change to a lua table to support
	 * user-defined types. */
	for (i = 0; i < nargs; i++) {
		if (lua_isstring(L, i+2) && strcmp(lua_tostring(L, i+2), "...") == 0) {
			argl->nargs = i;
			argl->variadic = 1;
			break;
		}
		type = type_info(L, i+2, &arraysize);
		luaL_argcheck(L, type && arraysize == 0, 2,
			"expect a non-array FFI type");
		argl->atypes[i] = type; /* XXX not referencing */
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
	size_t arraysize;

	type = type_info(L, 1, &arraysize);
	luaL_argcheck(L, type && arraysize == 0, 1,
			"expect a non-array FFI type");
	size = luaL_checkinteger(L, 2);
	luaL_argcheck(L, size > 0, 2, "expect a positive size");
	/* stack top is ffi_type* (pushed by type_info) */
	lua_pushvalue(L, 2); /* arraysize */
	lua_pushcclosure(L, makecvar, 2);
	return 1;
}

#if 0
static
int struct_t(lua_State *L)
{
	int table = 1;

	luaL_checktype(L, table, LUA_TTABLE);
}
#endif

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
