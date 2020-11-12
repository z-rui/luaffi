local ffi = require('ffi')
local _ENV=setmetatable({}, {__index=ffi})

--Constants
SDL_INIT_VIDEO = 0x00000020
SDL_WINDOWPOS_UNDEFINED = 0x1FFF0000
SDL_WINDOW_OPENGL = 0x00000002
SDL_WINDOW_FULLSCREEN = 0x00000001
SDL_WINDOW_FULLSCREEN_DESKTOP = ( SDL_WINDOW_FULLSCREEN | 0x00001000 )
SDL_WINDOW_RESIZABLE = 0x00000020

SDL_RENDERER_SOFTWARE = 1
SDL_RENDERER_ACCELERATED = 2
SDL_RENDERER_PRESENTVSYNC = 4
SDL_RENDERER_TARGETTEXTURE = 8

SDL_ALPHA_OPAQUE = 255

SDL_QUIT = 0x100
SDL_WINDOWEVENT = 0x200
SDL_KEYDOWN = 0x300

SDL_WINDOWEVENT_EXPOSED = 3

SDL_SCANCODE_RIGHT = 79
SDL_SCANCODE_LEFT = 80
SDL_SCANCODE_DOWN = 81
SDL_SCANCODE_UP = 82

--Typedefs
SDL_Keycode = sint32
SDL_Scancode = sint32  -- enum

--Structs
SDL_Rect = struct {
  sint, "x";
  sint, "y";
  sint, "w";
  sint, "h";
}
SDL_CommonEvent = struct {
  uint32, "type";
  uint32, "timestamp";
}
SDL_WindowEvent = struct {
  uint32, "type";
  uint32, "timestamp";
  uint32, "windowID";
  uint8, "event";
  uint8; uint8; uint8;  --padding
  sint32, "data1";
  sint32, "data2";
}
SDL_Keysym = struct {
  SDL_Scancode, "scancode";
  SDL_Keycode, "sym";
  uint16, "mod";
  uint32, "unused";
}
SDL_KeyboardEvent = struct {
  uint32, "type";
  uint32, "timestamp";
  uint32, "windowID";
  uint8, "state";
  uint8, "repeat";
  uint8; uint8;  --padding
  SDL_Keysym, "keysym";
}

--Functions
SDL_Init = cif { ret = sint, uint32 }
SDL_CreateWindow = cif { ret = pointer; pointer, sint, sint, sint, sint, uint32 }
SDL_CreateRenderer = cif { ret = pointer; pointer, sint, uint32 }
SDL_RenderSetScale = cif { ret = sint; pointer, float, float }
SDL_RenderSetLogicalSize = cif { ret = sint; pointer, sint, sint }
SDL_RenderFillRect = cif { pointer, pointer }
SDL_RenderClear = cif { pointer }
SDL_RenderPresent = cif { pointer }
SDL_SetRenderDrawColor = cif { ret = sint; pointer, uint8, uint8, uint8, uint8 }
SDL_Delay = cif { uint32 }
SDL_DestroyRenderer = cif { pointer }
SDL_DestroyWindow = cif { pointer }
SDL_Quit = cif {}
SDL_WaitEventTimeout = cif { ret = sint; pointer, sint }

return ffi.loadlib('libSDL2.so', _ENV)

-- vim: ts=2:sw=2:et
