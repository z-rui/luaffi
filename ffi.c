#define LUA_LIB

#include <assert.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <ffi.h>
#include <stdlib.h>
#include <string.h>

struct ctype {
	ffi_type *type;
	size_t arraysize;
};

static struct ctype voidtype = { &ffi_type_void, 0 };

/* The uservalue of a cfunc object can be:
 * 1) nil.  No information of return value and parameters
 *    was given.
 * 2) a ctype object.  It is the type of the return value.
 * 3) a Lua table t, where:
 *   - t[1] is the type of the return value;
 *   - t[2] is the type of the first argument;
 *   - t[n] is the type of the (n-1)-th argument.
 */
/* XXX: supporting variadic function calls turned out
 * to be not very helpful, due to casting difficulties.
 * This feature is removed for now;
 * rather, passing more arguments than specified is
 * supported, as a workaround.  Users are responsible
 * to get the casting right. */
struct cfunc {
	lua_CFunction fn;
	ffi_cif cif;
	/* we can borrow the abi, nargs field from cif. */
};

struct closure {
	lua_State *L;
	ffi_closure *cl;
	ffi_cif cif;
	void *addr;
	int ref; /* ref of lua cl */
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
int libindex(lua_State *L)
{
	lua_CFunction fn;
	struct cfunc *func;

	lua_pushvalue(L, lua_upvalueindex(1)); /* loadlib() */
	lua_pushliteral(L, "__FFI_LIBNAME");
	lua_rawget(L, 1);
	lua_pushvalue(L, 2); /* name */

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

	func = lua_newuserdata(L, sizeof (struct cfunc));
	func->fn = fn;
	func->cif.nargs = 0;
	func->cif.abi = FFI_DEFAULT_ABI;
	luaL_setmetatable(L, "ffi_cfunc");
	/* the uservalue is nil */

	/* cache the function in library table */
	lua_pushvalue(L, 2); /* name */
	lua_pushvalue(L, -2); /* func (i.e. the userdata) */
	lua_rawset(L, 1); /* lib[name] = func */

	return 1;
}

/* check ffi.h for correctness */
#define TYPE_IS_INT(T) ((1<<(T))&8160) /* 5--12 */
#define TYPE_IS_FLOAT(T) ((1<<(T))&28) /* 2, 3, 4 */

static const char *type_names[] = {
	"void", "int", "float", "double", "long double",
	"uint8", "int8", "uint16", "int16",
	"uint32", "int32", "uint64", "int64",
	"struct", "pointer", "complex",
};

static
struct ctype *c_typeof_(lua_State *L, int i, int pop)
{
	struct ctype *typ;

	lua_getuservalue(L, i);
	typ = luaL_checkudata(L, -1, "ffi_ctype");
	lua_pop(L, pop);
	return typ;
}

#include "casts.c"

static
ffi_type *type_info(lua_State *L, int i, size_t *arraysize)
{
	struct ctype *typ;

	typ = luaL_testudata(L, i, "ffi_ctype");
	luaL_argcheck(L, typ, i, "expect an FFI type");

	if (arraysize)
		*arraysize = typ->arraysize;
	else if (typ->arraysize > 0)
		luaL_argerror(L, i, "expect a non-array");

	return typ->type;
}

static
void populate_atypes(lua_State *L, ffi_type **atypes, int begin, int end)
{
	struct ctype *typ;
	int i;
	int ltype;

	for (i = begin; i < end; i++) {
		ltype = lua_type(L, i+2);
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
				atypes[i] = &ffi_type_pointer;
				if (luaL_testudata(L, i+2, "ffi_cvar")) {
					typ = c_typeof_(L, i+2, 1);
					if (typ && typ->arraysize == 0) {
						atypes[i] = typ->type;
					}
				}
				break;
				/* fallthrough */
			/* cannot be passed to C function */
			case LUA_TTABLE:
			case LUA_TTHREAD:
			default:
				lua_pushfstring(L, "cannot pass a lua %s to FFI function",
						lua_typename(L, ltype));
				luaL_argerror(L, i, lua_tostring(L, -1));
		}
	}
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

static struct cvar *makecvar_(lua_State *);
static unsigned f_getproto_(lua_State *L, ffi_type **rtype, ffi_type **arg_types, unsigned nargs);

/* TODO: can cache CIF */
int f_call(lua_State *L)
{
	struct cfunc *func;
	void **args;
	ffi_type *rtype = 0;
	ffi_type **atypes = 0;
	ffi_arg rvalue;
	ffi_status status;
	ffi_abi ABI;
	unsigned int i;
	unsigned nargs, nfixed;

	nargs = lua_gettop(L) - 1; /* exclude 'self' */
	func = luaL_checkudata(L, 1, "ffi_cfunc");
	ABI = func->cif.abi;
	atypes = alloca(sizeof (ffi_type *) * nargs);
	nfixed = f_getproto_(L, &rtype, atypes, nargs);

	if (nargs < nfixed) {
		return luaL_error(L, "expect %d arguments, %d given",
			nfixed, nargs);
	} else if (nargs > 0) {
		/* default types for additional arguments */
		populate_atypes(L, atypes, nfixed, nargs);
	}

	status = ffi_prep_cif(&func->cif, ABI, nargs, rtype, atypes);
	check_status(L, status);

	args = alloca(sizeof (void *) * nargs);
	for (i = 0; i < nargs; i++) {
		ffi_type *ftype = atypes[i];

		args[i] = alloca(sizeof (ftype->size));
		cast_lua_c(L, i+2, args[i], ftype->type);
	}

	if (rtype->size <= sizeof rvalue) {
		ffi_call(&func->cif, FFI_FN(func->fn), &rvalue, args);
		return cast_c_lua(L, &rvalue, rtype->type);
	} else {
		/* return typ is on stack top */
		void *var = makecvar_(L);
		ffi_call(&func->cif, FFI_FN(func->fn), var, args);
	}
	return 1;
}

/* |func, rtype, [atypes ...] -> func, proto */
static
void f_packproto_(lua_State *L)
{
	int nargs, i;

	nargs = lua_gettop(L) - 2;
	if (lua_isnil(L, 2)) {
		lua_pushlightuserdata(L, &voidtype);
		lua_replace(L, 2);
	} else {
		type_info(L, 2, 0); /* ensure not an array */
	}
	if (nargs > 0) {
		lua_createtable(L, nargs + 1, 0);
		lua_pushvalue(L, 2); /* return type */
		lua_rawseti(L, -2, 1);
		for (i = 0; i < nargs; i++) {
			lua_pushvalue(L, i+3);
			type_info(L, i+3, 0);    /* ensure not an array */
			lua_rawseti(L, -2, i+2);
		}
		lua_replace(L, 2);
	}
	lua_settop(L, 2);
}

static
int f_types(lua_State *L)
{
	luaL_checkudata(L, 1, "ffi_cfunc");
	f_packproto_(L);
	lua_setuservalue(L, 1);
	return 1;
}

/* |func ... -> |func ... rtype */
static
unsigned f_getproto_(lua_State *L, ffi_type **rtype, ffi_type **atypes, unsigned nargs)
{
	struct ctype *typ;
	unsigned i, nparams = 0; 

	switch (lua_getuservalue(L, 1)) {
		case LUA_TNIL:
			*rtype = &ffi_type_sint;
			break;
		case LUA_TLIGHTUSERDATA:
		case LUA_TUSERDATA:
			typ = lua_touserdata(L, -1);
			*rtype = typ->type;
			break;
		case LUA_TTABLE:
			nparams = (unsigned) (lua_rawlen(L, -1) - 1);
			if (nparams < nargs)
				nargs = nparams; /* nargs = min(nargs, nparams) */
			for (i = 0; i < nargs; i++) {
				lua_rawgeti(L, -1, i+2);
				typ = lua_touserdata(L, -1);
				lua_pop(L, 1); /* typ */
				if (!typ) break;
				atypes[i] = typ->type;
			}
			lua_rawgeti(L, -1, 1);
			typ = lua_touserdata(L, -1);
			*rtype = typ->type;
			lua_remove(L, -2); /* remove table */
			break;
		default:
			luaL_error(L, "FFI internal error: "
				"invalid uservalue of a ffi_cfunc object");
	}
	return nparams;
}

static
struct cvar *makecvar_(lua_State *L)
{
	size_t memsize;
	struct cvar *var;
	struct ctype *typ;

	/* type object is at stack top */
	typ = luaL_checkudata(L, -1, "ffi_ctype");

	memsize = typ->type->size;
	if (typ->arraysize > 0)
		memsize *= typ->arraysize;
	var = lua_newuserdata(L, memsize);
	luaL_setmetatable(L, "ffi_cvar");
	lua_insert(L, -2);
	lua_setuservalue(L, -2); /* var.uservalue = type */
	/* the type object is gone;
	 * stack top is now `var'. */
	return var;
}

static
int makecvar(lua_State *L)
{
	void *var;
	struct ctype *typ;
	int top;

	top = lua_gettop(L);
	typ = luaL_checkudata(L, 1, "ffi_ctype");
	lua_pushvalue(L, 1);
	var = makecvar_(L);

	if (top >= 2) {
		cast_lua_c(L, 2, var, typ->type->type);
	} else {
		memset(var, 0, typ->type->size * typ->arraysize);
	}
	return 1;
}

static
void *index2addr(lua_State *L, int *type)
{
	void *var;
	struct ctype *typ;
	size_t i;
	int rc;

	var = luaL_checkudata(L, 1, "ffi_cvar");
	typ = c_typeof_(L, 1, 1);
	i = lua_tointegerx(L, 2, &rc);
	if (!rc || typ->arraysize == 0) {
		const char *tname = type_names[typ->type->type];
		luaL_error(L, "attempt to index %s %s value",
			(tname[0] == 'i' || tname[0] == 'u') ? "an" : "a",
			tname);
	}
	if (i >= typ->arraysize) {
		luaL_error(L, "index out of range");
	}
	*type = typ->type->type;
	return (char *) var + i * typ->type->size;
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
	cast_lua_c(L, 3, addr, type);
	return 1;
}

static
int c_sizeof(lua_State *L)
{
	ffi_type *type;
	size_t size, arraysize;
	struct ctype *typ;

	if (luaL_testudata(L, 1, "ffi_cvar")) {
		typ = c_typeof_(L, 1, 1);
		type = typ->type;
		arraysize = typ->arraysize;
	} else {
		type = type_info(L, 1, &arraysize);
	}

	size = type->size;
	if (arraysize > 0)
		size *= arraysize;

	lua_pushinteger(L, size);
	return 1;
}

static
int c_addr(lua_State *L)
{
	void *var;

	var = luaL_checkudata(L, 1, "ffi_cvar");
	lua_pushlightuserdata(L, var);
	return 1;
}

static
int c_tostr(lua_State *L)
{
	void *var;
	struct ctype *typ;
	int type;

	var = luaL_checkudata(L, 1, "ffi_cvar");
	typ = c_typeof_(L, 1, 1);
	type = typ->type->type;
	if (typ->arraysize > 0) {
		if (typ->type->size == 1) { /* "string" */
			lua_pushlstring(L, (char *) var, typ->arraysize);
		} else {
			lua_pushfstring(L, "array of %s: %p",
				type_names[type], var);
		}
	} else {
		/* currently cast_c_lua never fails... */
		cast_c_lua(L, var, type);
		lua_tostring(L, -1);
	}
	return 1;
}

static
int f_tostr(lua_State *L)
{
	struct cfunc *func;

	func = luaL_checkudata(L, 1, "ffi_cfunc");
	lua_pushfstring(L, "C function: %p", func->fn);
	return 1;
}

static
int c_ptrderef(lua_State *L)
{
	void *ptr;
	ffi_type *type;
	size_t arraysize;
	size_t offset;
	size_t elemsize;

	if (!cast_lua_pointer(L, 1, &ptr))
		return luaL_argerror(L, 1, "expect a pointer");
	type = type_info(L, 2, &arraysize);
	offset = (size_t) luaL_optinteger(L, 3, 0);
	if (lua_isnoneornil(L, 4))
		elemsize = type->size;
	else
		elemsize = (size_t) luaL_checkinteger(L, 4);
	ptr = (char *) ptr + offset * elemsize;
	if (arraysize) {
		lua_pushvalue(L, 2); /* type */
		void *var = makecvar_(L);
		memcpy(var, ptr, type->size * arraysize);
		return 1;
	}
	return cast_c_lua(L, ptr, type->type);
}

static
int c_tonum(lua_State *L)
{
	void *var;
	struct ctype *typ;
	int type;

	var = luaL_checkudata(L, 1, "ffi_cvar");
	typ = c_typeof_(L, 1, 1);
	type = typ->type->type;
	if (TYPE_IS_INT(type) || TYPE_IS_FLOAT(type)) {
		cast_c_lua(L, var, type); /* must success */
	} else if (type == FFI_TYPE_POINTER) {
		/* for wizards who treat pointers as integers. */
		lua_pushinteger(L, (lua_Integer) var);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

/* Here's how I deal with types:
 *
 * A type is stored in a ctype struct.
 * It is set to be the uservalue of any
 * cvar object, so that it will not
 * get collected with alive instances of
 * this type.
 *
 * The `type' member in the struct either points
 * to a predefined static variable from libFFI,
 * or to a userdata that is allocated by Lua.
 * If it is a userdata, then that userdata
 * is set to be the uservalue of this object,
 * so the memory will not get collected.
 *
 * Rule of thumb: no memory is owned by this struct
 * and thus requires a finalizer. Any memory it referenced
 * to is either static or managed automatically
 * by Lua.
 *
 * If it is a compound type (TODO: not implemented yet),
 * then `type' must be a userdata (to be reconsidered
 * when I start implement compound types), and
 * it userdata will hold an uservalue of a Lua table,
 * referencing all relevant types.
 */

static
struct ctype *makectype_(lua_State *L, ffi_type *type, size_t arraysize)
{
	struct ctype *typ;

	typ = lua_newuserdata(L, sizeof (struct ctype));
	typ->type = type;
	typ->arraysize = arraysize; /* we are defining basic types */
	luaL_setmetatable(L, "ffi_ctype");
	return typ;
}

static
int makearraytype(lua_State *L)
{
	struct ctype *elemtyp, *typ;
	lua_Integer arraysize;

	elemtyp = luaL_checkudata(L, 1, "ffi_ctype");
	if (elemtyp->arraysize > 0) {
		return luaL_error(L, "nested array is not supported");
	}
	/* use signed integers fow now,
	 * as this prevents silly sizes such as -1.
	 */
	arraysize = luaL_checkinteger(L, 2);
	luaL_argcheck(L, arraysize > 0, 2, "expect a positive size");

	typ = makectype_(L, elemtyp->type, arraysize);
	lua_getuservalue(L, 1);
	lua_setuservalue(L, -2); /* also references that uservalue */
	typ->arraysize = (size_t) arraysize; /* only arraysize differs */

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
void cl_proxy(ffi_cif *cif, void *ret, void *args[], void *ud)
{
	struct closure *cl = (struct closure *) ud;
	lua_State *L = cl->L;
	unsigned nargs = cif->nargs;
	unsigned i;
	int rtype;

	if (!lua_checkstack(L, nargs + 1))
		luaL_error(L, "C stack overflow"); /* sorry... */
	lua_rawgeti(L, LUA_REGISTRYINDEX, cl->ref);
	luaL_checktype(L, -1, LUA_TFUNCTION);
	/* push arguments */
	for (i = 0; i < nargs; i++) {
		cast_c_lua(L, args[i], cif->arg_types[i]->type);
	}
	lua_call(L, nargs, 1);
	rtype = cif->rtype->type;
	if (rtype != FFI_TYPE_VOID) {
		/* XXX is it OK to raise if conversion failed? */
		cast_lua_c(L, -1, ret, rtype);
	}
	lua_pop(L, 1); /* pop return value from lua stack */
}

static
int cl_tostr(lua_State *L)
{
	struct closure *cl;

	cl = luaL_checkudata(L, 1, "ffi_closure");
	lua_pushfstring(L, "FFI closure: %p", cl->addr);
	return 1;
}

static
int cl_gc(lua_State *L)
{
	struct closure *cl;

	cl = luaL_checkudata(L, 1, "ffi_closure");
	if (cl->cl) {
		ffi_closure_free(cl->cl);
		cl->cl = 0;
	}
	luaL_unref(cl->L, LUA_REGISTRYINDEX, cl->ref);
	cl->ref = LUA_NOREF;
	return 0;
}

static
int makeclosure(lua_State *L)
{
	struct closure *cl;
	int nargs;
	ffi_type **atypes = 0;
	ffi_type *rtype;
	ffi_status status;

	luaL_checktype(L, 1, LUA_TFUNCTION);
	nargs = lua_gettop(L) - 2;

	cl = lua_newuserdata(L, sizeof (struct closure));
	cl->L = L;
	cl->cl = 0;
	cl->addr = 0;
	cl->ref = LUA_NOREF;
	luaL_setmetatable(L, "ffi_closure");

	lua_pushvalue(L, 1); /* the Lua function */
	cl->ref = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_replace(L, 1);

	f_packproto_(L); /* func, rtype, atypes... */

	if (nargs > 0) {
		atypes = lua_newuserdata(L, sizeof (ffi_type *) * nargs);
		lua_rawseti(L, -2, 0); /* just to keep a reference */
	}
	lua_setuservalue(L, 1);

	f_getproto_(L, &rtype, atypes, nargs);
	lua_pop(L, 1); /* rtype */

	status = ffi_prep_cif(&cl->cif, FFI_DEFAULT_ABI, nargs, rtype, atypes);
	check_status(L, status);
	cl->cl = ffi_closure_alloc(sizeof (ffi_closure), &cl->addr);
	if (!cl->cl)
		return luaL_error(L, "allocating closure failed");
	status = ffi_prep_closure_loc(cl->cl, &cl->cif, cl_proxy, cl, cl->addr);
	check_status(L, status);
	return 1;
}

static
luaL_Reg ffilib[] = {
	{"addr", c_addr},
	{"types", f_types},
	{"array", makearraytype},
#if 0
	{"ptrstr", c_ptrstr},
#endif
	{"deref", c_ptrderef},
	{"closure", makeclosure},
	{"sizeof", c_sizeof},
	{"tonumber", c_tonum},
	{"openlib", 0}, /* placeholder */
	{0, 0}
};

struct type_reg {
	char *name;
	ffi_type *type;
};
static
struct type_reg predefined_types[] = {
	{"uint", &ffi_type_uint},
	{"int", &ffi_type_sint},
	{"short", &ffi_type_sshort},
	{"ushort", &ffi_type_ushort},
	{"long", &ffi_type_slong},
	{"ulong", &ffi_type_ulong},
#if CHAR_MAX == SCHAR_MAX
	{"char", &ffi_type_schar},
	{"uchar", &ffi_type_uchar},
#else
	{"char", &ffi_type_uchar},
	{"schar", &ffi_type_schar},
#endif
	/* assume long long is 64 bit */
	{"longlong", &ffi_type_sint64},
	{"ulonglong", &ffi_type_uint64},

	{"float", &ffi_type_float},
	{"double", &ffi_type_double},
	{"longdouble", &ffi_type_longdouble},

	{"pointer", &ffi_type_pointer},
	{0, 0}
};

static
void define_types(lua_State *L)
{
	struct type_reg *p;
	int table;

	table = lua_gettop(L);
	for (p = predefined_types; p->name; p++) {
		makectype_(L, p->type, 0); /* 0: non-array */
		/* name with specific width, e.g., int32. */
		lua_pushvalue(L, -1);
		lua_setfield(L, table, type_names[p->type->type]);
		/* the given name, e.g., int. */
		lua_setfield(L, table, p->name);
	}
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

	luaL_newmetatable(L, "ffi_ctype");
	lua_pushcfunction(L, makecvar);
	lua_setfield(L, -2, "__call");
	
	luaL_newmetatable(L, "ffi_cvar");
	lua_pushcfunction(L, c_tostr);
	lua_setfield(L, -2, "__tostring");
	lua_pushcfunction(L, c_index);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, c_newindex);
	lua_setfield(L, -2, "__newindex");

	luaL_newmetatable(L, "ffi_cfunc");
	lua_pushcfunction(L, f_tostr);
	lua_setfield(L, -2, "__tostring");
	lua_pushcfunction(L, f_call);
	lua_setfield(L, -2, "__call");

	luaL_newmetatable(L, "ffi_closure");
	lua_pushcfunction(L, cl_tostr);
	lua_setfield(L, -2, "__tostring");
	lua_pushcfunction(L, cl_gc);
	lua_setfield(L, -2, "__gc");

	luaL_newlib(L, ffilib);
	lua_pushvalue(L, loadfunc);
	lua_pushcclosure(L, openlib, 1);
	lua_setfield(L, -2, "openlib");
	define_types(L);

	return 1;
}
