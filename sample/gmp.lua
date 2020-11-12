local ffi = require('ffi')

local _ENV = setmetatable({}, {__index=ffi}) 

mpz = struct {
  sint,
  sint,
  pointer, --mp_limb_t *
}
__gmpz_init = cif { ffi.pointer }
__gmpz_clears = cif { ffi.pointer, }
__gmpz_init_set_si = cif { ffi.pointer, ffi.slong }
__gmpz_add = cif { ffi.pointer, ffi.pointer, ffi.pointer }
__gmp_printf = cif { ret = ffi.sint, ffi.pointer, }
_ENV = loadlib('libgmp.so.10', _ENV)

local x, y = ffi.alloc(mpz), ffi.alloc(mpz)
__gmpz_init_set_si(x, 0)
__gmpz_init_set_si(y, 1)

for i = 0, 100 do
  __gmp_printf("f[%u] = %Zu\n", i, x)
  __gmpz_add(x, x, y)
  x, y = y, x
end
__gmpz_clears(x, y, nil)

-- vim:ts=2:sw=2:et
