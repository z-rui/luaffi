local print=print
local ffi = require('ffi')
local libc, div_t do
  local _ENV=ffi
  div_t = struct {
    sint, "quot",
    sint, "rem",
  }
  libc = loadlib('libc.so', {
    printf = cif {ret = sint; pointer},
    div = cif {ret = div_t; sint, sint},
    memcpy = cif {ret = pointer; pointer, pointer, size_t},
    strcat = cif {ret = pointer; pointer, pointer},
    strlen = cif {ret = size_t; pointer},
  })
end

for k, v in pairs(libc) do
  print(k, v)
end

do
  local _ENV=libc
  local buf = ffi.alloc(ffi.char, 128)
  print(buf)

  memcpy(buf, "hello, ", 8)
  strcat(buf, "world")
  printf("strlen(\"%s\") == %d\n", buf, strlen(buf))

  local r = div(7654321, 1234567)
  printf("%d %d\n", r.quot, r.rem)
end

-- vim: ts=2:sw=2:et
