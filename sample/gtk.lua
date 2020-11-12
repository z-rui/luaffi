#!/bin/env lua

local ffi = require("ffi")

local _ENV=setmetatable({}, {__index=ffi})
GTK_BUTTONS_CLOSE = 2
GTK_MESSAGE_INFO = 0

gtk_init = cif { pointer, pointer }
gtk_message_dialog_new =
  cif { ret = pointer; pointer, sint, sint, sint, pointer }
g_object_set = cif { pointer, pointer }
gtk_dialog_run = cif { pointer }
gtk_widget_destroy = cif { pointer }
_ENV = loadlib("libgtk-3.so", _ENV)

gtk_init(nil, nil)
local dlg = gtk_message_dialog_new(
  nil, 0, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "Hello, world!")
g_object_set(dlg, "secondary-text", "你好，世界！", nil)
gtk_dialog_run(dlg)
gtk_widget_destroy(dlg)

-- vim: ts=2:sw=2:et
