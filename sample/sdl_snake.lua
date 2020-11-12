--Game logic

local game = {
  dx = 1,
  dy = 0,
  height = 25,
  width = 25,
  size = 0,
  state = {},
  body = {head = 0, tail = 1},
  stopped = false,
}

function game:make_food()
  local x, y, pos
  repeat
    x = math.random(0, self.width-1)
    y = math.random(0, self.height-1)
    pos = y * self.width + x
  until self.state[pos] == nil
  self.state[pos] = 'FOOD'
  self.food_pos = pos
end

function game:add_body(x, y)
  local pos = y * self.width + x
  local body = self.body

  body.head = body.head % (self.width * self.height) + 1
  body[body.head] = pos
  self.state[pos] = 'BODY'
  self.size = self.size + 1
end

function game:remove_tail()
  local body = self.body
  local pos = body[body.tail]
  local x, y = pos % self.width, pos // self.width

  body.tail = body.tail % (self.width * self.height) + 1
  self.state[pos] = nil
  self.size = self.size - 1
end

function game:next()
  local body = self.body
  local pos = body[body.head]
  local x, y = pos % self.width, pos // self.width
  local next_x, next_y = x + self.dx, y + self.dy
  local next_pos = next_y * self.width + next_x
  if next_x < 0 or next_x >= self.width or
     next_y < 0 or next_y >= self.height then
     print('hit border', next_x, next_y)
     self.stopped = 1
     return
  end
  if self.state[next_pos] == 'BODY' then
    print('hit body', next_x, next_y)
    self.stopped = 1
    return
  end
  local state = self.state[next_pos]
  self:add_body(next_x, next_y)
  if state == 'FOOD' then
    self:make_food()
  else
    self:remove_tail()
  end
  if self.size == self.width * self.height then
    print('filled entire space')
    self.stopped = 1
  end
end

function game:setdir(dx, dy)
  if self.dx == -dx or self.dy == -dy then
    return false
  end
  self.dx, self.dy = dx, dy
  return true
end

--Graphical interface

local ffi = require("ffi")
local sdl = require("sdl")
local assert=assert
local pcall=pcall
local error=error
local print=print

local _ENV=sdl

if SDL_Init(SDL_INIT_VIDEO) < 0 then
  return
end

local window = SDL_CreateWindow(
  "An SDL2 window",
  SDL_WINDOWPOS_UNDEFINED,
  SDL_WINDOWPOS_UNDEFINED,
  600,
  600,
  SDL_WINDOW_OPENGL|SDL_WINDOW_FULLSCREEN_DESKTOP)

local renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED)
local kScale = 100

local drawrect do
  local rect = ffi.alloc(SDL_Rect)
  rect.w = kScale
  rect.h = kScale
  function drawrect(x, y, r, g, b)
    rect.x = x * kScale
    rect.y = y * kScale
    SDL_SetRenderDrawColor(renderer, r, g, b, 255)
    SDL_RenderFillRect(renderer, rect)
  end
end

local drawline do
  local rect = ffi.alloc(SDL_Rect)
  function drawline(x1, y1, x2, y2)
    if x1 > x2 then x1, x2 = x2, x1 end
    if y1 > y2 then y1, y2 = y2, y1 end
    rect.x = (x1 + 0.1) * kScale
    rect.y = (y1 + 0.1) * kScale
    rect.w = (x2 - x1 + 0.8) * kScale
    rect.h = (y2 - y1 + 0.8) * kScale
    SDL_RenderFillRect(renderer, rect)
  end
end

local function redraw(game)
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255)
  SDL_RenderClear(renderer)
  SDL_SetRenderDrawColor(renderer, 32, 32, 32, 255)
  SDL_RenderFillRect(renderer, nil)
  local body, width, height = game.body, game.width, game.height
  local i = body.tail
  repeat
    local x, y = body[i] % width, body[i] // width
    local j = i % (width * height) + 1
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE)
    drawline(body[i] % width, body[i] // width,
             body[j] % width, body[j] // width)
    i = j
  until i == body.head
  SDL_SetRenderDrawColor(renderer, 0, 255, 0, SDL_ALPHA_OPAQUE)
  drawline(body[i] % width, body[i] // width,
           body[i] % width, body[i] // width)
  local x, y = game.food_pos % width, game.food_pos // width
  SDL_SetRenderDrawColor(renderer, 255, 0, 0, SDL_ALPHA_OPAQUE)
  drawline(x, y, x, y)
  SDL_RenderPresent(renderer)
end

-- init state
SDL_RenderSetLogicalSize(renderer, game.width * kScale, game.height * kScale)
game:add_body(0, 0)
game:add_body(1, 0)
game:add_body(2, 0)
game:make_food()

local window_event_handler = {
  [SDL_WINDOWEVENT_EXPOSED] = function(event)
    redraw(game)
  end
}
local event_handler = {
  [SDL_WINDOWEVENT] = function(event)
    local eventid = ffi.deref(event, ffi.field(SDL_WindowEvent, "event"))
    local handler = window_event_handler[eventid]
    if handler then return handler(event) end
  end,
  [SDL_KEYDOWN] = function(event)
    local scancode = ffi.deref(event, ffi.field(SDL_KeyboardEvent, "keysym", "scancode"))
    if scancode == SDL_SCANCODE_UP then
      game:setdir(0, -1)
    elseif scancode == SDL_SCANCODE_DOWN then
      game:setdir(0, 1)
    elseif scancode == SDL_SCANCODE_LEFT then
      game:setdir(-1, 0)
    elseif scancode == SDL_SCANCODE_RIGHT then
      game:setdir(1, 0)
    else
      return
    end
    game:next()
    redraw(game)
  end,
}

local ok, err = pcall(function()
  local event = ffi.alloc(ffi.char, 56)  -- sizeof (SDL_Event) == 56
  while true do
    local status = SDL_WaitEventTimeout(event, 500)
    if status ~= 0 then
      local etype = ffi.deref(event, ffi.field(SDL_CommonEvent, "type"))
      if etype == SDL_QUIT then break end
      local handler = event_handler[etype]
      if handler then handler(event) end
    else
      game:next()
      redraw(game)
    end
    if game.stopped then
      break
    end
  end
end)

SDL_DestroyRenderer(renderer)
SDL_DestroyWindow(window)
SDL_Quit()
if err ~= nil then
  error(err)
end

-- vim: ts=2:sw=2:et
