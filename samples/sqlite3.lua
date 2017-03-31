#!/bin/env lua

local ffi = require('ffi')
local libc = ffi.openlib('libc.so.6')
local sqlite3 = ffi.openlib('libsqlite3.so')

-- C functions can call Lua closure
local callback = ffi.closure(
  function (ud, n, s, t)
    local msg = ffi.deref(s, ffi.pointer)
    libc.puts(msg)
    return 0
  end,
  ffi.int,
  ffi.pointer, ffi.int, ffi.pointer, ffi.pointer)

do local _ENV = sqlite3
  local db = ffi.pointer()
  sqlite3_open(':memory:', ffi.addr(db))
  sqlite3_exec(db, "SELECT 'zfyyfx';", callback, nil, nil)
  sqlite3_close(db)
end
