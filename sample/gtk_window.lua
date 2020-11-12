#!/bin/env lua

local ffi = require('ffi')
local gtk do
  local _ENV=ffi
  gtk = ffi.loadlib('libgtk-3.so', {
    -- Constants
    GTK_WINDOW_TOPLEVEL = 0,
    -- Functions
    cairo_close_path = cif {pointer},
    cairo_line_to = cif {pointer, double, double},
    cairo_move_to = cif {pointer, double, double},
    cairo_set_font_size = cif {pointer, double},
    cairo_set_source_rgb = cif {pointer, double, double, double},
    cairo_show_text = cif {pointer, pointer},
    cairo_stroke = cif {pointer},
    g_signal_connect_data =
      cif {pointer, pointer, pointer, pointer, pointer, sint},
    gtk_container_add = cif {pointer, pointer},
    gtk_drawing_area_new = cif {ret = pointer},
    gtk_init = cif {pointer, pointer},
    gtk_main = cif {},
    gtk_main_quit = cif {},
    gtk_widget_set_size_request = cif {pointer, sint, sint},
    gtk_widget_get_allocated_height = cif {ret = sint, pointer},
    gtk_widget_get_allocated_width = cif {ret = sint, pointer},
    gtk_widget_show = cif {pointer},
    gtk_window_new = cif {ret = pointer; sint},
    gtk_window_set_title = cif {pointer, pointer},
  })
end

do
  local math=math
  local _ENV=gtk

  gtk_init(nil, nil)
  local window = gtk_window_new(GTK_WINDOW_TOPLEVEL)
  gtk_window_set_title(window, "libFFI")
  g_signal_connect_data(window, "destroy", gtk_main_quit, nil, nil, 0)
  local darea = gtk_drawing_area_new()
  gtk_widget_set_size_request(darea, 200, 200)
  local redraw = ffi.closure(
    ffi.cif {ret=ffi.sint, ffi.pointer, ffi.pointer, ffi.pointer},
    function(widget, cr, data)
      local width = gtk_widget_get_allocated_width(widget);
      local height = gtk_widget_get_allocated_height(widget);
      local function P(i)
        local t = (0.4 * (i-1) + 0.5) * math.pi
        return 0.5*width * (math.cos(t) + 1),
               0.5*height * (1 - math.sin(t))
      end
      cairo_set_font_size(cr, 14)
      cairo_move_to(cr, 0, 20)
      cairo_show_text(cr, "Star of Mr. Zfy")
      cairo_set_source_rgb(cr, 1, 0, 0)
      cairo_move_to(cr, P(1))
      for i=3,9,2 do
        cairo_line_to(cr, P(i%5))
      end
      cairo_close_path(cr)
      cairo_stroke(cr)
      return false
    end)
  g_signal_connect_data(darea, "draw", redraw, nil, nil, 0)
  gtk_container_add(window, darea)
  gtk_widget_show(darea)
  gtk_widget_show(window)
  gtk_main()
end

-- vim: ts=2:sw=2:et
