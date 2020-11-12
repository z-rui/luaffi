local ffi = require('ffi')
local libm = {}
local real_funcs = { "sin", "cos", "tan", "sqrt", "exp", "log" }
local complex_funcs = { "csin", "ccos", "ctan", "csqrt", "cexp", "clog" }
do
  local cif = ffi.cif {ret = ffi.double; ffi.double}
  for _, f in ipairs(real_funcs) do
    libm[f] = cif
  end
  cif = ffi.cif {ret = ffi.complex_double; ffi.complex_double}
  for _, f in ipairs(complex_funcs) do
    libm[f] = cif
  end
  cif = ffi.cif {ret = ffi.double, ffi.complex_double}
  libm.creal = cif
  libm.cimag = cif
end

libm = ffi.loadlib('libm.so', libm)

for k, v in pairs(libm) do
  print(k, v)
end

---[[
local x = 2.0
for _, f in ipairs(real_funcs) do
  local y = libm[f](x)
  print(("%s(%g) = %g"):format(f, x, y))
end
--]]

local x = libm.csqrt(-2)
for _, f in ipairs(complex_funcs) do
  local y = libm[f](x)
  print(("%s(%g+%gI) = %g+%gI"):format(
    f, libm.creal(x), libm.cimag(x), libm.creal(y), libm.cimag(y)))
end

-- vim: ts=2:sw=2:et
