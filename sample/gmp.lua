local ffi = require('ffi')

local gmp do
  local _ENV=ffi
  gmp = loadlib('libgmp.so.10', {
    mpz = struct {
      sint,
      sint,
      pointer, --mp_limb_t *
    },
    __gmpz_init = cif { ffi.pointer },
    __gmpz_clears = cif { ffi.pointer, },
    __gmpz_init_set_si = cif { ffi.pointer, ffi.slong },
    __gmpz_add = cif { ffi.pointer, ffi.pointer, ffi.pointer },
    __gmp_printf = cif { ret = ffi.sint, ffi.pointer, },
  })
end

do
  local _ENV=gmp
  local x, y = ffi.alloc(mpz), ffi.alloc(mpz)
  __gmpz_init_set_si(x, 0)
  __gmpz_init_set_si(y, 1)

  for i = 0, 100 do
    __gmp_printf("f[%u] = %Zu\n", i, x)
    __gmpz_add(x, x, y)
    x, y = y, x
  end
  __gmpz_clears(x, y, nil)
end

-- vim:ts=2:sw=2:et
