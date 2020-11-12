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

function game:add_body(pos)
  local body = self.body

  body.head = body.head % (self.width * self.height) + 1
  body[body.head] = pos
  self.state[pos] = 'BODY'
  self.size = self.size + 1
end

function game:remove_tail()
  local body = self.body
  local pos = body[body.tail]

  body.tail = body.tail % (self.width * self.height) + 1
  self.state[pos] = nil
  self.size = self.size - 1
end

function game:next()
  local body = self.body
  local pos = body[body.head]
  local width, height = self.width, self.height
  local x, y = pos % width, pos // width
  local next_x, next_y = x + self.dx, y + self.dy
  local next_pos = next_y * width + next_x
  local state = self.state[next_pos]
  if next_x < 0 or next_x >= width or
     next_y < 0 or next_y >= height or
     state == 'BODY' then
     self.stopped = true
     return
  end
  if state ~= 'FOOD' then
    self:remove_tail()
  end
  self:add_body(next_pos)
  if self.size == width * height then
    self.stopped = true
    return
  end
  if state == 'FOOD' then
    self:make_food()
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
SDL_RenderSetLogicalSize(renderer, game.width * 100, game.height * 100)

local function drawline(x1, y1, x2, y2)
  if x1 > x2 then x1, x2 = x2, x1 end
  if y1 > y2 then y1, y2 = y2, y1 end
  local rect = ffi.alloc(SDL_Rect)
  rect.x = x1 * 100 + 10
  rect.y = y1 * 100 + 10
  rect.w = (x2 - x1) * 100 + 80
  rect.h = (y2 - y1) * 100 + 80
  SDL_RenderFillRect(renderer, rect)
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
game:add_body(0, 0)
game:add_body(1, 0)
game:add_body(2, 0)
game:make_food()

local event_handler = {
  [SDL_WINDOWEVENT] = function(event)
    local eventid = ffi.deref(event, ffi.field(SDL_WindowEvent, "event"))
    if eventid == SDL_WINDOWEVENT_EXPOSED then
      redraw(game)
    end
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
  -- main loop
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
