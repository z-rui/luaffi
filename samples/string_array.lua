#!/bin/env lua

local ffi = require("ffi")

local glib = ffi.openlib("/usr/lib/libglib-2.0.so.0")

ffi.cif(glib.g_strsplit, ffi.pointer)
ffi.cif(glib.g_strjoinv, ffi.pointer)
ffi.cif(glib.g_strv_length, ffi.int)
ffi.cif(glib.g_strfreev, nil)
ffi.cif(glib.g_free, nil)
ffi.cif(glib.g_print, nil)

local delim = "\n"
local strings = {"foo", "bar", "baz"}
local lines = table.concat(strings, delim)

do local _ENV = glib
  local strv = g_strsplit(lines, delim, 0)
  g_print("length: %d\n\n", g_strv_length(strv))
  for i = 1, #strings do
    local str = ffi.deref(strv, ffi.pointer, i-1) -- C index is 0-based
    g_print("%d. %s\n", i, str)
  end
  
  local result = g_strjoinv(delim,strv)
  g_print("\n%s\n", result)
  g_free(result)
  g_strfreev(strv)
end
