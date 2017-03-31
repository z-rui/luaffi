#!/bin/env lua

local ffi = require("ffi")

local gtk = ffi.openlib("/usr/lib/libgtk-x11-2.0.so.0")

ffi.cif(gtk.gtk_init, nil, ffi.pointer, ffi.pointer)
ffi.cif(gtk.gtk_message_dialog_new, ffi.pointer, ffi.pointer, ffi.int, ffi.int, ffi.int, ffi.pointer)
ffi.cif(gtk.gtk_dialog_run, ffi.int, ffi.pointer)
ffi.cif(gtk.gtk_widget_destroy, nil, ffi.pointer)

gtk.gtk_init(nil,nil)
local dlg = gtk.gtk_message_dialog_new(nil, 0, 0, 1, "libFFI Rocks!")
gtk.gtk_dialog_run(dlg)
gtk.gtk_widget_destroy(dlg)
