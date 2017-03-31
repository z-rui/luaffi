#!/bin/env lua

local ffi = require('ffi')
local gtk = ffi.openlib('libgtk-x11-2.0.so')
local gobject = ffi.openlib('libgobject-2.0.so')

local C do
local ipairs = ipairs
local type = type
C = setmetatable({gtk, gobject}, {
  __mode = "v", -- weak value
  __index = function(o, k)
    if type(k) ~= 'string' then return end
    for _, lib in ipairs(o) do
      local f = lib[k]
      if f then
        o[k] = f -- cache
        return f
      end
    end
  end
})
end

local x, y = {}, {}
for i, j in ipairs{1, 3, 5, 2, 4} do
  local t = 0.4*math.pi * (j-1) + 0.5*math.pi
  x[i] = 100 * (math.cos(t) + 1)
  y[i] = 100 * (1 - math.sin(t))
end

do
  local p, i, d = ffi.pointer, ffi.int, ffi.double
  local cif = ffi.cif
  cif(gtk.cairo_move_to, nil, p, d, d)
  cif(gtk.cairo_line_to, nil, p, d, d)
  cif(gtk.cairo_set_source_rgb, nil, p, d, d, d)
  cif(gtk.cairo_set_font_size, nil, p, d)
  cif(gtk.gdk_cairo_create, p, p)
  cif(gtk.gtk_window_new, p, i)
end

do local _ENV=C
  GTK_MESSAGE_WARNING = 1
  GTK_BUTTONS_OK = 1
  GTK_WINDOW_TOPLEVEL = 0

  gtk_init(nil, nil)
  local window = gtk_window_new(GTK_WINDOW_TOPLEVEL)
  gtk_window_set_title(window, "libFFI")
  g_signal_connect_data(window, "destroy", gtk_main_quit, nil, nil, 0)
  local darea = gtk_drawing_area_new()
  gtk_widget_set_size_request(darea, 200, 200)
  local redraw = ffi.closure(function(widget, event, ud)
    -- offsetof(GtkWidget, window) = 80 (on my system)
    -- TODO need a better way to access struct member
    local window = ffi.deref(widget, ffi.pointer, 80, 1)
    local cr = gdk_cairo_create(window)
    cairo_set_font_size(cr, 14)
    cairo_move_to(cr, 0, 20)
    cairo_show_text(cr, "Star of Mr. Zfy")
    cairo_set_source_rgb(cr, 1, 0, 0)
    cairo_move_to(cr, x[1], y[1])
    for i = 2, 5 do
      cairo_line_to(cr, x[i], y[i])
    end
    cairo_close_path(cr)
    cairo_stroke(cr)
    cairo_destroy(cr)
  end, nil, ffi.pointer, ffi.pointer, ffi.pointer)
  g_signal_connect_data(darea, "expose_event", redraw, nil, nil, 0)
  gtk_container_add(window, darea)
  gtk_widget_show(darea)
  gtk_widget_show(window)
  gtk_main()
end

-- vim: ts=2:sw=2:et
