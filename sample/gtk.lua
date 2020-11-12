#!/bin/env lua

local ffi = require("ffi")

local gtk do
  local _ENV=ffi
  gtk = loadlib("libgtk-3.so", {
    GTK_BUTTONS_CLOSE = 2,
    GTK_MESSAGE_INFO = 0,

    gtk_init = cif { pointer, pointer },
    gtk_message_dialog_new =
      cif { ret = pointer; pointer, sint, sint, sint, pointer },
    g_object_set = cif { pointer, pointer },
    gtk_dialog_run = cif { pointer },
    gtk_widget_destroy = cif { pointer },
  })
end

do
  local _ENV=gtk
  gtk_init(nil, nil)
  local dlg = gtk_message_dialog_new(
    nil,  --parent
    0, --flags
    GTK_MESSAGE_INFO,
    GTK_BUTTONS_CLOSE,
    "Hello, world!")

  g_object_set(dlg, "secondary-text", "你好，世界！", nil)
  gtk_dialog_run(dlg)
  gtk_widget_destroy(dlg)
end

-- vim: ts=2:sw=2:et
