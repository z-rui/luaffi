#!/bin/env lua

local ffi = require("ffi")

local libc = ffi.openlib('libc.so.6')

-- without configuring cif
libc.printf("%d %g %s %p\n", 1, 3.14, "works like a charm!", nil)

ffi.cif(libc.malloc, ffi.pointer, ffi.uint64)
ffi.cif(libc.free, nil, ffi.pointer)
ffi.cif(libc.strcpy, ffi.pointer, ffi.pointer, ffi.pointer)
ffi.cif(libc.strcat, ffi.pointer, ffi.pointer, ffi.pointer)
ffi.cif(libc.puts, ffi.int, ffi.pointer)

-- array
local foo = ffi.array(ffi.char, 7)()
libc.strcpy(foo, "foo")
libc.strcat(foo, "bar")
libc.puts(foo)
libc.strcpy(foo, "bar")
libc.puts(foo)
libc.puts("Yeah!")
foo = nil -- memory managed by Lua; no need to free!

-- structs
local div_t = ffi.struct{ ffi.int, ffi.int }
ffi.cif(libc.div, div_t, ffi.int, ffi.int)
x = libc.div(7654321, 1234567)
print(x[0], x[1])
